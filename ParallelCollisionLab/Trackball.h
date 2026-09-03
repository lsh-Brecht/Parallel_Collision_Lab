#pragma once
#include "Common.h"
#include <cmath>
#include <algorithm>


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
	bool     bTracking = false;
	FVector2 m0;
	FVector3 cam0Eye;
	FVector3 cam0Up;

	void Begin(const FVector3& eye, const FVector3& up, const FVector2& m)
	{
		bTracking = true;
		m0        = m;
		cam0Eye   = eye;
		cam0Up    = up;
	}

	void End()
	{
		bTracking = false;
	}

	void Update(const FVector2& m, const FVector3& at, FVector3& outEye, FVector3& outUp)
	{
		if (!bTracking)
			return;

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
		outEye = at + RotateVector(w, axis, -theta);
		outUp  = RotateVector(cam0Up, axis, -theta).Normalize();
	}
};
