#include "widget.h"
#include "state.h"
#include "canvas.h"

using namespace Gdiplus;

static inline Color curCol(BYTE alpha = 255) {
    return Color(alpha, A.color.r, A.color.g, A.color.b);
}

namespace {
constexpr float WL_W          = 58.0f;  // strip width
constexpr float WL_DRAG_H     = 12.0f;  // drag handle height
constexpr float WL_PADX       = 7.0f;   // horizontal inner padding
constexpr float WL_TOOL       = 40.0f;  // tool button size
constexpr float WL_TOOL_GAP   = 5.0f;   // gap between tool buttons
constexpr float WL_SECTION    = 9.0f;   // gap between sections
constexpr float WL_THICK_H    = 26.0f;  // thickness row height
constexpr float WL_WHEEL      = 44.0f;  // colour wheel diameter
constexpr float WL_BRIGHT_GAP = 5.0f;   // gap between wheel and brightness bar
constexpr float WL_BRIGHT_H   = 11.0f;  // brightness bar height
constexpr float WL_ACT_GAP    = 6.0f;   // gap between the two action buttons
constexpr float WL_BOTTOM     = 8.0f;   // bottom padding
constexpr float WL_CORNER     = 9.0f;   // tool button corner radius
constexpr float WL_WIN_R      = 16.0f;  // window corner radius
constexpr float WL_MARGIN     = 18.0f;  // distance from screen edge

constexpr float WL_THICK_R[kThicknessCount] = {1.5f, 2.5f, 3.7f, 5.0f, 6.5f};

inline int R(float v) { return (int)(v + 0.5f); }
}

// Per-monitor DPI scale for the widget window.
static float WidgetScale() {
    static auto pGetDpiForWindow =
        (UINT(WINAPI*)(HWND))GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");

    UINT dpi = 0;
    if (pGetDpiForWindow && A.widget) dpi = pGetDpiForWindow(A.widget);
    if (dpi == 0) {
        HDC dc = GetDC(nullptr);
        dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
    }
    if (dpi == 0) dpi = 96;
    return (float)dpi / 96.0f;
}

struct WidgetLayout {
    float scale = 1.0f;
    int   W = 0;
    int   padX = 0;
    int   corner = 0;

    RECT  drag{};
    int   toolSize = 0;
    int   toolCount = 0;
    RECT  tools[kToolCount]{};
    Tool  slot[kToolCount]{};
    bool  slotGroup[kToolCount]{};

    float thickRadii[kThicknessCount]   = {};
    float thickCenterX[kThicknessCount] = {};
    RECT  thicks[kThicknessCount]{};

    RECT  wheel{};
    RECT  bright{};

    RECT  clearBtn{};
    RECT  closeBtn{};
    int   total = 0;
};

static WidgetLayout ComputeLayout() {
    WidgetLayout L;
    const float s = WidgetScale();
    L.scale = s;

    const int W        = R(WL_W * s);
    const int padX     = R(WL_PADX * s);
    const int toolGap  = R(WL_TOOL_GAP * s);
    const int toolSize = R(WL_TOOL * s);
    const int section  = R(WL_SECTION * s);
    L.W      = W;
    L.padX   = padX;
    L.corner = R(WL_CORNER * s);
    L.toolSize = toolSize;

    if (A.toolGroupOpen) {
        L.toolCount = 5;
        L.slot[0] = Tool::Pen;
        L.slot[1] = Tool::Pencil;
        L.slot[2] = Tool::Highlighter;
        L.slot[3] = Tool::Eraser;
        L.slot[4] = Tool::Text;
    } else {
        L.toolCount = 3;
        L.slot[0] = A.drawTool;  L.slotGroup[0] = true;
        L.slot[1] = Tool::Eraser;
        L.slot[2] = Tool::Text;
    }

    int y = 0;

    const int dragH = R(WL_DRAG_H * s);
    L.drag = {0, y, W, y + dragH};
    y += dragH;

    const int tx = (W - toolSize) / 2;
    for (int i = 0; i < L.toolCount; ++i) {
        L.tools[i] = {tx, y, tx + toolSize, y + toolSize};
        y += toolSize + toolGap;
    }
    y -= toolGap;
    y += section;

    // Thickness preview dots
    float radS[kThicknessCount];
    float sumDia = 0.0f;
    for (int i = 0; i < kThicknessCount; ++i) {
        radS[i] = WL_THICK_R[i] * s;
        sumDia += radS[i] * 2.0f;
    }
    float availTh = (float)(W - 2 * padX);
    float gapTh   = (availTh - sumDia) / (float)(kThicknessCount - 1);
    if (gapTh < 0.0f) gapTh = 0.0f;
    float fx = (float)padX;
    for (int i = 0; i < kThicknessCount; ++i) {
        L.thickRadii[i]   = radS[i];
        L.thickCenterX[i] = fx + radS[i];
        fx = L.thickCenterX[i] + radS[i] + gapTh;
    }
    const int thickH = R(WL_THICK_H * s);
    for (int i = 0; i < kThicknessCount; ++i) {
        int hitLeft  = (i == 0)
                       ? padX
                       : (int)((L.thickCenterX[i - 1] + L.thickCenterX[i]) * 0.5f);
        int hitRight = (i == kThicknessCount - 1)
                       ? (W - padX)
                       : (int)((L.thickCenterX[i] + L.thickCenterX[i + 1]) * 0.5f);
        L.thicks[i] = {hitLeft, y, hitRight, y + thickH};
    }
    y += thickH;
    y += section;

    const int wheelD = min(R(WL_WHEEL * s), W - 2 * padX);
    const int wheelX = (W - wheelD) / 2;
    L.wheel = {wheelX, y, wheelX + wheelD, y + wheelD};
    y += wheelD + R(WL_BRIGHT_GAP * s);
    const int brightH = R(WL_BRIGHT_H * s);
    L.bright = {padX, y, W - padX, y + brightH};
    y += brightH;
    y += section;

    const int actGap = R(WL_ACT_GAP * s);
    const int bw     = (W - 2 * padX - actGap) / 2;
    L.clearBtn = {padX, y, padX + bw, y + bw};
    L.closeBtn = {W - padX - bw, y, W - padX, y + bw};
    y += bw;
    y += R(WL_BOTTOM * s);

    L.total = y;
    return L;
}

void RepositionWidget() {
    WidgetLayout L = ComputeLayout();
    A.wgW = L.W;
    A.wgH = L.total;

    HMONITOR mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(mon, &mi);
    int margin = R(WL_MARGIN * L.scale);
    int x, y;
    if (A.anchor == SideAnchor::Right) {
        x = mi.rcWork.right - A.wgW - margin;
    } else {
        x = mi.rcWork.left + margin;
    }
    y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - A.wgH) / 2;

    SetWindowPos(A.widget, HWND_TOPMOST, x, y, A.wgW, A.wgH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    int d = R(WL_WIN_R * L.scale) * 2;
    HRGN rgn = CreateRoundRectRgn(0, 0, A.wgW + 1, A.wgH + 1, d, d);
    SetWindowRgn(A.widget, rgn, TRUE);
}

static void FillRoundRect(Graphics& g, const Brush& brush, RectF rect, float r) {
    GraphicsPath path;
    path.AddArc(rect.X,                  rect.Y,                  r * 2, r * 2, 180, 90);
    path.AddArc(rect.X + rect.Width - r*2, rect.Y,                r * 2, r * 2, 270, 90);
    path.AddArc(rect.X + rect.Width - r*2, rect.Y + rect.Height - r*2, r * 2, r * 2,   0, 90);
    path.AddArc(rect.X,                  rect.Y + rect.Height - r*2, r * 2, r * 2,  90, 90);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

static void RoundRectPath(GraphicsPath& path, RectF r, float radius) {
    float d = radius * 2.0f;
    path.Reset();
    path.AddArc(r.X,                r.Y,                d, d, 180, 90);
    path.AddArc(r.X + r.Width - d,  r.Y,                d, d, 270, 90);
    path.AddArc(r.X + r.Width - d,  r.Y + r.Height - d, d, d,   0, 90);
    path.AddArc(r.X,                r.Y + r.Height - d, d, d,  90, 90);
    path.CloseFigure();
}

static void DrawPenIcon(Graphics& g, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.RotateTransform(-40);

    GraphicsPath barrel;
    RectF barrelRect(-14.0f, -3.5f, 20.0f, 7.0f);
    RoundRectPath(barrel, barrelRect, 2.0f);
    Color barrelTop    = selected ? Color(255,  70,  74,  86) : Color(255,  55,  58,  68);
    Color barrelBottom = selected ? Color(255, 110, 114, 130) : Color(255,  90,  94, 108);
    LinearGradientBrush barrelBrush(barrelRect, barrelTop, barrelBottom,
                                    LinearGradientModeVertical);
    g.FillPath(&barrelBrush, &barrel);

    SolidBrush capEnd(Color(255, 40, 42, 50));
    g.FillRectangle(&capEnd, -16.0f, -3.5f, 2.0f, 7.0f);

    LinearGradientBrush band(RectF(2.0f, -3.5f, 4.0f, 7.0f),
                             Color(255, 220, 222, 228),
                             Color(255, 160, 164, 172),
                             LinearGradientModeVertical);
    g.FillRectangle(&band, 2.0f, -3.5f, 4.0f, 7.0f);

    PointF nib[3] = { { 6.0f, -3.5f }, { 6.0f, 3.5f }, {14.0f, 0.0f } };
    SolidBrush nibBrush(accent);
    g.FillPolygon(&nibBrush, nib, 3);

    SolidBrush ink(Color(255, 20, 22, 28));
    g.FillEllipse(&ink, 12.5f, -1.0f, 2.0f, 2.0f);

    g.Restore(st);
}

static void DrawPencilIcon(Graphics& g, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.RotateTransform(-40);

    RectF body(-13.0f, -3.5f, 18.0f, 7.0f);
    LinearGradientBrush bodyBrush(body,
        Color(255, 250, 205, 70),
        Color(255, 215, 160, 35),
        LinearGradientModeVertical);
    g.FillRectangle(&bodyBrush, body);

    Pen hexLine(Color(80, 90, 60, 10), 1.0f);
    g.DrawLine(&hexLine, -13.0f, 0.0f, 5.0f, 0.0f);

    RectF eraser(-17.0f, -3.5f, 4.0f, 7.0f);
    SolidBrush eraserBrush(Color(255, 235, 130, 150));
    g.FillRectangle(&eraserBrush, eraser);

    LinearGradientBrush ferrule(RectF(-13.5f, -3.5f, 1.5f, 7.0f),
        Color(255, 200, 200, 205),
        Color(255, 140, 140, 148),
        LinearGradientModeVertical);
    g.FillRectangle(&ferrule, -13.5f, -3.5f, 1.5f, 7.0f);

    PointF wood[3] = { {5.0f, -3.5f}, {5.0f, 3.5f}, {11.0f, 0.0f} };
    SolidBrush woodBrush(Color(255, 230, 200, 150));
    g.FillPolygon(&woodBrush, wood, 3);

    PointF lead[3] = { {9.5f, -1.4f}, {9.5f, 1.4f}, {12.5f, 0.0f} };
    Color leadCol = selected ? accent : Color(255, 50, 52, 60);
    SolidBrush leadBrush(leadCol);
    g.FillPolygon(&leadBrush, lead, 3);

    g.Restore(st);
}

static void DrawHighlighterIcon(Graphics& g, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.RotateTransform(-40);

    Color capTint(255,
        (BYTE)min(255, (int)accent.GetR() + 40),
        (BYTE)min(255, (int)accent.GetG() + 40),
        (BYTE)min(255, (int)accent.GetB() + 40));
    GraphicsPath cap;
    RectF capRect(-15.0f, -5.5f, 8.0f, 11.0f);
    RoundRectPath(cap, capRect, 2.0f);
    SolidBrush capBrush(capTint);
    g.FillPath(&capBrush, &cap);

    GraphicsPath body;
    RectF bodyRect(-7.0f, -5.5f, 14.0f, 11.0f);
    RoundRectPath(body, bodyRect, 2.0f);
    Color bodyTop    = selected ? Color(255, 90, 94, 110) : Color(255, 70, 74, 86);
    Color bodyBottom = selected ? Color(255, 55, 60, 76)  : Color(255, 40, 44, 54);
    LinearGradientBrush bodyBrush(bodyRect, bodyTop, bodyBottom,
        LinearGradientModeVertical);
    g.FillPath(&bodyBrush, &body);

    SolidBrush bandBrush(accent);
    g.FillRectangle(&bandBrush, -7.0f, -5.5f, 14.0f, 2.5f);

    PointF tip[4] = {
        { 7.0f, -5.5f }, {15.0f, -2.5f }, {15.0f, 2.5f }, { 7.0f, 5.5f }
    };
    SolidBrush tipBrush(accent);
    g.FillPolygon(&tipBrush, tip, 4);

    Pen hi(Color(140, 255, 255, 255), 1.0f);
    g.DrawLine(&hi, 7.0f, -5.5f, 14.0f, -2.5f);

    g.Restore(st);
}

static void DrawEraserIcon(Graphics& g, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.RotateTransform(-18);

    GraphicsPath body;
    RectF bodyRect(-12.0f, -7.0f, 24.0f, 14.0f);
    RoundRectPath(body, bodyRect, 3.0f);

    Region region(&body);
    g.SetClip(&region);
    SolidBrush top(Color(255, 235, 120, 145));
    g.FillRectangle(&top, -12.0f, -7.0f, 24.0f, 6.0f);
    SolidBrush bot(Color(255, 250, 235, 210));
    g.FillRectangle(&bot, -12.0f, -1.0f, 24.0f, 8.0f);
    g.ResetClip();

    Pen bevel(Color(120, 0, 0, 0), 1.0f);
    g.DrawLine(&bevel, -12.0f, -1.0f, 12.0f, -1.0f);

    Pen outline(Color(180, 30, 30, 35), 1.0f);
    g.DrawPath(&outline, &body);

    if (selected) {
        Pen flick(accent, 1.4f);
        flick.SetStartCap(LineCapRound);
        flick.SetEndCap(LineCapRound);
        g.DrawLine(&flick, -16.0f, -4.0f, -19.0f, -4.0f);
        g.DrawLine(&flick, -16.0f,  0.0f, -20.0f,  0.0f);
        g.DrawLine(&flick, -16.0f,  4.0f, -19.0f,  4.0f);
    }

    g.Restore(st);
}

static void DrawTextIcon(Graphics& g, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Color glyphCol = selected ? accent : Color(255, 235, 235, 240);
    SolidBrush glyph(glyphCol);

    FontFamily fam(L"Segoe UI");
    Font font(&fam, 26.0f, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    RectF letterBox(-17.0f, -15.0f, 26.0f, 30.0f);
    g.DrawString(L"T", -1, &font, letterBox, &sf, &glyph);

    Pen caret(glyphCol, 1.8f);
    caret.SetStartCap(LineCapRound);
    caret.SetEndCap(LineCapRound);
    g.DrawLine(&caret, 9.0f, -9.0f, 9.0f, 9.0f);

    g.Restore(st);
}

static void DrawToolGlyph(Graphics& g, Tool t, Color accent, bool selected) {
    switch (t) {
        case Tool::Pen:         DrawPenIcon(g, accent, selected); break;
        case Tool::Pencil:      DrawPencilIcon(g, accent, selected); break;
        case Tool::Highlighter: DrawHighlighterIcon(g, accent, selected); break;
        case Tool::Eraser:      DrawEraserIcon(g, accent, selected); break;
        case Tool::Text:        DrawTextIcon(g, accent, selected); break;
    }
}


static void DrawClearGlyph(Graphics& g, Color c, float s) {
    Pen p(c, 1.6f * s);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    p.SetLineJoin(LineJoinRound);

    g.DrawLine(&p, -6.0f * s, -5.0f * s, 6.0f * s, -5.0f * s);
    g.DrawLine(&p, -2.5f * s, -5.0f * s, -2.0f * s, -7.5f * s);
    g.DrawLine(&p, -2.0f * s, -7.5f * s,  2.0f * s, -7.5f * s);
    g.DrawLine(&p,  2.0f * s, -7.5f * s,  2.5f * s, -5.0f * s);

    GraphicsPath body;
    body.AddLine(-4.5f * s, -5.0f * s, -3.7f * s, 7.0f * s);
    body.AddLine(-3.7f * s,  7.0f * s,  3.7f * s, 7.0f * s);
    body.AddLine( 3.7f * s,  7.0f * s,  4.5f * s, -5.0f * s);
    g.DrawPath(&p, &body);

    g.DrawLine(&p, -1.6f * s, -2.0f * s, -1.6f * s, 4.5f * s);
    g.DrawLine(&p,  1.6f * s, -2.0f * s,  1.6f * s, 4.5f * s);
}

static void DrawCloseGlyph(Graphics& g, Color c, float s) {
    Pen p(c, 1.8f * s);
    p.SetStartCap(LineCapRound);
    p.SetEndCap(LineCapRound);
    g.DrawLine(&p, -5.0f * s, -5.0f * s, 5.0f * s, 5.0f * s);
    g.DrawLine(&p,  5.0f * s, -5.0f * s, -5.0f * s, 5.0f * s);
}

static void DrawColorWheel(Graphics& g, RECT rc, float val) {
    int d = rc.right - rc.left;
    if (d <= 0) return;

    Bitmap bmp(d, d, PixelFormat32bppARGB);
    Rect lockR(0, 0, d, d);
    BitmapData bd;
    if (bmp.LockBits(&lockR, ImageLockModeWrite,
                     PixelFormat32bppARGB, &bd) != Ok) {
        return;
    }
    uint8_t* base = (uint8_t*)bd.Scan0;
    float cx = (d - 1) / 2.0f;
    float cy = (d - 1) / 2.0f;
    float radius = d / 2.0f;
    for (int yy = 0; yy < d; ++yy) {
        uint32_t* row = (uint32_t*)(base + (size_t)yy * bd.Stride);
        for (int xx = 0; xx < d; ++xx) {
            float dx = xx - cx;
            float dy = yy - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            uint32_t out = 0;
            if (dist <= radius) {
                float sat = dist / radius;
                if (sat > 1.0f) sat = 1.0f;
                float hue = atan2f(dy, dx) * 57.29578f;
                if (hue < 0.0f) hue += 360.0f;
                Swatch c = HSVtoRGB(hue, sat, val);
                float a = radius - dist;
                if (a > 1.0f) a = 1.0f;
                if (a < 0.0f) a = 0.0f;
                BYTE av = (BYTE)lroundf(a * 255.0f);
                out = ((uint32_t)av << 24) | ((uint32_t)c.r << 16)
                    | ((uint32_t)c.g << 8) | (uint32_t)c.b;
            }
            row[xx] = out;
        }
    }
    bmp.UnlockBits(&bd);
    g.DrawImage(&bmp, rc.left, rc.top, d, d);

    // Selection cursor at the current hue/sat
    float ang = A.hue * 0.0174533f;
    float r   = A.sat * radius;
    float hx  = rc.left + cx + cosf(ang) * r;
    float hy  = rc.top  + cy + sinf(ang) * r;
    float cr  = max(2.5f, radius * 0.10f);
    Pen ringO(Color(255, 20, 20, 24), 2.2f);
    g.DrawEllipse(&ringO, hx - cr, hy - cr, cr * 2, cr * 2);
    Pen ringI(Color(255, 255, 255, 255), 1.3f);
    g.DrawEllipse(&ringI, hx - cr, hy - cr, cr * 2, cr * 2);
}

// Horizontal brightness slider for the current hue/sat
static void DrawBrightnessBar(Graphics& g, RECT rc, float s) {
    RectF rf((REAL)rc.left, (REAL)rc.top,
             (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top));
    float rad = rf.Height / 2.0f;

    Swatch full = HSVtoRGB(A.hue, A.sat, 1.0f);
    RectF gradRect(rf.X - 1.0f, rf.Y, rf.Width + 2.0f, rf.Height);
    LinearGradientBrush grad(gradRect,
                             Color(255, 0, 0, 0),
                             Color(255, full.r, full.g, full.b),
                             LinearGradientModeHorizontal);
    FillRoundRect(g, grad, rf, rad);

    float hr = rf.Height / 2.0f + 1.5f * s;
    float travel = max(0.0f, rf.Width - 2.0f * hr);
    float hx = rf.X + hr + A.val * travel;
    float hy = rf.Y + rf.Height / 2.0f;
    SolidBrush thumbFill(curCol(255));
    g.FillEllipse(&thumbFill, hx - hr, hy - hr, hr * 2, hr * 2);
    Pen thumbRing(Color(255, 255, 255, 255), 1.6f * s);
    g.DrawEllipse(&thumbRing, hx - hr, hy - hr, hr * 2, hr * 2);
}

static float BrightnessValueFromX(const WidgetLayout& L, int px) {
    float bh     = (float)(L.bright.bottom - L.bright.top);
    float hr     = bh / 2.0f + 1.5f * L.scale;
    float travel = (float)(L.bright.right - L.bright.left) - 2.0f * hr;
    float v = (travel > 0.0f)
              ? ((float)px - (float)L.bright.left - hr) / travel
              : 0.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

static void PaintWidget(HDC hdc, RECT client) {
    int W = client.right - client.left;
    int H = client.bottom - client.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    SolidBrush bg(Color(255, 28, 28, 32));
    g.FillRectangle(&bg, 0, 0, W, H);

    WidgetLayout L = ComputeLayout();
    const float s = L.scale;

    // Drag handle dots.
    SolidBrush dot(Color(120, 220, 220, 220));
    float dotGap = 5.0f * s;
    float dotR   = max(1.0f, 1.5f * s);
    float dcy    = (float)(L.drag.top + (L.drag.bottom - L.drag.top) / 2);
    for (int i = 0; i < 3; ++i) {
        float cx = (float)(W / 2) + (i - 1) * dotGap;
        g.FillEllipse(&dot, cx - dotR, dcy - dotR, dotR * 2, dotR * 2);
    }

    Color iconAccent = curCol(255);
    const float iconScale = (float)L.toolSize / 52.0f;
    for (int i = 0; i < L.toolCount; ++i) {
        RECT rb = L.tools[i];
        RectF rf((REAL)rb.left, (REAL)rb.top,
                 (REAL)(rb.right - rb.left), (REAL)(rb.bottom - rb.top));

        bool sel = L.slotGroup[i] ? IsDrawTool(A.tool) : (A.tool == L.slot[i]);
        Color fill = sel ? Color(255, 48, 64, 92) : Color(255, 40, 42, 48);
        SolidBrush b(fill);
        FillRoundRect(g, b, rf, (float)L.corner);

        GraphicsState gs = g.Save();
        g.TranslateTransform(rf.X + rf.Width / 2.0f, rf.Y + rf.Height / 2.0f);
        g.ScaleTransform(iconScale, iconScale);
        DrawToolGlyph(g, L.slot[i], iconAccent, sel);
        g.Restore(gs);

        // Expand hint on the collapsed group button
        if (L.slotGroup[i]) {
            float ccx = rf.X + rf.Width / 2.0f;
            float cyb = rf.Y + rf.Height - 6.0f * s;
            Pen ch(Color(170, 225, 225, 232), 1.3f * s);
            ch.SetStartCap(LineCapRound);
            ch.SetEndCap(LineCapRound);
            g.DrawLine(&ch, ccx - 3.0f * s, cyb, ccx, cyb + 2.4f * s);
            g.DrawLine(&ch, ccx + 3.0f * s, cyb, ccx, cyb + 2.4f * s);
        }
    }

    int trowMidY = (L.thicks[0].top + L.thicks[0].bottom) / 2;
    for (int i = 0; i < kThicknessCount; ++i) {
        float cx = L.thickCenterX[i];
        float cy = (float)trowMidY;
        float radius = L.thickRadii[i];
        bool sel = (i == A.thicknessIdx);
        Color c = sel ? curCol(255) : Color(255, 150, 152, 158);
        SolidBrush bd(c);
        g.FillEllipse(&bd, cx - radius, cy - radius, radius * 2, radius * 2);
    }

    DrawColorWheel(g, L.wheel, A.val);
    DrawBrightnessBar(g, L.bright, s);

    auto drawAction = [&](RECT b, Color accent, bool isClear) {
        RectF rf((REAL)b.left, (REAL)b.top,
                 (REAL)(b.right - b.left), (REAL)(b.bottom - b.top));
        SolidBrush fill(Color(255, 40, 42, 48));
        FillRoundRect(g, fill, rf, 7.0f * s);
        GraphicsState gs = g.Save();
        g.TranslateTransform(rf.X + rf.Width / 2.0f, rf.Y + rf.Height / 2.0f);
        if (isClear) DrawClearGlyph(g, accent, s);
        else         DrawCloseGlyph(g, accent, s);
        g.Restore(gs);
    };
    drawAction(L.clearBtn, Color(255, 235, 110, 110), true);
    drawAction(L.closeBtn, Color(255, 200, 205, 214), false);

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void PickFromWheel(const WidgetLayout& L, POINT p) {
    int d = L.wheel.right - L.wheel.left;
    float cx = L.wheel.left + (d - 1) / 2.0f;
    float cy = L.wheel.top  + (d - 1) / 2.0f;
    float radius = d / 2.0f;
    float dx = p.x - cx;
    float dy = p.y - cy;
    float dist = sqrtf(dx * dx + dy * dy);
    float sat = (radius > 0.0f) ? dist / radius : 0.0f;
    if (sat > 1.0f) sat = 1.0f;
    float hue = atan2f(dy, dx) * 57.29578f;
    if (hue < 0.0f) hue += 360.0f;
    SetPickerHSV(hue, sat, A.val);
}

LRESULT CALLBACK WidgetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    enum class Drag { None, Wheel, Bright };
    static Drag drag = Drag::None;
    switch (msg) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            WidgetLayout L = ComputeLayout();
            if (p.y >= L.drag.top && p.y <= L.drag.bottom) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            WidgetLayout L = ComputeLayout();

            for (int i = 0; i < L.toolCount; ++i) {
                if (!PtInRect(&L.tools[i], p)) continue;
                if (L.slotGroup[i]) {
                    A.toolGroupOpen = true;
                    RepositionWidget();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                Tool next = L.slot[i];
                if (A.tool == Tool::Text && next != Tool::Text)
                    CommitTextEdit();
                A.tool = next;
                if (IsDrawTool(next)) A.drawTool = next;
                bool wasOpen = A.toolGroupOpen;
                A.toolGroupOpen = false;
                if (wasOpen) RepositionWidget();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            for (int i = 0; i < kThicknessCount; ++i) {
                if (PtInRect(&L.thicks[i], p)) {
                    A.thicknessIdx = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    RedrawTextEditIfActive();
                    return 0;
                }
            }
            if (PtInRect(&L.wheel, p)) {
                drag = Drag::Wheel;
                PickFromWheel(L, p);
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                RedrawTextEditIfActive();
                return 0;
            }
            RECT brightHit = L.bright;
            int  bvpad = R(7.0f * L.scale);
            brightHit.top    -= bvpad;
            brightHit.bottom += bvpad;
            if (PtInRect(&brightHit, p)) {
                drag = Drag::Bright;
                SetPickerHSV(A.hue, A.sat, BrightnessValueFromX(L, p.x));
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                RedrawTextEditIfActive();
                return 0;
            }
            if (PtInRect(&L.clearBtn, p)) {
                ClearCanvas();
                return 0;
            }
            if (PtInRect(&L.closeBtn, p)) {
                DeactivateOverlay();
                return 0;
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (GetCapture() != hwnd) return 0;
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            WidgetLayout L = ComputeLayout();
            // Stick with whichever control the drag started on.
            if (drag == Drag::Wheel) {
                PickFromWheel(L, p);
            } else {
                SetPickerHSV(A.hue, A.sat, BrightnessValueFromX(L, p.x));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            RedrawTextEditIfActive();
            return 0;
        }
        case WM_LBUTTONUP:
            drag = Drag::None;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            PaintWidget(hdc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_RBUTTONDOWN: {
            A.anchor = (A.anchor == SideAnchor::Right) ? SideAnchor::Left : SideAnchor::Right;
            RepositionWidget();
            return 0;
        }
        case WM_DPICHANGED:
            RepositionWidget();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
