#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

//=============================================================================
// Input State
//=============================================================================
struct FInputState
{
	bool Reset;
	bool Toggle;
	bool ToggleSphereSize;
	bool Add;
	bool AddMany;
	bool Sub;
	bool SubMany;
	bool Pause;
	bool Wireframe;
	bool ResetCamera;
	bool Quit;
	bool Benchmark;
	bool ToggleGridVis;
	bool ToggleHUD;
	bool DecThread;
	bool IncThread;

	bool IncBvhDepth;
	bool DecBvhDepth;
	bool CycleBvhMode;

	int  SelectSolver;
	bool ToggleDamping;

	// Earth Directional Controls (Arrows + Q/E)
	bool MoveLeft;
	bool MoveRight;
	bool MoveUp;
	bool MoveDown;
	bool MoveForward;
	bool MoveBackward;

	bool bLButtonPressed;
	bool bLButtonReleased;
	bool bRButtonPressed;
	bool bRButtonReleased;
	bool bMouseMoving;
	bool bShiftDown;
	int  MouseX;
	int  MouseY;
	int  WheelDelta;

	bool bResized;
	int  NewWidth;
	int  NewHeight;

	void Clear() { *this = {}; SelectSolver = -1; }
};

//=============================================================================
// FWindow
//=============================================================================
class FWindow
{
public:
	bool Init(HINSTANCE hInstance, int width, int height, const wchar_t* title, int posX = 10, int posY = 10);
	void Shutdown();

	bool PumpMessages(FInputState& outInput);

	HWND  GetHWND()        const { return hWnd; }
	float GetAspectRatio() const;
	bool  IsMinimized()    const;
	void  GetClientSize(int& width, int& height) const;
	void  SetTitle(const wchar_t* title) { if (hWnd) SetWindowTextW(hWnd, title); }

private:
	HWND      hWnd           = nullptr;
	HINSTANCE hInstance      = nullptr;
	bool      bShiftHeld     = false;
	bool      bSpaceWasDown  = false;
	bool      bPendingResize = false;
	int       PendingWidth   = 0;
	int       PendingHeight  = 0;

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
