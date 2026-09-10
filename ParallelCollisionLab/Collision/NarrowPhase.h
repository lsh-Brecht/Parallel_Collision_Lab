#pragma once

#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// NarrowPhase - Precise sphere-sphere collision detection
//=============================================================================

/**
 * Tests collision between two spheres.
 * If colliding, returns true and fills outManifold with contact normal (B -> A)
 * and penetration depth.
 */
inline bool CheckSphereSphere(
    const FSphere& a,
    const FSphere& b,
    int indexA,
    int indexB,
    FCollisionManifold& outManifold)
{
    FVector3 diff = a.Center - b.Center; // Vector pointing from B to A
    float distSq = diff.LengthSq();
    float radiusSum = a.Radius + b.Radius;

    // Fast rejection: check squared distance against squared radius sum
    if (distSq >= radiusSum * radiusSum)
    {
        return false;
    }

    float dist = sqrtf(distSq);

    // Calculate unit normal pointing from B to A
    FVector3 normal;
    if (dist > 1e-6f)
    {
        normal = diff * (1.0f / dist);
    }
    else
    {
        // Degenerate case: spheres share the same center
        normal = FVector3(1.0f, 0.0f, 0.0f);
        dist = 0.0f;
    }

    outManifold.IndexA      = indexA;
    outManifold.IndexB      = indexB;
    outManifold.Normal      = normal;
    outManifold.Penetration = radiusSum - dist;

    return true;
}
