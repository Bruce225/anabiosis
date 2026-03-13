#include "pch.h"
#include "UIManager.h"
#include "GlobalState.h"
#include "StartOrbWnd.h"
#include "StartMenuWnd.h"

// UI 线程
DWORD WINAPI UIThread(LPVOID lpParam)
{
    HMODULE hModule = (HMODULE)lpParam;

    InitDirect2D();

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

    CleanupDirect2D();

    return 0;
}