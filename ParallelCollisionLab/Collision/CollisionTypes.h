#pragma once

#include "../Core/Common.h"

struct FCollisionManifold
{
    int      IndexA      = -1;
    int      IndexB      = -1;
    FVector3 Normal      = FVector3(0.0f, 0.0f, 0.0f);
    float    Penetration = 0.0f;
};

struct FCollisionStats
{
    double   BroadPhaseTimeMs     = 0.0;
    double   NarrowPhaseTimeMs    = 0.0;
    double   TotalSolveTimeMs     = 0.0;
    uint64_t CandidatePairCount   = 0;
    uint64_t ActualCollisionCount = 0;
};
