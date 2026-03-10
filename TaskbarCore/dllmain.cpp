// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <windows.h>
#include <stdio.h>

WNDPROC OldTaskbarProc;
HWND g_hookedWnd = NULL;
HHOOK g_hMouseHook = NULL;  // 鼠标钩子的句柄
HWND g_hStartBtn = NULL;    // 开始按钮的句柄
HANDLE g_hHookThread = NULL;   // 保留钩子线程的句柄

// 在 explorer.exe 接收到鼠标事件前拦截用的函数
LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        // 防止 DbgView 刷屏，过滤无关信息
        if ((wParam != WM_MOUSEMOVE) && (wParam != WM_NCHITTEST) && (wParam != WM_MOUSEHOVER))
        {
            MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

            wchar_t msgBuf[128];
            swprintf(msgBuf, 128, L"[Hook] MouseHook captured: 0x%04X at (%d, %d)\n", wParam, pMouseStruct->pt.x, pMouseStruct->pt.y);
            OutputDebugString(msgBuf);

            if (wParam == WM_LBUTTONDOWN)
            {
                if (g_hStartBtn)
                {
                    RECT startRect;
                    if (GetWindowRect(g_hStartBtn, &startRect))
                        if (PtInRect(&startRect, pMouseStruct->pt))
                        {
                            OutputDebugString(L"[Hook] BINGO! Start Button Left Click Intercepted!\n");
                            return 1;
                        }
                }
            }
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

// 为 Hook 创建独立线程
DWORD WINAPI MouseHookThread(LPVOID lpParam)
{
    HMODULE hModule = (HMODULE)lpParam;

    // 在独立的线程中安装全局鼠标 Hook
    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, hModule, 0);
    if (g_hMouseHook)
        OutputDebugString(L"[Hook] LL Mouse Hook Installed in Dedicated Thread!\n");

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

    // 启动专门的 Hook 线程
    if (!g_hHookThread)
    {
        g_hHookThread = CreateThread(NULL, 0, MouseHookThread, hModule, 0, NULL);
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

            // 还原窗口
            if (g_hookedWnd && OldTaskbarProc)
            {
                SetWindowLongPtr(g_hookedWnd, GWLP_WNDPROC, (LONG_PTR)OldTaskbarProc);
                OutputDebugString(L"[Hook] Original WndProc restored\n");
            }

            // 卸载鼠标 Hook
            if (g_hMouseHook)
            {
                UnhookWindowsHookEx(g_hMouseHook);
                g_hMouseHook = NULL;
                OutputDebugString(L"[Hook] Mouse Hook Unhooked\n");
            }

            // 卸载 Hook 线程
            // 使用 WaitForSingleObject 死等会出问题
            if (g_hHookThread)
            {
                // 给 GetMessage 发送退出消息
                DWORD threadId = GetThreadId(g_hHookThread);
                PostThreadMessage(threadId, WM_QUIT, 0, 0);
                
                // 关闭句柄
                CloseHandle(g_hHookThread);
                g_hHookThread = NULL;
                OutputDebugString(L"[Hook] Hook Thread stopped\n");
            }

            break;
        }
    }
    return TRUE;
}