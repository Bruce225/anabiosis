#include "pch.h"
#include "StartOrbWnd.h"
#include "GlobalState.h"

// Orb 按钮窗口过程
LRESULT CALLBACK OrbWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HBRUSH hBg = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            // 临时 Orb
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(80, 160, 255));
            HBRUSH hOrb = CreateSolidBrush(RGB(0, 100, 210));
            SelectObject(hdc, hPen);
            SelectObject(hdc, hOrb);
            Ellipse(hdc, rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3);
            DeleteObject(hPen);
            DeleteObject(hOrb);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            // 直接处理
            // 不再依赖鼠标 Hook
            if (g_hMenuWnd)
                PostMessage(g_hMenuWnd, WM_TOGGLE_STARTMENU, 0, 0);
            return 0;
        }

    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 Orb 覆盖窗口
HWND CreateOrbWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"Win7OrbButton";

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
        className, L"Win7Orb",
        WS_POPUP,
        r.left, r.top, w, h,
        hTaskbar,
        NULL, hInstance, NULL
    );

    if (hWnd) ShowWindow(hWnd, SW_SHOW);
    return hWnd;
}