#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1")
#pragma comment(lib, "dwrite")

//=============================================================================
// FTextRenderer
//=============================================================================
class FTextRenderer
{
public:
	bool Init(IDXGISwapChain* pSwapChain);
	void Shutdown();

	void DrawTextOverlay(const wchar_t* text, float x, float y, float width, float height);

private:
	ID2D1Factory*         D2DFactory      = nullptr;
	IDWriteFactory*       DWriteFactory   = nullptr;
	IDWriteTextFormat*    TextFormat      = nullptr;
	ID2D1RenderTarget*    D2DRenderTarget = nullptr;
	ID2D1SolidColorBrush* TextBrush       = nullptr;
};
