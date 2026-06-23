/**
 * CYD-Chess — Chess with AI for CYD (Cheap Yellow Display)
 * Clean layout: centered board, left captured, right status, no chrome bars
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

static TFT_eSPI     tft;
static TFT_eSPI    *disp = &tft;
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include "config.h"
#include "theme.h"
#include "pieces.h"
#include "board.h"
#include "engine.h"

uint16_t g_lightSqColor = 0x1082;  // default near-black; overridden for screenshot capture

static SPIClass    touchSPI(VSPI);
static XPT2046_Touchscreen ts(TOUCH_CS);

static int16_t  tx = 0, ty = 0;
static int      g_lastTouchSq = -1;  // drag-tracking
static int      g_touchStartSq = -1; // square where touch-down happened
static bool     g_touchActive = false;

// ── Game state ───────────────────────────────────────────────────────────
static Board    g_board;
static int8_t   g_selected = -1;
static Move     g_legalMoves[256];
static int      g_numLegal = 0;
static bool     g_gameOver = false;
static int      g_gameResult = RESULT_NONE;
static int8_t   g_playerSide = 1;
static Move     g_lastMove;
static bool     g_hasLastMove = false;
static char     g_statusMsg[32] = "";
static unsigned long g_statusTime = 0;
static bool     g_aiPending = false;       // deferred AI trigger
static unsigned long g_aiPendingTime = 0;
static bool     g_flashMsg = false;
static unsigned long g_flashTime = 0;
static const char* g_flashText = "";

// ── Undo history ───────────────────────────────────────────────────────────
#define MAX_HISTORY 20
static BoardState g_history[MAX_HISTORY];
static int      g_historyCount = 0;

// ── NVS ──────────────────────────────────────────────────────────────────
static int32_t nvsGetInt(const char *key, int32_t def) {
    Preferences p; p.begin("cyd-chess", true);
    int32_t v = p.getInt(key, def); p.end();
    return v;
}
static void nvsPutInt(const char *key, int32_t val) {
    Preferences p; p.begin("cyd-chess", false);
    p.putInt(key, val); p.end();
}

static void saveGame() {
    Preferences p; p.begin("cyd-chess", false);
    p.putBytes("board", g_board.state.squares, 64);
    p.putInt("side", g_board.state.sideToMove);
    p.putInt("castle", g_board.state.castling);
    p.putInt("ep", g_board.state.epSquare);
    p.putInt("hmclock", g_board.state.halfMoveClock);
    p.putInt("fmnum", g_board.state.fullMoveNumber);
    p.putInt("gameover", g_gameOver ? 1 : 0);
    p.putInt("result", g_gameResult);
    p.end();
}

static bool loadGame() {
    Preferences p; p.begin("cyd-chess", true);
    size_t len = p.getBytesLength("board");
    if (len != 64) { p.end(); return false; }
    p.getBytes("board", g_board.state.squares, 64);
    g_board.state.sideToMove = (int8_t)p.getInt("side", 1);
    g_board.state.castling = (uint8_t)p.getInt("castle", 0xF);
    g_board.state.epSquare = (int8_t)p.getInt("ep", -1);
    g_board.state.halfMoveClock = (uint8_t)p.getInt("hmclock", 0);
    g_board.state.fullMoveNumber = (uint8_t)p.getInt("fmnum", 1);
    g_gameOver = p.getInt("gameover", 0) != 0;
    g_gameResult = p.getInt("result", 0);
    p.end();
    bool wK=false, bK=false;
    for (int i=0;i<64;i++) {
        if (g_board.state.squares[i]==KING) wK=true;
        if (g_board.state.squares[i]==-KING) bK=true;
    }
    if (!wK||!bK) { g_board.setStartPos(); g_gameOver=false; return false; }
    return true;
}

static void clearSavedGame() {
    Preferences p; p.begin("cyd-chess", false);
    p.remove("board"); p.remove("side"); p.remove("castle");
    p.remove("ep"); p.remove("hmclock"); p.remove("fmnum");
    p.remove("gameover"); p.remove("result");
    p.end();
}

// ── Touch ────────────────────────────────────────────────────────────────
static int s_touchRotation = 2;
static bool rawToScreen(int16_t *sx, int16_t *sy) {
    if (!ts.touched()) return false;
    TS_Point pt = ts.getPoint();
    *sx = map(pt.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_W-1);
    *sy = map(pt.x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_H-1, 0);
#if CYD_USB_VERSION == 2
    *sx = SCREEN_W-1-*sx; *sy = SCREEN_H-1-*sy;
#endif
    *sx = constrain(*sx,0,SCREEN_W-1); *sy = constrain(*sy,0,SCREEN_H-1);
    return true;
}
// Simple tap-to-move: each new touch on a square fires one event
// Fires on touch-down, debounced per-square (no repeats while finger stays on same square)
static int  g_touchEvent = 0;
static int  g_touchEventSq = -1;

static void pollTouch() {
    g_touchEvent = 0;
    int16_t rx, ry;
    if (!rawToScreen(&rx, &ry)) {
        g_lastTouchSq = -1;
        g_touchActive = false;
        return;
    }
    tx = rx; ty = ry;

    int sq = -1;
    if (tx>=BOARD_X && tx<BOARD_X+BOARD_W && ty>=BOARD_Y && ty<BOARD_Y+BOARD_H) {
        int f=PX_TO_FILE(tx), r=7-PX_TO_RANK(ty);
        if (f>=0&&f<8&&r>=0&&r<8) sq = SQ(f,r);
    }

    // Fire on new touch-down OR when moving to a new square
    if (!g_touchActive || sq != g_lastTouchSq) {
        g_touchActive = true;
        g_lastTouchSq = sq;
        g_touchEvent = 1;
        g_touchEventSq = sq;
    }
}
static bool touchIsHeld(int16_t *ox=nullptr, int16_t *oy=nullptr) {
    int16_t rx,ry; bool t=rawToScreen(&rx,&ry);
    if (t) { if(ox)*ox=rx; if(oy)*oy=ry; }
    return t;
}
static void touchSetRotation(int r) { s_touchRotation=r&3; nvsPutInt("touch_rot",s_touchRotation); ts.setRotation(s_touchRotation); }
static int touchGetRotation() { return s_touchRotation; }

// ── Power button ─────────────────────────────────────────────────────────
static void drawPowerButton() {
    disp->drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R,   g_themeColor);
    disp->drawCircle(PWR_BTN_X, PWR_BTN_Y, PWR_BTN_R-1, g_themeColor);
    disp->drawLine(PWR_BTN_X, PWR_BTN_Y-PWR_BTN_R+3, PWR_BTN_X, PWR_BTN_Y-1, g_themeColor);
}
static bool hitPowerButton() {
    int dx=tx-PWR_BTN_X, dy=ty-PWR_BTN_Y;
    return (dx*dx+dy*dy <= (PWR_BTN_R+4)*(PWR_BTN_R+4));
}
static void goToSleep() {
    saveGame();
    disp->fillScreen(COL_BG);
    disp->setTextFont(2); disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM); disp->drawString("SLEEP", SCREEN_W/2, SCREEN_H/2);
    delay(500);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, 0);
    esp_deep_sleep_start();
}

// ── Undo button ──────────────────────────────────────────────────────────
static void redrawAll();  // forward decl — defined below

static void drawUndoButton() {
    int cx = UNDO_BTN_X, cy = UNDO_BTN_Y, r = UNDO_BTN_R;
    // Left-pointing arrow
    int lx = cx - r;
    int rx = cx + r;
    disp->drawLine(lx, cy, rx, cy, g_themeColor);
    disp->drawLine(lx, cy, lx + 3, cy - 3, g_themeColor);
    disp->drawLine(lx, cy, lx + 3, cy + 3, g_themeColor);
}
static bool hitUndoButton() {
    int dx=tx-UNDO_BTN_X, dy=ty-UNDO_BTN_Y;
    return (dx*dx+dy*dy <= (UNDO_BTN_R+4)*(UNDO_BTN_R+4));
}
static void performUndo() {
    if (g_historyCount == 0) return;
    g_aiPending = false;
    g_gameOver = false;
    g_gameResult = RESULT_NONE;
    g_historyCount--;
    memcpy(&g_board.state, &g_history[g_historyCount], sizeof(BoardState));
    g_selected = -1;
    g_numLegal = 0;
    g_hasLastMove = false;
    redrawAll();
    saveGame();
}

// ── Board drawing ────────────────────────────────────────────────────────
static void drawBoard() {
    for (int r=0; r<8; r++)
        for (int f=0; f<8; f++) {
            int sx=BOARD_X+f*SQ_SIZE, sy=BOARD_Y+r*SQ_SIZE;
            disp->fillRect(sx, sy, SQ_SIZE, SQ_SIZE, ((f+r)&1)?COL_DARK_SQ:COL_LIGHT_SQ);
        }
    // Rounded themed border around board
    int bx=BOARD_X-BOARD_BORDER, by=BOARD_Y-BOARD_BORDER;
    int bw=BOARD_W+BOARD_BORDER*2, bh=BOARD_H+BOARD_BORDER*2;
    disp->drawRoundRect(bx, by, bw, bh, 6, g_themeColor);
    disp->drawRoundRect(bx+1, by+1, bw-2, bh-2, 6, g_themeColor);
}

static void drawAllPieces() {
    for (int sq=0; sq<64; sq++) {
        int8_t p = g_board.state.squares[sq];
        if (p==0) continue;
        int cx=SQ_CX(SQ_FILE(sq)), cy=SQ_CY(7-SQ_RANK(sq));
        drawPiece(*disp, cx, cy, p, 18);
    }
}

static void drawSquareHighlight(int file, int rank) {
    int sx=BOARD_X+file*SQ_SIZE, sy=BOARD_Y+(7-rank)*SQ_SIZE;
    disp->fillRect(sx, sy, SQ_SIZE, SQ_SIZE, ((file+(7-rank))&1)?COL_DARK_SQ:COL_LIGHT_SQ);
    disp->drawRect(sx, sy, SQ_SIZE, SQ_SIZE, COL_SELECTED);
    disp->drawRect(sx+1, sy+1, SQ_SIZE-2, SQ_SIZE-2, COL_SELECTED);
    int8_t p=g_board.state.squares[SQ(file,rank)];
    if (p) drawPiece(*disp, sx+SQ_SIZE/2, sy+SQ_SIZE/2, p, 18);
}

static void drawLegalMoveDots() {
    for (int i=0; i<g_numLegal; i++) {
        int cx=SQ_CX(SQ_FILE(g_legalMoves[i].to));
        int cy=SQ_CY(7-SQ_RANK(g_legalMoves[i].to));
        drawLegalDot(*disp, cx, cy);
    }
}

static void drawLastMoveIndicator() {
    // Disabled — was causing visual confusion
}

// ── Left panel: captured pieces (stays within x=0..BOARD_X-BOARD_BORDER-1) ─
static void drawCaptured() {
    int lx = LPANEL_X + 2;
    int lw = BOARD_X - BOARD_BORDER - 4;  // leave gap for border
    disp->fillRect(LPANEL_X, BOARD_Y, lw+2, BOARD_H, COL_BG);

    int8_t on[2][8]={{0}};
    int8_t start[8]={0,8,2,2,2,1,1,0};
    for (int sq=0; sq<64; sq++) {
        int8_t p=g_board.state.squares[sq];
        if (!p) continue;
        int8_t pt=Board::pieceType(p); if (pt<1||pt>6) continue;
        on[Board::isWhite(p)?0:1][pt]++;
    }

    int mid = lx + lw/2;
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(TC_DATUM);
    disp->drawString("TOOK", mid, BOARD_Y-2);

    int idx=0, bx=lx+5, by=BOARD_Y+18;
    for (int pt=QUEEN; pt>=PAWN; pt--) {
        int n=start[pt]-on[1][pt];
        for (int j=0; j<n && idx<12; j++) {
            drawPiece(*disp, bx+(idx%2)*16, by+(idx/2)*14, -pt, 10);
            idx++;
        }
    }
    if (idx==0) { disp->setTextColor(COL_DIM_GRAY, COL_BG); disp->drawString("-", mid, by+4); }

    int wy = BOARD_Y + BOARD_H/2 - 12;
    disp->setTextColor(g_themeColor, COL_BG);
    disp->drawString("LOST", mid, wy);
    idx=0; wy+=20;
    for (int pt=QUEEN; pt>=PAWN; pt--) {
        int n=start[pt]-on[0][pt];
        for (int j=0; j<n && idx<12; j++) {
            drawPiece(*disp, bx+(idx%2)*16, wy+(idx/2)*14, pt, 10);
            idx++;
        }
    }
    if (idx==0) { disp->setTextColor(COL_DIM_GRAY, COL_BG); disp->drawString("-", mid, wy+4); }
}

// ── Right panel: turn / status (right-aligned, starts after border) ──────
static void drawRightInfo() {
    int rx = BOARD_X + BOARD_W + BOARD_BORDER + 2;  // start after border gap
    int rw = SCREEN_W - rx;
    disp->fillRect(rx, BOARD_Y, rw, BOARD_H, COL_BG);

    int cx = rx + rw/2;
    int cy = BOARD_Y + BOARD_H/2;

    if (g_gameOver) {
        disp->setTextFont(1);
        disp->setTextColor(g_themeColor, COL_BG);
        disp->setTextDatum(MC_DATUM);
        if (g_gameResult==RESULT_WHITE_WIN) {
            disp->drawString("WHITE", cx, cy-6);
            disp->drawString("WINS", cx, cy+2);
        } else if (g_gameResult==RESULT_BLACK_WIN) {
            disp->drawString("BLACK", cx, cy-6);
            disp->drawString("WINS", cx, cy+2);
        } else {
            disp->drawString("DRAW", cx, cy);
        }
        return;
    }

    bool yourTurn = (g_board.state.sideToMove == g_playerSide);
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    if (yourTurn && g_board.inCheck()) {
        disp->drawString("CHECK", cx, cy);
    } else {
        disp->drawString(yourTurn ? "YOUR" : "AI", cx, cy-5);
        disp->drawString(yourTurn ? "TURN" : "THINK", cx, cy+5);
    }
}

// ── NEW GAME button (right panel) ───────────────────────────────────────
static void drawNewButton() {
    disp->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 4, COL_BG);
    disp->drawRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 4, g_themeColor);
    disp->drawRoundRect(BTN_X+1, BTN_Y+1, BTN_W-2, BTN_H-2, 4, g_themeColor);
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(MC_DATUM);
    disp->drawString("NEW", BTN_X+BTN_W/2, BTN_Y+BTN_H/2);
}

static bool hitNewGame() { return tx>=BTN_X&&tx<=BTN_X+BTN_W&&ty>=BTN_Y&&ty<=BTN_Y+BTN_H; }

// ── Theme icon (bottom-right) ────────────────────────────────────────────
static void drawThemeIcon() {
    disp->fillCircle(THEME_ICON_X, THEME_ICON_Y, THEME_ICON_R, g_themeColor);
    disp->fillCircle(THEME_ICON_X, THEME_ICON_Y, THEME_ICON_R-2, COL_BG);
    disp->drawCircle(THEME_ICON_X, THEME_ICON_Y, THEME_ICON_R, g_themeColor);
}
static bool hitThemeIcon() {
    int dx=tx-THEME_ICON_X, dy=ty-THEME_ICON_Y;
    return (dx*dx+dy*dy <= (THEME_ICON_R+4)*(THEME_ICON_R+4));
}

// ── AI piece color icon (bottom-left) ───────────────────────────────────
static const uint16_t AI_COLORS[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
static const int AI_COLOR_COUNT = 6;
static int g_aiColorIdx = 0;

static void drawAiColorIcon() {
    disp->fillCircle(AICOLOR_ICON_X, AICOLOR_ICON_Y, AICOLOR_ICON_R, g_aiPieceColor);
    disp->fillCircle(AICOLOR_ICON_X, AICOLOR_ICON_Y, AICOLOR_ICON_R-2, COL_BG);
    disp->drawCircle(AICOLOR_ICON_X, AICOLOR_ICON_Y, AICOLOR_ICON_R, g_aiPieceColor);
}
static bool hitAiColorIcon() {
    int dx=tx-AICOLOR_ICON_X, dy=ty-AICOLOR_ICON_Y;
    return (dx*dx+dy*dy <= (AICOLOR_ICON_R+4)*(AICOLOR_ICON_R+4));
}
static void cycleAiColor() {
    g_aiColorIdx = (g_aiColorIdx + 1) % AI_COLOR_COUNT;
    g_aiPieceColor = AI_COLORS[g_aiColorIdx];
}
static unsigned long g_themeHoldStart = 0;

// Long-press theme icon → cycle touch rotation (fixes mirroring)
static void checkThemeLongPress() {
    if (!g_touchActive || !hitThemeIcon()) { g_themeHoldStart = 0; return; }
    if (g_themeHoldStart == 0) { g_themeHoldStart = millis(); return; }
    if (millis() - g_themeHoldStart > 800) {
        g_themeHoldStart = 0;
        int rot = (touchGetRotation() + 1) % 4;
        touchSetRotation(rot);
        // Show rotation number briefly
        disp->fillRect(RPANEL_X, BOARD_Y+BOARD_H/2-20, RPANEL_W, 20, COL_BG);
        disp->setTextFont(2);
        disp->setTextColor(COL_AMBER, COL_BG);
        disp->setTextDatum(MC_DATUM);
        char buf[8]; snprintf(buf,8,"ROT:%d",rot);
        disp->drawString(buf, RPANEL_CX, BOARD_Y+BOARD_H/2-10);
        delay(600);
        drawRightInfo();
    }
}

// ── Center-screen flash message ("CHECK!", "Checkmate!") ────────────────
static void triggerFlash(const char* msg) {
    g_flashMsg = true;
    g_flashTime = millis();
    g_flashText = msg;
}

static void drawFlashMessage() {
    if (!g_flashMsg) return;
    unsigned long elapsed = millis() - g_flashTime;
    if (elapsed > 800) { g_flashMsg = false; return; }
    // Pulse: fade in, hold, fade out
    uint16_t col = g_themeColor;
    if (elapsed < 100) col = COL_DIM_GRAY;       // dim at start
    else if (elapsed > 500) col = COL_DIM_GRAY;  // dim at end
    int cx = SCREEN_W/2, cy = BOARD_Y + BOARD_H/2;
    disp->setTextFont(4);
    disp->setTextColor(col, COL_BG);
    disp->setTextDatum(MC_DATUM);
    // Clear behind text
    int tw = disp->textWidth(g_flashText);
    disp->fillRect(cx-tw/2-4, cy-12, tw+8, 24, COL_BG);
    disp->drawString(g_flashText, cx, cy);
}

// ── Check indicator ─────────────────────────────────────────────────────
static void drawCheckIndicator() {
    if (!g_board.inCheck() || g_gameOver) return;
    bool playerInCheck = (g_board.state.sideToMove == g_playerSide);
    int cy = playerInCheck ? (BOARD_Y + BOARD_H + 15) : (BOARD_Y - 10);
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(TC_DATUM);
    disp->drawString("CHECK!", SCREEN_W/2, cy);
}

static void drawCheckmateLabel() {
    if (!g_gameOver) return;
    if (g_gameResult != RESULT_WHITE_WIN && g_gameResult != RESULT_BLACK_WIN) return;
    int cy = BOARD_Y + BOARD_H + 15;
    disp->setTextFont(1);
    disp->setTextColor(g_themeColor, COL_BG);
    disp->setTextDatum(TC_DATUM);
    disp->drawString("Checkmate!", SCREEN_W/2, cy);
}

// ── Full redraw ──────────────────────────────────────────────────────────
static void redrawAll() {
    disp->fillScreen(COL_BG);
    drawBoard();
    drawLastMoveIndicator();
    if (g_selected >= 0) {
        drawSquareHighlight(SQ_FILE(g_selected), SQ_RANK(g_selected));
        drawLegalMoveDots();
    }
    drawAllPieces();
    drawCheckIndicator();
    drawCaptured();
    drawRightInfo();
    drawNewButton();
    drawThemeIcon();
    drawAiColorIcon();
    drawPowerButton();
    drawUndoButton();
    drawFlashMessage();
}

// ── Serial debug ─────────────────────────────────────────────────────────
static void debugBoard() {
    Serial.println("--- Board ---");
    for (int r=7; r>=0; r--) {
        for (int f=0; f<8; f++) { Serial.print(Board::pieceChar(g_board.state.squares[SQ(f,r)])); Serial.print(' '); }
        Serial.println();
    }
    Serial.print("Side: "); Serial.println(g_board.state.sideToMove>0?"White":"Black");
    Move mv[256]; int n = g_board.genMoves(mv, 256);
    Serial.print("Legal moves: "); Serial.println(n);
    Serial.flush();
}

// ── Clear selection (redraw affected squares) ────────────────────────────
static void clearSelection() {
    if (g_selected < 0) return;
    // Clear highlight on selected square
    int f=SQ_FILE(g_selected), r=SQ_RANK(g_selected);
    uint16_t sc=((f+(7-r))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
    disp->fillRect(BOARD_X+f*SQ_SIZE, BOARD_Y+(7-r)*SQ_SIZE, SQ_SIZE, SQ_SIZE, sc);
    if (g_hasLastMove) {
        if (g_lastMove.from==g_selected) drawLastMoveHighlight(*disp,f,7-r);
        if (g_lastMove.to==g_selected)   drawLastMoveHighlight(*disp,f,7-r);
    }
    int8_t p=g_board.state.squares[g_selected];
    if (p) drawPiece(*disp, SQ_CX(f), SQ_CY(7-r), p, 18);
    // Clear legal move dots
    for (int i=0; i<g_numLegal; i++) {
        int df=SQ_FILE(g_legalMoves[i].to), dr=SQ_RANK(g_legalMoves[i].to);
        uint16_t dsc=((df+(7-dr))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
        disp->fillRect(BOARD_X+df*SQ_SIZE, BOARD_Y+(7-dr)*SQ_SIZE, SQ_SIZE, SQ_SIZE, dsc);
        int8_t dp=g_board.state.squares[g_legalMoves[i].to];
        if (dp) drawPiece(*disp, SQ_CX(df), SQ_CY(7-dr), dp, 18);
    }
    g_selected=-1; g_numLegal=0;
}

// ── AI move ───────────────────────────────────────────────────────────────
static void doAIMove() {
    Move bestMove;
    bool engOk = engineThink(g_board, bestMove);
    Serial.print("engOk="); Serial.print(engOk);
    Serial.print(" bestMove.from="); Serial.print((int)bestMove.from);
    Serial.print(" piece there="); Serial.println((int)g_board.state.squares[bestMove.from]);
    Serial.flush();

    if (!engOk) {
        Move all[256]; int n = g_board.genMoves(all, 256);
        if (n == 0) { g_gameOver=true; redrawAll(); return; }
        bestMove = all[0];
        Serial.print("FALLBACK: from="); Serial.print((int)bestMove.from); Serial.flush();
    }

    Serial.print("AI: "); Serial.print(Board::pieceChar(g_board.state.squares[bestMove.from]));
    Serial.print(" "); Serial.print((char)('a'+SQ_FILE(bestMove.from)));
    Serial.print((char)('1'+SQ_RANK(bestMove.from)));
    Serial.print("->"); Serial.print((char)('a'+SQ_FILE(bestMove.to)));
    Serial.println((char)('1'+SQ_RANK(bestMove.to))); Serial.flush();

    g_lastMove = bestMove;
    g_hasLastMove = true;
    g_board.makeMove(bestMove);

    g_gameResult = g_board.getResult();
    if (g_gameResult != RESULT_NONE) g_gameOver = true;

    g_selected = -1; g_numLegal = 0;
    if (g_board.inCheck()) triggerFlash("CHECK!");
    if (g_gameResult==RESULT_WHITE_WIN||g_gameResult==RESULT_BLACK_WIN) triggerFlash("Checkmate!");
    redrawAll();
    saveGame();
}

// ── Player move ──────────────────────────────────────────────────────────
static void handlePlayerMove(int toSquare) {
    Move chosen; bool found=false;
    for (int i=0; i<g_numLegal; i++)
        if (g_legalMoves[i].to==toSquare) { chosen=g_legalMoves[i]; found=true; break; }
    if (!found) return;

    if (chosen.promo==0) {
        int8_t p=g_board.state.squares[chosen.from];
        int dr=SQ_RANK(chosen.to);
        if (Board::pieceType(p)==PAWN && (dr==7||dr==0)) chosen.promo=QUEEN;
    }

    // Save state for undo before making the move
    if (g_historyCount < MAX_HISTORY) {
        memcpy(&g_history[g_historyCount], &g_board.state, sizeof(BoardState));
        g_historyCount++;
    }

    bool ok = g_board.makeMove(chosen);
    Serial.print("Player: ");
    Serial.print((char)('a'+SQ_FILE(chosen.from)));
    Serial.print((char)('1'+SQ_RANK(chosen.from)));
    Serial.print("->");
    Serial.print((char)('a'+SQ_FILE(chosen.to)));
    Serial.print((char)('1'+SQ_RANK(chosen.to)));
    Serial.print(" ok="); Serial.println(ok);
    Serial.flush();

    if (!ok) {
        Serial.println("MOVE REJECTED!"); Serial.flush();
        g_selected=-1; g_numLegal=0;
        return;  // don't flip side, don't trigger AI, don't redraw
    }

    g_lastMove=chosen; g_hasLastMove=true;
    g_selected=-1; g_numLegal=0;
    g_gameResult=g_board.getResult();
    if (g_gameResult!=RESULT_NONE) g_gameOver=true;

    if (g_board.inCheck()) triggerFlash("CHECK!");
    if (g_gameResult==RESULT_WHITE_WIN||g_gameResult==RESULT_BLACK_WIN) triggerFlash("Checkmate!");
    redrawAll();
    saveGame();
}

// ── Square selection ─────────────────────────────────────────────────────
static void selectSquare(int sq) {
    int8_t piece=g_board.state.squares[sq];

    if (g_selected<0) {
        if (piece!=0 && (piece*g_playerSide)>0) {
            g_selected=sq;
            Move all[256]; int n=g_board.genMoves(all,256);
            g_numLegal=0;
            for (int i=0; i<n; i++) if (all[i].from==sq) g_legalMoves[g_numLegal++]=all[i];
            drawSquareHighlight(SQ_FILE(sq), SQ_RANK(sq));
            drawLegalMoveDots();
        }
    } else if (sq==g_selected) {
        int f=SQ_FILE(sq), r=SQ_RANK(sq);
        uint16_t sc=((f+(7-r))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
        disp->fillRect(BOARD_X+f*SQ_SIZE, BOARD_Y+(7-r)*SQ_SIZE, SQ_SIZE, SQ_SIZE, sc);
        if (g_hasLastMove) {
            if (g_lastMove.from==sq) drawLastMoveHighlight(*disp,f,7-r);
            if (g_lastMove.to==sq)   drawLastMoveHighlight(*disp,f,7-r);
        }
        if (piece) drawPiece(*disp, SQ_CX(f), SQ_CY(7-r), piece, 18);
        g_selected=-1; g_numLegal=0;
    } else if (piece!=0 && (piece*g_playerSide)>0) {
        // Switch selection — clear old legal-move dots first
        for (int i=0; i<g_numLegal; i++) {
            int df=SQ_FILE(g_legalMoves[i].to), dr=SQ_RANK(g_legalMoves[i].to);
            uint16_t dsc=((df+(7-dr))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
            disp->fillRect(BOARD_X+df*SQ_SIZE, BOARD_Y+(7-dr)*SQ_SIZE, SQ_SIZE, SQ_SIZE, dsc);
            int8_t dp=g_board.state.squares[g_legalMoves[i].to];
            if (dp) drawPiece(*disp, SQ_CX(df), SQ_CY(7-dr), dp, 18);
        }
        // Clear old selected square highlight
        int of=SQ_FILE(g_selected), or_=SQ_RANK(g_selected);
        uint16_t sc=((of+(7-or_))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
        disp->fillRect(BOARD_X+of*SQ_SIZE, BOARD_Y+(7-or_)*SQ_SIZE, SQ_SIZE, SQ_SIZE, sc);
        if (g_hasLastMove) {
            if (g_lastMove.from==g_selected) drawLastMoveHighlight(*disp,of,7-or_);
            if (g_lastMove.to==g_selected)   drawLastMoveHighlight(*disp,of,7-or_);
        }
        int8_t op=g_board.state.squares[g_selected];
        if (op) drawPiece(*disp, SQ_CX(of), SQ_CY(7-or_), op, 18);
        // Set new selection
        g_selected=sq;
        Move all[256]; int n=g_board.genMoves(all,256);
        g_numLegal=0;
        for (int i=0; i<n; i++) if (all[i].from==sq) g_legalMoves[g_numLegal++]=all[i];
        drawSquareHighlight(SQ_FILE(sq), SQ_RANK(sq));
        drawLegalMoveDots();
    } else {
        bool legal=false;
        for (int i=0; i<g_numLegal; i++) if (g_legalMoves[i].to==sq) { legal=true; break; }
        if (legal) { handlePlayerMove(sq); }
        else {
            int of=SQ_FILE(g_selected), or_=SQ_RANK(g_selected);
            uint16_t sc=((of+(7-or_))&1)?COL_DARK_SQ:COL_LIGHT_SQ;
            disp->fillRect(BOARD_X+of*SQ_SIZE, BOARD_Y+(7-or_)*SQ_SIZE, SQ_SIZE, SQ_SIZE, sc);
            if (g_hasLastMove) {
                if (g_lastMove.from==g_selected) drawLastMoveHighlight(*disp,of,7-or_);
                if (g_lastMove.to==g_selected)   drawLastMoveHighlight(*disp,of,7-or_);
            }
            int8_t op=g_board.state.squares[g_selected];
            if (op) drawPiece(*disp, SQ_CX(of), SQ_CY(7-or_), op, 18);
            g_selected=-1; g_numLegal=0;
        }
    }
}

static bool hitBoard(int &sq) {
    if (tx>=BOARD_X && tx<BOARD_X+BOARD_W && ty>=BOARD_Y && ty<BOARD_Y+BOARD_H) {
        int f=PX_TO_FILE(tx), r=7-PX_TO_RANK(ty);
        if (f>=0&&f<8&&r>=0&&r<8) { sq=SQ(f,r);
            Serial.print("Touch: xy=("); Serial.print(tx); Serial.print(","); Serial.print(ty);
            Serial.print(") -> "); Serial.print((char)('a'+f)); Serial.println((char)('1'+r));
            return true; }
    }
    return false;
}

// ── Serial capture ───────────────────────────────────────────────────────
static void handleSerialCapture() {
    TFT_eSprite spr(&tft); spr.setColorDepth(8);
    uint8_t *fb=(uint8_t*)spr.createSprite(SCREEN_W,SCREEN_H);
    if (!fb) { Serial.print("OOM:"); Serial.println(ESP.getMaxAllocHeap()); return; }
    // Temporarily brighten light squares so they survive 16-bit→8-bit RGB332
    // truncation.  0x1082 (R=1,G=2,B=2) maps to 0x00 in 8-bit — invisible.
    // 0x2108 (R=4,G=8,B=8) is the darkest gray with all channels ≥1 in RGB332.
    auto oldLight = g_lightSqColor;
    g_lightSqColor = 0x2108;
    auto *pv=disp; disp=&spr; redrawAll(); disp=pv;
    g_lightSqColor = oldLight;
    Serial.print("RGB332:"); Serial.write(fb,SCREEN_W*SCREEN_H); Serial.flush();
    spr.deleteSprite();
}

// ── Calibration (ported from CYD-Poker) ──────────────────────────────────
#define CURRENT_CAL_VER 1
static uint8_t s_madctl=0x80;

static void applyOrientation() {
#if CYD_USB_VERSION==2
    tft.setRotation(1); tft.writecommand(TFT_MADCTL); tft.writedata(s_madctl);
#else
    tft.setRotation(1);
#endif
}
static uint8_t madctlForCombo(int idx) {
    switch(idx&3) {
        case 0: return TFT_MAD_MV|TFT_MAD_BGR;
        case 1: return TFT_MAD_MV|TFT_MAD_MY|TFT_MAD_BGR;
        case 2: return 0x00;
        default: return TFT_MAD_MY;
    }
}

static void displayCalibrate() {
#if CYD_USB_VERSION==2
    if (nvsGetInt("cal_ver",-1)>=CURRENT_CAL_VER) { s_madctl=(uint8_t)nvsGetInt("madctl",0x80); return; }
    s_madctl=madctlForCombo(0); digitalWrite(TFT_BL,HIGH);
    auto d=[&]() {
        disp->fillScreen(COL_BG); applyOrientation(); disp->fillScreen(COL_BG);
        disp->fillTriangle(2,2,60,2,2,60,COL_AMBER);
        disp->fillTriangle(4,4,56,4,4,56,COL_BG);
        disp->fillTriangle(2,2,60,2,2,60,COL_AMBER);
        disp->fillRect(SCREEN_W-50,2,48,8,g_themeColor);
        disp->fillRect(SCREEN_W-8,2,6,48,g_themeColor);
        disp->fillCircle(24,SCREEN_H-24,20,COL_AMBER);
        disp->fillCircle(24,SCREEN_H-24,16,COL_BG);
        disp->fillCircle(24,SCREEN_H-24,20,COL_AMBER);
        disp->drawLine(SCREEN_W-40,SCREEN_H-24,SCREEN_W-8,SCREEN_H-24,g_themeColor);
        disp->drawLine(SCREEN_W-24,SCREEN_H-40,SCREEN_W-24,SCREEN_H-8,g_themeColor);
        disp->drawCircle(SCREEN_W-24,SCREEN_H-24,14,g_themeColor);
        disp->fillRect(SCREEN_W/2-16,SCREEN_H/2-24,32,6,COL_WHITE);
        disp->fillRect(SCREEN_W/2-4,SCREEN_H/2-24,8,48,COL_WHITE);
        int idx;
        if (s_madctl==(TFT_MAD_MV|TFT_MAD_BGR)) idx=0;
        else if (s_madctl==(TFT_MAD_MV|TFT_MAD_MY|TFT_MAD_BGR)) idx=1;
        else if (s_madctl==0x00) idx=2; else idx=3;
        disp->setTextFont(4); disp->setTextColor(g_themeColor,COL_BG);
        char b[16]; snprintf(b,sizeof(b),"MODE %d",idx);
        int tw=disp->textWidth(b); disp->setCursor((SCREEN_W-tw)/2,68); disp->print(b);
        disp->setTextFont(2); disp->setTextColor(COL_WHITE,COL_BG);
        const char *m="Tap to change"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H-72); disp->print(m);
        disp->setTextFont(1); disp->setTextColor(COL_DIM_GRAY,COL_BG);
        m="Hold 2s to confirm"; tw=disp->textWidth(m);
        disp->setCursor((SCREEN_W-tw)/2,SCREEN_H-52); disp->print(m);
    };
    d();
    { unsigned long hs=0; bool wt=false; int cc=0;
        while(true) {
            bool nt=touchIsHeld();
            if (nt&&!wt) hs=millis();
            else if (!nt&&wt&&hs>0) { if (millis()-hs<1200) { cc=(cc+1)&3; s_madctl=madctlForCombo(cc); d(); } }
            if (nt&&wt&&hs>0&&millis()-hs>=2000) break;
            wt=nt; delay(30);
        }
        while(touchIsHeld()) delay(30); delay(200);
    }
    nvsPutInt("madctl",s_madctl);
#endif
}

static void touchCalibrate() {
#if CYD_USB_VERSION==2
    if (nvsGetInt("cal_ver",-1)>=CURRENT_CAL_VER) return;
    digitalWrite(TFT_BL,HIGH);
    auto d=[&]() {
        disp->fillScreen(COL_BG);
        disp->setTextFont(4); disp->setTextColor(g_themeColor,COL_BG);
        char b[4]; snprintf(b,sizeof(b),"%d",touchGetRotation());
        int tw=disp->textWidth(b); disp->setTextDatum(TC_DATUM);
        disp->drawString(b,SCREEN_W/2,SCREEN_H/2-40);
        disp->setTextFont(2); disp->setTextColor(COL_WHITE,COL_BG);
        const char *m="Tap to cycle touch"; tw=disp->textWidth(m);
        disp->setTextDatum(TC_DATUM); disp->drawString(m,SCREEN_W/2,SCREEN_H/2);
        disp->setTextFont(1); disp->setTextColor(COL_DIM_GRAY,COL_BG);
        m="Hold 2s to confirm"; tw=disp->textWidth(m);
        disp->setTextDatum(TC_DATUM); disp->drawString(m,SCREEN_W/2,SCREEN_H/2+30);
        const int CX=14,CY=14,CS=18; uint16_t tc=COL_DIM_GRAY;
        disp->drawRect(CX,CY,CS,CS,tc); disp->drawLine(CX,CY,CX+CS,CY+CS,tc);
        disp->drawLine(CX,CY+CS,CX+CS,CY,tc);
        disp->drawRect(SCREEN_W-CX-CS,CY,CS,CS,tc);
        disp->drawLine(SCREEN_W-CX-CS,CY,SCREEN_W-CX,CY+CS,tc);
        disp->drawLine(SCREEN_W-CX-CS,CY+CS,SCREEN_W-CX,CY,tc);
        disp->drawRect(CX,SCREEN_H-CY-CS,CS,CS,tc);
        disp->drawLine(CX,SCREEN_H-CY,CX+CS,SCREEN_H-CY-CS,tc);
        disp->drawLine(CX,SCREEN_H-CY-CS,CX+CS,SCREEN_H-CY,tc);
        disp->drawRect(SCREEN_W-CX-CS,SCREEN_H-CY-CS,CS,CS,tc);
        disp->drawLine(SCREEN_W-CX,SCREEN_H-CY,SCREEN_W-CX-CS,SCREEN_H-CY-CS,tc);
        disp->drawLine(SCREEN_W-CX-CS,SCREEN_H-CY,SCREEN_W-CX,SCREEN_H-CY-CS,tc);
    };
    d();
    unsigned long hs=0; bool wt=false; int cx=-1,cy=-1,lx=-1,ly=-1; bool dirty=false;
    while(true) {
        int16_t ttx,tty; bool nt=touchIsHeld(&ttx,&tty);
        if (nt) { cx=ttx; cy=tty; }
        if (nt&&!wt) { hs=millis(); lx=cx; ly=cy; dirty=true; }
        else if (!nt&&wt&&hs>0) {
            if (millis()-hs<1200) { touchSetRotation((touchGetRotation()+1)%4); d(); }
            if (lx>=0) disp->fillCircle(lx,ly,7,COL_BG); lx=ly=-1; dirty=false;
        }
        else if (nt&&wt&&hs>0&&millis()-hs>=2000) { if(lx>=0) disp->fillCircle(lx,ly,7,COL_BG); break; }
        if (nt&&dirty&&(cx!=lx||cy!=ly)) { if(lx>=0) disp->fillCircle(lx,ly,7,COL_BG); disp->fillCircle(cx,cy,6,COL_AMBER); disp->drawCircle(cx,cy,6,COL_WHITE); lx=cx; ly=cy; }
        wt=nt; delay(30);
    }
    while(touchIsHeld()) delay(30); delay(200);
    nvsPutInt("cal_ver",CURRENT_CAL_VER); nvsPutInt("touch_cal",1);
#endif
}

// ── Splash ───────────────────────────────────────────────────────────────
static void showSplash() {
    disp->fillScreen(COL_BG);

    // Outline-only pieces in a row — tighter, moved down
    int cy = 65;
    int sp=48, sx0=(SCREEN_W-sp*3)/2;
    drawPieceOutline(*disp, sx0,      cy, ROOK,  40);
    drawPieceOutline(*disp, sx0+sp,   cy, KING,  42);
    drawPieceOutline(*disp, sx0+sp*2, cy, QUEEN, 42);
    drawPieceOutline(*disp, sx0+sp*3, cy, ROOK,  40);

    // Brand text below pieces
    disp->setTextFont(4);
    disp->setTextColor(g_themeColor, COL_BG);
    int tw = disp->textWidth("xXMayDayXx");
    disp->setCursor((SCREEN_W-tw)/2, cy+50);
    disp->print("xXMayDayXx");

    disp->setTextFont(4);
    disp->setTextColor(COL_WHITE, COL_BG);
    tw = disp->textWidth("xXCYD-ChessXx");
    disp->setCursor((SCREEN_W-tw)/2, cy+80);
    disp->print("xXCYD-ChessXx");

    disp->setTextFont(2);
    disp->setTextColor(g_themeColor, COL_BG);
    tw = disp->textWidth("xXQuantum-SmokeXx");
    disp->setCursor((SCREEN_W-tw)/2, cy+110);
    disp->print("xXQuantum-SmokeXx");

    disp->setTextFont(1);
    disp->setTextColor(COL_DIM_GRAY, COL_BG);
    tw = disp->textWidth("Loading...");
    disp->setCursor((SCREEN_W-tw)/2, SCREEN_H-14);
    disp->print("Loading...");
}

// ── Setup ────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    randomSeed(analogRead(34)+micros());

    themeInit();
    g_playerSide=1;

    // Try to resume saved game (from sleep / previous session)
    bool hasSavedGame = loadGame();

    tft.init();
#if CYD_USB_VERSION==2
    s_madctl=(uint8_t)nvsGetInt("madctl",0x80); applyOrientation();
#else
    tft.setRotation(1);
#endif
    disp->fillScreen(COL_BG);

    touchSPI.begin(TOUCH_SCLK,TOUCH_MISO,TOUCH_MOSI,TOUCH_CS);
    ts.begin(touchSPI);
#if CYD_USB_VERSION==2
    { Preferences p; p.begin("cyd-chess",true);
        int rot=p.getInt("touch_rot",-1);
        if (rot<0) { int oc=p.getInt("touch_cal",0); if(!oc) oc=p.getInt("touch_flip",1); rot=oc?2:0; }
        s_touchRotation=rot; p.end(); }
    ts.setRotation(s_touchRotation);
#else
    ts.setRotation(0);
#endif

    displayCalibrate(); applyOrientation(); touchCalibrate();

    if (!hasSavedGame) {
        // Fresh boot — show splash and start new game
        g_board.setStartPos();
        g_gameOver = false;
        g_gameResult = RESULT_NONE;
        showSplash();
        delay(3000);
    }
    // else: resumed saved game — skip splash, go straight to board

    pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,HIGH);

    redrawAll();
    Serial.println("CYD-Chess ready");
    debugBoard();
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
    // ── Serial ─────────────────────────────────────────────────────
    if (Serial.available()) {
        int cmd=Serial.read();
        if (cmd=='R'||cmd=='r') Serial.println("READY");
        else if (cmd=='S'||cmd=='s') handleSerialCapture();
        else if (cmd>='0'&&cmd<='9') redrawAll();
        else if (cmd=='D'||cmd=='d') debugBoard();
        else if (cmd=='Q'||cmd=='q') {
            int rot=(touchGetRotation()+1)%4; touchSetRotation(rot);
            Serial.print("Touch rot: "); Serial.println(rot);
        }
#if CYD_USB_VERSION==2
        else if (cmd=='M'||cmd=='m') {
            int cur=3;
            if (s_madctl==(TFT_MAD_MV|TFT_MAD_BGR)) cur=0;
            else if (s_madctl==(TFT_MAD_MV|TFT_MAD_MY|TFT_MAD_BGR)) cur=1;
            else if (s_madctl==0x00) cur=2;
            cur=(cur+1)&3; s_madctl=madctlForCombo(cur);
            nvsPutInt("madctl",s_madctl); nvsPutInt("cal_ver",CURRENT_CAL_VER);
            applyOrientation(); Serial.print("MADCTL_MODE:"); Serial.println(cur); redrawAll();
        }
        else if (cmd=='T'||cmd=='t') {
            int rot=(touchGetRotation()+1)%4; touchSetRotation(rot);
            nvsPutInt("touch_cal",1); Serial.print("TOUCH_ROT:"); Serial.println(rot);
        }
#endif
    }

    pollTouch();

    // ── Buttons ────────────────────────────────────────────────────
    if (g_touchEvent == 1) {
        if (hitPowerButton()) { goToSleep(); return; }
        if (hitThemeIcon()) { themeNext(); redrawAll(); delay(300); return; }
        if (hitAiColorIcon()) { cycleAiColor(); redrawAll(); delay(300); return; }
        if (hitNewGame()) {
            g_board.setStartPos(); g_selected=-1; g_numLegal=0;
            g_gameOver=false; g_gameResult=RESULT_NONE; g_hasLastMove=false;
            g_historyCount=0;
            g_statusMsg[0]=0; clearSavedGame(); redrawAll();
            Serial.println("New game"); delay(300); return;
        }
        if (hitUndoButton()) {
            if (g_historyCount > 0) { performUndo(); Serial.println("Undo"); }
            delay(200); return;
        }
    }

    // ── Board taps — simple select-then-move ───────────────────────
    if (g_touchEvent == 1 && g_touchEventSq >= 0) {
        int sq = g_touchEventSq;
        int8_t p = g_board.state.squares[sq];

        if (g_gameOver || g_board.state.sideToMove != g_playerSide) return;

        if (g_selected < 0) {
            // Nothing selected → select if player's piece
            if (p != 0 && (p * g_playerSide) > 0) {
                g_selected = sq;
                Move all[256]; int n=g_board.genMoves(all,256);
                g_numLegal=0;
                for (int i=0; i<n; i++) if (all[i].from==sq) g_legalMoves[g_numLegal++]=all[i];
                drawSquareHighlight(SQ_FILE(sq), SQ_RANK(sq));
                drawLegalMoveDots();
            }
        } else if (sq == g_selected) {
            // Tap same piece → deselect
            clearSelection();
        } else if (p != 0 && (p * g_playerSide) > 0) {
            // Tap different friendly piece → switch selection
            clearSelection();
            g_selected = sq;
            Move all[256]; int n=g_board.genMoves(all,256);
            g_numLegal=0;
            for (int i=0; i<n; i++) if (all[i].from==sq) g_legalMoves[g_numLegal++]=all[i];
            drawSquareHighlight(SQ_FILE(sq), SQ_RANK(sq));
            drawLegalMoveDots();
        } else {
            // Tap empty/enemy → check if legal move destination
            bool legal=false;
            for (int i=0; i<g_numLegal; i++) if (g_legalMoves[i].to==sq) { legal=true; break; }
            if (legal) {
                handlePlayerMove(sq);
                if (g_selected<0 && !g_gameOver && g_board.state.sideToMove!=g_playerSide) {
                    g_aiPending = true;
                    g_aiPendingTime = millis() + 80;
                }
            } else {
                clearSelection();
            }
        }
    }

    // ── Theme long-press → cycle touch rotation ────────────────────
    checkThemeLongPress();

    // ── AI move (fires after timer) ─────────────────────────────────
    if (g_aiPending && millis() > g_aiPendingTime) {
        g_aiPending = false;
        doAIMove();
    }

    delay(30);
}
