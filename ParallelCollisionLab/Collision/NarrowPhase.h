#pragma once

#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// NarrowPhase - Precise sphere-sphere collision detection
//=============================================================================
inline bool CheckSphereSphere(
    const FSphere& a,
    const FSphere& b,
    int indexA,
    int indexB,
    FCollisionManifold& outManifold)
{
    FVector3 diff = a.Center - b.Center;
    float distSq = diff.LengthSq();
    float radiusSum = a.Radius + b.Radius;

    if (distSq >= radiusSum * radiusSum)
    {
        return false;
    }

    float dist = sqrtf(distSq);
    FVector3 normal = (dist > 1e-6f) ? diff * (1.0f / dist) : FVector3(1.0f, 0.0f, 0.0f);

    outManifold.IndexA      = indexA;
    outManifold.IndexB      = indexB;
    outManifold.Normal      = normal;
    outManifold.Penetration = radiusSum - dist;

    return true;
}
