#pragma once

#include <algorithm>
#include "../../Core/Common.h"

struct FAABB
{
    FVector3 Min;
    FVector3 Max;

    FAABB()
        : Min(1e30f, 1e30f, 1e30f), Max(-1e30f, -1e30f, -1e30f)
    {
    }

    FAABB(const FVector3& InMin, const FVector3& InMax)
        : Min(InMin), Max(InMax)
    {
    }

    static FAABB FromSphere(const FVector3& Center, float Radius)
    {
        return FAABB(
            FVector3(Center.x - Radius, Center.y - Radius, Center.z - Radius),
            FVector3(Center.x + Radius, Center.y + Radius, Center.z + Radius)
        );
    }

    static FAABB FromPoint(const FVector3& Point)
    {
        return FAABB(Point, Point);
    }

    void ExpandBy(const FAABB& Other)
    {
        Min.x = (std::min)(Min.x, Other.Min.x);
        Min.y = (std::min)(Min.y, Other.Min.y);
        Min.z = (std::min)(Min.z, Other.Min.z);

        Max.x = (std::max)(Max.x, Other.Max.x);
        Max.y = (std::max)(Max.y, Other.Max.y);
        Max.z = (std::max)(Max.z, Other.Max.z);
    }

    void ExpandBy(const FVector3& Point)
    {
        Min.x = (std::min)(Min.x, Point.x);
        Min.y = (std::min)(Min.y, Point.y);
        Min.z = (std::min)(Min.z, Point.z);

        Max.x = (std::max)(Max.x, Point.x);
        Max.y = (std::max)(Max.y, Point.y);
        Max.z = (std::max)(Max.z, Point.z);
    }

    FAABB Union(const FAABB& Other) const
    {
        return FAABB(
            FVector3((std::min)(Min.x, Other.Min.x), (std::min)(Min.y, Other.Min.y), (std::min)(Min.z, Other.Min.z)),
            FVector3((std::max)(Max.x, Other.Max.x), (std::max)(Max.y, Other.Max.y), (std::max)(Max.z, Other.Max.z))
        );
    }

    bool Intersects(const FAABB& Other) const
    {
        return (Min.x <= Other.Max.x && Max.x >= Other.Min.x) &&
               (Min.y <= Other.Max.y && Max.y >= Other.Min.y) &&
               (Min.z <= Other.Max.z && Max.z >= Other.Min.z);
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
        if (d.x >= d.y && d.x >= d.z) return 0;
        if (d.y >= d.z)               return 1;
        return 2;
    }

    bool IsValid() const
    {
        return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
    }
};
