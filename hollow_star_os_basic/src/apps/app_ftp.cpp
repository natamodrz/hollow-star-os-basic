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
  showToast(ftpDriveSD ? tr("Drive: SD Card", "Dysk: Karta SD") : tr("Drive: Flash", "Dysk: Flash"));
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
        showToast(tr("FTP user saved!", "Zapisano login FTP!"));
      } else {
        showToast(tr("User cannot be empty", "Login nie moze byc pusty"));
      }
      modalClose();
    } else if (submitted && modalGetMode() == MODAL_FTP_PASS) {
      const char* val = modalGetInput();
      if (val[0]) {
        strncpy(sysConfig.ftpPass, val, sizeof(sysConfig.ftpPass) - 1);
        sysConfig.ftpPass[sizeof(sysConfig.ftpPass) - 1] = '\0';
        storageSaveConfig();
        netFtpReloadCreds();
        showToast(tr("FTP pass saved!", "Zapisano haslo FTP!"));
      } else {
        showToast(tr("Pass cannot be empty", "Haslo nie moze byc puste"));
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
    modalOpenInput(MODAL_FTP_USER, tr("FTP Username:", "Uzytkownik FTP:"), sysConfig.ftpUser);
    return true;
  }
  if (k == 'p' || k == 'P') {
    modalOpenInput(MODAL_FTP_PASS, tr("FTP Password:", "Haslo FTP:"), sysConfig.ftpPass);
    return true;
  }
  if (k == 'r' || k == 'R') {
    bool ok = storageMountSD();
    showToast(ok ? tr("SD mounted!", "SD zamontowana!") : tr("No SD card!", "Brak karty SD!"));
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

  d.drawStr(2, 17, tr("FTP SERVER (PASV :21)", "SERWER FTP (PASV :21)"));
  char buf[28];
  if (ftpDriveSD) {
    rysujIkoneKartaSD(2, 20);
    snprintf(buf, sizeof(buf), "%s<SD Card> %s", tr("Drv:", "Dysk:"), sdMounted ? "OK" : tr("NO", "BRAK"));
  } else {
    rysujIkoneCzip(2, 20);
    snprintf(buf, sizeof(buf), "%s<Flash LittleFS>", tr("Drv:", "Dysk:"));
  }
  d.drawStr(11, 26, buf);

  if (netSnap.wifiConnected) {
    snprintf(buf, sizeof(buf), "IP: %s", netSnap.ipStr);
    d.drawStr(2, 34, buf);
    d.drawStr(2, 42, netSnap.ftpStatus);
  } else {
    d.drawStr(2, 34, tr("No Wi-Fi! Configure in", "Brak Wi-Fi! Ustaw w"));
    d.drawStr(2, 42, tr("Settings -> Wi-Fi", "Ustawienia -> Wi-Fi"));
  }

  snprintf(buf, sizeof(buf), "U:%.8s  P:%.8s", sysConfig.ftpUser, sysConfig.ftpPass);
  d.drawStr(2, 51, buf);

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, netSnap.ftpRunning
                   ? tr("Ent:Stop U/P:Acct </>:D", "Ent:Stop U/P:Kont </>:D")
                   : tr("Ent:Start U/P:Acct F1:?", "Ent:Start U/P:Kont F1:?"));

  if (modalGetMode() != MODAL_NONE) rysujModal();
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_FTP_EN[] = {
  "</>/Tab/D: Flash / SD",
  "U: Edit FTP username",
  "P: Edit FTP password",
  "Enter/Spc: Start / Stop",
  "R: Mount / Refresh SD",
  "Esc: Tools menu"
};
static const char* const HELP_FTP_PL[] = {
  "</>/Tab/D: Zmien Flash/SD",
  "U: Zmien uzytkownika FTP",
  "P: Zmien haslo FTP",
  "Enter/Spc: Start / Stop",
  "R: Montuj/Odswiez SD",
  "Esc: Menu narzedzi"
};

const AppScreen screenAppFtp = { nullptr, nullptr, ftpKey, ftpDraw, HELP_FTP_EN, HELP_FTP_PL, 6 };

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
  d.drawStr(4, 30, "No Wi-Fi (Pico)");
  d.drawStr(4, 62, "Esc: Back");
  uiFinishFrame();
}

const AppScreen screenAppFtp = { nullptr, nullptr, ftpKey, ftpDraw, nullptr, nullptr, 0 };

#endif
