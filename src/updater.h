#pragma once

#include "common.h"

inline constexpr WPARAM kUpdateNone       = 0;
inline constexpr WPARAM kUpdateAvailable  = 1;
inline constexpr WPARAM kUpdateDownloaded = 2;
inline constexpr WPARAM kUpdateFailed     = 3;

void StartUpdateCheck(bool manual);
void StartUpdateDownload();

bool           IsUpdateAvailable();
bool           HasInstallerUrl();
const wchar_t* LatestVersionTag();
const wchar_t* DownloadedInstallerPath();
const wchar_t* ReleasesPageUrl();
bool           IsManualCheck();
