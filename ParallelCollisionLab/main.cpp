#include "Window.h"
#include "Renderer.h"
#include "Circle.h"

#include <ctime>

//=============================================================================
// Timer
//=============================================================================
static LARGE_INTEGER g_Frequency;
static LARGE_INTEGER g_LastTime;

static void InitTimer()
{
	QueryPerformanceFrequency(&g_Frequency);
	QueryPerformanceCounter(&g_LastTime);
}

static float GetDeltaTime()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	float dt = (float)(now.QuadPart - g_LastTime.QuadPart) / (float)g_Frequency.QuadPart;
	g_LastTime = now;
	return dt;
}

//=============================================================================
// WinMain
//=============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	srand((unsigned int)time(nullptr));

	FWindow window;
	if (!window.Init(hInstance, 1024, 1024, L"Parallel Collision Lab"))
		return -1;

	URenderer renderer;
	renderer.Init(window.GetHWND());

	std::vector<FVertexSimple> unitCircleVerts = CreateUnitCircleVertices();
	ID3D11Buffer* circleVB     = renderer.CreateVertexBuffer(unitCircleVerts);
	const UINT    circleVCount = (UINT)unitCircleVerts.size();

	int numCircles = MIN_CIRCLES;
	float aspect   = window.GetAspectRatio();
	std::vector<FCircle> circles = CreateCircles(numCircles, aspect);

	bool bPaused = false;

	InitTimer();

	bool bRunning = true;
	while (bRunning)
	{
		FInputState input;
		if (!window.PumpMessages(input))
			break;

		if (input.Quit)
			break;

		aspect = window.GetAspectRatio();

		if (input.Reset)
		{
			circles = CreateCircles(numCircles, aspect);
		}
		else if (input.Toggle)
		{
			numCircles = (numCircles < 137) ? MAX_CIRCLES : MIN_CIRCLES;
			circles    = CreateCircles(numCircles, aspect);
		}
		else if (input.Add || input.AddMany)
		{
			int delta  = input.AddMany ? 16 : 1;
			numCircles = min(numCircles + delta, MAX_CIRCLES);
			circles    = CreateCircles(numCircles, aspect);
		}
		else if (input.Sub || input.SubMany)
		{
			int delta  = input.SubMany ? 16 : 1;
			numCircles = max(numCircles - delta, MIN_CIRCLES);
			circles    = CreateCircles(numCircles, aspect);
		}

		if (input.Pause)
			bPaused = !bPaused;

		float dt = GetDeltaTime();

		if (!bPaused && !window.IsMinimized())
		{
			for (FCircle& c : circles)
			{
				c.Update(dt);
				c.WallCollisionCheck(aspect);
			}

			for (size_t i = 0; i < circles.size(); ++i)
			{
				for (size_t j = i + 1; j < circles.size(); ++j)
				{
					if (circles[i].CollisionCheck(circles[j]) < 0.0f)
						circles[i].HandleCollision(circles[j]);
				}
			}
		}

		if (!window.IsMinimized())
		{
			float boundW = (aspect >= 1.0f) ? aspect : 1.0f;
			float boundH = (aspect >= 1.0f) ? 1.0f : (1.0f / aspect);
			FMatrix4x4 viewProj = FMatrix4x4::OrthoLH(-boundW, boundW, -boundH, boundH, 0.0f, 1.0f);

			renderer.BeginFrame(viewProj);

			for (const FCircle& c : circles)
			{
				renderer.RenderCircle(c.GetModelMatrix(), c.Color, circleVB, circleVCount);
			}

			renderer.EndFrame();
		}
	}

	renderer.ReleaseVertexBuffer(circleVB);
	renderer.Shutdown();
	window.Shutdown();

	return 0;
}
