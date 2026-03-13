#include "pch.h"
#include "StartOrbWnd.h"
#include "GlobalState.h"

ID2D1HwndRenderTarget* g_pOrbRenderTarget = nullptr;
int g_OrbState = 0; // 0: 默认, 1: 悬停, 2: 按下
bool g_bOrbTrackingMouse = false;

void RenderOrb(HWND hwnd)
{
    if (!g_pD2DFactory) return;

    if (!g_pOrbRenderTarget)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

        HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, size),
            &g_pOrbRenderTarget
        );

        if (FAILED(hr)) return;
    }

    g_pOrbRenderTarget->BeginDraw();
    g_pOrbRenderTarget->Clear(D2D1::ColorF(0, 0, 0)); // 背景

    D2D1_SIZE_F size = g_pOrbRenderTarget->GetSize();
    D2D1_POINT_2F center = D2D1::Point2F(size.width / 2.0f, size.height / 2.0f);
    float radius = (min(size.width, size.height) / 2.0f) - 3.0f;

    ID2D1SolidColorBrush* pBrush = nullptr;
    if (g_OrbState == 0) 
        g_pOrbRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.4f, 0.8f), &pBrush);
        else if (g_OrbState == 1) 
            g_pOrbRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.6f, 1.0f), &pBrush);
        else if (g_OrbState == 2) 
            g_pOrbRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.3f, 0.6f), &pBrush);

    if (pBrush)
    {
        g_pOrbRenderTarget->FillEllipse(D2D1::Ellipse(center, radius, radius), pBrush);
        
        ID2D1SolidColorBrush* pBorderBrush = nullptr;
        g_pOrbRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.8f), &pBorderBrush);
        if (pBorderBrush)
        {
            g_pOrbRenderTarget->DrawEllipse(D2D1::Ellipse(center, radius, radius), pBorderBrush, 1.5f);
            pBorderBrush->Release();
        }

        pBrush->Release();
    }

    HRESULT hr = g_pOrbRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        if (g_pOrbRenderTarget) { g_pOrbRenderTarget->Release(); g_pOrbRenderTarget = nullptr; }
    }
}

// Orb 按钮窗口过程
LRESULT CALLBACK OrbWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_PAINT:
        {
            RenderOrb(hwnd);
            ValidateRect(hwnd, NULL);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (!g_bOrbTrackingMouse)
            {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_bOrbTrackingMouse = true;
            }

            if (g_OrbState != 2) // 未按下
            {
                if (g_OrbState != 1)
                {
                    g_OrbState = 1; // 悬停
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            g_bOrbTrackingMouse = false;
            g_OrbState = 0; 
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            SetCapture(hwnd);
            g_OrbState = 2; 
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            ReleaseCapture();
            
			// 检测鼠标是否仍在按钮上
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            if (PtInRect(&rc, pt))
            {
                g_OrbState = 1; // 悬停
                if (g_hMenuWnd)
                    PostMessage(g_hMenuWnd, WM_TOGGLE_STARTMENU, 0, 0);
            }
                else
                {
                    g_OrbState = 0; 
                }
            
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        case WM_DESTROY:
        {
            if (g_pOrbRenderTarget)
            {
                g_pOrbRenderTarget->Release();
                g_pOrbRenderTarget = nullptr;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 Orb 覆盖窗口
HWND CreateOrbWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"VistaOrbButton";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OrbWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);

    // 获取原 Start 按钮坐标以覆盖
    RECT r = {};
    if (g_hStartBtn) GetWindowRect(g_hStartBtn, &r);

    int w = r.right - r.left;
    int h = r.bottom - r.top;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,  // 置顶 & 不进任务栏
        className, L"VistaOrb",
        WS_POPUP,
        r.left, r.top, w, h,
        hTaskbar,
        NULL, hInstance, NULL
    );

    if (hWnd) ShowWindow(hWnd, SW_SHOW);
    return hWnd;
}