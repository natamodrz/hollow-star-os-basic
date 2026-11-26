#include "app_settings.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../bt_audio.h"
#include "../../net_ftp.h"

static int fmtSel = 0;
static int pendingFmtTarget = 0;
static int editTimeField = 0;
static int langOptSel = 0;
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
      showToast(tr("Unplug jack first!", "Wyl. gdy Jack wpiety!"));
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
    d.drawStr(2, 18, tr("BT disabled (Jack)", "BT wylaczony (Jack)"));
  } else if (audioSnap.btScanning) {
    char anim[24];
    const char* dots[4] = {"", ".", "..", "..."};
    snprintf(anim, sizeof(anim), "%s%s", tr("Scanning BT", "Skanuje BT"), dots[animFrame & 3]);
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
    if (idx == 0)      snprintf(line, sizeof(line), "%s", tr("[Scan BT devices]", "[Skanuj urzadzenia]"));
    else if (idx == 1) snprintf(line, sizeof(line), "%s", tr("[Disconnect audio]", "[Rozlacz audio]"));
    else if (idx == 2) snprintf(line, sizeof(line), "%s", tr("[Clear pairings]", "[Wyczysc parowanie]"));
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
      snprintf(title, sizeof(title), "%s %.16s", tr("Pass:", "Haslo:"), pendingWifiSsid);
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
    if (idx == 0)      snprintf(line, sizeof(line), "%s", tr("[Scan Wi-Fi networks]", "[Skanuj sieci Wi-Fi]"));
    else if (idx == 1) snprintf(line, sizeof(line), "%s", tr("[Connect saved Wi-Fi]", "[Polacz z zapisana]"));
    else if (idx == 2) snprintf(line, sizeof(line), "%s", tr("[Sync NTP time]", "[Synchronizuj NTP]"));
    else if (idx == 3) snprintf(line, sizeof(line), "%s", tr("[Disconnect Wi-Fi]", "[Rozlacz Wi-Fi]"));
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
  d.setFont(u8g2_font_5x7_tr); d.drawStr(4, 30, "No BT (Pico)");
  uiFinishFrame();
}
static bool wifiTick(unsigned long) { return false; }
static bool wifiKey(uint8_t k) { if (k == KEY_ESC) uiSetScreen(APP_SETTINGS_MENU); return true; }
static void wifiDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer(); rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr); d.drawStr(4, 30, "No Wi-Fi (Pico)");
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
  d.setFont(u8g2_font_6x10_tr); d.drawStr(24, 22, tr("LCD CONTRAST", "KONTRAST LCD"));
  d.drawFrame(12, 28, 104, 10);
  int fillW = (sysConfig.contrast * 100) / 63;
  if (fillW > 0) d.drawBox(14, 30, fillW, 6);
  d.drawStr(44, 50, buf);
  d.setFont(u8g2_font_5x7_tr); d.drawStr(10, 62, tr("</> Change  Esc:Save", "</> Zmien  Esc:Zapisz"));
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
  d.setFont(u8g2_font_6x10_tr); d.drawStr(28, 21, tr("SET SYSTEM TIME", "USTAW GODZINE"));
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
  d.setFont(u8g2_font_5x7_tr); d.drawStr(6, 62, tr("</> Field ^/v Chg Esc:OK", "</> Pole ^/v Zmien Esc:OK"));
  uiFinishFrame();
}

// --- Język / Language ---
static void langEnter() {
  langOptSel = (sysConfig.lang == 1) ? 1 : 0;
}

static bool langKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_UP)        langOptSel = (langOptSel + 2) % 3;
  else if (k == KEY_DOWN) langOptSel = (langOptSel + 1) % 3;
  else if (k == '\n' || k == ' ') {
    if (langOptSel < 2) {
      sysConfig.lang = (uint8_t)langOptSel;
      storageSaveConfig();
      showToast(sysConfig.lang == 1 ? "Jezyk: Polski" : "Language: English");
    } else {
      uiSetScreen(APP_SETUP);
    }
  }
  return true;
}

static void langDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("SYSTEM LANGUAGE", "JEZYK SYSTEMU"));
  d.drawHLine(0, 20, 128);

  const char* opts[3] = {
    "English (Default)",
    "Polski",
    tr("[Run 1st Setup Wizard]", "[Uruchom 1st Setup]")
  };
  for (int i = 0; i < 3; i++) {
    int y = 22 + i * 10;
    char line[28];
    if (i < 2 && sysConfig.lang == (uint8_t)i) snprintf(line, sizeof(line), "* %s", opts[i]);
    else                                       snprintf(line, sizeof(line), "  %s", opts[i]);
    if (i == langOptSel) {
      d.drawBox(0, y, 128, 9);
      d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 7, line);
    }
  }
  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Ent:Apply Esc:Back F1:?", "Ent:Wybierz Esc:Wstecz"));
  rysujToastJesliAktywny();
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
      showToast(ok ? tr("Formatted!", "Sformatowano!") : tr("Format failed!", "Blad formatowania"));
      modalClose();
    }
    return true;
  }

  if (k == KEY_ESC) { uiSetScreen(APP_SETTINGS_MENU); return true; }
  if (k == KEY_UP || k == KEY_DOWN) fmtSel = 1 - fmtSel;
  else if (k == '\n' || k == ' ') {
    pendingFmtTarget = fmtSel;
    modalOpenConfirm(MODAL_CONFIRM_FMT, (fmtSel == 0)
                     ? tr("Format Flash (LittleFS)", "Format Flash (LittleFS)")
                     : tr("Format SD (FAT32)", "Format SD (FAT32)"));
  }
  return true;
}

static void formatDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("FORMAT STORAGE", "FORMATOWANIE PAMIECI"));
  d.drawHLine(0, 20, 128);

  const char* opts[2] = {
    tr("Format Flash (LittleFS)", "Formatuj Flash (LittleFS)"),
    tr("Format SD Card (FAT32)", "Formatuj karte SD (FAT32)")
  };
  for (int i = 0; i < 2; i++) {
    int y = 26 + i * 12;
    if (i == fmtSel) {
      d.drawBox(0, y, 128, 10);
      d.setDrawColor(0); d.drawStr(2, y + 8, opts[i]); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 8, opts[i]);
    }
  }
  d.drawStr(2, 62, tr("Ent:Select  Esc:Back", "Ent:Wybierz  Esc:Powrot"));

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
  snprintf(ramLine, sizeof(ramLine), "%s %d B (%dK)",
           tr("Free RAM:", "Wolny RAM:"), freeRam, freeRam / 1024);

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
  d.drawStr(14, 62, tr("Esc:Back  F1:Shortcuts", "Esc:Powrot  F1:Skroty"));
  uiFinishFrame();
}

static const char* const HELP_SET_BT_EN[] = { "^/v: Select", "Ent/Spc: Scan/Connect", "Esc: Settings menu", "F1: Shortcuts" };
static const char* const HELP_SET_BT_PL[] = { "^/v: Wybor pozycji", "Ent/Spc: Skanuj/Polacz", "Esc: Menu ustawien", "F1: Skroty" };

static const char* const HELP_SET_WIFI_EN[] = { "^/v: Select AP/Action", "Ent/Spc: Scan/NTP/OK", "Esc: Settings menu", "F1: Shortcuts" };
static const char* const HELP_SET_WIFI_PL[] = { "^/v: Wybor sieci/akcji", "Ent/Spc: Skan/NTP/OK", "Esc: Menu ustawien", "F1: Skroty" };

static const char* const HELP_SET_CONTRAST_EN[] = { "</v: Decrease contrast", ">/^: Increase contrast", "Ent/Esc: Save & back", "F1: Shortcuts" };
static const char* const HELP_SET_CONTRAST_PL[] = { "</v: Zmniejsz kontrast", ">/^: Zwieksz kontrast", "Ent/Esc: Zapisz i wroc", "F1: Skroty" };

static const char* const HELP_SET_TIME_EN[] = { "</>: Select Hour/Min", "^/v: Change value", "Ent/Esc: Confirm", "F1: Shortcuts" };
static const char* const HELP_SET_TIME_PL[] = { "</>: Wybierz Godz/Min", "^/v: Zmien wartosc", "Ent/Esc: Zatwierdz", "F1: Skroty" };

static const char* const HELP_SET_LANG_EN[] = { "^/v: Choose language", "Ent/Spc: Apply/Setup", "Esc: Settings menu", "F1: Shortcuts" };
static const char* const HELP_SET_LANG_PL[] = { "^/v: Wybierz jezyk", "Ent/Spc: Zastosuj", "Esc: Menu ustawien", "F1: Skroty" };

static const char* const HELP_SET_FORMAT_EN[] = { "^/v: Flash / SD", "Ent/Spc: Format (confirm)", "Esc: Settings menu", "F1: Shortcuts" };
static const char* const HELP_SET_FORMAT_PL[] = { "^/v: Flash / SD", "Ent/Spc: Formatuj (potw.)", "Esc: Menu ustawien", "F1: Skroty" };

static const char* const HELP_SET_ABOUT_EN[] = { "Auto-refresh RAM: 1s", "Esc: Settings menu", "F1: Shortcuts" };
static const char* const HELP_SET_ABOUT_PL[] = { "Auto-odswiezanie RAM: 1s", "Esc: Menu ustawien", "F1: Skroty" };

const AppScreen screenSetBt       = { nullptr,    btTick,    btKey,       btDraw,       HELP_SET_BT_EN,       HELP_SET_BT_PL,       4 };
const AppScreen screenSetWifi     = { nullptr,    wifiTick,  wifiKey,     wifiDraw,     HELP_SET_WIFI_EN,     HELP_SET_WIFI_PL,     4 };
const AppScreen screenSetContrast = { nullptr,    nullptr,   contrastKey, contrastDraw, HELP_SET_CONTRAST_EN, HELP_SET_CONTRAST_PL, 4 };
const AppScreen screenSetTime     = { timeEnter,  nullptr,   timeKey,     timeDraw,     HELP_SET_TIME_EN,     HELP_SET_TIME_PL,     4 };
const AppScreen screenSetLang     = { langEnter,  nullptr,   langKey,     langDraw,     HELP_SET_LANG_EN,     HELP_SET_LANG_PL,     4 };
const AppScreen screenSetFormat   = { nullptr,    nullptr,   formatKey,   formatDraw,   HELP_SET_FORMAT_EN,   HELP_SET_FORMAT_PL,   4 };
const AppScreen screenSetAbout    = { aboutEnter, aboutTick, aboutKey,    aboutDraw,    HELP_SET_ABOUT_EN,    HELP_SET_ABOUT_PL,    3 };
