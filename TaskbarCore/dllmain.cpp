// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include <windows.h>
#include <stdio.h>

#include "GlobalState.h" // 全局变量
#include "UIManager.h"
#include "HookManager.h"

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

    LONG_PTR oldProc = SetWindowLongPtr(hTaskbar, GWLP_WNDPROC, (LONG_PTR)NewTaskbarProc);

    // 子类化
    if (oldProc)
    {
        OldTaskbarProc = (WNDPROC)oldProc;
        g_hookedWnd = hTaskbar;
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