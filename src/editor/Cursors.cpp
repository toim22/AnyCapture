// Cursor stamps (C25): arrow, hand, I-beam, busy, crosshair, move, resize and click indicators drawn as vectors at any
// size — placed from the stamp gallery ("Cursors") as editable, resizable Stamp objects ("cursor:<kind>"). Compiled in
// both programs because Render.cpp (also in the capture, for pins) draws the stamps.
#include "EditorTools.h"

using namespace Gdiplus;

namespace snap::ed {

// ================================================================ C25 cursor stamps
std::vector<std::pair<std::wstring, std::wstring>> CursorStamps() {
    return { { L"cursor:arrow", Tr(L"Seta do mouse") }, { L"cursor:arrowdark", Tr(L"Seta do mouse (escura)") },
             { L"cursor:hand", Tr(L"Mão (link)") }, { L"cursor:ibeam", Tr(L"Cursor de texto") }, { L"cursor:wait", Tr(L"Ocupado") },
             { L"cursor:cross", Tr(L"Mira") }, { L"cursor:move", Tr(L"Mover") }, { L"cursor:resize", Tr(L"Redimensionar") },
             { L"cursor:click", Tr(L"Indicador de clique") }, { L"cursor:clickright", Tr(L"Clique direito") } };
}

namespace {
void Poly(GraphicsPath& gp, std::initializer_list<PointF> pts, float k, PointF o = { 0, 0 }) {
    std::vector<PointF> v;
    for (auto p : pts) v.push_back({ o.X + p.X * k, o.Y + p.Y * k });
    gp.AddPolygon(v.data(), (INT)v.size());
}
void RoundBox(GraphicsPath& gp, RectF r, float rad) {
    rad = std::min({ rad, r.Width / 2, r.Height / 2 });
    gp.StartFigure();
    gp.AddArc(r.X, r.Y, 2 * rad, 2 * rad, 180, 90);
    gp.AddArc(r.GetRight() - 2 * rad, r.Y, 2 * rad, 2 * rad, 270, 90);
    gp.AddArc(r.GetRight() - 2 * rad, r.GetBottom() - 2 * rad, 2 * rad, 2 * rad, 0, 90);
    gp.AddArc(r.X, r.GetBottom() - 2 * rad, 2 * rad, 2 * rad, 90, 90);
    gp.CloseFigure();
}
// filled shape with an outline around the union (the outline is drawn first, wider, and the fill covers the inner lines)
void Outlined(Graphics& g, GraphicsPath& gp, Color fill, Color line, float w) {
    Pen p(line, 2 * w);
    p.SetLineJoin(LineJoinRound);
    g.DrawPath(&p, &gp);
    SolidBrush b(fill);
    g.FillPath(&b, &gp);
}
} // namespace

Image RenderCursorStamp(const std::wstring& id, int px) {
    std::wstring kind = id.size() > 7 ? id.substr(7) : L"arrow";
    px = std::max(16, px);
    // drawing grid: 24 units on the longer side
    const float k = px / 24.0f, w = std::max(1.0f, k * 0.9f);
    int iw = px, ih = px;
    if (kind == L"arrow" || kind == L"arrowdark") iw = (int)std::ceil(px * 15 / 24.0f);
    Image img(iw, ih, 0);
    auto bmp = img.WrapGdip();
    Graphics g(bmp.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    const Color white(255, 255, 255, 255), black(255, 0, 0, 0);
    if (kind == L"arrow" || kind == L"arrowdark") {
        GraphicsPath gp;
        Poly(gp, { { 1.5f, 1 }, { 1.5f, 19.5f }, { 6, 15.5f }, { 9, 22.5f }, { 12, 21.2f }, { 9.1f, 14.5f }, { 14.5f, 14.5f } }, k);
        bool dark = kind == L"arrowdark";
        Outlined(g, gp, dark ? black : white, dark ? white : black, w);
    } else if (kind == L"hand") {
        GraphicsPath gp;
        gp.SetFillMode(FillModeWinding);
        RoundBox(gp, RectF(7 * k, 1 * k, 3.6f * k, 13 * k), 1.8f * k);              // index finger
        RoundBox(gp, RectF(10.2f * k, 7.5f * k, 3.4f * k, 7 * k), 1.7f * k);        // fingers
        RoundBox(gp, RectF(13.2f * k, 8.5f * k, 3.4f * k, 6.5f * k), 1.7f * k);
        RoundBox(gp, RectF(16.2f * k, 9.8f * k, 3.2f * k, 5.6f * k), 1.6f * k);
        RoundBox(gp, RectF(7 * k, 11 * k, 12.4f * k, 11.5f * k), 3.5f * k);        // palm
        Matrix m; m.RotateAt(-35, PointF(5 * k, 15 * k));
        GraphicsPath thumb; RoundBox(thumb, RectF(1.5f * k, 13.5f * k, 8 * k, 3.4f * k), 1.7f * k);
        thumb.Transform(&m);
        gp.AddPath(&thumb, FALSE);
        Outlined(g, gp, white, black, w);
        Pen crease(Color(255, 0, 0, 0), w * 0.8f);
        g.DrawLine(&crease, 10.4f * k, 11 * k, 10.4f * k, 14 * k);
        g.DrawLine(&crease, 13.4f * k, 11.5f * k, 13.4f * k, 14.2f * k);
        g.DrawLine(&crease, 16.4f * k, 12.2f * k, 16.4f * k, 14.6f * k);
    } else if (kind == L"ibeam") {
        GraphicsPath gp;
        Poly(gp, { { 7, 2 }, { 11, 2 }, { 12, 3 }, { 13, 2 }, { 17, 2 }, { 17, 4 }, { 13, 4 }, { 13, 20 }, { 17, 20 }, { 17, 22 },
                   { 13, 22 }, { 12, 21 }, { 11, 22 }, { 7, 22 }, { 7, 20 }, { 11, 20 }, { 11, 4 }, { 7, 4 } }, k);
        Outlined(g, gp, black, white, w);
    } else if (kind == L"wait") {
        RectF r(3 * k, 3 * k, 18 * k, 18 * k);
        Pen ring(Color(255, 220, 224, 230), 3.2f * k);
        g.DrawEllipse(&ring, r);
        Pen arc(Color(255, 0, 120, 215), 3.2f * k);
        arc.SetStartCap(LineCapRound); arc.SetEndCap(LineCapRound);
        g.DrawArc(&arc, r, -90, 110);
    } else if (kind == L"cross") {
        GraphicsPath gp;
        gp.AddRectangle(RectF(11 * k, 1 * k, 2 * k, 22 * k));
        gp.AddRectangle(RectF(1 * k, 11 * k, 22 * k, 2 * k));
        gp.SetFillMode(FillModeWinding);
        Outlined(g, gp, black, white, w);
    } else if (kind == L"move" || kind == L"resize") {
        GraphicsPath gp;
        gp.SetFillMode(FillModeWinding);
        // double arrow (resize) or four arrows (move)
        Poly(gp, { { 1, 12 }, { 6, 7 }, { 6, 10.5f }, { 18, 10.5f }, { 18, 7 }, { 23, 12 }, { 18, 17 }, { 18, 13.5f }, { 6, 13.5f }, { 6, 17 } }, k);
        if (kind == L"move") Poly(gp, { { 12, 1 }, { 17, 6 }, { 13.5f, 6 }, { 13.5f, 18 }, { 17, 18 }, { 12, 23 }, { 7, 18 }, { 10.5f, 18 }, { 10.5f, 6 }, { 7, 6 } }, k);
        Outlined(g, gp, black, white, w);
    } else {   // click / clickright: concentric rings with a dot (where the click happened)
        Color c = kind == L"clickright" ? Color(255, 0, 120, 215) : Color(255, 229, 50, 45);
        for (int i = 0; i < 3; i++) {
            float rr = (4.5f + i * 3.6f) * k;
            Pen p(Color((BYTE)(230 - i * 70), c.GetR(), c.GetG(), c.GetB()), 1.8f * k);
            g.DrawEllipse(&p, 12 * k - rr, 12 * k - rr, 2 * rr, 2 * rr);
        }
        SolidBrush dot(c);
        g.FillEllipse(&dot, 12 * k - 2.4f * k, 12 * k - 2.4f * k, 4.8f * k, 4.8f * k);
    }
    g.Flush(FlushIntentionSync);
    return img;
}

} // namespace snap::ed
