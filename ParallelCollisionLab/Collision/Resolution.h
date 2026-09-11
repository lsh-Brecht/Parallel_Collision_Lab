#pragma once

#include <vector>
#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// CollisionResolution - Impulse and position correction solver
//=============================================================================
inline void ResolveCollisions(
    std::vector<FSphere>& spheres,
    const std::vector<FCollisionManifold>& manifolds)
{
    for (const FCollisionManifold& m : manifolds)
    {
        FSphere& a = spheres[m.IndexA];
        FSphere& b = spheres[m.IndexB];

        FVector3 relVel = a.Velocity - b.Velocity;
        float sepVel = relVel.Dot(m.Normal);

        if (sepVel > 0.0f)
        {
            continue;
        }

        // Impulse
        float m1 = a.Mass;
        float m2 = b.Mass;
        float massSum = m1 + m2;
        if (massSum > 0.0f)
        {
            FVector3 impulseA = m.Normal * (-((2.0f * m2 / massSum) * sepVel));
            FVector3 impulseB = m.Normal * (((2.0f * m1 / massSum) * sepVel));

            a.Velocity += impulseA;
            b.Velocity += impulseB;
        }

        // Position correction
        if (m.Penetration > 0.0f)
        {
            float halfOverlap = m.Penetration * 0.5f;
            FVector3 corr = m.Normal * halfOverlap;
            a.Center += corr;
            b.Center -= corr;
        }
    }
}
