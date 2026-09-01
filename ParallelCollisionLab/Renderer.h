#pragma once

#include <windows.h>
#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#include <d3d11.h>
#include <d3dcompiler.h>

#include "Common.h"
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

	ID3D11RasterizerState*  RasterizerState = nullptr;
	D3D11_VIEWPORT          ViewportInfo    = {};

	ID3D11VertexShader*     VertexShader  = nullptr;
	ID3D11PixelShader*      PixelShader   = nullptr;
	ID3D11InputLayout*      InputLayout   = nullptr;
	UINT                    Stride        = 0;

	ID3D11Buffer*           CBPerFrame   = nullptr;
	ID3D11Buffer*           CBPerObject  = nullptr;

	FLOAT ClearColor[4] = { 0.025f, 0.025f, 0.025f, 1.0f };

public:
	bool Init(HWND hWnd);
	void Shutdown();

	void BeginFrame(const FMatrix4x4& viewProj);
	void EndFrame();

	void RenderCircle(const FMatrix4x4& model, const FVector4& color,
	                  ID3D11Buffer* pVB, UINT vertexCount);

	ID3D11Buffer* CreateVertexBuffer(const std::vector<FVertexSimple>& vertices);
	void          ReleaseVertexBuffer(ID3D11Buffer* pBuffer);

private:
	void CreateDeviceAndSwapChain(HWND hWnd);
	void CreateFrameBuffer();
	void CreateRasterizerState();
	void CreateShader();
	void CreateConstantBuffers();

	void ReleaseDeviceAndSwapChain();
	void ReleaseFrameBuffer();
	void ReleaseRasterizerState();
	void ReleaseShader();
	void ReleaseConstantBuffers();

	template<typename T>
	void UpdateConstantBuffer(ID3D11Buffer* pBuffer, const T& data);
};
