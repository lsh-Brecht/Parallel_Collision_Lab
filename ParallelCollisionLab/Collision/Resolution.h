#pragma once

#include <vector>
#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// CollisionResolution - Impulse and position correction solver
//=============================================================================

/**
 * Resolves collisions using elastic collision impulse and position correction.
 * Designed to run sequentially on a single thread to guarantee deterministic,
 * race-free state updates.
 */
inline void ResolveCollisions(
    std::vector<FSphere>& spheres,
    const std::vector<FCollisionManifold>& manifolds)
{
    for (const FCollisionManifold& m : manifolds)
    {
        FSphere& a = spheres[m.IndexA];
        FSphere& b = spheres[m.IndexB];

        // Relative velocity along contact normal: (vA - vB) . Normal
        // Normal points from B to A
        FVector3 relVel = a.Velocity - b.Velocity;
        float sepVel = relVel.Dot(m.Normal);

        // If spheres are already moving away from each other, skip impulse and position correction.
        // This prevents double-bounce artifacts when spheres remain overlapping across ticks.
        if (sepVel > 0.0f)
        {
            continue;
        }

        // 1. Elastic collision velocity impulse
        float m1 = a.Mass;
        float m2 = b.Mass;
        float massSum = m1 + m2;
        if (massSum > 0.0f)
        {
            // Delta vA = -Normal * (2 * m2 / (m1 + m2)) * sepVel
            // Delta vB =  Normal * (2 * m1 / (m1 + m2)) * sepVel
            FVector3 impulseA = m.Normal * (-((2.0f * m2 / massSum) * sepVel));
            FVector3 impulseB = m.Normal * (((2.0f * m1 / massSum) * sepVel));

            a.Velocity += impulseA;
            b.Velocity += impulseB;
        }

        // 2. Position correction (penetration resolution)
        if (m.Penetration > 0.0f)
        {
            float halfOverlap = m.Penetration * 0.5f;
            FVector3 corr = m.Normal * halfOverlap;
            a.Center += corr;
            b.Center -= corr;
        }
    }
}
