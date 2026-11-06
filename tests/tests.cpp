// Integration tests of Snapnab's critical pieces (console executable, not part of the product)
#include "Common.h"
#include "Image.h"
#include "Cv.h"
#include "Gif.h"
#include "SnapFile.h"
#include "Xml.h"
#include "Ipc.h"
#include "Effects.h"
#include "Icons.h"
#include "Ocr.h"
#include "Settings.h"
#include "../src/capture/Recorder.h"
#include "../src/capture/Panoramic.h"
#include "../src/capture/Webcam.h"
#include "../src/editor/Video.h"
#include "../src/editor/Document.h"
#include "../src/editor/QuickStyles.h"
#include "../src/editor/EditorTools.h"
#include "Beautify.h"
#include "Color.h"
#include "Net.h"
#include "Upload.h"
#include "snapcore.h"
#include <cstdio>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <cmath>

using namespace snap;

namespace snap::xmlref {      // the former C++ reader and writer (XmlReference.cpp)
std::string Serialize(const XmlNode& root, bool decl);
bool Parse(const std::string& text, XmlNode& root);
}

static int g_fail = 0;
static std::wstring TestTempDir() { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); std::wstring d = t; if (!d.empty() && d.back() == L'\\') d.pop_back(); return d; }
#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static Image Pattern(int w, int h, int seed) {
    Image img(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t hsh = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u) ^ (uint32_t)(seed * 83492791u);
            hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
            int v = (int)(hsh & 255);
            // smooth blocks + noise: similar to text/icons on a page
            int blk = ((x / 24 + (y / 18) * 7 + seed) * 2654435761u >> 24) & 255;
            img.at(x, y) = ARGB(255, (blk + v / 4) & 255, (blk * 3 + v / 8) & 255, (255 - blk + v / 6) & 255);
        }
    return img;
}

static std::wstring DigitsOfTest(const std::wstring& s) {
    std::wstring d;
    for (wchar_t c : s) if (c >= L'0' && c <= L'9') d += c;
    return d;
}

static std::wstring Tmp(const wchar_t* name) {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    return std::wstring(t) + L"snapnab_test_" + name;
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc > 2 && std::wstring(argv[1]) == L"dump") {      // debugging: prints the XML of a .snap
        std::vector<SnapFrame> fr;
        if (!LoadSnapFile(argv[2], fr)) { printf("failed to open\n"); return 1; }
        printf("%s", XmlSerialize(fr[0].doc).c_str());
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc > 2 && std::wstring(argv[1]) == L"comparesnap") {   // reads every .snap of a folder with both containers
        int files = 0, same = 0, bothFail = 0;
        double tRead[2] = {}, tWrite[2] = {};
        auto now = [] { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart / f.QuadPart; };
        std::wstring out = Tmp(L"compare.snap");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((std::wstring(argv[2]) + L"\\*.snap").c_str(), &fd);
        for (BOOL more = h != INVALID_HANDLE_VALUE; more; more = FindNextFileW(h, &fd)) {
            std::wstring p = std::wstring(argv[2]) + L"\\" + fd.cFileName;
            std::vector<SnapFrame> a, b;
            double t0 = now();
            SetSnapBackend(SnapBackend::Rust);
            bool okA = LoadSnapFile(p, a);
            double t1 = now();
            SetSnapBackend(SnapBackend::Windows);
            bool okB = LoadSnapFile(p, b);
            tRead[0] += t1 - t0; tRead[1] += now() - t1;
            if (okA) {                          // and writes it back with each one (to a temporary file)
                for (int k = 0; k < 2; k++) {
                    SetSnapBackend(k ? SnapBackend::Windows : SnapBackend::Rust);
                    double w0 = now(); SaveSnapFile(out, a); tWrite[k] += now() - w0;
                }
                DeleteFileW(out.c_str());
            }
            bool eq = okA == okB && a.size() == b.size();
            for (size_t i = 0; eq && i < a.size(); i++) {
                eq = a[i].base.w == b[i].base.w && a[i].base.px == b[i].base.px &&
                     XmlSerialize(a[i].doc) == XmlSerialize(b[i].doc) && a[i].embedded.size() == b[i].embedded.size();
                for (auto& e : a[i].embedded) eq = eq && b[i].embedded.count(e.first) && b[i].embedded.at(e.first).px == e.second.px;
            }
            files++;
            if (eq) same++;
            if (!okA && !okB) bothFail++;
            if (!eq) printf("  DIFFERENT %ls (Rust %s, Windows %s)\n", fd.cFileName, okA ? "ok" : "failed", okB ? "ok" : "failed");
        }
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        printf("%d .snap files: %d read the same by both containers (%d refused by both)\n", files, same, bothFail);
        printf("read:  Rust %.0f ms, Windows %.0f ms (PNG decoding included)\n", tRead[0] * 1000, tRead[1] * 1000);
        printf("write: Rust %.0f ms, Windows %.0f ms (PNG encoding included)\n", tWrite[0] * 1000, tWrite[1] * 1000);
        return same == files ? 0 : 1;
    }
    if (argc > 2 && std::wstring(argv[1]) == L"audio") {    // debugging: reads only the audio track
        MFStartup(MF_VERSION);
        IMFSourceReader* rd = nullptr;
        HRESULT hr = MFCreateSourceReaderFromURL(argv[2], nullptr, &rd);
        printf("open 0x%08X\n", hr);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        IMFMediaType* mt = nullptr; MFCreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio); mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM); mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        printf("settype 0x%08X\n", rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt));
        int peak = 0;
        for (int i = 0; i < 10000; i++) {
            DWORD idx, flags; LONGLONG ts; IMFSample* smp = nullptr;
            hr = rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &idx, &flags, &ts, &smp);
            if (FAILED(hr)) { printf("sample %d FAILED 0x%08X\n", i, hr); break; }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { printf("EOS after %d samples, last ts %.3f\n", i, ts / 1e7); break; }
            if (i < 3 || i % 50 == 0) printf("sample %d ts %.3f flags %u\n", i, ts / 1e7, flags);
            if (smp) {
                IMFMediaBuffer* b = nullptr; BYTE* p; DWORD len;
                if (SUCCEEDED(smp->ConvertToContiguousBuffer(&b)) && SUCCEEDED(b->Lock(&p, nullptr, &len))) {
                    for (DWORD k = 0; k + 1 < len; k += 2) peak = std::max(peak, std::abs((int)*(const int16_t*)(p + k)));
                    b->Unlock();
                }
                if (b) b->Release();
                smp->Release();
            }
        }
        printf("audio peak: %d of 32767 (%.1f%%)\n", peak, peak * 100.0 / 32767);
        return 0;
    }
    if (argc > 3 && std::wstring(argv[1]) == L"frame") {    // debugging: saves the first frame of an MP4 as PNG
        GdiplusSession gdip;
        ed::VideoReader rd;
        Image f; LONGLONG ts;
        if (!rd.Open(argv[2]) || !rd.ReadFrame(f, ts)) { printf("failed to read the video\n"); return 1; }
        printf("%dx%d -> %s\n", f.w, f.h, SaveImageFile(argv[3], f) ? "saved" : "failed");
        return 0;
    }
    GdiplusSession gdip;

    printf("[1] Phase correlation (scroll stitching)\n");
    {
        Image page = Pattern(700, 1600, 3);
        for (int s : { 37, 120, 250 }) {
            Image a = page.Crop({ 0, 100, 700, 600 });
            Image b = page.Crop({ 0, 100 + s, 700, 600 + s });       // content moved up by s px (scrolled down)
            double conf = 0;
            POINT t = cv::DetectTranslation(a, b, &conf);
            CHECK(-t.y == s && t.x == 0, "scroll of %d px detected as %d (dx=%d, conf %.1f)", s, -t.y, t.x, conf);
        }
    }

    printf("[2] Magic wand / inpainting\n");
    {
        Image img(100, 80, 0xFFFFFFFF);
        img.FillRect({ 20, 20, 60, 50 }, 0xFFFF0000);
        cv::Mask m = cv::DoWand(img, { 30, 30 }, 10, false);
        RECT b = cv::MaskBounds(m, img.w, img.h);
        CHECK(b.left == 20 && b.top == 20 && b.right == 60 && b.bottom == 50, "wand selects the red rectangle (%ld,%ld)-(%ld,%ld)", b.left, b.top, b.right, b.bottom);
        cv::Mask all = cv::DoWand(img, { 5, 5 }, 100, false);
        CHECK(!cv::MaskEmpty(all) && all.size() == 8000 && std::all_of(all.begin(), all.end(), [](uint8_t v) { return v == 255; }), "tolerance 100 = whole layer");
        Image f = img;
        cv::SmartFill(f, m);
        CHECK(f.at(40, 35) == 0xFFFFFFFF, "Smart fill paints the hole with the dominant surrounding color (0x%08X)", f.at(40, 35));
        float tol = cv::SmartMoveAutoTolerance(img);
        CHECK(tol >= 2 && tol <= 75, "Smart Move automatic tolerance in the range [2;75]: %.2f", tol);
    }

    printf("[3] .snap format (structured storage with a header)\n");
    {
        std::wstring p = Tmp(L"doc.snap");
        SnapFrame f; f.base = Pattern(321, 123, 9);
        f.doc.setW("CaptureMode", L"Region ç → 中文");    // non-ASCII on purpose: checks UTF-8 metadata
        XmlNode& objs = f.doc.add("Objects");
        objs.add("Arrow").setd("X1", 1.5).setd("Y1", 2).setd("X2", 100).setd("Y2", 50).set("PenColor", "#FFE5322D");
        f.embedded["Image_1.png"] = Pattern(10, 10, 1);
        objs.add("Image").set("StreamName", "Image_1.png");
        bool saved = SaveSnapFile(p, { f });
        std::vector<SnapFrame> in;
        bool loaded = LoadSnapFile(p, in);
        CHECK(saved && loaded && IsSnapFile(p), "save/open .snap");
        CHECK(loaded && in[0].base.px == f.base.px, "base image identical after a round trip");
        CHECK(loaded && in[0].doc.getW("CaptureMode") == L"Region ç → 中文", "UTF-8 metadata preserved");
        CHECK(loaded && in[0].embedded.count("Image_1.png") && in[0].embedded["Image_1.png"].w == 10, "embedded image by StreamName");
        auto d = ed::Document::Load(p);
        CHECK(d && d->st.objs.size() == 2 && d->st.objs[0]->type == ed::ObjType::Arrow, "Document loads objects (%zu)", d ? d->st.objs.size() : 0);
        DeleteFileW(p.c_str());

        // the two containers (snapcore in Rust and the Windows structured storage API) read each other's files: two
        // frames, metadata and an embedded image, written by one and read by the other
        SnapBackend def = GetSnapBackend();
        SnapFrame f2; f2.base = Pattern(64, 48, 3);
        f2.doc.add("Objects").add("Text").setW("Text", L"segunda moldura ✓");
        auto same = [&](const std::vector<SnapFrame>& v) {
            return v.size() == 2 && v[0].base.px == f.base.px && v[1].base.px == f2.base.px &&
                   v[0].doc.getW("CaptureMode") == L"Region ç → 中文" && v[0].embedded.count("Image_1.png") &&
                   v[0].embedded.at("Image_1.png").px == f.embedded["Image_1.png"].px &&
                   v[1].doc.child("Objects") && v[1].doc.child("Objects")->children[0].getW("Text") == L"segunda moldura ✓";
        };
        for (SnapBackend writer : { SnapBackend::Rust, SnapBackend::Windows }) {
            SetSnapBackend(writer);
            bool ok = SaveSnapFile(p, { f, f2 });
            SetSnapBackend(writer == SnapBackend::Rust ? SnapBackend::Windows : SnapBackend::Rust);
            std::vector<SnapFrame> v;
            ok = ok && LoadSnapFile(p, v);
            XmlNode doc0;
            ok = ok && same(v) && LoadSnapDocument(p, doc0) && doc0.getW("CaptureMode") == L"Region ç → 中文";
            CHECK(ok, "a .snap written by %s opens with %s (2 frames, metadata, embedded image)",
                  writer == SnapBackend::Rust ? "snapcore (Rust)" : "the Windows API", writer == SnapBackend::Rust ? "the Windows API" : "snapcore (Rust)");
        }
        // damaged files: the right header over garbage, and a file cut in half, are refused without crashing
        SetSnapBackend(SnapBackend::Rust);
        SaveSnapFile(p, { f, f2 });
        std::string bytes;
        {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD sz = GetFileSize(h, nullptr), rd = 0;
            bytes.resize(sz); ReadFile(h, bytes.data(), sz, &rd, nullptr); CloseHandle(h);
        }
        auto writeBytes = [&](const std::string& b) {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD wr = 0; WriteFile(h, b.data(), (DWORD)b.size(), &wr, nullptr); CloseHandle(h);
        };
        std::string garbage = bytes.substr(0, 24);
        for (int i = 0; i < 8192; i++) garbage += (char)((i * 2654435761u) >> 13);
        bool refused = true;
        for (const std::string& bad : { garbage, bytes.substr(0, bytes.size() / 2) }) {
            writeBytes(bad);
            for (SnapBackend b : { SnapBackend::Rust, SnapBackend::Windows }) {
                SetSnapBackend(b);
                std::vector<SnapFrame> v;
                refused = refused && IsSnapFile(p) && !LoadSnapFile(p, v);
            }
        }
        CHECK(refused, "damaged .snap files (garbage after the header, cut in half) are refused by both containers");
        SetSnapBackend(def);
        DeleteFileW(p.c_str());
    }

    printf("[4] Rendering of every object type + PDF\n");
    {
        auto d = ed::Document::FromImage(Pattern(400, 300, 4), L"t");
        for (int t = 0; t < (int)ed::ObjType::Count; t++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = (ed::ObjType)t; o->p1 = { 20.0f + t * 5, 20 }; o->p2 = { 200.0f, 150.0f + t * 3 };
            o->text = L"Hello"; o->tail = { 250, 250 }; o->pts = { { 10, 10 }, { 50, 60 }, { 90, 20 } };
            if (o->type == ed::ObjType::Picture) o->picture = std::make_shared<Image>(Pattern(30, 30, 2));
            d->st.objs.push_back(o);
        }
        Image r = d->Render();
        CHECK(r.w == 400 && r.h == 300 && r.px != d->st.base->px, "render of %zu objects", d->st.objs.size());
        // tool variations: 24 callouts, ends, dash patterns, shapes, steps, connected magnifier and quick styles
        auto v = ed::Document::FromImage(Pattern(600, 400, 3), L"v");
        for (int k = 0; k < ed::kCalloutShapes; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Callout; o->style.calloutShape = k; o->style.fill = 0xFF2080E0; o->style.width = 2;
            o->p1 = { 10.0f + (k % 6) * 95, 10.0f + (k / 6) * 90 }; o->p2 = { o->p1.X + 70, o->p1.Y + 45 }; o->tail = { o->p1.X + 90, o->p1.Y + 80 };
            o->text = L"Hello"; o->style.shaded = k % 2 == 0;
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::CapCount; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Arrow; o->style.arrowStart = k; o->style.arrowEnd = (k + 3) % ed::CapCount; o->style.dash = k % ed::DashCount;
            o->style.bezier = k % 2 == 1; o->p1 = { 20.0f, 30.0f + k * 40 }; o->p2 = { 300.0f, 50.0f + k * 40 }; o->ctrl = { 160.0f, 10.0f + k * 40 };
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::ShapeCount; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Shape; o->style.shapeKind = k; o->style.fill = k % 2 ? 0xFF40A040 : 0; o->style.shadowDir = k % 9;
            o->p1 = { 320.0f + (k % 4) * 65, 20.0f + (k / 4) * 70 }; o->p2 = { o->p1.X + 55, o->p1.Y + 50 };
            v->st.objs.push_back(o);
        }
        for (int k = 0; k < ed::StepTypeCount * 4; k++) {
            auto o = std::make_shared<ed::DrawObj>();
            o->type = ed::ObjType::Step; o->style.stepShape = k % 4; o->style.stepType = k / 4; o->stepValue = k + 1;
            o->p1 = o->p2 = { 330.0f + (k % 5) * 50, 200.0f + (k / 5) * 45 };
            v->st.objs.push_back(o);
        }
        auto mg = std::make_shared<ed::DrawObj>();
        mg->type = ed::ObjType::Magnify; mg->style.magnifyLink = true; mg->p1 = { 450, 300 }; mg->p2 = { 560, 390 }; mg->tail = { 380, 340 };
        v->st.objs.push_back(mg);
        auto hl = std::make_shared<ed::DrawObj>();
        hl->type = ed::ObjType::Highlighter; hl->style.stroke = 0xFFFFEB3B; hl->style.opacity = 50; hl->p1 = { 10, 360 }; hl->p2 = { 300, 390 };
        v->st.objs.push_back(hl);
        Image vr = v->Render();
        if (const wchar_t* dump = _wgetenv(L"SNAPNAB_DUMP")) {
            SaveImageFile(std::wstring(dump) + L"\\variacoes.png", vr);
            Image ic(96, 96, 0xFFFFFFFF);                 // enlarged Steps icon, to check the drawing
            { auto bmp = ic.WrapGdip(); Gdiplus::Graphics gi(bmp.get()); icons::Draw(gi, icons::Step, Gdiplus::RectF(0, 0, 96, 96), Gdiplus::Color(255, 56, 56, 62)); }
            SaveImageFile(std::wstring(dump) + L"\\icone_passos.png", ic);
            Image fi(150, 96, 0xFFFFFFFF);                // Fill enlarged and at the toolbar sizes (24 and 20 px)
            {
                auto bmp = fi.WrapGdip(); Gdiplus::Graphics gi(bmp.get());
                Gdiplus::Color ink(255, 56, 56, 62);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(0, 0, 96, 96), ink);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(108, 20, 24, 24), ink);
                icons::Draw(gi, icons::Fill, Gdiplus::RectF(110, 60, 20, 20), ink);
            }
            SaveImageFile(std::wstring(dump) + L"\\icone_preencher.png", fi);
        }
        uint32_t under = vr.at(150, 375), base = v->st.base->at(150, 375);
        CHECK(vr.w == 600 && under != base && CA(under) == 255, "%zu object variations rendered; translucent rectangular highlighter", v->st.objs.size());
        {   // group (CPaintOpGroup): renders the children and comes back unchanged from the .snap
            auto gd = ed::Document::FromImage(Pattern(300, 200, 5), L"g");
            auto grp = std::make_shared<ed::DrawObj>();
            grp->type = ed::ObjType::Group;
            ed::DrawObj a; a.type = ed::ObjType::Shape; a.p1 = { 10, 10 }; a.p2 = { 80, 60 };
            ed::DrawObj b2; b2.type = ed::ObjType::Arrow; b2.p1 = { 100, 150 }; b2.p2 = { 200, 40 };
            grp->children = { a, b2 };
            grp->UpdateGroupRect();
            gd->st.objs.push_back(grp);
            gd->path = Tmp(L"group.snap");
            bool saved = gd->Save();
            auto back = ed::Document::Load(gd->path);
            bool ok = saved && back && back->st.objs.size() == 1 && back->st.objs[0]->type == ed::ObjType::Group &&
                      back->st.objs[0]->children.size() == 2 && back->st.objs[0]->children[1].type == ed::ObjType::Arrow;
            Image gr = back ? back->Render() : Image();
            CHECK(ok && gr.px != gd->st.base->px && back->st.objs[0]->HitTest({ 150, 95 }, 3), "group with 2 objects written, read and drawn");
            DeleteFileW(gd->path.c_str());
        }
        int previews = 0;
        for (int t = 0; t < (int)ed::Tool::Count; t++)
            for (auto& st : ed::GenerateStyles((ed::Tool)t, { 0xFFEF4136, 0xFF6A2C91, 0xFF15A589, 0xFFF7A823 }))
                previews += ed::QuickStylePreview((ed::Tool)t, st, 48).w == 48;
        CHECK(previews > 100, "%d quick style thumbnails", previews);
        std::wstring pdf = Tmp(L"out.pdf"), err;
        bool ok = ed::SavePdf(pdf, r, err);
        CHECK(ok && FileSize(pdf) > 1000, "PDF written (%llu bytes)", (unsigned long long)FileSize(pdf));
        DeleteFileW(pdf.c_str());
    }

    printf("[5] Animated GIF (LZW, delta frames, 4 and 8 bpp)\n");
    for (int bpp : { 8, 4 }) {
        std::wstring p = Tmp(L"anim.gif");
        GifWriter gw;
        gw.Open(p, 120, 90, 10, bpp, true, true);
        for (int i = 0; i < 6; i++) { Image fr = Pattern(120, 90, 0); fr.FillRect({ i * 15, 20, i * 15 + 20, 60 }, 0xFF00AA00); gw.AddFrame(fr); }
        bool closed = gw.Close();
        Image dec;
        bool decoded = LoadImageFile(p, dec);
        CHECK(closed && decoded && dec.w == 120 && dec.h == 90, "GIF %d bpp valid and decodable by WIC (%llu bytes)", bpp, (unsigned long long)FileSize(p));
        DeleteFileW(p.c_str());
    }

    printf("[6] Effects\n");
    {
        Image s = Pattern(80, 60, 5);
        CHECK(fx::Border(s, 5, 0xFF000000).w == 90, "border");
        CHECK(fx::Rotate90(s, true).w == 60, "rotate 90");
        Image t(50, 40, 0xFFFFFFFF); t.FillRect({ 10, 10, 30, 25 }, 0xFF123456);
        Image tr = fx::AutoTrim(t);
        CHECK(tr.w == 20 && tr.h == 15, "auto trim (%dx%d)", tr.w, tr.h);
        Image e = fx::EdgeEffect(s, fx::Edge::Torn, 10, fx::All, true);
        CHECK(!e.empty() && e.HasTransparency(), "torn edge with shadow");
        // all effects of the capture window's "Effects" menu at once
        fx::CaptureEffects all;
        all.border = all.filter = all.shadow = all.caption = all.replaceColor = all.scale = true;
        all.edge = all.trim = all.adjust = all.rotate = all.watermark = all.resolution = true;
        all.scalePercent = 50; all.rotateKind = 0; all.dpi = 300; all.filterKind = 1;
        Image fxAll = fx::ApplyCaptureEffects(s, all);
        CHECK(!fxAll.empty() && fxAll.dpi == 300.0f, "12 capture effects applied (%dx%d)", fxAll.w, fxAll.h);
        std::wstring dp = Tmp(L"dpi.png");
        Image back;
        CHECK(SaveImageFile(dp, fxAll) && LoadImageFile(dp, back) && back.w == fxAll.w, "PNG with stored resolution");
        DeleteFileW(dp.c_str());
    }

    printf("[7] Shared memory IPC (sender → receiver with ack)\n");
    {
        HANDLE fakeEditor = CreateMutexW(nullptr, FALSE, kEditorMutex);
        ipc::Notify editor, capture;
        std::atomic<int> got{ 0 }; std::wstring text;
        editor.Start(ipc::kEditor, [&](const ipc::Message& m) { if (m.code == ipc::OpenFile) text = m.text; got++; });
        capture.Start(ipc::kCapture, nullptr);
        bool s1 = capture.SendInt(ipc::kEditor, ipc::ShowEditor, 1, true);
        bool s2 = capture.SendString(ipc::kEditor, ipc::OpenFile, L"C:\\x\\captura.snap", true);
        for (int i = 0; i < 50 && got < 2; i++) Sleep(20);
        CHECK(s1 && s2 && got == 2 && text == L"C:\\x\\captura.snap", "2 messages delivered (int and string)");
        editor.Stop(); capture.Stop();
        CloseHandle(fakeEditor);
    }

    printf("[8] MP4 recording (H.264/AAC) and frame orientation\n");
    std::wstring mp4 = Tmp(L"rec.mp4");
    {
        // test window with a known vertical gradient, to check the orientation
        WNDCLASSW wc{}; wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_PAINT) {
                // left half: vertical gradient (orientation); right half: white (misaligned rows show up)
                PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
                for (int y = 0; y < r.bottom; y += 4) { RECT b{ 0, y, r.right / 2, y + 4 }; HBRUSH br = CreateSolidBrush(RGB(y * 255 / r.bottom, 40, 255 - y * 255 / r.bottom)); FillRect(dc, &b, br); DeleteObject(br); }
                RECT wr{ r.right / 2, 0, r.right, r.bottom }; FillRect(dc, &wr, (HBRUSH)GetStockObject(WHITE_BRUSH));
                EndPaint(h, &ps); return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"SnapnabTestWnd";
        RegisterClassW(&wc);
        // 854 px: width that is not a multiple of 16 (the decoder delivers padded rows)
        HWND w = CreateWindowExW(WS_EX_TOPMOST, L"SnapnabTestWnd", L"t", WS_POPUP | WS_VISIBLE, 100, 100, 854, 360, nullptr, nullptr, wc.hInstance, nullptr);
        UpdateWindow(w);
        MSG msg; for (int i = 0; i < 20; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(20); }
        Recorder rec;
        RecordSettings rs; rs.rect = { 100, 100, 954, 460 }; rs.fps = 15; rs.path = mp4; rs.hardware = false;
        rs.microphone = false; rs.systemAudio = true;
        rs.highlightCursor = true; rs.clickEffects = true; rs.keyMode = 1;      // overlays drawn in the video (C07)
        rs.leftClickColor = RGB(0, 255, 0);                                     // a color the test pattern does not have
        rs.crashSafe = true;                                                    // fragmented MP4 remuxed at the end (C14)
        bool started = rec.Start(rs, nullptr, 0);
        CHECK(started, "recorder started %s", started ? "" : WToUtf8(rec.lastError).c_str());
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        {   // a click on the test window (C07): it must leave a ripple in the video
            POINT old; GetCursorPos(&old);
            SetCursorPos(300, 200);
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_MOUSE;
            in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(2, in, sizeof(INPUT));
            Sleep(30);
            SetCursorPos(old.x, old.y);
        }
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        rec.Pause(); Sleep(500); rec.Resume();
        for (int i = 0; i < 30; i++) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(25); }
        double el = rec.Elapsed();
        bool stopped = rec.Stop();
        DestroyWindow(w);
        {
            // nothing of this recording is left in the recovery folder (C14)
            bool left = false;
            WIN32_FIND_DATAW fd;
            HANDLE fh = FindFirstFileW((Recorder::RecoveryDir() + L"\\*.target").c_str(), &fd);
            if (fh != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring tp = Recorder::RecoveryDir() + L"\\" + fd.cFileName;
                    HANDLE t = CreateFileW(tp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                    char b[2048]; DWORD n = 0;
                    if (t != INVALID_HANDLE_VALUE) { ReadFile(t, b, sizeof b, &n, nullptr); CloseHandle(t); }
                    if (Utf8ToW(std::string(b, n)) == mp4) left = true;
                } while (FindNextFileW(fh, &fd));
                FindClose(fh);
            }
            CHECK(!left, "crash-safe recording: the fragmented file was remuxed and removed from the recovery folder");
        }
        CHECK(stopped && FileSize(mp4) > 2000, "MP4 finalized (%llu bytes, %.2f s recorded, pause excluded)", (unsigned long long)FileSize(mp4), el);
        ed::VideoReader rd;
        Image f; LONGLONG ts;
        bool opened = rd.Open(mp4) && rd.ReadFrame(f, ts);
        CHECK(opened && rd.width == 854 && rd.height == 360, "MP4 decodable (%dx%d, %.2f s, %.1f fps)", rd.width, rd.height, rd.duration / 1e7, rd.fps);
        if (opened) {
            // the top must be blue (high B, low R); the bottom red
            uint32_t top = f.at(200, 10), bot = f.at(200, 350);
            CHECK(CB(top) > CR(top) && CR(bot) > CB(bot), "correct orientation (top B=%d R=%d, bottom R=%d B=%d)", CB(top), CR(top), CR(bot), CB(bot));
            // aligned rows: the gradient | white boundary stays in the middle of the image at every height
            bool aligned = true;
            for (int y : { 20, 180, 340 }) aligned &= CG(f.at(400, y)) < 120 && CG(f.at(460, y)) > 200;
            CHECK(aligned, "frame rows aligned with a width that is not a multiple of 16 (y=340: left G=%d, right G=%d)", CG(f.at(400, 340)), CG(f.at(460, 340)));
            CHECK(rd.duration > 1.0e7 && rd.duration < 3.5e7, "duration consistent with the recorded time without the pause (%.2f s)", rd.duration / 1e7);
            // the click ripple (green here) shows up in some frame around the click point (200, 100 in the video)
            int ripple = 0;
            Image fr2; LONGLONG ts2;
            ed::VideoReader rd2;
            if (rd2.Open(mp4))
                while (rd2.ReadFrame(fr2, ts2) && !ripple)
                    for (int y = 40; y < 160 && y < fr2.h; y++)
                        for (int x = 140; x < 260 && x < fr2.w; x++) { uint32_t p = fr2.at(x, y); if (CG(p) > 150 && CR(p) < 90 && CB(p) < 90) ripple++; }
            CHECK(ripple > 0, "click ripple drawn in the video (%d green pixels)", ripple);
        }
    }

    printf("[9] Video cutting (ToFileWithCuts) and GIF from video\n");
    {
        std::vector<ed::Interval> cuts = { { 3000000, 8000000 } };    // removes 0.3 s – 0.8 s
        CHECK(ed::PrecedingCutsDuration(cuts, 9000000) == 5000000 && ed::IsInsideAnyCut(cuts, 5000000) && !ed::IsInsideAnyCut(cuts, 9000000),
              "PrecedingCutsDuration / IsInsideAnyCut");
        std::wstring out = Tmp(L"cut.mp4"), err;
        bool ok = ed::TranscodeWithCuts(mp4, out, cuts, nullptr, err);
        ed::VideoReader a, b;
        a.Open(mp4); b.Open(out);
        double diff = (a.duration - b.duration) / 1e7;
        CHECK(ok && std::fabs(diff - 0.5) < 0.2, "cut MP4 is %.2f s shorter (expected 0.5) %s", diff, WToUtf8(err).c_str());
        a.Close(); b.Close();
        std::wstring gif = Tmp(L"from_video.gif");
        bool gok = ed::CreateGifFromVideo(mp4, gif, 0, 0, 8, 320, 8, true, true, nullptr, err);
        Image g;
        CHECK(gok && LoadImageFile(gif, g) && g.w == 320, "GIF from video (%llu bytes)", (unsigned long long)FileSize(gif));
        DeleteFileW(out.c_str()); DeleteFileW(gif.c_str());
        if (!ok) CopyFileW(mp4.c_str(), Tmp(L"failed.mp4").c_str(), FALSE);
    }
    DeleteFileW(mp4.c_str());

    printf("[10] OCR (Windows.Media.Ocr)\n");
    {
        Image img(600, 120, 0xFFFFFFFF);
        auto bmp = img.WrapGdip();
        { Gdiplus::Graphics gr(bmp.get()); gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
          Gdiplus::FontFamily ff(L"Segoe UI"); Gdiplus::Font fnt(&ff, 48, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
          Gdiplus::SolidBrush br(Gdiplus::Color(255, 0, 0, 0)); gr.DrawString(L"Snapnab OCR 2026", -1, &fnt, Gdiplus::PointF(20, 25), &br); }
        std::wstring text, err;
        bool ok = RunOcr(img, text, err);
        bool match = text.find(L"Snapnab") != std::wstring::npos || text.find(L"OCR") != std::wstring::npos;
        if (!ok) printf("  info OCR unavailable: %s\n", WToUtf8(err).c_str());
        else CHECK(match, "recognized text: \"%s\"", WToUtf8(Trim(text)).c_str());
        // the engine drops a line holding "1111"; the second pass reads it word by word (the test card for C11)
        // (200 px tall: the image is enlarged for the engine; 520 px: it is not, and the pieces are enlarged on their own)
        for (int cardH : { 200, 520 }) if (ok) {
            Image card(900, cardH, 0xFFFFFFFF);
            auto cb = card.WrapGdip();
            { Gdiplus::Graphics gr(cb.get()); gr.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
              Gdiplus::FontFamily ff(L"Segoe UI"); Gdiplus::Font fnt(&ff, 27, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
              Gdiplus::SolidBrush br(Gdiplus::Color(255, 0, 0, 0));
              gr.DrawString(L"Telefone: +55 (11) 98765-4321", -1, &fnt, Gdiplus::PointF(30, 30), &br);
              gr.DrawString(L"Cartao: 4111 1111 1111 1111", -1, &fnt, Gdiplus::PointF(30, 90), &br); }
            std::vector<OcrLine> lines;
            RunOcrLines(card, lines, err);
            std::wstring found, all;
            for (auto& it : ed::FindSensitive(lines)) if (it.kind == Tr(L"Cartão")) found = it.text;
            for (auto& l : lines) all += L"[" + l.text + L"]";
            CHECK(DigitsOfTest(found) == L"4111111111111111", "card number with \"1111\" found by the second OCR pass (%d px tall image): \"%s\" in %s",
                  cardH, WToUtf8(found).c_str(), WToUtf8(all).c_str());
        }
    }

    printf("[11] Panoramic scrolling (frame stitching)\n");
    {
        auto noise = [](int w, int h) {
            Image img(w, h);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    uint32_t v = (uint32_t)(x * 73856093u ^ y * 19349663u);
                    v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
                    img.at(x, y) = 0xFF000000 | (v & 0xFFFFFF);
                }
            return img;
        };
        // vertical, with a move back up in the middle (the move back must not duplicate content)
        Image page = noise(300, 1000);
        PanoramaStitcher v;
        for (int y : { 0, 37, 90, 150, 240, 200, 300, 380, 470, 560, 640, 720, 800 }) v.Add(page.Crop({ 0, y, 300, y + 200 }));
        CHECK(v.axis() == 1 && v.result().w == 300 && v.result().h == 1000 && v.result().px == page.px,
              "vertical: %dx%d rebuilt identical to the page", v.result().w, v.result().h);
        // horizontal
        Image wide = noise(1200, 150);
        PanoramaStitcher hz;
        for (int x = 0; x <= 900; x += 60) hz.Add(wide.Crop({ x, 0, x + 300, 150 }));
        CHECK(hz.axis() == 2 && hz.result().w == 1200 && hz.result().px == wide.px, "horizontal: %dx%d", hz.result().w, hz.result().h);
        // a repeated frame (nothing scrolled) adds nothing
        PanoramaStitcher same;
        same.Add(page.Crop({ 0, 0, 300, 200 }));
        bool added = same.Add(page.Crop({ 0, 0, 300, 200 }));
        CHECK(!added && same.result().h == 200, "an unchanged frame is ignored");
    }

    printf("[12] Webcam chroma key\n");
    {
        COLORREF green = RGB(0, 177, 64);
        uint8_t bg = ChromaAlpha(0xFF00B140, green, 40), bgDark = ChromaAlpha(0xFF00702A, green, 40);
        uint8_t skin = ChromaAlpha(0xFFE0AC90, green, 40), shirt = ChromaAlpha(0xFF2050C0, green, 40);
        CHECK(bg == 0 && bgDark == 0, "green background (lit and in shadow) removed (alpha %d, %d)", bg, bgDark);
        CHECK(skin == 255 && shirt == 255, "skin and blue shirt kept (alpha %d, %d)", skin, shirt);
    }

    printf("[14] Editor batch: shapes, gradients, labels, layers, clipboard, PDF links\n");
    {
        using ed::DrawObj; using ed::ObjType;
        auto solid = [](int w, int h, uint32_t c) { return Image(w, h, c); };
        auto render1 = [&](const Image& base, std::function<void(DrawObj&)> make) {
            auto d = ed::Document::FromImage(base, L"b");
            auto o = std::make_shared<DrawObj>();
            make(*o);
            d->st.objs.push_back(o);
            return d->Render();
        };
        auto similar = [](uint32_t a, uint32_t b, int tol) {
            return std::abs(CR(a) - CR(b)) <= tol && std::abs(CG(a) - CG(b)) <= tol && std::abs(CB(a) - CB(b)) <= tol;
        };
        // highlighter as an ellipse: center tinted, corner of the box untouched
        Image white = solid(200, 160, 0xFFFFFFFF);
        Image he = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFFFF0000; o.style.opacity = 100;
                                                     o.style.shadow = false; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(he.at(100, 80), 0xFFFF0000, 10) && similar(he.at(24, 24), 0xFFFFFFFF, 2), "highlighter ellipse: fills the ellipse, not the box corner");
        // polygon highlighter (clicked corners): inside tinted, outside untouched
        Image hp = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFF0000FF; o.style.shadow = false;
                                                     o.style.areaShape = ed::AreaPolygon; o.pts = { { 20, 20 }, { 180, 20 }, { 20, 140 } }; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(hp.at(40, 40), 0xFF0000FF, 10) && similar(hp.at(170, 130), 0xFFFFFFFF, 2), "polygon highlighter (triangle) drawn only inside");
        // blur in an ellipse: inside changes, outside the ellipse stays as it was
        Image pat = Pattern(200, 160, 9);
        Image be = render1(pat, [](DrawObj& o) { o.type = ObjType::Blur; o.style.amount = 12; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(be.at(100, 80) != pat.at(100, 80) && be.at(22, 22) == pat.at(22, 22), "blur in an ellipse: corner of the box unchanged");
        // freehand pixelate keeps the outside
        Image pf = render1(pat, [](DrawObj& o) { o.type = ObjType::Pixelate; o.style.amount = 10; o.style.areaShape = ed::AreaFreehand;
                                                   o.pts = { { 60, 30 }, { 140, 40 }, { 150, 120 }, { 70, 130 }, { 40, 80 } }; o.p1 = { 40, 30 }; o.p2 = { 150, 130 }; });
        CHECK(pf.at(5, 5) == pat.at(5, 5) && pf.at(100, 80) != pat.at(100, 80), "freehand pixelate: only inside the outline");
        // round spotlight: inside as is, outside darker
        Image sp = render1(white, [](DrawObj& o) { o.type = ObjType::Spotlight; o.style.amount = 8; o.style.areaShape = ed::ShapeEllipse; o.p1 = { 40, 30 }; o.p2 = { 160, 130 }; });
        CHECK(similar(sp.at(100, 80), 0xFFFFFFFF, 2) && CR(sp.at(42, 32)) < 230, "round spotlight darkens outside the ellipse (also the box corner)");
        // linear gradient fill (0° = left to right) in a shape
        Image gs = render1(white, [](DrawObj& o) { o.type = ObjType::Shape; o.style.width = 0; o.style.shadow = false; o.style.fill = 0xFFFF0000;
                                                     o.style.grad.type = 1; o.style.grad.angle = 0; o.style.grad.stops = { { 0, 0xFFFF0000 }, { 1, 0xFF0000FF } };
                                                     o.p1 = { 10, 10 }; o.p2 = { 190, 150 }; });
        CHECK(CR(gs.at(14, 80)) > 200 && CB(gs.at(14, 80)) < 60 && CB(gs.at(186, 80)) > 200 && CR(gs.at(186, 80)) < 60, "shape with a linear gradient (red → blue)");
        Image fillG = solid(100, 100, 0);
        ed::Gradient rg; rg.type = 2; rg.stops = { { 0, 0xFFFFFFFF }, { 1, 0xFF000000 } };
        ed::FillGradientMask(fillG, {}, { 0, 0, 100, 100 }, rg, 100);
        CHECK(CR(fillG.at(50, 50)) > 240 && CR(fillG.at(1, 1)) < 30, "radial gradient fill (Fill tool / canvas color): white center, dark corners");
        // step with an outline: the ring outside the circle takes the outline color
        Image so = render1(solid(120, 120, 0xFF000000), [](DrawObj& o) { o.type = ObjType::Step; o.style.shadow = false; o.style.stroke = 0xFF2060FF;
                                                                            o.style.fontSize = 40; o.style.stepOutline = 0xFFFFFFFF; o.style.stepOutlineWidth = 6; o.p1 = o.p2 = { 60, 60 }; });
        CHECK(similar(so.at(60, 60 - 29), 0xFFFFFFFF, 40), "step with a white outline (contrast on a dark background)");
        // line spacing: double spacing makes the text block taller
        DrawObj tx; tx.type = ObjType::Text; tx.text = L"one\ntwo\nthree"; tx.style.fontSize = 20;
        float h1 = ed::TextBlockHeight(tx, 300);
        tx.style.lineSpacing = 2.0f;
        float h2 = ed::TextBlockHeight(tx, 300);
        CHECK(h2 > h1 * 1.6f, "line spacing 2× (%.0f px → %.0f px)", h1, h2);
        // arrow label at the tail: drawn beyond the tail, part of the bounds and of the hit test
        DrawObj ar; ar.type = ObjType::Arrow; ar.p1 = { 150, 80 }; ar.p2 = { 280, 80 }; ar.text = L"Label"; ar.style.fontSize = 24; ar.style.shadow = false;
        Gdiplus::RectF lb = ed::ArrowLabelRect(ar);
        CHECK(lb.GetRight() <= 150 && ar.Bounds().X <= lb.X + 1 && ar.HitTest({ lb.X + lb.Width / 2, lb.Y + lb.Height / 2 }, 2), "arrow label beyond the tail (bounds and hit test)");
        Image al = render1(solid(300, 160, 0xFFFFFFFF), [&](DrawObj& o) { o = ar; });
        int inked = 0;
        for (int y = (int)lb.Y; y < (int)lb.GetBottom(); y++) for (int x = (int)lb.X; x < (int)lb.GetRight(); x++) inked += !similar(al.at(x, y), 0xFFFFFFFF, 30);
        CHECK(inked > 30, "arrow label text drawn (%d pixels)", inked);
        // frosted callout: what is behind the body gets blurred even with a transparent fill
        Image fc = render1(pat, [](DrawObj& o) { o.type = ObjType::Callout; o.style.calloutShape = 16; o.style.fill = 0; o.style.width = 0; o.style.shadow = false;
                                                   o.style.bgBlur = true; o.style.bgBlurAmount = 10; o.p1 = { 40, 30 }; o.p2 = { 160, 130 }; });
        CHECK(fc.at(100, 80) != pat.at(100, 80) && fc.at(5, 5) == pat.at(5, 5), "callout background blur (frosted glass)");
        // loupe with a handle: bounds include the handle, which is drawn outside the lens
        DrawObj lp; lp.type = ObjType::Magnify; lp.style.loupe = true; lp.style.loupeHandle = true; lp.style.stroke = 0xFF303030; lp.style.shadow = false;
        lp.p1 = { 20, 20 }; lp.p2 = { 100, 100 };
        Gdiplus::RectF lpb = lp.Bounds();
        Image lr = render1(solid(200, 200, 0xFFFFFFFF), [&](DrawObj& o) { o = lp; });
        CHECK(lpb.GetRight() > 120 && !similar(lr.at(105, 105), 0xFFFFFFFF, 30), "loupe: handle outside the lens drawn and inside the bounds");
        // label stamp: wide badge with the text
        float asp = ed::StampAspect(L"label:2E9E5B:Depois");
        Image ls = ed::StampImage(L"label:2E9E5B:Depois", 96);
        CHECK(asp > 1.5f && !ls.empty() && similar(ls.at(6, ls.h * 3 / 4), 0xFF2E9E5B, 50), "label stamp (aspect %.1f)", asp);
        // hidden objects are not rendered nor exported; locked/name/visibility, guides and links survive the .snap
        auto d = ed::Document::FromImage(solid(120, 90, 0xFFFFFFFF), L"l");
        auto hid = std::make_shared<DrawObj>(); hid->type = ObjType::Shape; hid->style.fill = 0xFF000000; hid->style.shadow = false; hid->p1 = { 10, 10 }; hid->p2 = { 60, 60 };
        hid->hidden = true; hid->locked = true; hid->name = L"Fundo azul ç";
        auto lk = std::make_shared<DrawObj>(); lk->type = ObjType::Link; lk->text = L"https://example.com/a b"; lk->p1 = { 70, 10 }; lk->p2 = { 110, 30 };
        auto pt = std::make_shared<DrawObj>(); pt->type = ObjType::Highlighter; pt->style.areaShape = ed::AreaPolygon; pt->pts = { { 5, 70 }, { 40, 60 }, { 30, 85 } };
        pt->p1 = { 5, 60 }; pt->p2 = { 40, 85 }; pt->floating = true; pt->style.lineSpacing = 1.5f;
        auto st = std::make_shared<DrawObj>(); st->type = ObjType::Step; st->style.stepOutlineWidth = 4; st->style.stepOutline = 0xFF112233; st->p1 = st->p2 = { 90, 70 };
        auto sh = std::make_shared<DrawObj>(); sh->type = ObjType::Shape; sh->style.grad.type = 2; sh->style.grad.stops = { { 0, 0xFFFFFFFF }, { 0.5f, 0xFF00FF00 }, { 1, 0xFF000000 } };
        sh->p1 = { 0, 0 }; sh->p2 = { 5, 5 };
        auto co = std::make_shared<DrawObj>(); co->type = ObjType::Callout; co->style.bgBlur = true; co->style.bgBlurAmount = 13; co->style.lineSpacing = 2.5f; co->text = L"x";
        co->p1 = { 0, 0 }; co->p2 = { 10, 10 };
        auto mg2 = std::make_shared<DrawObj>(); mg2->type = ObjType::Magnify; mg2->style.loupe = true; mg2->style.loupeHandle = false; mg2->style.magnifyShape = ed::MagStar;
        auto arw = std::make_shared<DrawObj>(); arw->type = ObjType::Arrow; arw->text = L"Olá"; arw->style.fontSize = 31; arw->p1 = { 1, 1 }; arw->p2 = { 50, 1 };
        d->st.objs = { hid, lk, pt, st, sh, co, mg2, arw };
        d->guidesX = { 12.5f }; d->guidesY = { 40 };
        Image hr = d->Render();
        CHECK(similar(hr.at(30, 30), 0xFFFFFFFF, 2) && similar(hr.at(80, 15), 0xFFFFFFFF, 2), "hidden object and link areas are not drawn");
        d->path = Tmp(L"batch.snap");
        bool saved = d->Save();
        auto back = ed::Document::Load(d->path);
        bool ok = saved && back && back->st.objs.size() == 8;
        if (ok) {
            auto& o = back->st.objs;
            ok = o[0]->hidden && o[0]->locked && o[0]->name == L"Fundo azul ç" && o[1]->type == ObjType::Link && o[1]->text == L"https://example.com/a b" &&
                 o[2]->style.areaShape == ed::AreaPolygon && o[2]->pts.size() == 3 && o[2]->floating && o[3]->style.stepOutlineWidth == 4 &&
                 o[3]->style.stepOutline == 0xFF112233 && o[4]->style.grad.type == 2 && o[4]->style.grad.stops.size() == 3 &&
                 o[4]->style.grad.stops[1].color == 0xFF00FF00 && o[5]->style.bgBlur && o[5]->style.bgBlurAmount == 13 &&
                 std::fabs(o[5]->style.lineSpacing - 2.5f) < 0.01f && o[6]->style.loupe && !o[6]->style.loupeHandle && o[6]->style.magnifyShape == ed::MagStar &&
                 o[7]->text == L"Olá" && o[7]->style.fontSize == 31 && back->guidesX.size() == 1 && back->guidesY.size() == 1 && back->guidesY[0] == 40;
        }
        CHECK(ok, ".snap round trip of the new properties (name, hidden, locked, link, polygon, outline, gradient, blur, spacing, loupe, label, guides)");
        DeleteFileW(d->path.c_str());
        // objects written by older versions: the new properties take their defaults
        XmlNode old("Rect");
        old.setd("X1", 1).setd("Y1", 2).setd("X2", 30).setd("Y2", 40).set("PenColor", "#FFFF0000").seti("Opacity", 100);
        SnapFrame dummy;
        auto oo = ed::ObjFromXml(old, dummy);
        XmlNode oldHl("Highlight");
        oldHl.setd("X1", 1).setd("Y1", 2).setd("X2", 30).setd("Y2", 40).set("Points", "1.0,2.0 10.0,20.0 30.0,40.0 ");
        auto oh = ed::ObjFromXml(oldHl, dummy);
        CHECK(oo && !oo->hidden && !oo->locked && oo->name.empty() && !oo->style.grad.Active() && oo->style.lineSpacing == 1.0f &&
              oo->style.areaShape == ed::ShapeRect && oo->style.stepOutlineWidth == 0 && !oo->style.bgBlur && !oo->style.loupe &&
              oh && oh->IsPath() && !oh->IsPointShape(), "old objects open with the defaults (old freehand highlighter still a path)");
        // quick styles keep the new properties
        ed::Style qs; qs.lineSpacing = 1.8f; qs.stepOutlineWidth = 2; qs.areaShape = ed::ShapeRoundRect; qs.loupe = true; qs.bgBlur = true;
        qs.grad.type = 1; qs.grad.angle = 45; qs.grad.stops = { { 0, 0xFF000000 }, { 1, 0xFFFFFFFF } };
        ed::Style qb = ed::StyleFromXml(ed::StyleToXml(qs));
        CHECK(ed::SameStyle(qs, qb) && qb.areaShape == ed::ShapeRoundRect && qb.grad.angle == 45, "quick style round trip with spacing, outline, area shape, loupe, blur and gradient");
        // clipboard: objects (with an embedded image) copied in one image and pasted in another
        DrawObj pic; pic.type = ObjType::Picture; pic.picture = std::make_shared<Image>(Pattern(12, 9, 4)); pic.p1 = { 33, 44 }; pic.p2 = { 45, 53 };
        DrawObj tt; tt.type = ObjType::Text; tt.text = L"Copiado"; tt.p1 = { 5, 6 }; tt.p2 = { 100, 40 };
        std::wstring src;
        auto clip = ed::ObjectsFromClip(ed::ObjectsToClip({ pic, tt }, L"C:\\doc.snap"), &src);
        CHECK(clip.size() == 2 && src == L"C:\\doc.snap" && clip[0].picture && clip[0].picture->px == pic.picture->px && clip[0].p1.X == 33 &&
              clip[1].text == L"Copiado", "objects on the clipboard: positions, text and embedded image preserved");
        // source formatting of copied text (paste keeping the formatting)
        ed::Style rf;
        bool rtf = ed::ParseRtfFormat("{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Arial;}{\\f1\\fmodern Consolas;}}{\\colortbl;\\red255\\green0\\blue0;}"
                                      "\\pard\\f1\\fs32\\b\\cf1 Hello}", rf);
        CHECK(rtf && rf.font == L"Consolas" && std::fabs(rf.fontSize - 21.33f) < 0.1f && rf.bold && rf.textColor == 0xFFFF0000, "RTF: font, size, bold and color of the first run");
        ed::Style hf;
        bool html = ed::ParseHtmlFormat("Version:0.9\r\n<html><body><!--StartFragment--><span style=\"font-family: 'Fira Code', monospace; font-size: 14px; color: #569cd6\"><i>int</i></span><!--EndFragment--></body></html>", hf);
        CHECK(html && hf.font == L"Fira Code" && hf.fontSize == 14 && hf.textColor == 0xFF569CD6 && hf.italic, "HTML: font, size, color and italic of the first text");
        // PDF: the captured links become /Link annotations with the address
        std::wstring lpdf = Tmp(L"links.pdf"), lerr;
        bool pok = ed::SavePdf(lpdf, solid(200, 100, 0xFFFFFFFF), lerr, { { Gdiplus::RectF(10, 10, 50, 20), L"https://example.com/x(1)" } });
        std::string body;
        { HANDLE fh = CreateFileW(lpdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
          if (fh != INVALID_HANDLE_VALUE) { DWORD n = GetFileSize(fh, nullptr), rd = 0; body.resize(n); ReadFile(fh, body.data(), n, &rd, nullptr); CloseHandle(fh); } }
        CHECK(pok && body.find("/Subtype /Link") != std::string::npos && body.find("(https://example.com/x\\(1\\))") != std::string::npos, "PDF with a clickable link");
        DeleteFileW(lpdf.c_str());
        // the new icons draw something
        int drawn = 0;
        for (auto id : { icons::Lock, icons::Unlock, icons::Eye, icons::EyeOff, icons::Pin, icons::AlignLeft, icons::AlignBottom, icons::DistributeH,
                         icons::DistributeV, icons::Ruler, icons::Link, icons::Group, icons::Sliders }) {
            Image ic(24, 24, 0);
            { auto bmp = ic.WrapGdip(); Gdiplus::Graphics gi(bmp.get()); icons::Draw(gi, id, Gdiplus::RectF(0, 0, 24, 24), Gdiplus::Color(255, 0, 0, 0)); }
            drawn += std::count_if(ic.px.begin(), ic.px.end(), [](uint32_t p) { return CA(p) > 0; }) > 20;
        }
        CHECK(drawn == 13, "new icons (lock, eye, pin, align, distribute, ruler, link, group, volume): %d of 13", drawn);
        // a polygon area whose corners do not exist yet (shape just switched) is drawn as its box, not skipped
        Image hn = render1(white, [](DrawObj& o) { o.type = ObjType::Highlighter; o.style.stroke = 0xFFFF0000; o.style.opacity = 100;
                                                     o.style.shadow = false; o.style.areaShape = ed::AreaPolygon; o.p1 = { 20, 20 }; o.p2 = { 180, 140 }; });
        CHECK(similar(hn.at(30, 30), 0xFFFF0000, 10), "polygon highlighter without corners drawn as its box");
        // settings import with merge: the current items stay, the missing presets, favorites and pinned stamps are added
        {
            std::string cur = "<SavedPresets Version=\"1\"><Presets><CaptureSettings DisplayName=\"A\" SelectionMode=\"0\"/></Presets></SavedPresets>";
            std::string imp = "<SavedPresets Version=\"1\"><Presets><CaptureSettings DisplayName=\"A\" SelectionMode=\"2\"/>"
                              "<CaptureSettings DisplayName=\"B\" SelectionMode=\"1\"/></Presets></SavedPresets>";
            XmlNode m; XmlParse(settings::MergeXml(cur, imp), m);
            const XmlNode* ps = m.child("Presets");
            CHECK(ps && ps->children.size() == 2 && ps->children[0].get("SelectionMode") == "0" && ps->children[1].get("DisplayName") == "B",
                  "settings merge: preset B added inside <Presets>, preset A keeps the current values");
            std::string fcur = "<Favorites><Favorite ToolMode=\"1\"><Style Foreground=\"#FF000000\"/></Favorite></Favorites>";
            std::string fimp = "<Favorites><Favorite ToolMode=\"1\"><Style Foreground=\"#FF000000\"/></Favorite>"
                               "<Favorite ToolMode=\"2\"><Style Foreground=\"#FFFF0000\"/></Favorite></Favorites>";
            XmlNode f; XmlParse(settings::MergeXml(fcur, fimp), f);
            std::string scur = "<StampOrder><Pinned><Item Id=\"a\"/></Pinned></StampOrder>";
            std::string simp = "<StampOrder><Category Key=\"x\"><Item Id=\"b\"/></Category><Pinned><Item Id=\"a\"/><Item Id=\"c\"/></Pinned></StampOrder>";
            XmlNode s; XmlParse(settings::MergeXml(scur, simp), s);
            const XmlNode* pin = s.child("Pinned");
            CHECK(f.children.size() == 2 && s.children.size() == 2 && pin && pin->children.size() == 2,
                  "settings merge: new favorite added once, stamp category and pinned stamp added");
        }
        if (const wchar_t* dump = _wgetenv(L"SNAPNAB_DUMP")) {
            SaveImageFile(std::wstring(dump) + L"\\lote_formas.png", gs);
            SaveImageFile(std::wstring(dump) + L"\\lote_lupa.png", lr);
            SaveImageFile(std::wstring(dump) + L"\\lote_rotulo.png", ls);
            SaveImageFile(std::wstring(dump) + L"\\lote_seta.png", al);
            SaveImageFile(std::wstring(dump) + L"\\lote_passo.png", so);
        }
    }

    printf("[15] Competitor batch: formats, metadata, capture and editor tools\n");
    {
        // C03: lossless WebP keeps every visible pixel; lossy WebP decodes to the same size
        Image src(64, 48, 0);
        for (int y = 0; y < src.h; y++)
            for (int x = 0; x < src.w; x++) src.at(x, y) = ARGB(x < 8 ? 0 : 255 - y, x * 4, y * 5, (x + y) * 2);
        SaveOptions lo; lo.webpLossless = true; lo.stripMetadata = true;
        std::vector<BYTE> wl, wq;
        Image back;
        bool encL = EncodeToMemoryEx(src, ImageFormat::WEBP, wl, lo) && DecodeFromMemory(wl.data(), wl.size(), back);
        bool same = encL && back.w == src.w && back.h == src.h;
        for (size_t i = 0; same && i < src.px.size(); i++)
            if (CA(src.px[i]) && src.px[i] != back.px[i]) same = false;
        CHECK(same, "WebP lossless round trip (%zu bytes), alpha and colors kept", wl.size());
        SaveOptions qo = lo; qo.webpLossless = false; qo.webpQuality = 60;
        Image backQ;
        bool encQ = EncodeToMemoryEx(src, ImageFormat::WEBP, wq, qo) && DecodeFromMemory(wq.data(), wq.size(), backQ);
        CHECK(encQ && backQ.w == 64 && backQ.h == 48 && wq.size() > 0, "WebP lossy (quality 60): %zu bytes", wq.size());
        std::wstring wpath = TestTempDir() + L"\\snapnab_test.webp";
        bool wfile = SaveImageFile(wpath, src) && LoadImageFile(wpath, back) && back.w == 64;
        CHECK(wfile && FormatFromExt(L".WEBP") == ImageFormat::WEBP && FormatFromExt(L".heic") == ImageFormat::HEIC,
              "WebP file saved and opened by extension");
        DeleteFileW(wpath.c_str());
        if (CanEncode(ImageFormat::AVIF)) {
            std::vector<BYTE> av; Image ab;
            bool ok = EncodeToMemoryEx(src, ImageFormat::AVIF, av, qo) && DecodeFromMemory(av.data(), av.size(), ab);
            CHECK(ok && ab.w == 64 && ab.h == 48, "AVIF round trip through the Windows codec (%zu bytes)", av.size());
        } else printf("  skip AVIF: no AV1 encoder on this computer\n");
        // C29: metadata written when kept (camera, date, GPS) and absent when stripped
        Image photo = Pattern(80, 60, 3);
        photo.meta = std::make_shared<ImageMeta>();
        photo.meta->make = L"TestCam"; photo.meta->model = L"Model X"; photo.meta->dateTaken = L"2024:05:06 07:08:09";
        photo.meta->gps = true; photo.meta->lat = -23.5; photo.meta->lon = -46.625;
        std::wstring jp = TestTempDir() + L"\\snapnab_meta.jpg";
        SaveOptions keep; keep.stripMetadata = false;
        Image rj;
        bool mk = SaveImageFileEx(jp, photo, keep) && LoadImageFile(jp, rj) && rj.meta;
        CHECK(mk && rj.meta->make == L"TestCam" && rj.meta->model == L"Model X" && rj.meta->dateTaken.substr(0, 10) == L"2024:05:06" &&
              rj.meta->gps && std::fabs(rj.meta->lat + 23.5) < 0.01 && std::fabs(rj.meta->lon + 46.625) < 0.01,
              "metadata kept in JPEG: camera, date and GPS (%ls | %ls | %ls | %ls | %d %.3f %.3f)", mk ? rj.meta->software.c_str() : L"-", mk ? rj.meta->make.c_str() : L"-", mk ? rj.meta->model.c_str() : L"-", mk ? rj.meta->dateTaken.c_str() : L"-", mk ? (int)rj.meta->gps : -1, mk ? rj.meta->lat : 0.0, mk ? rj.meta->lon : 0.0);
        SaveOptions strip; strip.stripMetadata = true;
        Image rs;
        bool st = SaveImageFileEx(jp, photo, strip) && LoadImageFile(jp, rs);
        CHECK(st && (!rs.meta || (rs.meta->make.empty() && !rs.meta->gps && rs.meta->software.empty())), "metadata removed when stripping");
        DeleteFileW(jp.c_str());
        // [15] next
    }

    printf("[16] Competitor batch: editor tools, canvas, outputs\n");
    {
        using namespace snap::ed;
        // C11: sensitive data patterns
        CHECK(SensitiveKind(L"maria.silva@example.com.br") == Tr(L"E-mail"), "e-mail recognized");
        CHECK(SensitiveKind(L"4111 1111 1111 1111") == Tr(L"Cartão") && SensitiveKind(L"4111 1111 1111 1112").empty(), "card number with Luhn check");
        CHECK(SensitiveKind(L"529.982.247-25") == L"CPF" && SensitiveKind(L"111.111.111-11").empty(), "CPF with check digits");
        CHECK(SensitiveKind(L"GB82 WEST 1234 5698 7654 32") == L"IBAN", "IBAN with mod 97");
        CHECK(SensitiveKind(L"192.168.10.254") == Tr(L"Endereço IP") && SensitiveKind(L"+55 (11) 98765-4321") == Tr(L"Telefone"), "IP address and phone");
        CHECK(SensitiveKind(L"https://example.com/reset?token=abc123XYZ") == Tr(L"Link com token") &&
              SensitiveKind(L"sk-abcdefghijklmnop1234567890") == Tr(L"Chave de API"), "URL with a token and an API key");
        CHECK(SensitiveKind(L"Relatório anual").empty() && SensitiveKind(L"2026-10-31").empty(), "plain text and dates are not sensitive");
        {
            OcrLine ln;
            auto word = [&](const wchar_t* t, LONG x0, LONG x1) { OcrWord w; w.text = t; w.box = { x0, 10, x1, 30 }; ln.words.push_back(w); };
            word(L"Fone:", 0, 40); word(L"(11)", 50, 80); word(L"98765-4321", 85, 170); word(L"ok", 180, 200);
            auto items = FindSensitive({ ln });
            CHECK(items.size() == 1 && items[0].box.left <= 50 && items[0].box.right >= 170 && items[0].box.left > 40,
                  "phone spread over OCR words becomes one box over those words (%zu item(s))", items.size());
        }
        // C09: combine side by side / stacked / grid
        Image a(100, 50, 0xFFFF0000), b(60, 80, 0xFF00FF00), c(40, 40, 0xFF0000FF);
        CombineOptions co; co.gap = 10; co.margin = 5;
        co.layout = 0; Image h = CombineImages({ a, b, c }, co);
        co.layout = 1; Image v = CombineImages({ a, b, c }, co);
        co.layout = 2; co.columns = 2; Image gr = CombineImages({ a, b, c }, co);
        CHECK(h.w == 5 + 100 + 10 + 60 + 10 + 40 + 5 && h.h == 5 + 80 + 5 && v.w == 5 + 100 + 5 && v.h == 5 + 50 + 10 + 80 + 10 + 40 + 5,
              "combine side by side %dx%d and stacked %dx%d", h.w, h.h, v.w, v.h);
        CHECK(gr.w == 5 + 100 + 10 + 60 + 5 && gr.h == 5 + 80 + 10 + 40 + 5 && gr.at(5 + 50, 5 + 15 + 25) == 0xFFFF0000, "combine as a grid %dx%d, centered", gr.w, gr.h);
        // C17: difference
        Image d1(50, 40, 0xFFFFFFFF), d2 = d1;
        d2.FillRect({ 10, 10, 20, 15 }, 0xFF000000);
        int changed = 0; RECT box{};
        Image diff = DiffImage(d1, d2, 10, &changed, &box);
        CHECK(changed == 50 && box.left == 10 && box.right == 20 && box.top == 10 && box.bottom == 15 && diff.at(12, 12) == 0xFFE5322D,
              "difference image: %d changed pixels in the right box", changed);
        // C23: draw & hold recognition
        {
            std::vector<Gdiplus::PointF> line, rect, ell, tri, arrow;
            for (int i = 0; i <= 40; i++) line.push_back(Gdiplus::PointF(10.0f + i * 5, 20.0f + i * 2 + (i % 2 ? 1.0f : -1.0f)));
            for (int i = 0; i <= 40; i++) rect.push_back(Gdiplus::PointF(10.0f + i * 5, 10));
            for (int i = 0; i <= 20; i++) rect.push_back(Gdiplus::PointF(210, 10.0f + i * 5));
            for (int i = 0; i <= 40; i++) rect.push_back(Gdiplus::PointF(210.0f - i * 5, 110));
            for (int i = 0; i <= 19; i++) rect.push_back(Gdiplus::PointF(10, 110.0f - i * 5));
            for (int i = 0; i <= 60; i++) { double t = i * 2 * 3.14159265 / 60; ell.push_back(Gdiplus::PointF((float)(150 + 100 * cos(t)), (float)(100 + 50 * sin(t)))); }
            Gdiplus::PointF tp[3] = { { 100, 10 }, { 190, 150 }, { 10, 150 } };
            for (int s = 0; s < 3; s++) for (int i = 0; i < 20; i++) {
                Gdiplus::PointF p = tp[s], q = tp[(s + 1) % 3];
                tri.push_back(Gdiplus::PointF(p.X + (q.X - p.X) * i / 20, p.Y + (q.Y - p.Y) * i / 20));
            }
            tri.push_back(tp[0]);
            for (int i = 0; i <= 40; i++) arrow.push_back(Gdiplus::PointF(10.0f + i * 5, 50));
            for (int i = 1; i <= 6; i++) arrow.push_back(Gdiplus::PointF(210.0f - i * 4, 50.0f - i * 4));
            Gdiplus::PointF pa, pb;
            int kl = RecognizeStroke(line, pa, pb), kr = RecognizeStroke(rect, pa, pb), ke = RecognizeStroke(ell, pa, pb);
            int kt = RecognizeStroke(tri, pa, pb), ka = RecognizeStroke(arrow, pa, pb);
            CHECK(kl == RecLine && kr == RecRect && ke == RecEllipse && kt == RecTriangle, "rough strokes: line %d, rectangle %d, ellipse %d, triangle %d", kl, kr, ke, kt);
            CHECK(ka == RecArrow && std::fabs(pb.X - 210) < 1, "a line ending in a hook is an arrow (%d, tip x %.0f)", ka, pb.X);
            // a square drawn by hand: rounded corners, wobbly sides, the end short of the start (was taken for an ellipse)
            std::vector<Gdiplus::PointF> sq;
            const double side = 150, rad = 30, pi = 3.14159265;
            for (int c = 0; c < 4; c++) {                           // corners: top right, bottom right, bottom left, top left
                double cx = c < 2 ? side - rad : rad, cy = c == 1 || c == 2 ? side - rad : rad, a0 = -pi / 2 + c * pi / 2;
                double ex = c == 0 ? rad : c == 1 ? side : c == 2 ? side - rad : 0, ey = c == 0 ? 0 : c == 1 ? rad : c == 2 ? side : side - rad;
                double fx = cx + rad * cos(a0), fy = cy + rad * sin(a0);
                for (int i = 0; i <= 12; i++)                       // the wobbly side leading to the corner, then its arc
                    sq.push_back(Gdiplus::PointF((float)(ex + (fx - ex) * i / 12 + (i % 3) - 1), (float)(ey + (fy - ey) * i / 12 + (i % 2) * 2 - 1)));
                for (int i = 1; i <= 8; i++) { double t = a0 + i * (pi / 2) / 8; sq.push_back(Gdiplus::PointF((float)(cx + rad * cos(t)), (float)(cy + rad * sin(t)))); }
            }
            sq.resize(sq.size() - 3);
            // the same square tilted by 9 degrees (its box no longer fits it: low fill, corners away from the box's)
            std::vector<Gdiplus::PointF> tilt;
            for (auto& p : sq) {
                double x = p.X - side / 2, y = p.Y - side / 2, c = cos(9 * pi / 180), s = sin(9 * pi / 180);
                tilt.push_back(Gdiplus::PointF((float)(200 + x * c - y * s), (float)(200 + x * s + y * c)));
            }
            // an uneven ellipse: egg-shaped, wobbly, not closed
            std::vector<Gdiplus::PointF> egg;
            for (int i = 0; i <= 56; i++) {
                double t = i * 2 * pi / 60, rx = (cos(t) > 0 ? 110 : 90) + 4 * sin(5 * t);
                egg.push_back(Gdiplus::PointF((float)(150 + rx * cos(t)), (float)(100 + (55 + 3 * cos(7 * t)) * sin(t))));
            }
            int ks = RecognizeStroke(sq, pa, pb), kt2 = RecognizeStroke(tilt, pa, pb), kg = RecognizeStroke(egg, pa, pb);
            CHECK(ks == RecRect && kt2 == RecRect && kg == RecEllipse,
                  "hand-drawn: rounded square %d, tilted square %d, uneven ellipse %d (squares were taken for ellipses)", ks, kt2, kg);
        }
        // C34: smart page breaks fall between "text lines"
        {
            Image page(200, 1000, 0xFFFFFFFF);
            for (int y = 0; y < 1000; y += 40) page.FillRect({ 10, y + 8, 190, y + 30 }, 0xFF202020);   // a line of text every 40 px
            auto cuts = SmartBreaks(page, 300);
            bool quiet = !cuts.empty();
            for (int ct : cuts) quiet &= (ct % 40) < 8 || (ct % 40) >= 30;
            auto parts = SplitImage(page, cuts, false);
            int total = 0; for (auto& p : parts) total += p.h;
            CHECK(quiet && cuts.size() >= 3 && total == 1000, "smart breaks between text lines (%zu cuts), parts add up to the height", cuts.size());
        }
        // C26: date and time stamp
        {
            SYSTEMTIME t{}; t.wYear = 2026; t.wMonth = 10; t.wDay = 31; t.wHour = 14; t.wMinute = 5; t.wSecond = 9;
            CHECK(DateStampText(L"yyyy-MM-dd HH:mm:ss", t) == L"2026-10-31 14:05:09" && DateStampText(L"'Hora' h:mm tt", t) == L"Hora 2:05 PM",
                  "date stamp formats (%ls)", DateStampText(L"'Hora' h:mm tt", t).c_str());
            CHECK(!DateStampText(L"", t).empty() && !DateStampText(L"dddd MMMM", t).empty(), "date stamp with the Windows short format and names");
        }
        // C36: contrast
        CHECK(std::fabs(color::ContrastRatio(RGB(0, 0, 0), RGB(255, 255, 255)) - 21.0) < 0.01 &&
              ContrastReport(RGB(119, 119, 119), RGB(255, 255, 255)).find(L"4.48:1") != std::wstring::npos,
              "WCAG contrast ratio (black/white 21:1, #777 on white 4.48:1)");
        {
            Image sel(40, 20, 0xFFFFFFFF);
            sel.FillRect({ 5, 5, 35, 15 }, 0xFF102040);
            COLORREF fg = 0, bg = 0;
            DominantColors(sel, fg, bg);
            CHECK(bg == RGB(255, 255, 255) && color::ContrastRatio(fg, bg) > 10, "dominant colors of a selection: background and text");
        }
        // C27: background removal keeps the subject and clears the background
        {
            Image photo(120, 90, 0xFFF4F4F4);
            for (int y = 0; y < photo.h; y++) for (int x = 0; x < photo.w; x++) photo.at(x, y) = ARGB(255, 240 - y / 6, 242 - y / 6, 245);
            for (int y = 25; y < 65; y++) for (int x = 35; x < 85; x++) photo.at(x, y) = 0xFFC03020;
            Image cut = RemoveBackground(photo, 30, 0);
            CHECK(CA(cut.at(2, 2)) == 0 && CA(cut.at(118, 88)) == 0 && CA(cut.at(60, 45)) == 255, "background removed, subject kept (corner a=%d, center a=%d)", CA(cut.at(2, 2)), CA(cut.at(60, 45)));
        }
        // C25: cursor stamps are drawn with transparency
        {
            Image cur = RenderCursorStamp(L"cursor:arrow", 64), hand = RenderCursorStamp(L"cursor:hand", 64);
            CHECK(!cur.empty() && CA(cur.at(0, cur.h - 1)) == 0 && CA(cur.at(cur.w / 4, cur.h / 3)) > 200 && !hand.empty(), "cursor stamps (%dx%d)", cur.w, cur.h);
        }
        // C12: words in an area, highlighter snapped to the lines
        {
            OcrLine l1, l2;
            OcrWord w1; w1.text = L"Hello"; w1.box = { 10, 10, 60, 30 }; l1.words.push_back(w1);
            OcrWord w2; w2.text = L"world"; w2.box = { 70, 10, 130, 30 }; l1.words.push_back(w2);
            OcrWord w3; w3.text = L"next"; w3.box = { 10, 50, 50, 70 }; l2.words.push_back(w3);
            auto inArea = WordsInArea({ l1, l2 }, Gdiplus::RectF(0, 0, 90, 40));
            auto snapped = SnapToTextLines({ l1, l2 }, Gdiplus::RectF(30, 15, 120, 45));
            CHECK(inArea.size() == 1 && snapped.size() == 2 && snapped[0].X < 12 && snapped[0].GetRight() > 128, "text-only words in an area (%zu), highlighter snapped to %zu line(s)", inArea.size(), snapped.size());
        }
        // C05: beautify adds the padding around the capture
        {
            fx::BeautifyOptions bo; bo.padding = 30; bo.autoBalance = false; bo.aspect = 0; bo.background = 0;
            POINT org{};
            Image bt = fx::Beautify(Image(100, 60, 0xFF3366CC), bo, &org);
            CHECK(bt.w == 160 && bt.h == 120 && org.x == 30 && org.y == 30, "beautify: %dx%d with the capture at %ld,%ld", bt.w, bt.h, org.x, org.y);
        }
        // C21: AWS Signature V4 (the S3 "GET Object" example of the AWS documentation) and the custom uploader syntax
        {
            std::map<std::string, std::string> hd = { { "host", "examplebucket.s3.amazonaws.com" }, { "range", "bytes=0-9" },
                { "x-amz-content-sha256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" }, { "x-amz-date", "20130524T000000Z" } };
            std::string auth = upload::SigV4Authorization("GET", "/test.txt", "", hd, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                                                          "20130524T000000Z", "us-east-1", "s3", "AKIAIOSFODNN7EXAMPLE", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
            CHECK(auth.find("Signature=f0e8bdb87c964420e857bd35b5d6ed310bd44f0170aba48dd91039c6036bdb41") != std::string::npos, "SigV4 signature matches the AWS example");
            std::string resp = R"({"data":{"link":"https://i.example/abc.png","id":7},"ok":true})";
            CHECK(upload::ExpandSyntax("{json:data.link}", "a.png", resp, {}) == "https://i.example/abc.png" &&
                  upload::ExpandSyntax("name={filename}", "a.png", resp, {}) == "name=a.png", "custom uploader syntax ({json:…}, {filename})");
            net::Json j;
            bool parsed = net::Json::Parse(R"({"a":[1,2,{"b":"cé"}],"t":true})", j);
            const net::Json* pb = parsed ? j.Path("a[2].b") : nullptr;
            CHECK(pb && Utf8ToW(pb->Text()) == L"cé" && j.Path("t") && j.Path("t")->b, "JSON reader (paths, unicode escapes)");
        }
        // C34: a long image becomes a PDF of several pages
        {
            Image tall(600, 4000, 0xFFFFFFFF);
            for (int y = 0; y < 4000; y += 40) tall.FillRect({ 20, y + 10, 580, y + 30 }, 0xFF303030);
            std::wstring pdf = TestTempDir() + L"\\snapnab_long.pdf", err;
            bool ok = SavePdf(pdf, tall, err);
            std::string data;
            if (ok) {
                HANDLE f = CreateFileW(pdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                DWORD sz = GetFileSize(f, nullptr), n = 0;
                data.resize(sz); ReadFile(f, data.data(), sz, &n, nullptr); CloseHandle(f);
            }
            size_t cp = data.find("/Count ");
            int pages = cp != std::string::npos ? atoi(data.c_str() + cp + 7) : 0;
            CHECK(ok && (pages >= 3 || !SplitLongPages()), "long image split across PDF pages (%d pages)", pages);
            DeleteFileW(pdf.c_str());
        }
        // C35 / C37 / C43: sketch, vertical text, text along a line and pen pressure are saved and read back
        {
            DrawObj o; o.type = ObjType::Line; o.p1 = { 10, 10 }; o.p2 = { 200, 60 }; o.text = L"along";
            o.style.sketch = true; o.style.sketchSeed = 42; o.style.textOnPath = true;
            DrawObj p; p.type = ObjType::Pen; p.pts = { { 1, 1 }, { 5, 5 }, { 9, 4 } }; p.pressure = { 0.2f, 0.6f, 1.0f };
            DrawObj t; t.type = ObjType::Text; t.p1 = { 0, 0 }; t.p2 = { 80, 200 }; t.text = L"縦書き"; t.style.vertical = true;
            DrawObj hl; hl.type = ObjType::Blur; hl.p1 = { 0, 0 }; hl.p2 = { 100, 40 }; hl.style.textOnly = true; hl.textBoxes = { Gdiplus::RectF(5, 5, 30, 12) };
            SnapFrame fr; int counter = 1;
            auto r1 = ObjFromXml(ObjToXml(o, fr, counter), fr), r2 = ObjFromXml(ObjToXml(p, fr, counter), fr);
            auto r3 = ObjFromXml(ObjToXml(t, fr, counter), fr), r4 = ObjFromXml(ObjToXml(hl, fr, counter), fr);
            CHECK(r1 && r1->style.sketch && r1->style.sketchSeed == 42 && r1->style.textOnPath && r1->HasLabel() && r1->text == L"along" &&
                  r2 && r2->pressure.size() == 3 && std::fabs(r2->pressure[1] - 0.6f) < 0.01f && r3 && r3->style.vertical &&
                  r4 && r4->style.textOnly && r4->textBoxes.size() == 1, "new object properties survive the .snap round trip");
            Image canvas(240, 220, 0xFFFFFFFF);
            auto bmp = canvas.WrapGdip();
            Gdiplus::Graphics gg(bmp.get());
            RenderObject(gg, canvas, o); RenderObject(gg, canvas, p); RenderObject(gg, canvas, t); RenderObject(gg, canvas, hl);
            gg.Flush(Gdiplus::FlushIntentionSync);
            int dark = 0; for (uint32_t px : canvas.px) if (CR(px) < 200) dark++;
            CHECK(dark > 200, "sketch line, text along it, pressure pen and vertical text are drawn (%d px)", dark);
        }
    }

    printf("[13] Translations (lang\\*.po)\n");
    {
        // sequence of conversions of a printf string (%1$s counts by position): the translation must have the same
        auto specs = [](const std::wstring& s) {
            std::vector<std::pair<int, wchar_t>> v;
            for (size_t i = 0; i < s.size(); i++) {
                if (s[i] != L'%') continue;
                size_t j = i + 1;
                if (j < s.size() && s[j] == L'%') { i = j; continue; }
                int pos = 0;
                size_t k = j;
                while (k < s.size() && iswdigit(s[k])) k++;
                if (k < s.size() && s[k] == L'$' && k > j) { pos = _wtoi(s.substr(j, k - j).c_str()); j = k + 1; }
                while (j < s.size() && wcschr(L"-+ #0", s[j])) j++;
                while (j < s.size() && (iswdigit(s[j]) || s[j] == L'*' || s[j] == L'.')) j++;
                if (s.compare(j, 3, L"I64") == 0) j += 3;
                while (j < s.size() && wcschr(L"hlzjtL", s[j])) j++;
                if (j >= s.size()) break;
                wchar_t c = towlower(s[j]);
                v.push_back({ pos, c == L'i' ? L'd' : c });
                i = j;
            }
            std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
            std::wstring out;
            for (auto& p : v) out += p.second;
            return out;
        };
        CHECK(Format(L"%2$s, %1$s", L"world", L"hello") == L"hello, world", "Format accepts positional parameters (%%1$s)");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((ExeDir() + L"\\lang\\*.po").c_str(), &fd);
        size_t full = 0;
        std::vector<std::pair<std::wstring, size_t>> counts;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::vector<std::pair<std::wstring, std::wstring>> e;
                bool ok = i18n::LoadPo(ExeDir() + L"\\lang\\" + fd.cFileName, e);
                int bad = 0;
                for (auto& [id, str] : e) if (specs(id) != specs(str)) { if (!bad) printf("       %ls: \"%ls\"\n", fd.cFileName, id.c_str()); bad++; }
                CHECK(ok && !e.empty() && !bad, "%ls: %zu strings, %d with %%d/%%s markers different from the original", fd.cFileName, e.size(), bad);
                counts.push_back({ fd.cFileName, e.size() });
                full = std::max(full, e.size());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        CHECK(!counts.empty(), "%zu catalogs in the lang folder", counts.size());
        for (auto& [name, n] : counts) CHECK(n == full, "%ls complete (%zu of %zu)", name.c_str(), n, full);
    }

    printf("[17] XML: Rust and the former C++\n");
    {
        // the Rust reader and writer must give the trees and the bytes the C++ ones gave. Strings that are not valid
        // UTF-8 (only garbage input makes them) are compared by shape: Rust turns the bad bytes into U+FFFD.
        auto validUtf8 = [](const std::string& t) {
            return t.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, t.data(), (int)t.size(), nullptr, 0) > 0;
        };
        auto sameStr = [&](const std::string& ref, const std::string& rust) {
            return validUtf8(ref) ? ref == rust : validUtf8(rust);
        };
        auto same = [&](auto& self, const XmlNode& a, const XmlNode& b) -> bool {
            if (!sameStr(a.name, b.name) || !sameStr(a.text, b.text) || a.attrs.size() != b.attrs.size() ||
                a.children.size() != b.children.size()) return false;
            for (size_t i = 0; i < a.attrs.size(); i++)
                if (!sameStr(a.attrs[i].first, b.attrs[i].first) || !sameStr(a.attrs[i].second, b.attrs[i].second)) return false;
            for (size_t i = 0; i < a.children.size(); i++) if (!self(self, a.children[i], b.children[i])) return false;
            return true;
        };
        auto valid = [&](auto& self, const XmlNode& n) -> bool {
            if (!validUtf8(n.name) || !validUtf8(n.text)) return false;
            for (auto& [k, v] : n.attrs) if (!validUtf8(k) || !validUtf8(v)) return false;
            for (auto& c : n.children) if (!self(self, c)) return false;
            return true;
        };
        // one text through both readers; false (and the text shown) when they differ. 'thrown' counts the texts the
        // former reader could not handle (it threw on "<a><b/" at the very end; the Rust one reports malformed)
        int thrown = 0;
        auto compare = [&](const std::string& t) {
            XmlNode a, b;
            bool okA;
            try { okA = xmlref::Parse(t, a); } catch (...) { thrown++; return !XmlParse(t, b); }
            bool okB = XmlParse(t, b);
            bool eq = okA == okB && same(same, a, b);
            if (eq && valid(valid, a))
                eq = xmlref::Serialize(a, true) == XmlSerialize(b) && xmlref::Serialize(a, false) == XmlSerialize(a, false);
            if (!eq) printf("       differs: \"%.200s\"\n", t.c_str());
            return eq;
        };

        // real documents: the XML files of the user's Snapnab folder (presets, Names.xml…) and the index and frame
        // documents inside the .snap files of the library, only read
        std::vector<std::string> real;
        auto walk = [&](auto& self, const std::wstring& dir) -> void {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                std::wstring n = fd.cFileName, path = dir + L"\\" + n;
                if (n == L"." || n == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { self(self, path); continue; }
                std::wstring ext = n.size() > 4 ? n.substr(n.size() - 4) : L"";
                for (auto& c : ext) c = towlower(c);
                if (ext == L".xml") {
                    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (f == INVALID_HANDLE_VALUE) continue;
                    std::string buf(GetFileSize(f, nullptr), '\0');
                    DWORD rd = 0;
                    ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, nullptr);
                    CloseHandle(f);
                    buf.resize(rd);
                    real.push_back(std::move(buf));
                } else if (ext == L"snap") {
                    SnapcoreReader* r = snapcore_open(path.c_str(), path.size());
                    if (!r) continue;
                    auto read = [&](const std::string& stream) {
                        SnapcoreBuf b{};
                        if (!snapcore_read(r, stream.data(), stream.size(), &b)) return false;
                        real.emplace_back(reinterpret_cast<const char*>(b.data), b.len);
                        snapcore_buf_free(b);
                        return true;
                    };
                    if (read("Index"))
                        for (int i = 0; i < 64 && read("Frame" + std::to_string(i) + "/Document"); i++) {}
                    snapcore_close(r);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        };
        walk(walk, AppDataDir());
        wchar_t extra[MAX_PATH];         // more documents to compare (a copy of another library, for example)
        if (GetEnvironmentVariableW(L"SNAPNAB_XML_CORPUS", extra, MAX_PATH)) walk(walk, extra);
        int realBad = 0;
        for (auto& t : real) if (!compare(t)) realBad++;
        CHECK(!realBad, "%zu real XML documents (Snapnab folder and .snap files) read and written alike, %d differ", real.size(), realBad);

        // generated text: pieces of XML, malformed on purpose most of the time
        static const char* pieces[] = { "<a", "<b", "<Frame", ">", "/>", "</a>", "</b>", "</x>", " ", "\n", "\t", "k=",
            "\"v\"", "'w'", "=", "\"", "'", "&amp;", "&lt;", "&#65;", "&#x1F600;", "&#xD800;", "&#-1;", "&#x0x41;",
            "&bogus;", "&", ";", "text", "\xC3\xA7", "\xE4\xB8\xAD", "<!-- c -->", "<?pi?>", "<!DOCTYPE d>",
            "<![CDATA[x<y&z]]>", "\xEF\xBB\xBF", "\r\n" };
        uint32_t seed = 12345;
        auto rnd = [&](uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
        int genBad = 0, genOk = 0;
        const int kTexts = 30000;
        for (int k = 0; k < kTexts; k++) {
            std::string t = rnd(4) ? "<a" : "";
            for (uint32_t n = 1 + rnd(40); n--;) t += pieces[rnd(_countof(pieces))];
            if (!compare(t)) genBad++;
            XmlNode tmp;
            try { if (xmlref::Parse(t, tmp)) genOk++; } catch (...) {}
        }
        CHECK(!genBad, "%d generated texts (%d well formed, %d the former reader threw on): %d differ", kTexts, genOk, thrown, genBad);

        // generated trees: written alike, and read back the same by both readers
        static const char* words[] = { "a", "Frame", "x-y", "\xC3\xA7\xE4\xB8\xAD", "&<>\"'", "\r\n\t", " sp ", "", "1.5" };
        int treeBad = 0;
        for (int k = 0; k < 3000; k++) {
            auto make = [&](auto& self, XmlNode& n, int depth) -> void {
                n.name = std::string("N") + words[rnd(3)];
                for (uint32_t i = rnd(4); i--;)
                    n.attrs.emplace_back("k" + std::to_string(rnd(9)), std::string(words[rnd(_countof(words))]) + words[rnd(_countof(words))]);
                if (rnd(3) == 0) n.text = std::string(words[rnd(_countof(words))]) + "x";
                if (depth < 4) for (uint32_t i = rnd(4); i--;) self(self, n.children.emplace_back(), depth + 1);
            };
            XmlNode root;
            make(make, root, 0);
            std::string a = xmlref::Serialize(root, true), b = XmlSerialize(root);
            XmlNode ra, rb;
            if (!(a == b && xmlref::Parse(a, ra) && XmlParse(b, rb) && same(same, ra, rb))) treeBad++;
        }
        CHECK(!treeBad, "3000 generated trees written byte for byte alike and read back alike (%d differ)", treeBad);

        // files: written by Rust, read back by Rust and by the former reader
        std::wstring xp = Tmp(L"xml.xml");
        XmlNode doc("Presets");
        doc.add("Preset").setW("Name", L"Região ç → 中文 😀").set("Hotkey", "Ctrl+\"<&>\"");
        XmlNode back, refBack, untouched("Keep");
        bool saved = XmlSaveFile(xp, doc), loaded = XmlLoadFile(xp, back);
        std::string bytes;
        HANDLE f = CreateFileW(xp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            bytes.resize(GetFileSize(f, nullptr));
            DWORD rd = 0;
            ReadFile(f, bytes.data(), (DWORD)bytes.size(), &rd, nullptr);
            CloseHandle(f);
        }
        DeleteFileW(xp.c_str());
        bool missing = !XmlLoadFile(xp, untouched) && untouched.name == "Keep";
        CHECK(saved && loaded && bytes == xmlref::Serialize(doc, true) && xmlref::Parse(bytes, refBack) && same(same, refBack, back) &&
              back.child("Preset") && back.child("Preset")->getW("Name") == L"Região ç → 中文 😀" && missing,
              "XML file written and read back; a missing file leaves the tree as it was");
    }

    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL OK", g_fail);
    CoUninitialize();
    return g_fail ? 1 : 0;
}
