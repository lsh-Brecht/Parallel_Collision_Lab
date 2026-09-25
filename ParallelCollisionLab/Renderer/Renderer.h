#pragma once

#include <windows.h>
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#include <d3d11.h>
#include <d3dcompiler.h>

#include "../Core/Common.h"
#include <vector>

//=============================================================================
// Constant Buffers (b0: PerFrame, b1: PerObject)
//=============================================================================
struct FPerFrameConstants
{
	FMatrix4x4 ViewProj;
};

struct FPerObjectConstants
{
	FMatrix4x4 Model;
	FVector4   Color;
	int        RenderMode = 0; // 0: Solid, 1: Earth, 2: Mars, 3: UVMap
	float      Padding[3] = { 0.0f, 0.0f, 0.0f };
};

//=============================================================================
// URenderer
//=============================================================================
class URenderer
{
public:
	ID3D11Device*           Device         = nullptr;
	ID3D11DeviceContext*    DeviceContext   = nullptr;
	IDXGISwapChain*         SwapChain      = nullptr;

	ID3D11Texture2D*        FrameBuffer    = nullptr;
	ID3D11RenderTargetView* FrameBufferRTV = nullptr;

	ID3D11Texture2D*          DepthStencilBuffer = nullptr;
	ID3D11DepthStencilView*   DepthStencilView   = nullptr;
	ID3D11DepthStencilState*  DepthStencilState  = nullptr;

	ID3D11RasterizerState*  RasterizerState     = nullptr;
	ID3D11RasterizerState*  RasterizerWireframe = nullptr;
	bool                    bWireframe          = false;
	D3D11_VIEWPORT          ViewportInfo        = {};

	ID3D11VertexShader*     VertexShader  = nullptr;
	ID3D11PixelShader*      PixelShader   = nullptr;
	ID3D11InputLayout*      InputLayout   = nullptr;
	UINT                    Stride        = 0;

	ID3D11Buffer*           CBPerFrame   = nullptr;
	ID3D11Buffer*           CBPerObject  = nullptr;

	FLOAT ClearColor[4] = { 0.05f, 0.05f, 0.05f, 1.0f };

public:
	bool Init(HWND hWnd);
	void Shutdown();
	void OnResize(int newWidth, int newHeight);

	void BeginFrame(const FMatrix4x4& viewProj);
	void EndFrame();

	void RenderSphere(const FMatrix4x4& model, const FVector4& color,
	                  ID3D11Buffer* pVB, UINT vertexCount, int renderMode = 0);

	void RenderDynamicLines(const std::vector<FVertexSimple>& lines, const FVector4& color);

	void ToggleWireframe() { bWireframe = !bWireframe; }

	ID3D11Buffer* CreateVertexBuffer(const std::vector<FVertexSimple>& vertices);
	void          ReleaseVertexBuffer(ID3D11Buffer* pBuffer);

	ID3D11Buffer* DynamicLineVB       = nullptr;
	UINT          DynamicLineCapacity = 0;

	ID3D11ShaderResourceView* EarthSRV      = nullptr;
	ID3D11ShaderResourceView* MarsSRV       = nullptr;
	ID3D11SamplerState*       PlanetSampler = nullptr;

private:
	void CreateDeviceAndSwapChain(HWND hWnd);
	void CreateFrameBuffer();
	void CreateDepthBuffer();
	void CreateRasterizerState();
	void CreateShader();
	void CreateConstantBuffers();
	void CreatePlanetTextures();

	void ReleaseDeviceAndSwapChain();
	void ReleaseFrameBuffer();
	void ReleaseDepthBuffer();
	void ReleaseRasterizerState();
	void ReleaseShader();
	void ReleaseConstantBuffers();
	void ReleasePlanetTextures();

	template<typename T>
	void UpdateConstantBuffer(ID3D11Buffer* pBuffer, const T& data);
};
