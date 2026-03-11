// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <windows.h>
#include <stdio.h>

WNDPROC OldTaskbarProc;
HWND g_hookedWnd = NULL;
HWND g_hStartBtn = NULL;        // 开始按钮句柄

HHOOK g_hMouseHook = NULL;      // 鼠标钩子句柄
HANDLE g_hHookThread = NULL;    // 保留钩子线程句柄

HHOOK g_hKeyboardHook = NULL;   // 键盘钩子句柄
bool g_bWinKeyDown = false;     // 记录 Win 键按下与否
bool g_bOtherKeyDown = false;   // 记录其他键同时按下与否

HWND g_hMenuWnd = NULL;         // 开始菜单窗口句柄
HANDLE g_hUIThread = NULL;      // UI 线程句柄
DWORD g_UIThreadId = 0;         // UI 线程 ID

HWND g_hOrbWnd = NULL;          // Orb 覆盖窗口句柄

// 自定义消息 切换开始菜单显示状态
// 用 1024 以上的来自定义
#define WM_TOGGLE_STARTMENU (WM_USER + 1)  

// Orb 按钮窗口过程
LRESULT CALLBACK OrbWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            SetTimer(hwnd, 1, 50, NULL);
            return 0;

        case WM_TIMER: 
        {
            // SWP_SHOWWINDOW 同时保证可见 + 置于最顶
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            break;
        }

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
    if (g_hStartBtn)
        GetWindowRect(g_hStartBtn, &r);

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

// UI 线程
DWORD WINAPI UIThread(LPVOID lpParam)
{
    HMODULE hModule = (HMODULE)lpParam;

    g_hMenuWnd = CreateStartMenuWindow(hModule);
    g_hOrbWnd = CreateOrbWindow(hModule);

    if (!g_hMenuWnd || !g_hOrbWnd)
    {
        OutputDebugString(L"[Hook] Failed to create start menu window\n");
        return 1;
    }
    OutputDebugString(L"[Hook] Start menu window created\n");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if (g_hMenuWnd)
    {
        DestroyWindow(g_hMenuWnd);
        g_hMenuWnd = NULL;
    }

    if (g_hOrbWnd) 
    { 
        DestroyWindow(g_hOrbWnd);  
        g_hOrbWnd = NULL; 
    }
    return 0;
}

// 在 explorer.exe 接收到鼠标事件前拦截用的函数
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        // 防止 DbgView 刷屏，过滤无关信息
        if ((wParam != WM_MOUSEMOVE) && 
            (wParam != WM_NCHITTEST) && 
            (wParam != WM_MOUSEHOVER))
        {
            MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

            wchar_t msgBuf[128];
            swprintf(msgBuf, 128, L"[Hook] MouseHook captured: 0x%04X at (%d, %d)\n", wParam, pMouseStruct->pt.x, pMouseStruct->pt.y);
            OutputDebugString(msgBuf);

            //if (wParam == WM_LBUTTONDOWN)
            //{
            //    if (g_hStartBtn)
            //    {
            //        RECT startRect;
            //        if (GetWindowRect(g_hStartBtn, &startRect))
            //            if (PtInRect(&startRect, pMouseStruct->pt))
            //            {
            //                OutputDebugString(L"[Hook] BINGO! Start Button Left Click Intercepted!\n");
            //                if (g_hMenuWnd)
            //                    PostMessage(g_hMenuWnd, WM_TOGGLE_STARTMENU, 0, 0);
            //                return 1;
            //            }
            //    }
            //}
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

// 在 explorer.exe 接收到 Win 键前拦截用的函数
LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* pKbd = (KBDLLHOOKSTRUCT*)lParam;
        bool isWinKey = ((pKbd->vkCode == VK_LWIN) || (pKbd->vkCode == VK_RWIN));

        // Win 键按下时重置状态
        // Win 键松开时启用拦截
        if ((wParam == WM_KEYDOWN) || (wParam == WM_SYSKEYDOWN))
        {
            if (isWinKey)
            {
                // 仅按下 Win 键，标记并重置 OtherKeyDown
                // 第一次按下时重置
                if (!g_bWinKeyDown) 
                {
                    g_bWinKeyDown = true;
                    g_bOtherKeyDown = false;
                }
            }
                else if (g_bWinKeyDown) g_bOtherKeyDown = true; // 按下组合键   
        }
            else if ((wParam == WM_KEYUP) || (wParam == WM_SYSKEYUP))
            {
                if (isWinKey)
                {
                    g_bWinKeyDown = false;

                    // 若松开 Win 键且不曾按过其他键
                    if (!g_bOtherKeyDown)
                    {
                        OutputDebugString(L"[Hook] BINGO! Pure Win Key Press Intercepted!\n");

                        // 
                        if (g_hMenuWnd)
                            PostMessage(g_hMenuWnd, WM_TOGGLE_STARTMENU, 0, 0);

                        // 伪造按键防止 Win 键卡死
                        INPUT inputs[2] = {};

                        // 按下伪造键
                        inputs[0].type = INPUT_KEYBOARD;
                        inputs[0].ki.wVk = 0x88; // 某系统未定义的虚拟键码
                        inputs[0].ki.dwFlags = 0;

                        // 松开伪造键
                        inputs[1].type = INPUT_KEYBOARD;
                        inputs[1].ki.wVk = 0x88;
                        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

                        SendInput(2, inputs, sizeof(INPUT));  // 发送按下抬起事件
                    }
                }
            }
    }

    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// 为 Hook 创建独立线程
DWORD WINAPI HookThread(LPVOID lpParam)
{
    HMODULE hModule = (HMODULE)lpParam;

    // 安装鼠标 Hook
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hModule, 0);
    if (g_hMouseHook)
        OutputDebugString(L"[Hook] LL Mouse Hook Installed!\n");

    // 安装键盘 Hook
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, hModule, 0);
    if (g_hKeyboardHook)
        OutputDebugString(L"[Hook] LL Keyboard Hook Installed!\n");

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 当收到退出指示时卸载 Hoiok 
    if (g_hMouseHook)
    {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }

    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }

    return 0;
}

// 捕捉非鼠标消息
LRESULT CALLBACK NewTaskbarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    /*
    if (uMsg == WM_PAINT)
    {
        OutputDebugString(L"[Hook] WM_PAINT received in Shell_TrayWnd!\n");
        LRESULT ret = CallWindowProc(OldTaskbarProc, hwnd, uMsg, wParam, lParam);

        HDC hdc = GetWindowDC(hwnd);
        RECT rect;
        GetWindowRect(hwnd, &rect);
        if (hdc)
        {
            HBRUSH hBrush = CreateSolidBrush(RGB(255, 0, 0)); // 红色
            if (hBrush)
            {
                RECT testRect = { 0, 0, 100, 40 };
                FillRect(hdc, &testRect, hBrush);
                DeleteObject(hBrush);
            }
            ReleaseDC(hwnd, hdc);
        }
        else OutputDebugString(L"[Hook] Failed\n");

        return ret;
    }


    if (uMsg == WM_MOUSEMOVE)
    {
        OutputDebugString(L"[Hook] Mouse is moving over Shell_TrayWnd!\n");
    }

    if (uMsg == WM_LBUTTONDOWN)
    {
        OutputDebugString(L"[Hook] Left Click!\n");
        return 0;
    }
    */

    // 测试 Hook
    if ((uMsg != WM_NCHITTEST) && (uMsg != WM_SETCURSOR) && (uMsg != WM_MOUSEMOVE) && (uMsg != WM_TIMER))
    {
        wchar_t msgBuf[128];
        swprintf(msgBuf, 128, L"[Hook] Msg intercepted: 0x%04X\n", uMsg);
        OutputDebugString(msgBuf);
    }

    return CallWindowProc(OldTaskbarProc, hwnd, uMsg, wParam, lParam);
}

// 启动劫持函数
void StartHijack(HMODULE hModule)
{
    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);
    if (!hTaskbar)
    {
        OutputDebugString(L"[Hook] Shell_TrayWnd not found\n");
        return;
    }

    // 寻找开始菜单按钮，其类名为 "Start". 用 Spy++ 发现的.
    g_hStartBtn = FindWindowEx(hTaskbar, NULL, L"Start", NULL);

    if (!g_hStartBtn)
    {
        OutputDebugString(L"[Hook] Start button not found\n");
        return;
    }

    if (OldTaskbarProc)
    {
        OutputDebugString(L"[Hook] Already hijacked\n");
        return;
    }

    LONG_PTR oldProc = SetWindowLongPtr(g_hStartBtn, GWLP_WNDPROC, (LONG_PTR)NewTaskbarProc);

    // 子类化
    if (oldProc)
    {
        OldTaskbarProc = (WNDPROC)oldProc;
        g_hookedWnd = g_hStartBtn;
    }

    // 启动 UI 线程
    g_hUIThread = CreateThread(NULL, 0, UIThread, hModule, 0, &g_UIThreadId);

    // 启动专门的 Hook 线程
    if (!g_hHookThread)
    {
        g_hHookThread = CreateThread(NULL, 0, HookThread, hModule, 0, NULL);
    }

    OutputDebugString(L"[Hook] Hijack Started!\n");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
            OutputDebugString(L"[Hook] DLL_PROCESS_ATTACH\n");
            StartHijack(hModule);
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            OutputDebugString(L"[Hook] DLL_PROCESS_DETACH\n");

            // 卸载鼠标和键盘 Hook
            if (g_hMouseHook)
            {
                UnhookWindowsHookEx(g_hMouseHook);
                g_hMouseHook = NULL;
                OutputDebugString(L"[Hook] Mouse Hook Unhooked\n");
            }

            if (g_hKeyboardHook)
            {
                UnhookWindowsHookEx(g_hKeyboardHook);
                g_hKeyboardHook = NULL;
                OutputDebugString(L"[Hook] Keyboard Hook Unhooked\n");
            }

            // 还原窗口
            if (g_hookedWnd && OldTaskbarProc)
            {
                SetWindowLongPtr(g_hookedWnd, GWLP_WNDPROC, (LONG_PTR)OldTaskbarProc);
                OutputDebugString(L"[Hook] Original WndProc restored\n");
            }

            // 停止 UI 线程
            if (g_hUIThread)
            {
                PostThreadMessage(GetThreadId(g_hUIThread), WM_QUIT, 0, 0);
                WaitForSingleObject(g_hUIThread, 100); 
                CloseHandle(g_hUIThread);
                g_hUIThread = NULL;
                OutputDebugString(L"[Hook] UI Thread stopped\n");
            }

            // 停止 Hook 线程
            if (g_hHookThread)
            {
                PostThreadMessage(GetThreadId(g_hHookThread), WM_QUIT, 0, 0);
                WaitForSingleObject(g_hHookThread, 100);
                CloseHandle(g_hHookThread);
                g_hHookThread = NULL;
                OutputDebugString(L"[Hook] Hook Thread stopped\n");
            }

            break;
        }
    }
    return TRUE;
}