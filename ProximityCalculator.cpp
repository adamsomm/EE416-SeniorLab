#include "ProximityCalculator.h"
#include <cmath>
#include <vector>
#include <iostream>

// --- Constructor ---
// Initializes all the constant values for the class instance.
ProximityCalculator::ProximityCalculator() :
    ANCHOR_1_COORDS{0.0, 5.0},
    ANCHOR_2_COORDS{8.0, 8.0},
    ROOM_BOUNDS{0, 10, 0, 10},
    RSSI_AT_ONE_METER(-59.0),
    N_PATH_LOSS_DIVISOR(20.0)
{
    // Constructor body is empty as all initialization is done above
}

// --- Helper Function: RSSI to Distance ---
double ProximityCalculator::rssiToDistance(double rssi) {
    return pow(10, (RSSI_AT_ONE_METER - rssi) / N_PATH_LOSS_DIVISOR);
}

// --- Helper Function: Check if point is in room ---
bool ProximityCalculator::isPointInRoom(Point point) {
    return (point.x >= ROOM_BOUNDS.min_x &&
            point.x <= ROOM_BOUNDS.max_x &&
            point.y >= ROOM_BOUNDS.min_y &&
            point.y <= ROOM_BOUNDS.max_y);
}

// --- Helper Function: The Error Function ---
// Calculates the Sum of Squared Errors for a given guess.
double ProximityCalculator::calculate_error_at_point(Point guess_pos, double r1, double r2) {
    
    // 1. Calculate geometric distance from the guess to each anchor
    double dist_to_anchor1 = sqrt(pow(guess_pos.x - ANCHOR_1_COORDS.x, 2) + 
                                  pow(guess_pos.y - ANCHOR_1_COORDS.y, 2));
    
    double dist_to_anchor2 = sqrt(pow(guess_pos.x - ANCHOR_2_COORDS.x, 2) + 
                                  pow(guess_pos.y - ANCHOR_2_COORDS.y, 2));

    // 2. Calculate the error (difference) for each anchor
    double error1 = dist_to_anchor1 - r1;
    double error2 = dist_to_anchor2 - r2;

    // 3. Return the Sum of Squared Errors
    return (error1 * error1) + (error2 * error2);
}


// --- Static Solver Wrapper ---
// This C-style function is what NLopt actually calls.
double ProximityCalculator::nlopt_objective_function(unsigned n, const double* x, double* grad, void* data) {
    // 'n' is the number of dimensions (2 for us: x, y)
    // 'x' is the array [x_guess, y_guess] from the solver
    // 'grad' is for the gradient (we don't use it for this algorithm)
    // 'data' is our custom void* pointer

    // 1. Re-cast the void* data back to our struct
    OptimizationData* opt_data = static_cast<OptimizationData*>(data);

    // 2. Create a Point from the solver's guess
    Point guess_pos = {x[0], x[1]};

    // 3. Use the 'self' pointer to call the non-static member function
    return opt_data->self->calculate_error_at_point(guess_pos, opt_data->r1, opt_data->r2);
}


// --- Main Public Function ---
bool ProximityCalculator::isInRoom(double rssi1, double rssi2) {
    
    // 1. Convert RSSI to (noisy) distance estimates
    double r1 = rssiToDistance(rssi1);
    double r2 = rssiToDistance(rssi2);

    // 2. Prepare the data packet for the solver
    OptimizationData data;
    data.self = this; // Pass a pointer to this class instance
    data.r1 = r1;
    data.r2 = r2;

    // 3. Initialize the NLopt solver
    // (n=2 means we are solving for 2 variables, x and y)
    // LN_BOBYQA is a good, standard algorithm that doesn't need gradients
    nlopt::opt solver(nlopt::LN_BOBYQA, 2);

    // 4. Set the "objective function" (our error function)
    solver.set_min_objective(ProximityCalculator::nlopt_objective_function, &data);

    // 5. Set the search boundaries (constrain search to the room)
    /*std::vector<double> lower_bounds = {ROOM_BOUNDS.min_x, ROOM_BOUNDS.min_y};
    std::vector<double> upper_bounds = {ROOM_BOUNDS.max_x, ROOM_BOUNDS.max_y};
    solver.set_lower_bounds(lower_bounds);
    solver.set_upper_bounds(upper_bounds);*/

    // 6. Set a stop condition (e.g., a timeout or tolerance)
    solver.set_xtol_rel(1e-4); // Stop when results change by < 0.01%

    // 7. Set the initial guess (a 2D vector [x, y])
    // The center of the room is a safe bet.
    std::vector<double> guess = {
        (ROOM_BOUNDS.min_x + ROOM_BOUNDS.max_x) / 2.0,
        (ROOM_BOUNDS.min_y + ROOM_BOUNDS.max_y) / 2.0
    };

    double min_error_found; // This will be filled by the solver

    // 8. RUN THE SOLVER!
    try {
        nlopt::result result = solver.optimize(guess, min_error_found);

        // 'guess' vector is now overwritten with the best-fit solution
        Point best_fit_position = {guess[0], guess[1]};
        
        // 9. Check if this single point is in the room
        return isPointInRoom(best_fit_position);

    } catch (const std::exception& e) {
        // The solver failed for some reason
        std::cerr << "Solver failed: " << e.what() << std::endl;
        return false;
    }
}
