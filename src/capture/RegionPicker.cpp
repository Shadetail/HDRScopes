#include "capture/RegionPicker.h"
#include <algorithm>

namespace regionpicker {

static const wchar_t* kClass = L"HDRScopesRegionPicker";

struct PickState {
    RECT    output{};
    int     w = 0, h = 0;
    HDC     shotDC = nullptr;     // memory DC holding the frozen screenshot
    HBITMAP shotBmp = nullptr, shotOld = nullptr;
    bool    dragging = false, done = false, ok = false;
    POINT   start{}, cur{};
};
static PickState* g_ps = nullptr;

static RECT NormRect(POINT a, POINT b) {
    RECT r;
    r.left   = std::min(a.x, b.x); r.top    = std::min(a.y, b.y);
    r.right  = std::max(a.x, b.x); r.bottom = std::max(a.y, b.y);
    return r;
}

static void Paint(HWND hwnd) {
    PickState* s = g_ps;
    if (!s) return;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Double-buffer.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, s->w, s->h);
    HBITMAP oldb = (HBITMAP)SelectObject(mem, bmp);

    // Base = the frozen screenshot.
    BitBlt(mem, 0, 0, s->w, s->h, s->shotDC, 0, 0, SRCCOPY);

    // Dim everything with ~55% black via a 1x1 source stretched with AlphaBlend.
    HDC bDC = CreateCompatibleDC(hdc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1; bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bits) ((DWORD*)bits)[0] = 0x00000000; // black, alpha handled by blend
    HBITMAP bOld = (HBITMAP)SelectObject(bDC, bBmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 140, 0 };
    AlphaBlend(mem, 0, 0, s->w, s->h, bDC, 0, 0, 1, 1, bf);

    RECT sel = NormRect(s->start, s->cur);
    int sw = sel.right - sel.left, sh = sel.bottom - sel.top;
    if (sw > 0 && sh > 0) {
        // Restore the bright screenshot inside the selection.
        BitBlt(mem, sel.left, sel.top, sw, sh, s->shotDC, sel.left, sel.top, SRCCOPY);
        // Border.
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(80, 200, 255));
        HGDIOBJ op = SelectObject(mem, pen);
        HGDIOBJ ob = SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, sel.left, sel.top, sel.right, sel.bottom);
        SelectObject(mem, op); SelectObject(mem, ob); DeleteObject(pen);
        // Dimensions readout (in desktop coords).
        wchar_t txt[80];
        swprintf(txt, 80, L"%d x %d  @ (%ld, %ld)", sw, sh,
                 sel.left + s->output.left, sel.top + s->output.top);
        SetBkMode(mem, OPAQUE); SetBkColor(mem, RGB(0, 0, 0));
        SetTextColor(mem, RGB(255, 255, 255));
        int ty = (sel.top > 22) ? sel.top - 20 : sel.bottom + 4;
        TextOutW(mem, sel.left + 2, ty, txt, (int)wcslen(txt));
    }

    // Instructions.
    const wchar_t* hint =
        L"Drag to select region   |   release / Enter = confirm   |   Esc / right-click = cancel";
    SetBkMode(mem, OPAQUE); SetBkColor(mem, RGB(0, 0, 0));
    SetTextColor(mem, RGB(230, 230, 230));
    TextOutW(mem, 16, 16, hint, (int)wcslen(hint));

    BitBlt(hdc, 0, 0, s->w, s->h, mem, 0, 0, SRCCOPY);

    SelectObject(bDC, bOld); DeleteObject(bBmp); DeleteDC(bDC);
    SelectObject(mem, oldb); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    PickState* s = g_ps;
    if (!s) return DefWindowProc(h, m, w, l);
    switch (m) {
    case WM_LBUTTONDOWN:
        s->dragging = true;
        s->start = { (int)(short)LOWORD(l), (int)(short)HIWORD(l) };
        s->cur = s->start;
        SetCapture(h);
        return 0;
    case WM_MOUSEMOVE:
        if (s->dragging) {
            s->cur = { (int)(short)LOWORD(l), (int)(short)HIWORD(l) };
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->dragging) {
            s->dragging = false;
            ReleaseCapture();
            RECT sel = NormRect(s->start, s->cur);
            if (sel.right - sel.left >= 4 && sel.bottom - sel.top >= 4) s->ok = true;
            s->done = true;
        }
        return 0;
    case WM_RBUTTONDOWN:
        s->ok = false; s->done = true;
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { s->ok = false; s->done = true; }
        else if (w == VK_RETURN) {
            RECT sel = NormRect(s->start, s->cur);
            if (sel.right - sel.left >= 4 && sel.bottom - sel.top >= 4) s->ok = true;
            s->done = true;
        }
        return 0;
    case WM_PAINT:
        Paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1; // we fully repaint
    }
    return DefWindowProc(h, m, w, l);
}

bool PickScreenRegion(const RECT& outputRect, RECT& out) {
    PickState s;
    s.output = outputRect;
    s.w = outputRect.right - outputRect.left;
    s.h = outputRect.bottom - outputRect.top;
    if (s.w <= 0 || s.h <= 0) return false;

    // Freeze a screenshot of the output region (CAPTUREBLT grabs layered windows).
    HDC screen = GetDC(nullptr);
    s.shotDC  = CreateCompatibleDC(screen);
    s.shotBmp = CreateCompatibleBitmap(screen, s.w, s.h);
    s.shotOld = (HBITMAP)SelectObject(s.shotDC, s.shotBmp);
    BitBlt(s.shotDC, 0, 0, s.w, s.h, screen, outputRect.left, outputRect.top, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = PickProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"",
        WS_POPUP, outputRect.left, outputRect.top, s.w, s.h,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) return false;

    g_ps = &s;
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);

    MSG msg;
    while (!s.done && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyWindow(h);
    g_ps = nullptr;

    SelectObject(s.shotDC, s.shotOld);
    DeleteObject(s.shotBmp);
    DeleteDC(s.shotDC);

    if (s.ok) {
        RECT sel = NormRect(s.start, s.cur);
        out.left   = sel.left   + outputRect.left;
        out.top    = sel.top    + outputRect.top;
        out.right  = sel.right  + outputRect.left;
        out.bottom = sel.bottom + outputRect.top;
    }
    return s.ok;
}

} // namespace regionpicker
