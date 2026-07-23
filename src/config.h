#pragma once

#include "common.h"

struct HotkeySpec {
    UINT mods = 0;
    UINT vk   = 0;
};

struct AppConfig {
    HotkeySpec toggle{MOD_CONTROL | MOD_ALT,   'D'};
    HotkeySpec undo  {MOD_CONTROL | MOD_SHIFT, 'Z'};
    HotkeySpec clear {MOD_CONTROL | MOD_SHIFT, 'X'};
};

extern AppConfig C;

std::wstring ConfigFilePath();

void LoadConfig();

bool SaveConfig();

std::wstring DescribeHotkey(const HotkeySpec& hk);

std::wstring LauncherExePath();

bool HasLauncher();

bool OpenLauncher(bool openSettings);
