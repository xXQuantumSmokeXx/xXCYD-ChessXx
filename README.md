# CYD-Chess

Full chess game with AI opponent for the ESP32 CYD (Cheap Yellow Display). Built on the same hardware layer as [CYD-Poker](https://github.com/xXQuantumSmokeXx/CYD-Poker).

## Features

- **Complete chess rules** — castling, en passant, pawn promotion
- **AI opponent** — minimax with alpha-beta pruning, material + piece-square evaluation
- **Touch-based play** — tap to select, tap destination to move
- **30×30 pixel-art pieces** — real chess piece bitmaps rendered as themed outlines with black faces
- **Dual theme system** — 9 accent colors for the board/UI, 6 independent colors for AI pieces
- **Game persistence** — board state survives power cycles via NVS
- **Captured pieces display** — left panel shows taken/lost pieces
- **Check/Checkmate flash** — center-screen pulse animation
- **2USB calibration** — interactive display + touch calibration on first boot
- **Serial screenshot capture** — RGB332 protocol
- **Deep sleep** — tap power button (top-right) to sleep, touch to wake

## Hardware

- ESP32-32E (1-USB) and 2USB CYD variants
- ILI9341 TFT display (320×240 landscape)
- XPT2046 touch controller
- Built with PlatformIO + TFT_eSPI

## Flash

| Board | Firmware |
|-------|----------|
| ESP32-32E (1-USB) | `CYD-Chess-1usb.bin` |
| 2USB | `CYD-Chess-2usb.bin` |

Merged images — flash at offset `0x00`:
```bash
esptool.py --chip esp32 write_flash 0x0 CYD-Chess-1usb.bin
```

Or use [M5Launcher](https://github.com/bmorcelli/M5Launcher) from SD card.

## Build

```bash
pio run -e cyd_chess        # 1-USB
pio run -e cyd_chess_2usb   # 2-USB
```

## How to Play

- You play **White** (bottom), AI plays **Black** (top)
- **Tap** a piece to select it — legal moves appear as colored dots
- **Tap** a legal destination to move
- AI responds automatically
- Bottom-right circle: cycle UI theme color
- Bottom-left circle: cycle AI piece color
- Top-right circle: deep sleep

## Credits

Chess piece bitmaps from [maotek/ESP32-Chess](https://github.com/maotek/ESP32-Chess). Hardware layer ported from [CYD-Poker](https://github.com/xXQuantumSmokeXx/CYD-Poker). Built by xXQuantum-SmokeXx with Claude Code.
