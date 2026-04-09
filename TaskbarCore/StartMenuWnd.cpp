#include "pch.h"
#include <shellapi.h>
#include <ShlObj.h>
#include <vector>
#include <algorithm>
#include "StartMenuWnd.h"
#include "GlobalState.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <powrprof.h>
#pragma comment(lib, "PowrProf.lib")

ID2D1Bitmap* g_pAvatarBitmap = nullptr; 
ID2D1RenderTarget* g_pMenuRenderTarget = nullptr;
IWICBitmap* g_pMenuWicBitmap = nullptr;
int g_MenuWidth = 0;
int g_MenuHeight = 0;

HWND g_hAvatarWnd = NULL; // 头像窗口句柄
ID2D1RenderTarget* g_pAvatarRenderTarget = nullptr;
IWICBitmap* g_pAvatarWicBitmap = nullptr;

std::vector<StartMenuItem> g_LeftItems;
std::vector<StartMenuItem> g_RightItems;

// 底部控制按钮
D2D1_RECT_F g_PowerBtnBounds = { 0, 0, 0, 0 };
D2D1_RECT_F g_LockBtnBounds = { 0, 0, 0, 0 };
D2D1_RECT_F g_ArrowBtnBounds = { 0, 0, 0, 0 };

bool g_bPowerHovered = false;
bool g_bLockHovered = false;
bool g_bArrowHovered = false;

ID2D1Bitmap* g_pBtnNormal = nullptr;
ID2D1Bitmap* g_pBtnHover = nullptr;
ID2D1Bitmap* g_pBtnPressed = nullptr;

bool g_bPowerPressed = false;
bool g_bLockPressed = false;
bool g_bArrowPressed = false;

// 文本格式句柄
IDWriteTextFormat* g_pLeftTextFormat = nullptr;
IDWriteTextFormat* g_pLeftSubTextFormat = nullptr;  // 副标题
IDWriteTextFormat* g_pLeftBoldTextFormat = nullptr;  // 加粗
IDWriteTextFormat* g_pRightTextFormat = nullptr;

ID2D1Bitmap* g_pCachedBgBitmap = nullptr; // 缓存背景模糊位图用
ID2D1Bitmap* g_pAllProgramsArrow = nullptr; // “所有程序”小箭头

// 搜索框相关状态
ID2D1Bitmap* g_pSearchIcon = nullptr;
ID2D1Bitmap* g_pSearchClearBg = nullptr;
IDWriteTextFormat* g_pSearchTextFormat = nullptr;       // 占位符
IDWriteTextFormat* g_pSearchInputTextFormat = nullptr;  // 输入态
std::wstring g_SearchText = L"";                        // 当前输入文字
bool g_bSearchFocused = false;
bool g_bSearchIconHovered = false;                      // 叉叉是否悬停
bool g_bSearchIconPressed = false;
float g_SearchBgAlpha = 0.0f;                           // 透明度动画
float g_SearchCrossAlpha = 0.0f;
D2D1_RECT_F g_SearchBoxBounds = { 0 };                  // 鼠标命中测试
D2D1_RECT_F g_SearchIconBounds = { 0 };                 // 搜索图标命中测试

void InitMenuItems()
{
    if (!g_LeftItems.empty()) return;

    g_LeftItems.push_back({ L"Internet", ItemPosition::LeftPane, {0}, false, L"Internet Explorer" });
    g_LeftItems.push_back({ L"电子邮件", ItemPosition::LeftPane, {0}, false, L"Windows Mail" });

    g_LeftItems.push_back({ L"-", ItemPosition::LeftPane, {0}, false, L"", true });
    
    g_LeftItems.push_back({ L"欢迎中心", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Windows 照片库", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Windows 移动中心", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Windows 会议室", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Windows Live Messenger 下载", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Windows Media Player", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Foo", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"Bar", ItemPosition::LeftPane });
    g_LeftItems.push_back({ L"FooBar2000", ItemPosition::LeftPane });

    g_LeftItems.push_back({ L"-", ItemPosition::LeftPane, {0}, false, L"", true });

    g_LeftItems.push_back({ L"所有程序", ItemPosition::LeftPane, {0}, false, L"", false, true });

    g_RightItems.push_back({ L"Administrator", ItemPosition::RightPane });
    g_RightItems.push_back({ L"文档", ItemPosition::RightPane });
    g_RightItems.push_back({ L"图片", ItemPosition::RightPane });
    g_RightItems.push_back({ L"音乐", ItemPosition::RightPane });
    g_RightItems.push_back({ L"-", ItemPosition::RightPane });
    g_RightItems.push_back({ L"最近使用的项目", ItemPosition::RightPane });
    g_RightItems.push_back({ L"计算机", ItemPosition::RightPane });
    g_RightItems.push_back({ L"网络", ItemPosition::RightPane });
    g_RightItems.push_back({ L"连接到", ItemPosition::RightPane });
    g_RightItems.push_back({ L"-", ItemPosition::RightPane });
    g_RightItems.push_back({ L"控制面板", ItemPosition::RightPane });
    g_RightItems.push_back({ L"默认程序", ItemPosition::RightPane });
    g_RightItems.push_back({ L"帮助和支持", ItemPosition::RightPane });
}

// 读取头像文件
bool LoadAvatarBitmap(HWND hwnd)
{
    if (!g_pMenuRenderTarget || g_pAvatarBitmap) return true;

    wchar_t szPath[MAX_PATH];
    HMODULE hModule = NULL;

    // 获取 DLL 句柄
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&LoadAvatarBitmap,
        &hModule
    );

    GetModuleFileNameW(hModule, szPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(szPath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    wcscat_s(szPath, MAX_PATH, L"\\source\\Avatar.bmp");

    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pSource = nullptr;
    IWICFormatConverter* pConverter = nullptr;

    // 创建解码器
    HRESULT hr = g_pWICFactory->CreateDecoderFromFilename(
        szPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);

    if (SUCCEEDED(hr))
        hr = pDecoder->GetFrame(0, &pSource);

    if (SUCCEEDED(hr))
    {
        // 转换格式为 D2D 32bppPBGRA
        hr = g_pWICFactory->CreateFormatConverter(&pConverter);
        if (SUCCEEDED(hr))
        {
            hr = pConverter->Initialize(
                pSource, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
        }
    }

    if (SUCCEEDED(hr))
    {
        // 创建 D2D 位图
        hr = g_pMenuRenderTarget->CreateBitmapFromWicBitmap(pConverter, NULL, &g_pAvatarBitmap);
    }

    // 释放 WIC 资源
    if (pDecoder) pDecoder->Release();
    if (pSource) pSource->Release();
    if (pConverter) pConverter->Release();

    return SUCCEEDED(hr);
}

// 加载透明位图
bool LoadSpriteBitmap(LPCWSTR filename, ID2D1Bitmap** ppBitmap)
{
    if (!g_pMenuRenderTarget || *ppBitmap) return true;

    wchar_t szPath[MAX_PATH];
    HMODULE hModule = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
        (LPCWSTR)&LoadSpriteBitmap, &hModule);
    GetModuleFileNameW(hModule, szPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(szPath, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    wcscat_s(szPath, MAX_PATH, L"\\source\\");
    wcscat_s(szPath, MAX_PATH, filename);

    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pSource = nullptr;
    IWICFormatConverter* pConverter = nullptr;

    HRESULT hr = g_pWICFactory->CreateDecoderFromFilename(szPath, NULL, GENERIC_READ, 
        WICDecodeMetadataCacheOnLoad, &pDecoder);
    if (SUCCEEDED(hr)) hr = pDecoder->GetFrame(0, &pSource);
    if (SUCCEEDED(hr)) 
    {
        hr = g_pWICFactory->CreateFormatConverter(&pConverter);
        if (SUCCEEDED(hr)) hr = pConverter->Initialize(pSource, GUID_WICPixelFormat32bppPBGRA, 
            WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
    }
    if (SUCCEEDED(hr))
        hr = g_pMenuRenderTarget->CreateBitmapFromWicBitmap(pConverter, NULL, ppBitmap);

    if (pDecoder) pDecoder->Release();
    if (pSource) pSource->Release();
    if (pConverter) pConverter->Release();
    return SUCCEEDED(hr);
}

// 模糊拟合算法
void FastBoxBlur(BYTE* pPixels, int width, int height, int radius)
{
    if (radius < 1) return;
    int stride = width * 4;
    std::vector<BYTE> temp(stride * height);
    int count = 2 * radius + 1;

    // 行处理
    for (int y = 0; y < height; ++y)
    {
        int sumB = 0, sumG = 0, sumR = 0;
        int rowOffset = y * stride;

        // 初始化活动窗口
        for (int k = -radius; k <= radius; ++k)
        {
            int px = (std::max)(0, (std::min)(width - 1, k));
            sumB += pPixels[rowOffset + px * 4];
            sumG += pPixels[rowOffset + px * 4 + 1];
            sumR += pPixels[rowOffset + px * 4 + 2];
        }
        for (int x = 0; x < width; ++x)
        {
            temp[rowOffset + x * 4] = sumB / count;
            temp[rowOffset + x * 4 + 1] = sumG / count;
            temp[rowOffset + x * 4 + 2] = sumR / count;
            temp[rowOffset + x * 4 + 3] = 255;

            // 窗口滑动
            int leftPx = (std::max)(0, x - radius);
            int rightPx = (std::min)(width - 1, x + radius + 1);
            sumB += pPixels[rowOffset + rightPx * 4] - pPixels[rowOffset + leftPx * 4];
            sumG += pPixels[rowOffset + rightPx * 4 + 1] - pPixels[rowOffset + leftPx * 4 + 1];
            sumR += pPixels[rowOffset + rightPx * 4 + 2] - pPixels[rowOffset + leftPx * 4 + 2];
        }
    }

    // 列处理
    for (int x = 0; x < width; ++x)
    {
        int sumB = 0, sumG = 0, sumR = 0;
        for (int k = -radius; k <= radius; ++k)
        {
            int py = (std::max)(0, (std::min)(height - 1, k));
            int idx = py * stride + x * 4;
            sumB += temp[idx];
            sumG += temp[idx + 1];
            sumR += temp[idx + 2];
        }
        for (int y = 0; y < height; ++y)
        {
            int outIdx = y * stride + x * 4;
            pPixels[outIdx] = sumB / count;
            pPixels[outIdx + 1] = sumG / count;
            pPixels[outIdx + 2] = sumR / count;
            pPixels[outIdx + 3] = 255; // 补满不透明度

            // 窗口滑动
            int topPy = (std::max)(0, y - radius);
            int bottomPy = (std::min)(height - 1, y + radius + 1);
            int topIdx = topPy * stride + x * 4;
            int bottomIdx = bottomPy * stride + x * 4;

            sumB += temp[bottomIdx] - temp[topIdx];
            sumG += temp[bottomIdx + 1] - temp[topIdx + 1];
            sumR += temp[bottomIdx + 2] - temp[topIdx + 2];
        }
    }
}

void UpdateBackgroundBlur(HWND hwnd, int width, int height)
{
    if ((width <= 0) || (height <= 0) || !g_pMenuRenderTarget) return;

    // 1/2 降采样
    int downWidth = width / 2;
    int downHeight = height / 2;

    HDC hdcBgScreen = GetDC(NULL);
    HDC hdcBgMem = CreateCompatibleDC(hdcBgScreen);

    BITMAPINFO bmiBg = { 0 };
    bmiBg.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiBg.bmiHeader.biWidth = downWidth;
    bmiBg.bmiHeader.biHeight = -downHeight;
    bmiBg.bmiHeader.biPlanes = 1;
    bmiBg.bmiHeader.biBitCount = 32;
    bmiBg.bmiHeader.biCompression = BI_RGB;

    void* pBgBits = nullptr;
    HBITMAP hBgBitmap = CreateDIBSection(hdcBgScreen, &bmiBg, DIB_RGB_COLORS, &pBgBits, NULL, 0);
    HBITMAP hOldBg = (HBITMAP)SelectObject(hdcBgMem, hBgBitmap);

    RECT rcBgRect;
    GetWindowRect(hwnd, &rcBgRect);

    // 捕获前临时忽略
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    // 快速缩放
    SetStretchBltMode(hdcBgMem, COLORONCOLOR);
    StretchBlt(hdcBgMem, 0, 0, downWidth, downHeight,
        hdcBgScreen, rcBgRect.left, rcBgRect.top, width, height, SRCCOPY);

    // 捕获后恢复
    SetWindowDisplayAffinity(hwnd, WDA_NONE);

    // 模糊半径减半
    FastBoxBlur((BYTE*)pBgBits, downWidth, downHeight, 4);
    FastBoxBlur((BYTE*)pBgBits, downWidth, downHeight, 4);

    if (g_pCachedBgBitmap)
    {
        g_pCachedBgBitmap->Release();
        g_pCachedBgBitmap = nullptr;
    }

    D2D1_BITMAP_PROPERTIES bgProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    g_pMenuRenderTarget->CreateBitmap(D2D1::SizeU(downWidth, downHeight), pBgBits, downWidth * 4, bgProps, &g_pCachedBgBitmap);

    SelectObject(hdcBgMem, hOldBg);
    DeleteObject(hBgBitmap);
    DeleteDC(hdcBgMem);
    ReleaseDC(NULL, hdcBgScreen);
}

// 渲染开始菜单
void RenderStartMenu(HWND hwnd) 
{
    if (!g_pD2DFactory || !g_pWICFactory) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if ((width == 0) || (height == 0)) return;

    // 尺寸变化
    if (g_pMenuWicBitmap && ((width != g_MenuWidth) || (height != g_MenuHeight))) 
    {
        if (g_pMenuRenderTarget) 
        {
            g_pMenuRenderTarget->Release();
            g_pMenuRenderTarget = nullptr;
        }
        g_pMenuWicBitmap->Release();
        g_pMenuWicBitmap = nullptr;
    }

    if (!g_pMenuWicBitmap) 
    {
        HRESULT hr = g_pWICFactory->CreateBitmap(
            width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
            &g_pMenuWicBitmap);
        if (FAILED(hr)) return;
        g_MenuWidth = width;
        g_MenuHeight = height;
    }

    if (!g_pMenuRenderTarget) 
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);

        HRESULT hr = g_pD2DFactory->CreateWicBitmapRenderTarget(
            g_pMenuWicBitmap, props, &g_pMenuRenderTarget);
        if (FAILED(hr)) return;
    }

    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"User32.dll");
    if (hUser32)
    {
        typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    float dpiScale = dpi / 96.0f;
    if (dpiScale <= 0) dpiScale = 1.0f;

    float padding = 10.0f * dpiScale;
    float rightPaneWidth = 140.0f * dpiScale;
    float searchHeight = 28.0f * dpiScale;
    float cornerRadius = 6.0f * dpiScale;

    g_pMenuRenderTarget->BeginDraw();

    // 背景全透明
    g_pMenuRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    ID2D1SolidColorBrush *pBrush = nullptr;

    D2D1_ROUNDED_RECT windowRRect = D2D1::RoundedRect(
        D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 
        cornerRadius, cornerRadius
    );

    // 铺上模糊背景
    if (g_pCachedBgBitmap)
    {
        ID2D1BitmapBrush* pBgBrush = nullptr;

        // 渲染时拉伸回原尺寸
        D2D1_BITMAP_BRUSH_PROPERTIES brushProps = D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

        // 放大 2 倍
        D2D1_BRUSH_PROPERTIES baseProps = D2D1::BrushProperties(
            1.0f, D2D1::Matrix3x2F::Scale(2.0f, 2.0f));

        if (SUCCEEDED(g_pMenuRenderTarget->CreateBitmapBrush(g_pCachedBgBitmap, &brushProps, &baseProps, &pBgBrush)))
        {
            g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pBgBrush);
            pBgBrush->Release();
        }
    }

    // 黑透明底板
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &pBrush))) 
    {
        g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pBrush);
        pBrush->Release();
    }

    // 高光渐变层的质感
    // 从亮到按
    ID2D1LinearGradientBrush *pGradientBrush = nullptr;
    ID2D1GradientStopCollection *pGradientStops = nullptr;
    D2D1_GRADIENT_STOP stops[3];
    stops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f); 
    stops[0].position = 0.0f;
    stops[1].color = D2D1::ColorF(0.6f, 0.6f, 0.6f, 0.0f);  // 渐变
    stops[1].position = 0.3f;
    stops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f);
    stops[2].position = 1.0f;

    if (SUCCEEDED(g_pMenuRenderTarget->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pGradientStops)))
    {
        g_pMenuRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, (float)height)),
            pGradientStops,
            &pGradientBrush
        );
        pGradientStops->Release();
    }

    if (pGradientBrush)
    {
        g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pGradientBrush);
        pGradientBrush->Release();
    }

    float innerRadius = 3.0f * dpiScale;

    // 左侧列表和搜索栏
    ID2D1SolidColorBrush *pWhiteBrush = nullptr;
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &pWhiteBrush);

    if (pWhiteBrush) 
    {
        // 列表框
        D2D1_ROUNDED_RECT leftListRRect = D2D1::RoundedRect(
            D2D1::RectF(padding, padding, 
                width - padding - rightPaneWidth - 2.0f * dpiScale, 
                height - padding - searchHeight- 0.5f * dpiScale),
            innerRadius, innerRadius
        );
        g_pMenuRenderTarget->FillRoundedRectangle(leftListRRect, pWhiteBrush);


        // 列表框外圈描边
        ID2D1SolidColorBrush* pListBorderBrush = nullptr;
        if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f, 0.75f), &pListBorderBrush)))
        {
            // 向外扩 0.5f
            D2D1_ROUNDED_RECT listStrokeRRect = D2D1::RoundedRect(
                D2D1::RectF(
                    leftListRRect.rect.left - 0.5f,
                    leftListRRect.rect.top - 0.5f,
                    leftListRRect.rect.right + 0.5f,
                    leftListRRect.rect.bottom + 0.5f
                ),
                innerRadius, innerRadius
            );
            g_pMenuRenderTarget->DrawRoundedRectangle(listStrokeRRect, pListBorderBrush, 1.0f * dpiScale);
            pListBorderBrush->Release();
        }

        // 列表框灰白描边
        ID2D1SolidColorBrush* pListLightBorderBrush = nullptr;
        // 透明度白色
        if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.90f, 0.90f, 0.75f), &pListLightBorderBrush)))
        {
            // 黑框外侧外扩 1 像素
            // 即 1.5f
            D2D1_ROUNDED_RECT listLightStrokeRRect = D2D1::RoundedRect(
                D2D1::RectF(
                    leftListRRect.rect.left - 1.72f,
                    leftListRRect.rect.top - 1.72f,
                    leftListRRect.rect.right + 1.72f,
                    leftListRRect.rect.bottom + 1.72f
                ),
                // 外圈包裹内圈
                // 外圈圆角半径加 1
                innerRadius + 1.0f,
                innerRadius + 1.0f
            );
            g_pMenuRenderTarget->DrawRoundedRectangle(listLightStrokeRRect, pListLightBorderBrush, 1.0f * dpiScale);
            pListLightBorderBrush->Release();
        }

        // 搜索框
        D2D1_RECT_F searchRect = D2D1::RectF(
            padding, 
            height - padding - searchHeight + 7.5f * dpiScale, 
            width - padding - rightPaneWidth - 2.0f * dpiScale, 
            height - padding
        );
        g_SearchBoxBounds = searchRect; // 保存用于消息处理
        g_pMenuRenderTarget->FillRectangle(searchRect, pWhiteBrush);

        // 搜索框外圈描边
        if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.7f), &pListBorderBrush)))
        {
            // 外扩 0.5f
            D2D1_RECT_F searchStrokeRect = D2D1::RectF(
                searchRect.left - 1.5f,
                searchRect.top - 1.5f,
                searchRect.right + 1.5f,
                searchRect.bottom + 1.5f
            );
            g_pMenuRenderTarget->DrawRectangle(searchStrokeRect, pListBorderBrush, 1.0f * dpiScale);
            pListBorderBrush->Release();
        }

        // 加载放大镜位图
        LoadSpriteBitmap(L"search_icon.png", &g_pSearchIcon);
        LoadSpriteBitmap(L"search_hover.png", &g_pSearchClearBg);

        if (g_pSearchIcon)
        {
            float iconSize = 17.0f * dpiScale;
            float iconY = searchRect.top + (searchRect.bottom - searchRect.top - iconSize) / 2.0f;
            D2D1_RECT_F iconRect = D2D1::RectF(
                searchRect.right - iconSize - 4.0f * dpiScale, // 靠右对齐
                iconY,
                searchRect.right - 4.0f * dpiScale,
                iconY + iconSize);

            g_SearchIconBounds = searchRect;
            g_SearchIconBounds.left = iconRect.left - 4.0f * dpiScale;

            // 鼠标悬停或按下叉叉
            if (!g_SearchText.empty() && g_pSearchClearBg && (g_bSearchIconHovered || g_bSearchIconPressed))
            {
                int stateIndex = g_bSearchIconPressed ? 2 : 1; // 2 为暗蓝按下，1 为亮蓝悬停

                D2D1_SIZE_F bgSize = g_pSearchClearBg->GetSize();
                float segHeight = bgSize.height / 4.0f;

                D2D1_RECT_F bgSrcRect = D2D1::RectF(
                    0.0f,
                    stateIndex * segHeight + 1.0f,
                    bgSize.width,
                    (stateIndex + 1) * segHeight - 2.0f
                );

                float clearBtnWidth = 27.0f * dpiScale;
                D2D1_RECT_F bgDestRect = D2D1::RectF(
                    searchRect.right - clearBtnWidth,
                    searchRect.top,
                    searchRect.right,
                    searchRect.bottom
                );
                g_pMenuRenderTarget->DrawBitmap(
                    g_pSearchClearBg, bgDestRect, 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &bgSrcRect
                );
            }

            // 截取位图
            D2D1_SIZE_F bmpSize = g_pSearchIcon->GetSize();
            float halfWidth = bmpSize.width / 2.0f; // 取原图一半宽
            D2D1_RECT_F srcRect;

            if (g_SearchText.empty())       // 无文本截取放大镜
            {
                srcRect = D2D1::RectF(0.0f, 0.0f,
                    halfWidth, bmpSize.height);
            }
                else                        // 有文本截取叉叉
                {
                    srcRect = D2D1::RectF(halfWidth, 0.0f, 
                        bmpSize.width, bmpSize.height);
                }

            g_pMenuRenderTarget->DrawBitmap(
                g_pSearchIcon,
                iconRect,
                1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                &srcRect
            );
        }

        // 绘制输入文字 / 占位符
        if (g_pSearchTextFormat || g_pSearchInputTextFormat)
        {
            D2D1_RECT_F textRect = searchRect;
            textRect.left += 6.0f * dpiScale; // 左边距
            textRect.right -= 20.0f * dpiScale; // 为右边放大镜留空

            ID2D1SolidColorBrush* pPlacholderBrush = nullptr;
            ID2D1SolidColorBrush* pInputTextBrush = nullptr;
            g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.4f, 0.5f, 0.6f, 1.0f), &pPlacholderBrush); // 淡淡的蓝灰色
            g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &pInputTextBrush);  // 黑色输入字

            if (g_SearchText.empty() && !g_bSearchFocused)
            {
                // 无输入且无焦点显示“开始搜索”
                g_pMenuRenderTarget->DrawTextW(L"开始搜索", 4, g_pSearchTextFormat, textRect, pPlacholderBrush);
            }
                else
                {
                    IDWriteTextFormat* pInputFmt = g_pSearchInputTextFormat ? g_pSearchInputTextFormat : g_pSearchTextFormat;

                    if (pInputFmt && pInputTextBrush)
                    {
                        g_pMenuRenderTarget->DrawTextW(
                            g_SearchText.c_str(),
                            static_cast<UINT32>(g_SearchText.length()),
                            pInputFmt,
                            textRect,
                            pInputTextBrush);
                    }

                    // 测量光标位置
                    if (g_bSearchFocused && pInputFmt && pInputTextBrush && ((GetTickCount() / 500) % 2 == 0))
                    {
                        float textWidth = 0.0f;

                        if (!g_SearchText.empty() && g_pDWriteFactory)
                        {
                            IDWriteTextLayout* pLayout = nullptr;
                            HRESULT hrLayout = g_pDWriteFactory->CreateTextLayout(
                                g_SearchText.c_str(),
                                static_cast<UINT32>(g_SearchText.length()),
                                pInputFmt,
                                textRect.right - textRect.left,
                                textRect.bottom - textRect.top,
                                &pLayout);

                            if (SUCCEEDED(hrLayout) && pLayout)
                            {
                                DWRITE_TEXT_METRICS metrics = {};
                                if (SUCCEEDED(pLayout->GetMetrics(&metrics)))
                                {
                                    textWidth = metrics.widthIncludingTrailingWhitespace;
                                }
                                pLayout->Release();
                            }
                        }

                        float curX = textRect.left + textWidth + 1.0f * dpiScale;
                        curX = (std::min)(curX, textRect.right - 1.0f * dpiScale);

                        g_pMenuRenderTarget->DrawLine(
                            D2D1::Point2F(curX, textRect.top + 4.0f * dpiScale),
                            D2D1::Point2F(curX, textRect.bottom - 4.0f * dpiScale),
                            pInputTextBrush, 1.0f * dpiScale);
                    }
                }
            if (pPlacholderBrush) pPlacholderBrush->Release();
            if (pInputTextBrush) pInputTextBrush->Release();
        }
    }
    
    if (pWhiteBrush) pWhiteBrush->Release();

// 菜单窗口外圈边缘
    ID2D1SolidColorBrush* pDarkBorderBrush = nullptr;
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.9f), &pDarkBorderBrush)))
    {
        D2D1_ROUNDED_RECT outerBorderRRect = D2D1::RoundedRect(
            D2D1::RectF(2.0f, 2.0f, width - 2.0f, height - 2.0f),
            cornerRadius - 1.5f * dpiScale, cornerRadius - 1.5f * dpiScale
        );
        g_pMenuRenderTarget->DrawRoundedRectangle(outerBorderRRect, pDarkBorderBrush, 2.0f * dpiScale);
        pDarkBorderBrush->Release();
    }

    // 内层边框
    ID2D1SolidColorBrush* pLightBorderBrush = nullptr;
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f, 0.6f), &pLightBorderBrush)))
    {
        // 往里收缩 3 像素
        float inset = 3.0f * dpiScale;
        D2D1_ROUNDED_RECT innerHighlightRRect = D2D1::RoundedRect(
            D2D1::RectF(inset, inset, width - inset, height - inset),
            cornerRadius - 2.5f * dpiScale, cornerRadius - 2.5f * dpiScale
        );
        g_pMenuRenderTarget->DrawRoundedRectangle(innerHighlightRRect, pLightBorderBrush, 1.0f * dpiScale);
        pLightBorderBrush->Release();
    }

// 格式初始化
    // 初始化左侧标题
    if (!g_pLeftTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei",
            NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f * dpiScale,
            L"zh-CN",
            &g_pLeftTextFormat
        );
        if (g_pLeftTextFormat)
        {
            g_pLeftTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pLeftTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // 初始化左侧副标题
    if (!g_pLeftSubTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.0f * dpiScale, L"zh-CN", &g_pLeftSubTextFormat);
        if (g_pLeftSubTextFormat)
        {
            g_pLeftSubTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pLeftSubTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

	// 初始化左侧加粗字形
    if (!g_pLeftBoldTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 12.3f * dpiScale, L"zh-CN", &g_pLeftBoldTextFormat);
        if (g_pLeftBoldTextFormat)
        {
            g_pLeftBoldTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pLeftBoldTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // 初始化右侧文本
    if (!g_pRightTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei",
            NULL,
            DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f * dpiScale,
            L"zh-CN",
            &g_pRightTextFormat
        );
        if (g_pRightTextFormat)
        {
            g_pRightTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pRightTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // 初始化搜索框文字
    if (!g_pSearchTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei", NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_ITALIC, // 提示词斜体
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f * dpiScale, L"zh-CN", &g_pSearchTextFormat);

        if (g_pSearchTextFormat)
        {
            g_pSearchTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pSearchTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // 初始化搜索框输入文字
    if (!g_pSearchInputTextFormat && g_pDWriteFactory)
    {
        g_pDWriteFactory->CreateTextFormat(
            L"Microsoft YaHei", NULL,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, // 输入态常规字体
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f * dpiScale, L"zh-CN", &g_pSearchInputTextFormat);

        if (g_pSearchInputTextFormat)
        {
            g_pSearchInputTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            g_pSearchInputTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
//

    ID2D1SolidColorBrush* pDarkTextBrush = nullptr;
    ID2D1SolidColorBrush* pLightTextBrush = nullptr;

    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 1.0f), &pDarkTextBrush);
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &pLightTextBrush);

// 绘制左侧列表
    LoadSpriteBitmap(L"all_programs_arrow.png", &g_pAllProgramsArrow);

    float currentY = padding + 2.0f * dpiScale;
    // float itemHeight = 36.0f * dpiScale;
    float allProgHeight = 37.0f * dpiScale;
    float searchRectTop = height - padding - searchHeight + 7.5f * dpiScale;

    for (size_t i = 0; i < g_LeftItems.size(); i++)
    {
        StartMenuItem& item = g_LeftItems[i];
        if (item.IsAllPrograms)
        {
            item.Bounds = D2D1::RectF(
                padding + 4.0f * dpiScale,
                searchRectTop - allProgHeight,
                width - rightPaneWidth - padding - 4.0f * dpiScale,
                searchRectTop
            );
        }
            else if (item.IsSeparator)
            {
                float sepHeight = 9.0f * dpiScale;

                // 底分割线在 “所有程序” 上面
                if (((i + 1) < g_LeftItems.size()) && g_LeftItems[i + 1].IsAllPrograms)
                {
                    item.Bounds = D2D1::RectF(
                        padding + 4.0f * dpiScale,
                        searchRectTop - allProgHeight - sepHeight,
                        width - rightPaneWidth - padding - 4.0f * dpiScale,
                        searchRectTop - allProgHeight
                    );
                    // 不增 currentY
                }
                    else
                    {
                        item.Bounds = D2D1::RectF(
                            padding + 4.0f * dpiScale,
                            currentY,
                            width - rightPaneWidth - padding - 4.0f * dpiScale,
                            currentY + sepHeight
                        );
                        currentY += sepHeight;
                    }
            }
                else
                {
                    float itemHeight = 40.5f * dpiScale;
                    item.Bounds = D2D1::RectF(
                        padding + 4.0f * dpiScale,
                        currentY,
                        width - rightPaneWidth - padding - 4.0f * dpiScale,
                        currentY + itemHeight
                    );
                    currentY += itemHeight;
                }

        if (item.IsHovered && !item.IsSeparator)
        {
            ID2D1SolidColorBrush* pLeftHoverBrush = nullptr;
            if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.9f, 1.0f, 1.0f), &pLeftHoverBrush)))
            {
                D2D1_RECT_F hoverRect = item.Bounds;

                // 单独针对 “所有程序”
                if (item.IsAllPrograms)
                {
                    hoverRect.bottom -= 12.0f * dpiScale;
                    hoverRect.top -= 0.0f * dpiScale;
                }

                g_pMenuRenderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(hoverRect, 4.0f, 4.0f), pLeftHoverBrush);
                pLeftHoverBrush->Release();
            }
        }

        if (item.IsSeparator)
        {
            ID2D1SolidColorBrush* pSepLightBrushL = nullptr;
            // 灰白分割线
            if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.8f, 0.8f, 0.8f), &pSepLightBrushL)))
            {
                float lineY = item.Bounds.top + (item.Bounds.bottom - item.Bounds.top) / 2.0f;
                g_pMenuRenderTarget->DrawLine(
                    D2D1::Point2F(item.Bounds.left + 9.0f * dpiScale, lineY),
                    D2D1::Point2F(item.Bounds.right - 9.0f * dpiScale, lineY),
                    pSepLightBrushL, 1.0f * dpiScale);
                pSepLightBrushL->Release();
            }
        }
            else if (item.IsAllPrograms)
            {
                if (g_pLeftBoldTextFormat && pDarkTextBrush)
                {
                    D2D1_RECT_F textBounds = item.Bounds;
                    float iconSize = 15.0f * dpiScale;
                    float iconY = item.Bounds.top + (item.Bounds.bottom - item.Bounds.top - iconSize) / 2.0f - 6.0f * dpiScale;

                    float leftOffset = 4.0 * dpiScale;
                    // 绘制小箭头
                    if (g_pAllProgramsArrow)
                    {
                        D2D1_RECT_F destRect = D2D1::RectF(
                            textBounds.left + leftOffset,
                            iconY,
                            textBounds.left + leftOffset + iconSize,
                            iconY + iconSize
                        );
                        g_pMenuRenderTarget->DrawBitmap(g_pAllProgramsArrow, destRect);
                    }

                    textBounds.left += leftOffset + iconSize + 23.0f * dpiScale;
                    
                    float textUpOffset = 6.0f * dpiScale;
					textBounds.top -= textUpOffset;
					textBounds.bottom -= textUpOffset;
                    
                    g_pMenuRenderTarget->DrawTextW(
                        item.Title.c_str(),
                        static_cast<UINT32>(item.Title.length()),
                        g_pLeftBoldTextFormat,
                        textBounds,
                        pDarkTextBrush
                    );
                }
            }
                else
                {
                    D2D1_RECT_F textBounds = item.Bounds;

                    // 为后续图标准备占位出缩进
                    float iconSize = 28.0f * dpiScale;
                    textBounds.left += iconSize + 8.0f * dpiScale + 6.0f * dpiScale;

                    // 判断是否有副标题并上下两段绘制
                    if (!item.SubTitle.empty() && g_pLeftSubTextFormat && g_pLeftBoldTextFormat && pDarkTextBrush)
                    {
                        // 行高边界
                        D2D1_RECT_F titleBounds = textBounds;
                        titleBounds.bottom = titleBounds.top + (item.Bounds.bottom - item.Bounds.top) / 2.0f + 2.0f * dpiScale;
                        titleBounds.top += 4.0f * dpiScale;

                        D2D1_RECT_F subTitleBounds = textBounds;
                        subTitleBounds.top = titleBounds.bottom - 4.0f * dpiScale;
                        subTitleBounds.bottom -= 4.0f * dpiScale;

                        float offsetTitle = 2.0f * dpiScale;
						titleBounds.top -= offsetTitle;
                        titleBounds.bottom -= offsetTitle;

                        subTitleBounds.top += offsetTitle;
                        subTitleBounds.bottom += offsetTitle;

                        g_pMenuRenderTarget->DrawTextW(
                            item.Title.c_str(), 
                            static_cast<UINT32>(item.Title.length()),
                            g_pLeftBoldTextFormat, 
                            titleBounds, 
                            pDarkTextBrush);

                        ID2D1SolidColorBrush* pGrayTextBrush = nullptr;
                        if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.45f, 0.45f, 1.0f), &pGrayTextBrush)))
                        {
                            g_pMenuRenderTarget->DrawTextW(
                                item.SubTitle.c_str(), 
                                static_cast<UINT32>(item.SubTitle.length()),
                                g_pLeftSubTextFormat, 
                                subTitleBounds, 
                                pGrayTextBrush);
                            pGrayTextBrush->Release();
                        }
                    }
                        else if (g_pLeftTextFormat && pDarkTextBrush)
                        {
                            g_pMenuRenderTarget->DrawTextW(
                                item.Title.c_str(), 
                                static_cast<UINT32>(item.Title.length()),
                                g_pLeftTextFormat, 
                                textBounds, 
                                pDarkTextBrush);
                        }
                }
    }

    // 重置 Y 坐标，绘制右侧列表
    float rightCurrentY = padding + 34.0f * dpiScale;
    float rightItemHeight = 36.0f * dpiScale;

    ID2D1SolidColorBrush* pShadowBrush = nullptr;
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f), &pShadowBrush);

    ID2D1SolidColorBrush* pSepDarkBrush = nullptr;
    ID2D1SolidColorBrush* pSepLightBrush = nullptr;
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f), &pSepDarkBrush);
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f), &pSepLightBrush);

    for (size_t i = 0; i < g_RightItems.size(); i++)
    {
        StartMenuItem& item = g_RightItems[i];

        if (item.Title == L"")
        {
            // 空隙
            item.Bounds = D2D1::RectF(0, 0, 0, 0);
            rightCurrentY += 8.0f * dpiScale;
            continue;
        }

        if (item.Title == L"-")
        {
            // 分割线
            float sepHeight = 7.0f * dpiScale;
            item.Bounds = D2D1::RectF(
                width - rightPaneWidth,
                rightCurrentY,
                width - padding,
                rightCurrentY + sepHeight
            );

            if (pSepDarkBrush && pSepLightBrush)
            {
                float lineY = rightCurrentY + sepHeight / 2.0f + 0.5f;
                float startX = width - rightPaneWidth + 1.0f * dpiScale;
                float endX = width - padding - 1.0f * dpiScale;

                g_pMenuRenderTarget->DrawLine(D2D1::Point2F(startX, lineY), 
                    D2D1::Point2F(endX, lineY), pSepDarkBrush, 1.0f * dpiScale);
                g_pMenuRenderTarget->DrawLine(D2D1::Point2F(startX, lineY + 1.0f * dpiScale), 
                    D2D1::Point2F(endX, lineY + 1.0f * dpiScale), pSepLightBrush, 1.0f * dpiScale);
            }

            rightCurrentY += sepHeight;
            continue;
        }

        // 默认判定
        item.Bounds = D2D1::RectF(
            width - rightPaneWidth - 3.5f * dpiScale,
            rightCurrentY,
            width - padding - 0.5f * dpiScale,
            rightCurrentY + rightItemHeight
        );

        if (item.IsHovered)
        {
            // 按钮绘图区域
            // 上下留白 1.5px 
            D2D1_RECT_F btnRect = D2D1::RectF(
                item.Bounds.left + 0.5f * dpiScale,
                item.Bounds.top + 2.0f * dpiScale,
                item.Bounds.right - 0.5f * dpiScale,
                item.Bounds.bottom - 2.0f * dpiScale
            );

            // 光影渐变
            ID2D1LinearGradientBrush* pGlossyBrush = nullptr;
            ID2D1GradientStopCollection* pStops = nullptr;
            D2D1_GRADIENT_STOP stops[5];
            stops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f); // 顶部白反光
            stops[0].position = 0.0f;
            stops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f); // 中间淡
            stops[1].position = 0.45f;
            stops[2].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.15f); // 暗部
            stops[2].position = 0.50f;
            stops[3].color = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.20f); // 下半深色
            stops[3].position = 0.74f;
            stops[4].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f); // 底部亮光
            stops[4].position = 1.0f;

            if (SUCCEEDED(g_pMenuRenderTarget->CreateGradientStopCollection(stops, 5, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pStops)))
            {
                g_pMenuRenderTarget->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, btnRect.top), D2D1::Point2F(0, btnRect.bottom)),
                    pStops, &pGlossyBrush
                );
                pStops->Release();
            }

        // 边框
            ID2D1SolidColorBrush* pOuterBorderBrush = nullptr;     // 最外层描边
            ID2D1SolidColorBrush* pMiddleBorderBrush = nullptr;    // 中间层
            g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.25f), &pOuterBorderBrush);
            g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f), &pMiddleBorderBrush);

            // 最里层描边 用渐变
            ID2D1LinearGradientBrush* pInnerStrokeBrush = nullptr;
            ID2D1GradientStopCollection* pInnerStops = nullptr;
            D2D1_GRADIENT_STOP iStops[2];
            iStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.60f); // 内层顶亮白
            iStops[0].position = 0.0f;
            iStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.50f); // 内层底暗淡
            iStops[1].position = 1.0f;
            if (SUCCEEDED(g_pMenuRenderTarget->CreateGradientStopCollection(iStops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pInnerStops)))
            {
                g_pMenuRenderTarget->CreateLinearGradientBrush(
                    D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, btnRect.top), D2D1::Point2F(0, btnRect.bottom)),
                    pInnerStops, &pInnerStrokeBrush);
                pInnerStops->Release();
            }

            float r = 2.5f * dpiScale; // 圆角半径

            // 填充主渐变色
            if (pGlossyBrush)
            {
                g_pMenuRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnRect, r, r), pGlossyBrush);
                pGlossyBrush->Release();
            }

            // 三层依次向外偏移
            if (pInnerStrokeBrush)
            {
                // 最内层
                g_pMenuRenderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(btnRect.left + 1.0f, btnRect.top + 1.0f, btnRect.right - 1.0f, btnRect.bottom - 1.0f),
                        r - 1.0f, r - 1.0f),
                    pInnerStrokeBrush, 1.0f * dpiScale);
                pInnerStrokeBrush->Release();
            }
            if (pMiddleBorderBrush)
            {
                // 中间层
                g_pMenuRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(btnRect, r, r), pMiddleBorderBrush, 1.0f * dpiScale);
                pMiddleBorderBrush->Release();
            }
            if (pOuterBorderBrush)
            {
                // 最外层
                g_pMenuRenderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(btnRect.left - 1.2f, btnRect.top - 1.2f, btnRect.right + 1.2f, btnRect.bottom + 1.2f),
                        r + 1.0f, r + 1.0f),
                    pOuterBorderBrush, 1.0f * dpiScale);
                pOuterBorderBrush->Release();
            }
        }

        // 文字阴影
        if (g_pRightTextFormat && pLightTextBrush)
        {
            D2D1_RECT_F textBounds = item.Bounds;
            textBounds.left += 8.0f * dpiScale;

            if (pShadowBrush)
            {
                D2D1_RECT_F shadowBounds = textBounds;
                shadowBounds.left += 1.0f * dpiScale;
                shadowBounds.top += 1.0f * dpiScale;
                shadowBounds.right += 1.0f * dpiScale;
                shadowBounds.bottom += 1.0f * dpiScale;
                g_pMenuRenderTarget->DrawTextW(
                    item.Title.c_str(),
                    static_cast<UINT32>(item.Title.length()),
                    g_pRightTextFormat,
                    shadowBounds,
                    pShadowBrush
                );
            }

            g_pMenuRenderTarget->DrawTextW(
                item.Title.c_str(),
                static_cast<UINT32>(item.Title.length()),
                g_pRightTextFormat,
                textBounds,
                pLightTextBrush
            );

            // “最近使用的项目”向右小箭头
            if (item.Title == L"最近使用的项目")
            {
                ID2D1PathGeometry* pArrowGeo = nullptr;
                if (SUCCEEDED(g_pD2DFactory->CreatePathGeometry(&pArrowGeo)))
                {
                    ID2D1GeometrySink* pSink = nullptr;
                    if (SUCCEEDED(pArrowGeo->Open(&pSink)))
                    {
                        float arrowX = item.Bounds.right - 14.0f * dpiScale;
                        float arrowY = item.Bounds.top + (item.Bounds.bottom - item.Bounds.top) / 2.0f;
                        float h = 4.5f * dpiScale;

                        pSink->BeginFigure(D2D1::Point2F(arrowX, arrowY - h), D2D1_FIGURE_BEGIN_FILLED);
                        pSink->AddLine(D2D1::Point2F(arrowX + h, arrowY));
                        pSink->AddLine(D2D1::Point2F(arrowX, arrowY + h));
                        pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        pSink->Close();

                        // 箭头阴影
                        if (pShadowBrush)
                        {
                            D2D1_MATRIX_3X2_F translation = D2D1::Matrix3x2F::Translation(1.0f * dpiScale, 1.0f * dpiScale);
                            ID2D1TransformedGeometry* pShadowGeo = nullptr;
                            g_pD2DFactory->CreateTransformedGeometry(pArrowGeo, translation, &pShadowGeo);
                            if (pShadowGeo)
                            {
                                g_pMenuRenderTarget->FillGeometry(pShadowGeo, pShadowBrush);
                                pShadowGeo->Release();
                            }
                        }

                        g_pMenuRenderTarget->FillGeometry(pArrowGeo, pLightTextBrush);
                        pSink->Release();
                    }
                    pArrowGeo->Release();
                }
            }

        }

        rightCurrentY += rightItemHeight;
    }

// 底部系统控制按钮
    float sbTop = height - padding - searchHeight + 6.5f * dpiScale;
    float sbBottom = height - padding + 1.0f * dpiScale;

    float btnLeft = width - rightPaneWidth - 3.5f * dpiScale;
    float btnRight = width - padding - 3.5f * dpiScale;

    float arrowW = 22.0f * dpiScale;
    float pwrW = (btnRight - btnLeft - arrowW) / 2.0f;
    float lockW = btnRight - btnLeft - arrowW - pwrW;

    g_PowerBtnBounds = D2D1::RectF(btnLeft, sbTop, btnLeft + pwrW, sbBottom);
    g_LockBtnBounds = D2D1::RectF(g_PowerBtnBounds.right, sbTop, g_PowerBtnBounds.right + lockW, sbBottom);
    g_ArrowBtnBounds = D2D1::RectF(g_LockBtnBounds.right, sbTop, btnRight, sbBottom);

    // 位图渲染
    LoadSpriteBitmap(L"btn_normal.png", &g_pBtnNormal);
    LoadSpriteBitmap(L"btn_hover.bmp", &g_pBtnHover);
    LoadSpriteBitmap(L"btn_pressed.bmp", &g_pBtnPressed);

    struct SysBtnLayout 
    {
        D2D1_RECT_F rect;
        float rLeft;
        float rRight;
        int iconType;
        bool isHovered;
        bool isPressed;
    };
    SysBtnLayout sysBtns[3] = 
    {
        { g_PowerBtnBounds, 3.0f * dpiScale, 0.0f, 1, g_bPowerHovered, g_bPowerPressed },
        { g_LockBtnBounds,  0.0f, 0.0f, 2, g_bLockHovered,  g_bLockPressed },
        { g_ArrowBtnBounds, 0.0f, 3.0f * dpiScale, 3, g_bArrowHovered, g_bArrowPressed }
    };

    for (int k = 0; k <= 2; k++)
    {
        SysBtnLayout& sb = sysBtns[k];

        // 圆角裁切
        ID2D1PathGeometry* pPath = nullptr;
        g_pD2DFactory->CreatePathGeometry(&pPath);
        if (pPath)
        {
            ID2D1GeometrySink* pSink = nullptr;
            pPath->Open(&pSink);
            if (pSink)
            {
                pSink->BeginFigure(D2D1::Point2F(sb.rect.left + sb.rLeft, sb.rect.top), D2D1_FIGURE_BEGIN_FILLED);
                pSink->AddLine(D2D1::Point2F(sb.rect.right - sb.rRight, sb.rect.top));
                if (sb.rRight > 0) 
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(sb.rect.right, sb.rect.top + sb.rRight), 
                        D2D1::SizeF(sb.rRight, sb.rRight), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                pSink->AddLine(D2D1::Point2F(sb.rect.right, sb.rect.bottom - sb.rRight));
                if (sb.rRight > 0) 
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(sb.rect.right - sb.rRight, sb.rect.bottom), 
                        D2D1::SizeF(sb.rRight, sb.rRight), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                pSink->AddLine(D2D1::Point2F(sb.rect.left + sb.rLeft, sb.rect.bottom));
                if (sb.rLeft > 0) 
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(sb.rect.left, sb.rect.bottom - sb.rLeft), 
                        D2D1::SizeF(sb.rLeft, sb.rLeft), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                pSink->AddLine(D2D1::Point2F(sb.rect.left, sb.rect.top + sb.rLeft));
                if (sb.rLeft > 0) 
                    pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(sb.rect.left + sb.rLeft, sb.rect.top), 
                        D2D1::SizeF(sb.rLeft, sb.rLeft), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
                pSink->Close();
                pSink->Release();
            }
        }

        // 基础图像底板渲染
        ID2D1Bitmap* pBmpToDraw = g_pBtnNormal;
        if (sb.isPressed) pBmpToDraw = g_pBtnPressed;
        else if (sb.isHovered) pBmpToDraw = g_pBtnHover;

        if (pBmpToDraw)
        {
            D2D1_SIZE_F bmpSize = pBmpToDraw->GetSize();
            float segWidth = bmpSize.width / 6.0f;
            float segHeight = bmpSize.height;

            float cropX = 2.0f;
            float cropY = 2.0f;
            D2D1_RECT_F srcRect;

            if (sb.iconType == 1) // 电源键 (切片3)
            {
                srcRect = D2D1::RectF(3 * segWidth + cropX, cropY, 4 * segWidth - cropX, segHeight - cropY);
            }
                else if (sb.iconType == 2) // 锁定键 (切片5)
                {
                    srcRect = D2D1::RectF(5 * segWidth + cropX, cropY, 6 * segWidth - cropX, segHeight - cropY);
                }
                    else if (sb.iconType == 3) // 小箭头 
                    {
                        // 裁取锁定键左侧 10 像素
                        srcRect = D2D1::RectF(5 * segWidth + cropX, cropY, 5 * segWidth + cropX + 10.0f, segHeight - cropY);
                    }

            D2D1_RECT_F renderRect = sb.rect;
            renderRect.left += 1.0f * dpiScale;
            renderRect.top += 1.0f * dpiScale;
            renderRect.right -= 1.0f * dpiScale;
            renderRect.bottom -= 1.0f * dpiScale;
            if (sb.isPressed) {
                renderRect.left += 1.0f * dpiScale; renderRect.top += 1.0f * dpiScale;
                renderRect.right += 1.0f * dpiScale; renderRect.bottom += 1.0f * dpiScale;
            }

            g_pMenuRenderTarget->DrawBitmap(pBmpToDraw, renderRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcRect);
        }

        if (sb.iconType == 3)
        {
            // 画白色三角体
            float cx = (sb.rect.left + sb.rect.right) / 2.0f;
            float cy = (sb.rect.top + sb.rect.bottom) / 2.0f;
            float pOff = sb.isPressed ? (1.0f * dpiScale) : 0.0f;
            ID2D1SolidColorBrush* pArrowBr = nullptr;
            g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &pArrowBr);
            if (pArrowBr) 
            {
                // 阴影 & 本体
                for (int shadow = 1; shadow >= 0; shadow--) 
                {
                    ID2D1SolidColorBrush* pBrToUse = pArrowBr;
                    ID2D1SolidColorBrush* pTempShadow = nullptr;
                    float off = pOff;
                    if (shadow == 1) 
                    {
                        g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.7f), &pTempShadow);
                        pBrToUse = pTempShadow;
                        off += 1.0f * dpiScale;
                    }
                    if (pBrToUse) 
                    {
                        ID2D1PathGeometry* pg = nullptr;
                        if (SUCCEEDED(g_pD2DFactory->CreatePathGeometry(&pg))) 
                        {
                            ID2D1GeometrySink* sink = nullptr;
                            if (SUCCEEDED(pg->Open(&sink))) 
                            {
                                sink->BeginFigure(D2D1::Point2F(cx + off - 2.0f * dpiScale, cy + off - 4.0f * dpiScale), D2D1_FIGURE_BEGIN_FILLED);
                                sink->AddLine(D2D1::Point2F(cx + off + 3.0f * dpiScale, cy + off));
                                sink->AddLine(D2D1::Point2F(cx + off - 2.0f * dpiScale, cy + off + 4.0f * dpiScale));
                                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                                sink->Close();
                                sink->Release();
                                g_pMenuRenderTarget->FillGeometry(pg, pBrToUse);
                            }
                            pg->Release();
                        }
                        if (pTempShadow) pTempShadow->Release();
                    }
                }
                pArrowBr->Release();
            }
        }
        if (pPath) pPath->Release();
    }

    // 三键大边框
    D2D1_ROUNDED_RECT groupRRect = D2D1::RoundedRect(
        D2D1::RectF(btnLeft, sbTop, btnRight, sbBottom),
        3.0f * dpiScale, 3.0f * dpiScale
    );

    ID2D1SolidColorBrush* pGrpOuter = nullptr;
    ID2D1SolidColorBrush* pGrpMid = nullptr;
    ID2D1SolidColorBrush* pGrpInner = nullptr;

    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &pGrpOuter); // 暗灰
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.95f), &pGrpMid);   // 纯黑
    g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.35f), &pGrpInner); // 内白

    if (pGrpOuter && pGrpMid && pGrpInner)
    {
        // 外沿暗灰
        D2D1_ROUNDED_RECT rrectOut = groupRRect;
        rrectOut.rect.left -= 1.0f * dpiScale; rrectOut.rect.top -= 1.0f * dpiScale;
        rrectOut.rect.right += 1.0f * dpiScale; rrectOut.rect.bottom += 1.0f * dpiScale;
        rrectOut.radiusX += 1.0f * dpiScale; rrectOut.radiusY += 1.0f * dpiScale;
        g_pMenuRenderTarget->DrawRoundedRectangle(rrectOut, pGrpOuter, 1.0f * dpiScale);

        // 主层纯黑
        g_pMenuRenderTarget->DrawRoundedRectangle(groupRRect, pGrpMid, 1.0f * dpiScale);

        // 缩进一圈描亮边
        D2D1_ROUNDED_RECT rrectIn = groupRRect;
        rrectIn.rect.left += 1.0f * dpiScale; rrectIn.rect.top += 1.0f * dpiScale;
        rrectIn.rect.right -= 1.0f * dpiScale; rrectIn.rect.bottom -= 1.0f * dpiScale;
        rrectIn.radiusX = (std::max)(0.0f, rrectIn.radiusX - 1.0f * dpiScale);
        rrectIn.radiusY = (std::max)(0.0f, rrectIn.radiusY - 1.0f * dpiScale);
        g_pMenuRenderTarget->DrawRoundedRectangle(rrectIn, pGrpInner, 1.0f * dpiScale);

        // 竖直分割线
        float sep1 = g_LockBtnBounds.left;
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep1 - 1.0f * dpiScale, sbTop + 1.0f * dpiScale),
            D2D1::Point2F(sep1 - 1.0f * dpiScale, sbBottom - 1.0f * dpiScale), pGrpInner, 1.0f * dpiScale);
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep1, sbTop),
            D2D1::Point2F(sep1, sbBottom), pGrpMid, 1.0f * dpiScale);
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep1 + 1.0f * dpiScale, sbTop + 1.0f * dpiScale),
            D2D1::Point2F(sep1 + 1.0f * dpiScale, sbBottom - 1.0f * dpiScale), pGrpInner, 1.0f * dpiScale);

        float sep2 = g_ArrowBtnBounds.left;
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep2 - 1.0f * dpiScale, sbTop + 1.0f * dpiScale),
            D2D1::Point2F(sep2 - 1.0f * dpiScale, sbBottom - 1.0f * dpiScale), pGrpInner, 1.0f * dpiScale);
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep2, sbTop),
            D2D1::Point2F(sep2, sbBottom), pGrpMid, 1.0f * dpiScale);
        g_pMenuRenderTarget->DrawLine(D2D1::Point2F(sep2 + 1.0f * dpiScale, sbTop + 1.0f * dpiScale),
            D2D1::Point2F(sep2 + 1.0f * dpiScale, sbBottom - 1.0f * dpiScale), pGrpInner, 1.0f * dpiScale);
    }

    if (pGrpOuter) pGrpOuter->Release();
    if (pGrpMid) pGrpMid->Release();
    if (pGrpInner) pGrpInner->Release();
//

    HRESULT hr = g_pMenuRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) 
    {
        if (g_pMenuRenderTarget) 
        {
            g_pMenuRenderTarget->Release();
            g_pMenuRenderTarget = nullptr;
        }
        if (g_pMenuWicBitmap) 
        {
            g_pMenuWicBitmap->Release();
            g_pMenuWicBitmap = nullptr;
        }
        return;
    }
 
// 提交到 UpdateLayeredWindow
    HDC hdcScreen = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pDibBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    IWICBitmapLock *pLock = nullptr;
    WICRect lockRect = {0, 0, width, height};
    if (SUCCEEDED(g_pMenuWicBitmap->Lock(&lockRect, WICBitmapLockRead, &pLock))) 
    {
        UINT cbStride = 0;
        UINT cbBufferSize = 0;
        BYTE *pWicPixels = nullptr;

        pLock->GetStride(&cbStride);
        pLock->GetDataPointer(&cbBufferSize, &pWicPixels);

        // 逐行拷贝像素
        UINT dibStride = width * 4;
        for (int y = 0; y < height; y++) 
            memcpy((BYTE *)pDibBits + y * dibStride, pWicPixels + y * cbStride,
                    dibStride);

        pLock->Release();
    }

    POINT ptSrc = {0, 0};
    SIZE winSize = {width, height};

    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    RECT wndRect;
    GetWindowRect(hwnd, &wndRect);
    POINT ptDst = {wndRect.left, wndRect.top};

    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &winSize, hMemoryDC, &ptSrc, 0,
                        &blend, ULW_ALPHA);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hdcScreen);
}

void RenderAvatarWindow(HWND hwnd)
{
    if (!g_pD2DFactory || !g_pWICFactory) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if ((width == 0) || (height == 0)) return;

    // 独立初始化
    if (!g_pAvatarWicBitmap)
    {
        HRESULT hr = g_pWICFactory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &g_pAvatarWicBitmap);
        if (FAILED(hr)) return;
    }
    if (!g_pAvatarRenderTarget)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
        HRESULT hr = g_pD2DFactory->CreateWicBitmapRenderTarget(g_pAvatarWicBitmap, props, &g_pAvatarRenderTarget);
        if (FAILED(hr)) return;
    }

    // 获取 DPI
    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"User32.dll");
    if (hUser32)
    {
        typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    float dpiScale = dpi / 96.0f;
    if (dpiScale <= 0) dpiScale = 1.0f;

    g_pAvatarRenderTarget->BeginDraw();
    g_pAvatarRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // 坐标以小窗口居中
    float avatarSize = 58.0f * dpiScale;
    float avatarX = ((float)width - avatarSize) / 2.0f;
    float avatarY = ((float)height - avatarSize) / 2.0f;
    D2D1_RECT_F avatarRect = D2D1::RectF(avatarX, avatarY, avatarX + avatarSize, avatarY + avatarSize);

    // 枕形外框
    ID2D1PathGeometry* pPillowGeo = nullptr;
    if (SUCCEEDED(g_pD2DFactory->CreatePathGeometry(&pPillowGeo)))
    {
        ID2D1GeometrySink* pSink = nullptr;
        if (SUCCEEDED(pPillowGeo->Open(&pSink)))
        {
            float cr = 3.2f * dpiScale;     // 圆角半径
            float bulge = 1.0f * dpiScale;  // 向外凸起的弧度量
            float cBulge = bulge * 2.0f;    // 二次贝塞尔控制点的偏移量

            float L = avatarRect.left, T = avatarRect.top, R = avatarRect.right, B = avatarRect.bottom;
            float midX = (L + R) / 2.0f, midY = (T + B) / 2.0f;

            pSink->BeginFigure(D2D1::Point2F(L + cr, T), D2D1_FIGURE_BEGIN_FILLED);

            // 上边 向外微凸
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(midX, T - cBulge), D2D1::Point2F(R - cr, T)));
            // 右上圆角
            pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(R, T + cr), D2D1::SizeF(cr, cr), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            // 右边
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(R + cBulge, midY), D2D1::Point2F(R, B - cr)));
            // 右下圆角
            pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(R - cr, B), D2D1::SizeF(cr, cr), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            // 下边
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(midX, B + cBulge), D2D1::Point2F(L + cr, B)));
            // 左下圆角
            pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(L, B - cr), D2D1::SizeF(cr, cr), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            // 左边
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(L - cBulge, midY), D2D1::Point2F(L, T + cr)));
            // 左上圆角
            pSink->AddArc(D2D1::ArcSegment(D2D1::Point2F(L + cr, T), D2D1::SizeF(cr, cr), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));

            pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
            pSink->Close();
            pSink->Release();
        }
    }

    // 玻璃底板
    ID2D1LinearGradientBrush* pGlassBaseBrush = nullptr;
    ID2D1GradientStopCollection* pBaseStops = nullptr;

    D2D1_GRADIENT_STOP baseStops[4];
    baseStops[0].color = D2D1::ColorF(0.90f, 0.90f, 0.90f, 0.95f);  // 不透明的白
    baseStops[0].position = 0.0f;  // 比例
    baseStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f);  // 淡一点的白
    baseStops[1].position = 0.3f;
    baseStops[2].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f);  // 更透明的白
    baseStops[2].position = 0.5f;
    baseStops[3].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);  // 全透明
    baseStops[3].position = 1.0f;

    if (SUCCEEDED(g_pAvatarRenderTarget->CreateGradientStopCollection(baseStops, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pBaseStops)))
    {
        // 垂直渐变
        g_pAvatarRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F((avatarRect.left + avatarRect.right) / 2.0f, avatarRect.top),
                D2D1::Point2F((avatarRect.left + avatarRect.right) / 2.0f, avatarRect.bottom)
            ),
            pBaseStops, &pGlassBaseBrush);
        pBaseStops->Release();
    }

    if (pPillowGeo && pGlassBaseBrush)
        g_pAvatarRenderTarget->FillGeometry(pPillowGeo, pGlassBaseBrush);

    ID2D1SolidColorBrush* pSideHighlightBrush = nullptr;
    if (SUCCEEDED(g_pAvatarRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.3f), &pSideHighlightBrush)))
    {
        // 在左侧 1.5px 处画一条极细的淡白线
        g_pAvatarRenderTarget->DrawLine(
            D2D1::Point2F(avatarRect.left + 1.5f * dpiScale, avatarRect.top + 5.0f * dpiScale),
            D2D1::Point2F(avatarRect.left + 1.5f * dpiScale, avatarRect.bottom - 5.0f * dpiScale),
            pSideHighlightBrush, 0.5f * dpiScale);
        pSideHighlightBrush->Release();
    }

    // 内部头像图片区 
    float glassThickness = 3.0f * dpiScale; // 玻璃面板宽度
    float innerR = 1.2f * dpiScale;
    D2D1_RECT_F innerPicRect = D2D1::RectF(
        avatarRect.left + glassThickness,
        avatarRect.top + glassThickness,
        avatarRect.right - glassThickness,
        avatarRect.bottom - glassThickness
    );
    D2D1_ROUNDED_RECT innerPicRRect = D2D1::RoundedRect(innerPicRect, innerR, innerR);

    // 玻璃内白边
    // 扩大 1 像素画在照片黑边外围
    D2D1_ROUNDED_RECT cutoutRRect = D2D1::RoundedRect(
        D2D1::RectF(innerPicRect.left - 1.0f * dpiScale - 0.5f,
            innerPicRect.top - 1.0f * dpiScale - 0.5f,
            innerPicRect.right + 1.0f * dpiScale + 0.5f,
            innerPicRect.bottom + 1.0f * dpiScale + 0.5f),
        3.0f * dpiScale, 3.0f * dpiScale
    );
    ID2D1SolidColorBrush* pCutoutHighlightBrush = nullptr;
    if (SUCCEEDED(g_pAvatarRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f, 0.85f), &pCutoutHighlightBrush)))
    {
        g_pAvatarRenderTarget->DrawRoundedRectangle(cutoutRRect, pCutoutHighlightBrush, 1.0f * dpiScale);
        pCutoutHighlightBrush->Release();
    }

    LoadAvatarBitmap(hwnd);

    if (g_pAvatarBitmap)
    {
        // DrawBitmap 渲染头像
        g_pAvatarRenderTarget->DrawBitmap(
            g_pAvatarBitmap,
            innerPicRRect.rect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
    }
        else
        {
            ID2D1SolidColorBrush* pPicPlaceholderBrush = nullptr;
            if (SUCCEEDED(g_pAvatarRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.45f, 0.15f, 1.0f), &pPicPlaceholderBrush)))
            {
                g_pAvatarRenderTarget->FillRoundedRectangle(innerPicRRect, pPicPlaceholderBrush);
                pPicPlaceholderBrush->Release();
            }
        }

    // 照片表面果冻玻璃反光
    ID2D1LinearGradientBrush* pJellyGlossBrush = nullptr;
    ID2D1GradientStopCollection* pJellyStops = nullptr;
    D2D1_GRADIENT_STOP jStops[3];
    jStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.75f); // 顶部白
    jStops[0].position = 0.0f;
    jStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f);  // 滑动到中间
    jStops[1].position = 0.44f;
    jStops[2].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f);  // 切断
    jStops[2].position = 0.46f;

    if (SUCCEEDED(g_pAvatarRenderTarget->CreateGradientStopCollection(jStops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pJellyStops)))
    {
        // 纯垂直线性渐变覆盖照片区域
        g_pAvatarRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(innerPicRect.left, innerPicRect.top),
                D2D1::Point2F(innerPicRect.left, innerPicRect.bottom)),
            pJellyStops, &pJellyGlossBrush);
        pJellyStops->Release();
    }
    if (pJellyGlossBrush)
    {
        g_pAvatarRenderTarget->FillRoundedRectangle(innerPicRRect, pJellyGlossBrush);
        pJellyGlossBrush->Release();
    }

    // 照片内部黑色描边
    D2D1_ROUNDED_RECT sharpDarkRRect = D2D1::RoundedRect(
        D2D1::RectF(
            innerPicRect.left + 0.5f,
            innerPicRect.top + 0.5f,
            innerPicRect.right - 0.5f,
            innerPicRect.bottom - 0.5f),
        2.5f * dpiScale, 2.5f * dpiScale
    );
    ID2D1SolidColorBrush* pPicDarkBorderBrush = nullptr;
    if (SUCCEEDED(g_pAvatarRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.25f, 0.25f, 0.85f), &pPicDarkBorderBrush)))
    {
        g_pAvatarRenderTarget->DrawRoundedRectangle(innerPicRRect, pPicDarkBorderBrush, 1.0f * dpiScale);
        pPicDarkBorderBrush->Release();
    }

    // 最外层双色渐变描边 (左上白 - 右下青) 
    ID2D1LinearGradientBrush* pOuterBorderBrush = nullptr;
    ID2D1GradientStopCollection* pBorderStops = nullptr;
    D2D1_GRADIENT_STOP borderStops[4];
    borderStops[0].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f); // 左上纯白
    borderStops[0].position = 0.0f;
    borderStops[1].color = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f);  // 白色衰减
    borderStops[1].position = 0.45f;
    borderStops[2].color = D2D1::ColorF(0.0f, 0.6f, 0.9f, 0.4f);  // 青色渐入
    borderStops[2].position = 0.55f;
    borderStops[3].color = D2D1::ColorF(0.0f, 0.8f, 1.0f, 0.95f); // 右下亮青 (天蓝色)
    borderStops[3].position = 1.0f;

    if (SUCCEEDED(g_pAvatarRenderTarget->CreateGradientStopCollection(borderStops, 4, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pBorderStops)))
    {
        // 对角线拉伸覆盖边缘
        g_pAvatarRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(avatarRect.left, avatarRect.top),
                D2D1::Point2F(avatarRect.right, avatarRect.bottom)),
            pBorderStops, &pOuterBorderBrush);
        pBorderStops->Release();
    }

    if (pPillowGeo && pOuterBorderBrush)
    {
        // 描边
        g_pAvatarRenderTarget->DrawGeometry(pPillowGeo, pOuterBorderBrush, 1.2f * dpiScale);
        pOuterBorderBrush->Release();

        // 缩放得到外描边
        ID2D1TransformedGeometry* pOuterEdgeGeo = nullptr;

        // 创建缩放矩阵扩大 101.5%
        // 目的为外移约 1px 
        D2D1_MATRIX_3X2_F scale = D2D1::Matrix3x2F::Scale(
            1.035f, 1.035f,
            D2D1::Point2F((avatarRect.left + avatarRect.right) / 2.0f, (avatarRect.top + avatarRect.bottom) / 2.0f)
        );

        if (SUCCEEDED(g_pD2DFactory->CreateTransformedGeometry(pPillowGeo, scale, &pOuterEdgeGeo)))
        {
            ID2D1SolidColorBrush* pOuterDarkBorderBrush = nullptr;
            // 暗灰细线
            if (SUCCEEDED(g_pAvatarRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.05f, 0.05f, 0.05f, 0.75f), &pOuterDarkBorderBrush)))
            {
                g_pAvatarRenderTarget->DrawGeometry(pOuterEdgeGeo, pOuterDarkBorderBrush, 0.5f * dpiScale);
                pOuterDarkBorderBrush->Release();
            }
            pOuterEdgeGeo->Release();
        }
    }

    if (pGlassBaseBrush) pGlassBaseBrush->Release();
    if (pPillowGeo) pPillowGeo->Release();

    HRESULT hr = g_pAvatarRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) 
    { 
        g_pAvatarRenderTarget->Release(); 
        g_pAvatarRenderTarget = nullptr; 
        g_pAvatarWicBitmap->Release(); 
        g_pAvatarWicBitmap = nullptr; 
        return; 
    }

    // 提交到分层窗口
    HDC hdcScreen = GetDC(NULL); 
    HDC hMemoryDC = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi = { 0 }; 
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); 
    bmi.bmiHeader.biWidth = width; 
    bmi.bmiHeader.biHeight = -height; 
    bmi.bmiHeader.biPlanes = 1; 
    bmi.bmiHeader.biBitCount = 32; 
    bmi.bmiHeader.biCompression = BI_RGB;
    void* pDibBits = nullptr; 
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
    IWICBitmapLock* pLock = nullptr; 
    WICRect lockRect = { 0, 0, width, height };
    if (SUCCEEDED(g_pAvatarWicBitmap->Lock(&lockRect, WICBitmapLockRead, &pLock)))
    {
        UINT cbStride = 0, cbBufferSize = 0; 
        BYTE* pWicPixels = nullptr;
        pLock->GetStride(&cbStride); 
        pLock->GetDataPointer(&cbBufferSize, &pWicPixels);
        for (int y = 0; y < height; y++) 
            memcpy((BYTE*)pDibBits + y * (width * 4), pWicPixels + y * cbStride, width * 4);
        pLock->Release();
    }
    POINT ptSrc = { 0, 0 }; 
    SIZE winSize = { width, height };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    RECT wndRect; 
    GetWindowRect(hwnd, &wndRect); 
    POINT ptDst = { wndRect.left, wndRect.top };
    UpdateLayeredWindow(hwnd, hdcScreen, &ptDst, &winSize, hMemoryDC, &ptSrc, 0, &blend, ULW_ALPHA);
    SelectObject(hMemoryDC, hOldBitmap); 
    DeleteObject(hBitmap); 
    DeleteDC(hMemoryDC); 
    ReleaseDC(NULL, hdcScreen);
}

LRESULT CALLBACK AvatarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_MOUSEACTIVATE:
        {
            return MA_NOACTIVATE;  // 头像框莫抢焦点
        }

        // 鼠标悬停状态
        case WM_SETCURSOR:
        {
            // 鼠标变为小手手
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }

        // 鼠标点击事件
        case WM_LBUTTONDOWN:
        {
            //
            return 0;
        }

        case WM_LBUTTONUP:
        {
            // 打开用户账户设置
            ShellExecute(NULL, L"open", L"control", L"userpasswords", NULL, SW_SHOWNORMAL);
            
            // 收起
            HWND hOwner = GetWindow(hwnd, GW_OWNER);
            if (hOwner)
            {
                ShowWindow(hOwner, SW_HIDE);
            }
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (IsWindowVisible(hwnd)) RenderAvatarWindow(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

HWND CreateAvatarWindow(HINSTANCE hInstance, HWND hOwner)
{
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), 0, 
        AvatarProc, 0, 0, 
        hInstance, NULL, 
        LoadCursor(NULL, IDC_ARROW), 
        NULL, NULL, 
        L"VistaStartMenuAvatar", 
        NULL 
    };
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"VistaStartMenuAvatar", L"Avatar",
        WS_POPUP, 0, 0, 0, 0, hOwner, NULL, hInstance, NULL
    );

    return hWnd;
}

// 重新设置位置 
// 用于任务栏上下左右到处放
void RecalculateMenuPosition(HWND hwnd) 
{
    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"User32.dll");
    if (hUser32) 
    {
        typedef UINT(WINAPI* GetDpiForWindowProc)(HWND);
        GetDpiForWindowProc pGetDpiForWindow = (GetDpiForWindowProc)GetProcAddress(hUser32, "GetDpiForWindow");
        if (pGetDpiForWindow) dpi = pGetDpiForWindow(hwnd);
    }
    float scale = dpi / 96.0f;
    if (scale <= 0) scale = 1.0f;

    // 根据 DPI 动态缩放
    int menuWidth = (int)(413 * scale);   // 两个基础值的设置
    int menuHeight = (int)(540 * scale);  // 其实并没有什么理由

    // 定位基于 Orb 
    RECT btnRect = {0};
    if (g_hStartBtn) GetWindowRect(g_hStartBtn, &btnRect);
        else if (g_hOrbWnd && IsWindowVisible(g_hOrbWnd)) GetWindowRect(g_hOrbWnd, &btnRect);
            else 
            {
                btnRect.left = 0;
                btnRect.top = GetSystemMetrics(SM_CYSCREEN);
            }

    HMONITOR hMonitor = MonitorFromRect(&btnRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mInfo = {sizeof(MONITORINFO)};
    GetMonitorInfo(hMonitor, &mInfo);

    // 获取任务栏位置和边缘
    APPBARDATA abd = {sizeof(APPBARDATA)};
    SHAppBarMessage(ABM_GETTASKBARPOS, &abd);

    int x = btnRect.left;
    int y = btnRect.top - menuHeight;

    // 根据任务栏所处屏幕边缘决定弹出逻辑
    if ((abd.uEdge == ABE_BOTTOM) || (btnRect.top > (mInfo.rcWork.bottom - 100)))
    {
        // 任务栏在底部则向上弹出
        y = btnRect.top - menuHeight;
        x = btnRect.left;
    } 
        else if ((abd.uEdge == ABE_TOP) || (btnRect.bottom < (mInfo.rcWork.top + 100)))
        {
            // 顶部向下
            y = btnRect.bottom;
            x = btnRect.left;
        } 
            else if ((abd.uEdge == ABE_LEFT) || (btnRect.right < (mInfo.rcWork.left + 100)))
            {
                // 左侧向右
                y = btnRect.bottom - menuHeight;
                x = btnRect.right;
            } 
                else if ((abd.uEdge == ABE_RIGHT) || (btnRect.left > (mInfo.rcWork.right - 100)))
                {
                    // 右侧向左
                    y = btnRect.bottom - menuHeight;
                    x = btnRect.left - menuWidth;
                }

    // 防超    
    if (x < mInfo.rcWork.left) x = mInfo.rcWork.left;
    if ((x + menuWidth) > mInfo.rcWork.right) 
        x = mInfo.rcWork.right - menuWidth;
    if (y < mInfo.rcWork.top) y = mInfo.rcWork.top;
    if ((y + menuHeight) > mInfo.rcWork.bottom) 
        y = mInfo.rcWork.bottom - menuHeight;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, menuWidth, menuHeight, 
        SWP_NOACTIVATE | SWP_NOZORDER);

    // 头像跟随主菜单移动
    if (g_hAvatarWnd)
    {
        int avatarWinSize = (int)(72.0f * scale); 
        int rightPaneWidth = (int)(140.0f * scale);

        int avatarX = x + menuWidth - (rightPaneWidth / 2) - (avatarWinSize / 2) - (int)(6.0f * scale);
        int avatarY = y - (int)(30.0f * scale); // 向上

        SetWindowPos(g_hAvatarWnd, HWND_TOPMOST, avatarX, avatarY, avatarWinSize, avatarWinSize,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }

    //// 圆角区域裁剪
    //int rgnRad = (int)(6.0f * scale * 2.0f);
    //HRGN hRgn = CreateRoundRectRgn(0, 0, menuWidth, menuHeight, rgnRad, rgnRad);
    //if (hRgn) SetWindowRgn(hwnd, hRgn, TRUE);
}

struct RecentItemData 
{
    std::wstring name;
    std::wstring path;
    FILETIME ftLastWrite;
};

// 构造 Recent 右键弹窗
void ShowRecentMenu(HWND hwndParent, int x, int y)
{
    HMENU hMenu = CreatePopupMenu();

    PWSTR path = nullptr;
    std::vector<RecentItemData> recentItems;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Recent, 0, NULL, &path)))
    {
        std::wstring searchPath = std::wstring(path) + L"\\*.lnk";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                std::wstring name = fd.cFileName;
                std::wstring fullPath = std::wstring(path) + L"\\" + name;

                // 剔除 .lnk 后缀
                size_t lastDot = name.find_last_of(L".");
                if (lastDot != std::wstring::npos) name = name.substr(0, lastDot);

                recentItems.push_back({ name, fullPath, fd.ftLastWriteTime });
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
        CoTaskMemFree(path);
    }

    // 按写入时间降序排序
    std::sort(recentItems.begin(), recentItems.end(), [](const RecentItemData& a, const RecentItemData& b) 
        { return CompareFileTime(&a.ftLastWrite, &b.ftLastWrite) > 0; });

    // 限制最多显示 15 个
    int count = 0;
    for (size_t i = 0; i < recentItems.size() && count < 15; ++i)
    {
        AppendMenuW(hMenu, MF_STRING, 1001 + count, recentItems[i].name.c_str());
        count++;
    }

    if (GetMenuItemCount(hMenu) == 0)
    {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"（空）");
    }

    int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hwndParent, NULL);
    DestroyMenu(hMenu);

    // 选择-执行-清场
    if ((cmd >= 1001) && (cmd < (1001 + count)))
    {
        int index = cmd - 1001;
        ShellExecuteW(NULL, L"open", recentItems[index].path.c_str(), NULL, NULL, SW_SHOWNORMAL);

        KillTimer(hwndParent, 1);
        ShowWindow(hwndParent, SW_HIDE);
        if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
    }
}

// 开始菜单窗口
LRESULT CALLBACK StartMenuProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) 
    {
        case WM_CREATE: 
        {
            //
            return 0;
        }

        case WM_PAINT: 
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (IsWindowVisible(hwnd)) RenderStartMenu(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // 定时器
		// 实现实时捕获屏幕内容
        case WM_TIMER:
        {
            if ((wParam == 1) && IsWindowVisible(hwnd))
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                UpdateBackgroundBlur(hwnd, rc.right - rc.left,
                    rc.bottom - rc.top);
                RenderStartMenu(hwnd);
            }
                else if (wParam == 2)
                {
                    KillTimer(hwnd, 2);

                    RECT rcWnd;
                    GetWindowRect(hwnd, &rcWnd);

                    for (size_t i = 0; i < g_RightItems.size(); ++i)
                    {
                        if (g_RightItems[i].Title == L"最近使用的项目")
                        {
                            // 右边沿作为起始点
                            int popupX = rcWnd.left + (int)g_RightItems[i].Bounds.right;
                            int popupY = rcWnd.top + (int)g_RightItems[i].Bounds.top;

                            ShowRecentMenu(hwnd, popupX, popupY);

                            InvalidateRect(hwnd, NULL, FALSE);
                            break;
                        }
                    }
                }
            return 0;
        }

        case WM_TOGGLE_STARTMENU: 
        {
            if (IsWindowVisible(hwnd)) 
            {
                KillTimer(hwnd, 1);
                KillTimer(hwnd, 2);
                ShowWindow(hwnd, SW_HIDE);
                if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
            } 
                else 
                {
                    // 弹出前重算位置
                    // 可能移动或变过分辨率
                    RecalculateMenuPosition(hwnd);

                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    UpdateBackgroundBlur(hwnd, rc.right - rc.left, rc.bottom - rc.top);
                    
                    RenderStartMenu(hwnd);
                    if (g_hAvatarWnd) 
                        RenderAvatarWindow(g_hAvatarWnd);

                    ShowWindow(hwnd, SW_SHOW);

                    if (g_hAvatarWnd)
                    {
                        //ShowWindow(g_hAvatarWnd, SW_SHOW);
                        SetWindowPos(g_hAvatarWnd, HWND_TOPMOST,
                            0, 0, 0, 0, 
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        
                    }

                    SetForegroundWindow(hwnd);
                    SetTimer(hwnd, 1, 33, NULL);  // 1000/10=100fps
                }
            return 0;
        }

        // 焦点管理
        // 点击空白或 ECS 隐藏菜单
        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE) 
            {
                POINT pt;
                GetCursorPos(&pt);
                if (WindowFromPoint(pt) == g_hOrbWnd) // 防止因左键 Orb 失焦而隐藏菜单
                    return 0;                         // 理论上应 LBUTTONUP 时再隐藏

                if (IsWindowVisible(hwnd)) 
                {
                    KillTimer(hwnd, 1);
                    KillTimer(hwnd, 2);
                    ShowWindow(hwnd, SW_HIDE);
                    if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
                    OutputDebugString(L"[Hook] Start menu auto-hidden due to focus loss\n");
                }
            }
            return 0;
        }

        case WM_KEYDOWN: 
        {
            if (wParam == VK_ESCAPE) 
            {
                KillTimer(hwnd, 1);
                KillTimer(hwnd, 2);
                ShowWindow(hwnd, SW_HIDE);
                if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_DESTROY: 
        {
            KillTimer(hwnd, 1);
            KillTimer(hwnd, 2);

            // 释放字符样式
            if (g_pLeftTextFormat) 
            { 
                g_pLeftTextFormat->Release(); 
                g_pLeftTextFormat = nullptr; 
            }
            if (g_pLeftSubTextFormat) 
            { 
                g_pLeftSubTextFormat->Release(); 
                g_pLeftSubTextFormat = nullptr; 
            }
            if (g_pLeftBoldTextFormat) 
            { 
                g_pLeftBoldTextFormat->Release(); 
                g_pLeftBoldTextFormat = nullptr; 
            }
            if (g_pRightTextFormat) 
            { 
                g_pRightTextFormat->Release(); 
                g_pRightTextFormat = nullptr; 
            }

            // 释放图片句柄
            if (g_pAllProgramsArrow) 
            { 
                g_pAllProgramsArrow->Release(); 
                g_pAllProgramsArrow = nullptr; 
            }
            if (g_pAvatarBitmap)
            {
                g_pAvatarBitmap->Release();
                g_pAvatarBitmap = nullptr;
            }

            if (g_pMenuRenderTarget) 
            {
                g_pMenuRenderTarget->Release();
                g_pMenuRenderTarget = nullptr;
            }
            if (g_pMenuWicBitmap) 
            {
                g_pMenuWicBitmap->Release();
                g_pMenuWicBitmap = nullptr;
            }
            if (g_pBtnNormal) 
            {
                g_pBtnNormal->Release(); 
                g_pBtnNormal = nullptr; 
            }
            if (g_pBtnHover) 
            { 
                g_pBtnHover->Release(); 
                g_pBtnHover = nullptr; 
            }
            if (g_pBtnPressed) 
            { 
                g_pBtnPressed->Release(); 
                g_pBtnPressed = nullptr; 
            }

			// 释放搜索框资源
            if (g_pSearchTextFormat) 
            { 
                g_pSearchTextFormat->Release(); 
                g_pSearchTextFormat = nullptr; 
            }
            if (g_pSearchInputTextFormat)
            {
                g_pSearchInputTextFormat->Release();
                g_pSearchInputTextFormat = nullptr;
            }
            if (g_pSearchIcon) 
            { 
                g_pSearchIcon->Release(); 
                g_pSearchIcon = nullptr; 
            }
            if (g_pSearchClearBg)
            {
                g_pSearchClearBg->Release();
                g_pSearchClearBg = nullptr;
            }
            return 0;
        }

        case WM_WINDOWPOSCHANGED:
        {
            // 菜单切换时强制刷新按钮
            if (g_hOrbWnd)
            {
                InvalidateRect(g_hOrbWnd, NULL, FALSE);
            }
            break;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            bool bNeedRedraw = false;
            bool bHoverRecent = false;

            for (size_t i = 0; i < g_LeftItems.size(); i++)
            {
                StartMenuItem& item = g_LeftItems[i];
                bool bWasHovered = item.IsHovered;
                item.IsHovered = ((pt.x > item.Bounds.left) && (pt.x < item.Bounds.right) &&
                    (pt.y > item.Bounds.top) && (pt.y < item.Bounds.bottom));
                if (item.IsSeparator || (item.Title == L"-"))
                    item.IsHovered = false; // 取消分隔线悬停
                if (bWasHovered != item.IsHovered) bNeedRedraw = true;
            }

            for (size_t i = 0; i < g_RightItems.size(); i++)
            {
                StartMenuItem& item = g_RightItems[i];
                bool bWasHovered = item.IsHovered;
                item.IsHovered = ((pt.x > item.Bounds.left) && (pt.x < item.Bounds.right) &&
                    (pt.y > item.Bounds.top) && (pt.y < item.Bounds.bottom));
                if ((item.Title == L"-") || (item.Title == L"")) item.IsHovered = false;
                if (bWasHovered != item.IsHovered) bNeedRedraw = true;
                
                if (item.IsHovered && item.Title == L"最近使用的项目")
                {
                    bHoverRecent = true;
                }
            }

            // 击中测试
            bool pwr = ((pt.x > g_PowerBtnBounds.left) && (pt.x < g_PowerBtnBounds.right) && (pt.y > g_PowerBtnBounds.top) && (pt.y < g_PowerBtnBounds.bottom));
            bool lck = ((pt.x > g_LockBtnBounds.left) && (pt.x < g_LockBtnBounds.right) && (pt.y > g_LockBtnBounds.top) && (pt.y < g_LockBtnBounds.bottom));
            bool arr = ((pt.x > g_ArrowBtnBounds.left) && (pt.x < g_ArrowBtnBounds.right) && (pt.y > g_ArrowBtnBounds.top) && (pt.y < g_ArrowBtnBounds.bottom));

            if (g_bPowerHovered != pwr)
            {
                g_bPowerHovered = pwr;
                bNeedRedraw = true;
            }
            if (g_bLockHovered != lck) 
            { 
                g_bLockHovered = lck; 
                bNeedRedraw = true; 
            }
            if (g_bArrowHovered != arr) 
            { 
                g_bArrowHovered = arr; 
                bNeedRedraw = true; 
            }

            // 悬停 400ms 定时器
            static bool s_wasHoverRecent = false;
            if (bHoverRecent && !s_wasHoverRecent) SetTimer(hwnd, 2, 400, NULL);
                else if (!bHoverRecent && s_wasHoverRecent) KillTimer(hwnd, 2);

            s_wasHoverRecent = bHoverRecent;

            if (bNeedRedraw) RenderStartMenu(hwnd);

            // 叉叉的悬浮命中测试
            bool iconHovered = (!g_SearchText.empty()) &&
                ((pt.x > g_SearchIconBounds.left) && (pt.x < g_SearchIconBounds.right) &&
                    (pt.y > g_SearchIconBounds.top) && (pt.y < g_SearchIconBounds.bottom));

            if (g_bSearchIconHovered != iconHovered)
            {
                g_bSearchIconHovered = iconHovered;
                bNeedRedraw = true;
            }

            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            bool bNeedRedraw = false;

            // 检测按下状态
            if ((pt.x > g_PowerBtnBounds.left) && (pt.x < g_PowerBtnBounds.right) &&
                (pt.y > g_PowerBtnBounds.top) && (pt.y < g_PowerBtnBounds.bottom)) 
            {
                g_bPowerPressed = true; 
                bNeedRedraw = true;
            }
            if ((pt.x > g_LockBtnBounds.left) && (pt.x < g_LockBtnBounds.right) &&
                (pt.y > g_LockBtnBounds.top) && (pt.y < g_LockBtnBounds.bottom)) 
            {
                g_bLockPressed = true; 
                bNeedRedraw = true;
            }
            if ((pt.x > g_ArrowBtnBounds.left) && (pt.x < g_ArrowBtnBounds.right) &&
                (pt.y > g_ArrowBtnBounds.top) && (pt.y < g_ArrowBtnBounds.bottom)) 
            {
                g_bArrowPressed = true; 
                bNeedRedraw = true;
            }

            // 点击搜索框测试
            bool bHitSearch = ((pt.x > g_SearchBoxBounds.left) && (pt.x < g_SearchBoxBounds.right) &&
                (pt.y > g_SearchBoxBounds.top) && (pt.y < g_SearchBoxBounds.bottom));

            // 点击叉叉不清除
            // 松开再清除
            if (!g_SearchText.empty() &&
                (pt.x > g_SearchIconBounds.left) && (pt.x < g_SearchIconBounds.right) &&
                (pt.y > g_SearchIconBounds.top) && (pt.y < g_SearchIconBounds.bottom))
            {
                g_bSearchIconPressed = true;
                bNeedRedraw = true;

            }
                else if (bHitSearch != g_bSearchFocused)
                {
                    g_bSearchFocused = bHitSearch;
                    bNeedRedraw = true;
                }
            
            if (bNeedRedraw) RenderStartMenu(hwnd);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) }; // 【新增取点】
            bool bNeedRedraw = false;

            // 清理搜索叉叉从按压恢复普通态
            if (g_bSearchIconPressed)
            {
                g_bSearchIconPressed = false;

                // 松开且仍在范围内，属于正规的清零点击
                if (!g_SearchText.empty() &&
                    (pt.x > g_SearchIconBounds.left) && (pt.x < g_SearchIconBounds.right) &&
                    (pt.y > g_SearchIconBounds.top) && (pt.y < g_SearchIconBounds.bottom))
                {
                    g_SearchText.clear();
                    g_bSearchFocused = true; // 清空了依然留焦点
                }
                bNeedRedraw = true;
            }

            // 清理按下状态
            if (g_bPowerPressed || g_bLockPressed || g_bArrowPressed)
            {
                g_bPowerPressed = false;
                g_bLockPressed = false;
                g_bArrowPressed = false;
                bNeedRedraw = true;
            }
            
            if (bNeedRedraw) RenderStartMenu(hwnd);

            bool bItemClicked = false;

            // 左侧列表
            for (size_t i = 0; i < g_LeftItems.size(); ++i)
            {
                if (g_LeftItems[i].IsHovered)
                {

                    bItemClicked = true;
                    break;
                }
            }

            // 右侧列表
            if (!bItemClicked)
            {
                for (size_t i = 0; i < g_RightItems.size(); i++)
                {
                    if (g_RightItems[i].IsHovered)
                    {
                        std::wstring title = g_RightItems[i].Title;

                        // 打开相应页面
                        if (title == L"Administrator")
                            ShellExecuteW(NULL, L"open", L"shell:Profile", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"文档")
                            ShellExecuteW(NULL, L"open", L"shell:Personal", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"图片")
                            ShellExecuteW(NULL, L"open", L"shell:My Pictures", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"音乐")
                            ShellExecuteW(NULL, L"open", L"shell:My Music", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"最近使用的项目")
                            ShellExecuteW(NULL, L"open", L"shell:Recent", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"计算机")
                            ShellExecuteW(NULL, L"open", L"shell:MyComputerFolder", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"网络")
                            ShellExecuteW(NULL, L"open", L"shell:NetworkPlacesFolder", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"连接到")
                            ShellExecuteW(NULL, L"open", L"ms-availablenetworks:", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"控制面板")
                            ShellExecuteW(NULL, L"open", L"control", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"默认程序")
                            ShellExecuteW(NULL, L"open", L"ms-settings:defaultapps", NULL, NULL, SW_SHOWNORMAL);
                        else if (title == L"帮助和支持")
                            ShellExecuteW(NULL, L"open", L"ms-contact-support:", NULL, NULL, SW_SHOWNORMAL);

                        bItemClicked = true;
                        break;
                    }
                }
            }

            if (!bItemClicked)
            {
                if (g_bPowerHovered)
                {
                    // 睡眠
                    SetSuspendState(FALSE, FALSE, FALSE);
                    bItemClicked = true;
                }
                else if (g_bLockHovered)
                {
                    // 锁定
                    ShellExecuteW(NULL, L"open", L"rundll32.exe", L"user32.dll,LockWorkStation", NULL, SW_SHOWNORMAL);
                    bItemClicked = true;
                }
                else if (g_bArrowHovered)
                {
                    // 关机选项（待
                    bItemClicked = true;
                }
            }

            // 点击后关闭开始菜单
            if (bItemClicked)
            {
                KillTimer(hwnd, 1);
                KillTimer(hwnd, 2);
                ShowWindow(hwnd, SW_HIDE);
                if (g_hAvatarWnd)
                {
                    ShowWindow(g_hAvatarWnd, SW_HIDE);
                }
            }

            return 0;
        }

        // 捕获键盘打字输入
        case WM_CHAR:
        {
            if (g_bSearchFocused)
            {
                wchar_t ch = (wchar_t)wParam;
                if (ch == VK_BACK) // 退格键
                {
                    if (!g_SearchText.empty()) g_SearchText.pop_back();
                }
                    else if (ch == VK_RETURN) // 回车执行搜索
                    {
                        // 搜索逻辑（占位）
                    }
                        else if (ch >= 0x20)
                        {
                            g_SearchText += ch;
                        }
                RenderStartMenu(hwnd);
            }
            return 0;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 StartMenu 窗口
HWND CreateStartMenuWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"VistaStartMenu";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = StartMenuProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // 设置光标
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, // 置顶 & 不进任务栏 & 分层窗口支持
        className,
        L"VistaStartMenu",
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    if (hWnd)
    {
        // 屏幕截图中隐形
        // SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);

        //  DWM 圆角裁剪 
        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }

    InitMenuItems(); 

    g_hAvatarWnd = CreateAvatarWindow(hInstance, hWnd);
    //SetWindowDisplayAffinity(g_hAvatarWnd, WDA_EXCLUDEFROMCAPTURE); // 头像窗口同样隐形

    return hWnd;
}