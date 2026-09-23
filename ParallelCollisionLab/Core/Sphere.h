#pragma once

#define _USE_MATH_DEFINES
#include "Common.h"
#include <vector>
#include <cmath>

//=============================================================================
// Unit Sphere
//=============================================================================
static const int SPHERE_STACKS = 18;
static const int SPHERE_SLICES = 36;

inline std::vector<FVertexSimple> CreateUnitSphereVertices()
{
	std::vector<FVertexSimple> verts;
	verts.reserve(SPHERE_STACKS * SPHERE_SLICES * 6);

	for (int i = 0; i < SPHERE_STACKS; ++i)
	{
		float theta0 = (float)M_PI * (float)i / (float)SPHERE_STACKS;
		float theta1 = (float)M_PI * (float)(i + 1) / (float)SPHERE_STACKS;

		for (int j = 0; j < SPHERE_SLICES; ++j)
		{
			float phi0 = 2.0f * (float)M_PI * (float)j / (float)SPHERE_SLICES;
			float phi1 = 2.0f * (float)M_PI * (float)(j + 1) / (float)SPHERE_SLICES;

			FVertexSimple p0 = { sinf(theta0) * cosf(phi0), cosf(theta0), -sinf(theta0) * sinf(phi0) };
			FVertexSimple p1 = { sinf(theta1) * cosf(phi0), cosf(theta1), -sinf(theta1) * sinf(phi0) };
			FVertexSimple p2 = { sinf(theta1) * cosf(phi1), cosf(theta1), -sinf(theta1) * sinf(phi1) };
			FVertexSimple p3 = { sinf(theta0) * cosf(phi1), cosf(theta0), -sinf(theta0) * sinf(phi1) };

			verts.push_back(p0);
			verts.push_back(p1);
			verts.push_back(p2);

			verts.push_back(p0);
			verts.push_back(p2);
			verts.push_back(p3);
		}
	}
	return verts;
}

//=============================================================================
// Cornell Box Walls
//=============================================================================
inline std::vector<FVertexSimple> CreateWallVertices(int wallType, float L)
{
	std::vector<FVertexSimple> v;
	if (wallType == 0)
	{
		v.push_back({ -L, -L,  L });
		v.push_back({ -L,  L, -L });
		v.push_back({ -L, -L, -L });
		v.push_back({ -L, -L,  L });
		v.push_back({ -L,  L,  L });
		v.push_back({ -L,  L, -L });
	}
	else if (wallType == 1)
	{
		v.push_back({  L, -L, -L });
		v.push_back({  L,  L,  L });
		v.push_back({  L, -L,  L });
		v.push_back({  L, -L, -L });
		v.push_back({  L,  L, -L });
		v.push_back({  L,  L,  L });
	}
	else if (wallType == 2)
	{
		v.push_back({ -L, -L, -L });
		v.push_back({  L, -L, -L });
		v.push_back({  L, -L,  L });
		v.push_back({ -L, -L, -L });
		v.push_back({  L, -L,  L });
		v.push_back({ -L, -L,  L });

		v.push_back({ -L,  L,  L });
		v.push_back({  L,  L,  L });
		v.push_back({  L,  L, -L });
		v.push_back({ -L,  L,  L });
		v.push_back({  L,  L, -L });
		v.push_back({ -L,  L, -L });

		v.push_back({ -L, -L,  L });
		v.push_back({  L, -L,  L });
		v.push_back({  L,  L,  L });
		v.push_back({ -L, -L,  L });
		v.push_back({  L,  L,  L });
		v.push_back({ -L,  L,  L });

		v.push_back({  L, -L, -L });
		v.push_back({ -L, -L, -L });
		v.push_back({ -L,  L, -L });
		v.push_back({  L, -L, -L });
		v.push_back({ -L,  L, -L });
		v.push_back({  L,  L, -L });
	}
	return v;
}

//=============================================================================
// FSphere
//=============================================================================
static const int   MIN_SPHERES  = 16;
static const int   MAX_SPHERES  = 4096;
static const float SPEED_FACTOR = 0.8f;

struct FSphere
{
	int32_t  Id = 0;
	FVector3 Center;
	FVector3 Velocity;
	FVector4 Color;
	float    Radius;
	float    Mass;

	FMatrix4x4 GetModelMatrix() const
	{
		return FMatrix4x4::Translate(Center.x, Center.y, Center.z) * FMatrix4x4::Scale(Radius, Radius, Radius);
	}

	void Update(float DeltaTime)
	{
		Center += Velocity * (DeltaTime * SPEED_FACTOR);
	}

	void BoxCollisionCheck(float BoxHalfSize)
	{
		const float maxX = BoxHalfSize - Radius;
		if (Center.x > maxX)
		{
			Center.x = maxX;
			if (Velocity.x > 0.0f) Velocity.x = -fabsf(Velocity.x);
		}
		else if (Center.x < -maxX)
		{
			Center.x = -maxX;
			if (Velocity.x < 0.0f) Velocity.x = fabsf(Velocity.x);
		}

		const float maxY = BoxHalfSize - Radius;
		if (Center.y > maxY)
		{
			Center.y = maxY;
			if (Velocity.y > 0.0f) Velocity.y = -fabsf(Velocity.y);
		}
		else if (Center.y < -maxY)
		{
			Center.y = -maxY;
			if (Velocity.y < 0.0f) Velocity.y = fabsf(Velocity.y);
		}

		const float maxZ = BoxHalfSize - Radius;
		if (Center.z > maxZ)
		{
			Center.z = maxZ;
			if (Velocity.z > 0.0f) Velocity.z = -fabsf(Velocity.z);
		}
		else if (Center.z < -maxZ)
		{
			Center.z = -maxZ;
			if (Velocity.z < 0.0f) Velocity.z = fabsf(Velocity.z);
		}
	}

	float CollisionCheck(const FSphere& Other) const
	{
		float dist = (Center - Other.Center).Length();
		return dist - (Radius + Other.Radius);
	}
};

//=============================================================================
// Sphere Creation
//=============================================================================
inline std::vector<FSphere> CreateSpheres(int numSpheres, float L, bool bMultiScale = false)
{
	std::vector<FSphere> spheres;
	spheres.reserve(numSpheres);

	float scale = cbrtf((float)MIN_SPHERES) / cbrtf((float)numSpheres);

	int numLarge  = 0;
	int numMedium = 0;
	if (bMultiScale)
	{
		numLarge  = (numSpheres >= 64) ? 2 : 1;
		numMedium = (numSpheres >= 128) ? 6 : (numSpheres >= 32 ? 2 : 1);
	}

	for (int i = 0; i < numSpheres; ++i)
	{
		FSphere c;
		c.Id = i;
		bool bColliding;
		int attempts = 0;

		do
		{
			bColliding = false;
			if (++attempts > 1000)
				break;

			if (!bMultiScale)
			{
				c.Radius = RandF(0.08f, 0.22f) * scale;
			}
			else
			{
				if (i < numLarge)
				{
					c.Radius = RandF(0.24f, 0.30f);
				}
				else if (i < numLarge + numMedium)
				{
					c.Radius = RandF(0.07f, 0.11f);
				}
				else
				{
					c.Radius = RandF(0.06f, 0.16f) * scale;
				}
			}

			c.Mass = c.Radius * c.Radius * c.Radius;

			float bound = (L - c.Radius) * 0.95f;
			if (bound < 0.01f) bound = 0.01f;
			c.Center   = FVector3(RandF(-bound, bound), RandF(-bound, bound), RandF(-bound, bound));
			c.Velocity = FVector3(RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f));
			c.Color    = HSVtoRGB(RandF(0.0f, 360.0f), RandF(0.85f, 1.0f), RandF(0.85f, 1.0f));

			for (const FSphere& e : spheres)
			{
				if (c.CollisionCheck(e) < 0.0f)
				{
					bColliding = true;
					break;
				}
			}
		}
		while (bColliding);

		spheres.push_back(c);
	}

	return spheres;
}
