#pragma once

#include "../Core/Common.h"
#include <vector>

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

// wallType: 0=Left, 1=Right, 2=Floor/Ceiling/Front/Back
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
