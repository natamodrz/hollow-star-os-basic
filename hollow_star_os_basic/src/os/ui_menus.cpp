#include "ui_menus.h"
#include "ui_common.h"
#include "../../usb_kbd.h"
#include "../../net_ftp.h"
#include "../apps/app_notes.h"
#include "../apps/app_files.h"

static const char* PAGE0_NAMES[6] = {
  "Temp. RP2040", "Kalkulator", "Notatnik",
  "Gry",          "Muzyka",     "Ustawienia"
};
static const char* PAGE1_NAMES[3] = {
  "Pliki", "Narzedzia", "Uspienie"
};
static const char* GAMES_NAMES[3] = {
  "Brick Breaker", "Tetris", "Wyscigi"
};
static const char* TOOLS_NAMES[2] = {
  "Serwer FTP", "Sync NTP"
};
static const char* SETTINGS_NAMES[6] = {
  "Bluetooth", "Wi-Fi / NTP", "Kontrast",
  "Czas",      "Formatowanie","About"
};

static int mainPage    = 0;
static int wybranaMain = 0;
static int wybranaGry  = 0;
static int wybranaNar  = 0;
static int wybranaUst  = 0;

static void rysujIkonePage0(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
    case 0: // Temp
      d.drawFrame(cx - 4, cy - 6, 5, 9);
      d.drawBox(cx - 5, cy + 2, 7, 5);
      d.drawVLine(cx - 2, cy - 3, 6);
      d.drawHLine(cx + 2, cy - 5, 3);
      d.drawHLine(cx + 2, cy - 2, 2);
      d.drawHLine(cx + 2, cy + 1, 3);
      break;
    case 1: // Kalkulator
      d.drawFrame(cx - 6, cy - 7, 13, 14);
      d.drawBox(cx - 4, cy - 5, 9, 3);
      for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++) d.drawBox(cx - 4 + c * 3, cy + r * 3, 2, 2);
      break;
    case 2: // Notatnik (.md)
      d.drawFrame(cx - 6, cy - 7, 13, 14);
      d.drawHLine(cx - 6, cy - 4, 13);
      d.drawHLine(cx - 4, cy - 1, 9);
      d.drawHLine(cx - 4, cy + 2, 9);
      d.drawHLine(cx - 4, cy + 5, 6);
      break;
    case 3: // Gry
      d.drawFrame(cx - 8, cy - 5, 17, 10);
      d.drawHLine(cx - 6, cy, 5);
      d.drawVLine(cx - 4, cy - 2, 5);
      d.drawBox(cx + 2, cy - 2, 2, 2);
      d.drawBox(cx + 5, cy, 2, 2);
      break;
    case 4: // Muzyka
      d.drawBox(cx - 5, cy - 6, 11, 2);
      d.drawVLine(cx - 4, cy - 4, 8);
      d.drawVLine(cx + 4, cy - 4, 8);
      d.drawBox(cx - 7, cy + 3, 4, 3);
      d.drawBox(cx + 1, cy + 3, 4, 3);
      break;
    case 5: // Ustawienia
      d.drawHLine(cx - 7, cy - 4, 15); d.drawBox(cx - 4, cy - 6, 3, 5);
      d.drawHLine(cx - 7, cy,     15); d.drawBox(cx + 2, cy - 2, 3, 5);
      d.drawHLine(cx - 7, cy + 4, 15); d.drawBox(cx - 2, cy + 2, 3, 5);
      break;
  }
}

static void rysujIkonePage1(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
    case 0: // Pliki
      d.drawBox(cx - 7, cy - 6, 6, 2);
      d.drawFrame(cx - 7, cy - 4, 15, 10);
      d.drawHLine(cx - 5, cy - 1, 11);
      break;
    case 1: // Narzędzia
      d.drawFrame(cx - 7, cy - 6, 15, 12);
      d.drawPixel(cx - 4, cy - 2);
      d.drawPixel(cx - 3, cy - 1);
      d.drawPixel(cx - 2, cy);
      d.drawPixel(cx - 3, cy + 1);
      d.drawPixel(cx - 4, cy + 2);
      d.drawHLine(cx, cy + 2, 5);
      break;
    case 2: // Uśpienie
      d.drawFrame(cx - 6, cy - 5, 13, 11);
      d.setDrawColor(d.getDrawColor() ? 0 : 1);
      d.drawBox(cx - 2, cy - 6, 5, 3);
      d.setDrawColor(d.getDrawColor() ? 0 : 1);
      d.drawBox(cx - 1, cy - 7, 3, 7);
      break;
  }
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
    case 2: // Wyścigi
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

static void rysujIkoneUst(int idx, int cx, int cy) {
  U8G2 &d = uiDisplay();
  switch (idx) {
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
    case 2: // Kontrast
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawBox(cx, cy - 6, 7, 13);
      break;
    case 3: // Czas
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawVLine(cx, cy - 4, 5);
      d.drawHLine(cx, cy, 4);
      break;
    case 4: // Formatowanie
      d.drawHLine(cx - 5, cy - 5, 11);
      d.drawBox(cx - 2, cy - 7, 5, 2);
      d.drawFrame(cx - 4, cy - 4, 9, 10);
      d.drawVLine(cx - 1, cy - 2, 6);
      d.drawVLine(cx + 1, cy - 2, 6);
      break;
    case 5: // About
      d.drawFrame(cx - 6, cy - 6, 13, 13);
      d.drawBox(cx - 1, cy - 4, 2, 2);
      d.drawBox(cx - 1, cy - 1, 2, 6);
      break;
  }
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

  int count = (mainPage == 0) ? 6 : 3;
  for (int i = 0; i < count; i++) {
    int col = i % 3, row = i / 3;
    int x = 3 + col * 42, y = 11 + row * 21;
    drawTile(x, y, 38, 19, i == wybranaMain, (mainPage == 0) ? rysujIkonePage0 : rysujIkonePage1, i);
  }

  // Na karcie 2 (3 kafelki w górnym rzędzie) dolny pas pokazuje branding OS
  if (mainPage == 1) {
    d.drawFrame(3, 33, 122, 17);
    rysujHollowStar(13, 41, true);
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(24, 41, OS_BRAND_NAME);
    d.drawStr(24, 48, OS_BRAND_EDITION "  " OS_BRAND_VERSION);
  }

  const char* appName = (mainPage == 0) ? PAGE0_NAMES[wybranaMain] : PAGE1_NAMES[wybranaMain];
  drawMenuFooter(appName, false);

  // Indykator kart: pusta ramka = aktywna strona, wypełniony kwadrat = nieaktywna
  for (int p = 0; p < 2; p++) {
    int dx = 113 + p * 8;
    if (mainPage == p) d.drawFrame(dx, 56, 5, 5);
    else               d.drawBox(dx, 56, 5, 5);
  }

  rysujToastJesliAktywny();
  d.sendBuffer();
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
      if (col == 0) { mainPage = 1; wybranaMain = 2; }
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
      else if (wybranaMain == 2) appNotesOpenFile(false, "/start.md", APP_MENU, false);
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

  // Karta 2 (0: Pliki, 1: Narzędzia, 2: Uśpienie)
  if (k == KEY_LEFT) {
    if (wybranaMain == 0) { mainPage = 0; wybranaMain = 2; }
    else wybranaMain--;
  } else if (k == KEY_RIGHT) {
    if (wybranaMain == 2) { mainPage = 0; wybranaMain = 0; }
    else wybranaMain++;
  } else if (k == '\n' || k == ' ') {
    if (wybranaMain == 0)      { appFilesResetRoot(); uiSetScreen(APP_FILES); }
    else if (wybranaMain == 1) uiSetScreen(APP_TOOLS_MENU);
    else if (wybranaMain == 2) osEnterSuspend();
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
  drawMenuFooter(GAMES_NAMES[wybranaGry], true);
  d.sendBuffer();
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
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  for (int i = 0; i < 2; i++) {
    drawTile(14 + i * 54, 16, 46, 28, i == wybranaNar, rysujIkoneNar, i);
  }
  drawMenuFooter(TOOLS_NAMES[wybranaNar], true);
  rysujToastJesliAktywny();
  d.sendBuffer();
}

static bool toolsKey(uint8_t k) {
  if (k == KEY_ESC) uiSetScreen(APP_MENU);
  else if (k == KEY_LEFT || k == KEY_RIGHT) wybranaNar = 1 - wybranaNar;
  else if (k == '\n' || k == ' ') {
    if (wybranaNar == 0) {
      uiSetScreen(APP_FTP);
    } else if (netSnap.wifiConnected) {
      netFtpSendCmd(NCMD_NTP_SYNC);
      showToast("Sync NTP w tle...");
    } else {
      showToast("Brak polaczenia Wi-Fi!");
    }
  }
  return true;
}

// --- Podmenu: Ustawienia (3x2) ---
static void settingsDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  for (int i = 0; i < 6; i++) {
    int col = i % 3, row = i / 3;
    drawTile(3 + col * 42, 11 + row * 21, 38, 19, i == wybranaUst, rysujIkoneUst, i);
  }
  drawMenuFooter(SETTINGS_NAMES[wybranaUst], true);
  d.sendBuffer();
}

static bool settingsKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(APP_MENU);
    return true;
  }
  int col = wybranaUst % 3, row = wybranaUst / 3;
  if (k == KEY_LEFT)  col = (col + 2) % 3;
  else if (k == KEY_RIGHT) col = (col + 1) % 3;
  else if (k == KEY_UP || k == KEY_DOWN) row = 1 - row;
  else if (k == '\n' || k == ' ') {
    if (wybranaUst == 0)      uiSetScreen(APP_SET_BT);
    else if (wybranaUst == 1) uiSetScreen(APP_SET_WIFI);
    else if (wybranaUst == 2) uiSetScreen(APP_SET_CONTRAST);
    else if (wybranaUst == 3) uiSetScreen(APP_SET_TIME);
    else if (wybranaUst == 4) uiSetScreen(APP_SET_FORMAT);
    else if (wybranaUst == 5) uiSetScreen(APP_SET_ABOUT);
    return true;
  }
  wybranaUst = row * 3 + col;
  return true;
}

const AppScreen screenMainMenu     = { nullptr, nullptr, mainKey,     mainDraw };
const AppScreen screenGamesMenu    = { nullptr, nullptr, gamesKey,    gamesDraw };
const AppScreen screenToolsMenu    = { nullptr, nullptr, toolsKey,    toolsDraw };
const AppScreen screenSettingsMenu = { nullptr, nullptr, settingsKey, settingsDraw };
