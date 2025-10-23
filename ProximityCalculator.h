#pragma once

// Include the optimization library header
#include <nlopt.hpp> // Ensure the NLopt library is installed and accessible
// Include our shared geometry definitions
#include "Geometry.h"

// This struct is used to pass data to the NLopt C-style callback
struct OptimizationData {
    // A pointer back to the class instance to access its members
    class ProximityCalculator* self; 
    double r1; // The noisy distance from anchor 1
    double r2; // The noisy distance from anchor 2
};

class ProximityCalculator {
public:
    // --- Public Interface ---

    // Constructor (initializes constants)
    ProximityCalculator();

    // The main public function.
    // Returns 'true' if the tag is estimated to be in the room.
    bool isInRoom(double rssi1, double rssi2);

private:
    // --- Private Helper Functions ---

    // Converts RSSI (dBm) to a distance (meters)
    double rssiToDistance(double rssi);

    // Checks if a given (x, y) point is inside the room boundaries
    bool isPointInRoom(Point point);

    // This is the "Error Function"
    // It's called by the Nlopt wrapper to calculate the
    // "wrongness" of a guess.
    double calculate_error_at_point(Point guess_pos, double r1, double r2);

    // --- Private Member Variables (Constants) ---
    const Point ANCHOR_1_COORDS;
    const Point ANCHOR_2_COORDS;
    const Rect  ROOM_BOUNDS;
    const double RSSI_AT_ONE_METER;
    const double N_PATH_LOSS_DIVISOR;

    // --- Static Solver Wrapper ---
    // NLopt requires a static C-style function for its callback.
    // This function unpacks the data and calls our class member function.
    static double nlopt_objective_function(unsigned n, const double* x, double* grad, void* data);
};
