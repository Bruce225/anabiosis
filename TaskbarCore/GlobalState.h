#ifndef	GLOBAL_STATE_H
#define GLOBAL_STATE_H

#include <windows.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

// 全局变量神明
extern WNDPROC OldTaskbarProc;
extern HWND g_hookedWnd;
extern HWND g_hStartBtn;        // 开始按钮句柄

extern HHOOK g_hMouseHook;      // 鼠标钩子句柄
extern HANDLE g_hHookThread;    // 保留钩子线程句柄

extern HHOOK g_hKeyboardHook;   // 键盘钩子句柄
extern bool g_bWinKeyDown;     // 记录 Win 键按下与否
extern bool g_bOtherKeyDown;   // 记录其他键同时按下与否

extern HWND g_hMenuWnd;         // 开始菜单窗口句柄
extern HANDLE g_hUIThread;      // UI 线程句柄
extern DWORD g_UIThreadId;      // UI 线程 ID

extern HWND g_hOrbWnd;          // Orb 覆盖窗口句柄

extern ID2D1Factory* g_pD2DFactory;
extern IWICImagingFactory* g_pWICFactory;
void InitDirect2D();
void CleanupDirect2D();

extern IDWriteFactory* g_pDWriteFactory;

// 自定义消息 切换开始菜单显示状态
// 用 1024 以上的来自定义
#define WM_TOGGLE_STARTMENU (WM_USER + 1)  

#endif // GLOBAL_STATE_H