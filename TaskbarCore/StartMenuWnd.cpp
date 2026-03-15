#include "pch.h"
#include <shellapi.h>
#include "StartMenuWnd.h"
#include "GlobalState.h"

ID2D1RenderTarget *g_pMenuRenderTarget = nullptr;
IWICBitmap *g_pMenuWicBitmap = nullptr;
int g_MenuWidth = 0;
int g_MenuHeight = 0;

// 渲染开始菜单
void RenderStartMenu(HWND hwnd) 
{
    if (!g_pD2DFactory || !g_pWICFactory) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if ((width == 0) || (height == 0)) return;

    // 尺寸变化
    if (g_pMenuWicBitmap && ((width != g_MenuWidth) || (height != g_MenuHeight))) 
    {
        if (g_pMenuRenderTarget) 
        {
            g_pMenuRenderTarget->Release();
            g_pMenuRenderTarget = nullptr;
        }
        g_pMenuWicBitmap->Release();
        g_pMenuWicBitmap = nullptr;
    }

    if (!g_pMenuWicBitmap) 
    {
        HRESULT hr = g_pWICFactory->CreateBitmap(
            width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
            &g_pMenuWicBitmap);
        if (FAILED(hr)) return;
        g_MenuWidth = width;
        g_MenuHeight = height;
    }

    if (!g_pMenuRenderTarget) 
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

        HRESULT hr = g_pD2DFactory->CreateWicBitmapRenderTarget(
            g_pMenuWicBitmap, props, &g_pMenuRenderTarget);
        if (FAILED(hr)) return;
    }

    g_pMenuRenderTarget->BeginDraw();

    // 半透明测试
    g_pMenuRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f));

    // 边框
    ID2D1SolidColorBrush *pBrush = nullptr;
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &pBrush))) 
    {
        D2D1_RECT_F borderRect = D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f);
        g_pMenuRenderTarget->DrawRectangle(borderRect, pBrush, 1.0f);
        pBrush->Release();
    }

    HRESULT hr = g_pMenuRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) 
    {
        if (g_pMenuRenderTarget) 
        {
            g_pMenuRenderTarget->Release();
            g_pMenuRenderTarget = nullptr;
        }
        if (g_pMenuWicBitmap) 
        {
            g_pMenuWicBitmap->Release();
            g_pMenuWicBitmap = nullptr;
        }
        return;
    }

    // 提交到 UpdateLayeredWindow
    HDC hdcScreen = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pDibBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    IWICBitmapLock *pLock = nullptr;
    WICRect lockRect = {0, 0, width, height};
    if (SUCCEEDED(g_pMenuWicBitmap->Lock(&lockRect, WICBitmapLockRead, &pLock))) 
    {
        UINT cbStride = 0;
        UINT cbBufferSize = 0;
        BYTE *pWicPixels = nullptr;

        pLock->GetStride(&cbStride);
        pLock->GetDataPointer(&cbBufferSize, &pWicPixels);

        // 逐行拷贝像素
        UINT dibStride = width * 4;
        for (int y = 0; y < height; y++) 
            memcpy((BYTE *)pDibBits + y * dibStride, pWicPixels + y * cbStride,
                    dibStride);

        pLock->Release();
    }

    POINT ptSrc = {0, 0};
    SIZE winSize = {width, height};

    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    RECT wndRect;
    GetWindowRect(hwnd, &wndRect);
    POINT ptDst = {wndRect.left, wndRect.top};

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &winSize, hMemoryDC, &ptSrc, 0,
                        &blend, ULW_ALPHA);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hdcScreen);
}

// 重新设置位置 
// 用于任务栏上下左右到处放
void RecalculateMenuPosition(HWND hwnd) 
{
    // (固定 待调整)
    int menuWidth = 380;
    int menuHeight = 540;

    // 定位基于 Orb 
    RECT btnRect = {0};
    if (g_hStartBtn) GetWindowRect(g_hStartBtn, &btnRect);
        else if (g_hOrbWnd && IsWindowVisible(g_hOrbWnd)) GetWindowRect(g_hOrbWnd, &btnRect);
            else 
            {
                btnRect.left = 0;
                btnRect.top = GetSystemMetrics(SM_CYSCREEN);
            }

    HMONITOR hMonitor = MonitorFromRect(&btnRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mInfo = {sizeof(MONITORINFO)};
    GetMonitorInfo(hMonitor, &mInfo);

    // 获取任务栏位置和边缘
    APPBARDATA abd = {sizeof(APPBARDATA)};
    SHAppBarMessage(ABM_GETTASKBARPOS, &abd);

    int x = btnRect.left;
    int y = btnRect.top - menuHeight;

    // 根据任务栏所处屏幕边缘决定弹出逻辑
    if ((abd.uEdge == ABE_BOTTOM) || (btnRect.top > (mInfo.rcWork.bottom - 100)))
    {
        // 任务栏在底部则向上弹出
        y = btnRect.top - menuHeight;
        x = btnRect.left;
    } 
        else if ((abd.uEdge == ABE_TOP) || (btnRect.bottom < (mInfo.rcWork.top + 100)))
        {
            // 顶部向下
            y = btnRect.bottom;
            x = btnRect.left;
        } 
            else if ((abd.uEdge == ABE_LEFT) || (btnRect.right < (mInfo.rcWork.left + 100)))
            {
                // 左侧向右
                y = btnRect.bottom - menuHeight;
                x = btnRect.right;
            } 
                else if ((abd.uEdge == ABE_RIGHT) || (btnRect.left > (mInfo.rcWork.right - 100)))
                {
                    // 右侧向左
                    y = btnRect.bottom - menuHeight;
                    x = btnRect.left - menuWidth;
                }

    // 防超    
    if (x < mInfo.rcWork.left) x = mInfo.rcWork.left;
    if ((x + menuWidth) > mInfo.rcWork.right) 
        x = mInfo.rcWork.right - menuWidth;
    if (y < mInfo.rcWork.top) y = mInfo.rcWork.top;
    if ((y + menuHeight) > mInfo.rcWork.bottom) 
        y = mInfo.rcWork.bottom - menuHeight;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, menuWidth, menuHeight,
            SWP_NOACTIVATE | SWP_NOZORDER);
}

// 开始菜单窗口
LRESULT CALLBACK StartMenuProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) 
    {
        case WM_CREATE: 
        {
            //
            return 0;
        }

        case WM_PAINT: 
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (IsWindowVisible(hwnd)) RenderStartMenu(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TOGGLE_STARTMENU: 
        {
            if (IsWindowVisible(hwnd)) 
            {
                ShowWindow(hwnd, SW_HIDE);
                OutputDebugString(L"[Hook] Start menu hidden\n");
            } 
                else 
                {
                    // 弹出前重新计算一次位置（因为任务栏可能移动或变过分辨率）
                    RecalculateMenuPosition(hwnd);
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    RenderStartMenu(hwnd);
                    OutputDebugString(L"[Hook] Start menu shown\n");
                }
            return 0;
        }

        // 焦点管理
        // 点击空白或 ECS 隐藏菜单
        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE) 
            {
                POINT pt;
                GetCursorPos(&pt);
                if (WindowFromPoint(pt) == g_hOrbWnd) // 防止因左键 Orb 失焦而隐藏菜单
                    return 0;                           // 理论上应 LBUTTONUP 时再隐藏

                if (IsWindowVisible(hwnd)) 
                {
                    ShowWindow(hwnd, SW_HIDE);
                    OutputDebugString(L"[Hook] Start menu auto-hidden due to focus loss\n");
                }
            }
            return 0;
        }

        case WM_KEYDOWN: 
        {
            if (wParam == VK_ESCAPE) 
            {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_DESTROY: 
        {
            if (g_pMenuRenderTarget) 
            {
                g_pMenuRenderTarget->Release();
                g_pMenuRenderTarget = nullptr;
            }
            if (g_pMenuWicBitmap) 
            {
                g_pMenuWicBitmap->Release();
                g_pMenuWicBitmap = nullptr;
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 StartMenu 窗口
HWND CreateStartMenuWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"VistaStartMenu";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = StartMenuProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // 设置光标
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, // 置顶 & 不进任务栏 & 分层窗口支持
        className,
        L"VistaStartMenu",
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    return hWnd;
}