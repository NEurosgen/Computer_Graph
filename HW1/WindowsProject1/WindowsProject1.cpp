#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <assert.h>
#include <tchar.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

ID3D11Device* m_pDevice = nullptr;
ID3D11DeviceContext* m_pDeviceContext = nullptr;
IDXGISwapChain* m_pSwapChain = nullptr;
ID3D11RenderTargetView* m_pBackBufferRTV = nullptr;

UINT m_width = 1280;
UINT m_height = 720;

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }


HRESULT CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
        SAFE_RELEASE(pBackBuffer);
    }
    return result;
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

    result = D3D11CreateDevice(
        pSelectedAdapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        NULL,
        flags,
        levels,
        1,
        D3D11_SDK_VERSION,
        &m_pDevice,
        &level,
        &m_pDeviceContext
    );

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
    swapChainDesc.Flags = 0;

    result = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    SAFE_RELEASE(pFactory);

    if (FAILED(result)) return result;

    return CreateRenderTarget();
}

void Render() {
    if (!m_pDeviceContext || !m_pSwapChain) return;
    m_pDeviceContext->ClearState();

    ID3D11RenderTargetView* views[] = { m_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, nullptr);
    static const FLOAT BackColor[4] = { 0.25f, 0.25f, 0.25f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(m_pBackBufferRTV, BackColor);
    m_pSwapChain->Present(1, 0);
}


void Cleanup() {
    if (m_pDeviceContext) m_pDeviceContext->ClearState();
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
            HRESULT hr = m_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
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

    HWND hWnd = CreateWindow(L"DX11Lesson", L"DirectX 11 Initialization", WS_OVERLAPPEDWINDOW,
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
            if (msg.message == WM_QUIT) {
                exit = true;
            }
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