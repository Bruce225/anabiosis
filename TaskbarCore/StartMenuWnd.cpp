#include "pch.h"
#include "StartMenuWnd.h"
#include "GlobalState.h"

// 开始菜单窗口
LRESULT CALLBACK StartMenuProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_TOGGLE_STARTMENU)
    {
        if (IsWindowVisible(hwnd))
        {
            ShowWindow(hwnd, SW_HIDE);
            OutputDebugString(L"[Hook] Start menu hidden\n");
        }
        else
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            OutputDebugString(L"[Hook] Start menu shown\n");
        }
        return 0;
    }

    // 焦点管理
    // 点击空白或 ECS 隐藏菜单
    if (uMsg == WM_ACTIVATE)
    {
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            if (IsWindowVisible(hwnd))
            {
                ShowWindow(hwnd, SW_HIDE);
                OutputDebugString(L"[Hook] Start menu auto-hidden due to focus loss\n");
            }
        }
        return 0;
    }

    if ((uMsg == WM_KEYDOWN) && (wParam == VK_ESCAPE))
    {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 StartMenu 窗口
HWND CreateStartMenuWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"CustomStartMenu";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = StartMenuProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // 设置光标
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    // 获取任务栏位置
    RECT taskbarRect = {};
    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);
    if (hTaskbar) GetWindowRect(hTaskbar, &taskbarRect);

    int menuWidth = 320;
    int menuHeight = 500;
    int x = taskbarRect.left + 16;
    int y = taskbarRect.top - menuHeight;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,  // 置顶 & 不在任务栏
        className,
        L"CustomStartMenu",
        WS_POPUP,
        x, y, menuWidth, menuHeight,
        NULL, NULL, hInstance, NULL
    );

    return hWnd;
}