#ifndef HOOK_MANAGER_H
#define HOOK_MANAGER_H

#include <windows.h>

// 为 Hook 创建独立线程
DWORD WINAPI HookThread(LPVOID lpParam);

// 捕捉非鼠标消息
LRESULT CALLBACK NewTaskbarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#endif // HOOK_MANAGER_H