#include "Window.h"
#include <windowsx.h>
#ifdef IsMinimized
#undef IsMinimized
#endif

//=============================================================================
// Window Lifecycle
//=============================================================================
bool FWindow::Init(HINSTANCE _hInstance, int width, int height, const wchar_t* title, int posX, int posY)
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
		posX, posY,
		width, height,
		nullptr, nullptr, hInstance, nullptr
	);

	if (hWnd)
	{
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	}

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
	outInput.bShiftDown = bShiftHeld;

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
		else if (msg.message == WM_RBUTTONDOWN)
		{
			SetCapture(hWnd);
			outInput.bRButtonPressed = true;
			outInput.MouseX          = GET_X_LPARAM(msg.lParam);
			outInput.MouseY          = GET_Y_LPARAM(msg.lParam);
		}
		else if (msg.message == WM_RBUTTONUP)
		{
			ReleaseCapture();
			outInput.bRButtonReleased = true;
			outInput.MouseX           = GET_X_LPARAM(msg.lParam);
			outInput.MouseY           = GET_Y_LPARAM(msg.lParam);
		}
		else if (msg.message == WM_MOUSEMOVE)
		{
			outInput.bMouseMoving = true;
			outInput.MouseX       = GET_X_LPARAM(msg.lParam);
			outInput.MouseY       = GET_Y_LPARAM(msg.lParam);
		}
		else if (msg.message == WM_MOUSEWHEEL)
		{
			outInput.WheelDelta += GET_WHEEL_DELTA_WPARAM(msg.wParam);
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

			case '1':
			case VK_NUMPAD1:
			case VK_F1:
				outInput.SelectSolver = 0;
				break;

			case '2':
			case VK_NUMPAD2:
			case VK_F2:
				outInput.SelectSolver = 1;
				break;

			case '3':
			case VK_NUMPAD3:
			case VK_F3:
				outInput.SelectSolver = 2;
				break;

			case '4':
			case VK_NUMPAD4:
			case VK_F4:
				outInput.SelectSolver = 3;
				break;

			case '5':
			case VK_NUMPAD5:
			case VK_F5:
				outInput.SelectSolver = 4;
				break;

			case '6':
			case VK_NUMPAD6:
			case VK_F6:
				outInput.SelectSolver = 5;
				break;

			case VK_TAB:
				outInput.CycleSolver = true;
				break;

			case 'B':
				outInput.Benchmark = true;
				break;

			case 'G':
				outInput.ToggleGridVis = true;
				break;

			case 'H':
				outInput.ToggleHUD = true;
				break;

			case 'V':
				outInput.CycleBvhMode = true;
				break;

			case VK_PRIOR:
				outInput.IncBvhDepth = true;
				break;

			case VK_NEXT:
				outInput.DecBvhDepth = true;
				break;

			case VK_OEM_4:
				outInput.DecThread = true;
				break;

			case VK_OEM_6:
				outInput.IncThread = true;
				break;
			}
		}

		if (msg.message == WM_KEYUP && msg.wParam == VK_SPACE)
			bSpaceWasDown = false;
	}

	if (bPendingResize)
	{
		outInput.bResized  = true;
		outInput.NewWidth  = PendingWidth;
		outInput.NewHeight = PendingHeight;
		bPendingResize     = false;
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
	FWindow* pThis = reinterpret_cast<FWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

	switch (msg)
	{
	case WM_SIZE:
		if (pThis && wParam != SIZE_MINIMIZED)
		{
			pThis->bPendingResize = true;
			pThis->PendingWidth   = LOWORD(lParam);
			pThis->PendingHeight  = HIWORD(lParam);
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}
