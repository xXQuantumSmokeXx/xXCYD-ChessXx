#include <Arduino.h>
#include <Preferences.h>
#include "theme.h"
#include "config.h"

const ThemeEntry g_themes[THEME_COUNT] = {
    {" CYAN",   0x07FF},
    {" GREEN",  0x07E0},
    {" RED",    0xF800},
    {" ORANGE", 0xFD20},
    {" YELLOW", 0xFFE0},
    {" GRAY",   0x8410},
    {" PURPLE", 0x8010},
    {" PINK",   0xF81F},
    {" WHITE",  0xFFFF},
};

uint16_t g_themeColor = 0x07FF;
int      g_themeIdx   = 0;

void themeInit() {
    Preferences p;
    p.begin("cyd-chess", true);
    int idx = p.getInt("theme", 0);
    if (idx < 0 || idx >= THEME_COUNT) idx = 0;
    g_themeIdx = idx;
    g_themeColor = g_themes[idx].color;
    p.end();
}

void themeNext() {
    g_themeIdx = (g_themeIdx + 1) % THEME_COUNT;
    g_themeColor = g_themes[g_themeIdx].color;
    Preferences p;
    p.begin("cyd-chess", false);
    p.putInt("theme", g_themeIdx);
    p.end();
}

void themeSet(int idx) {
    if (idx < 0 || idx >= THEME_COUNT) return;
    g_themeIdx = idx;
    g_themeColor = g_themes[idx].color;
    Preferences p;
    p.begin("cyd-chess", false);
    p.putInt("theme", g_themeIdx);
    p.end();
}
