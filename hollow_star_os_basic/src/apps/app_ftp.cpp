#include "app_ftp.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../net_ftp.h"

#if HSOS_HAS_WIRELESS

static bool ftpDriveSD = false;

static void ftpSwitchDrive() {
  ftpDriveSD = !ftpDriveSD;
  if (ftpDriveSD && !sdMounted) {
    storageMountSD();
  }
  if (netSnap.ftpRunning) {
    netFtpSendCmd(NCMD_FTP_SET_DRIVE, ftpDriveSD ? 1 : 0);
  }
  showToast(ftpDriveSD ? "Dysk: Karta SD" : "Dysk: Flash");
}

static bool ftpKey(uint8_t k) {
  if (modalGetMode() != MODAL_NONE) {
    bool confirmed = false, submitted = false;
    modalHandleKey(k, confirmed, submitted);
    if (submitted && modalGetMode() == MODAL_FTP_USER) {
      const char* val = modalGetInput();
      if (val[0]) {
        strncpy(sysConfig.ftpUser, val, sizeof(sysConfig.ftpUser) - 1);
        sysConfig.ftpUser[sizeof(sysConfig.ftpUser) - 1] = '\0';
        storageSaveConfig();
        netFtpReloadCreds();
        showToast("Zapisano login FTP!");
      } else {
        showToast("Login nie moze byc pusty");
      }
      modalClose();
    } else if (submitted && modalGetMode() == MODAL_FTP_PASS) {
      const char* val = modalGetInput();
      if (val[0]) {
        strncpy(sysConfig.ftpPass, val, sizeof(sysConfig.ftpPass) - 1);
        sysConfig.ftpPass[sizeof(sysConfig.ftpPass) - 1] = '\0';
        storageSaveConfig();
        netFtpReloadCreds();
        showToast("Zapisano haslo FTP!");
      } else {
        showToast("Haslo nie moze byc puste");
      }
      modalClose();
    }
    return true;
  }

  if (k == KEY_ESC) { uiSetScreen(APP_TOOLS_MENU); return true; }
  if (k == KEY_TAB || k == KEY_LEFT || k == KEY_RIGHT || k == 'd' || k == 'D') {
    ftpSwitchDrive();
    return true;
  }
  if (k == 'u' || k == 'U') {
    modalOpenInput(MODAL_FTP_USER, "Uzytkownik FTP:", sysConfig.ftpUser);
    return true;
  }
  if (k == 'p' || k == 'P') {
    modalOpenInput(MODAL_FTP_PASS, "Haslo FTP:", sysConfig.ftpPass);
    return true;
  }
  if (k == 'r' || k == 'R') {
    bool ok = storageMountSD();
    showToast(ok ? "SD zamontowana!" : "Brak karty SD!");
    return true;
  }
  if (k == '\n' || k == ' ') {
    if (netSnap.ftpRunning) netFtpSendCmd(NCMD_FTP_STOP);
    else                    netFtpSendCmd(NCMD_FTP_START, ftpDriveSD ? 1 : 0);
    return true;
  }
  return false;
}

static void ftpDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  d.drawStr(2, 17, "SERWER FTP (PASV :21)");
  char buf[28];
  if (ftpDriveSD) {
    snprintf(buf, sizeof(buf), "Dysk:<Karta SD> %s", sdMounted ? "OK" : "BRAK");
  } else {
    snprintf(buf, sizeof(buf), "Dysk:<Flash LittleFS>");
  }
  d.drawStr(2, 26, buf);

  if (netSnap.wifiConnected) {
    snprintf(buf, sizeof(buf), "IP: %s", netSnap.ipStr);
    d.drawStr(2, 34, buf);
    d.drawStr(2, 42, netSnap.ftpStatus);
  } else {
    d.drawStr(2, 34, "Brak Wi-Fi! Ustaw w");
    d.drawStr(2, 42, "Ustawienia -> Wi-Fi");
  }

  snprintf(buf, sizeof(buf), "U:%.8s  P:%.8s", sysConfig.ftpUser, sysConfig.ftpPass);
  d.drawStr(2, 51, buf);

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, netSnap.ftpRunning ? "Ent:Stop U/P:Kont </>:D" : "Ent:Start U/P:Kont F1:?");

  if (modalGetMode() != MODAL_NONE) rysujModal();
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_FTP[] = {
  "</>/Tab/D: Zmien Flash/SD",
  "U: Zmien uzytkownika FTP",
  "P: Zmien haslo FTP",
  "Enter/Spc: Start / Stop",
  "R: Montuj/Odswiez SD",
  "Esc: Menu narzedzi"
};

const AppScreen screenAppFtp = { nullptr, nullptr, ftpKey, ftpDraw, HELP_FTP, 6 };

#else

static bool ftpKey(uint8_t k) {
  if (k == KEY_ESC) uiSetScreen(APP_MENU);
  return true;
}

static void ftpDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(4, 30, "Brak Wi-Fi (Pico)");
  d.drawStr(4, 62, "Esc: Powrot");
  uiFinishFrame();
}

const AppScreen screenAppFtp = { nullptr, nullptr, ftpKey, ftpDraw, nullptr, 0 };

#endif
