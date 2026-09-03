#pragma once

#include <windows.h>

//=============================================================================
// Input State
//=============================================================================
struct FInputState
{
	bool Reset;
	bool Toggle;
	bool Add;
	bool AddMany;
	bool Sub;
	bool SubMany;
	bool Pause;
	bool Wireframe;
	bool ResetCamera;
	bool Quit;

	bool bLButtonPressed;
	bool bLButtonReleased;
	bool bMouseMoving;
	int  MouseX;
	int  MouseY;

	void Clear() { *this = {}; }
};

//=============================================================================
// FWindow
//=============================================================================
class FWindow
{
public:
	bool Init(HINSTANCE hInstance, int width, int height, const wchar_t* title);
	void Shutdown();

	bool PumpMessages(FInputState& outInput);

	HWND  GetHWND()        const { return hWnd; }
	float GetAspectRatio() const;
	bool  IsMinimized()    const;
	void  GetClientSize(int& width, int& height) const;

private:
	HWND      hWnd          = nullptr;
	HINSTANCE hInstance     = nullptr;
	bool      bShiftHeld    = false;
	bool      bSpaceWasDown = false;

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
