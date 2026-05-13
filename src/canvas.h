#pragma once

#include "common.h"

void CreateCanvasSurface();
void DestroyCanvasSurface();
void ClearCanvas();
void Undo();
void UpdateOverlay(const RECT* dirty);
void DrawSegment(POINT a, POINT b);
void StartStroke(POINT screenPt);
void EndStroke();
void ActivateOverlay();
void DeactivateOverlay();

LRESULT CALLBACK CanvasProc(HWND, UINT, WPARAM, LPARAM);
