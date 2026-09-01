#pragma once

#define _USE_MATH_DEFINES
#include "Common.h"
#include <vector>
#include <cmath>

//=============================================================================
// Unit Circle
//=============================================================================
static const int CIRCLE_TESS = 72;

inline std::vector<FVertexSimple> CreateUnitCircleVertices()
{
	std::vector<FVertexSimple> verts;
	verts.reserve(CIRCLE_TESS * 3);

	const float step = 2.0f * (float)M_PI / (float)CIRCLE_TESS;
	for (int i = 0; i < CIRCLE_TESS; ++i)
	{
		float a0 = step * (float)i;
		float a1 = step * (float)(i + 1);

		verts.push_back({ 0.0f,     0.0f,     0.0f });
		verts.push_back({ cosf(a0), sinf(a0), 0.0f });
		verts.push_back({ cosf(a1), sinf(a1), 0.0f });
	}
	return verts;
}

//=============================================================================
// FCircle
//=============================================================================
static const int   MIN_CIRCLES  = 16;
static const int   MAX_CIRCLES  = 256;
static const float SPEED_FACTOR = 0.6f;

struct FCircle
{
	FVector2 Center;
	FVector2 Velocity;
	FVector4 Color;
	float    Radius;
	float    Mass;

	FMatrix4x4 GetModelMatrix() const
	{
		return FMatrix4x4::Translate(Center.x, Center.y, 0.0f) * FMatrix4x4::Scale(Radius, Radius, 1.0f);
	}

	void Update(float dt)
	{
		Center += Velocity * (dt * SPEED_FACTOR);
	}

	void WallCollisionCheck(float aspectRatio)
	{
		float boundW = (aspectRatio >= 1.0f) ? aspectRatio : 1.0f;
		float boundH = (aspectRatio >= 1.0f) ? 1.0f : (1.0f / aspectRatio);

		if (Center.x + Radius >  boundW && Velocity.x > 0.0f)  Velocity.x *= -1.0f;
		if (Center.x - Radius < -boundW && Velocity.x < 0.0f)  Velocity.x *= -1.0f;
		if (Center.y + Radius >  boundH && Velocity.y > 0.0f)  Velocity.y *= -1.0f;
		if (Center.y - Radius < -boundH && Velocity.y < 0.0f)  Velocity.y *= -1.0f;
	}

	float CollisionCheck(const FCircle& other) const
	{
		FVector2 diff = Center - other.Center;
		return diff.Length() - (Radius + other.Radius);
	}

	void HandleCollision(FCircle& other)
	{
		FVector2 x1_x2 = Center - other.Center;
		FVector2 v1_v2 = Velocity - other.Velocity;

		if (v1_v2.Dot(x1_x2) > 0.0f)
			return;

		float distSq = x1_x2.LengthSq();
		if (distSq == 0.0f)
			return;

		float m1 = Mass;
		float m2 = other.Mass;
		FVector2 x2_x1 = FVector2(-x1_x2.x, -x1_x2.y);
		FVector2 v2_v1 = FVector2(-v1_v2.x, -v1_v2.y);

		float impulse = v2_v1.Dot(x2_x1) / distSq;

		Velocity       = Velocity       + x2_x1 * ((2.0f * m2 / (m1 + m2)) * impulse);
		other.Velocity = other.Velocity + x1_x2 * ((2.0f * m1 / (m1 + m2)) * impulse);

		float dist    = sqrtf(distSq);
		float overlap = (Radius + other.Radius - dist) * 0.5f;
		if (overlap > 0.0f)
		{
			FVector2 correction = x1_x2 * (overlap / dist);
			Center       = Center       + correction;
			other.Center = other.Center - correction;
		}
	}
};

//=============================================================================
// Circle Creation
//=============================================================================
inline std::vector<FCircle> CreateCircles(int numCircles, float aspectRatio)
{
	std::vector<FCircle> circles;
	circles.reserve(numCircles);

	float boundW = (aspectRatio >= 1.0f) ? aspectRatio : 1.0f;
	float boundH = (aspectRatio >= 1.0f) ? 1.0f : (1.0f / aspectRatio);

	for (int i = 0; i < numCircles; ++i)
	{
		FCircle c;
		bool bColliding;

		do
		{
			bColliding = false;

			float scale = sqrtf((float)MIN_CIRCLES) / sqrtf((float)numCircles);
			c.Radius    = RandF(0.02f, 0.23f) * scale;
			c.Mass      = c.Radius * c.Radius;

			float xBound = (boundW - c.Radius) * 0.98f;
			float yBound = (boundH - c.Radius) * 0.98f;
			c.Center   = FVector2(RandF(-xBound, xBound), RandF(-yBound, yBound));
			c.Velocity = FVector2(RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f));
			c.Color    = FVector4(RandF(0.1f, 0.9f), RandF(0.1f, 0.9f), RandF(0.1f, 0.9f), 1.0f);

			for (const FCircle& e : circles)
			{
				if (c.CollisionCheck(e) < 0.0f)
				{
					bColliding = true;
					break;
				}
			}
		}
		while (bColliding);

		circles.push_back(c);
	}

	return circles;
}
