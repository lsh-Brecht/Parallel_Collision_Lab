#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>

//=============================================================================
// Math Types
//=============================================================================
struct FVector2
{
	float x, y;
	FVector2(float _x = 0.0f, float _y = 0.0f) : x(_x), y(_y) {}

	FVector2 operator+(const FVector2& v) const { return FVector2(x + v.x, y + v.y); }
	FVector2 operator-(const FVector2& v) const { return FVector2(x - v.x, y - v.y); }
	FVector2 operator*(float s)           const { return FVector2(x * s, y * s); }
	FVector2& operator+=(const FVector2& v) { x += v.x; y += v.y; return *this; }

	float Dot(const FVector2& v)  const { return x * v.x + y * v.y; }
	float LengthSq()              const { return x * x + y * y; }
	float Length()                const { return sqrtf(LengthSq()); }
};

struct FVector4
{
	float x, y, z, w;
	FVector4(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f, float _w = 1.0f)
		: x(_x), y(_y), z(_z), w(_w) {}
};

struct FVertexSimple
{
	float x, y, z;
};

//=============================================================================
// FMatrix4x4
//=============================================================================
struct FMatrix4x4
{
	float m[4][4];

	FMatrix4x4() { for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m[i][j] = 0.0f; }

	static FMatrix4x4 Identity()
	{
		FMatrix4x4 M;
		M.m[0][0] = M.m[1][1] = M.m[2][2] = M.m[3][3] = 1.0f;
		return M;
	}

	static FMatrix4x4 Translate(float tx, float ty, float tz)
	{
		FMatrix4x4 M = Identity();
		M.m[0][3] = tx;
		M.m[1][3] = ty;
		M.m[2][3] = tz;
		return M;
	}

	static FMatrix4x4 Scale(float sx, float sy, float sz)
	{
		FMatrix4x4 M = Identity();
		M.m[0][0] = sx;
		M.m[1][1] = sy;
		M.m[2][2] = sz;
		return M;
	}

	static FMatrix4x4 OrthoLH(float left, float right, float bottom, float top, float nearZ, float farZ)
	{
		FMatrix4x4 M;
		M.m[0][0] = 2.0f / (right - left);
		M.m[0][3] = -(right + left) / (right - left);
		M.m[1][1] = 2.0f / (top - bottom);
		M.m[1][3] = -(top + bottom) / (top - bottom);
		M.m[2][2] = 1.0f / (farZ - nearZ);
		M.m[2][3] = -nearZ / (farZ - nearZ);
		M.m[3][3] = 1.0f;
		return M;
	}

	FMatrix4x4 operator*(const FMatrix4x4& B) const
	{
		FMatrix4x4 C;
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				for (int k = 0; k < 4; ++k)
					C.m[i][j] += m[i][k] * B.m[k][j];
		return C;
	}
};

//=============================================================================
// Math Utilities
//=============================================================================
inline float RandF(float lo, float hi)
{
	return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}
