#include "ui_common.h"
#include "../../usb_kbd.h"

AppScratchRam appScratch;
AudioUiSnapshot audioSnap;
NetUiSnapshot   netSnap;
AppState ekran = APP_MENU;

static U8G2 *gDisp = nullptr;

// Zegar systemowy
static int clockBaseHour = 12;
static int clockBaseMin  = 0;
static unsigned long clockBaseMs = 0;
static int lastDisplayedMin = -1;

// Toast
static unsigned long toastUntil = 0;
static char toastMsg[26] = {0};

// Modal
static ModalMode modalMode = MODAL_NONE;
static char modalTitle[26] = {0};
static char modalInput[64] = {0};

// Uniwersalne okno skrótów klawiszowych pod F1
static bool helpOpen = false;
static const char* const* helpLinesPtr = nullptr;
static uint8_t helpLinesCount = 0;
static uint8_t helpScroll = 0;

void uiAttachDisplay(U8G2 *disp) {
  gDisp = disp;
}

U8G2& uiDisplay() {
  return *gDisp;
}

void getClock(int &h, int &m) {
  unsigned long elapsedMins = (millis() - clockBaseMs) / 60000UL;
  unsigned long total = (unsigned long)clockBaseHour * 60UL + (unsigned long)clockBaseMin + elapsedMins;
  h = (int)((total / 60UL) % 24UL);
  m = (int)(total % 60UL);
}

void setClock(int h, int m) {
  clockBaseHour = ((h % 24) + 24) % 24;
  clockBaseMin  = ((m % 60) + 60) % 60;
  clockBaseMs   = millis();
  lastDisplayedMin = clockBaseMin;
}

bool clockMinuteTicked() {
  int h, m;
  getClock(h, m);
  if (m != lastDisplayedMin) {
    lastDisplayedMin = m;
    return true;
  }
  return false;
}

void showToast(const char* msg, unsigned long ms) {
  strncpy(toastMsg, msg ? msg : "", sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toastUntil = millis() + ms;
}

bool toastUpdateExpiry(unsigned long now) {
  if (toastUntil > 0 && now >= toastUntil) {
    toastUntil = 0;
    return true;
  }
  return false;
}

void rysujToastJesliAktywny() {
  if (millis() < toastUntil && toastMsg[0]) {
    U8G2 &d = uiDisplay();
    d.setDrawColor(0);
    d.drawBox(8, 24, 112, 16);
    d.setDrawColor(1);
    d.drawFrame(8, 24, 112, 16);
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(12, 35, toastMsg);
  }
}

ModalMode modalGetMode() { return modalMode; }
void modalClose() { modalMode = MODAL_NONE; }

void modalOpenInput(ModalMode mode, const char* title, const char* initial) {
  modalMode = mode;
  strncpy(modalTitle, title ? title : "", sizeof(modalTitle) - 1);
  modalTitle[sizeof(modalTitle) - 1] = '\0';
  strncpy(modalInput, initial ? initial : "", sizeof(modalInput) - 1);
  modalInput[sizeof(modalInput) - 1] = '\0';
}

void modalOpenConfirm(ModalMode mode, const char* title) {
  modalMode = mode;
  strncpy(modalTitle, title ? title : "", sizeof(modalTitle) - 1);
  modalTitle[sizeof(modalTitle) - 1] = '\0';
  modalInput[0] = '\0';
}

const char* modalGetInput() { return modalInput; }

bool modalHandleKey(uint8_t k, bool &confirmedOut, bool &submittedOut) {
  confirmedOut = false;
  submittedOut = false;
  if (modalMode == MODAL_NONE) return false;

  if (k == KEY_ESC) {
    modalMode = MODAL_NONE;
    return true;
  }

  if (modalMode == MODAL_WIFI_PASS || modalMode == MODAL_NEW_MD ||
      modalMode == MODAL_FTP_USER  || modalMode == MODAL_FTP_PASS) {
    size_t maxLen = (modalMode == MODAL_FTP_USER || modalMode == MODAL_FTP_PASS) ? 22 : 60;
    size_t len = strlen(modalInput);
    if (k == '\b') {
      if (len > 0) modalInput[len - 1] = '\0';
    } else if (k == '\n') {
      submittedOut = true;
    } else if (k >= 32 && k <= 126 && len < maxLen) {
      modalInput[len] = (char)k;
      modalInput[len + 1] = '\0';
    }
    return true;
  }

  if (k == 'y' || k == 'Y') {
    confirmedOut = true;
  }
  return true;
}

void rysujModal() {
  U8G2 &d = uiDisplay();
  d.setDrawColor(0);
  d.drawBox(4, 16, 120, 38);
  d.setDrawColor(1);
  d.drawFrame(4, 16, 120, 38);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(8, 26, modalTitle);

  if (modalMode == MODAL_WIFI_PASS || modalMode == MODAL_NEW_MD ||
      modalMode == MODAL_FTP_USER  || modalMode == MODAL_FTP_PASS) {
    d.drawFrame(8, 30, 112, 11);
    char vis[24];
    size_t len = strlen(modalInput);
    const char* start = (len > 20) ? (modalInput + len - 20) : modalInput;
    snprintf(vis, sizeof(vis), "%s_", start);
    d.drawStr(10, 38, vis);
    d.drawStr(8, 50, tr("Ent:OK  Esc:Cancel", "Ent:OK  Esc:Anuluj"));
  } else {
    d.drawStr(8, 38, tr("Are you sure?", "Czy na pewno?"));
    d.drawStr(8, 50, tr("[Y] Yes   [Esc] No", "[Y] Tak   [Esc] Nie"));
  }
}

void rysujHollowStar(int cx, int cy, bool smallIcon) {
  U8G2 &d = uiDisplay();
  static const int8_t ptsLarge[10][2] = {
    {  0, -13}, {  3,  -4}, { 12,  -4}, {  5,   2}, {  8,  11},
    {  0,   6}, { -8,  11}, { -5,   2}, {-12,  -4}, { -3,  -4}
  };
  static const int8_t ptsSmall[10][2] = {
    {  0, -7}, {  2, -2}, {  7, -2}, {  3,  1}, {  4,  6},
    {  0,  3}, { -4,  6}, { -3,  1}, { -7, -2}, { -2, -2}
  };
  const int8_t (*pts)[2] = smallIcon ? ptsSmall : ptsLarge;
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    d.drawLine(cx + pts[i][0], cy + pts[i][1], cx + pts[j][0], cy + pts[j][1]);
  }
}

void rysujBootSplash(const char* statusLine) {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  d.setDrawColor(1);
  d.drawFrame(0, 0, 128, 64);
  d.drawFrame(2, 2, 124, 60);

  rysujHollowStar(21, 27, false);

  d.setFont(u8g2_font_6x10_tr);
  d.drawStr(38, 22, "HOLLOW STAR");
  d.drawStr(38, 33, "OS " OS_BRAND_VERSION);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(38, 41, HSOS_BOARD_TAG " SMP");

  d.drawHLine(6, 45, 116);
  d.drawStr(8, 56, statusLine ? statusLine : tr("Initializing...", "Inicjalizacja..."));
  d.sendBuffer();
}

static void rysujIkoneSluchawkiPasek(int hx, bool active) {
  U8G2 &d = uiDisplay();
  d.drawHLine(hx + 1, 1, 7);
  d.drawVLine(hx,     2, 4);
  d.drawVLine(hx + 8, 2, 4);
  if (active) {
    d.drawBox(hx,     4, 3, 4);
    d.drawBox(hx + 6, 4, 3, 4);
  } else {
    d.drawFrame(hx,     4, 3, 4);
    d.drawFrame(hx + 6, 4, 3, 4);
  }
}

#if HSOS_HAS_WIRELESS
static void rysujIkoneWifiPasek(int wx) {
  U8G2 &d = uiDisplay();
  // 4 schodkowe słupki sygnału jak w telefonie (ostre prostokąty, y=1..7)
  int bars = 0;
  if (netSnap.wifiConnected) {
    int rssi = netSnap.wifiRssi;
    if (rssi == 0 || rssi <= -120) bars = 3;
    else if (rssi >= -55) bars = 4;
    else if (rssi >= -67) bars = 3;
    else if (rssi >= -78) bars = 2;
    else bars = 1;
  }

  for (int i = 0; i < 4; i++) {
    int bx = wx + i * 3;
    int bh = 2 + i * 2; // 2, 4, 6, 8 -> przytnie się do 7px wysokości
    if (bh > 7) bh = 7;
    if (i < bars) {
      d.drawBox(bx, 8 - bh, 2, bh);
    } else {
      // Nieaktywny słupek / brak sieci: ostra 1px podstawa wskaźnika
      d.drawHLine(bx, 7, 2);
    }
  }
}
#endif

void rysujPasekStanu() {
  U8G2 &d = uiDisplay();
  d.setDrawColor(1);

  // LEWA: Kwadratowa ikona klawiatury + kropka połączenia
  d.drawFrame(2, 1, 11, 7);
  d.drawPixel(4, 3); d.drawPixel(6, 3); d.drawPixel(8, 3); d.drawPixel(10, 3);
  d.drawHLine(4, 5, 7);
  if (kbdConnected) d.drawBox(15, 3, 3, 3);
  else              d.drawFrame(15, 3, 3, 3);

  // ŚRODEK: Aktualny czas HH:MM
  int h, m;
  getClock(h, m);
  char timeStr[8];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", h, m);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(51, 7, timeStr);

  // PRAWA: Zależna od wersji Pico W vs zwykłe Pico
#if HSOS_HAS_WIRELESS
  rysujIkoneWifiPasek(95);
  if (audioSnap.activeSink == ASINK_JACK) {
    rysujIkoneSluchawkiPasek(115, true);
  } else {
    int bx = 114;
    d.drawVLine(bx + 2, 1, 7);
    d.drawLine(bx + 2, 1, bx + 4, 3);
    d.drawLine(bx + 4, 3, bx, 6);
    d.drawLine(bx, 2, bx + 4, 5);
    d.drawLine(bx + 4, 5, bx + 2, 7);
    if (audioSnap.btConnected) {
      d.drawBox(122, 3, 3, 3);
    }
  }
#else
  rysujIkoneSluchawkiPasek(115, audioSnap.jackConnected || audioSnap.activeSink == ASINK_JACK);
#endif

  d.drawHLine(0, 9, 128);
}

void rysujPopupKoniecGry(const char* title) {
  U8G2 &d = uiDisplay();
  d.setDrawColor(0);
  d.drawBox(14, 22, 100, 28);
  d.setDrawColor(1);
  d.drawFrame(14, 22, 100, 28);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(36, 34, title);
  d.drawStr(18, 45, tr("Ent:Retry Esc:Menu", "Ent:Gra Esc:Menu"));
}

bool uiHelpIsOpen() { return helpOpen; }

void uiHelpOpen(const char* const* lines, uint8_t count) {
  if (!lines || count == 0) return;
  helpLinesPtr = lines;
  helpLinesCount = count;
  helpScroll = 0;
  helpOpen = true;
}

void uiHelpClose() {
  helpOpen = false;
}

bool uiHelpHandleKey(uint8_t k) {
  if (!helpOpen) return false;
  if (k == KEY_ESC || k == KEY_F1 || k == '\n' || k == ' ') {
    helpOpen = false;
    return true;
  }
  int maxScroll = (helpLinesCount > 4) ? (helpLinesCount - 4) : 0;
  if (k == KEY_UP && helpScroll > 0) helpScroll--;
  else if (k == KEY_DOWN && helpScroll < maxScroll) helpScroll++;
  return true;
}

static void rysujHelpOverlay() {
  if (!helpOpen || !helpLinesPtr) return;
  U8G2 &d = uiDisplay();
  d.setDrawColor(0);
  d.drawBox(2, 11, 124, 52);
  d.setDrawColor(1);
  d.drawFrame(2, 11, 124, 52);
  d.setFont(u8g2_font_5x7_tr);

  char hdr[24];
  const char* baseTitle = tr("SHORTCUTS [F1]", "SKROTY [F1]");
  if (helpLinesCount > 4) {
    int maxScroll = helpLinesCount - 4;
    snprintf(hdr, sizeof(hdr), "%s %d/%d", baseTitle, (int)helpScroll + 1, maxScroll + 1);
  } else {
    snprintf(hdr, sizeof(hdr), "%s", baseTitle);
  }
  d.drawStr(5, 19, hdr);
  d.drawHLine(4, 21, 120);

  for (int i = 0; i < 4 && (helpScroll + i) < helpLinesCount; i++) {
    d.drawStr(5, 30 + i * 8, helpLinesPtr[helpScroll + i]);
  }

  d.drawHLine(4, 54, 120);
  d.drawStr(5, 61, (helpLinesCount > 4)
                   ? tr("^/v:Scroll  Esc/F1:OK", "^/v:Przewin  Esc/F1:OK")
                   : tr("Esc / F1 / Ent: Close", "Esc / F1 / Ent: Zamknij"));
}

void uiFinishFrame() {
  if (helpOpen) rysujHelpOverlay();
  uiDisplay().sendBuffer();
}

void rysujIkoneFolder(int x, int y) {
  U8G2 &d = uiDisplay();
  d.drawBox(x, y, 3, 1);
  d.drawFrame(x, y + 1, 7, 5);
}

void rysujIkonePlik(int x, int y) {
  U8G2 &d = uiDisplay();
  d.drawFrame(x, y, 6, 7);
  d.drawHLine(x + 1, y + 2, 3);
  d.drawHLine(x + 1, y + 4, 4);
}

void rysujIkoneMuzyka(int x, int y) {
  U8G2 &d = uiDisplay();
  d.drawHLine(x + 2, y, 5);
  d.drawVLine(x + 2, y, 5);
  d.drawVLine(x + 6, y, 5);
  d.drawBox(x,     y + 4, 3, 3);
  d.drawBox(x + 4, y + 4, 3, 3);
}

void rysujIkoneCzip(int x, int y) {
  U8G2 &d = uiDisplay();
  d.drawFrame(x + 1, y + 1, 5, 5);
  d.drawPixel(x + 3, y + 3);
  d.drawPixel(x,     y + 2); d.drawPixel(x,     y + 4);
  d.drawPixel(x + 6, y + 2); d.drawPixel(x + 6, y + 4);
  d.drawPixel(x + 2, y);     d.drawPixel(x + 4, y);
  d.drawPixel(x + 2, y + 6); d.drawPixel(x + 4, y + 6);
}

void rysujIkoneKartaSD(int x, int y) {
  U8G2 &d = uiDisplay();
  d.drawHLine(x + 1, y, 5);
  d.drawVLine(x,     y + 1, 6);
  d.drawVLine(x + 5, y,     7);
  d.drawHLine(x,     y + 6, 6);
  d.drawVLine(x + 2, y + 2, 2);
  d.drawVLine(x + 4, y + 2, 2);
}
