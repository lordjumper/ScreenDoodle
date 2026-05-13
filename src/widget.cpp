#include "widget.h"
#include "state.h"
#include "canvas.h"

using namespace Gdiplus;

static inline Color currentColor(BYTE alpha = 255) {
    const Swatch& s = kPalette[A.paletteIdx];
    return Color(alpha, s.r, s.g, s.b);
}

struct WidgetLayout {
    int W = 72;
    int dragH = 14;
    int padX = 8;
    int gap = 6;
    int toolSize = 52;
    int sectionGap = 10;
    int swatchSize = 16;
    int swatchPerRow = 3;
    int thickRowH = 38;
    int actionH = 32;

    float thickRadii[kThicknessCount]   = {1.5f, 2.5f, 3.7f, 5.0f, 6.5f};
    float thickCenterX[kThicknessCount] = {};

    RECT drag{};
    RECT tools[4]{};
    RECT thicks[kThicknessCount]{};
    RECT swatches[kPaletteCount]{};
    RECT clearBtn{};
    RECT closeBtn{};
    int  total = 0;
};

static WidgetLayout ComputeLayout() {
    WidgetLayout L;
    int y = 0;
    L.drag = {0, y, L.W, y + L.dragH};
    y += L.dragH;

    int x = (L.W - L.toolSize) / 2;
    for (int i = 0; i < 4; ++i) {
        L.tools[i] = {x, y, x + L.toolSize, y + L.toolSize};
        y += L.toolSize + L.gap;
    }
    y -= L.gap;
    y += L.sectionGap;

    float sumDia = 0.0f;
    for (int i = 0; i < kThicknessCount; ++i) sumDia += L.thickRadii[i] * 2.0f;
    float availTh = (float)(L.W - 2 * L.padX);
    float gapTh   = (availTh - sumDia) / (float)(kThicknessCount - 1);
    if (gapTh < 0.0f) gapTh = 0.0f;
    float fx = (float)L.padX;
    for (int i = 0; i < kThicknessCount; ++i) {
        L.thickCenterX[i] = fx + L.thickRadii[i];
        fx = L.thickCenterX[i] + L.thickRadii[i] + gapTh;
    }

    for (int i = 0; i < kThicknessCount; ++i) {
        int hitLeft  = (i == 0)
                       ? L.padX
                       : (int)((L.thickCenterX[i - 1] + L.thickCenterX[i]) * 0.5f);
        int hitRight = (i == kThicknessCount - 1)
                       ? (L.W - L.padX)
                       : (int)((L.thickCenterX[i] + L.thickCenterX[i + 1]) * 0.5f);
        L.thicks[i] = {hitLeft, y, hitRight, y + L.thickRowH};
    }
    y += L.thickRowH;
    y += L.sectionGap;

    int rows = (kPaletteCount + L.swatchPerRow - 1) / L.swatchPerRow;
    int gridW = L.swatchPerRow * L.swatchSize + (L.swatchPerRow - 1) * L.gap;
    int sxBase = (L.W - gridW) / 2;
    for (int i = 0; i < kPaletteCount; ++i) {
        int r = i / L.swatchPerRow;
        int c = i % L.swatchPerRow;
        int px = sxBase + c * (L.swatchSize + L.gap);
        int py = y + r * (L.swatchSize + L.gap);
        L.swatches[i] = {px, py, px + L.swatchSize, py + L.swatchSize};
    }
    y += rows * L.swatchSize + (rows - 1) * L.gap;
    y += L.sectionGap;

    L.clearBtn = {L.padX, y, L.W - L.padX, y + L.actionH};
    y += L.actionH + L.gap;
    L.closeBtn = {L.padX, y, L.W - L.padX, y + L.actionH};
    y += L.actionH;
    y += 8;

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
    int margin = 18;
    int x, y;
    if (A.anchor == SideAnchor::Right) {
        x = mi.rcWork.right - A.wgW - margin;
    } else {
        x = mi.rcWork.left + margin;
    }
    y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - A.wgH) / 2;

    SetWindowPos(A.widget, HWND_TOPMOST, x, y, A.wgW, A.wgH,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    HRGN rgn = CreateRoundRectRgn(0, 0, A.wgW + 1, A.wgH + 1, 18, 18);
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

static void DrawPenIcon(Graphics& g, RectF r, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
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

    PointF nib[3] = {
        { 6.0f, -3.5f },
        { 6.0f,  3.5f },
        {14.0f,  0.0f }
    };
    SolidBrush nibBrush(accent);
    g.FillPolygon(&nibBrush, nib, 3);

    SolidBrush ink(Color(255, 20, 22, 28));
    g.FillEllipse(&ink, 12.5f, -1.0f, 2.0f, 2.0f);

    g.Restore(st);
}

static void DrawPencilIcon(Graphics& g, RectF r, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
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

static void DrawHighlighterIcon(Graphics& g, RectF r, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
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
        { 7.0f, -5.5f },
        {15.0f, -2.5f },
        {15.0f,  2.5f },
        { 7.0f,  5.5f }
    };
    SolidBrush tipBrush(accent);
    g.FillPolygon(&tipBrush, tip, 4);

    Pen hi(Color(140, 255, 255, 255), 1.0f);
    g.DrawLine(&hi, 7.0f, -5.5f, 14.0f, -2.5f);

    g.Restore(st);
}

static void DrawEraserIcon(Graphics& g, RectF r, Color accent, bool selected) {
    GraphicsState st = g.Save();
    g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
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

    SolidBrush dot(Color(120, 220, 220, 220));
    for (int i = 0; i < 3; ++i) {
        float cx = (float)(W / 2 - 10 + i * 10);
        float cy = (float)(L.drag.top + (L.drag.bottom - L.drag.top) / 2);
        g.FillEllipse(&dot, cx - 1.5f, cy - 1.5f, 3.0f, 3.0f);
    }

    const Tool tools[4] = { Tool::Pen, Tool::Pencil, Tool::Highlighter, Tool::Eraser };
    for (int i = 0; i < 4; ++i) {
        RECT rb = L.tools[i];
        RectF rf((REAL)rb.left, (REAL)rb.top,
                 (REAL)(rb.right - rb.left), (REAL)(rb.bottom - rb.top));
        bool sel = (A.tool == tools[i]);
        Color fill = sel ? Color(255, 60, 62, 70) : Color(255, 38, 40, 46);
        SolidBrush b(fill);
        FillRoundRect(g, b, rf, 10.0f);
        if (sel) {
            Pen accent(Color(255, 90, 170, 255), 1.6f);
            GraphicsPath path;
            path.AddArc(rf.X, rf.Y, 20.0f, 20.0f, 180, 90);
            path.AddArc(rf.X + rf.Width - 20, rf.Y, 20.0f, 20.0f, 270, 90);
            path.AddArc(rf.X + rf.Width - 20, rf.Y + rf.Height - 20, 20.0f, 20.0f, 0, 90);
            path.AddArc(rf.X, rf.Y + rf.Height - 20, 20.0f, 20.0f, 90, 90);
            path.CloseFigure();
            g.DrawPath(&accent, &path);
        }
        Color iconAccent = currentColor(255);
        switch (tools[i]) {
            case Tool::Pen:         DrawPenIcon(g, rf, iconAccent, sel); break;
            case Tool::Pencil:      DrawPencilIcon(g, rf, iconAccent, sel); break;
            case Tool::Highlighter: DrawHighlighterIcon(g, rf, iconAccent, sel); break;
            case Tool::Eraser:      DrawEraserIcon(g, rf, iconAccent, sel); break;
        }
    }

    int trowTop    = L.thicks[0].top;
    int trowBottom = L.thicks[0].bottom;
    int trowMidY   = (trowTop + trowBottom) / 2;
    for (int i = 0; i < kThicknessCount; ++i) {
        float cx = L.thickCenterX[i];
        float cy = (float)trowMidY;
        float radius = L.thickRadii[i];
        bool sel = (i == A.thicknessIdx);
        Color c = sel ? currentColor(255) : Color(255, 200, 200, 205);
        SolidBrush bd(c);
        g.FillEllipse(&bd, cx - radius, cy - radius, radius * 2, radius * 2);
        if (sel) {
            Pen ring(Color(180, 255, 255, 255), 1.2f);
            g.DrawEllipse(&ring, cx - radius - 2, cy - radius - 2,
                          (radius + 2) * 2, (radius + 2) * 2);
        }
    }

    for (int i = 0; i < kPaletteCount; ++i) {
        RECT sb = L.swatches[i];
        float cx = (float)((sb.left + sb.right) / 2);
        float cy = (float)((sb.top + sb.bottom) / 2);
        float r = (float)(sb.right - sb.left) / 2.0f;
        SolidBrush b(Color(255, kPalette[i].r, kPalette[i].g, kPalette[i].b));
        g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
        if (i == A.paletteIdx) {
            Pen ring(Color(255, 255, 255, 255), 1.8f);
            g.DrawEllipse(&ring, cx - r - 2, cy - r - 2, (r + 2) * 2, (r + 2) * 2);
        } else {
            Pen ring(Color(60, 255, 255, 255), 1.0f);
            g.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);
        }
    }

    auto drawAction = [&](RECT b, const WCHAR* label, Color accent) {
        RectF rf((REAL)b.left, (REAL)b.top,
                 (REAL)(b.right - b.left), (REAL)(b.bottom - b.top));
        SolidBrush bg2(Color(255, 40, 42, 48));
        FillRoundRect(g, bg2, rf, 8.0f);
        Pen border(accent, 2.0f);
        GraphicsPath path;
        float r = 8.0f;
        path.AddArc(rf.X, rf.Y, r*2, r*2, 180, 90);
        path.AddArc(rf.X + rf.Width - r*2, rf.Y, r*2, r*2, 270, 90);
        path.AddArc(rf.X + rf.Width - r*2, rf.Y + rf.Height - r*2, r*2, r*2, 0, 90);
        path.AddArc(rf.X, rf.Y + rf.Height - r*2, r*2, r*2, 90, 90);
        path.CloseFigure();
        g.DrawPath(&border, &path);

        FontFamily fam(L"Segoe UI");
        Font font(&fam, 9.5f, FontStyleRegular, UnitPoint);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush textBrush(Color(255, 235, 235, 240));
        g.DrawString(label, -1, &font, rf, &sf, &textBrush);
    };
    drawAction(L.clearBtn, L"Clear", Color(255, 235, 90, 90));
    drawAction(L.closeBtn, L"Close", Color(180, 120, 170, 230));

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK WidgetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
            const Tool tools[4] = { Tool::Pen, Tool::Pencil, Tool::Highlighter, Tool::Eraser };
            for (int i = 0; i < 4; ++i) {
                if (PtInRect(&L.tools[i], p)) {
                    A.tool = tools[i];
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            for (int i = 0; i < kThicknessCount; ++i) {
                if (PtInRect(&L.thicks[i], p)) {
                    A.thicknessIdx = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            for (int i = 0; i < kPaletteCount; ++i) {
                if (PtInRect(&L.swatches[i], p)) {
                    A.paletteIdx = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
