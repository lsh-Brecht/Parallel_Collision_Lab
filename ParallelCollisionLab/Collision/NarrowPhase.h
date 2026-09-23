#pragma once

#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// NarrowPhase - Precise sphere-sphere collision detection
//=============================================================================
inline bool CheckSphereSphere(
    const FSphere& SphereA,
    const FSphere& SphereB,
    int IndexA,
    int IndexB,
    FCollisionManifold& OutManifold)
{
    FVector3 Diff = SphereA.Center - SphereB.Center;
    float DistSq = Diff.LengthSq();
    float RadiusSum = SphereA.Radius + SphereB.Radius;

    if (DistSq >= RadiusSum * RadiusSum)
    {
        return false;
    }

    float Dist = sqrtf(DistSq);
    FVector3 Normal = (Dist > 1e-6f) ? Diff * (1.0f / Dist) : FVector3(1.0f, 0.0f, 0.0f);

    OutManifold.IndexA      = IndexA;
    OutManifold.IndexB      = IndexB;
    OutManifold.Normal      = Normal;
    OutManifold.Penetration = RadiusSum - Dist;

    return true;
}
