#include "app_calc.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include <math.h>

static char calcA[16] = "";
static char calcB[16] = "";
static char calcWynik[16] = "";
static char calcOp = 0;

static double parseCalcDouble(const char* s) {
  if (!s || !s[0]) return 0.0;
  bool neg = false;
  int i = 0;
  if (s[i] == '-') { neg = true; i++; }
  double val = 0.0;
  while (s[i] >= '0' && s[i] <= '9') {
    val = val * 10.0 + (double)(s[i] - '0');
    i++;
  }
  if (s[i] == '.') {
    i++;
    double frac = 0.1;
    while (s[i] >= '0' && s[i] <= '9') {
      val += (double)(s[i] - '0') * frac;
      frac *= 0.1;
      i++;
    }
  }
  return neg ? -val : val;
}

static void formatCalcDouble(double w, char* out, size_t cap) {
  if (isnan(w) || isinf(w) || fabs(w) >= 100000000.0) {
    snprintf(out, cap, "Przepeln.");
    return;
  }
  long long iv = (long long)llround(w);
  if (fabs(w - (double)iv) < 1e-9) {
    snprintf(out, cap, "%lld", iv);
    return;
  }
  long long scaled = (long long)llround(w * 100.0);
  const char* sign = (scaled < 0) ? "-" : "";
  long long absS = (scaled < 0) ? -scaled : scaled;
  snprintf(out, cap, "%s%lld.%02lld", sign, absS / 100LL, absS % 100LL);
}

static void obliczKalkulator() {
  if (!calcA[0] || calcOp == 0 || !calcB[0]) return;
  double a = parseCalcDouble(calcA);
  double b = parseCalcDouble(calcB);
  double w = 0.0;
  if (calcOp == '+') w = a + b;
  else if (calcOp == '-') w = a - b;
  else if (calcOp == '*') w = a * b;
  else if (calcOp == '/') {
    if (b == 0.0) {
      snprintf(calcWynik, sizeof(calcWynik), "Blad /0");
      return;
    }
    w = a / b;
  }
  formatCalcDouble(w, calcWynik, sizeof(calcWynik));
}

static void appendDigit(char* buf, size_t cap, uint8_t k) {
  size_t len = strlen(buf);
  if (len + 1 >= cap) return;
  if (k == '.' && strchr(buf, '.') != nullptr) return;
  buf[len] = (char)k;
  buf[len + 1] = '\0';
}

static bool calcKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_MENU); return true; }
  if (k == KEY_DEL || k == 'c' || k == 'C') {
    calcA[0] = calcB[0] = calcWynik[0] = '\0';
    calcOp = 0;
    return true;
  }
  if (k == '\b') {
    if (calcWynik[0]) calcWynik[0] = '\0';
    else if (calcB[0]) calcB[strlen(calcB) - 1] = '\0';
    else if (calcOp != 0) calcOp = 0;
    else if (calcA[0]) calcA[strlen(calcA) - 1] = '\0';
    return true;
  }
  if ((k >= '0' && k <= '9') || k == '.') {
    if (calcWynik[0]) {
      snprintf(calcA, sizeof(calcA), "%s", (k == '.') ? "0." : "");
      if (k != '.') { calcA[0] = (char)k; calcA[1] = '\0'; }
      calcOp = 0;
      calcB[0] = calcWynik[0] = '\0';
    } else if (calcOp == 0) {
      appendDigit(calcA, sizeof(calcA), k);
    } else {
      appendDigit(calcB, sizeof(calcB), k);
    }
    return true;
  }
  if (k == '+' || k == '-' || k == '*' || k == '/') {
    if (calcWynik[0] && strcmp(calcWynik, "Blad /0") != 0 && strcmp(calcWynik, "Przepeln.") != 0) {
      strncpy(calcA, calcWynik, sizeof(calcA) - 1);
      calcA[sizeof(calcA) - 1] = '\0';
      calcB[0] = calcWynik[0] = '\0';
    }
    if (calcA[0]) calcOp = (char)k;
    return true;
  }
  if (k == '\n' || k == '=') {
    obliczKalkulator();
    return true;
  }
  return false;
}

static void calcDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_6x10_tr);
  d.drawStr(2, 20, "KALKULATOR");

  char wyr[36];
  if (calcOp != 0) snprintf(wyr, sizeof(wyr), "%s%c%s", calcA, calcOp, calcB);
  else             snprintf(wyr, sizeof(wyr), "%s", calcA);
  size_t wlen = strlen(wyr);
  const char* vis = (wlen > 20) ? (wyr + wlen - 20) : wyr;
  d.drawStr(4, 32, vis);

  if (calcWynik[0]) {
    d.setFont(u8g2_font_9x15_tr);
    char res[20];
    snprintf(res, sizeof(res), "=%.12s", calcWynik);
    d.drawStr(4, 49, res);
  }
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 62, "Ent:= Del:Clr Esc:Menu");
  d.sendBuffer();
}

const AppScreen screenAppCalc = { nullptr, nullptr, calcKey, calcDraw };
