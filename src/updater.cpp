#include "updater.h"
#include "state.h"

#include <winhttp.h>
#include <cctype>

namespace {

struct UpdaterState {
    volatile LONG busy = 0;
    bool          available = false;
    bool          manual = false;
    std::wstring  latestTag;
    std::wstring  downloadUrl;
    std::wstring  installerPath;
};

UpdaterState g;

constexpr wchar_t kReleasesPage[] =
    L"https://github.com/lordjumper/ScreenDoodle/releases";
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] =
    L"/repos/lordjumper/ScreenDoodle/releases/latest";
constexpr wchar_t kUserAgentHeader[] =
    L"User-Agent: ScreenDoodle-Updater\r\n";

bool HttpsGetHostPath(const wchar_t* host, const wchar_t* path,
                       std::string& outBody) {
    bool ok = false;
    HINTERNET sess = WinHttpOpen(L"ScreenDoodle-Updater/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return false;
    WinHttpSetTimeouts(sess, 8000, 8000, 12000, 20000);
    HINTERNET conn = WinHttpConnect(sess, host,
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) { WinHttpCloseHandle(sess); return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (req) {
        WinHttpAddRequestHeaders(req, kUserAgentHeader, (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, statusSize = sizeof(status);
            WinHttpQueryHeaders(req,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                WINHTTP_NO_HEADER_INDEX);
            if (status >= 200 && status < 300) {
                ok = true;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                    size_t off = outBody.size();
                    if (off + avail > 4 * 1024 * 1024) { ok = false; break; }
                    outBody.resize(off + avail);
                    DWORD read = 0;
                    if (!WinHttpReadData(req, &outBody[off], avail, &read)) {
                        ok = false; break;
                    }
                    outBody.resize(off + read);
                }
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return ok;
}

std::string ReadJsonString(const std::string& json, size_t& pos) {
    while (pos < json.size()
           && (json[pos] == ' ' || json[pos] == ':'
               || json[pos] == '\t' || json[pos] == '\n'
               || json[pos] == '\r'))
        ++pos;
    if (pos >= json.size() || json[pos] != '"') return {};
    ++pos;
    std::string out;
    out.reserve(64);
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char c = json[pos + 1];
            switch (c) {
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case '/':  out += '/';  break;
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'u':
                    if (pos + 5 < json.size()) { pos += 4; }
                    break;
                default:   out += c;    break;
            }
            pos += 2;
        } else {
            out += json[pos++];
        }
    }
    if (pos < json.size()) ++pos;
    return out;
}

std::string FindJsonString(const std::string& json,
                            const char* key, size_t startFrom = 0) {
    std::string pat = "\"";
    pat += key;
    pat += "\"";
    size_t i = json.find(pat, startFrom);
    if (i == std::string::npos) return {};
    i += pat.size();
    return ReadJsonString(json, i);
}

std::string PickInstallerUrl(const std::string& json) {
    std::string fallback;
    const std::string key = "\"browser_download_url\"";
    size_t pos = 0;
    while ((pos = json.find(key, pos)) != std::string::npos) {
        pos += key.size();
        std::string url = ReadJsonString(json, pos);
        if (url.size() < 4) continue;
        std::string lower = url;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);
        if (lower.compare(lower.size() - 4, 4, ".exe") != 0) continue;
        if (lower.find("setup") != std::string::npos
            || lower.find("install") != std::string::npos)
            return url;
        if (fallback.empty()) fallback = url;
    }
    return fallback;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

int CompareVersionStrings(const wchar_t* a, const wchar_t* b) {
    auto next = [](const wchar_t*& p) {
        if (*p == L'v' || *p == L'V') ++p;
        int n = 0;
        bool any = false;
        while (*p >= L'0' && *p <= L'9') {
            n = n * 10 + (*p - L'0');
            ++p;
            any = true;
        }
        if (!any) return -1;
        if (*p == L'.') ++p;
        return n;
    };
    for (int i = 0; i < 8; ++i) {
        const wchar_t* pa = a;
        const wchar_t* pb = b;
        for (int s = 0; s < i; ++s) { next(pa); next(pb); }
        int va = next(pa); if (va < 0) va = 0;
        int vb = next(pb); if (vb < 0) vb = 0;
        if (va < vb) return -1;
        if (va > vb) return  1;
    }
    return 0;
}

bool DownloadToFile(const std::wstring& url, const std::wstring& dest) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;     uc.dwHostNameLength = ARRAYSIZE(host);
    uc.lpszUrlPath  = path;     uc.dwUrlPathLength  = ARRAYSIZE(path);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    bool ok = false;
    HINTERNET sess = WinHttpOpen(L"ScreenDoodle-Updater/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return false;
    WinHttpSetTimeouts(sess, 8000, 8000, 12000, 60000);

    HINTERNET conn = WinHttpConnect(sess, host, uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS)
                  ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (req) {
        WinHttpAddRequestHeaders(req, kUserAgentHeader, (DWORD)-1L,
            WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            && WinHttpReceiveResponse(req, nullptr)) {
            HANDLE f = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                ok = true;
                BYTE buf[16384];
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                    DWORD chunk = avail > sizeof(buf)
                                  ? (DWORD)sizeof(buf) : avail;
                    DWORD read = 0;
                    if (!WinHttpReadData(req, buf, chunk, &read) || read == 0) {
                        ok = false; break;
                    }
                    DWORD wrote = 0;
                    if (!WriteFile(f, buf, read, &wrote, nullptr)
                        || wrote != read) {
                        ok = false; break;
                    }
                }
                CloseHandle(f);
                if (!ok) DeleteFileW(dest.c_str());
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return ok;
}

DWORD WINAPI CheckThread(LPVOID) {
    std::string body;
    bool ok = HttpsGetHostPath(kApiHost, kApiPath, body);
    if (!ok || body.empty()) {
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateFailed, 0);
        return 0;
    }
    std::string tag = FindJsonString(body, "tag_name");
    std::string url = PickInstallerUrl(body);
    if (tag.empty()) {
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateFailed, 0);
        return 0;
    }
    std::wstring wtag = Utf8ToWide(tag);
    std::wstring wurl = Utf8ToWide(url);

    if (CompareVersionStrings(wtag.c_str(), kAppVersion) > 0) {
        g.available   = true;
        g.latestTag   = std::move(wtag);
        g.downloadUrl = std::move(wurl);
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateAvailable, 0);
    } else {
        g.available = false;
        g.latestTag = std::move(wtag);
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateNone, 0);
    }
    return 0;
}

DWORD WINAPI DownloadThread(LPVOID) {
    if (g.downloadUrl.empty()) {
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateFailed, 0);
        return 0;
    }
    wchar_t tmpDir[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmpDir);
    if (n == 0 || n >= MAX_PATH) {
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateFailed, 0);
        return 0;
    }
    std::wstring dest(tmpDir);
    dest += L"ScreenDoodle-Update.exe";
    if (!DownloadToFile(g.downloadUrl, dest)) {
        InterlockedExchange(&g.busy, 0);
        if (A.msgWnd)
            PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateFailed, 0);
        return 0;
    }
    g.installerPath = std::move(dest);
    InterlockedExchange(&g.busy, 0);
    if (A.msgWnd)
        PostMessageW(A.msgWnd, WM_UPDATE_RESULT, kUpdateDownloaded, 0);
    return 0;
}

}

void StartUpdateCheck(bool manual) {
    if (InterlockedCompareExchange(&g.busy, 1, 0) != 0) return;
    g.manual = manual;
    HANDLE h = CreateThread(nullptr, 0, CheckThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g.busy, 0);
}

void StartUpdateDownload() {
    if (g.downloadUrl.empty()) return;
    if (InterlockedCompareExchange(&g.busy, 1, 0) != 0) return;
    HANDLE h = CreateThread(nullptr, 0, DownloadThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else InterlockedExchange(&g.busy, 0);
}

bool           IsUpdateAvailable()        { return g.available; }
bool           HasInstallerUrl()          { return !g.downloadUrl.empty(); }
const wchar_t* LatestVersionTag()         { return g.latestTag.c_str(); }
const wchar_t* DownloadedInstallerPath()  { return g.installerPath.c_str(); }
const wchar_t* ReleasesPageUrl()          { return kReleasesPage; }
bool           IsManualCheck()            { return g.manual; }
