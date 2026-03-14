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

ID2D1Factory* g_pD2DFactory = nullptr;
IWICImagingFactory* g_pWICFactory = nullptr;

void InitDirect2D()
{
    // 初始化 COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (!g_pD2DFactory)
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);

    if (!g_pWICFactory)
        CoCreateInstance(
            CLSID_WICImagingFactory,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory,
            (LPVOID*)&g_pWICFactory
        );
}

void CleanupDirect2D()
{
    if (g_pWICFactory)
    {
        g_pWICFactory->Release();
        g_pWICFactory = nullptr;
    }

    if (g_pD2DFactory)
    {
        g_pD2DFactory->Release();
        g_pD2DFactory = nullptr;
    }

    CoUninitialize();
}