#pragma once

#include "Common.h"
#include <vector>

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
		float v0     = (float)i / (float)SPHERE_STACKS;
		float v1     = (float)(i + 1) / (float)SPHERE_STACKS;

		for (int j = 0; j < SPHERE_SLICES; ++j)
		{
			float phi0 = 2.0f * (float)M_PI * (float)j / (float)SPHERE_SLICES;
			float phi1 = 2.0f * (float)M_PI * (float)(j + 1) / (float)SPHERE_SLICES;
			float u0   = (float)j / (float)SPHERE_SLICES;
			float u1   = (float)(j + 1) / (float)SPHERE_SLICES;

			auto MakeVertex = [](float theta, float phi, float u, float v) -> FVertexSimple
			{
				float sinT = sinf(theta);
				float cosT = cosf(theta);
				float sinP = sinf(phi);
				float cosP = cosf(phi);
				return { -sinT * sinP, cosT, sinT * cosP, u, v };
			};

			FVertexSimple p0 = MakeVertex(theta0, phi0, u0, v0);
			FVertexSimple p1 = MakeVertex(theta1, phi0, u0, v1);
			FVertexSimple p2 = MakeVertex(theta1, phi1, u1, v1);
			FVertexSimple p3 = MakeVertex(theta0, phi1, u1, v0);

			// Outward front-facing (CCW) winding order: exterior is front-facing, interior is culled
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
	FVector4 BaseColor;
	float    Radius;
	float    Mass;
	bool     bIsSleeping = false;
	float    SleepTimer  = 0.0f;

	FMatrix4x4 GetModelMatrix() const
	{
		return FMatrix4x4::Translate(Center.x, Center.y, Center.z) * FMatrix4x4::Scale(Radius, Radius, Radius);
	}

	void WakeUp()
	{
		bIsSleeping = false;
		SleepTimer  = 0.0f;
	}

	void Update(float DeltaTime, bool bApplyDamping = false,
	            float DampingFactor = 0.992f,
	            float SleepThreshold = 0.06f,
	            float SleepTimeReq = 0.25f,
	            const FVector4& SleepColor = FVector4(0.35f, 0.36f, 0.40f, 1.0f),
	            float ColorLerpSpeed = 6.0f)
	{
		if (bApplyDamping && !bIsSleeping)
		{
			float decay = powf(DampingFactor, DeltaTime * 60.0f);
			Velocity *= decay;

			float speedSq = Velocity.LengthSq();
			if (speedSq < SleepThreshold * SleepThreshold)
			{
				SleepTimer += DeltaTime;
				if (SleepTimer >= SleepTimeReq)
				{
					bIsSleeping = true;
					Velocity = FVector3(0.0f, 0.0f, 0.0f);
				}
			}
			else
			{
				SleepTimer = 0.0f;
			}
		}

		if (!bIsSleeping)
		{
			Center += Velocity * (DeltaTime * SPEED_FACTOR);
		}

		// Smooth Color Lerp
		FVector4 targetColor = bIsSleeping ? SleepColor : BaseColor;
		float t = (std::min)(1.0f, DeltaTime * ColorLerpSpeed);
		Color = FVector4::Lerp(Color, targetColor, t);
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

static const float EARTH_BASE_RADIUS = 0.24f;

inline std::vector<FSphere> CreateSpheres(int numSpheres, float L, bool bMultiScale = false)
{
	std::vector<FSphere> spheres;
	spheres.reserve(numSpheres);

	float scale = cbrtf((float)MIN_SPHERES) / cbrtf((float)numSpheres);

	if (numSpheres > 0)
	{
		FSphere earth;
		earth.Id          = 0;
		earth.Radius      = EARTH_BASE_RADIUS * scale;
		earth.Mass        = earth.Radius * earth.Radius * earth.Radius;
		earth.Center      = FVector3(0.0f, 0.0f, 0.0f);
		earth.Velocity    = FVector3(RandF(-0.8f, 0.8f), RandF(-0.8f, 0.8f), RandF(-0.8f, 0.8f));
		earth.Color       = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		earth.BaseColor   = earth.Color;
		earth.bIsSleeping = false;
		earth.SleepTimer  = 0.0f;
		spheres.push_back(earth);
	}

	// 2. Remaining ordinary spheres initialization
	int numLarge  = 0;
	int numMedium = 0;
	if (bMultiScale)
	{
		numLarge  = (numSpheres >= 64) ? 2 : 1;
		numMedium = (numSpheres >= 128) ? 6 : (numSpheres >= 32 ? 2 : 1);
	}

	for (int i = 1; i < numSpheres; ++i)
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
			c.Center      = FVector3(RandF(-bound, bound), RandF(-bound, bound), RandF(-bound, bound));
			c.Velocity    = FVector3(RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f), RandF(-1.0f, 1.0f));
			c.Color       = HSVtoRGB(RandF(0.0f, 360.0f), RandF(0.85f, 1.0f), RandF(0.85f, 1.0f));
			c.BaseColor   = c.Color;
			c.bIsSleeping = false;
			c.SleepTimer  = 0.0f;

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
