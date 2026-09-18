#pragma once

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

struct FVector3
{
	float x, y, z;
	FVector3(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f) : x(_x), y(_y), z(_z) {}

	FVector3 operator+(const FVector3& v) const { return FVector3(x + v.x, y + v.y, z + v.z); }
	FVector3 operator-(const FVector3& v) const { return FVector3(x - v.x, y - v.y, z - v.z); }
	FVector3 operator*(float s)           const { return FVector3(x * s, y * s, z * s); }
	FVector3 operator-() const { return FVector3(-x, -y, -z); }
	FVector3& operator+=(const FVector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
	FVector3& operator-=(const FVector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }

	float Dot(const FVector3& v) const { return x * v.x + y * v.y + z * v.z; }
	float LengthSq()             const { return x * x + y * y + z * z; }
	float Length()               const { return sqrtf(LengthSq()); }

	FVector3 Cross(const FVector3& v) const
	{
		return FVector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x);
	}

	FVector3 Normalize() const
	{
		float len = Length();
		return (len > 0.0f) ? (*this * (1.0f / len)) : *this;
	}
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

	static FMatrix4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ)
	{
		FMatrix4x4 M;
		float h = 1.0f / tanf(fovY * 0.5f);
		float w = h / aspect;
		M.m[0][0] = w;
		M.m[1][1] = h;
		M.m[2][2] = farZ / (farZ - nearZ);
		M.m[2][3] = -(nearZ * farZ) / (farZ - nearZ);
		M.m[3][2] = 1.0f;
		M.m[3][3] = 0.0f;
		return M;
	}

	static FMatrix4x4 LookAtLH(const FVector3& eye, const FVector3& at, const FVector3& up)
	{
		FVector3 zAxis = (at - eye).Normalize();
		FVector3 xAxis = up.Cross(zAxis).Normalize();
		FVector3 yAxis = zAxis.Cross(xAxis);

		FMatrix4x4 M;
		M.m[0][0] = xAxis.x; M.m[0][1] = xAxis.y; M.m[0][2] = xAxis.z; M.m[0][3] = -xAxis.Dot(eye);
		M.m[1][0] = yAxis.x; M.m[1][1] = yAxis.y; M.m[1][2] = yAxis.z; M.m[1][3] = -yAxis.Dot(eye);
		M.m[2][0] = zAxis.x; M.m[2][1] = zAxis.y; M.m[2][2] = zAxis.z; M.m[2][3] = -zAxis.Dot(eye);
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

// Convert HSV (H: [0, 360], S: [0, 1], V: [0, 1]) to RGB color
inline FVector4 HSVtoRGB(float h, float s, float v)
{
	float c = v * s;
	float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
	float m = v - c;
	float r = 0.0f, g = 0.0f, b = 0.0f;

	if      (h < 60.0f)  { r = c; g = x; b = 0.0f; }
	else if (h < 120.0f) { r = x; g = c; b = 0.0f; }
	else if (h < 180.0f) { r = 0.0f; g = c; b = x; }
	else if (h < 240.0f) { r = 0.0f; g = x; b = c; }
	else if (h < 300.0f) { r = x; g = 0.0f; b = c; }
	else                 { r = c; g = 0.0f; b = x; }

	return FVector4(r + m, g + m, b + m, 1.0f);
}
