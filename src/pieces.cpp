#include <Arduino.h>
#include "pieces.h"
#include "config.h"
#include "theme.h"
#include "piece_bmp.h"

uint16_t g_aiPieceColor = 0x8010;  // default purple

// Map piece type to bitmap array
static const uint16_t* getBmp(int8_t pt) {
    switch (pt) {
        case KING:   return b_king;
        case QUEEN:  return b_queen;
        case ROOK:   return b_rook;
        case BISHOP: return b_bishop;
        case KNIGHT: return b_knight;
        case PAWN:   return b_pawn;
        default:     return nullptr;
    }
}

// Render 30×30 bitmap centered at (cx,cy), scaled to fit "size" pixels tall.
// 0xffff = transparent. All other pixels drawn in "color".
static void drawFromBmp(TFT_eSPI &tft, int cx, int cy, int size, uint16_t color, const uint16_t *bmp) {
    if (!bmp) return;
    float scale = (float)size / 30.0f;
    int sw = (int)(30.0f * scale);
    int x0 = cx - sw/2;
    int y0 = cy - size/2;

    for (int row=0; row<30; row++) {
        for (int col=0; col<30; col++) {
            uint16_t px = bmp[row*30 + col];
            if (px == 0xffff) continue; // transparent
            int sx = x0 + (int)(col * scale);
            int sy = y0 + (int)(row * scale);
            int pw = (int)((col+1)*scale) - (int)(col*scale);
            int ph = (int)((row+1)*scale) - (int)(row*scale);
            if (pw<1) pw=1; if (ph<1) ph=1;
            tft.fillRect(sx, sy, pw, ph, color);
        }
    }
}

// ── Public ───────────────────────────────────────────────────────────────
void drawPiece(TFT_eSPI &tft, int cx, int cy, int8_t piece, int size) {
    if (piece==0) return;
    uint16_t outline = Board::isWhite(piece) ? g_themeColor : g_aiPieceColor;
    const uint16_t *bmp = getBmp(Board::pieceType(piece));
    // 3px outline for board pieces
    drawFromBmp(tft, cx, cy, size+6, outline, bmp);
    drawFromBmp(tft, cx, cy, size, 0x0000, bmp);
}

void drawPieceOutline(TFT_eSPI &tft, int cx, int cy, int8_t piece, int size) {
    if (piece==0) return;
    uint16_t outline = Board::isWhite(piece) ? g_themeColor : g_aiPieceColor;
    const uint16_t *bmp = getBmp(Board::pieceType(piece));
    // 4px outline for boot screen pieces
    drawFromBmp(tft, cx, cy, size+8, outline, bmp);
    drawFromBmp(tft, cx, cy, size, 0x0000, bmp);
}

void drawPieceSmall(TFT_eSPI &tft, int cx, int cy, int8_t piece) {
    drawPiece(tft, cx, cy, piece, 10);
}

void drawLegalDot(TFT_eSPI &tft, int cx, int cy) {
    tft.fillCircle(cx, cy, 4, COL_LEGAL_MOVE);
    tft.drawCircle(cx, cy, 4, g_themeColor);
}

void drawLastMoveHighlight(TFT_eSPI &tft, int file, int rank) {
    int sx=BOARD_X+file*SQ_SIZE+1, sy=BOARD_Y+(7-rank)*SQ_SIZE+1;
    tft.drawRect(sx, sy, SQ_SIZE-2, SQ_SIZE-2, COL_LAST_MOVE);
}

void drawSelectedHighlight(TFT_eSPI &tft, int file, int rank) {
    int sx=BOARD_X+file*SQ_SIZE, sy=BOARD_Y+(7-rank)*SQ_SIZE;
    tft.drawRect(sx, sy, SQ_SIZE, SQ_SIZE, COL_SELECTED);
    tft.drawRect(sx+1, sy+1, SQ_SIZE-2, SQ_SIZE-2, COL_SELECTED);
}
