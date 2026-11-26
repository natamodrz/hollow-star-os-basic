#include "ui_menus.h"
#include "ui_common.h"
#include "../../usb_kbd.h"
#include "../../net_ftp.h"
#include "../apps/app_notes.h"
#include "../apps/app_files.h"

static const char* PAGE0_NAMES_EN[6] = {
  "Sys Info", "Calculator", "Notes",
  "Games",    "Music",      "Settings"
};
static const char* PAGE0_NAMES_PL[6] = {
  "Sys Info", "Kalkulator", "Notatnik",
  "Gry",      "Muzyka",     "Ustawienia"
};

#if HSOS_HAS_WIRELESS
static const char* PAGE1_NAMES_EN[3] = { "Files", "Tools",     "Suspend" };
static const char* PAGE1_NAMES_PL[3] = { "Pliki", "Narzedzia", "Uspienie" };
static const int PAGE1_COUNT = 3;

static const char* SETTINGS_NAMES_EN[7] = {
  "Bluetooth", "Wi-Fi / NTP", "Contrast", "Time",
  "Language",  "Format",      "About"
};
static const char* SETTINGS_NAMES_PL[7] = {
  "Bluetooth", "Wi-Fi / NTP", "Kontrast", "Czas",
  "Jezyk",     "Formatowanie","About"
};
static const int SETTINGS_COUNT = 7;
static const int SETTINGS_COLS  = 4;
#else
static const char* PAGE1_NAMES_EN[2] = { "Files", "Suspend" };
static const char* PAGE1_NAMES_PL[2] = { "Pliki", "Uspienie" };
static const int PAGE1_COUNT = 2;

static const char* SETTINGS_NAMES_EN[5] = {
  "Contrast", "Time", "Language", "Format", "About"
};
static const char* SETTINGS_NAMES_PL[5] = {
  "Kontrast", "Czas", "Jezyk", "Formatowanie", "About"
};
static const int SETTINGS_COUNT = 5;
static const int SETTINGS_COLS  = 3;
#endif

static const char* GAMES_NAMES_EN[3] = { "Brick Breaker", "Tetris", "Racing" };
static const char* GAMES_NAMES_PL[3] = { "Brick Breaker", "Tetris", "Wyscigi" };
static const char* TOOLS_NAMES_EN[2] = { "FTP Server", "NTP Sync" };
static const char* TOOLS_NAMES_PL[2] = { "Serwer FTP", "Sync NTP" };

static int mainPage    = 0;
static int wybranaMain = 0;
static int wybranaGry  = 0;
static int wybranaNar  = 0;
static int wybranaUst  = 0;

static void rysujIkonePage0(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
    case 0: // Sys Info (chip / monitor)
      d.drawFrame(cx - 6, cy - 5, 13, 11);
      d.drawBox(cx - 4, cy - 3, 2, 7);
      d.drawBox(cx - 1, cy - 1, 2, 5);
      d.drawBox(cx + 2, cy - 3, 2, 7);
      break;
    case 1: // Calculator
      d.drawFrame(cx - 6, cy - 7, 13, 14);
      d.drawBox(cx - 4, cy - 5, 9, 3);
      for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) d.drawBox(cx - 4 + c * 3, cy + r * 3, 2, 2);
      break;
    case 2: // Notes (.md)
      d.drawFrame(cx - 6, cy - 7, 13, 14);
      d.drawHLine(cx - 6, cy - 4, 13);
      d.drawHLine(cx - 4, cy - 1, 9);
      d.drawHLine(cx - 4, cy + 2, 9);
      d.drawHLine(cx - 4, cy + 5, 6);
      break;
    case 3: // Games
      d.drawFrame(cx - 8, cy - 5, 17, 10);
      d.drawHLine(cx - 6, cy, 5);
      d.drawVLine(cx - 4, cy - 2, 5);
      d.drawBox(cx + 2, cy - 2, 2, 2);
      d.drawBox(cx + 5, cy, 2, 2);
      break;
    case 4: // Music
      d.drawBox(cx - 5, cy - 6, 11, 2);
      d.drawVLine(cx - 4, cy - 4, 8);
      d.drawVLine(cx + 4, cy - 4, 8);
      d.drawBox(cx - 7, cy + 3, 4, 3);
      d.drawBox(cx + 1, cy + 3, 4, 3);
      break;
    case 5: // Settings
      d.drawHLine(cx - 7, cy - 4, 15); d.drawBox(cx - 4, cy - 6, 3, 5);
      d.drawHLine(cx - 7, cy,     15); d.drawBox(cx + 2, cy - 2, 3, 5);
      d.drawHLine(cx - 7, cy + 4, 15); d.drawBox(cx - 2, cy + 2, 3, 5);
      break;
  }
}

static void rysujIkoneSleep(int cx, int cy) {
  U8G2 &d = uiDisplay();
  d.drawFrame(cx - 6, cy - 5, 13, 11);
  d.setDrawColor(d.getDrawColor() ? 0 : 1);
  d.drawBox(cx - 2, cy - 6, 5, 3);
  d.setDrawColor(d.getDrawColor() ? 0 : 1);
  d.drawBox(cx - 1, cy - 7, 3, 7);
}

static void rysujIkonePage1(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  if (idx == 0) { // Files
    d.drawBox(cx - 7, cy - 6, 6, 2);
    d.drawFrame(cx - 7, cy - 4, 15, 10);
    d.drawHLine(cx - 5, cy - 1, 11);
#if HSOS_HAS_WIRELESS
  } else if (idx == 1) { // Tools
    d.drawFrame(cx - 7, cy - 6, 15, 12);
    d.drawPixel(cx - 4, cy - 2);
    d.drawPixel(cx - 3, cy - 1);
    d.drawPixel(cx - 2, cy);
    d.drawPixel(cx - 3, cy + 1);
    d.drawPixel(cx - 4, cy + 2);
    d.drawHLine(cx, cy + 2, 5);
  } else {
    rysujIkoneSleep(cx, cy);
  }
#else
  } else {
    rysujIkoneSleep(cx, cy);
  }
#endif
}

static void rysujIkoneGry(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
    case 0: // Brick Breaker
      for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) d.drawBox(cx - 8 + c * 6, cy - 7 + r * 3, 5, 2);
      d.drawBox(cx + 1, cy + 1, 2, 2);
      d.drawBox(cx - 5, cy + 6, 11, 2);
      break;
    case 1: // Tetris
      d.drawBox(cx - 7, cy + 3, 4, 4);
      d.drawBox(cx - 2, cy + 3, 4, 4);
      d.drawBox(cx + 3, cy + 3, 4, 4);
      d.drawBox(cx - 2, cy - 2, 4, 4);
      d.drawFrame(cx + 3, cy - 7, 4, 4);
      d.drawFrame(cx + 3, cy - 2, 4, 4);
      break;
    case 2: // Racing
      d.drawVLine(cx - 8, cy - 7, 5); d.drawVLine(cx - 8, cy, 5);
      d.drawVLine(cx + 8, cy - 7, 5); d.drawVLine(cx + 8, cy, 5);
      d.drawBox(cx - 1, cy - 6, 3, 3);
      d.drawBox(cx - 4, cy - 3, 9, 3);
      d.drawBox(cx - 1, cy,     3, 3);
      d.drawBox(cx - 4, cy + 3, 3, 3);
      d.drawBox(cx + 2, cy + 3, 3, 3);
      break;
  }
}

static void rysujIkoneNar(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
    case 0: // FTP
      d.drawVLine(cx - 4, cy - 5, 11);
      d.drawHLine(cx - 6, cy - 3, 5);
      d.drawHLine(cx - 5, cy - 4, 3);
      d.drawVLine(cx + 4, cy - 5, 11);
      d.drawHLine(cx + 2, cy + 3, 5);
      d.drawHLine(cx + 3, cy + 4, 3);
      break;
    case 1: // Sync NTP
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawVLine(cx, cy - 4, 5);
      d.drawHLine(cx, cy, 4);
      d.drawBox(cx + 4, cy - 6, 3, 3);
      break;
  }
}

static void rysujIkoneUstLogical(int logicalIdx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (logicalIdx) {
    case 0: // Bluetooth
      d.drawVLine(cx, cy - 6, 13);
      d.drawLine(cx, cy - 6, cx + 4, cy - 2);
      d.drawLine(cx + 4, cy - 2, cx - 3, cy + 4);
      d.drawLine(cx - 3, cy - 4, cx + 4, cy + 2);
      d.drawLine(cx + 4, cy + 2, cx, cy + 6);
      break;
    case 1: // Wi-Fi
      d.drawBox(cx - 6, cy + 2, 3, 4);
      d.drawBox(cx - 2, cy - 1, 3, 7);
      d.drawBox(cx + 2, cy - 4, 3, 10);
      break;
    case 2: // Contrast
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawBox(cx, cy - 6, 7, 13);
      break;
    case 3: // Time
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawVLine(cx, cy - 4, 5);
      d.drawHLine(cx, cy, 4);
      break;
    case 4: // Language (kwadratowa siatka globu)
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawHLine(cx - 6, cy, 13);
      d.drawVLine(cx, cy - 6, 13);
      d.drawFrame(cx - 3, cy - 6, 7, 13);
      break;
    case 5: // Format
      d.drawHLine(cx - 5, cy - 5, 11);
      d.drawBox(cx - 2, cy - 7, 5, 2);
      d.drawFrame(cx - 4, cy - 4, 9, 10);
      d.drawVLine(cx - 1, cy - 2, 6);
      d.drawVLine(cx + 1, cy - 2, 6);
      break;
    case 6: // About
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawBox(cx - 1, cy - 4, 2, 2);
      d.drawBox(cx - 1, cy - 1, 2, 6);
      break;
  }
}

static void rysujIkoneUst(int idx, int cx, int cy) {
#if HSOS_HAS_WIRELESS
  rysujIkoneUstLogical(idx, cx, cy);
#else
  rysujIkoneUstLogical(idx + 2, cx, cy);
#endif
}

static void drawTile(int x, int y, int w, int h, bool selected, void (*drawIcon)(int, int, int), int idx) {
  U8G2 &d = uiDisplay();
  if (selected) {
    d.drawBox(x, y, w, h);
    d.setDrawColor(0);
    drawIcon(idx, x + w / 2, y + (h + 1) / 2);
    d.setDrawColor(1);
  } else {
    d.drawFrame(x, y, w, h);
    drawIcon(idx, x + w / 2, y + (h + 1) / 2);
  }
}

static void drawMenuFooter(const char* label, bool showEscHint) {
  U8G2 &d = uiDisplay();
  d.drawHLine(0, 53, 128);
  d.setFont(u8g2_font_6x10_tr);
  d.drawStr(2, 63, label);
  if (showEscHint) {
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(94, 62, "Esc:M");
  }
}

// --- Menu główne ---
static void mainDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();

  int count = (mainPage == 0) ? 6 : PAGE1_COUNT;
  if (wybranaMain >= count) wybranaMain = count - 1;

  for (int i = 0; i < count; i++) {
    int col = i % 3, row = i / 3;
#if HSOS_HAS_WIRELESS
    int x = 3 + col * 42;
    int w = 38;
#else
    int x = (mainPage == 0) ? (3 + col * 42) : (14 + col * 54);
    int w = (mainPage == 0) ? 38 : 46;
#endif
    int y = 11 + row * 21;
    drawTile(x, y, w, 19, i == wybranaMain, (mainPage == 0) ? rysujIkonePage0 : rysujIkonePage1, i);
  }

  if (mainPage == 1) {
    d.drawFrame(3, 33, 122, 17);
    rysujHollowStar(13, 41, true);
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(24, 41, OS_BRAND_NAME);
    d.drawStr(24, 48, HSOS_BOARD_TAG " " OS_BRAND_VERSION);
  }

  const char* appName = (mainPage == 0)
                        ? (sysConfig.lang == 1 ? PAGE0_NAMES_PL[wybranaMain] : PAGE0_NAMES_EN[wybranaMain])
                        : (sysConfig.lang == 1 ? PAGE1_NAMES_PL[wybranaMain] : PAGE1_NAMES_EN[wybranaMain]);
  drawMenuFooter(appName, false);

  for (int p = 0; p < 2; p++) {
    int dx = 113 + p * 8;
    if (mainPage == p) d.drawFrame(dx, 56, 5, 5);
    else               d.drawBox(dx, 56, 5, 5);
  }

  rysujToastJesliAktywny();
  uiFinishFrame();
}

static bool mainKey(uint8_t k) {
  if (k == KEY_TAB) {
    mainPage = 1 - mainPage;
    wybranaMain = 0;
    return true;
  }
  if (mainPage == 0) {
    int col = wybranaMain % 3, row = wybranaMain / 3;
    if (k == KEY_LEFT) {
      if (col == 0) { mainPage = 1; wybranaMain = PAGE1_COUNT - 1; }
      else          { col--; wybranaMain = row * 3 + col; }
    } else if (k == KEY_RIGHT) {
      if (col == 2) { mainPage = 1; wybranaMain = 0; }
      else          { col++; wybranaMain = row * 3 + col; }
    } else if (k == KEY_UP || k == KEY_DOWN) {
      row = 1 - row;
      wybranaMain = row * 3 + col;
    } else if (k == '\n' || k == ' ') {
      if (wybranaMain == 0)      uiSetScreen(APP_TEMP);
      else if (wybranaMain == 1) uiSetScreen(APP_CALC);
      else if (wybranaMain == 2) appNotesOpenSetup(APP_MENU);
      else if (wybranaMain == 3) uiSetScreen(APP_GAMES_MENU);
      else if (wybranaMain == 4) {
        if (!audioSnap.sdReady) btAudioSendCmd(ACMD_SCAN_SD);
        uiSetScreen(APP_MUSIC);
      } else if (wybranaMain == 5) {
        uiSetScreen(APP_SETTINGS_MENU);
      }
    }
    return true;
  }

  if (k == KEY_LEFT) {
    if (wybranaMain == 0) { mainPage = 0; wybranaMain = 2; }
    else wybranaMain--;
  } else if (k == KEY_RIGHT) {
    if (wybranaMain >= PAGE1_COUNT - 1) { mainPage = 0; wybranaMain = 0; }
    else wybranaMain++;
  } else if (k == '\n' || k == ' ') {
#if HSOS_HAS_WIRELESS
    if (wybranaMain == 0)      { appFilesResetRoot(); uiSetScreen(APP_FILES); }
    else if (wybranaMain == 1) uiSetScreen(APP_TOOLS_MENU);
    else if (wybranaMain == 2) osEnterSuspend();
#else
    if (wybranaMain == 0)      { appFilesResetRoot(); uiSetScreen(APP_FILES); }
    else if (wybranaMain == 1) osEnterSuspend();
#endif
  }
  return true;
}

// --- Podmenu: Gry ---
static void gamesDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  for (int i = 0; i < 3; i++) {
    drawTile(3 + i * 42, 16, 38, 28, i == wybranaGry, rysujIkoneGry, i);
  }
  drawMenuFooter(sysConfig.lang == 1 ? GAMES_NAMES_PL[wybranaGry] : GAMES_NAMES_EN[wybranaGry], true);
  uiFinishFrame();
}

static bool gamesKey(uint8_t k) {
  if (k == KEY_ESC)        uiSetScreen(APP_MENU);
  else if (k == KEY_LEFT)  wybranaGry = (wybranaGry + 2) % 3;
  else if (k == KEY_RIGHT) wybranaGry = (wybranaGry + 1) % 3;
  else if (k == '\n' || k == ' ') {
    if (wybranaGry == 0)      uiSetScreen(APP_BRICK);
    else if (wybranaGry == 1) uiSetScreen(APP_TETRIS);
    else if (wybranaGry == 2) uiSetScreen(APP_RACING);
  }
  return true;
}

// --- Podmenu: Narzędzia ---
static void toolsDraw() {
#if !HSOS_HAS_WIRELESS
  uiSetScreen(APP_MENU);
  return;
#else
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  for (int i = 0; i < 2; i++) {
    drawTile(14 + i * 54, 16, 46, 28, i == wybranaNar, rysujIkoneNar, i);
  }
  drawMenuFooter(sysConfig.lang == 1 ? TOOLS_NAMES_PL[wybranaNar] : TOOLS_NAMES_EN[wybranaNar], true);
  rysujToastJesliAktywny();
  uiFinishFrame();
#endif
}

static bool toolsKey(uint8_t k) {
#if !HSOS_HAS_WIRELESS
  (void)k;
  uiSetScreen(APP_MENU);
  return true;
#else
  if (k == KEY_ESC) uiSetScreen(APP_MENU);
  else if (k == KEY_LEFT || k == KEY_RIGHT) wybranaNar = 1 - wybranaNar;
  else if (k == '\n' || k == ' ') {
    if (wybranaNar == 0) {
      uiSetScreen(APP_FTP);
    } else if (netSnap.wifiConnected) {
      netFtpSendCmd(NCMD_NTP_SYNC);
      showToast(tr("NTP sync started...", "Sync NTP w tle..."));
    } else {
      showToast(tr("Wi-Fi not connected!", "Brak polaczenia Wi-Fi!"));
    }
  }
  return true;
#endif
}

// --- Podmenu: Ustawienia ---
static void settingsDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  if (wybranaUst >= SETTINGS_COUNT) wybranaUst = 0;

#if HSOS_HAS_WIRELESS
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int col = i % SETTINGS_COLS, row = i / SETTINGS_COLS;
    drawTile(2 + col * 31, 11 + row * 21, 29, 19, i == wybranaUst, rysujIkoneUst, i);
  }
#else
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int col = i % SETTINGS_COLS, row = i / SETTINGS_COLS;
    drawTile(3 + col * 42, 11 + row * 21, 38, 19, i == wybranaUst, rysujIkoneUst, i);
  }
#endif
  drawMenuFooter(sysConfig.lang == 1 ? SETTINGS_NAMES_PL[wybranaUst] : SETTINGS_NAMES_EN[wybranaUst], true);
  uiFinishFrame();
}

static bool settingsKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(APP_MENU);
    return true;
  }
  if (k == KEY_LEFT) {
    wybranaUst = (wybranaUst + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
  } else if (k == KEY_RIGHT) {
    wybranaUst = (wybranaUst + 1) % SETTINGS_COUNT;
  } else if (k == KEY_UP || k == KEY_DOWN) {
    int col = wybranaUst % SETTINGS_COLS;
    int row = 1 - (wybranaUst / SETTINGS_COLS);
    int target = row * SETTINGS_COLS + col;
    if (target >= SETTINGS_COUNT) target = SETTINGS_COUNT - 1;
    wybranaUst = target;
  } else if (k == '\n' || k == ' ') {
#if HSOS_HAS_WIRELESS
    static const AppState mapW[7] = {
      APP_SET_BT, APP_SET_WIFI, APP_SET_CONTRAST, APP_SET_TIME,
      APP_SET_LANG, APP_SET_FORMAT, APP_SET_ABOUT
    };
    uiSetScreen(mapW[wybranaUst]);
#else
    static const AppState mapP[5] = {
      APP_SET_CONTRAST, APP_SET_TIME, APP_SET_LANG, APP_SET_FORMAT, APP_SET_ABOUT
    };
    uiSetScreen(mapP[wybranaUst]);
#endif
  }
  return true;
}

static const char* const HELP_MAIN_EN[] = {
  "</>/^/v: Select app",
  "Tab: Page 1 / 2",
  "Ent/Spc: Open app",
  "F1: Keyboard shortcuts"
};
static const char* const HELP_MAIN_PL[] = {
  "</>/^/v: Wybor aplikacji",
  "Tab: Strona 1 / 2",
  "Ent/Spc: Otworz",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_GAMES_EN[] = {
  "</>: Select game",
  "Ent/Spc: Launch game",
  "Esc: Main menu",
  "F1: Keyboard shortcuts"
};
static const char* const HELP_GAMES_PL[] = {
  "</>: Wybor gry",
  "Ent/Spc: Uruchom gre",
  "Esc: Menu glowne",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_TOOLS_EN[] = {
  "</>: Select tool",
  "Ent/Spc: Run",
  "Esc: Main menu",
  "F1: Keyboard shortcuts"
};
static const char* const HELP_TOOLS_PL[] = {
  "</>: Wybor narzedzia",
  "Ent/Spc: Uruchom",
  "Esc: Menu glowne",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SETTINGS_EN[] = {
  "</>/^/v: Select setting",
  "Ent/Spc: Open",
  "Esc: Main menu",
  "F1: Keyboard shortcuts"
};
static const char* const HELP_SETTINGS_PL[] = {
  "</>/^/v: Wybor opcji",
  "Ent/Spc: Otworz",
  "Esc: Menu glowne",
  "F1: Skroty klawiszowe"
};

const AppScreen screenMainMenu     = { nullptr, nullptr, mainKey,     mainDraw,     HELP_MAIN_EN,     HELP_MAIN_PL,     4 };
const AppScreen screenGamesMenu    = { nullptr, nullptr, gamesKey,    gamesDraw,    HELP_GAMES_EN,    HELP_GAMES_PL,    4 };
const AppScreen screenToolsMenu    = { nullptr, nullptr, toolsKey,    toolsDraw,    HELP_TOOLS_EN,    HELP_TOOLS_PL,    4 };
const AppScreen screenSettingsMenu = { nullptr, nullptr, settingsKey, settingsDraw, HELP_SETTINGS_EN, HELP_SETTINGS_PL, 4 };
