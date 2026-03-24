#include "pch.h"
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include "StartMenuWnd.h"
#include "GlobalState.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

ID2D1Bitmap* g_pAvatarBitmap = nullptr; 
ID2D1RenderTarget* g_pMenuRenderTarget = nullptr;
IWICBitmap* g_pMenuWicBitmap = nullptr;
int g_MenuWidth = 0;
int g_MenuHeight = 0;

HWND g_hAvatarWnd = NULL; // 头像窗口句柄
ID2D1RenderTarget* g_pAvatarRenderTarget = nullptr;
IWICBitmap* g_pAvatarWicBitmap = nullptr;

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

// 捕获桌面底部
    RECT rcBgRect;
    GetWindowRect(hwnd, &rcBgRect);
    int screenX = rcBgRect.left;
    int screenY = rcBgRect.top;

    HDC hdcBgScreen = GetDC(NULL);
    HDC hdcBgMem = CreateCompatibleDC(hdcBgScreen);
    BITMAPINFO bmiBg = { 0 };
    bmiBg.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmiBg.bmiHeader.biWidth = width;
    bmiBg.bmiHeader.biHeight = -height;
    bmiBg.bmiHeader.biPlanes = 1;
    bmiBg.bmiHeader.biBitCount = 32;
    bmiBg.bmiHeader.biCompression = BI_RGB;

    void* pBgBits = nullptr;
    HBITMAP hBgBitmap = CreateDIBSection(hdcBgScreen, &bmiBg, DIB_RGB_COLORS, &pBgBits, NULL, 0);
    HBITMAP hOldBg = (HBITMAP)SelectObject(hdcBgMem, hBgBitmap);

    // 捕获被覆盖屏幕
    BitBlt(hdcBgMem, 0, 0, width, height, hdcBgScreen, screenX, screenY, SRCCOPY);

    // 半径 10 的模糊
    FastBoxBlur((BYTE*)pBgBits, width, height, 7);
    FastBoxBlur((BYTE*)pBgBits, width, height, 7);

    ID2D1Bitmap* pBgD2DBitmap = nullptr;

    D2D1_BITMAP_PROPERTIES bgProps = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    g_pMenuRenderTarget->CreateBitmap(D2D1::SizeU(width, height), pBgBits, width * 4, bgProps, &pBgD2DBitmap);

    SelectObject(hdcBgMem, hOldBg);
    DeleteObject(hBgBitmap);
    DeleteDC(hdcBgMem);
    ReleaseDC(NULL, hdcBgScreen);
//


    // 背景全透明
    g_pMenuRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    ID2D1SolidColorBrush *pBrush = nullptr;

    D2D1_ROUNDED_RECT windowRRect = D2D1::RoundedRect(
        D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 
        cornerRadius, cornerRadius
    );

    // 铺上模糊背景
    if (pBgD2DBitmap)
    {
        ID2D1BitmapBrush* pBgBrush = nullptr;
        if (SUCCEEDED(g_pMenuRenderTarget->CreateBitmapBrush(pBgD2DBitmap, &pBgBrush)))
        {
            g_pMenuRenderTarget->FillRoundedRectangle(windowRRect, pBgBrush);
            pBgBrush->Release();
        }
        pBgD2DBitmap->Release();
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
            D2D1::RectF(padding, padding, width - padding - rightPaneWidth - 2.0f * dpiScale, height - padding - searchHeight),
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
            height - padding - searchHeight + 8.0f * dpiScale, 
            width - padding - rightPaneWidth - 2.0f * dpiScale, 
            height - padding
        );
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
    }
    
    if (pWhiteBrush) pWhiteBrush->Release();

// 菜单窗口外圈边缘
    ID2D1SolidColorBrush* pDarkBorderBrush = nullptr;
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.8f), &pDarkBorderBrush)))
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
    if (SUCCEEDED(g_pMenuRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f, 0.4f), &pLightBorderBrush)))
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
            if (wParam == 1 && IsWindowVisible(hwnd))
            {
                RenderStartMenu(hwnd);
            }
            return 0;
        }

        case WM_TOGGLE_STARTMENU: 
        {
            if (IsWindowVisible(hwnd)) 
            {
                KillTimer(hwnd, 1);
                ShowWindow(hwnd, SW_HIDE);
                if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
                OutputDebugString(L"[Hook] Start menu hidden\n");
            } 
                else 
                {
                    // 弹出前重算位置
                    // 可能移动或变过分辨率
                    RecalculateMenuPosition(hwnd);
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                    RenderStartMenu(hwnd);

                    SetTimer(hwnd, 1, 10, NULL);  // 1000/10=100fps

                    if (g_hAvatarWnd)
                    {
                        //ShowWindow(g_hAvatarWnd, SW_SHOW);
                        SetWindowPos(g_hAvatarWnd, HWND_TOPMOST,
                            0, 0, 0, 0, 
                            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        RenderAvatarWindow(g_hAvatarWnd);
                    }

                    OutputDebugString(L"[Hook] Start menu shown\n");
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

                //if (WindowFromPoint(pt) == g_hAvatarWnd) 
                 //   return 0;

                if (IsWindowVisible(hwnd)) 
                {
                    KillTimer(hwnd, 1);
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
                ShowWindow(hwnd, SW_HIDE);
                if (g_hAvatarWnd) ShowWindow(g_hAvatarWnd, SW_HIDE);
                return 0;
            }
            break;
        }

        case WM_DESTROY: 
        {
            KillTimer(hwnd, 1);

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
        SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE);

        //  DWM 圆角裁剪 
        DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    }

    g_hAvatarWnd = CreateAvatarWindow(hInstance, hWnd);
    SetWindowDisplayAffinity(g_hAvatarWnd, WDA_EXCLUDEFROMCAPTURE); // 头像窗口同样隐形

    return hWnd;
}