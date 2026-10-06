#pragma once
#include <windows.h>
inline COLORREF ClientPixel(HWND window, int x, int y) {
    RECT area{}; GetClientRect(window,&area);
    HDC dc = GetDC(window), buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc,area.right,area.bottom);
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer); ReleaseDC(window,dc); return CLR_INVALID;
    }
    auto previous = SelectObject(buffer,bitmap);
    const COLORREF color = PrintWindow(window,buffer,PW_CLIENTONLY) ? GetPixel(buffer,x,y) : CLR_INVALID;
    SelectObject(buffer,previous); DeleteObject(bitmap); DeleteDC(buffer); ReleaseDC(window,dc); return color;
}
