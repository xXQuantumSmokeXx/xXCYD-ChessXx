#pragma once
#include "board.h"

// ── Engine configuration ─────────────────────────────────────────────────
#define ENGINE_MAX_DEPTH    2     // 2-ply — fast, stable on ESP32
#define INF                 30000  // "infinite" score

// ── Engine entry points ──────────────────────────────────────────────────

// Find the best move for the current side to move.
// Returns true if a move was found, false if no legal moves exist.
bool engineThink(const Board &board, Move &bestMove);

// Static position evaluation (centipawns from white's perspective).
// Positive = white advantage, negative = black advantage.
int engineEvaluate(const Board &board);
