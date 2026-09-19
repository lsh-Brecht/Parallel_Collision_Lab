#pragma once

#include "AABB.h"

//=============================================================================
// Memory Layout (Total: 32 bytes):
// - Bounds:     24 bytes (Min 12B + Max 12B)
// - LeftChild:   4 bytes (If leaf: -1; If internal: index of left child node)
// - RightChild:  4 bytes (If leaf: SphereIndex; If internal: index of right child node)
//=============================================================================
struct FBVHNode
{
    FAABB Bounds;
    int   LeftChild  = -1;
    int   RightChild = -1;

    bool IsLeaf() const
    {
        return LeftChild == -1;
    }

    int GetSphereIndex() const
    {
        return RightChild;
    }

    void SetLeaf(int sphereIndex, const FAABB& bounds)
    {
        Bounds     = bounds;
        LeftChild  = -1;
        RightChild = sphereIndex;
    }

    void SetInternal(int leftChildIdx, int rightChildIdx, const FAABB& bounds)
    {
        Bounds     = bounds;
        LeftChild  = leftChildIdx;
        RightChild = rightChildIdx;
    }
};

static_assert(sizeof(FBVHNode) == 32, "FBVHNode must be exactly 32 bytes for optimal L1/L2 cache alignment!");
