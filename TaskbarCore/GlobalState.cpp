#include "pch.h"
#include "GlobalState.h"

// 全局变量定义
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