#include "config.h"
#include "state.h"

#include <cwctype>

AppConfig C;

namespace {

std::wstring AppDataDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring dir(buf, n);
    dir += L"\\ScreenDoodle";
    return dir;
}

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0
              && size.QuadPart < 256 * 1024;
    if (ok) {
        out.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ok = ReadFile(f, &out[0], (DWORD)out.size(), &read, nullptr) != 0;
        out.resize(read);
    }
    CloseHandle(f);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, nullptr) != 0
              && wrote == data.size();
    CloseHandle(f);
    return ok;
}

size_t SkipWs(const std::string& j, size_t i) {
    while (i < j.size() && (j[i] == ' ' || j[i] == '\t'
                            || j[i] == '\n' || j[i] == '\r'))
        ++i;
    return i;
}

size_t FindKey(const std::string& j, const char* key) {
    std::string pat = "\"";
    pat += key;
    pat += "\"";
    size_t i = j.find(pat);
    if (i == std::string::npos) return std::string::npos;
    i = SkipWs(j, i + pat.size());
    if (i >= j.size() || j[i] != ':') return std::string::npos;
    return SkipWs(j, i + 1);
}

std::string Sub(const std::string& j, const char* key) {
    size_t i = FindKey(j, key);
    if (i == std::string::npos || j[i] != '{') return {};
    int depth = 0;
    for (size_t k = i; k < j.size(); ++k) {
        if (j[k] == '{') ++depth;
        else if (j[k] == '}') {
            if (--depth == 0) return j.substr(i, k - i + 1);
        }
    }
    return {};
}

std::string GetStr(const std::string& j, const char* key,
                   const std::string& def) {
    size_t i = FindKey(j, key);
    if (i == std::string::npos || j[i] != '"') return def;
    ++i;
    std::string out;
    while (i < j.size() && j[i] != '"') {
        if (j[i] == '\\' && i + 1 < j.size()) ++i;
        out += j[i++];
    }
    return out.empty() ? def : out;
}

int GetInt(const std::string& j, const char* key, int def, int lo, int hi) {
    size_t i = FindKey(j, key);
    if (i == std::string::npos) return def;
    bool neg = (j[i] == '-');
    if (neg) ++i;
    if (i >= j.size() || j[i] < '0' || j[i] > '9') return def;
    int v = 0;
    while (i < j.size() && j[i] >= '0' && j[i] <= '9') {
        v = v * 10 + (j[i++] - '0');
        if (v > 1000000) break;
    }
    if (neg) v = -v;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

struct NameVal { const char* name; int val; };

const NameVal kTools[] = {
    {"pen",         (int)Tool::Pen},
    {"pencil",      (int)Tool::Pencil},
    {"highlighter", (int)Tool::Highlighter},
    {"eraser",      (int)Tool::Eraser},
    {"text",        (int)Tool::Text},
};

const char* ToolName(Tool t) {
    for (const auto& e : kTools)
        if (e.val == (int)t) return e.name;
    return "pen";
}

Tool ParseTool(const std::string& s, Tool def) {
    for (const auto& e : kTools)
        if (s == e.name) return (Tool)e.val;
    return def;
}

struct KeyName { const wchar_t* name; UINT vk; };

const KeyName kNamedKeys[] = {
    {L"Space",    VK_SPACE},  {L"Tab",     VK_TAB},
    {L"Escape",   VK_ESCAPE}, {L"Insert",  VK_INSERT},
    {L"Delete",   VK_DELETE}, {L"Home",    VK_HOME},
    {L"End",      VK_END},    {L"PageUp",  VK_PRIOR},
    {L"PageDown", VK_NEXT},   {L"Up",      VK_UP},
    {L"Down",     VK_DOWN},   {L"Left",    VK_LEFT},
    {L"Right",    VK_RIGHT},
};

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

bool ParseHotkey(const std::wstring& text, HotkeySpec& out) {
    HotkeySpec hk{0, 0};
    size_t start = 0;
    while (start <= text.size()) {
        size_t plus = text.find(L'+', start);
        std::wstring part = text.substr(
            start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
        while (!part.empty() && iswspace(part.front())) part.erase(part.begin());
        while (!part.empty() && iswspace(part.back()))  part.pop_back();

        if (!part.empty()) {
            if (EqualsNoCase(part, L"Ctrl") || EqualsNoCase(part, L"Control"))
                hk.mods |= MOD_CONTROL;
            else if (EqualsNoCase(part, L"Alt"))
                hk.mods |= MOD_ALT;
            else if (EqualsNoCase(part, L"Shift"))
                hk.mods |= MOD_SHIFT;
            else if (EqualsNoCase(part, L"Win") || EqualsNoCase(part, L"Meta"))
                hk.mods |= MOD_WIN;
            else if (part.size() == 1
                     && ((part[0] >= L'A' && part[0] <= L'Z')
                         || (part[0] >= L'a' && part[0] <= L'z')
                         || (part[0] >= L'0' && part[0] <= L'9')))
                hk.vk = (UINT)towupper(part[0]);
            else if ((part[0] == L'F' || part[0] == L'f') && part.size() <= 3) {
                int n = _wtoi(part.c_str() + 1);
                if (n >= 1 && n <= 12) hk.vk = VK_F1 + (UINT)(n - 1);
            } else {
                for (const auto& k : kNamedKeys)
                    if (EqualsNoCase(part, k.name)) { hk.vk = k.vk; break; }
            }
        }

        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }

    if (hk.vk == 0 || hk.mods == 0) return false;
    out = hk;
    return true;
}

std::wstring Utf8ToWideStr(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(),
                        &out[0], n, nullptr, nullptr);
    return out;
}

void ReadHotkey(const std::string& obj, const char* key, HotkeySpec& target) {
    std::string raw = GetStr(obj, key, "");
    if (raw.empty()) return;
    HotkeySpec parsed;
    if (ParseHotkey(Utf8ToWideStr(raw), parsed)) target = parsed;
}

}

std::wstring ConfigFilePath() {
    std::wstring dir = AppDataDir();
    if (dir.empty()) return {};
    return dir + L"\\config.json";
}

std::wstring DescribeHotkey(const HotkeySpec& hk) {
    std::wstring s;
    if (hk.mods & MOD_CONTROL) s += L"Ctrl+";
    if (hk.mods & MOD_ALT)     s += L"Alt+";
    if (hk.mods & MOD_SHIFT)   s += L"Shift+";
    if (hk.mods & MOD_WIN)     s += L"Win+";

    if (hk.vk >= VK_F1 && hk.vk <= VK_F12) {
        wchar_t buf[8];
        _snwprintf_s(buf, ARRAYSIZE(buf), _TRUNCATE, L"F%u", hk.vk - VK_F1 + 1);
        s += buf;
        return s;
    }
    for (const auto& k : kNamedKeys) {
        if (k.vk == hk.vk) { s += k.name; return s; }
    }
    if ((hk.vk >= 'A' && hk.vk <= 'Z') || (hk.vk >= '0' && hk.vk <= '9')) {
        s += (wchar_t)hk.vk;
        return s;
    }
    s += L'?';
    return s;
}

namespace {

std::wstring ReadLauncherValue(const wchar_t* name) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ScreenDoodle", 0,
                      KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return {};

    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hk, name, nullptr, &type, (LPBYTE)buf, &size);
    RegCloseKey(hk);

    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return std::wstring(buf);
}

}

std::wstring LauncherExePath() {
    std::wstring path = ReadLauncherValue(L"LauncherPath");
    if (path.empty()) return {};
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
    return path;
}

bool HasLauncher() {
    return !LauncherExePath().empty();
}

bool OpenLauncher(bool openSettings) {
    std::wstring exe = LauncherExePath();
    if (exe.empty()) return false;

    std::wstring params = ReadLauncherValue(L"LauncherArgs");
    if (openSettings) {
        if (!params.empty()) params += L' ';
        params += L"--settings";
    }

    HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(),
                                params.empty() ? nullptr : params.c_str(),
                                nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

void LoadConfig() {
    std::wstring path = ConfigFilePath();
    if (path.empty()) return;

    std::string json;
    if (!ReadWholeFile(path, json) || json.empty()) return;

    A.tool         = ParseTool(GetStr(json, "tool", ToolName(A.tool)), A.tool);
    if (IsDrawTool(A.tool)) A.drawTool = A.tool;
    A.thicknessIdx = GetInt(json, "thicknessIdx", A.thicknessIdx,
                            0, kThicknessCount - 1);
    A.anchor       = (GetStr(json, "anchor", "right") == "left")
                     ? SideAnchor::Left : SideAnchor::Right;

    std::string col = Sub(json, "color");
    if (!col.empty()) {
        Swatch c{
            (BYTE)GetInt(col, "r", A.color.r, 0, 255),
            (BYTE)GetInt(col, "g", A.color.g, 0, 255),
            (BYTE)GetInt(col, "b", A.color.b, 0, 255)
        };
        SetPickerRGB(c);
    }

    std::string hk = Sub(json, "hotkeys");
    if (!hk.empty()) {
        ReadHotkey(hk, "toggle", C.toggle);
        ReadHotkey(hk, "undo",   C.undo);
        ReadHotkey(hk, "clear",  C.clear);
    }
}

bool SaveConfig() {
    std::wstring dir = AppDataDir();
    if (dir.empty()) return false;
    CreateDirectoryW(dir.c_str(), nullptr);

    char buf[1024];
    int n = _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "{\n"
        "  \"tool\": \"%s\",\n"
        "  \"thicknessIdx\": %d,\n"
        "  \"color\": { \"r\": %u, \"g\": %u, \"b\": %u },\n"
        "  \"anchor\": \"%s\",\n"
        "  \"hotkeys\": {\n"
        "    \"toggle\": \"%s\",\n"
        "    \"undo\": \"%s\",\n"
        "    \"clear\": \"%s\"\n"
        "  }\n"
        "}\n",
        ToolName(A.tool),
        A.thicknessIdx,
        (unsigned)A.color.r, (unsigned)A.color.g, (unsigned)A.color.b,
        A.anchor == SideAnchor::Left ? "left" : "right",
        WideToUtf8(DescribeHotkey(C.toggle)).c_str(),
        WideToUtf8(DescribeHotkey(C.undo)).c_str(),
        WideToUtf8(DescribeHotkey(C.clear)).c_str());
    if (n < 0) return false;

    std::wstring path = ConfigFilePath();
    std::wstring tmp  = path + L".tmp";
    if (!WriteWholeFile(tmp, std::string(buf, (size_t)n))) return false;
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}
