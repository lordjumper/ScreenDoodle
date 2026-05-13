#pragma once

#include "common.h"

bool IsAutoStartEnabled();
bool SetAutoStart(bool enabled);

void OpenSettingsWindow();

LRESULT CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
