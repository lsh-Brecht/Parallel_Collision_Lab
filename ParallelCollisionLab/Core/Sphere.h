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
	FVector3 Center;
	FVector3 Velocity;
	FVector4 Color;
	float    Radius;
	float    Mass;

	FMatrix4x4 GetModelMatrix() const
	{
		return FMatrix4x4::Translate(Center.x, Center.y, Center.z) * FMatrix4x4::Scale(Radius, Radius, Radius);
	}

	void Update(float dt)
	{
		Center += Velocity * (dt * SPEED_FACTOR);
	}

	void BoxCollisionCheck(float L)
	{
		if (Center.x + Radius >  L && Velocity.x > 0.0f) Velocity.x *= -1.0f;
		if (Center.x - Radius < -L && Velocity.x < 0.0f) Velocity.x *= -1.0f;
		if (Center.y + Radius >  L && Velocity.y > 0.0f) Velocity.y *= -1.0f;
		if (Center.y - Radius < -L && Velocity.y < 0.0f) Velocity.y *= -1.0f;
		if (Center.z + Radius >  L && Velocity.z > 0.0f) Velocity.z *= -1.0f;
		if (Center.z - Radius < -L && Velocity.z < 0.0f) Velocity.z *= -1.0f;
	}

	float CollisionCheck(const FSphere& other) const
	{
		float dist = (Center - other.Center).Length();
		return dist - (Radius + other.Radius);
	}

	void HandleCollision(FSphere& other)
	{
		FVector3 x1_x2 = Center - other.Center;
		FVector3 v1_v2 = Velocity - other.Velocity;

		if (v1_v2.Dot(x1_x2) > 0.0f)
			return;

		float distSq = x1_x2.LengthSq();
		if (distSq == 0.0f)
			return;

		float m1 = Mass;
		float m2 = other.Mass;
		FVector3 x2_x1 = FVector3(-x1_x2.x, -x1_x2.y, -x1_x2.z);
		FVector3 v2_v1 = FVector3(-v1_v2.x, -v1_v2.y, -v1_v2.z);

		float impulse = v2_v1.Dot(x2_x1) / distSq;

		Velocity       += x2_x1 * ((2.0f * m2 / (m1 + m2)) * impulse);
		other.Velocity += x1_x2 * ((2.0f * m1 / (m1 + m2)) * impulse);

		float dist = sqrtf(distSq);
		float overlap = (Radius + other.Radius - dist) * 0.5f;
		if (overlap > 0.0f)
		{
			FVector3 corr = x1_x2 * (overlap / dist);
			Center       += corr;
			other.Center -= corr;
		}
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

	for (int i = 0; i < numSpheres; ++i)
	{
		FSphere c;
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
				float rRatio = static_cast<float>(i) / static_cast<float>(numSpheres);
				if (rRatio < 0.015f || (numSpheres < 64 && i == 0))
				{
					c.Radius = RandF(0.35f, 0.48f);
				}
				else if (rRatio < 0.10f || (numSpheres < 64 && i <= 2))
				{
					c.Radius = RandF(0.12f, 0.20f);
				}
				else
				{
					c.Radius = RandF(0.025f, 0.055f) * (scale * 1.35f);
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
