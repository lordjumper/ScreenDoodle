#include "canvas.h"
#include "state.h"
#include "widget.h"

using Gdiplus::Bitmap;
using Gdiplus::BitmapData;
using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::FontFamily;
using Gdiplus::FontStyleBold;
using Gdiplus::Graphics;
using Gdiplus::ImageLockModeRead;
using Gdiplus::Ok;
using Gdiplus::PointF;
using Gdiplus::REAL;
using Gdiplus::RectF;
using Gdiplus::SmoothingModeAntiAlias;
using Gdiplus::SolidBrush;
using Gdiplus::StringFormat;
using Gdiplus::StringFormatFlagsMeasureTrailingSpaces;
using Gdiplus::StringFormatFlagsNoClip;
using Gdiplus::StringTrimmingNone;
using Gdiplus::TextRenderingHintAntiAlias;
using Gdiplus::UnitPixel;

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

    A.strokeCov = new (std::nothrow) uint8_t[(size_t)A.sw * A.sh];
    if (!A.strokeCov) {
        DestroyCanvasSurface();
        return;
    }

    std::fill_n(A.drawBits,  (size_t)A.sw * A.sh, 0x01000000u);
    std::fill_n(A.savedBits, (size_t)A.sw * A.sh, 0x01000000u);
    std::fill_n(A.strokeCov, (size_t)A.sw * A.sh, (uint8_t)0);
    A.history.clear();
    A.history.shrink_to_fit();
    std::vector<PlacedTextBox>().swap(A.textBoxes);
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
    delete[] A.strokeCov;
    A.strokeCov = nullptr;
    std::vector<AppState::UndoEntry>().swap(A.history);
    std::vector<PlacedTextBox>().swap(A.textBoxes);
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

static void RestoreSavedRegion(RECT r) {
    if (!A.drawBits || !A.savedBits) return;
    if (r.left < 0) r.left = 0;
    if (r.top  < 0) r.top  = 0;
    if (r.right  > A.sw) r.right  = A.sw;
    if (r.bottom > A.sh) r.bottom = A.sh;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = A.savedBits + (size_t)(r.top + y) * A.sw + r.left;
        uint32_t*       dst = A.drawBits  + (size_t)(r.top + y) * A.sw + r.left;
        std::memcpy(dst, src, (size_t)w * 4);
    }
}

static inline bool RectEmpty(const RECT& r) {
    return r.right <= r.left || r.bottom <= r.top;
}

static inline RECT UnionRect(const RECT& a, const RECT& b) {
    if (RectEmpty(a)) return b;
    if (RectEmpty(b)) return a;
    RECT u;
    u.left   = min(a.left,   b.left);
    u.top    = min(a.top,    b.top);
    u.right  = max(a.right,  b.right);
    u.bottom = max(a.bottom, b.bottom);
    return u;
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
    if (A.text.active) CancelTextEdit();
    if (!A.drawBits) return;

    std::vector<AppState::UndoEntry>().swap(A.history);
    std::vector<PlacedTextBox>().swap(A.textBoxes);

    std::fill_n(A.drawBits, (size_t)A.sw * A.sh, 0x01000000u);
    if (A.savedBits)
        std::fill_n(A.savedBits, (size_t)A.sw * A.sh, 0x01000000u);
    if (A.strokeCov)
        std::fill_n(A.strokeCov, (size_t)A.sw * A.sh, (uint8_t)0);

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

static void StampCircleAliased(float cx, float cy, float r,
                               BYTE sR, BYTE sG, BYTE sB, BYTE sA) {
    if (!A.drawBits || r <= 0.0f) return;
    int x0 = max((int)floorf(cx - r), 0);
    int x1 = min((int)ceilf (cx + r), A.sw);
    int y0 = max((int)floorf(cy - r), 0);
    int y1 = min((int)ceilf (cy + r), A.sh);
    float r2 = r * r;
    for (int y = y0; y < y1; ++y) {
        float dy = (float)y + 0.5f - cy;
        uint32_t* row = A.drawBits + (size_t)y * A.sw;
        for (int x = x0; x < x1; ++x) {
            float dx = (float)x + 0.5f - cx;
            if (dx*dx + dy*dy <= r2)
                BlendPremul(&row[x], sR, sG, sB, sA, 255);
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

static void StampCapsule(float ax, float ay, float bx, float by, float r,
                         BYTE sR, BYTE sG, BYTE sB) {
    if (!A.drawBits || !A.savedBits || !A.strokeCov || r <= 0.0f) return;

    float lo_x = (ax < bx ? ax : bx) - r - 1.0f;
    float hi_x = (ax > bx ? ax : bx) + r + 1.0f;
    float lo_y = (ay < by ? ay : by) - r - 1.0f;
    float hi_y = (ay > by ? ay : by) + r + 1.0f;
    int x0 = max((int)floorf(lo_x), 0);
    int x1 = min((int)ceilf (hi_x), A.sw);
    int y0 = max((int)floorf(lo_y), 0);
    int y1 = min((int)ceilf (hi_y), A.sh);
    if (x0 >= x1 || y0 >= y1) return;

    float seg_dx = bx - ax;
    float seg_dy = by - ay;
    float seg_len2 = seg_dx * seg_dx + seg_dy * seg_dy;
    bool isPoint = (seg_len2 < 1e-6f);
    float inv_len2 = isPoint ? 0.0f : 1.0f / seg_len2;
    float edge   = r + 0.5f;
    float edge2  = edge * edge;

    for (int y = y0; y < y1; ++y) {
        float py = (float)y + 0.5f - ay;
        size_t rowBase = (size_t)y * A.sw;
        for (int x = x0; x < x1; ++x) {
            float px = (float)x + 0.5f - ax;
            float qx, qy;
            if (isPoint) {
                qx = px; qy = py;
            } else {
                float t = (px * seg_dx + py * seg_dy) * inv_len2;
                if (t < 0.0f) t = 0.0f;
                else if (t > 1.0f) t = 1.0f;
                qx = px - t * seg_dx;
                qy = py - t * seg_dy;
            }
            float d2 = qx * qx + qy * qy;
            if (d2 >= edge2) continue;
            float d = sqrtf(d2);
            float covF = r - d + 0.5f;
            unsigned cov;
            if (covF >= 1.0f)      cov = 255;
            else if (covF <= 0.0f) continue;
            else                   cov = (unsigned)(covF * 255.0f + 0.5f);

            uint8_t* covSlot = &A.strokeCov[rowBase + x];
            if (cov <= *covSlot) continue;
            *covSlot = (uint8_t)cov;

            uint32_t* dst = &A.drawBits[rowBase + x];
            *dst = A.savedBits[rowBase + x];
            BlendPremul(dst, sR, sG, sB, 255, cov);
        }
    }
}

static void ClearStrokeCoverageRect(RECT r) {
    if (!A.strokeCov) return;
    if (r.left < 0) r.left = 0;
    if (r.top  < 0) r.top  = 0;
    if (r.right  > A.sw) r.right  = A.sw;
    if (r.bottom > A.sh) r.bottom = A.sh;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; ++y) {
        std::fill_n(A.strokeCov + (size_t)(r.top + y) * A.sw + r.left,
                    w, (uint8_t)0);
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
        ClearStrokeCoverageRect(A.strokeDirty);
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
        if (A.tool == Tool::Pen) {
            StampCapsule((float)ax, (float)ay, (float)bx, (float)by,
                         radius, s.r, s.g, s.b);
        } else {
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
                    StampCircleAliased(cx, cy, radius, s.r, s.g, s.b, alpha);
                }
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

static void BuildTextFormat(StringFormat& sf) {
    sf.SetFormatFlags(StringFormatFlagsMeasureTrailingSpaces
                      | StringFormatFlagsNoClip);
    sf.SetTrimming(StringTrimmingNone);
}

static int CountLines(const std::wstring& text) {
    int n = 1;
    for (wchar_t c : text) if (c == L'\n') ++n;
    return n;
}

static void MeasureText(const std::wstring& text, float fontSize,
                        int& outW, int& outH) {
    FontFamily fam(L"Segoe UI");
    Font font(&fam, fontSize, FontStyleBold, UnitPixel);
    Bitmap probe(2, 2, PixelFormat32bppARGB);
    Graphics g(&probe);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    StringFormat sf;
    BuildTextFormat(sf);

    RectF baseBox;
    g.MeasureString(L"M", 1, &font, PointF(0.0f, 0.0f), &sf, &baseBox);
    int lineH = (int)ceilf(baseBox.Height);
    if (lineH < 1) lineH = (int)ceilf(fontSize);
    int lc = CountLines(text);

    if (text.empty()) {
        outW = 0;
        outH = lineH;
        return;
    }

    RectF layout(0.0f, 0.0f, 1e7f, 1e7f);
    RectF box;
    INT cpFit = 0, linesFilled = 0;
    g.MeasureString(text.c_str(), (INT)text.size(), &font,
                    layout, &sf, &box, &cpFit, &linesFilled);

    outW = (int)ceilf(box.Width);
    int multiH = (int)ceilf(box.Height);
    int expected = lc * lineH;
    outH = multiH > expected ? multiH : expected;
}

static int LineHeightOf(float fontSize) {
    int w = 0, h = 0;
    MeasureText(std::wstring(), fontSize, w, h);
    return h;
}

static void StampText(const std::wstring& text, int destX, int destY,
                      float fontSize, BYTE r, BYTE g, BYTE b,
                      RECT& outRect) {
    outRect = {0, 0, 0, 0};
    if (text.empty() || !A.drawBits) return;

    int tw = 0, th = 0;
    MeasureText(text, fontSize, tw, th);
    if (tw <= 0 || th <= 0) return;

    int pad = (int)ceilf(fontSize * 0.25f);
    int bmpW = tw + pad * 2;
    int bmpH = th + pad * 2;

    Bitmap bmp(bmpW, bmpH, PixelFormat32bppARGB);
    {
        Graphics gg(&bmp);
        gg.Clear(Color(0, 0, 0, 0));
        gg.SetSmoothingMode(SmoothingModeAntiAlias);
        gg.SetTextRenderingHint(TextRenderingHintAntiAlias);
        FontFamily fam(L"Segoe UI");
        Font font(&fam, fontSize, FontStyleBold, UnitPixel);
        SolidBrush brush(Color(255, r, g, b));
        StringFormat sf;
        BuildTextFormat(sf);
        RectF layout((REAL)pad, (REAL)pad,
                     (REAL)(bmpW - pad), (REAL)(bmpH - pad));
        gg.DrawString(text.c_str(), (INT)text.size(), &font,
                      layout, &sf, &brush);
    }

    Gdiplus::Rect lockArea(0, 0, bmpW, bmpH);
    BitmapData bd;
    if (bmp.LockBits(&lockArea, ImageLockModeRead,
                     PixelFormat32bppARGB, &bd) != Ok)
        return;

    int dstX = destX - pad;
    int dstY = destY - pad;
    int x0 = max(dstX, 0);
    int y0 = max(dstY, 0);
    int x1 = min(dstX + bmpW, A.sw);
    int y1 = min(dstY + bmpH, A.sh);

    if (x0 < x1 && y0 < y1) {
        for (int y = y0; y < y1; ++y) {
            const uint32_t* srcRow = (const uint32_t*)
                ((const BYTE*)bd.Scan0 + (size_t)(y - dstY) * bd.Stride);
            uint32_t* dstRow = A.drawBits + (size_t)y * A.sw;
            for (int x = x0; x < x1; ++x) {
                uint32_t s = srcRow[x - dstX];
                unsigned cov = (s >> 24) & 0xFFu;
                if (cov) BlendPremul(&dstRow[x], r, g, b, 255, cov);
            }
        }
        outRect.left   = x0;
        outRect.top    = y0;
        outRect.right  = x1;
        outRect.bottom = y1;
    }

    bmp.UnlockBits(&bd);
}

static void StampFrame(RECT r, RECT& outRect) {
    outRect = {0, 0, 0, 0};
    if (!A.drawBits) return;
    int xMin = max(r.left,       0);
    int yMin = max(r.top,        0);
    int xMax = min(r.right  - 1, A.sw - 1);
    int yMax = min(r.bottom - 1, A.sh - 1);
    if (xMin > xMax || yMin > yMax) return;

    constexpr unsigned alphaEdge = 140;
    constexpr unsigned alphaCorner = 90;
    constexpr BYTE fr = 220, fg = 225, fb = 235;

    auto put = [&](int x, int y, unsigned a) {
        BlendPremul(&A.drawBits[(size_t)y * A.sw + x], fr, fg, fb, a, 255);
    };

    if (r.top >= 0 && r.top < A.sh)
        for (int x = xMin; x <= xMax; ++x) put(x, r.top, alphaEdge);
    if (r.bottom - 1 >= 0 && r.bottom - 1 < A.sh)
        for (int x = xMin; x <= xMax; ++x) put(x, r.bottom - 1, alphaEdge);
    if (r.left >= 0 && r.left < A.sw)
        for (int y = yMin; y <= yMax; ++y) put(r.left, y, alphaEdge);
    if (r.right - 1 >= 0 && r.right - 1 < A.sw)
        for (int y = yMin; y <= yMax; ++y) put(r.right - 1, y, alphaEdge);

    if (r.left >= 0 && r.top >= 0 && r.left < A.sw && r.top < A.sh)
        put(r.left, r.top, alphaCorner);
    if (r.right - 1 >= 0 && r.top >= 0 && r.right - 1 < A.sw && r.top < A.sh)
        put(r.right - 1, r.top, alphaCorner);
    if (r.left >= 0 && r.bottom - 1 >= 0 && r.left < A.sw && r.bottom - 1 < A.sh)
        put(r.left, r.bottom - 1, alphaCorner);
    if (r.right - 1 >= 0 && r.bottom - 1 >= 0
        && r.right - 1 < A.sw && r.bottom - 1 < A.sh)
        put(r.right - 1, r.bottom - 1, alphaCorner);

    outRect.left   = max(r.left,   0);
    outRect.top    = max(r.top,    0);
    outRect.right  = min(r.right,  A.sw);
    outRect.bottom = min(r.bottom, A.sh);
}

static void StampCaret(int x, int y, int height, RECT& outRect) {
    outRect = {0, 0, 0, 0};
    if (!A.drawBits || height <= 0) return;
    int barW = max(1, (int)((float)currentTextSize() / 18.0f));
    int x0 = max(x, 0);
    int y0 = max(y, 0);
    int x1 = min(x + barW, A.sw);
    int y1 = min(y + height, A.sh);
    if (x0 >= x1 || y0 >= y1) return;
    const Swatch& s = kPalette[A.paletteIdx];
    uint32_t pix = (255u << 24) | ((uint32_t)s.r << 16)
                 | ((uint32_t)s.g << 8) | (uint32_t)s.b;
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* row = A.drawBits + (size_t)yy * A.sw;
        for (int xx = x0; xx < x1; ++xx) row[xx] = pix;
    }
    outRect.left = x0; outRect.top = y0;
    outRect.right = x1; outRect.bottom = y1;
}

static void RedrawTextEdit() {
    if (!A.text.active || !A.drawBits || !A.savedBits) return;

    RECT restoreRect = A.text.lastRect;
    if (!RectEmpty(restoreRect)) RestoreSavedRegion(restoreRect);

    int oxLocal = A.text.origin.x - A.sx;
    int oyLocal = A.text.origin.y - A.sy;
    float fontSize = (float)currentTextSize();
    const Swatch& s = kPalette[A.paletteIdx];

    int textW = 0, textH = 0;
    MeasureText(A.text.buffer, fontSize, textW, textH);

    int lineH     = LineHeightOf(fontSize);
    int lineCount = CountLines(A.text.buffer);

    size_t lastBreak = A.text.buffer.find_last_of(L'\n');
    int lastLineW = 0, lastLineH = 0;
    if (lastBreak == std::wstring::npos) {
        MeasureText(A.text.buffer, fontSize, lastLineW, lastLineH);
    } else {
        std::wstring tail = A.text.buffer.substr(lastBreak + 1);
        MeasureText(tail, fontSize, lastLineW, lastLineH);
    }

    RECT textRect{0, 0, 0, 0};
    if (!A.text.buffer.empty()) {
        StampText(A.text.buffer, oxLocal, oyLocal, fontSize,
                  s.r, s.g, s.b, textRect);
    }

    int caretGap = max(1, (int)(fontSize * 0.05f));
    int caretX   = oxLocal + lastLineW + caretGap;
    int caretY   = oyLocal + (lineCount - 1) * lineH;
    int caretW   = max(1, (int)(fontSize / 18.0f));

    RECT caretRect{0, 0, 0, 0};
    if (A.text.caretVisible) {
        StampCaret(caretX, caretY, lineH, caretRect);
    }

    int framePad = max(4, (int)(fontSize * 0.18f));
    int contentRight  = max(oxLocal + textW, caretX + caretW);
    int contentBottom = oyLocal + max(textH, lineCount * lineH);
    RECT frameTarget{
        oxLocal - framePad,
        oyLocal - framePad,
        contentRight + framePad,
        contentBottom + framePad
    };
    RECT frameRect{0, 0, 0, 0};
    StampFrame(frameTarget, frameRect);

    RECT combined = UnionRect(UnionRect(textRect, caretRect), frameRect);
    A.text.lastRect = combined;

    RECT dirty = UnionRect(restoreRect, combined);
    if (!RectEmpty(dirty)) UpdateOverlay(&dirty);
}

void BeginTextEdit(POINT screenPt, const std::wstring* initial) {
    if (A.text.active) CommitTextEdit();
    if (!A.canvas || !A.drawBits) return;
    A.text.active       = true;
    A.text.dragging     = false;
    A.text.origin       = screenPt;
    if (initial) A.text.buffer = *initial;
    else         A.text.buffer.clear();
    A.text.lastRect     = {0, 0, 0, 0};
    A.text.caretVisible = true;
    if (A.text.caretTimer) KillTimer(A.canvas, A.text.caretTimer);
    A.text.caretTimer   = SetTimer(A.canvas, 1, 500, nullptr);
    RedrawTextEdit();
}

static void EnforceTextBoxBudget(size_t incomingBytes) {
    while (A.textBoxes.size() >= AppState::kMaxTextBoxes
           && !A.textBoxes.empty()) {
        A.textBoxes.erase(A.textBoxes.begin());
    }
    size_t total = incomingBytes;
    for (const auto& b : A.textBoxes)
        total += b.backdrop.capacity() * sizeof(uint32_t);
    while (total > kTextBoxByteBudget && !A.textBoxes.empty()) {
        total -= A.textBoxes.front().backdrop.capacity() * sizeof(uint32_t);
        A.textBoxes.erase(A.textBoxes.begin());
    }
}

void CommitTextEdit() {
    if (!A.text.active) return;
    if (A.text.caretTimer) {
        KillTimer(A.canvas, A.text.caretTimer);
        A.text.caretTimer = 0;
    }

    RECT restoreRect = A.text.lastRect;
    if (!RectEmpty(restoreRect)) RestoreSavedRegion(restoreRect);

    RECT textRect{0, 0, 0, 0};
    if (!A.text.buffer.empty()) {
        int oxLocal = A.text.origin.x - A.sx;
        int oyLocal = A.text.origin.y - A.sy;
        float fontSize = (float)currentTextSize();
        const Swatch& s = kPalette[A.paletteIdx];
        StampText(A.text.buffer, oxLocal, oyLocal, fontSize,
                  s.r, s.g, s.b, textRect);
        if (!RectEmpty(textRect)) {
            int bw = textRect.right  - textRect.left;
            int bh = textRect.bottom - textRect.top;

            PlacedTextBox box;
            box.text     = A.text.buffer;
            box.origin   = A.text.origin;
            box.sizeIdx  = A.thicknessIdx;
            box.colorIdx = A.paletteIdx;
            box.bounds   = textRect;
            box.backdropRect = textRect;
            box.backdrop.resize((size_t)bw * bh);
            for (int y = 0; y < bh; ++y) {
                std::memcpy(&box.backdrop[(size_t)y * bw],
                            &A.savedBits[(size_t)(textRect.top + y) * A.sw
                                         + textRect.left],
                            (size_t)bw * 4);
            }
            box.backdrop.shrink_to_fit();

            PushHistory(textRect);
            SyncSavedFromDraw(textRect);

            EnforceTextBoxBudget(box.backdrop.capacity() * sizeof(uint32_t));
            A.textBoxes.push_back(std::move(box));
        }
    }

    RECT dirty = UnionRect(restoreRect, textRect);
    A.text.active = false;
    A.text.dragging = false;
    A.text.buffer.clear();
    A.text.lastRect = {0, 0, 0, 0};
    if (!RectEmpty(dirty)) UpdateOverlay(&dirty);
}

void CancelTextEdit() {
    if (!A.text.active) return;
    if (A.text.caretTimer) {
        KillTimer(A.canvas, A.text.caretTimer);
        A.text.caretTimer = 0;
    }
    RECT restoreRect = A.text.lastRect;
    if (!RectEmpty(restoreRect)) {
        RestoreSavedRegion(restoreRect);
        UpdateOverlay(&restoreRect);
    }
    A.text.active = false;
    A.text.dragging = false;
    A.text.buffer.clear();
    A.text.lastRect = {0, 0, 0, 0};
}

void RedrawTextEditIfActive() {
    if (A.text.active) {
        A.text.caretVisible = true;
        RedrawTextEdit();
    }
}

static void RestoreBoxBackdrop(const PlacedTextBox& box) {
    if (!A.drawBits || !A.savedBits || box.backdrop.empty()) return;
    int x0 = box.backdropRect.left;
    int y0 = box.backdropRect.top;
    int x1 = box.backdropRect.right;
    int y1 = box.backdropRect.bottom;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > A.sw) x1 = A.sw;
    if (y1 > A.sh) y1 = A.sh;
    int w = x1 - x0;
    int h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    int stride = box.backdropRect.right - box.backdropRect.left;
    int srcOffX = x0 - box.backdropRect.left;
    int srcOffY = y0 - box.backdropRect.top;
    for (int y = 0; y < h; ++y) {
        const uint32_t* src = &box.backdrop[(size_t)(srcOffY + y) * stride + srcOffX];
        uint32_t* dDraw  = &A.drawBits [(size_t)(y0 + y) * A.sw + x0];
        uint32_t* dSaved = &A.savedBits[(size_t)(y0 + y) * A.sw + x0];
        std::memcpy(dDraw,  src, (size_t)w * 4);
        std::memcpy(dSaved, src, (size_t)w * 4);
    }
}

bool HitTestTextBox(POINT localPt) {
    for (size_t i = A.textBoxes.size(); i-- > 0; ) {
        if (PtInRect(&A.textBoxes[i].bounds, localPt)) return true;
    }
    return false;
}

bool TryPickUpTextBox(POINT localPt) {
    for (size_t i = A.textBoxes.size(); i-- > 0; ) {
        if (!PtInRect(&A.textBoxes[i].bounds, localPt)) continue;
        PlacedTextBox box = std::move(A.textBoxes[i]);
        A.textBoxes.erase(A.textBoxes.begin() + (ptrdiff_t)i);

        if (A.text.active) CommitTextEdit();

        RestoreBoxBackdrop(box);
        UpdateOverlay(&box.backdropRect);

        A.thicknessIdx = box.sizeIdx;
        A.paletteIdx   = box.colorIdx;
        if (A.widget) InvalidateRect(A.widget, nullptr, FALSE);

        BeginTextEdit(box.origin, &box.text);
        return true;
    }
    return false;
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
    if (A.text.active) CommitTextEdit();
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
            POINT screenPoint;
            screenPoint.x = GET_X_LPARAM(lp) + A.sx;
            screenPoint.y = GET_Y_LPARAM(lp) + A.sy;
            if (A.tool == Tool::Text) {
                SetFocus(hwnd);
                POINT localPt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (A.text.active && PtInRect(&A.text.lastRect, localPt)) {
                    A.text.dragging       = true;
                    A.text.dragOffset.x   = screenPoint.x - A.text.origin.x;
                    A.text.dragOffset.y   = screenPoint.y - A.text.origin.y;
                    A.text.caretVisible   = false;
                    SetCapture(hwnd);
                    RedrawTextEditIfActive();
                    return 0;
                }
                if (TryPickUpTextBox(localPt)) {
                    A.text.dragging     = true;
                    A.text.dragOffset.x = screenPoint.x - A.text.origin.x;
                    A.text.dragOffset.y = screenPoint.y - A.text.origin.y;
                    A.text.caretVisible = false;
                    SetCapture(hwnd);
                    RedrawTextEditIfActive();
                    return 0;
                }
                BeginTextEdit(screenPoint);
                return 0;
            }
            SetCapture(hwnd);
            StartStroke(screenPoint);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (A.text.dragging) {
                POINT screenPoint;
                screenPoint.x = GET_X_LPARAM(lp) + A.sx;
                screenPoint.y = GET_Y_LPARAM(lp) + A.sy;
                A.text.origin.x = screenPoint.x - A.text.dragOffset.x;
                A.text.origin.y = screenPoint.y - A.text.dragOffset.y;
                RedrawTextEditIfActive();
                return 0;
            }
            if (!A.drawing) return 0;
            POINT screenPoint;
            screenPoint.x = GET_X_LPARAM(lp) + A.sx;
            screenPoint.y = GET_Y_LPARAM(lp) + A.sy;
            DrawSegment(A.lastPt, screenPoint);
            A.lastPt = screenPoint;
            return 0;
        }
        case WM_LBUTTONUP: {
            if (A.text.dragging) {
                A.text.dragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                A.text.caretVisible = true;
                RedrawTextEditIfActive();
                return 0;
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            EndStroke();
            if (A.widget && IsWindowVisible(A.widget)) {
                SetWindowPos(A.widget, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED: {
            if (A.text.dragging) {
                A.text.dragging = false;
                A.text.caretVisible = true;
                RedrawTextEditIfActive();
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            if (A.text.active) CommitTextEdit();
            A.tool = (A.tool == Tool::Eraser) ? Tool::Pen : Tool::Eraser;
            if (A.widget) InvalidateRect(A.widget, nullptr, FALSE);
            return 0;
        }
        case WM_CHAR: {
            if (!A.text.active) return 0;
            wchar_t ch = (wchar_t)wp;
            if (ch == 0x08) {
                if (!A.text.buffer.empty()) {
                    A.text.buffer.pop_back();
                    A.text.caretVisible = true;
                    RedrawTextEditIfActive();
                }
            } else if (ch == 0x09) {
                A.text.buffer.append(L"    ");
                A.text.caretVisible = true;
                RedrawTextEditIfActive();
            } else if (ch >= 0x20 && ch != 0x7F) {
                A.text.buffer.push_back(ch);
                A.text.caretVisible = true;
                RedrawTextEditIfActive();
            }
            return 0;
        }
        case WM_TIMER: {
            if (wp == 1 && A.text.active) {
                A.text.caretVisible = !A.text.caretVisible;
                RedrawTextEdit();
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) {
                if (A.text.active) { CommitTextEdit(); return 0; }
                DeactivateOverlay();
                return 0;
            }
            if (A.text.active) {
                if (wp == VK_RETURN) {
                    A.text.buffer.push_back(L'\n');
                    A.text.caretVisible = true;
                    RedrawTextEditIfActive();
                    return 0;
                }
                if (wp == VK_DELETE) {
                    CancelTextEdit();
                    return 0;
                }
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
        case WM_SETCURSOR: {
            if (LOWORD(lp) == HTCLIENT && A.tool == Tool::Text) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                if (A.text.dragging
                    || (A.text.active && PtInRect(&A.text.lastRect, pt))
                    || HitTestTextBox(pt)) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                }
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
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
