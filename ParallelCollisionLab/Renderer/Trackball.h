#pragma once

#include "../Core/Common.h"
#include <cmath>
#include <algorithm>

//=============================================================================
// Utility
//=============================================================================
inline FVector2 CursorToNDC(int MouseX, int MouseY, int Width, int Height)
{
	if (Width <= 1 || Height <= 1)
		return FVector2(0.0f, 0.0f);
	float nx = static_cast<float>(MouseX) / static_cast<float>(Width - 1);
	float ny = static_cast<float>(MouseY) / static_cast<float>(Height - 1);
	return FVector2(nx * 2.0f - 1.0f, 1.0f - ny * 2.0f);
}

//=============================================================================
// FTrackball
//=============================================================================
class FTrackball
{
public:
	int      TrackingMode = 0;
	FVector2 StartNDC;
	FVector3 StartEye;
	FVector3 StartUp;

	static constexpr float MinDistance = 1.0f;
	static constexpr float MaxDistance = 15.0f;

	void Begin(const FVector3& InEye, const FVector3& InUp, const FVector2& InNDC, int InMode)
	{
		TrackingMode = InMode;
		StartNDC     = InNDC;
		StartEye     = InEye;
		StartUp      = InUp;
	}

	void End()
	{
		TrackingMode = 0;
	}

	bool IsTracking() const
	{
		return TrackingMode != 0;
	}

	void Update(const FVector2& InNDC, const FVector3& InAt, FVector3& OutEye, FVector3& OutUp)
	{
		if (TrackingMode == 1)
		{
			UpdateRotating(InNDC, InAt, OutEye, OutUp);
		}
		else if (TrackingMode == 2)
		{
			UpdateZooming(InNDC, InAt, OutEye);
		}
	}

private:
	void UpdateRotating(const FVector2& InNDC, const FVector3& InAt, FVector3& OutEye, FVector3& OutUp)
	{
		FVector3 p1(InNDC.x - StartNDC.x, InNDC.y - StartNDC.y, 0.0f);
		if (p1.LengthSq() < 0.000001f)
		{
			OutEye = StartEye;
			OutUp  = StartUp;
			return;
		}

		p1.z = sqrtf((std::max)(0.0f, 1.0f - p1.LengthSq()));
		p1   = p1.Normalize();

		FVector3 p0(0.0f, 0.0f, 1.0f);
		FVector3 c = p0.Cross(p1);

		FVector3 zAxis = (InAt - StartEye).Normalize();
		FVector3 xAxis = StartUp.Cross(zAxis).Normalize();
		FVector3 yAxis = zAxis.Cross(xAxis);

		FVector3 v = xAxis * c.x + yAxis * c.y + zAxis * c.z;
		float vLen = v.Length();
		if (vLen < 0.000001f)
			return;

		float theta = asinf((std::min)(vLen, 1.0f));
		FVector3 axis = v.Normalize();

		auto RotateVector = [](const FVector3& vec, const FVector3& u, float angle) -> FVector3
		{
			float cosA = cosf(angle);
			float sinA = sinf(angle);
			return vec * cosA + u.Cross(vec) * sinA + u * (u.Dot(vec) * (1.0f - cosA));
		};

		FVector3 w = StartEye - InAt;
		OutEye = InAt + RotateVector(w, axis, theta);
		OutUp  = RotateVector(StartUp, axis, theta);
	}

	void UpdateZooming(const FVector2& InNDC, const FVector3& InAt, FVector3& OutEye)
	{
		float dy = InNDC.y - StartNDC.y;
		float factor = 1.0f - dy * 2.0f;
		if (factor < 0.1f) factor = 0.1f;

		FVector3 dir = StartEye - InAt;
		float dist = dir.Length();
		float newDist = dist * factor;
		if (newDist < MinDistance) newDist = MinDistance;
		if (newDist > MaxDistance) newDist = MaxDistance;

		if (dist > 0.0001f)
		{
			OutEye = InAt + dir * (newDist / dist);
		}
	}
};
