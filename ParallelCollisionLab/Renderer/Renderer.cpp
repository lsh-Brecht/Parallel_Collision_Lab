#include "Renderer.h"

bool URenderer::Init(HWND hWnd)
{
	CreateDeviceAndSwapChain(hWnd);
	CreateFrameBuffer();
	CreateDepthBuffer();
	CreateRasterizerState();
	CreateShader();
	CreateConstantBuffers();

	DynamicLineCapacity = 65536;
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = (UINT)(sizeof(FVertexSimple) * DynamicLineCapacity);
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	Device->CreateBuffer(&desc, nullptr, &DynamicLineVB);

	return true;
}

void URenderer::Shutdown()
{
	if (DynamicLineVB) { DynamicLineVB->Release(); DynamicLineVB = nullptr; }
	ReleaseConstantBuffers();
	ReleaseShader();
	ReleaseRasterizerState();
	ReleaseDepthBuffer();
	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseFrameBuffer();
	ReleaseDeviceAndSwapChain();
}

void URenderer::OnResize(int newWidth, int newHeight)
{
	if (!SwapChain || newWidth <= 0 || newHeight <= 0)
		return;

	DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
	ReleaseFrameBuffer();
	ReleaseDepthBuffer();

	HRESULT hr = SwapChain->ResizeBuffers(2, (UINT)newWidth, (UINT)newHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
	if (FAILED(hr))
		return;

	ViewportInfo = { 0.0f, 0.0f, (float)newWidth, (float)newHeight, 0.0f, 1.0f };
	CreateFrameBuffer();
	CreateDepthBuffer();
}

//=============================================================================
// Rendering Pipeline
//=============================================================================
void URenderer::BeginFrame(const FMatrix4x4& viewProj)
{
	DeviceContext->ClearRenderTargetView(FrameBufferRTV, ClearColor);
	DeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DeviceContext->RSSetViewports(1, &ViewportInfo);
	DeviceContext->RSSetState(bWireframe ? RasterizerWireframe : RasterizerState);
	DeviceContext->OMSetRenderTargets(1, &FrameBufferRTV, DepthStencilView);
	DeviceContext->OMSetDepthStencilState(DepthStencilState, 0);
	DeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);

	DeviceContext->VSSetShader(VertexShader, nullptr, 0);
	DeviceContext->PSSetShader(PixelShader, nullptr, 0);
	DeviceContext->IASetInputLayout(InputLayout);

	ID3D11Buffer* cbs[2] = { CBPerFrame, CBPerObject };
	DeviceContext->VSSetConstantBuffers(0, 2, cbs);

	FPerFrameConstants perFrame;
	perFrame.ViewProj = viewProj;
	UpdateConstantBuffer(CBPerFrame, perFrame);
}

void URenderer::RenderSphere(const FMatrix4x4& model, const FVector4& color,
                              ID3D11Buffer* pVB, UINT vertexCount)
{
	FPerObjectConstants perObj;
	perObj.Model = model;
	perObj.Color = color;
	UpdateConstantBuffer(CBPerObject, perObj);

	UINT offset = 0;
	DeviceContext->IASetVertexBuffers(0, 1, &pVB, &Stride, &offset);
	DeviceContext->Draw(vertexCount, 0);
}

void URenderer::RenderDynamicLines(const std::vector<FVertexSimple>& lines, const FVector4& color)
{
	if (lines.empty() || !DeviceContext)
		return;

	UINT count = static_cast<UINT>(lines.size());
	if (count > DynamicLineCapacity || !DynamicLineVB)
	{
		if (DynamicLineVB) { DynamicLineVB->Release(); DynamicLineVB = nullptr; }
		DynamicLineCapacity = max(count * 2, 65536u);
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)(sizeof(FVertexSimple) * DynamicLineCapacity);
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Device->CreateBuffer(&desc, nullptr, &DynamicLineVB);
	}

	D3D11_MAPPED_SUBRESOURCE msr;
	HRESULT hr = DeviceContext->Map(DynamicLineVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	if (SUCCEEDED(hr))
	{
		memcpy(msr.pData, lines.data(), sizeof(FVertexSimple) * count);
		DeviceContext->Unmap(DynamicLineVB, 0);

		FPerObjectConstants perObj;
		perObj.Model = FMatrix4x4::Identity();
		perObj.Color = color;
		UpdateConstantBuffer(CBPerObject, perObj);

		UINT offset = 0;
		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		DeviceContext->IASetVertexBuffers(0, 1, &DynamicLineVB, &Stride, &offset);
		DeviceContext->Draw(count, 0);
		DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
}

void URenderer::EndFrame()
{
	SwapChain->Present(1, 0);
}

//=============================================================================
// Vertex Buffer
//=============================================================================
ID3D11Buffer* URenderer::CreateVertexBuffer(const std::vector<FVertexSimple>& vertices)
{
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth  = (UINT)(sizeof(FVertexSimple) * vertices.size());
	desc.Usage      = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags  = D3D11_BIND_VERTEX_BUFFER;

	D3D11_SUBRESOURCE_DATA srd = { vertices.data() };

	ID3D11Buffer* pBuffer = nullptr;
	Device->CreateBuffer(&desc, &srd, &pBuffer);
	return pBuffer;
}

void URenderer::ReleaseVertexBuffer(ID3D11Buffer* pBuffer)
{
	if (pBuffer) pBuffer->Release();
}

//=============================================================================
// Device & Resources Setup
//=============================================================================
void URenderer::CreateDeviceAndSwapChain(HWND hWnd)
{
	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferDesc.Width  = 0;
	desc.BufferDesc.Height = 0;
	desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count  = 1;
	desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount       = 2;
	desc.OutputWindow      = hWnd;
	desc.Windowed          = TRUE;
	desc.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
		featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
		&desc, &SwapChain, &Device, nullptr, &DeviceContext
	);

	SwapChain->GetDesc(&desc);
	ViewportInfo = { 0.0f, 0.0f,
		(float)desc.BufferDesc.Width,
		(float)desc.BufferDesc.Height,
		0.0f, 1.0f };
}

void URenderer::CreateFrameBuffer()
{
	SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&FrameBuffer);

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	Device->CreateRenderTargetView(FrameBuffer, &rtvDesc, &FrameBufferRTV);
}

void URenderer::CreateDepthBuffer()
{
	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width     = (UINT)ViewportInfo.Width;
	depthDesc.Height    = (UINT)ViewportInfo.Height;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format    = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Usage     = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	Device->CreateTexture2D(&depthDesc, nullptr, &DepthStencilBuffer);

	Device->CreateDepthStencilView(DepthStencilBuffer, nullptr, &DepthStencilView);

	D3D11_DEPTH_STENCIL_DESC dsDesc = {};
	dsDesc.DepthEnable    = TRUE;
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
	Device->CreateDepthStencilState(&dsDesc, &DepthStencilState);
}

void URenderer::CreateRasterizerState()
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode              = D3D11_FILL_SOLID;
	desc.CullMode              = D3D11_CULL_BACK;
	desc.FrontCounterClockwise = TRUE;
	Device->CreateRasterizerState(&desc, &RasterizerState);

	desc.FillMode              = D3D11_FILL_WIREFRAME;
	desc.CullMode              = D3D11_CULL_NONE;
	desc.FrontCounterClockwise = FALSE;
	Device->CreateRasterizerState(&desc, &RasterizerWireframe);
}

void URenderer::CreateShader()
{
	ID3DBlob* vsBlob   = nullptr;
	ID3DBlob* psBlob   = nullptr;
	ID3DBlob* errBlob  = nullptr;

	const wchar_t* shaderPath = L"Renderer/Shader.hlsl";
	if (GetFileAttributesW(shaderPath) == INVALID_FILE_ATTRIBUTES)
	{
		shaderPath = L"Shader.hlsl";
	}

	HRESULT hr = D3DCompileFromFile(shaderPath, nullptr, nullptr,
		"mainVS", "vs_5_0", 0, 0, &vsBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob)
		{
			OutputDebugStringA((const char*)errBlob->GetBufferPointer());
			errBlob->Release();
		}
		return;
	}

	Device->CreateVertexShader(
		vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &VertexShader);

	hr = D3DCompileFromFile(shaderPath, nullptr, nullptr,
		"mainPS", "ps_5_0", 0, 0, &psBlob, &errBlob);
	if (FAILED(hr))
	{
		if (errBlob)
		{
			OutputDebugStringA((const char*)errBlob->GetBufferPointer());
			errBlob->Release();
		}
		vsBlob->Release();
		return;
	}

	Device->CreatePixelShader(
		psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &PixelShader);

	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	Device->CreateInputLayout(
		layout, ARRAYSIZE(layout),
		vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &InputLayout);

	Stride = sizeof(FVertexSimple);

	vsBlob->Release();
	psBlob->Release();
}

void URenderer::CreateConstantBuffers()
{
	{
		UINT byteWidth = (sizeof(FPerFrameConstants) + 0xf) & ~0xf;
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth      = byteWidth;
		desc.Usage          = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		Device->CreateBuffer(&desc, nullptr, &CBPerFrame);
	}

	{
		UINT byteWidth = (sizeof(FPerObjectConstants) + 0xf) & ~0xf;
		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth      = byteWidth;
		desc.Usage          = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
		Device->CreateBuffer(&desc, nullptr, &CBPerObject);
	}
}

template<typename T>
void URenderer::UpdateConstantBuffer(ID3D11Buffer* pBuffer, const T& data)
{
	D3D11_MAPPED_SUBRESOURCE msr;
	DeviceContext->Map(pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
	memcpy(msr.pData, &data, sizeof(T));
	DeviceContext->Unmap(pBuffer, 0);
}

void URenderer::ReleaseDeviceAndSwapChain()
{
	if (DeviceContext) DeviceContext->Flush();
	if (SwapChain)     { SwapChain->Release();     SwapChain     = nullptr; }
	if (DeviceContext) { DeviceContext->Release();  DeviceContext = nullptr; }
	if (Device)        { Device->Release();         Device        = nullptr; }
}

void URenderer::ReleaseFrameBuffer()
{
	if (FrameBufferRTV) { FrameBufferRTV->Release(); FrameBufferRTV = nullptr; }
	if (FrameBuffer)    { FrameBuffer->Release();    FrameBuffer    = nullptr; }
}

void URenderer::ReleaseDepthBuffer()
{
	if (DepthStencilState)  { DepthStencilState->Release();  DepthStencilState  = nullptr; }
	if (DepthStencilView)   { DepthStencilView->Release();   DepthStencilView   = nullptr; }
	if (DepthStencilBuffer) { DepthStencilBuffer->Release(); DepthStencilBuffer = nullptr; }
}

void URenderer::ReleaseRasterizerState()
{
	if (RasterizerWireframe) { RasterizerWireframe->Release(); RasterizerWireframe = nullptr; }
	if (RasterizerState)     { RasterizerState->Release();     RasterizerState     = nullptr; }
}

void URenderer::ReleaseShader()
{
	if (InputLayout)  { InputLayout->Release();   InputLayout  = nullptr; }
	if (PixelShader)  { PixelShader->Release();   PixelShader  = nullptr; }
	if (VertexShader) { VertexShader->Release();  VertexShader = nullptr; }
}

void URenderer::ReleaseConstantBuffers()
{
	if (CBPerFrame)  { CBPerFrame->Release();  CBPerFrame  = nullptr; }
	if (CBPerObject) { CBPerObject->Release(); CBPerObject = nullptr; }
}
