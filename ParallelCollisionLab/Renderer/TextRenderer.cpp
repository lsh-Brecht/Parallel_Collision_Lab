#include "TextRenderer.h"

bool FTextRenderer::Init(IDXGISwapChain* pSwapChain)
{
	if (!pSwapChain)
		return false;

	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &D2DFactory);
	if (FAILED(hr))
		return false;

	hr = DWriteCreateFactory(
		DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(&DWriteFactory)
	);
	if (FAILED(hr))
		return false;

	hr = DWriteFactory->CreateTextFormat(
		L"Consolas",
		nullptr,
		DWRITE_FONT_WEIGHT_SEMI_BOLD,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		16.0f,
		L"en-US",
		&TextFormat
	);
	if (FAILED(hr))
		return false;

	TextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	TextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	TextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

	return CreateRenderTarget(pSwapChain);
}

void FTextRenderer::ReleaseRenderTarget()
{
	if (TextBrush)       { TextBrush->Release();       TextBrush = nullptr; }
	if (D2DRenderTarget) { D2DRenderTarget->Release(); D2DRenderTarget = nullptr; }
}

bool FTextRenderer::CreateRenderTarget(IDXGISwapChain* pSwapChain)
{
	if (!pSwapChain || !D2DFactory)
		return false;

	IDXGISurface* pSurface = nullptr;
	HRESULT hr = pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pSurface));
	if (FAILED(hr))
		return false;

	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		0.0f, 0.0f
	);

	hr = D2DFactory->CreateDxgiSurfaceRenderTarget(pSurface, &props, &D2DRenderTarget);
	pSurface->Release();
	if (FAILED(hr))
		return false;

	hr = D2DRenderTarget->CreateSolidColorBrush(
		D2D1::ColorF(D2D1::ColorF::Yellow),
		&TextBrush
	);
	if (FAILED(hr))
		return false;

	return true;
}

void FTextRenderer::Shutdown()
{
	ReleaseRenderTarget();
	if (TextFormat)      { TextFormat->Release();      TextFormat = nullptr; }
	if (DWriteFactory)   { DWriteFactory->Release();   DWriteFactory = nullptr; }
	if (D2DFactory)      { D2DFactory->Release();      D2DFactory = nullptr; }
}

//=============================================================================
// Rendering
//=============================================================================
void FTextRenderer::DrawTextOverlay(const wchar_t* text, float x, float y, float width, float height)
{
	if (!D2DRenderTarget || !TextFormat || !TextBrush || !text)
		return;

	D2DRenderTarget->BeginDraw();
	D2DRenderTarget->DrawTextW(
		text,
		(UINT32)wcslen(text),
		TextFormat,
		D2D1::RectF(x, y, x + width, y + height),
		TextBrush
	);
	D2DRenderTarget->EndDraw();
}
