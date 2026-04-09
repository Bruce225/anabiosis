#ifndef START_MENU_WND_H
#define	START_MENU_WND_H

#include <windows.h>
#include <string>
#include <vector>

enum class ItemPosition 
{ 
    LeftPane, 
    RightPane, 
    BottomAction 
};

struct StartMenuItem
{
    std::wstring Title;
    ItemPosition Position;
    D2D1_RECT_F Bounds = { 0 };      // 绘制时实时计算并赋值
    bool IsHovered = false;          // 悬停状态
    std::wstring SubTitle;           // 副标题
    bool IsSeparator = false;        // 是否为分隔线
    bool IsAllPrograms = false;      // 专为底部"所有程序"栏
    ID2D1Bitmap* pIconBmp = nullptr;
};

// 全局存储
extern std::vector<StartMenuItem> g_LeftItems;
extern std::vector<StartMenuItem> g_RightItems;

// 数据初始化
void InitMenuItems();

// 创建 StartMenu 窗口
HWND CreateStartMenuWindow(HINSTANCE hInstance);

#endif // START_MENU_WND_H