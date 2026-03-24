#include <windows.h>
#include <iostream>
#include <TLHelp32.h>
#include <conio.h>

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

bool EjectDLL(DWORD processId, const char* dllName)
{
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return false;

    // dllName 是 const char* 类型
    // 故窄转宽，由于 VS2026 未定义 MODULEENTRY32A
    // MODULEENTRY32A 为多字节版本，VS 不知为何未定义，令人汗颜
    wchar_t wDllName[MAX_PATH];
    size_t convertedChars = 0;
    mbstowcs_s(&convertedChars, wDllName, MAX_PATH, dllName, _TRUNCATE);

    bool unloaded = false;
    int maxAttempts = 10; // 多试试

    for (int i = 1; i <= maxAttempts; i++)
    {
        // 重新获取目标进程模块快照
        // 检查 DLL 是否仍然加载
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
        if (hSnapshot == INVALID_HANDLE_VALUE) break;

        MODULEENTRY32W me32;
        me32.dwSize = sizeof(MODULEENTRY32W);
        HMODULE hModuleInRemoteProcess = NULL;

        if (Module32FirstW(hSnapshot, &me32))
        {
            do
            {
                if (_wcsicmp(me32.szModule, wDllName) == 0)
                {
                    hModuleInRemoteProcess = me32.hModule;
                    break;
                }
            } 
            while (Module32NextW(hSnapshot, &me32));
        }
        CloseHandle(hSnapshot);

        if (!hModuleInRemoteProcess)
        {
            // 找不到 DLL（卸载成功）
            unloaded = true;
            break;
        }

        // 若引用计数依然 > 0, 执行 FreeLibrary 减 1
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "FreeLibrary"),
            (LPVOID)hModuleInRemoteProcess, 0, NULL);

        if (hThread)
        {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
            // 留出时间触发 DLL_PROCESS_DETACH
            Sleep(50);
        }
            else break;
    }
    
    CloseHandle(hProcess);
    return unloaded;
}

int main()
{
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
	const char* targetDllName = "TaskbarCore.dll";

    if (lastSlash != nullptr)
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash - dllPath) - 1, "TaskbarCore.dll");

    printf("按'q'键卸载 DLL!\n\n");

    DWORD lastInjectedPid = 0;

    while (true)
    {
        // 检测输入
        if (_kbhit())
        {
            char ch = _getch();
            if (ch == 'q' || ch == 'Q')
            {
                break;
            }
        }
        // 查找 explorer.exe
        HWND hwnd = FindWindow(L"Shell_TrayWnd", NULL);
        if (hwnd)
        {
            DWORD pid;
            GetWindowThreadProcessId(hwnd, &pid);

            // 发现新的 explorer.exe PID
            if (pid != 0 && pid != lastInjectedPid)
            {
                Sleep(1500);
                if (InjectDLL(pid, dllPath))
                {
                    printf("成功注入 explorer.exe (新 PID: %lu)\n", pid);
                    lastInjectedPid = pid;
                }
                    else
                    {
                        printf("注入失败，错误码: %lu\n", GetLastError());
                        Sleep(2000); 
                    }
            }
        }
            else lastInjectedPid = 0;

        Sleep(500);
    }

    if (lastInjectedPid != 0)
    {
        printf("\n正在卸载 DLL\n");
        if (EjectDLL(lastInjectedPid, targetDllName)) printf("卸载成功\n");
            else printf("卸载失败\n");
    }

    system("pause");
    return 0;
}