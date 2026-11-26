#include "app_setup.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../net_ftp.h"

enum SetupStep : uint8_t {
  SETUP_LANG = 0,
  SETUP_WIFI,
  SETUP_TIME,
  SETUP_WELCOME
};

static SetupStep setupStep = SETUP_LANG;
static int langSel = 0;
static int wifiSel = 0;
static int timeField = 0;
static char pendingSsid[36] = {0};
static unsigned long lastAnimMs = 0;
static uint8_t animFrame = 0;

static void setupEnter() {
  setupStep = SETUP_LANG;
  langSel = (sysConfig.lang == 1) ? 1 : 0;
  wifiSel = 0;
  timeField = 0;
  animFrame = 0;
  lastAnimMs = millis();
}

static bool setupTick(unsigned long now) {
  if (setupStep == SETUP_WIFI && netSnap.wifiBusy) {
    if (now - lastAnimMs >= 300UL) {
      lastAnimMs = now;
      animFrame++;
      return true;
    }
  }
  return false;
}

static bool setupKey(uint8_t k) {
  if (setupStep == SETUP_LANG) {
    if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) {
      langSel = 1 - langSel;
      sysConfig.lang = (uint8_t)langSel;
    } else if (k == '\n' || k == ' ') {
      sysConfig.lang = (uint8_t)langSel;
      storageSaveConfig();
#if HSOS_HAS_WIRELESS
      setupStep = SETUP_WIFI;
      wifiSel = 0;
#else
      setupStep = SETUP_TIME;
      timeField = 0;
#endif
    }
    return true;
  }

#if HSOS_HAS_WIRELESS
  if (setupStep == SETUP_WIFI) {
    if (modalGetMode() != MODAL_NONE) {
      bool confirmed = false, submitted = false;
      modalHandleKey(k, confirmed, submitted);
      if (submitted && modalGetMode() == MODAL_WIFI_PASS) {
        strncpy(sysConfig.wifiSsid, pendingSsid, sizeof(sysConfig.wifiSsid) - 1);
        sysConfig.wifiSsid[sizeof(sysConfig.wifiSsid) - 1] = '\0';
        strncpy(sysConfig.wifiPass, modalGetInput(), sizeof(sysConfig.wifiPass) - 1);
        sysConfig.wifiPass[sizeof(sysConfig.wifiPass) - 1] = '\0';
        storageSaveConfig();
        netFtpSendCmd(NCMD_WIFI_CONNECT_SAVED);
        modalClose();
        wifiSel = 1; // Podświetl [Dalej / Next] po wpisaniu hasła
      }
      return true;
    }

    if (k == KEY_TAB || k == KEY_ESC) {
      setupStep = SETUP_TIME;
      timeField = 0;
      return true;
    }

    int total = 2 + netSnap.apCount;
    if (k == KEY_UP)        wifiSel = (wifiSel + total - 1) % total;
    else if (k == KEY_DOWN) wifiSel = (wifiSel + 1) % total;
    else if (k == '\n' || k == ' ') {
      if (wifiSel == 0 && !netSnap.wifiBusy) {
        netFtpSendCmd(NCMD_WIFI_SCAN);
      } else if (wifiSel == 1) {
        setupStep = SETUP_TIME;
        timeField = 0;
      } else if (wifiSel >= 2 && !netSnap.wifiBusy) {
        strncpy(pendingSsid, netSnap.aps[wifiSel - 2].ssid, sizeof(pendingSsid) - 1);
        pendingSsid[sizeof(pendingSsid) - 1] = '\0';
        char title[26];
        snprintf(title, sizeof(title), "%s %.16s", tr("Pass:", "Haslo:"), pendingSsid);
        modalOpenInput(MODAL_WIFI_PASS, title);
      }
    }
    return true;
  }
#endif

  if (setupStep == SETUP_TIME) {
    if (k == KEY_LEFT || k == KEY_RIGHT) {
      timeField = 1 - timeField;
    } else if (k == KEY_UP || k == KEY_DOWN) {
      int h, m;
      getClock(h, m);
      int delta = (k == KEY_UP) ? 1 : -1;
      if (timeField == 0) setClock(h + delta, m);
      else                setClock(h, m + delta);
    } else if (k == '\n' || k == ' ') {
      setupStep = SETUP_WELCOME;
    } else if (k == KEY_ESC) {
#if HSOS_HAS_WIRELESS
      setupStep = SETUP_WIFI;
#else
      setupStep = SETUP_LANG;
#endif
    }
    return true;
  }

  // SETUP_WELCOME
  if (k == '\n' || k == ' ' || k == KEY_ESC) {
    sysConfig.setupDone = true;
    storageSaveConfig();
    uiSetScreen(APP_MENU);
  }
  return true;
}

static void drawLangStep(U8G2 &d) {
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, "1. LANGUAGE / JEZYK");
  d.drawHLine(0, 20, 128);

  const char* opts[2] = { "English (Default)", "Polski" };
  for (int i = 0; i < 2; i++) {
    int y = 24 + i * 12;
    if (i == langSel) {
      d.drawBox(0, y, 128, 10);
      d.setDrawColor(0); d.drawStr(6, y + 8, opts[i]); d.setDrawColor(1);
    } else {
      d.drawStr(6, y + 8, opts[i]);
    }
  }
  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, "^/v:Pick  Ent:Next");
}

#if HSOS_HAS_WIRELESS
static void drawWifiStep(U8G2 &d) {
  d.setFont(u8g2_font_5x7_tr);
  if (netSnap.wifiBusy) {
    const char* dots[4] = {"", ".", "..", "..."};
    char hdr[26];
    snprintf(hdr, sizeof(hdr), "2. %.17s%s", netSnap.wifiStatus, dots[animFrame & 3]);
    d.drawStr(2, 18, hdr);
  } else {
    char hdr[26];
    snprintf(hdr, sizeof(hdr), "2.WI-FI: %.15s", netSnap.wifiStatus);
    d.drawStr(2, 18, hdr);
  }
  d.drawHLine(0, 20, 128);

  int total = 2 + netSnap.apCount;
  if (wifiSel >= total) wifiSel = total - 1;
  int startIdx = max(0, min(wifiSel - 1, total - 3));
  for (int i = 0; i < 3 && (startIdx + i) < total; i++) {
    int idx = startIdx + i;
    int y = 22 + i * 10;
    char line[28];
    if (idx == 0) {
      snprintf(line, sizeof(line), "%s", tr("[Scan Wi-Fi networks]", "[Skanuj sieci Wi-Fi]"));
    } else if (idx == 1) {
      snprintf(line, sizeof(line), "%s",
               netSnap.wifiConnected ? tr("[Next: Set Time ->]", "[Dalej: Ustaw czas ->]")
                                     : tr("[Skip Wi-Fi ->]", "[Pomin Wi-Fi ->]"));
    } else {
      const WifiApEntry &ap = netSnap.aps[idx - 2];
      snprintf(line, sizeof(line), ">%.16s %ddB", ap.ssid, (int)ap.rssi);
    }
    if (idx == wifiSel) {
      d.drawBox(0, y, 128, 9);
      d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 7, line);
    }
  }
  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Ent:Select  Tab:Next", "Ent:Wybierz  Tab:Dalej"));
  if (modalGetMode() != MODAL_NONE) rysujModal();
}
#endif

static void drawTimeStep(U8G2 &d) {
  int h, m;
  getClock(h, m);
  char hBuf[4], mBuf[4];
  snprintf(hBuf, sizeof(hBuf), "%02d", h);
  snprintf(mBuf, sizeof(mBuf), "%02d", m);

  d.setFont(u8g2_font_6x10_tr);
#if HSOS_HAS_WIRELESS
  d.drawStr(16, 21, tr("3. SET TIME", "3. USTAW CZAS"));
#else
  d.drawStr(16, 21, tr("2. SET TIME", "2. USTAW CZAS"));
#endif
  d.setFont(u8g2_font_10x20_tr);
  if (timeField == 0) {
    d.drawBox(32, 26, 24, 20);
    d.setDrawColor(0); d.drawStr(34, 43, hBuf); d.setDrawColor(1);
  } else {
    d.drawFrame(32, 26, 24, 20); d.drawStr(34, 43, hBuf);
  }
  d.drawStr(59, 43, ":");
  if (timeField == 1) {
    d.drawBox(72, 26, 24, 20);
    d.setDrawColor(0); d.drawStr(74, 43, mBuf); d.setDrawColor(1);
  } else {
    d.drawFrame(72, 26, 24, 20); d.drawStr(74, 43, mBuf);
  }
  d.setFont(u8g2_font_5x7_tr);
  if (netSnap.ntpSynced) d.drawStr(40, 53, "NTP OK");
  d.drawStr(4, 62, tr("</>:Field ^/v:Chg Ent:OK", "</>:Pole ^/v:Zm Ent:OK"));
}

static void drawWelcomeStep(U8G2 &d) {
  d.drawFrame(0, 0, 128, 64);
  d.drawFrame(2, 2, 124, 60);

  rysujHollowStar(23, 25, false);

  d.setFont(u8g2_font_9x15_tr);
  d.drawStr(42, 24, tr("Welcome!", "Witaj!"));

  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(42, 34, OS_BRAND_NAME);
  d.drawStr(42, 42, HSOS_BOARD_TAG " " OS_BRAND_VERSION);

  d.drawHLine(6, 47, 116);
  d.drawStr(10, 57, tr("Press Enter to start", "Nacisnij Enter -> Start"));
}

static void setupDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  d.setDrawColor(1);

  if (setupStep == SETUP_WELCOME) {
    drawWelcomeStep(d);
    uiFinishFrame();
    return;
  }

  rysujPasekStanu();
  if (setupStep == SETUP_LANG) {
    drawLangStep(d);
#if HSOS_HAS_WIRELESS
  } else if (setupStep == SETUP_WIFI) {
    drawWifiStep(d);
#endif
  } else {
    drawTimeStep(d);
  }
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_SETUP_EN[] = {
  "1st Time Setup Wizard",
  "^/v: Navigate options",
  "</>: Edit / Field",
  "Enter: Confirm & Next",
  "Tab: Skip Wi-Fi step"
};
static const char* const HELP_SETUP_PL[] = {
  "Kreator 1. uruchomienia",
  "^/v: Wybor opcji",
  "</>: Zmiana wartosci",
  "Enter: Zatwierdz i dalej",
  "Tab: Pomin krok Wi-Fi"
};

const AppScreen screenAppSetup = { setupEnter, setupTick, setupKey, setupDraw, HELP_SETUP_EN, HELP_SETUP_PL, 5 };
