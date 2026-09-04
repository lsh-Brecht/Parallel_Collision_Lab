#pragma once

#include "Common.h"
#include <cmath>
#include <algorithm>

//=============================================================================
// Utility
//=============================================================================
inline FVector2 CursorToNDC(int mouseX, int mouseY, int width, int height)
{
	if (width <= 1 || height <= 1)
		return FVector2(0.0f, 0.0f);
	float nx = (float)mouseX / (float)(width - 1);
	float ny = (float)mouseY / (float)(height - 1);
	return FVector2(nx * 2.0f - 1.0f, 1.0f - ny * 2.0f);
}

//=============================================================================
// FTrackball
//=============================================================================
class FTrackball
{
public:
	int      mode = 0;
	FVector2 m0;
	FVector3 cam0Eye;
	FVector3 cam0Up;

	static constexpr float MinDistance = 5.0f;
	static constexpr float MaxDistance = 15.0f;

	void Begin(const FVector3& eye, const FVector3& up, const FVector2& m, int inMode)
	{
		mode    = inMode;
		m0      = m;
		cam0Eye = eye;
		cam0Up  = up;
	}

	void End()
	{
		mode = 0;
	}

	bool IsTracking() const
	{
		return mode != 0;
	}

	void Update(const FVector2& m, const FVector3& at, FVector3& outEye, FVector3& outUp)
	{
		if (mode == 1)
		{
			UpdateRotating(m, at, outEye, outUp);
		}
		else if (mode == 2)
		{
			UpdateZooming(m, at, outEye);
		}
	}

	void ApplyWheelZoom(int wheelDelta, const FVector3& at, FVector3& eye)
	{
		FVector3 toAt = at - eye;
		float dist = toAt.Length();
		if (dist < 0.0001f)
			return;

		FVector3 n = toAt * (1.0f / dist);
		float newDist = dist - (float)wheelDelta * 0.002f;
		newDist = (std::max)(MinDistance, (std::min)(MaxDistance, newDist));
		eye = at - n * newDist;
	}

private:
	void UpdateRotating(const FVector2& m, const FVector3& at, FVector3& outEye, FVector3& outUp)
	{
		FVector3 p1(m.x - m0.x, m.y - m0.y, 0.0f);
		if (p1.LengthSq() < 0.000001f)
		{
			outEye = cam0Eye;
			outUp  = cam0Up;
			return;
		}

		p1.z = sqrtf((std::max)(0.0f, 1.0f - p1.LengthSq()));
		p1   = p1.Normalize();

		FVector3 p0(0.0f, 0.0f, 1.0f);
		FVector3 c = p0.Cross(p1);

		FVector3 zAxis = (at - cam0Eye).Normalize();
		FVector3 xAxis = cam0Up.Cross(zAxis).Normalize();
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

		FVector3 w = cam0Eye - at;
		outEye = at + RotateVector(w, axis, theta);
		outUp  = RotateVector(cam0Up, axis, theta).Normalize();
	}

	void UpdateZooming(const FVector2& m, const FVector3& at, FVector3& outEye)
	{
		FVector3 p1(m.x - m0.x, m.y - m0.y, 0.0f);
		if (p1.LengthSq() < 0.000001f)
			return;

		FVector3 toAt = at - cam0Eye;
		float dist = toAt.Length();
		if (dist < 0.0001f)
			return;

		FVector3 n = toAt * (1.0f / dist);
		float zoom = dist - p1.y * dist * 3.0f;
		zoom = (std::max)(MinDistance, (std::min)(MaxDistance, zoom));

		outEye = at - n * zoom;
	}
};
