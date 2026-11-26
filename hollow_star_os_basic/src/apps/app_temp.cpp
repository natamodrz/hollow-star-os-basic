#include "app_temp.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"

static unsigned long lastTempMs = 0;

static void tempEnter() {
  lastTempMs = millis();
}

static bool tempTick(unsigned long now) {
  if (now - lastTempMs >= 1000UL) {
    lastTempMs = now;
    return true;
  }
  return false;
}

static bool tempKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(APP_MENU);
    return true;
  }
  return false;
}

static void tempDraw() {
  float temp = analogReadTemp();
  int t10 = (int)(temp * 10.0f + (temp >= 0.0f ? 0.5f : -0.5f));
  int whole = t10 / 10;
  int frac  = abs(t10 % 10);

  char buf[20];
  snprintf(buf, sizeof(buf), "%d.%d C", whole, frac);

  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_6x10_tr);  d.drawStr(10, 21, "Temperatura RP2040");
  d.setFont(u8g2_font_10x20_tr); d.drawStr(28, 45, buf);
  d.setFont(u8g2_font_5x7_tr);   d.drawStr(14, 62, "Esc:Powrot  F1:Skroty");
  uiFinishFrame();
}

static const char* const HELP_TEMP[] = {
  "Auto-odswiezanie: co 1 s",
  "Esc: Powrot do menu",
  "F1: Skroty klawiszowe"
};

const AppScreen screenAppTemp = { tempEnter, tempTick, tempKey, tempDraw, HELP_TEMP, 3 };
