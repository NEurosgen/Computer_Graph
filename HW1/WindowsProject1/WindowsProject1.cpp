#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <tchar.h>
#include <DirectXMath.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }

ID3D11Device* m_pDevice = nullptr;
ID3D11DeviceContext* m_pDeviceContext = nullptr;
IDXGISwapChain* m_pSwapChain = nullptr;
ID3D11RenderTargetView* m_pBackBufferRTV = nullptr;
ID3D11Texture2D* m_pDepthStencilBuffer = nullptr;
ID3D11DepthStencilView* m_pDepthStencilView = nullptr;
ID3D11Buffer* m_pVertexBuffer = nullptr;
ID3D11Buffer* m_pIndexBuffer = nullptr;
ID3D11VertexShader* m_pVertexShader = nullptr;
ID3D11PixelShader* m_pPixelShader = nullptr;
ID3D11InputLayout* m_pInputLayout = nullptr;


ID3D11Buffer* m_pGeomBuffer = nullptr;
ID3D11Buffer* m_pSceneBuffer = nullptr;

UINT m_width = 1280;
UINT m_height = 720;
ULONGLONG startTime = 0;

struct Vertex {
    float x, y, z;
    struct { unsigned char r, g, b, a; } color;
};


struct GeomBuffer {
    XMMATRIX model;
};

struct SceneBuffer {
    XMMATRIX vp;
};

const char* ShadersSource = R"(
cbuffer GeomBuffer : register(b0) {
    float4x4 model;
};

cbuffer SceneBuffer : register(b1) {
    float4x4 vp;
};

struct VSInput {
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VSOutput {
    float4 pos : SV_Position;
    float4 color : COLOR;
};

VSOutput vs(VSInput vertex) {
    VSOutput result;
    float4 worldPos = mul(model, float4(vertex.pos, 1.0));
    result.pos = mul(vp, worldPos);
    
    result.color = vertex.color;
    return result;
}

float4 ps(VSOutput pixel) : SV_Target0 {
    return pixel.color;
}
)";

HRESULT CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
        SAFE_RELEASE(pBackBuffer);
    }


    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    result = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &m_pDepthStencilBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, nullptr, &m_pDepthStencilView);
    }

    return result;
}

HRESULT InitScene() {
    HRESULT hr = S_OK;


    Vertex Vertices[] = {
        {-0.5f, -0.5f, -0.5f, {255, 0, 0, 255}},
        {-0.5f,  0.5f, -0.5f, {0, 255, 0, 255}},
        { 0.5f,  0.5f, -0.5f, {0, 0, 255, 255}},
        { 0.5f, -0.5f, -0.5f, {255, 255, 0, 255}},
        {-0.5f, -0.5f,  0.5f, {0, 255, 255, 255}},
        {-0.5f,  0.5f,  0.5f, {255, 0, 255, 255}},
        { 0.5f,  0.5f,  0.5f, {255, 255, 255, 255}},
        { 0.5f, -0.5f,  0.5f, {0, 0, 0, 255}}
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(Vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = Vertices;
    hr = m_pDevice->CreateBuffer(&vbDesc, &vbData, &m_pVertexBuffer);
    if (FAILED(hr)) return hr;


    USHORT Indices[] = {
        0,1,2, 0,2,3, // Front
        4,6,5, 4,7,6, // Back
        4,5,1, 4,1,0, // Left
        3,2,6, 3,6,7, // Right
        1,5,6, 1,6,2, // Top
        4,0,3, 4,3,7  // Bottom
    };

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(Indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = Indices;
    hr = m_pDevice->CreateBuffer(&ibDesc, &ibData, &m_pIndexBuffer);
    if (FAILED(hr)) return hr;


    D3D11_BUFFER_DESC geomDesc = {};
    geomDesc.ByteWidth = sizeof(GeomBuffer);
    geomDesc.Usage = D3D11_USAGE_DEFAULT;
    geomDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_pDevice->CreateBuffer(&geomDesc, nullptr, &m_pGeomBuffer);
    if (FAILED(hr)) return hr;


    D3D11_BUFFER_DESC sceneDesc = {};
    sceneDesc.ByteWidth = sizeof(SceneBuffer);
    sceneDesc.Usage = D3D11_USAGE_DYNAMIC;
    sceneDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    sceneDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_pDevice->CreateBuffer(&sceneDesc, nullptr, &m_pSceneBuffer);
    if (FAILED(hr)) return hr;

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* pVSBlob = nullptr;
    ID3DBlob* pPSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    hr = D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr)) {
        if (pErrorBlob) OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
        return hr;
    }
    hr = m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pVertexShader);

    hr = D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr)) return hr;
    hr = m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPixelShader);

    D3D11_INPUT_ELEMENT_DESC InputDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    hr = m_pDevice->CreateInputLayout(InputDesc, 2, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pInputLayout);

    SAFE_RELEASE(pVSBlob);
    SAFE_RELEASE(pPSBlob);
    SAFE_RELEASE(pErrorBlob);

    return hr;
}

HRESULT InitDirectX(HWND hWnd) {
    HRESULT result;
    IDXGIFactory* pFactory = nullptr;
    result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (FAILED(result)) return result;

    IDXGIAdapter* pSelectedAdapter = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
    UINT adapterIdx = 0;
    while (SUCCEEDED(pFactory->EnumAdapters(adapterIdx, &pAdapter))) {
        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);
        if (wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0) {
            pSelectedAdapter = pAdapter;
            break;
        }
        pAdapter->Release();
        adapterIdx++;
    }

    if (!pSelectedAdapter) return E_FAIL;
    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    result = D3D11CreateDevice(pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, levels, 1, D3D11_SDK_VERSION, &m_pDevice, &level, &m_pDeviceContext);
    SAFE_RELEASE(pSelectedAdapter);
    if (FAILED(result)) return result;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = m_width;
    swapChainDesc.BufferDesc.Height = m_height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = true;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    result = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    SAFE_RELEASE(pFactory);
    if (FAILED(result)) return result;

    result = CreateRenderTarget();
    if (FAILED(result)) return result;

    startTime = GetTickCount64();

    return InitScene();
}

void Render() {
    if (!m_pDeviceContext || !m_pSwapChain) return;

    double elapsedSec = (GetTickCount64() - startTime) / 1000.0;

    m_pDeviceContext->ClearState();

    ID3D11RenderTargetView* views[] = { m_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, m_pDepthStencilView);

    static const FLOAT BackColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(m_pBackBufferRTV, BackColor);
    m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);


    GeomBuffer geomBuffer;
    geomBuffer.model = XMMatrixRotationY((float)elapsedSec) * XMMatrixRotationX((float)elapsedSec * 0.5f);
    m_pDeviceContext->UpdateSubresource(m_pGeomBuffer, 0, nullptr, &geomBuffer, 0, 0);


    float fov = XM_PI / 3.0f;
    float aspectRatio = (float)m_width / (float)m_height;


    float camAngle = (float)elapsedSec * 0.5f;
    float camRadius = 3.0f;
    XMVECTOR camPos = XMVectorSet(sinf(camAngle) * camRadius, 1.0f, cosf(camAngle) * camRadius, 1.0f);
    XMVECTOR focusPoint = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR upDirection = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(camPos, focusPoint, upDirection);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspectRatio, 0.1f, 100.0f);


    D3D11_MAPPED_SUBRESOURCE subresource;
    if (SUCCEEDED(m_pDeviceContext->Map(m_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource))) {
        SceneBuffer* pSceneBuffer = reinterpret_cast<SceneBuffer*>(subresource.pData);
        pSceneBuffer->vp = XMMatrixMultiply(view, proj);
        m_pDeviceContext->Unmap(m_pSceneBuffer, 0);
    }

    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)m_width;
    viewport.Height = (FLOAT)m_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_pDeviceContext->RSSetViewports(1, &viewport);

    m_pDeviceContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    UINT strides[] = { sizeof(Vertex) };
    UINT offsets[] = { 0 };
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, strides, offsets);
    m_pDeviceContext->IASetInputLayout(m_pInputLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


    ID3D11Buffer* constBuffers[] = { m_pGeomBuffer, m_pSceneBuffer };
    m_pDeviceContext->VSSetConstantBuffers(0, 2, constBuffers);

    m_pDeviceContext->VSSetShader(m_pVertexShader, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPixelShader, nullptr, 0);

    m_pDeviceContext->DrawIndexed(36, 0, 0);

    m_pSwapChain->Present(1, 0);
}

void Cleanup() {
    if (m_pDeviceContext) m_pDeviceContext->ClearState();

    SAFE_RELEASE(m_pGeomBuffer);
    SAFE_RELEASE(m_pSceneBuffer);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthStencilBuffer);
    SAFE_RELEASE(m_pVertexBuffer);
    SAFE_RELEASE(m_pIndexBuffer);
    SAFE_RELEASE(m_pVertexShader);
    SAFE_RELEASE(m_pPixelShader);
    SAFE_RELEASE(m_pInputLayout);
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pSwapChain);
    SAFE_RELEASE(m_pDeviceContext);
    SAFE_RELEASE(m_pDevice);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (m_pSwapChain && m_pDevice && wParam != SIZE_MINIMIZED) {
            SAFE_RELEASE(m_pBackBufferRTV);
            SAFE_RELEASE(m_pDepthStencilView);
            SAFE_RELEASE(m_pDepthStencilBuffer);
            m_width = LOWORD(lParam);
            m_height = HIWORD(lParam);
            HRESULT hr = m_pSwapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);
            if (SUCCEEDED(hr)) {
                CreateRenderTarget();
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX11Lesson", nullptr };
    RegisterClassEx(&wc);
    RECT rc = { 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    HWND hWnd = CreateWindow(L"DX11Lesson", L"DirectX 11 3D Cube", WS_OVERLAPPEDWINDOW,
        100, 100, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);

    if (FAILED(InitDirectX(hWnd))) {
        Cleanup();
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg = { 0 };
    bool exit = false;
    while (!exit) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) exit = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            Render();
        }
    }

    Cleanup();
    return (int)msg.wParam;
}