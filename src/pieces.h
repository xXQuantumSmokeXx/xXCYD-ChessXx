#pragma once
#include <TFT_eSPI.h>
#include "board.h"

extern uint16_t g_aiPieceColor;  // separate color for AI pieces

// Draw a filled chess piece centered at (cx, cy). size = height in pixels.
void drawPiece(TFT_eSPI &tft, int cx, int cy, int8_t piece, int size = 20);

// Outline-only version (for boot logo)
void drawPieceOutline(TFT_eSPI &tft, int cx, int cy, int8_t piece, int size = 20);

// Small icon for captured piece display (~12px)
void drawPieceSmall(TFT_eSPI &tft, int cx, int cy, int8_t piece);

// UI indicators
void drawLegalDot(TFT_eSPI &tft, int cx, int cy);
void drawLastMoveHighlight(TFT_eSPI &tft, int file, int rank);
void drawSelectedHighlight(TFT_eSPI &tft, int file, int rank);
