#pragma once

#include <algorithm>
#include <cmath>
#include "../../Core/Common.h"

struct FAABB
{
    FVector3 Min;
    FVector3 Max;

    FAABB()
        : Min(1e30f, 1e30f, 1e30f), Max(-1e30f, -1e30f, -1e30f)
    {
    }

    FAABB(const FVector3& inMin, const FVector3& inMax)
        : Min(inMin), Max(inMax)
    {
    }

    static FAABB FromSphere(const FVector3& center, float radius)
    {
        return FAABB(
            FVector3(center.x - radius, center.y - radius, center.z - radius),
            FVector3(center.x + radius, center.y + radius, center.z + radius)
        );
    }

    static FAABB FromPoint(const FVector3& p)
    {
        return FAABB(p, p);
    }

    void ExpandBy(const FAABB& other)
    {
        Min.x = (std::min)(Min.x, other.Min.x);
        Min.y = (std::min)(Min.y, other.Min.y);
        Min.z = (std::min)(Min.z, other.Min.z);

        Max.x = (std::max)(Max.x, other.Max.x);
        Max.y = (std::max)(Max.y, other.Max.y);
        Max.z = (std::max)(Max.z, other.Max.z);
    }

    void ExpandBy(const FVector3& p)
    {
        Min.x = (std::min)(Min.x, p.x);
        Min.y = (std::min)(Min.y, p.y);
        Min.z = (std::min)(Min.z, p.z);

        Max.x = (std::max)(Max.x, p.x);
        Max.y = (std::max)(Max.y, p.y);
        Max.z = (std::max)(Max.z, p.z);
    }

    FAABB Union(const FAABB& other) const
    {
        return FAABB(
            FVector3((std::min)(Min.x, other.Min.x), (std::min)(Min.y, other.Min.y), (std::min)(Min.z, other.Min.z)),
            FVector3((std::max)(Max.x, other.Max.x), (std::max)(Max.y, other.Max.y), (std::max)(Max.z, other.Max.z))
        );
    }

    bool Intersects(const FAABB& other) const
    {
        return (Min.x <= other.Max.x && Max.x >= other.Min.x) &&
               (Min.y <= other.Max.y && Max.y >= other.Min.y) &&
               (Min.z <= other.Max.z && Max.z >= other.Min.z);
    }

    FVector3 GetCenter() const
    {
        return FVector3(
            (Min.x + Max.x) * 0.5f,
            (Min.y + Max.y) * 0.5f,
            (Min.z + Max.z) * 0.5f
        );
    }

    FVector3 GetExtent() const
    {
        return FVector3(
            Max.x - Min.x,
            Max.y - Min.y,
            Max.z - Min.z
        );
    }

    float GetSurfaceArea() const
    {
        FVector3 d = GetExtent();
        return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
    }

    int GetLongestAxis() const
    {
        FVector3 d = GetExtent();
        if (d.x >= d.y && d.x >= d.z) return 0; // X
        if (d.y >= d.z)               return 1; // Y
        return 2;                               // Z
    }

    bool IsValid() const
    {
        return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
    }
};
