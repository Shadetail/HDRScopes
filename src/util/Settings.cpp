#include "util/Settings.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#pragma comment(lib, "shell32.lib")

std::wstring Settings::FilePath() {
    PWSTR path = nullptr;
    std::wstring dir = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        dir = path;
        CoTaskMemFree(path);
    }
    dir += L"\\HDRScopes";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

// ---- tiny key=value writer/reader -------------------------------------------
namespace {
void W(std::ostream& o, const char* k, bool v)   { o << k << "=" << (v ? 1 : 0) << "\n"; }
void W(std::ostream& o, const char* k, int v)    { o << k << "=" << v << "\n"; }
void W(std::ostream& o, const char* k, float v)  { o << k << "=" << v << "\n"; }
void W(std::ostream& o, const char* k, double v) { o << k << "=" << v << "\n"; }

struct KV {
    std::map<std::string, std::string> m;
    bool getb(const char* k, bool d) const { auto i = m.find(k); return i == m.end() ? d : i->second != "0"; }
    int  geti(const char* k, int d) const { auto i = m.find(k); return i == m.end() ? d : atoi(i->second.c_str()); }
    float getf(const char* k, float d) const { auto i = m.find(k); return i == m.end() ? d : (float)atof(i->second.c_str()); }
    double getd(const char* k, double d) const { auto i = m.find(k); return i == m.end() ? d : atof(i->second.c_str()); }
    bool has(const char* k) const { return m.find(k) != m.end(); }
};
}

void Settings::Save() const {
    std::ofstream o(FilePath());
    if (!o) return;
    W(o, "showFps", showFps);
    W(o, "fpsLimit", fpsLimit);
    W(o, "uiFollowSdrWhite", uiFollowSdrWhite);
    W(o, "debugShowTestPattern", debugShowTestPattern);
    W(o, "outputIndex", outputIndex);
    W(o, "regionMode", regionMode);
    for (int i = 0; i < 4; ++i) { char k[32]; snprintf(k, 32, "dragRect%d", i); W(o, k, dragRect[i]); }
    W(o, "layout", (int)layout);
    for (int i = 0; i < 4; ++i) { char k[32]; snprintf(k, 32, "panelScope%d", i); W(o, k, (int)panelScope[i]); }
    W(o, "quality", (int)quality);
    W(o, "bilinearDownsample", bilinearDownsample);
    W(o, "gratR", graticuleColor.x); W(o, "gratG", graticuleColor.y); W(o, "gratB", graticuleColor.z);
    W(o, "graticuleOpacity", graticuleOpacity);
    W(o, "colorize", colorize);
    W(o, "showHoverProbe", showHoverProbe);
    W(o, "waveMode", waveMode);
    for (int i = 0; i < 3; ++i) { char k[32]; snprintf(k, 32, "chan%d", i); W(o, k, channelEnabled[i]); }
    W(o, "extents", extents);
    W(o, "extentsStyle", extentsStyle);
    W(o, "extentsSupersample", extentsSupersample);
    W(o, "gain", gain);
    W(o, "sdrWhiteZoom", sdrWhiteZoom);
    W(o, "refCount", (int)refLines.size());
    for (size_t i = 0; i < refLines.size(); ++i) {
        char k[32];
        snprintf(k, 32, "refNits%zu", i);    W(o, k, refLines[i].nits);
        snprintf(k, 32, "refEnabled%zu", i); W(o, k, refLines[i].enabled);
    }
    W(o, "refLineThickness", refLineThickness);
    W(o, "histoMode", histoMode);
    W(o, "histoGain", histoGain);
    for (int i = 0; i < 3; ++i) { char k[32]; snprintf(k, 32, "histoChan%d", i); W(o, k, histoChannelEnabled[i]); }
    W(o, "vectorGain", vectorGain);
    W(o, "vectorShowSkin", vectorShowSkin);
    W(o, "cieDiagram", cieDiagram);
    W(o, "cieGain", cieGain);
    W(o, "cieShowRec2020", cieShowRec2020);
    W(o, "cieShowP3", cieShowP3);
    W(o, "cieShowRec709", cieShowRec709);
    for (int i = 0; i < 4; ++i) {
        char k[32];
        snprintf(k, 32, "zoom%d", i); W(o, k, zoom[i]);
        snprintf(k, 32, "panX%d", i); W(o, k, panX[i]);
        snprintf(k, 32, "panY%d", i); W(o, k, panY[i]);
    }
    W(o, "wndL", wndL); W(o, "wndT", wndT); W(o, "wndR", wndR); W(o, "wndB", wndB);
    W(o, "wndShow", wndShow);
}

void Settings::Load() {
    std::ifstream in(FilePath());
    if (!in) return;
    KV kv;
    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv.m[line.substr(0, eq)] = line.substr(eq + 1);
    }

    showFps = kv.getb("showFps", showFps);
    fpsLimit = kv.geti("fpsLimit", fpsLimit);
    uiFollowSdrWhite = kv.getb("uiFollowSdrWhite", uiFollowSdrWhite);
    debugShowTestPattern = kv.getb("debugShowTestPattern", debugShowTestPattern);
    outputIndex = kv.geti("outputIndex", outputIndex);
    regionMode = kv.geti("regionMode", regionMode);
    for (int i = 0; i < 4; ++i) { char k[32]; snprintf(k, 32, "dragRect%d", i); dragRect[i] = kv.geti(k, dragRect[i]); }
    layout = (LayoutMode)kv.geti("layout", (int)layout);
    for (int i = 0; i < 4; ++i) { char k[32]; snprintf(k, 32, "panelScope%d", i); panelScope[i] = (ScopeType)kv.geti(k, (int)panelScope[i]); }
    quality = (Quality)kv.geti("quality", (int)quality);
    bilinearDownsample = kv.getb("bilinearDownsample", bilinearDownsample);
    graticuleColor.x = kv.getf("gratR", graticuleColor.x);
    graticuleColor.y = kv.getf("gratG", graticuleColor.y);
    graticuleColor.z = kv.getf("gratB", graticuleColor.z);
    graticuleOpacity = kv.getf("graticuleOpacity", graticuleOpacity);
    colorize = kv.getb("colorize", colorize);
    showHoverProbe = kv.getb("showHoverProbe", showHoverProbe);
    waveMode = kv.geti("waveMode", waveMode);
    for (int i = 0; i < 3; ++i) { char k[32]; snprintf(k, 32, "chan%d", i); channelEnabled[i] = kv.getb(k, channelEnabled[i]); }
    extents = kv.getb("extents", extents);
    extentsStyle = kv.geti("extentsStyle", extentsStyle);
    extentsSupersample = kv.getb("extentsSupersample", extentsSupersample);
    gain = kv.getf("gain", gain);
    sdrWhiteZoom = kv.getb("sdrWhiteZoom", sdrWhiteZoom);
    int rc = kv.geti("refCount", -1);
    if (rc >= 0) {
        refLines.clear();
        for (int i = 0; i < rc; ++i) {
            char k[32]; RefLine r;
            snprintf(k, 32, "refNits%d", i);    r.nits = kv.getd(k, 100.0);
            snprintf(k, 32, "refEnabled%d", i); r.enabled = kv.getb(k, true);
            refLines.push_back(r);
        }
    }
    refLineThickness = kv.getf("refLineThickness", refLineThickness);
    histoMode = kv.geti("histoMode", histoMode);
    histoGain = kv.getf("histoGain", histoGain);
    for (int i = 0; i < 3; ++i) { char k[32]; snprintf(k, 32, "histoChan%d", i); histoChannelEnabled[i] = kv.getb(k, histoChannelEnabled[i]); }
    vectorGain = kv.getf("vectorGain", vectorGain);
    vectorShowSkin = kv.getb("vectorShowSkin", vectorShowSkin);
    cieDiagram = kv.geti("cieDiagram", cieDiagram);
    cieGain = kv.getf("cieGain", cieGain);
    cieShowRec2020 = kv.getb("cieShowRec2020", cieShowRec2020);
    cieShowP3 = kv.getb("cieShowP3", cieShowP3);
    cieShowRec709 = kv.getb("cieShowRec709", cieShowRec709);
    for (int i = 0; i < 4; ++i) {
        char k[32];
        snprintf(k, 32, "zoom%d", i); zoom[i] = kv.getf(k, zoom[i]);
        snprintf(k, 32, "panX%d", i); panX[i] = kv.getf(k, panX[i]);
        snprintf(k, 32, "panY%d", i); panY[i] = kv.getf(k, panY[i]);
    }
    wndL = kv.geti("wndL", wndL); wndT = kv.geti("wndT", wndT);
    wndR = kv.geti("wndR", wndR); wndB = kv.geti("wndB", wndB);
    wndShow = kv.geti("wndShow", wndShow);
}
