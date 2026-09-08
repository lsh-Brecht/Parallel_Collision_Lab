#pragma once

#include "../ICollisionSolver.h"

//=============================================================================
// NestedLoopSolver - Naive O(n^2) nested loop collision detection
//=============================================================================
class NestedLoopSolver : public ICollisionSolver
{
public:
    void Solve(std::vector<FSphere>& spheres) override
    {
        for (size_t i = 0; i < spheres.size(); ++i)
        {
            for (size_t j = i + 1; j < spheres.size(); ++j)
            {
                if (spheres[i].CollisionCheck(spheres[j]) < 0.0f)
                    spheres[i].HandleCollision(spheres[j]);
            }
        }
    }

    const wchar_t* GetName() const override { return L"NestedLoop"; }
};
