#pragma once

#include <vector>
#include "../../Core/Common.h"
#include "../../Core/Sphere.h"

//=============================================================================
// IUniformGridVisualizer - Interface for 3D uniform grid lattice & cell visualization
//=============================================================================
class IUniformGridVisualizer
{
public:
    virtual ~IUniformGridVisualizer() = default;
    virtual void BuildGrid(const std::vector<FSphere>& Spheres) = 0;
    virtual void GenerateActiveCellLines(std::vector<FVertexSimple>& OutLines) const = 0;
    virtual void GenerateFloorAndWallGridLines(std::vector<FVertexSimple>& OutLines, float InBoxHalfSize = 2.0f) const = 0;
};
