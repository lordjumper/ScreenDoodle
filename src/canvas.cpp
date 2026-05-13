#include "canvas.h"
#include "state.h"
#include "widget.h"

static HBITMAP CreateArgbDIB(int w, int h, void** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    return bmp;
}

void CreateCanvasSurface() {
    DestroyCanvasSurface();

    A.sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    A.sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    A.sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    A.sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (A.sw <= 0 || A.sh <= 0) return;

    HDC screen = GetDC(nullptr);

    void* db = nullptr;
    A.drawBmp = CreateArgbDIB(A.sw, A.sh, &db);
    A.drawBits = (uint32_t*)db;
    A.drawDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!A.drawBmp || !A.drawBits || !A.drawDC) {
        DestroyCanvasSurface();
        return;
    }
    A.drawOldBmp = SelectObject(A.drawDC, A.drawBmp);

    A.savedBits = new (std::nothrow) uint32_t[(size_t)A.sw * A.sh];
    if (!A.savedBits) {
        DestroyCanvasSurface();
        return;
    }

    std::fill_n(A.drawBits,  (size_t)A.sw * A.sh, 0x01000000u);
    std::fill_n(A.savedBits, (size_t)A.sw * A.sh, 0x01000000u);
    A.history.clear();
    A.history.shrink_to_fit();
}

void DestroyCanvasSurface() {
    if (A.drawDC && A.drawOldBmp) {
        SelectObject(A.drawDC, A.drawOldBmp);
        A.drawOldBmp = nullptr;
    }
    if (A.drawDC)  { DeleteDC(A.drawDC); A.drawDC = nullptr; }
    if (A.drawBmp) { DeleteObject(A.drawBmp); A.drawBmp = nullptr; }
    A.drawBits = nullptr;
    delete[] A.savedBits;
    A.savedBits = nullptr;
    std::vector<AppState::UndoEntry>().swap(A.history);
    HeapCompact(GetProcessHeap(), 0);
}

static void SyncSavedFromDraw(RECT r) {
    if (!A.drawBits || !A.savedBits) return;
    if (r.left < 0) r.left = 0;
    if (r.top  < 0) r.top  = 0;
    if (r.right  > A.sw) r.right  = A.sw;
    if (r.bottom > A.sh) r.bottom = A.sh;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = A.drawBits  + (size_t)(r.top + y) * A.sw + r.left;
        uint32_t*       dst = A.savedBits + (size_t)(r.top + y) * A.sw + r.left;
        std::memcpy(dst, src, (size_t)w * 4);
    }
}

static void PushHistory(RECT r) {
    if (!A.savedBits) return;
    if (r.left < 0) r.left = 0;
    if (r.top  < 0) r.top  = 0;
    if (r.right  > A.sw) r.right  = A.sw;
    if (r.bottom > A.sh) r.bottom = A.sh;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    AppState::UndoEntry e;
    e.rect = r;

    bool allSeed = true;
    for (int y = 0; y < h && allSeed; ++y) {
        const uint32_t* src = A.savedBits + (size_t)(r.top + y) * A.sw + r.left;
        for (int x = 0; x < w; ++x) {
            if (src[x] != 0x01000000u) { allSeed = false; break; }
        }
    }
    if (!allSeed) {
        e.pixels.resize((size_t)w * h);
        for (int y = 0; y < h; ++y) {
            const uint32_t* src = A.savedBits + (size_t)(r.top + y) * A.sw + r.left;
            std::memcpy(e.pixels.data() + (size_t)y * w, src, (size_t)w * 4);
        }
        e.pixels.shrink_to_fit();
    }
    A.history.push_back(std::move(e));

    auto bytesUsed = []() {
        size_t total = 0;
        for (const auto& entry : A.history)
            total += entry.pixels.capacity() * sizeof(uint32_t);
        return total;
    };
    while (A.history.size() > 1
           && (A.history.size() > AppState::kMaxHistory
               || bytesUsed() > kHistoryByteBudget)) {
        A.history.erase(A.history.begin());
    }
}

void ClearCanvas() {
    if (!A.drawBits) return;

    std::vector<AppState::UndoEntry>().swap(A.history);

    std::fill_n(A.drawBits, (size_t)A.sw * A.sh, 0x01000000u);
    if (A.savedBits)
        std::fill_n(A.savedBits, (size_t)A.sw * A.sh, 0x01000000u);

    RECT full{0, 0, A.sw, A.sh};
    UpdateOverlay(&full);

    HeapCompact(GetProcessHeap(), 0);
}

void Undo() {
    if (A.history.empty() || !A.drawBits) return;
    AppState::UndoEntry e = std::move(A.history.back());
    A.history.pop_back();
    int w = e.rect.right - e.rect.left;
    int h = e.rect.bottom - e.rect.top;
    if (e.pixels.empty()) {
        for (int y = 0; y < h; ++y) {
            uint32_t* draw  = A.drawBits  + (size_t)(e.rect.top + y) * A.sw + e.rect.left;
            uint32_t* saved = A.savedBits + (size_t)(e.rect.top + y) * A.sw + e.rect.left;
            std::fill_n(draw,  w, 0x01000000u);
            std::fill_n(saved, w, 0x01000000u);
        }
    } else {
        for (int y = 0; y < h; ++y) {
            const uint32_t* src = e.pixels.data() + (size_t)y * w;
            uint32_t* draw  = A.drawBits  + (size_t)(e.rect.top + y) * A.sw + e.rect.left;
            uint32_t* saved = A.savedBits + (size_t)(e.rect.top + y) * A.sw + e.rect.left;
            std::memcpy(draw,  src, (size_t)w * 4);
            std::memcpy(saved, src, (size_t)w * 4);
        }
    }
    UpdateOverlay(&e.rect);
}

void UpdateOverlay(const RECT* dirty) {
    if (!A.canvas || !A.drawDC) return;

    POINT dst{A.sx, A.sy};
    SIZE  sz{A.sw, A.sh};
    POINT src{0, 0};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    UPDATELAYEREDWINDOWINFO info{};
    info.cbSize   = sizeof(info);
    info.pptDst   = &dst;
    info.psize    = &sz;
    info.hdcSrc   = A.drawDC;
    info.pptSrc   = &src;
    info.pblend   = &bf;
    info.dwFlags  = ULW_ALPHA;
    info.prcDirty = dirty;

    UpdateLayeredWindowIndirect(A.canvas, &info);
}

static inline void BlendPremul(uint32_t* dst,
                               unsigned sR, unsigned sG, unsigned sB, unsigned sA,
                               unsigned cov_255) {
    unsigned eA = (sA * cov_255 + 127) / 255;
    if (eA == 0) return;
    unsigned eR = (sR * eA + 127) / 255;
    unsigned eG = (sG * eA + 127) / 255;
    unsigned eB = (sB * eA + 127) / 255;
    uint32_t d = *dst;
    unsigned dB = d & 0xFFu;
    unsigned dG = (d >> 8)  & 0xFFu;
    unsigned dR = (d >> 16) & 0xFFu;
    unsigned dA = (d >> 24) & 0xFFu;
    unsigned inv = 255u - eA;
    unsigned oR = eR + (dR * inv + 127) / 255;
    unsigned oG = eG + (dG * inv + 127) / 255;
    unsigned oB = eB + (dB * inv + 127) / 255;
    unsigned oA = eA + (dA * inv + 127) / 255;
    *dst = (oA << 24) | (oR << 16) | (oG << 8) | oB;
}

static inline unsigned DiscCoverage(int x, int y, float cx, float cy, float r) {
    float dx = (x + 0.5f) - cx;
    float dy = (y + 0.5f) - cy;
    float d2 = dx*dx + dy*dy;
    float edge2 = (r + 0.5f) * (r + 0.5f);
    if (d2 >= edge2) return 0;
    float d = sqrtf(d2);
    float cov = r - d + 0.5f;
    if (cov >= 1.0f) return 255;
    if (cov <= 0.0f) return 0;
    return (unsigned)(cov * 255.0f + 0.5f);
}

static void StampCircle(float cx, float cy, float r,
                        BYTE sR, BYTE sG, BYTE sB, BYTE sA) {
    if (!A.drawBits || r <= 0.0f) return;
    int x0 = max((int)floorf(cx - r - 1.0f), 0);
    int x1 = min((int)ceilf (cx + r + 1.0f), A.sw);
    int y0 = max((int)floorf(cy - r - 1.0f), 0);
    int y1 = min((int)ceilf (cy + r + 1.0f), A.sh);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = A.drawBits + (size_t)y * A.sw;
        for (int x = x0; x < x1; ++x) {
            unsigned cov = DiscCoverage(x, y, cx, cy, r);
            if (cov) BlendPremul(&row[x], sR, sG, sB, sA, cov);
        }
    }
}

static void StampHighlighter(float cx, float cy, float r,
                             BYTE sR, BYTE sG, BYTE sB, BYTE sA) {
    if (!A.drawBits || !A.savedBits || r <= 0.0f) return;
    int x0 = max((int)floorf(cx - r - 1.0f), 0);
    int x1 = min((int)ceilf (cx + r + 1.0f), A.sw);
    int y0 = max((int)floorf(cy - r - 1.0f), 0);
    int y1 = min((int)ceilf (cy + r + 1.0f), A.sh);
    for (int y = y0; y < y1; ++y) {
        const uint32_t* savedRow = A.savedBits + (size_t)y * A.sw;
        uint32_t*       drawRow  = A.drawBits  + (size_t)y * A.sw;
        for (int x = x0; x < x1; ++x) {
            unsigned cov = DiscCoverage(x, y, cx, cy, r);
            if (!cov) continue;
            uint32_t candidate = savedRow[x];
            BlendPremul(&candidate, sR, sG, sB, sA, cov);
            unsigned candA = (candidate >> 24) & 0xFFu;
            unsigned currA = (drawRow[x]  >> 24) & 0xFFu;
            if (candA > currA) drawRow[x] = candidate;
        }
    }
}

static void StampErase(float cx, float cy, float r) {
    if (!A.drawBits || r <= 0.0f) return;
    int x0 = max((int)floorf(cx - r - 1.0f), 0);
    int x1 = min((int)ceilf (cx + r + 1.0f), A.sw);
    int y0 = max((int)floorf(cy - r - 1.0f), 0);
    int y1 = min((int)ceilf (cy + r + 1.0f), A.sh);
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = A.drawBits + (size_t)y * A.sw;
        for (int x = x0; x < x1; ++x) {
            unsigned cov = DiscCoverage(x, y, cx, cy, r);
            if (!cov) continue;

            unsigned inv = 255u - cov;
            uint32_t p = row[x];
            unsigned pB = p & 0xFFu;
            unsigned pG = (p >> 8)  & 0xFFu;
            unsigned pR = (p >> 16) & 0xFFu;
            unsigned pA = (p >> 24) & 0xFFu;
            unsigned oR = (pR * inv + 127) / 255;
            unsigned oG = (pG * inv + 127) / 255;
            unsigned oB = (pB * inv + 127) / 255;
            unsigned oA = (pA * inv + 127) / 255;
            if (oA < 1u) { row[x] = 0x01000000u; }
            else         { row[x] = (oA << 24) | (oR << 16) | (oG << 8) | oB; }
        }
    }
}

void StartStroke(POINT screenPt) {
    A.drawing = true;
    A.lastPt = screenPt;
    A.strokeDirty = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    DrawSegment(screenPt, screenPt);
}

void EndStroke() {
    if (A.drawing
        && A.strokeDirty.right > A.strokeDirty.left
        && A.strokeDirty.bottom > A.strokeDirty.top) {
        PushHistory(A.strokeDirty);
        SyncSavedFromDraw(A.strokeDirty);
    }
    A.drawing = false;
}

void DrawSegment(POINT a, POINT b) {
    if (!A.drawBits) return;

    int ax = a.x - A.sx, ay = a.y - A.sy;
    int bx = b.x - A.sx, by = b.y - A.sy;

    float w = (float)currentThickness();
    BYTE  alpha = 255;
    bool  eraser = false;
    bool  highlighter = false;

    switch (A.tool) {
        case Tool::Pen:
            w = (float)currentThickness();
            alpha = 255;
            break;
        case Tool::Pencil:
            w = (float)currentThickness() * 0.65f;
            if (w < 1.0f) w = 1.0f;
            alpha = 215;
            break;
        case Tool::Highlighter:
            w = (float)currentThickness() * 3.2f;
            alpha = 128;
            highlighter = true;
            break;
        case Tool::Eraser:
            w = (float)currentThickness() * 2.6f;
            alpha = 0;
            eraser = true;
            break;
        default:
            return;
    }

    {
        const Swatch& s = kPalette[A.paletteIdx];
        float radius = w * 0.5f;
        float dx = (float)(bx - ax), dy = (float)(by - ay);
        float dist = sqrtf(dx*dx + dy*dy);
        int steps = max(1, (int)ceilf(dist));
        for (int i = 0; i <= steps; ++i) {
            float t = (steps == 0) ? 0.0f : (float)i / (float)steps;
            float cx = (float)ax + dx * t;
            float cy = (float)ay + dy * t;
            if (eraser) {
                StampErase(cx, cy, radius);
            } else if (highlighter) {
                StampHighlighter(cx, cy, radius, s.r, s.g, s.b, alpha);
            } else {
                StampCircle(cx, cy, radius, s.r, s.g, s.b, alpha);
            }
        }
    }

    int pad = (int)(w * 0.5f) + 3;
    RECT seg{
        min(ax, bx) - pad,
        min(ay, by) - pad,
        max(ax, bx) + pad,
        max(ay, by) + pad
    };
    seg.left   = max(seg.left,   (LONG)0);
    seg.top    = max(seg.top,    (LONG)0);
    seg.right  = min(seg.right,  (LONG)A.sw);
    seg.bottom = min(seg.bottom, (LONG)A.sh);

    if (seg.left   < A.strokeDirty.left  ) A.strokeDirty.left   = seg.left;
    if (seg.top    < A.strokeDirty.top   ) A.strokeDirty.top    = seg.top;
    if (seg.right  > A.strokeDirty.right ) A.strokeDirty.right  = seg.right;
    if (seg.bottom > A.strokeDirty.bottom) A.strokeDirty.bottom = seg.bottom;

    UpdateOverlay(&seg);
}

void ActivateOverlay() {
    if (A.active) return;
    CreateCanvasSurface();
    if (!A.drawBits) return;

    if (A.canvas) {
        DestroyWindow(A.canvas);
        A.canvas = nullptr;
    }
    A.canvas = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        L"ScreenDoodle_Canvas", L"ScreenDoodle Canvas",
        WS_POPUP,
        A.sx, A.sy, A.sw, A.sh,
        nullptr, nullptr, A.hInst, nullptr);
    if (!A.canvas) {
        DestroyCanvasSurface();
        return;
    }

    RECT full{0, 0, A.sw, A.sh};
    UpdateOverlay(&full);

    ShowWindow(A.canvas, SW_SHOWNA);
    SetWindowPos(A.canvas, HWND_TOPMOST, A.sx, A.sy, A.sw, A.sh,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    UpdateOverlay(&full);

    SetForegroundWindow(A.canvas);
    SetFocus(A.canvas);

    RepositionWidget();
    ShowWindow(A.widget, SW_SHOWNOACTIVATE);
    SetWindowPos(A.widget, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    A.active = true;
}

void DeactivateOverlay() {
    if (!A.active) return;
    ShowWindow(A.widget, SW_HIDE);
    if (A.canvas) {
        DestroyWindow(A.canvas);
        A.canvas = nullptr;
    }
    DestroyCanvasSurface();
    A.active = false;
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            POINT screenPoint;
            screenPoint.x = GET_X_LPARAM(lp) + A.sx;
            screenPoint.y = GET_Y_LPARAM(lp) + A.sy;
            StartStroke(screenPoint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!A.drawing) return 0;
            POINT screenPoint;
            screenPoint.x = GET_X_LPARAM(lp) + A.sx;
            screenPoint.y = GET_Y_LPARAM(lp) + A.sy;
            DrawSegment(A.lastPt, screenPoint);
            A.lastPt = screenPoint;
            return 0;
        }
        case WM_LBUTTONUP: {
            if (GetCapture() == hwnd) ReleaseCapture();
            EndStroke();
            if (A.widget && IsWindowVisible(A.widget)) {
                SetWindowPos(A.widget, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            A.tool = (A.tool == Tool::Eraser) ? Tool::Pen : Tool::Eraser;
            if (A.widget) InvalidateRect(A.widget, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) {
                DeactivateOverlay();
                return 0;
            }
            if (wp == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                Undo();
                return 0;
            }
            if (wp == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                ClearCanvas();
                return 0;
            }
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case WM_ACTIVATE:
            if (LOWORD(wp) != WA_INACTIVE
                && A.widget && IsWindowVisible(A.widget)) {
                SetWindowPos(A.widget, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
