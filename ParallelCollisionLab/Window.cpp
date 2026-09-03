#include "Window.h"
#include <windowsx.h>
#ifdef IsMinimized
#undef IsMinimized
#endif

//=============================================================================
// Window Lifecycle
//=============================================================================
bool FWindow::Init(HINSTANCE _hInstance, int width, int height, const wchar_t* title)
{
	hInstance = _hInstance;

	WNDCLASSW wc    = {};
	wc.lpfnWndProc  = WndProc;
	wc.hInstance    = hInstance;
	wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"ParallelCollisionLabClass";
	RegisterClassW(&wc);

	hWnd = CreateWindowExW(
		0,
		L"ParallelCollisionLabClass",
		title,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		width, height,
		nullptr, nullptr, hInstance, nullptr
	);

	return (hWnd != nullptr);
}

void FWindow::Shutdown()
{
	if (hWnd)
	{
		DestroyWindow(hWnd);
		hWnd = nullptr;
	}
	UnregisterClassW(L"ParallelCollisionLabClass", hInstance);
}

//=============================================================================
// Message Loop & Input
//=============================================================================
bool FWindow::PumpMessages(FInputState& outInput)
{
	outInput.Clear();

	bShiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);

		if (msg.message == WM_QUIT)
			return false;

		if (msg.message == WM_LBUTTONDOWN)
		{
			SetCapture(hWnd);
			outInput.bLButtonPressed = true;
			outInput.MouseX          = GET_X_LPARAM(msg.lParam);
			outInput.MouseY          = GET_Y_LPARAM(msg.lParam);
		}
		else if (msg.message == WM_LBUTTONUP)
		{
			ReleaseCapture();
			outInput.bLButtonReleased = true;
			outInput.MouseX           = GET_X_LPARAM(msg.lParam);
			outInput.MouseY           = GET_Y_LPARAM(msg.lParam);
		}
		else if (msg.message == WM_MOUSEMOVE)
		{
			outInput.bMouseMoving = true;
			outInput.MouseX       = GET_X_LPARAM(msg.lParam);
			outInput.MouseY       = GET_Y_LPARAM(msg.lParam);
		}

		if (msg.message == WM_KEYDOWN)
		{
			switch (msg.wParam)
			{
			case VK_ESCAPE:
				outInput.Quit = true;
				PostQuitMessage(0);
				return false;

			case 'R':
				outInput.Reset = true;
				break;

			case 'F':
				outInput.Toggle = true;
				break;

			case 'W':
				outInput.Wireframe = true;
				break;

			case VK_HOME:
				outInput.ResetCamera = true;
				break;

			case VK_OEM_PLUS:
			case VK_ADD:
				if (bShiftHeld) outInput.AddMany = true;
				else            outInput.Add     = true;
				break;

			case VK_OEM_MINUS:
			case VK_SUBTRACT:
				if (bShiftHeld) outInput.SubMany = true;
				else            outInput.Sub     = true;
				break;

			case VK_SPACE:
				if (!bSpaceWasDown)
				{
					outInput.Pause = true;
					bSpaceWasDown  = true;
				}
				break;
			}
		}

		if (msg.message == WM_KEYUP && msg.wParam == VK_SPACE)
			bSpaceWasDown = false;
	}

	return true;
}

float FWindow::GetAspectRatio() const
{
	RECT rect;
	GetClientRect(hWnd, &rect);
	int w = rect.right  - rect.left;
	int h = rect.bottom - rect.top;
	if (h == 0) return 1.0f;
	return (float)w / (float)h;
}

void FWindow::GetClientSize(int& width, int& height) const
{
	RECT rect;
	GetClientRect(hWnd, &rect);
	width  = rect.right  - rect.left;
	height = rect.bottom - rect.top;
}

bool FWindow::IsMinimized() const
{
	return IsIconic(hWnd) != 0;
}

//=============================================================================
// WndProc
//=============================================================================
LRESULT CALLBACK FWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}
