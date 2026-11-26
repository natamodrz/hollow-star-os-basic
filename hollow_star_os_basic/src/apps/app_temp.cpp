#include "app_temp.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"

static unsigned long lastInfoMs = 0;
static bool flashOk = false;
static uint64_t flashFreeB = 0;
static uint64_t flashTotalB = 0;
static bool sdOk = false;
static uint64_t sdFreeB = 0;
static uint64_t sdTotalB = 0;

static void refreshDiskStats(bool remountSD) {
  flashOk = storageGetFlashSpace(flashFreeB, flashTotalB);
  if (remountSD && !sdMounted) {
    storageMountSD();
  }
  sdOk = storageGetSDSpace(sdFreeB, sdTotalB);
}

static void fmtBytesCompact(char *out, size_t cap, uint64_t b) {
  if (b >= (1024ULL * 1024ULL * 1024ULL)) {
    uint32_t g10 = (uint32_t)((b * 10ULL) >> 30);
    snprintf(out, cap, "%lu.%luG", (unsigned long)(g10 / 10), (unsigned long)(g10 % 10));
  } else if (b >= (1024ULL * 1024ULL)) {
    uint32_t m10 = (uint32_t)((b * 10ULL) >> 20);
    snprintf(out, cap, "%lu.%luM", (unsigned long)(m10 / 10), (unsigned long)(m10 % 10));
  } else {
    uint32_t kb = (uint32_t)(b >> 10);
    snprintf(out, cap, "%luK", (unsigned long)kb);
  }
}

static void tempEnter() {
  lastInfoMs = millis();
  refreshDiskStats(false);
}

static bool tempTick(unsigned long now) {
  if (now - lastInfoMs >= 1000UL) {
    lastInfoMs = now;
    return true;
  }
  return false;
}

static bool tempKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(APP_MENU);
    return true;
  }
  if (k == 'r' || k == 'R') {
    refreshDiskStats(true);
    showToast(sdOk ? tr("Disks refreshed!", "Dyski odswiezone!") : tr("No SD card", "Brak karty SD"));
    return true;
  }
  return false;
}

static void tempDraw() {
  float temp = analogReadTemp();
  int t10 = (int)(temp * 10.0f + (temp >= 0.0f ? 0.5f : -0.5f));
  int whole = t10 / 10;
  int frac  = abs(t10 % 10);

  int usedRam  = rp2040.getUsedHeap();
  int totalRam = rp2040.getTotalHeap();
  int ramPct   = (totalRam > 0) ? ((usedRam * 100) / totalRam) : 0;

  char lineTemp[28], lineRam[28], lineFlash[28], lineSD[28];
  snprintf(lineTemp, sizeof(lineTemp), "%s %d.%d C",
           tr("CPU Temp :", "Temp CPU :"), whole, frac);
  snprintf(lineRam, sizeof(lineRam), "%s %dK/%dK %d%%",
           tr("RAM Used :", "RAM zaj. :"), usedRam / 1024, totalRam / 1024, ramPct);

  if (flashOk) {
    char f1[10], f2[10];
    fmtBytesCompact(f1, sizeof(f1), flashFreeB);
    fmtBytesCompact(f2, sizeof(f2), flashTotalB);
    snprintf(lineFlash, sizeof(lineFlash), "%s %s / %s",
             tr("Flash Free:", "Flash wol:"), f1, f2);
  } else {
    snprintf(lineFlash, sizeof(lineFlash), "%s %s",
             tr("Flash Free:", "Flash wol:"), tr("FS Err", "Blad FS"));
  }

  if (sdOk) {
    char s1[10], s2[10];
    fmtBytesCompact(s1, sizeof(s1), sdFreeB);
    fmtBytesCompact(s2, sizeof(s2), sdTotalB);
    snprintf(lineSD, sizeof(lineSD), "%s %s / %s",
             tr("SD Free  :", "SD wolne :"), s1, s2);
  } else {
    snprintf(lineSD, sizeof(lineSD), "%s %s",
             tr("SD Free  :", "SD wolne :"), tr("No SD card", "Brak karty"));
  }

  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("SYSTEM INFO (" HSOS_BOARD_TAG ")", "INFO SYSTEMOWE (" HSOS_BOARD_TAG ")"));
  d.drawHLine(0, 20, 128);
  d.drawStr(2, 28, lineTemp);
  d.drawStr(2, 36, lineRam);
  d.drawStr(2, 44, lineFlash);
  d.drawStr(2, 52, lineSD);
  d.drawHLine(0, 54, 128);
  d.drawStr(2, 62, tr("R:Disks  Esc:M  F1:Help", "R:Dyski  Esc:M  F1:Pom"));
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_TEMP_EN[] = {
  "Temp & RAM: every 1s",
  "R: Refresh Flash & SD",
  "Esc: Back to menu",
  "F1: Keyboard shortcuts"
};
static const char* const HELP_TEMP_PL[] = {
  "Temp i RAM: co 1 s",
  "R: Odswiez Flash i SD",
  "Esc: Powrot do menu",
  "F1: Skroty klawiszowe"
};

const AppScreen screenAppTemp = { tempEnter, tempTick, tempKey, tempDraw, HELP_TEMP_EN, HELP_TEMP_PL, 4 };
