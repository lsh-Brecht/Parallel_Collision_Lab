#pragma once

#include <cstdint>
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

//=============================================================================
// FCollisionStats - Profiling and pipeline metrics per simulation tick
//=============================================================================
struct FCollisionStats
{
    float    BroadPhaseTimeMs     = 0.0f;
    float    NarrowPhaseTimeMs    = 0.0f;
    float    ResolutionTimeMs     = 0.0f;
    float    TotalSolveTimeMs     = 0.0f;
    uint64_t CandidatePairCount   = 0;
    uint64_t ActualCollisionCount = 0;
};
