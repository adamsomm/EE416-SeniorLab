#include "ProximityCalculator.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <limits>    // Required for std::numeric_limits
#include <algorithm> // Required for std::max and std::min

// --- Constructor ---
// Initializes all the constant values for the class instance.
ProximityCalculator::ProximityCalculator() :
    ANCHOR_1_COORDS{0.0, 5.0},
    ANCHOR_2_COORDS{10.0, 5.0},
    ROOM_BOUNDS{0, 10, 0, 10},
    RSSI_AT_ONE_METER(-59.0),
    N_PATH_LOSS_DIVISOR(2.0) // This corresponds to N=2 in the formula
{
    // Constructor body is empty as all initialization is done above
}

// --- Helper Function: RSSI to Distance ---
double ProximityCalculator::rssiToDistance(double rssi) {
    // Formula: distance = 10^((RSSI_at_1m - RSSI) / (10 * N))
    return pow(10, (RSSI_AT_ONE_METER - rssi) / (10 * N_PATH_LOSS_DIVISOR));
}

// --- Helper Function: Distance to RSSI ---
double ProximityCalculator::distanceToRssi(double d) {
    // Formula: RSSI = RSSI_at_1m - 10 * N * log10(distance)
    return RSSI_AT_ONE_METER - (10 * N_PATH_LOSS_DIVISOR * std::log10(d));
}

// --- Helper Function: Check if point is in room ---
bool ProximityCalculator::isPointInRoom(Point point) {
    const double EPSILON = 1e-9;
    
    return (point.x >= ROOM_BOUNDS.min_x - EPSILON &&
            point.x <= ROOM_BOUNDS.max_x + EPSILON &&
            point.y >= ROOM_BOUNDS.min_y - EPSILON &&
            point.y <= ROOM_BOUNDS.max_y + EPSILON);
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

// --- Main Public Function (Grid Search Solver) ---
bool ProximityCalculator::isInRoom(double rssi1, double rssi2) {

    // Convert RSSI to (noisy) distance estimates
    double r1 = rssiToDistance(rssi1);
    double r2 = rssiToDistance(rssi2);

    // --- Grid Search Parameters ---
    const double COARSE_STEP = 0.5; // Coarse step: check every 0.5 meters
    const double FINE_STEP = 0.05;  // Fine step: check every 0.05 meters (5 cm)
    const double FINE_RANGE = 1.0;  // Search +/- 1.0m around the best coarse point
    
    // We will search *outside* the room to find the true best-fit point.
    const double SEARCH_MARGIN = 5.0; // Search 5m beyond the room boundaries
    
    // Initial best result is max possible error (a very large number)
    double min_error = std::numeric_limits<double>::max();
    Point best_fit_position = {4.0, 5.0};  

    // Define wider search bounds ***
    const double search_min_x = ROOM_BOUNDS.min_x - SEARCH_MARGIN;
    const double search_max_x = ROOM_BOUNDS.max_x + SEARCH_MARGIN;
    const double search_min_y = ROOM_BOUNDS.min_y - SEARCH_MARGIN;
    const double search_max_y = ROOM_BOUNDS.max_y + SEARCH_MARGIN;
    
    // STEP 1: COARSE GRID SEARCH
    Point coarse_best_point = best_fit_position;

    // Use wider search bounds 
    for (double x = search_min_x; x <= search_max_x; x += COARSE_STEP) {
        for (double y = search_min_y; y <= search_max_y; y += COARSE_STEP) {
            
            Point current_point = {x, y};
            double current_error = calculate_error_at_point(current_point, r1, r2);
            
            if (current_error < min_error) {
                min_error = current_error;
                coarse_best_point = current_point;
            }
        }
    }

    // STEP 2: FINE GRID SEARCH (Local Refinement)
    // Define the local search bounds based on the coarse result
    double fine_min_x = std::max(search_min_x, coarse_best_point.x - FINE_RANGE);
    double fine_max_x = std::min(search_max_x, coarse_best_point.x + FINE_RANGE);
    double fine_min_y = std::max(search_min_y, coarse_best_point.y - FINE_RANGE);
    double fine_max_y = std::min(search_max_y, coarse_best_point.y + FINE_RANGE);
    
    min_error = std::numeric_limits<double>::max();
    
    for (double x = fine_min_x; x <= fine_max_x; x += FINE_STEP) {
        for (double y = fine_min_y; y <= fine_max_y; y += FINE_STEP) {
            
            Point current_point = {x, y};
            double current_error = calculate_error_at_point(current_point, r1, r2);
            
            if (current_error < min_error) {
                min_error = current_error;
                best_fit_position = current_point;
            }
        }
    }
    
    return isPointInRoom(best_fit_position);
}

