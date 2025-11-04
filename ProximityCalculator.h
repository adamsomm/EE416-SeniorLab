#pragma once

// Include our shared geometry definitions
#include "Geometry.h"

class ProximityCalculator {
public:
    // --- Public Interface ---

    // Constructor (initializes constants)
    ProximityCalculator();

    // The main public function.
    // Returns 'true' if the tag is estimated to be in the room.
    bool isInRoom(double rssi1, double rssi2);
    
    double distanceToRssi(double d);
    double rssiToDistance(double rssi);
    double calculate_error_at_point(Point guess_pos, double r1, double r2);
    bool isPointInRoom(Point point);




private:
    // --- Private Helper Functions ---

    // Converts RSSI (dBm) to a distance (meters)
    // double rssiToDistance(double rssi);

    // Checks if a given (x, y) point is inside the room boundaries
    // bool isPointInRoom(Point point);

    // This is the "Error Function"
    // It calculates the "wrongness" of a guess.
    // double calculate_error_at_point(Point guess_pos, double r1, double r2);

    // --- Private Member Variables (Constants) ---
    const Point ANCHOR_1_COORDS;
    const Point ANCHOR_2_COORDS;
    const Rect  ROOM_BOUNDS;
    const double RSSI_AT_ONE_METER;
    const double N_PATH_LOSS_DIVISOR;
};
