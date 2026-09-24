#pragma once

#include <vector>
#include "../Core/Sphere.h"
#include "CollisionTypes.h"

//=============================================================================
// CollisionResolution - Impulse and position correction solver
//=============================================================================
inline void ResolveCollisions(
    std::vector<FSphere>& Spheres,
    const std::vector<FCollisionManifold>& Manifolds)
{
    for (const FCollisionManifold& Manifold : Manifolds)
    {
        FSphere& SphereA = Spheres[Manifold.IndexA];
        FSphere& SphereB = Spheres[Manifold.IndexB];

        FVector3 RelVel = SphereA.Velocity - SphereB.Velocity;
        float SepVel = RelVel.Dot(Manifold.Normal);

        if (SepVel > 0.0f)
        {
            continue;
        }

        // Impulse
        float MassA = SphereA.Mass;
        float MassB = SphereB.Mass;
        float MassSum = MassA + MassB;
        if (MassSum > 0.0f)
        {
            FVector3 ImpulseA = Manifold.Normal * (-((2.0f * MassB / MassSum) * SepVel));
            FVector3 ImpulseB = Manifold.Normal * (((2.0f * MassA / MassSum) * SepVel));

            SphereA.Velocity += ImpulseA;
            SphereB.Velocity += ImpulseB;

            if (ImpulseA.LengthSq() > 0.0001f) SphereA.WakeUp();
            if (ImpulseB.LengthSq() > 0.0001f) SphereB.WakeUp();
        }

        // Position correction
        if (Manifold.Penetration > 0.0f)
        {
            float HalfOverlap = Manifold.Penetration * 0.5f;
            FVector3 Corr = Manifold.Normal * HalfOverlap;
            SphereA.Center += Corr;
            SphereB.Center -= Corr;
        }
    }
}
