#include "pch.h"
#include <shellapi.h>
#include "StartMenuWnd.h"
#include "GlobalState.h"

enum ACCENT_STATE 
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5,
    ACCENT_INVALID_STATE = 6
};

struct ACCENT_POLICY 
{
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB 
{
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA 
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL(WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

// 背景模糊
void EnableBlurBehind(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser)
    {
        pfnSetWindowCompositionAttribute setWindowCompositionAttribute = 
            (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        
        if (setWindowCompositionAttribute)
        {
            // Flags = 2，使用 GradientColor
            ACCENT_POLICY accent = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 2, 0x00000000, 0 };
            
            WINDOWCOMPOSITIONATTRIBDATA data;
            data.Attrib = WCA_ACCENT_POLICY;
            data.pvData = &accent;
            data.cbData = sizeof(accent);
            
            setWindowCompositionAttribute(hwnd, &data);
        }
    }
}

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

    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"User32.dll");
    if (hUser32) 
    {
        typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    float dpiScale = dpi / 96.0f;
    if (dpiScale <= 0) dpiScale = 1.0f;

    float padding = 8.0f * dpiScale;
    float rightPaneWidth = 140.0f * dpiScale;
    float searchHeight = 44.0f * dpiScale;
    float cornerRadius = 6.0f * dpiScale;

    // 背景全透明
    g_pMenuRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    ID2D1SolidColorBrush *pBrush = nullptr;

    D2D1_ROUNDED_RECT windowRRect = D2D1::RoundedRect(
        D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 
        cornerRadius, cornerRadius
    );

    // 黑透明底板
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.2f), &pBrush))) 
    {
        g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pBrush);
        pBrush->Release();
    }

    // 高光渐变层的质感
    // 从亮到按
    ID2D1LinearGradientBrush *pGradientBrush = nullptr;
    ID2D1GradientStopCollection *pGradientStops = nullptr;
    D2D1_GRADIENT_STOP stops[3];
    stops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.6f); 
    stops[0].position = 0.0f;
    stops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);  // 渐变
    stops[1].position = 0.35f;
    stops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f);
    stops[2].position = 1.0f;

    if (SUCCEEDED(g_pMenuRenderTarget->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pGradientStops)))
    {
        g_pMenuRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, (float)height)),
            pGradientStops,
            &pGradientBrush
        );
        pGradientStops->Release();
    }

    if (pGradientBrush)
    {
        g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pGradientBrush);
        pGradientBrush->Release();
    }

    // 左侧白色背景
    // 后续加入文件管理
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &pBrush))) 
    {
        D2D1_RECT_F leftRect = D2D1::RectF(padding, padding, width - padding - rightPaneWidth, height - padding - searchHeight);
        g_pMenuRenderTarget->FillRectangle(leftRect, pBrush);
        
        // 搜索栏
        pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.7f));
        D2D1_RECT_F searchRect = D2D1::RectF(padding, height - padding - searchHeight + 4*dpiScale, width - padding - rightPaneWidth, height - padding);
        g_pMenuRenderTarget->FillRectangle(searchRect, pBrush);
        
        // 搜索栏底色
        pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
        D2D1_RECT_F searchInner = D2D1::RectF(searchRect.left + 2*dpiScale, searchRect.top + 2*dpiScale, searchRect.right - 2*dpiScale, searchRect.bottom - 2*dpiScale);
        g_pMenuRenderTarget->FillRectangle(searchInner, pBrush);

        pBrush->Release();
    }

    // 控制面板
    // 右侧叠加暗色
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.5f), &pBrush))) 
    {
        D2D1_RECT_F rightRect = D2D1::RectF(width - padding - rightPaneWidth, padding, width - padding, height - padding);
        g_pMenuRenderTarget->FillRectangle(rightRect, pBrush);
        pBrush->Release();
    }

    // 边框线
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f), &pBrush))) 
    {
        g_pMenuRenderTarget->DrawRoundedRectangle(windowRRect, pBrush, 1.0f); // 窗口外黑线

        // 左右面板的边框
        D2D1_RECT_F leftRectOutline = D2D1::RectF(padding - 0.5f, padding - 0.5f, width - padding - rightPaneWidth + 0.5f, height - padding - searchHeight + 0.5f);
        g_pMenuRenderTarget->DrawRectangle(leftRectOutline, pBrush, 1.0f);
        
        D2D1_RECT_F rightRectOutline = D2D1::RectF(width - padding - rightPaneWidth - 0.5f, padding - 0.5f, width - padding + 0.5f, height - padding + 0.5f);
        g_pMenuRenderTarget->DrawRectangle(rightRectOutline, pBrush, 1.0f);

        // 黑线内部的偏白线
        pBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f));
        
        // 玻璃内层高亮边缘
        D2D1_ROUNDED_RECT innerWindowHighlight = D2D1::RoundedRect(
            D2D1::RectF(1.5f, 1.5f, width - 1.5f, height - 1.5f), 
            cornerRadius - 1.0f, cornerRadius - 1.0f
        );
        g_pMenuRenderTarget->DrawRoundedRectangle(innerWindowHighlight, pBrush, 1.0f);
        
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
    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"User32.dll");
    if (hUser32) 
    {
        typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    float scale = dpi / 96.0f;
    if (scale <= 0) scale = 1.0f;

    // 根据 DPI 动态缩放
    int menuWidth = (int)(380 * scale);   // 两个基础值的设置
    int menuHeight = (int)(540 * scale);  // 其实并没有什么理由

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
                    // 弹出前重算位置
                    // 可能移动或变过分辨率
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
                    return 0;                         // 理论上应 LBUTTONUP 时再隐藏

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

    if (hWnd) EnableBlurBehind(hWnd);

    return hWnd;
}