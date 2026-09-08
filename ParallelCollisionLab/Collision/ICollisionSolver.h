#pragma once

#include <vector>
#include "../Core/Sphere.h"

//=============================================================================
// ICollisionSolver - Interface for sphere-sphere collision algorithms
//=============================================================================
class ICollisionSolver
{
public:
    virtual ~ICollisionSolver() = default;

    virtual void Solve(std::vector<FSphere>& spheres) = 0;

    virtual const wchar_t* GetName() const = 0;
};
