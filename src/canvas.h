#pragma once

#include "common.h"

void CreateCanvasSurface();
void DestroyCanvasSurface();
void ClearCanvas();
void Undo();
void UpdateOverlay(const RECT* dirty);
void DrawSegment(POINT a, POINT b);
void DrawSmoothStep(POINT newPt);
void StartStroke(POINT screenPt);
void EndStroke();
void ActivateOverlay();
void DeactivateOverlay();

void BeginTextEdit(POINT screenPt, const std::wstring* initial = nullptr);
void CommitTextEdit();
void CancelTextEdit();
void RedrawTextEditIfActive();
bool TryPickUpTextBox(POINT localPt);
bool HitTestTextBox(POINT localPt);

LRESULT CALLBACK CanvasProc(HWND, UINT, WPARAM, LPARAM);
