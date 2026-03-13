#include "pch.h"
#include "HookManager.h"
#include "GlobalState.h"
#include <stdio.h>

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
    // 拦截窗口位置变化消息
    if (uMsg == WM_WINDOWPOSCHANGED)
    {
        // 任务栏完成置顶
        LRESULT ret = CallWindowProc(OldTaskbarProc, hwnd, uMsg, wParam, lParam);

        // Orb 提到最顶层
        if (g_hOrbWnd)
        {
            RECT r = { 0 };
            if (g_hStartBtn) GetWindowRect(g_hStartBtn, &r);
            SetWindowPos(g_hOrbWnd, HWND_TOPMOST, r.left, r.top, 0, 0,
                SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
        }
        return ret;
    }

    return CallWindowProc(OldTaskbarProc, hwnd, uMsg, wParam, lParam);
}