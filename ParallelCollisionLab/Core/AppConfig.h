#pragma once

#include "Common.h"

//=============================================================================
// Application & Simulation Configuration Constants
//=============================================================================
namespace Config
{
    // Window settings
    static const int            WINDOW_WIDTH  = 768;
    static const int            WINDOW_HEIGHT = 1024;
    static const wchar_t* const WINDOW_TITLE  = L"Parallel Collision Lab";

    // Simulation Domain & Physics
    static const float          BOX_HALF_SIZE            = 2.0f;
    static const float          DEFAULT_DAMPING_FACTOR   = 0.992f; // Velocity retention rate at 60 FPS (~0.61 after 1s)
    static const float          SLEEP_VELOCITY_THRESHOLD = 0.06f;  // Speed below which sphere enters sleep timer
    static const float          SLEEP_TIME_REQUIRED      = 0.25f;  // Seconds below threshold to enter dormant sleep state
    static const float          COLOR_LERP_SPEED         = 6.0f;   // Speed factor for smooth color transitions
    static const FVector4       COLOR_SLEEPING(0.35f, 0.36f, 0.40f, 1.0f); // Slate gray for dormant/sleeping spheres

    // Camera Defaults
    static const FVector3       CAMERA_DEFAULT_EYE(0.0f, 0.0f, -10.0f);
    static const FVector3       CAMERA_DEFAULT_AT(0.0f, 0.0f, 0.0f);
    static const FVector3       CAMERA_DEFAULT_UP(0.0f, 1.0f, 0.0f);
    static const float          CAMERA_FOV_DEG = 50.0f;
    static const float          CAMERA_NEAR    = 0.1f;
    static const float          CAMERA_FAR     = 100.0f;

    // Cornell Box Wall Colors
    static const FVector4       WALL_LEFT_COLOR(0.630f, 0.065f, 0.050f, 1.0f);   // Red
    static const FVector4       WALL_RIGHT_COLOR(0.137f, 0.447f, 0.090f, 1.0f);  // Green
    static const FVector4       WALL_OTHER_COLOR(0.725f, 0.710f, 0.680f, 1.0f);  // Off-white

    // Uniform Grid Visualization Colors
    static const FVector4       GRID_WALL_COLOR(0.18f, 0.52f, 0.92f, 0.85f);     // Blueprint Royal Blue
    static const FVector4       GRID_ACTIVE_COLOR(0.0f, 0.95f, 1.0f, 1.0f);     // Bright Cyan Wireframe

    // HUD & Profiling
    static const double         HUD_REFRESH_INTERVAL = 0.25;

    // Network Window Layout Configuration
    static const int            SERVER_WINDOW_POS_X  = 10;
    static const int            SERVER_WINDOW_POS_Y  = 10;
    static const int            CLIENT_WINDOW_POS_X  = 765;
    static const int            CLIENT_WINDOW_POS_Y  = 10;
}
