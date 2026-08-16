// Update check, deliberately tiny: GET github.com/<repo>/releases/latest with
// redirects disabled — GitHub answers 302 with the newest release's tag URL in
// the Location header, so the version comes from one header read. No JSON, no
// API quota, no auth, and the only data sent is the request itself.
#include "util/UpdateCheck.h"
#include "util/Common.h"
#include <winhttp.h>
#include <atomic>
#include <charconv>
#include <thread>
#include <tuple>

namespace {

std::atomic<bool> g_newer{ false };
std::string g_tag, g_url;  // written by the worker before g_newer is set

bool ParseVersion(const char* s, int v[3]) {
    const char* p = s;
    const char* end = s + std::char_traits<char>::length(s);
    for (int i = 0; i < 3; ++i) {
        if (p == end || *p < '0' || *p > '9') return false;
        auto parsed = std::from_chars(p, end, v[i]);
        if (parsed.ec != std::errc()) return false;
        p = parsed.ptr;
        if (i < 2) {
            if (p == end || *p != '.') return false;
            ++p;
        }
    }
    return p == end;
}

void Worker() {
    HINTERNET ses = WinHttpOpen(L"HDRScopes", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return;
    WinHttpSetTimeouts(ses, 5000, 5000, 5000, 5000);
    HINTERNET con = WinHttpConnect(ses, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", L"/Shadetail/HDRScopes/releases/latest",
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                        : nullptr;
    if (req) {
        DWORD off = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof(off));
        wchar_t loc[512];
        DWORD n = sizeof(loc);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr) &&
            WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &n, WINHTTP_NO_HEADER_INDEX)) {
            // Accept only the exact release-tag URL shape (the URL ends up in
            // ShellExecute, so don't trust anything looser — and a relative
            // Location wouldn't open anyway).
            std::string url;
            for (wchar_t c : std::wstring(loc)) url += (char)c;  // URL is ASCII
            const char prefix[] = "https://github.com/Shadetail/HDRScopes/releases/tag/";
            std::string tag = url.rfind(prefix, 0) == 0 ? url.substr(sizeof(prefix) - 1) : "";
            int cur[3], got[3];
            if (tag.size() > 1 && tag[0] == 'v' &&
                ParseVersion(tag.c_str() + 1, got) && ParseVersion(HDRSCOPES_VERSION, cur) &&
                std::make_tuple(got[0], got[1], got[2]) > std::make_tuple(cur[0], cur[1], cur[2])) {
                g_tag = tag;
                g_url = url;
                g_newer.store(true, std::memory_order_release);
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
}

}

namespace updatecheck {

void StartAsync() {
    static std::atomic<bool> started{ false };
    if (started.exchange(true)) return;
    std::thread(Worker).detach();
}

bool NewerAvailable(std::string* tag, std::string* url) {
    if (!g_newer.load(std::memory_order_acquire)) return false;
    *tag = g_tag;
    *url = g_url;
    return true;
}

}
