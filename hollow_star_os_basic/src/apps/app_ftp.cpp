#include "app_ftp.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../net_ftp.h"

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
  if (k == KEY_ESC) { uiSetScreen(APP_TOOLS_MENU); return true; }
  if (k == KEY_TAB || k == KEY_LEFT || k == KEY_RIGHT || k == 'd' || k == 'D') {
    ftpSwitchDrive();
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

  d.drawStr(2, 18, "SERWER FTP (PASV :21)");
  char buf[28];
  if (ftpDriveSD) {
    snprintf(buf, sizeof(buf), "Dysk:<Karta SD> %s", sdMounted ? "OK" : "BRAK");
  } else {
    snprintf(buf, sizeof(buf), "Dysk:<Flash LittleFS>");
  }
  d.drawStr(2, 28, buf);

  if (netSnap.wifiConnected) {
    snprintf(buf, sizeof(buf), "IP: %s", netSnap.ipStr);
    d.drawStr(2, 37, buf);
    d.drawStr(2, 46, netSnap.ftpStatus);
  } else {
    d.drawStr(2, 38, "Brak Wi-Fi! Ustaw w");
    d.drawStr(2, 47, "Ustawienia -> Wi-Fi");
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, netSnap.ftpRunning ? "Ent:Stop </>:Dysk F1:?" : "Ent:Start </>:Dysk F1:?");
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_FTP[] = {
  "</>/Tab/D: Zmien Flash/SD",
  "Enter/Spc: Start / Stop",
  "R: Montuj/Odswiez SD",
  "Esc: Menu narzedzi",
  "F1: Skroty klawiszowe"
};

const AppScreen screenAppFtp = { nullptr, nullptr, ftpKey, ftpDraw, HELP_FTP, 5 };
