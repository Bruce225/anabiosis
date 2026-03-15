#include "pch.h"
#include "StartOrbWnd.h"
#include "GlobalState.h"

ID2D1RenderTarget* g_pOrbRenderTarget = nullptr;
ID2D1Bitmap* g_pOrbBitmap = nullptr;
IWICBitmap* g_pOrbWicBitmap = nullptr;
int g_OrbState = 0; // 0: 默认, 1: 悬停, 2: 按下
bool g_bOrbTrackingMouse = false;
int g_OrbWidth = 0;
int g_OrbHeight = 0;

wchar_t g_OrbImagePath[MAX_PATH] = {0};

// 读取和渲染
bool LoadOrbBitmap()
{
    if (!g_pOrbRenderTarget) return false;
    if (g_pOrbBitmap) return true;

    // 首次获取 DLL 路径，构建图片路径
    // 后续直接使用缓存路径
    if (g_OrbImagePath[0] == L'\0')
    {
        HMODULE hModule = NULL;

        // 获取当前代码所在 DLL 模块句柄
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)&LoadOrbBitmap,
            &hModule
        );

        GetModuleFileNameW(hModule, g_OrbImagePath, MAX_PATH);

        // 反斜杠截断
        wchar_t* lastSlash = wcsrchr(g_OrbImagePath, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        wcscat_s(g_OrbImagePath, MAX_PATH, L"\\source\\Orb.bmp");
    }

    // 读取 Orb
    HANDLE hFile = CreateFileW(g_OrbImagePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    DWORD fileSize = GetFileSize(hFile, NULL);
    BYTE* fileData = new BYTE[fileSize];

    DWORD bytesRead;
    ReadFile(hFile, fileData, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)fileData;
    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(fileData + sizeof(BITMAPFILEHEADER));

    int imgWidth = bih->biWidth;
    int imgHeight = abs(bih->biHeight);
    bool topDown = (bih->biHeight < 0);

    if (bih->biBitCount != 32)
    {
        delete[] fileData;
        return false;
    }

    BYTE* pixels = fileData + bfh->bfOffBits;

    // BMP 翻转
    int stride = imgWidth * 4;
    BYTE* correctedPixels = new BYTE[imgHeight * stride];
    if (!correctedPixels) 
    { 
        delete[] fileData; 
        return false; 
    }

    for (int y = 0; y < imgHeight; y++)
    {
        int srcRow = topDown ? y : (imgHeight - 1 - y);
        BYTE* srcLine = pixels + srcRow * stride;
        BYTE* dstLine = correctedPixels + y * stride;

        for (int x = 0; x < imgWidth; x++)
        {
            BYTE b = srcLine[x*4+0];
            BYTE g = srcLine[x*4+1];
            BYTE r = srcLine[x*4+2];
            BYTE a = srcLine[x*4+3];

            // 预乘 Alpha
            dstLine[x*4+0] = (BYTE)((b * a) / 255);
            dstLine[x*4+1] = (BYTE)((g * a) / 255);
            dstLine[x*4+2] = (BYTE)((r * a) / 255);
            dstLine[x*4+3] = a;
        }
    }

    // 从像素创建 D2D Bitmap
    D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    g_pOrbRenderTarget->CreateBitmap(
        D2D1::SizeU(imgWidth, imgHeight),
        correctedPixels,
        stride,
        bmpProps,
        &g_pOrbBitmap
    );

    delete[] correctedPixels;
    delete[] fileData;

    return g_pOrbBitmap != nullptr;
}

void RenderOrb(HWND hwnd)
{
    if (!g_pD2DFactory || !g_pWICFactory) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    // 尺寸变化时重建 WIC 位图和渲染目标
    if (g_pOrbWicBitmap && ((width != g_OrbWidth) || (height != g_OrbHeight)))
    {
        if (g_pOrbRenderTarget) 
        { 
            g_pOrbRenderTarget->Release(); 
            g_pOrbRenderTarget = nullptr; 
        }
        if (g_pOrbBitmap) 
        { 
            g_pOrbBitmap->Release(); 
            g_pOrbBitmap = nullptr; 
        }
        g_pOrbWicBitmap->Release(); 
        g_pOrbWicBitmap = nullptr;
    }

    // 创建 WIC 内存位图
    if (!g_pOrbWicBitmap)
    {
        HRESULT hr = g_pWICFactory->CreateBitmap(
            width, height,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnLoad,
            &g_pOrbWicBitmap
        );
        if (FAILED(hr)) return;
        g_OrbWidth = width;
        g_OrbHeight = height;
    }

    if (!g_pOrbRenderTarget)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT
        );

        HRESULT hr = g_pD2DFactory->CreateWicBitmapRenderTarget(g_pOrbWicBitmap, props, &g_pOrbRenderTarget);
        if (FAILED(hr)) return;
    }

    g_pOrbRenderTarget->BeginDraw();

    // 0.005f 防止全透明的穿透
    g_pOrbRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.005f));

    LoadOrbBitmap();

    if (g_pOrbBitmap)
    {
        D2D1_SIZE_F renderSize = g_pOrbRenderTarget->GetSize();
        D2D1_SIZE_F bitmapSize = g_pOrbBitmap->GetSize();

        // 拆分三状态 bmp 图片
        float singleHeight = bitmapSize.height / 3.0f;
        float yStart = singleHeight * g_OrbState; // 单个高度乘以 0-2 刚刚好

        // 计算图标拉伸
        float scale = min(renderSize.width / bitmapSize.width, renderSize.height / singleHeight);
        float drawWidth = bitmapSize.width * scale;
        float drawHeight = singleHeight * scale;
        float xOffset = (renderSize.width - drawWidth) / 2.0f;
        float yOffset = (renderSize.height - drawHeight) / 2.0f;

        D2D1_RECT_F destRect = D2D1::RectF(xOffset, yOffset, xOffset + drawWidth, yOffset + drawHeight);
        D2D1_RECT_F srcRect = D2D1::RectF(0.0f, yStart, bitmapSize.width, yStart + singleHeight);

        g_pOrbRenderTarget->DrawBitmap(
            g_pOrbBitmap,
            destRect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            srcRect
        );
    }
        else
        {
            // 
        }

    HRESULT hr = g_pOrbRenderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
    {
        if (g_pOrbRenderTarget) 
        { 
            g_pOrbRenderTarget->Release(); 
            g_pOrbRenderTarget = nullptr; 
        }
        if (g_pOrbBitmap) 
        { 
            g_pOrbBitmap->Release(); 
            g_pOrbBitmap = nullptr; 
        }
        if (g_pOrbWicBitmap) 
        { 
            g_pOrbWicBitmap->Release(); 
            g_pOrbWicBitmap = nullptr; 
        }
        return;
    }

    HDC hdcScreen = GetDC(NULL);
    HDC hMemoryDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 反转一下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pDibBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);

    IWICBitmapLock* pLock = nullptr;
    WICRect lockRect = { 0, 0, width, height };
    hr = g_pOrbWicBitmap->Lock(&lockRect, WICBitmapLockRead, &pLock);
    if (SUCCEEDED(hr))
    {
        UINT cbStride = 0;
        UINT cbBufferSize = 0;
        BYTE* pWicPixels = nullptr;

        pLock->GetStride(&cbStride);
        pLock->GetDataPointer(&cbBufferSize, &pWicPixels);

        // 逐行拷贝像素
        UINT dibStride = width * 4;
        for (int y = 0; y < height; y++)
        {
            memcpy(
                (BYTE*)pDibBits + y * dibStride,
                pWicPixels + y * cbStride,
                dibStride
            );
        }

        pLock->Release();
    }

    // 分层窗口背景透明
    POINT ptSrc = { 0, 0 };
    SIZE winSize = { width, height };

    // 使用 Alpha 通道
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

// Orb 按钮窗口过程
LRESULT CALLBACK OrbWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
        {            
            RenderOrb(hwnd);  // 防止 WS_EX_LAYERED 不主动 WM_PAINT
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            RenderOrb(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            RECT rc;
            GetClientRect(hwnd, &rc);
            float cx = (rc.right - rc.left) / 2.0f;
            float cy = (rc.bottom - rc.top) / 2.0f;
            float dx = pt.x - cx;
            float dy = pt.y - cy;
            float radius = min(cx, cy) * 0.95f; // 圆形半径检测

            if ((dx * dx + dy * dy) > radius * radius)
            {
                // 圆外不发光
                if (g_bOrbTrackingMouse && g_OrbState != 0) 
                {
                    g_OrbState = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            if (!g_bOrbTrackingMouse)
            {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_bOrbTrackingMouse = true;
            }

            if (g_OrbState != 2) // 未按下
            {
                g_OrbState = 1; // 悬停
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
        {
            g_bOrbTrackingMouse = false;
            g_OrbState = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            RECT rc;
            GetClientRect(hwnd, &rc);
            float cx = (rc.right - rc.left) / 2.0f;
            float cy = (rc.bottom - rc.top) / 2.0f;
            float dx = pt.x - cx;
            float dy = pt.y - cy;
            float radius = min(cx, cy) * 0.95f;

			if ((dx * dx + dy * dy) > radius * radius) return 0; // 圆外不响应

            SetCapture(hwnd);
            g_OrbState = 2;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            ReleaseCapture();

            // 检测鼠标是否仍在按钮圆形区域内
            POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            float cx = (rc.right - rc.left) / 2.0f;
            float cy = (rc.bottom - rc.top) / 2.0f;
            float dx = pt.x - cx;
            float dy = pt.y - cy;
            float radius = min(cx, cy) * 0.95f;

            if ((dx * dx + dy * dy) <= radius * radius)
            {
                g_OrbState = 1; // 悬停
                if (g_hMenuWnd)
                    PostMessage(g_hMenuWnd, WM_TOGGLE_STARTMENU, 0, 0);

            }
                else g_OrbState = 0;

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_DESTROY:
        {
            if (g_pOrbRenderTarget)
            {
                g_pOrbRenderTarget->Release();
                g_pOrbRenderTarget = nullptr;
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 创建 Orb 覆盖窗口
HWND CreateOrbWindow(HINSTANCE hInstance)
{
    const wchar_t* className = L"VistaOrbButton";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OrbWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);

    HWND hTaskbar = FindWindow(L"Shell_TrayWnd", NULL);

    // 获取原 Start 按钮坐标以覆盖
    RECT r = {};
    if (g_hStartBtn) GetWindowRect(g_hStartBtn, &r);

    int baseW = r.right - r.left;
    int baseH = r.bottom - r.top;

    // 放大 Orb
    float scaleFactor = 1.30f;
    int w = (int)(baseW * scaleFactor);
    int h = (int)(baseH * scaleFactor);

    // Orb 居中
    int offsetX = (w - baseW) / 2;
    int offsetY = (h - baseH) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,  // 置顶 & 不进任务栏 & 分层窗口支持
        className, L"VistaOrb",
        WS_POPUP,
        r.left - offsetX, r.top - offsetY, w, h,
        hTaskbar,
        NULL, hInstance, NULL
    );

    if (hWnd) ShowWindow(hWnd, SW_SHOW);
    return hWnd;
}