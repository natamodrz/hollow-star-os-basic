#include "app_settings.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../bt_audio.h"
#include "../../net_ftp.h"

static int fmtSel = 0;
static int pendingFmtTarget = 0;
static int editTimeField = 0;
static unsigned long lastAboutMs = 0;

#if HSOS_HAS_WIRELESS
static int btMenuSel = 0;
static int wifiMenuSel = 0;
static char pendingWifiSsid[36] = {0};
static unsigned long lastAnimMs = 0;
static int animFrame = 0;

// --- Bluetooth ---
static bool btTick(unsigned long now) {
  if (audioSnap.btScanning && (now - lastAnimMs >= 300UL)) {
    lastAnimMs = now;
    animFrame++;
    return true;
  }
  return false;
}

static bool btKey(uint8_t k) {
  int totalItems = 3 + audioSnap.btDevCount;
  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_UP)   btMenuSel = (btMenuSel + totalItems - 1) % totalItems;
  else if (k == KEY_DOWN) btMenuSel = (btMenuSel + 1) % totalItems;
  else if (k == '\n' || k == ' ') {
    if (audioSnap.btDisabledByJack && btMenuSel != 1 && btMenuSel != 2) {
      showToast("Wyl. gdy Jack wpiety!");
      return true;
    }
    if (btMenuSel == 0 && !audioSnap.btScanning) btAudioSendCmd(ACMD_BT_SCAN);
    else if (btMenuSel == 1) btAudioSendCmd(ACMD_BT_DISCONNECT);
    else if (btMenuSel == 2) btAudioSendCmd(ACMD_BT_CLEAR_PAIRING);
    else if (btMenuSel >= 3 && !audioSnap.btScanning) btAudioSendCmd(ACMD_BT_CONNECT_IDX, btMenuSel - 3);
  }
  return true;
}

static void btDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (audioSnap.btDisabledByJack) {
    d.drawStr(2, 18, "BT wylaczony (Jack)");
  } else if (audioSnap.btScanning) {
    char anim[24];
    const char* dots[4] = {"", ".", "..", "..."};
    snprintf(anim, sizeof(anim), "Skanuje BT%s", dots[animFrame & 3]);
    d.drawStr(2, 18, anim);
  } else {
    d.drawStr(2, 18, audioSnap.btStatus);
  }
  d.drawHLine(0, 20, 128);

  int totalItems = 3 + audioSnap.btDevCount;
  if (btMenuSel >= totalItems) btMenuSel = totalItems - 1;
  int startIdx = max(0, min(btMenuSel - 1, totalItems - 4));
  for (int i = 0; i < 4 && (startIdx + i) < totalItems; i++) {
    int idx = startIdx + i, y = 22 + i * 10;
    char line[28];
    if (idx == 0)      snprintf(line, sizeof(line), "[Skanuj urzadzenia]");
    else if (idx == 1) snprintf(line, sizeof(line), "[Rozlacz audio]");
    else if (idx == 2) snprintf(line, sizeof(line), "[Wyczysc parowanie]");
    else {
      const BtDevEntry &dev = audioSnap.btDevs[idx - 3];
      snprintf(line, sizeof(line), ">%.16s %ddB", dev.name, (int)dev.rssi);
    }
    if (idx == btMenuSel) {
      d.drawBox(0, y, 128, 9);
      d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 7, line);
    }
  }
  rysujToastJesliAktywny();
  uiFinishFrame();
}

// --- Wi-Fi / NTP ---
static bool wifiTick(unsigned long now) {
  if (netSnap.wifiBusy && (now - lastAnimMs >= 300UL)) {
    lastAnimMs = now;
    animFrame++;
    return true;
  }
  return false;
}

static bool wifiKey(uint8_t k) {
  if (modalGetMode() != MODAL_NONE) {
    bool confirmed = false, submitted = false;
    modalHandleKey(k, confirmed, submitted);
    if (submitted && modalGetMode() == MODAL_WIFI_PASS) {
      strncpy(sysConfig.wifiSsid, pendingWifiSsid, sizeof(sysConfig.wifiSsid) - 1);
      sysConfig.wifiSsid[sizeof(sysConfig.wifiSsid) - 1] = '\0';
      strncpy(sysConfig.wifiPass, modalGetInput(), sizeof(sysConfig.wifiPass) - 1);
      sysConfig.wifiPass[sizeof(sysConfig.wifiPass) - 1] = '\0';
      storageSaveConfig();
      netFtpSendCmd(NCMD_WIFI_CONNECT_SAVED);
      modalClose();
    }
    return true;
  }

  int totalItems = 4 + netSnap.apCount;
  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_UP)   wifiMenuSel = (wifiMenuSel + totalItems - 1) % totalItems;
  else if (k == KEY_DOWN) wifiMenuSel = (wifiMenuSel + 1) % totalItems;
  else if (k == '\n' || k == ' ') {
    if (wifiMenuSel == 0 && !netSnap.wifiBusy) netFtpSendCmd(NCMD_WIFI_SCAN);
    else if (wifiMenuSel == 1 && !netSnap.wifiBusy) netFtpSendCmd(NCMD_WIFI_CONNECT_SAVED);
    else if (wifiMenuSel == 2 && !netSnap.wifiBusy) netFtpSendCmd(NCMD_NTP_SYNC);
    else if (wifiMenuSel == 3) netFtpSendCmd(NCMD_WIFI_DISCONNECT);
    else if (wifiMenuSel >= 4 && !netSnap.wifiBusy) {
      strncpy(pendingWifiSsid, netSnap.aps[wifiMenuSel - 4].ssid, sizeof(pendingWifiSsid) - 1);
      pendingWifiSsid[sizeof(pendingWifiSsid) - 1] = '\0';
      char title[26];
      snprintf(title, sizeof(title), "Haslo: %.16s", pendingWifiSsid);
      modalOpenInput(MODAL_WIFI_PASS, title);
    }
  }
  return true;
}

static void wifiDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (netSnap.wifiBusy) {
    char anim[26];
    const char* dots[4] = {"", ".", "..", "..."};
    snprintf(anim, sizeof(anim), "%.18s%s", netSnap.wifiStatus, dots[animFrame & 3]);
    d.drawStr(2, 18, anim);
  } else {
    d.drawStr(2, 18, netSnap.wifiStatus);
  }
  d.drawHLine(0, 20, 128);

  int totalItems = 4 + netSnap.apCount;
  if (wifiMenuSel >= totalItems) wifiMenuSel = totalItems - 1;
  int startIdx = max(0, min(wifiMenuSel - 1, totalItems - 4));
  for (int i = 0; i < 4 && (startIdx + i) < totalItems; i++) {
    int idx = startIdx + i, y = 22 + i * 10;
    char line[28];
    if (idx == 0)      snprintf(line, sizeof(line), "[Skanuj sieci Wi-Fi]");
    else if (idx == 1) snprintf(line, sizeof(line), "[Polacz z zapisana]");
    else if (idx == 2) snprintf(line, sizeof(line), "[Synchronizuj NTP]");
    else if (idx == 3) snprintf(line, sizeof(line), "[Rozlacz Wi-Fi]");
    else {
      const WifiApEntry &ap = netSnap.aps[idx - 4];
      snprintf(line, sizeof(line), ">%.16s %ddB", ap.ssid, (int)ap.rssi);
    }
    if (idx == wifiMenuSel) {
      d.drawBox(0, y, 128, 9);
      d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 7, line);
    }
  }

  if (modalGetMode() != MODAL_NONE) rysujModal();
  uiFinishFrame();
}
#else
static bool btTick(unsigned long) { return false; }
static bool btKey(uint8_t k) { if (k == KEY_ESC) uiSetScreen(APP_SETTINGS_MENU); return true; }
static void btDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer(); rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr); d.drawStr(4, 30, "Brak BT (Pico)");
  uiFinishFrame();
}
static bool wifiTick(unsigned long) { return false; }
static bool wifiKey(uint8_t k) { if (k == KEY_ESC) uiSetScreen(APP_SETTINGS_MENU); return true; }
static void wifiDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer(); rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr); d.drawStr(4, 30, "Brak Wi-Fi (Pico)");
  uiFinishFrame();
}
#endif

// --- Kontrast ---
static bool contrastKey(uint8_t k) {
  if (k == KEY_ESC || k == '\n') {
    storageSaveConfig();
    uiSetScreen(APP_SETTINGS_MENU);
    return true;
  }
  if ((k == KEY_LEFT || k == KEY_DOWN) && sysConfig.contrast > 0) {
    sysConfig.contrast--;
    uiDisplay().setContrast(sysConfig.contrast * 4);
  } else if ((k == KEY_RIGHT || k == KEY_UP) && sysConfig.contrast < 63) {
    sysConfig.contrast++;
    uiDisplay().setContrast(sysConfig.contrast * 4);
  }
  return true;
}

static void contrastDraw() {
  U8G2 &d = uiDisplay();
  char buf[20];
  snprintf(buf, sizeof(buf), "%d / 63", sysConfig.contrast);
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_6x10_tr); d.drawStr(24, 22, "KONTRAST LCD");
  d.drawFrame(12, 28, 104, 10);
  int fillW = (sysConfig.contrast * 100) / 63;
  if (fillW > 0) d.drawBox(14, 30, fillW, 6);
  d.drawStr(44, 50, buf);
  d.setFont(u8g2_font_5x7_tr); d.drawStr(10, 62, "</> Zmien  Esc:Zapisz");
  uiFinishFrame();
}

// --- Czas ---
static void timeEnter() { editTimeField = 0; }

static bool timeKey(uint8_t k) {
  if (k == KEY_ESC || k == '\n') { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_LEFT || k == KEY_RIGHT) editTimeField = 1 - editTimeField;
  else if (k == KEY_UP || k == KEY_DOWN) {
    int h, m;
    getClock(h, m);
    int delta = (k == KEY_UP) ? 1 : -1;
    if (editTimeField == 0) setClock(h + delta, m);
    else                    setClock(h, m + delta);
  }
  return true;
}

static void timeDraw() {
  U8G2 &d = uiDisplay();
  int h, m;
  getClock(h, m);
  char hBuf[4], mBuf[4];
  snprintf(hBuf, sizeof(hBuf), "%02d", h);
  snprintf(mBuf, sizeof(mBuf), "%02d", m);

  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_6x10_tr); d.drawStr(26, 21, "USTAW GODZINE");
  d.setFont(u8g2_font_10x20_tr);
  if (editTimeField == 0) {
    d.drawBox(32, 26, 24, 20);
    d.setDrawColor(0); d.drawStr(34, 43, hBuf); d.setDrawColor(1);
  } else {
    d.drawFrame(32, 26, 24, 20); d.drawStr(34, 43, hBuf);
  }
  d.drawStr(59, 43, ":");
  if (editTimeField == 1) {
    d.drawBox(72, 26, 24, 20);
    d.setDrawColor(0); d.drawStr(74, 43, mBuf); d.setDrawColor(1);
  } else {
    d.drawFrame(72, 26, 24, 20); d.drawStr(74, 43, mBuf);
  }
  d.setFont(u8g2_font_5x7_tr); d.drawStr(6, 62, "</> Pole ^/v Zmien Esc:OK");
  uiFinishFrame();
}

// --- Formatowanie ---
static bool formatKey(uint8_t k) {
  if (modalGetMode() != MODAL_NONE) {
    bool confirmed = false, submitted = false;
    modalHandleKey(k, confirmed, submitted);
    if (confirmed && modalGetMode() == MODAL_CONFIRM_FMT) {
      btAudioSendCmd(ACMD_STOP);
      netFtpSendCmd(NCMD_FTP_STOP);
      vTaskDelay(pdMS_TO_TICKS(120));
      bool ok = (pendingFmtTarget == 0) ? storageFormatFlash() : storageFormatSD();
      if (pendingFmtTarget == 0) {
        uiDisplay().setContrast(sysConfig.contrast * 4);
      } else {
        btAudioSendCmd(ACMD_SCAN_SD);
      }
      showToast(ok ? "Sformatowano!" : "Blad formatowania");
      modalClose();
    }
    return true;
  }

  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_UP || k == KEY_DOWN) fmtSel = 1 - fmtSel;
  else if (k == '\n' || k == ' ') {
    pendingFmtTarget = fmtSel;
    modalOpenConfirm(MODAL_CONFIRM_FMT, (fmtSel == 0) ? "Format Flash (LittleFS)" : "Format SD (FAT32)");
  }
  return true;
}

static void formatDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, "FORMATOWANIE PAMIECI");
  d.drawHLine(0, 20, 128);

  const char* opts[2] = { "Formatuj Flash (LittleFS)", "Formatuj karte SD (FAT32)" };
  for (int i = 0; i < 2; i++) {
    int y = 26 + i * 12;
    if (i == fmtSel) {
      d.drawBox(0, y, 128, 10);
      d.setDrawColor(0); d.drawStr(2, y + 8, opts[i]); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 8, opts[i]);
    }
  }
  d.drawStr(2, 62, "Ent:Wybierz  Esc:Powrot");

  if (modalGetMode() != MODAL_NONE) rysujModal();
  rysujToastJesliAktywny();
  uiFinishFrame();
}

// --- About ---
static void aboutEnter() { lastAboutMs = millis(); }

static bool aboutTick(unsigned long now) {
  if (now - lastAboutMs >= 1000UL) {
    lastAboutMs = now;
    return true;
  }
  return false;
}

static bool aboutKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  return false;
}

static void aboutDraw() {
  U8G2 &d = uiDisplay();
  int freeRam = rp2040.getFreeHeap();
  char ramLine[28];
  snprintf(ramLine, sizeof(ramLine), "Wolny RAM: %d B (%dK)", freeRam, freeRam / 1024);

  d.clearBuffer();
  rysujPasekStanu();

  rysujHollowStar(12, 20, true);
  d.setFont(u8g2_font_6x10_tr);
  d.drawStr(23, 20, OS_BRAND_NAME);
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(23, 29, OS_BRAND_EDITION " " OS_BRAND_VERSION " | " HSOS_BOARD_TAG);

  d.drawHLine(4, 32, 120);
#if HSOS_HAS_WIRELESS
  d.drawStr(4, 41, "BT A2DP | Jack | WiFi FTP");
#else
  d.drawStr(4, 41, "PWM Jack | SD | LittleFS");
#endif
  d.drawStr(4, 50, ramLine);
  d.drawStr(14, 62, "Esc:Powrot  F1:Skroty");
  uiFinishFrame();
}

static const char* const HELP_SET_BT[] = {
  "^ / v: Wybor pozycji",
  "Ent/Spc: Skanuj / Polacz",
  "Esc: Menu ustawien",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SET_WIFI[] = {
  "^ / v: Wybor sieci/akcji",
  "Ent/Spc: Skan / NTP / OK",
  "Esc: Menu ustawien",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SET_CONTRAST[] = {
  "</v: Zmniejsz kontrast",
  ">/ ^: Zwieksz kontrast",
  "Ent/Esc: Zapisz i wroc",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SET_TIME[] = {
  "</>: Wybierz Godz / Min",
  "^ / v: Zmien wartosc",
  "Ent/Esc: Zatwierdz",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SET_FORMAT[] = {
  "^ / v: Wybierz Flash / SD",
  "Ent/Spc: Formatuj (potw.)",
  "Esc: Menu ustawien",
  "F1: Skroty klawiszowe"
};
static const char* const HELP_SET_ABOUT[] = {
  "Auto-odswiezanie RAM: 1s",
  "Esc: Menu ustawien",
  "F1: Skroty klawiszowe"
};

const AppScreen screenSetBt       = { nullptr,    btTick,    btKey,       btDraw,       HELP_SET_BT,       4 };
const AppScreen screenSetWifi     = { nullptr,    wifiTick,  wifiKey,     wifiDraw,     HELP_SET_WIFI,     4 };
const AppScreen screenSetContrast = { nullptr,    nullptr,   contrastKey, contrastDraw, HELP_SET_CONTRAST, 4 };
const AppScreen screenSetTime     = { timeEnter,  nullptr,   timeKey,     timeDraw,     HELP_SET_TIME,     4 };
const AppScreen screenSetFormat   = { nullptr,    nullptr,   formatKey,   formatDraw,   HELP_SET_FORMAT,   4 };
const AppScreen screenSetAbout    = { aboutEnter, aboutTick, aboutKey,    aboutDraw,    HELP_SET_ABOUT,    3 };
