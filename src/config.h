#pragma once

// ── Hardware version ──────────────────────────────────────────────────────
#ifndef CYD_USB_VERSION
#define CYD_USB_VERSION  1
#endif

// ── Display (ILI9341 on HSPI) ─────────────────────────────────────────────
#define TFT_MOSI   13
#define TFT_MISO   12
#define TFT_SCLK   14
#define TFT_CS     15
#define TFT_DC      2
#define TFT_RST    -1
#define TFT_BL     21

// ── Touch (XPT2046 on VSPI) ───────────────────────────────────────────────
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_SCLK 25
#define TOUCH_X_MIN  300
#define TOUCH_X_MAX 3900
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX 3800

// ── Screen ────────────────────────────────────────────────────────────────
#define SCREEN_W   320
#define SCREEN_H   240

// ── Board: 29px squares → 232×232, centered ──────────────────────────────
#define SQ_SIZE     29
#define BOARD_W     (8 * SQ_SIZE)   // 232
#define BOARD_H     (8 * SQ_SIZE)   // 232
#define BOARD_X     ((SCREEN_W - BOARD_W) / 2 + 2)  // 46
#define BOARD_Y     ((SCREEN_H - BOARD_H) / 2)  // 4
#define BOARD_BORDER 3

// Square center coordinates (screenRank = 7 - chessRank)
#define SQ_CX(file)  (BOARD_X + (file) * SQ_SIZE + SQ_SIZE / 2)
#define SQ_CY(sr)    (BOARD_Y + (sr) * SQ_SIZE + SQ_SIZE / 2)

// Pixel to board square
#define PX_TO_FILE(px)  (((px) - BOARD_X) / SQ_SIZE)
#define PX_TO_RANK(py)  (((py) - BOARD_Y) / SQ_SIZE)

// ── Left panel (captured pieces) ──────────────────────────────────────────
#define LPANEL_X    0
#define LPANEL_W    BOARD_X             // 72
#define LPANEL_CX   (LPANEL_W / 2)      // 36

// ── Right panel (turn indicator, status) ─────────────────────────────────
#define RPANEL_X    (BOARD_X + BOARD_W)  // 248
#define RPANEL_W    (SCREEN_W - RPANEL_X) // 72
#define RPANEL_CX   (RPANEL_X + RPANEL_W / 2) // 284

// ── Power button (top-right) ──────────────────────────────────────────────
#define PWR_BTN_X   306
#define PWR_BTN_Y   10
#define PWR_BTN_R   7

// ── NEW GAME button (right panel, under turn indicator) ──────────────────
#define BTN_W       36
#define BTN_H       20
#define BTN_RX      (BOARD_X + BOARD_W + BOARD_BORDER + 2)  // right panel start
#define BTN_X       (BTN_RX + (SCREEN_W - BTN_RX - BTN_W)/2)
#define BTN_Y       (BOARD_Y + BOARD_H/2 + 26)

// Theme icon (bottom-right corner, like power button style)
#define THEME_ICON_X  306
#define THEME_ICON_Y  228
#define THEME_ICON_R  7

// AI piece color icon (bottom-left corner)
#define AICOLOR_ICON_X  14
#define AICOLOR_ICON_Y  228
#define AICOLOR_ICON_R  7

// ── Colors (RGB565) ───────────────────────────────────────────────────────
#define COL_BG          0x0000
#define COL_WHITE       0xFFFF
#define COL_RED         0xF800
#define COL_GREEN       0x07E0
#define COL_GOLD        0xFDA0
#define COL_LIGHT_GRAY  0xC618
#define COL_MID_GRAY    0x8410
#define COL_DIM_GRAY    0x4208
#define COL_AMBER       0xFD40u

// Board squares — very dark
#define COL_LIGHT_SQ    0x1082    // dark gray (was 0x2104)
#define COL_DARK_SQ     0x0000    // pure black
#define COL_SELECTED    0xA124    // amber
#define COL_LEGAL_MOVE  0x2C44
#define COL_LAST_MOVE   0x7E8C

// Pieces — themed outline on both, white fill = theme color
#define COL_BPIECE_FILL 0x1082    // black pieces: near-black
// White piece fill = g_themeColor (set at runtime)

// ── Engine ────────────────────────────────────────────────────────────────
#define AI_THINK_DELAY  200
