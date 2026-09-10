#pragma once

#include "../Core/Common.h"

//=============================================================================
// FCollisionPair - Broad Phase output: candidate sphere indices
//=============================================================================
struct FCollisionPair
{
    int IndexA = -1;
    int IndexB = -1;
};

//=============================================================================
// FCollisionManifold - Narrow Phase output: contact geometry data
//=============================================================================
struct FCollisionManifold
{
    int      IndexA      = -1;
    int      IndexB      = -1;
    FVector3 Normal      = FVector3(0.0f, 0.0f, 0.0f); // Unit normal pointing from B to A
    float    Penetration = 0.0f;                       // Overlap depth (> 0 when colliding)
};
