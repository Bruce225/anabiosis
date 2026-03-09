#include <windows.h>
#include <iostream>

bool InjectDLL(DWORD processId, const char* dllPath)
{
    // 打开 explorer.exe
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return false;

    // 在 explorer 内存空间里申请一块空地，用来放 DLL 的路径字符串
    LPVOID pRemotePath = VirtualAllocEx(hProcess, NULL, strlen(dllPath) + 1, MEM_COMMIT, PAGE_READWRITE);

    // 把 DLL 路径写进去
    WriteProcessMemory(hProcess, pRemotePath, dllPath, strlen(dllPath) + 1, NULL);

    // 在 explorer 里启动新线程加载 DLL
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "LoadLibraryA"),
        pRemotePath, 0, NULL);

    if (hThread)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

int main()
{
    // 获取 explorer.exe 的进程 ID 
    HWND hwnd = FindWindow(L"Shell_TrayWnd", NULL);
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);

    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash != nullptr)
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash - dllPath) - 1, "TaskbarCore.dll");

    if (InjectDLL(pid, dllPath)) printf("注入成功\n");
        else printf("注入失败，错误码：%d\n", GetLastError());

    system("pause");
    return 0;
}