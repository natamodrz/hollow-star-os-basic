#include "app_calc.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include <math.h>
#include <ctype.h>

static char expr[64] = "";
static char resPrimary[24] = "";
static char resAlt[24] = "";
static bool hasResult = false;
static bool lastEvalOk = false;
static double lastVal = 0.0;
static bool fracMode = false;

enum CalcErr : uint8_t {
  CALC_OK = 0,
  CALC_ERR_SYNTAX,
  CALC_ERR_PAREN,
  CALC_ERR_DIV0,
  CALC_ERR_DOMAIN,
  CALC_ERR_OVERFLOW
};

struct CalcParser {
  const char* p;
  CalcErr err;
  int depth;

  void skipSpaces() {
    while (*p == ' ') p++;
  }

  double parseExpr() {
    if (err != CALC_OK) return 0.0;
    double v = parseTerm();
    while (err == CALC_OK) {
      skipSpaces();
      if (*p == '+') { p++; v += parseTerm(); }
      else if (*p == '-') { p++; v -= parseTerm(); }
      else break;
    }
    return v;
  }

  double parseTerm() {
    if (err != CALC_OK) return 0.0;
    double v = parseUnary();
    while (err == CALC_OK) {
      skipSpaces();
      if (*p == '*') {
        p++;
        v *= parseUnary();
      } else if (*p == '/') {
        p++;
        double rhs = parseUnary();
        if (err != CALC_OK) return 0.0;
        if (rhs == 0.0) { err = CALC_ERR_DIV0; return 0.0; }
        v /= rhs;
      } else if (*p == '(' || strncmp(p, "sqrt(", 5) == 0 || strncmp(p, "cbrt(", 5) == 0) {
        // Mnożenie niejawne np. 2(3+4) lub 2sqrt(9)
        v *= parseUnary();
      } else {
        break;
      }
    }
    return v;
  }

  double parseUnary() {
    if (err != CALC_OK) return 0.0;
    skipSpaces();
    if (*p == '+') { p++; return parseUnary(); }
    if (*p == '-') { p++; return -parseUnary(); }
    return parsePower();
  }

  double parsePower() {
    if (err != CALC_OK) return 0.0;
    double base = parsePrimary();
    if (err != CALC_OK) return 0.0;
    skipSpaces();
    if (*p == '^') {
      p++;
      double expv = parseUnary(); // prawostronnie łączne; obsługuje np. 2^-2 i 2^3^2
      if (err != CALC_OK) return 0.0;
      if (base == 0.0 && expv < 0.0) { err = CALC_ERR_DIV0; return 0.0; }
      if (base < 0.0 && fabs(expv - round(expv)) > 1e-9) { err = CALC_ERR_DOMAIN; return 0.0; }
      double res = pow(base, expv);
      if (isnan(res) || isinf(res)) { err = CALC_ERR_OVERFLOW; return 0.0; }
      return res;
    }
    return base;
  }

  double parsePrimary() {
    if (err != CALC_OK) return 0.0;
    skipSpaces();

    if (*p == '(' || strncmp(p, "sqrt(", 5) == 0 || strncmp(p, "cbrt(", 5) == 0) {
      if (depth >= 16) { err = CALC_ERR_OVERFLOW; return 0.0; }
      int kind = 0; // 0 = nawias, 1 = sqrt, 2 = cbrt
      if (strncmp(p, "sqrt(", 5) == 0) { kind = 1; p += 5; }
      else if (strncmp(p, "cbrt(", 5) == 0) { kind = 2; p += 5; }
      else { p++; }

      depth++;
      double inner = parseExpr();
      depth--;
      if (err != CALC_OK) return 0.0;
      skipSpaces();
      if (*p != ')') { err = CALC_ERR_PAREN; return 0.0; }
      p++;

      if (kind == 1) {
        if (inner < 0.0) { err = CALC_ERR_DOMAIN; return 0.0; }
        return sqrt(inner);
      }
      if (kind == 2) {
        return cbrt(inner);
      }
      return inner;
    }

    if (!isdigit((unsigned char)*p) && *p != '.') {
      err = CALC_ERR_SYNTAX;
      return 0.0;
    }

    bool anyDigit = false;
    double val = 0.0;
    while (isdigit((unsigned char)*p)) {
      anyDigit = true;
      val = val * 10.0 + (double)(*p - '0');
      p++;
    }
    if (*p == '.') {
      p++;
      double frac = 0.1;
      while (isdigit((unsigned char)*p)) {
        anyDigit = true;
        val += (double)(*p - '0') * frac;
        frac *= 0.1;
        p++;
      }
    }
    if (!anyDigit) { err = CALC_ERR_SYNTAX; return 0.0; }
    return val;
  }
};

static void formatDecimal(double w, char* out, size_t cap) {
  if (isnan(w) || isinf(w) || fabs(w) >= 100000000.0) {
    snprintf(out, cap, "%s", tr("Overflow", "Przepeln."));
    return;
  }
  double roundedInt = round(w);
  if (fabs(w - roundedInt) < 1e-9) {
    long long iv = (long long)roundedInt;
    snprintf(out, cap, "%lld", iv);
    return;
  }
  long long scaled = (long long)llround(w * 10000.0);
  const char* sign = (scaled < 0) ? "-" : "";
  long long absS = (scaled < 0) ? -scaled : scaled;
  long long whole = absS / 10000LL;
  long long frac  = absS % 10000LL;

  snprintf(out, cap, "%s%lld.%04lld", sign, whole, frac);
  size_t len = strlen(out);
  while (len > 0 && out[len - 1] == '0') out[--len] = '\0';
  if (len > 0 && out[len - 1] == '.') out[--len] = '\0';
}

static bool toFraction(double val, long long &numOut, long long &denOut) {
  if (isnan(val) || isinf(val) || fabs(val) > 100000.0) return false;
  bool neg = val < 0.0;
  double x = fabs(val);

  long long h1 = 1, h0 = 0, k1 = 0, k0 = 1;
  double b = x;
  for (int i = 0; i < 16; i++) {
    long long a = (long long)floor(b);
    long long h2 = a * h1 + h0;
    long long k2 = a * k1 + k0;
    if (k2 > 256) break;
    h0 = h1; h1 = h2;
    k0 = k1; k1 = k2;
    double rem = b - (double)a;
    if (rem < 1e-9) break;
    b = 1.0 / rem;
  }
  if (k1 <= 1 || k1 > 256) return false;
  if (fabs(x - (double)h1 / (double)k1) > 1e-7) return false;
  numOut = neg ? -h1 : h1;
  denOut = k1;
  return true;
}

static void refreshResultDisplay() {
  if (!hasResult || !lastEvalOk) return;
  char decBuf[24];
  formatDecimal(lastVal, decBuf, sizeof(decBuf));

  long long num = 0, den = 1;
  if (toFraction(lastVal, num, den)) {
    char frBuf[24];
    snprintf(frBuf, sizeof(frBuf), "%lld/%lld", num, den);
    if (fracMode) {
      snprintf(resPrimary, sizeof(resPrimary), "%s", frBuf);
      snprintf(resAlt, sizeof(resAlt), "dec: %s", decBuf);
    } else {
      snprintf(resPrimary, sizeof(resPrimary), "%s", decBuf);
      snprintf(resAlt, sizeof(resAlt), "%s %s", tr("frac:", "ulamek:"), frBuf);
    }
  } else {
    snprintf(resPrimary, sizeof(resPrimary), "%s", decBuf);
    resAlt[0] = '\0';
  }
}

static void evaluateExpr() {
  if (!expr[0]) return;
  CalcParser parser { expr, CALC_OK, 0 };
  double val = parser.parseExpr();
  parser.skipSpaces();
  if (parser.err == CALC_OK && *parser.p != '\0') {
    parser.err = CALC_ERR_SYNTAX;
  }

  hasResult = true;
  resAlt[0] = '\0';
  if (parser.err == CALC_OK && (isnan(val) || isinf(val) || fabs(val) >= 1e9)) {
    parser.err = CALC_ERR_OVERFLOW;
  }

  if (parser.err != CALC_OK) {
    lastEvalOk = false;
    switch (parser.err) {
      case CALC_ERR_DIV0:     snprintf(resPrimary, sizeof(resPrimary), "%s", tr("Err /0", "Blad /0")); break;
      case CALC_ERR_DOMAIN:   snprintf(resPrimary, sizeof(resPrimary), "%s", tr("Err sqrt", "Blad pierw.")); break;
      case CALC_ERR_PAREN:    snprintf(resPrimary, sizeof(resPrimary), "%s", tr("Err paren", "Blad nawias")); break;
      case CALC_ERR_OVERFLOW: snprintf(resPrimary, sizeof(resPrimary), "%s", tr("Overflow", "Przepeln.")); break;
      default:                snprintf(resPrimary, sizeof(resPrimary), "%s", tr("Syntax err", "Blad skladni")); break;
    }
    return;
  }

  lastEvalOk = true;
  lastVal = val;
  refreshResultDisplay();
}

static void appendToken(const char* tok) {
  size_t cur = strlen(expr);
  size_t add = strlen(tok);
  if (cur + add < sizeof(expr)) {
    memcpy(expr + cur, tok, add + 1);
  }
}

static bool calcKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_MENU); return true; }

  if (k == KEY_F2) {
    fracMode = !fracMode;
    refreshResultDisplay();
    showToast(fracMode ? tr("Mode: Fractions", "Tryb: Ulamki") : tr("Mode: Decimal", "Tryb: Dziesietne"), 900);
    return true;
  }

  if (k == KEY_DEL || k == 'c' || k == 'C') {
    expr[0] = resPrimary[0] = resAlt[0] = '\0';
    hasResult = lastEvalOk = false;
    return true;
  }

  if (k == '\b') {
    hasResult = false;
    resPrimary[0] = resAlt[0] = '\0';
    size_t len = strlen(expr);
    if (len >= 5 && (strcmp(expr + len - 5, "sqrt(") == 0 || strcmp(expr + len - 5, "cbrt(") == 0)) {
      expr[len - 5] = '\0';
    } else if (len > 0) {
      expr[len - 1] = '\0';
    }
    return true;
  }

  if (k == '\n' || k == '=') {
    evaluateExpr();
    return true;
  }

  if (k == ',') k = '.';

  bool isOp = (k == '+' || k == '-' || k == '*' || k == '/' || k == '^' || k == 'p' || k == 'P');
  if (hasResult) {
    if (isOp && lastEvalOk) {
      formatDecimal(lastVal, expr, sizeof(expr));
    } else {
      expr[0] = '\0';
    }
    hasResult = false;
    resPrimary[0] = resAlt[0] = '\0';
  }

  if (k == 'r' || k == 'R' || k == 's' || k == 'S') {
    appendToken("sqrt(");
  } else if (k == 'q' || k == 'Q') {
    appendToken("cbrt(");
  } else if (k == 'p' || k == 'P') {
    appendToken("^");
  } else if ((k >= '0' && k <= '9') || k == '.' || k == '+' || k == '-' ||
             k == '*' || k == '/' || k == '^' || k == '(' || k == ')') {
    char ch[2] = { (char)k, '\0' };
    appendToken(ch);
  }
  return true;
}

static void calcDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("CALCULATOR", "KALKULATOR"));
  d.drawStr(88, 18, fracMode ? tr("[FRAC]", "[ULAM]") : tr("[DEC]", "[DZIES]"));

  // Wyświetl maksymalnie 47 ostatnich znaków wyrażenia w 2 wierszach (24 znaki/wiersz)
  size_t elen = strlen(expr);
  char visBuf[50];
  const char* src = (elen > 46) ? (expr + elen - 46) : expr;
  snprintf(visBuf, sizeof(visBuf), "%s_", src);

  char line1[26], line2[26];
  size_t vlen = strlen(visBuf);
  size_t l1 = min(vlen, (size_t)24);
  memcpy(line1, visBuf, l1);
  line1[l1] = '\0';
  d.drawStr(2, 27, line1);

  if (vlen > 24) {
    size_t l2 = min(vlen - 24, (size_t)24);
    memcpy(line2, visBuf + 24, l2);
    line2[l2] = '\0';
    d.drawStr(2, 35, line2);
  } else if (hasResult && resAlt[0]) {
    d.drawStr(2, 35, resAlt);
  }

  if (hasResult && resPrimary[0]) {
    d.setFont(u8g2_font_9x15_tr);
    char out[24];
    snprintf(out, sizeof(out), "=%.12s", resPrimary);
    d.drawStr(2, 51, out);
  }

  d.setFont(u8g2_font_5x7_tr);
  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, "Ent:= R:sqrt ^:Pot F1:?");
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_CALC_EN[] = {
  "0-9 . ,: Digits & decimal",
  "+ - * / ( ): Operations",
  "^ or P: Power (e.g. 2^3)",
  "R or S: Sqrt sqrt(...)",
  "Q: Cube root cbrt(...)",
  "F2: Fraction / Decimal",
  "Ent or =: Evaluate",
  "Bksp: Del | C/Del: Clear",
  "Esc: Main menu"
};
static const char* const HELP_CALC_PL[] = {
  "0-9 . ,: Cyfry i ulamki",
  "+ - * / ( ): Dzialania",
  "^ lub P: Potega (np.2^3)",
  "R lub S: Pierw. sqrt(...)",
  "Q: Pierw. 3 st. cbrt(...)",
  "F2: Tryb Ulamek / Dzies.",
  "Ent lub =: Oblicz wynik",
  "Bksp: Cofnij | C/Del: Clr",
  "Esc: Menu glowne"
};

const AppScreen screenAppCalc = { nullptr, nullptr, calcKey, calcDraw, HELP_CALC_EN, HELP_CALC_PL, 9 };
