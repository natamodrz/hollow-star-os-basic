#include "app_basic.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include <ctype.h>

static const int BASIC_MAX_LINES = 40;

struct BasicForFrame {
  uint8_t varIdx;
  int32_t endVal;
  int32_t stepVal;
  int16_t retIdx; // indeks linii zaraz po FOR
};

static int lineCount = 0;
static int32_t vars[26];
static char cmdBuf[48] = "";
static char lastCmd[48] = "";

// Stan wykonywania programu
static bool basicRunning = false;
static bool waitingInput = false;
static uint8_t inputVarIdx = 0;
static int runIdx = 0;
static bool jumped = false;
static unsigned long delayUntilMs = 0;

static int gosubStack[8];
static int gosubSp = 0;
static BasicForFrame forStack[6];
static int forSp = 0;

static char curOutLine[26] = "";
static int curOutCol = 0;

static void flushOutLine() {
  for (int i = 0; i < 3; i++) {
    memcpy(appScratch.basic.out[i], appScratch.basic.out[i + 1], 26);
  }
  strncpy(appScratch.basic.out[3], curOutLine, 25);
  appScratch.basic.out[3][25] = '\0';
  curOutLine[0] = '\0';
  curOutCol = 0;
}

static void printStrRaw(const char* s, bool newline) {
  while (s && *s) {
    char c = *s++;
    if (c == '\n') {
      flushOutLine();
    } else {
      if (curOutCol >= 24) flushOutLine();
      curOutLine[curOutCol++] = c;
      curOutLine[curOutCol] = '\0';
    }
  }
  if (newline) flushOutLine();
}

static void clsConsole() {
  for (int i = 0; i < 4; i++) appScratch.basic.out[i][0] = '\0';
  curOutLine[0] = '\0';
  curOutCol = 0;
}

static void clearProgram() {
  lineCount = 0;
  memset(vars, 0, sizeof(vars));
  basicRunning = false;
  waitingInput = false;
  gosubSp = 0;
  forSp = 0;
}

static int findLineIndex(uint16_t num) {
  for (int i = 0; i < lineCount; i++) {
    if (appScratch.basic.lines[i].num == num) return i;
  }
  return -1;
}

static void setProgramLine(uint16_t num, const char* txt) {
  while (*txt == ' ') txt++;
  int idx = findLineIndex(num);
  if (*txt == '\0') {
    if (idx >= 0) {
      for (int i = idx; i < lineCount - 1; i++) {
        appScratch.basic.lines[i] = appScratch.basic.lines[i + 1];
      }
      lineCount--;
    }
    return;
  }

  if (idx >= 0) {
    strncpy(appScratch.basic.lines[idx].text, txt, sizeof(appScratch.basic.lines[0].text) - 1);
    appScratch.basic.lines[idx].text[sizeof(appScratch.basic.lines[0].text) - 1] = '\0';
    return;
  }

  if (lineCount >= BASIC_MAX_LINES) {
    printStrRaw("ERR: MEM FULL", true);
    return;
  }
  int ins = 0;
  while (ins < lineCount && appScratch.basic.lines[ins].num < num) ins++;
  for (int i = lineCount; i > ins; i--) {
    appScratch.basic.lines[i] = appScratch.basic.lines[i - 1];
  }
  appScratch.basic.lines[ins].num = num;
  strncpy(appScratch.basic.lines[ins].text, txt, sizeof(appScratch.basic.lines[0].text) - 1);
  appScratch.basic.lines[ins].text[sizeof(appScratch.basic.lines[0].text) - 1] = '\0';
  lineCount++;
}

// --- Parser wyrażeń całkowitoliczbowych ---
struct BasicExprParser {
  const char* p;
  bool err;

  void skipSp() { while (*p == ' ') p++; }

  bool matchWordCI(const char* w) {
    skipSp();
    size_t l = strlen(w);
    if (strncasecmp(p, w, l) == 0 && !isalnum((unsigned char)p[l])) {
      p += l;
      return true;
    }
    return false;
  }

  int32_t parseExpr() {
    if (err) return 0;
    int32_t v = parseTerm();
    while (!err) {
      skipSp();
      if (*p == '+') { p++; v += parseTerm(); }
      else if (*p == '-') { p++; v -= parseTerm(); }
      else break;
    }
    return v;
  }

  int32_t parseTerm() {
    if (err) return 0;
    int32_t v = parseFactor();
    while (!err) {
      skipSp();
      if (*p == '*') { p++; v *= parseFactor(); }
      else if (*p == '/') {
        p++;
        int32_t r = parseFactor();
        if (r == 0) { err = true; return 0; }
        v /= r;
      } else if (*p == '%') {
        p++;
        int32_t r = parseFactor();
        if (r == 0) { err = true; return 0; }
        v %= r;
      } else break;
    }
    return v;
  }

  int32_t parseFactor() {
    if (err) return 0;
    skipSp();
    if (*p == '+') { p++; return parseFactor(); }
    if (*p == '-') { p++; return -parseFactor(); }
    if (*p == '(') {
      p++;
      int32_t v = parseExpr();
      skipSp();
      if (*p == ')') p++;
      else err = true;
      return v;
    }
    if (strncasecmp(p, "RND(", 4) == 0) {
      p += 4;
      int32_t n = parseExpr();
      skipSp();
      if (*p == ')') p++; else err = true;
      return (n > 0) ? (int32_t)random(n) : 0;
    }
    if (strncasecmp(p, "ABS(", 4) == 0) {
      p += 4;
      int32_t n = parseExpr();
      skipSp();
      if (*p == ')') p++; else err = true;
      return (n < 0) ? -n : n;
    }
    if (isalpha((unsigned char)*p) && !isalnum((unsigned char)p[1])) {
      uint8_t idx = (uint8_t)(toupper((unsigned char)*p) - 'A');
      p++;
      return vars[idx];
    }
    if (isdigit((unsigned char)*p)) {
      int32_t v = 0;
      while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
      }
      return v;
    }
    err = true;
    return 0;
  }
};

static bool startsKeywordCI(const char* s, const char* kw, const char** restOut) {
  while (*s == ' ') s++;
  size_t l = strlen(kw);
  if (strncasecmp(s, kw, l) == 0 && (s[l] == '\0' || s[l] == ' ' || s[l] == '"' || s[l] == ':')) {
    *restOut = s + l;
    while (**restOut == ' ') (*restOut)++;
    return true;
  }
  return false;
}

static bool saveBasicFile(bool onSD, const char* nameRaw) {
  if (onSD && !sdMounted && !storageMountSD()) return false;
  char path[96];
  if (nameRaw[0] == '/') snprintf(path, sizeof(path), "%s", nameRaw);
  else                   snprintf(path, sizeof(path), "/%s", nameRaw);
  size_t l = strlen(path);
  if (l < 4 || strcasecmp(path + l - 4, ".bas") != 0) {
    strncat(path, ".bas", sizeof(path) - l - 1);
  }

  fsLock();
  File f = fsOpenWriteLocked(onSD, path);
  if (!f) { fsUnlock(); return false; }
  for (int i = 0; i < lineCount; i++) {
    f.printf("%u %s\n", (unsigned)appScratch.basic.lines[i].num, appScratch.basic.lines[i].text);
  }
  f.close();
  fsUnlock();
  return true;
}

static bool loadBasicFile(bool onSD, const char* nameRaw) {
  if (onSD && !sdMounted && !storageMountSD()) return false;
  char path[96];
  if (nameRaw[0] == '/') snprintf(path, sizeof(path), "%s", nameRaw);
  else                   snprintf(path, sizeof(path), "/%s", nameRaw);
  size_t l = strlen(path);
  if (l < 4 || strcasecmp(path + l - 4, ".bas") != 0) {
    strncat(path, ".bas", sizeof(path) - l - 1);
  }

  char buf[1024];
  if (!storageReadText(onSD, path, buf, sizeof(buf))) return false;
  clearProgram();
  char* p = buf;
  while (*p) {
    char* nl = strchr(p, '\n');
    if (nl) *nl = '\0';
    while (*p == ' ') p++;
    if (isdigit((unsigned char)*p)) {
      uint16_t num = (uint16_t)strtoul(p, &p, 10);
      setProgramLine(num, p);
    }
    if (!nl) break;
    p = nl + 1;
  }
  return true;
}

static void execStatement(const char* stmt) {
  while (*stmt == ' ') stmt++;
  if (*stmt == '\0' || *stmt == '\'') return;

  const char* arg = nullptr;
  if (startsKeywordCI(stmt, "REM", &arg)) return;

  if (startsKeywordCI(stmt, "CLS", &arg)) {
    clsConsole();
    return;
  }

  if (startsKeywordCI(stmt, "END", &arg) || startsKeywordCI(stmt, "STOP", &arg)) {
    basicRunning = false;
    return;
  }

  if (startsKeywordCI(stmt, "DELAY", &arg)) {
    BasicExprParser ep { arg, false };
    int32_t ms = ep.parseExpr();
    if (!ep.err && ms > 0) delayUntilMs = millis() + (unsigned long)min((int32_t)5000, ms);
    return;
  }

  if (startsKeywordCI(stmt, "PRINT", &arg) || stmt[0] == '?') {
    const char* p = (stmt[0] == '?') ? (stmt + 1) : arg;
    bool nl = true;
    while (*p) {
      while (*p == ' ') p++;
      if (!*p) break;
      if (*p == '"') {
        p++;
        const char* endq = strchr(p, '"');
        if (!endq) { printStrRaw(p, false); break; }
        char seg[46];
        size_t sl = min((size_t)(endq - p), sizeof(seg) - 1);
        memcpy(seg, p, sl); seg[sl] = '\0';
        printStrRaw(seg, false);
        p = endq + 1;
      } else {
        BasicExprParser ep { p, false };
        int32_t val = ep.parseExpr();
        if (ep.err) { printStrRaw("ERR: EXPR", true); basicRunning = false; return; }
        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%ld", (long)val);
        printStrRaw(numBuf, false);
        p = ep.p;
      }
      while (*p == ' ') p++;
      if (*p == ';') { nl = false; p++; }
      else if (*p == ',') { nl = false; printStrRaw(" ", false); p++; }
      else { nl = true; break; }
    }
    if (nl) printStrRaw("", true);
    return;
  }

  if (startsKeywordCI(stmt, "GOTO", &arg)) {
    uint16_t target = (uint16_t)strtoul(arg, nullptr, 10);
    int idx = findLineIndex(target);
    if (idx < 0) { printStrRaw("ERR: LINE", true); basicRunning = false; }
    else { runIdx = idx; jumped = true; }
    return;
  }

  if (startsKeywordCI(stmt, "GOSUB", &arg)) {
    if (gosubSp >= 8) { printStrRaw("ERR: STACK", true); basicRunning = false; return; }
    uint16_t target = (uint16_t)strtoul(arg, nullptr, 10);
    int idx = findLineIndex(target);
    if (idx < 0) { printStrRaw("ERR: LINE", true); basicRunning = false; return; }
    gosubStack[gosubSp++] = runIdx + 1;
    runIdx = idx;
    jumped = true;
    return;
  }

  if (startsKeywordCI(stmt, "RETURN", &arg)) {
    if (gosubSp <= 0) { printStrRaw("ERR: RET", true); basicRunning = false; return; }
    runIdx = gosubStack[--gosubSp];
    jumped = true;
    return;
  }

  if (startsKeywordCI(stmt, "INPUT", &arg)) {
    const char* p = arg;
    while (*p == ' ') p++;
    if (*p == '"') {
      p++;
      const char* eq = strchr(p, '"');
      if (eq) {
        char pr[24];
        size_t l = min((size_t)(eq - p), sizeof(pr) - 1);
        memcpy(pr, p, l); pr[l] = '\0';
        printStrRaw(pr, false);
        p = eq + 1;
        while (*p == ' ' || *p == ',' || *p == ';') p++;
      }
    } else {
      printStrRaw("? ", false);
    }
    if (isalpha((unsigned char)*p)) {
      inputVarIdx = (uint8_t)(toupper((unsigned char)*p) - 'A');
      waitingInput = true;
      cmdBuf[0] = '\0';
    } else {
      printStrRaw("ERR: INPUT", true);
      basicRunning = false;
    }
    return;
  }

  if (startsKeywordCI(stmt, "FOR", &arg)) {
    const char* p = arg;
    while (*p == ' ') p++;
    if (!isalpha((unsigned char)*p)) { printStrRaw("ERR: FOR", true); basicRunning = false; return; }
    uint8_t vIdx = (uint8_t)(toupper((unsigned char)*p) - 'A');
    p++;
    while (*p == ' ') p++;
    if (*p != '=') { printStrRaw("ERR: FOR =", true); basicRunning = false; return; }
    p++;
    BasicExprParser ep1 { p, false };
    int32_t startV = ep1.parseExpr();
    p = ep1.p;
    if (ep1.err || !startsKeywordCI(p, "TO", &p)) { printStrRaw("ERR: TO", true); basicRunning = false; return; }
    BasicExprParser ep2 { p, false };
    int32_t endV = ep2.parseExpr();
    p = ep2.p;
    int32_t stepV = 1;
    if (startsKeywordCI(p, "STEP", &p)) {
      BasicExprParser ep3 { p, false };
      stepV = ep3.parseExpr();
      if (ep3.err) { printStrRaw("ERR: STEP", true); basicRunning = false; return; }
    }
    vars[vIdx] = startV;
    if (forSp < 6) {
      forStack[forSp++] = { vIdx, endV, stepV, (int16_t)(runIdx + 1) };
    }
    return;
  }

  if (startsKeywordCI(stmt, "NEXT", &arg)) {
    if (forSp <= 0) { printStrRaw("ERR: NEXT", true); basicRunning = false; return; }
    BasicForFrame &fr = forStack[forSp - 1];
    vars[fr.varIdx] += fr.stepVal;
    bool cont = (fr.stepVal >= 0) ? (vars[fr.varIdx] <= fr.endVal) : (vars[fr.varIdx] >= fr.endVal);
    if (cont) {
      runIdx = fr.retIdx;
      jumped = true;
    } else {
      forSp--;
    }
    return;
  }

  if (startsKeywordCI(stmt, "IF", &arg)) {
    BasicExprParser ep1 { arg, false };
    int32_t lhs = ep1.parseExpr();
    const char* p = ep1.p;
    while (*p == ' ') p++;
    int op = 0; // 1:=, 2:<>, 3:<, 4:<=, 5:>, 6:>=
    if (p[0] == '<' && p[1] == '>') { op = 2; p += 2; }
    else if (p[0] == '!' && p[1] == '=') { op = 2; p += 2; }
    else if (p[0] == '<' && p[1] == '=') { op = 4; p += 2; }
    else if (p[0] == '>' && p[1] == '=') { op = 6; p += 2; }
    else if (p[0] == '=' && p[1] == '=') { op = 1; p += 2; }
    else if (p[0] == '=') { op = 1; p++; }
    else if (p[0] == '<') { op = 3; p++; }
    else if (p[0] == '>') { op = 5; p++; }
    BasicExprParser ep2 { p, false };
    int32_t rhs = ep2.parseExpr();
    p = ep2.p;
    if (ep1.err || ep2.err || op == 0 || !startsKeywordCI(p, "THEN", &p)) {
      printStrRaw("ERR: IF", true);
      basicRunning = false;
      return;
    }
    bool cond = false;
    switch (op) {
      case 1: cond = (lhs == rhs); break;
      case 2: cond = (lhs != rhs); break;
      case 3: cond = (lhs < rhs);  break;
      case 4: cond = (lhs <= rhs); break;
      case 5: cond = (lhs > rhs);  break;
      case 6: cond = (lhs >= rhs); break;
    }
    if (cond) {
      while (*p == ' ') p++;
      if (isdigit((unsigned char)*p)) {
        uint16_t target = (uint16_t)strtoul(p, nullptr, 10);
        int idx = findLineIndex(target);
        if (idx < 0) { printStrRaw("ERR: LINE", true); basicRunning = false; }
        else { runIdx = idx; jumped = true; }
      } else {
        execStatement(p);
      }
    }
    return;
  }

  // LET X = expr lub X = expr
  const char* assignPtr = stmt;
  if (startsKeywordCI(stmt, "LET", &arg)) assignPtr = arg;
  while (*assignPtr == ' ') assignPtr++;
  if (isalpha((unsigned char)assignPtr[0])) {
    uint8_t vIdx = (uint8_t)(toupper((unsigned char)assignPtr[0]) - 'A');
    const char* p = assignPtr + 1;
    while (*p == ' ') p++;
    if (*p == '=') {
      p++;
      BasicExprParser ep { p, false };
      int32_t v = ep.parseExpr();
      if (ep.err) { printStrRaw("ERR: EXPR", true); basicRunning = false; return; }
      vars[vIdx] = v;
      return;
    }
  }

  printStrRaw("ERR: SYNTAX", true);
  basicRunning = false;
}

static void handleImmediateLine(const char* raw) {
  while (*raw == ' ') raw++;
  if (!*raw) return;

  if (isdigit((unsigned char)*raw)) {
    char* rest = nullptr;
    uint16_t num = (uint16_t)strtoul(raw, &rest, 10);
    if (num > 0) setProgramLine(num, rest ? rest : "");
    return;
  }

  const char* arg = nullptr;
  if (startsKeywordCI(raw, "NEW", &arg)) {
    clearProgram();
    printStrRaw("READY", true);
  } else if (startsKeywordCI(raw, "LIST", &arg)) {
    for (int i = 0; i < lineCount; i++) {
      char line[56];
      snprintf(line, sizeof(line), "%u %s", (unsigned)appScratch.basic.lines[i].num, appScratch.basic.lines[i].text);
      printStrRaw(line, true);
    }
  } else if (startsKeywordCI(raw, "RUN", &arg)) {
    if (lineCount == 0) { printStrRaw("NO PROG", true); return; }
    memset(vars, 0, sizeof(vars));
    gosubSp = 0;
    forSp = 0;
    runIdx = 0;
    delayUntilMs = 0;
    basicRunning = true;
    waitingInput = false;
  } else if (startsKeywordCI(raw, "SAVE", &arg)) {
    if (!*arg) { printStrRaw("SAVE <NAME>", true); return; }
    bool ok = saveBasicFile(sdMounted, arg);
    printStrRaw(ok ? "SAVED OK" : "SAVE ERR", true);
  } else if (startsKeywordCI(raw, "LOAD", &arg)) {
    if (!*arg) { printStrRaw("LOAD <NAME>", true); return; }
    bool ok = loadBasicFile(sdMounted, arg);
    if (!ok && sdMounted) ok = loadBasicFile(false, arg);
    printStrRaw(ok ? "LOADED OK" : "LOAD ERR", true);
  } else if (startsKeywordCI(raw, "FILES", &arg)) {
    DirEntryInfo items[12];
    bool onSD = sdMounted;
    int n = storageListDir(onSD, "/", items, 12);
    for (int i = 0; i < n; i++) {
      size_t l = strlen(items[i].name);
      if (l >= 4 && strcasecmp(items[i].name + l - 4, ".bas") == 0) {
        printStrRaw(items[i].name, true);
      }
    }
  } else if (startsKeywordCI(raw, "HELP", &arg)) {
    printStrRaw("LIST RUN NEW CLR", true);
    printStrRaw("SAVE LOAD FILES", true);
  } else {
    execStatement(raw);
  }
}

static bool basicInited = false;

static void basicEnter() {
  if (!basicInited) {
    basicInited = true;
    clearProgram();
    clsConsole();
    printStrRaw("HOLLOW TINYBASIC v1.0", true);
    printStrRaw("10 PRINT \"HI!\" -> RUN", true);
  }
}

void appBasicLoadAndRun(bool onSD, const char* fullPath) {
  basicInited = true;
  clearProgram();
  clsConsole();
  if (loadBasicFile(onSD, fullPath)) {
    printStrRaw("LOADED -> RUN", true);
    runIdx = 0;
    basicRunning = (lineCount > 0);
  } else {
    printStrRaw("LOAD ERR", true);
  }
  uiSetScreen(APP_BASIC);
}

static bool basicTick(unsigned long now) {
  if (!basicRunning || waitingInput) return false;
  if (delayUntilMs > 0 && now < delayUntilMs) return false;
  delayUntilMs = 0;

  for (int step = 0; step < 32 && basicRunning && !waitingInput && delayUntilMs == 0; step++) {
    if (runIdx < 0 || runIdx >= lineCount) {
      basicRunning = false;
      printStrRaw("OK", true);
      break;
    }
    jumped = false;
    execStatement(appScratch.basic.lines[runIdx].text);
    if (!jumped && basicRunning && !waitingInput) {
      runIdx++;
    }
  }
  return true;
}

static bool basicKey(uint8_t k) {
  if (k == KEY_ESC) {
    if (basicRunning) {
      basicRunning = false;
      waitingInput = false;
      printStrRaw("BREAK", true);
    } else {
      uiSetScreen(APP_MENU);
    }
    return true;
  }

  if (k == KEY_UP) {
    if (lastCmd[0]) {
      strncpy(cmdBuf, lastCmd, sizeof(cmdBuf) - 1);
      cmdBuf[sizeof(cmdBuf) - 1] = '\0';
    }
    return true;
  }

  size_t len = strlen(cmdBuf);
  if (k == '\b') {
    if (len > 0) cmdBuf[len - 1] = '\0';
  } else if (k == KEY_DEL) {
    cmdBuf[0] = '\0';
  } else if (k == '\n') {
    if (waitingInput) {
      BasicExprParser ep { cmdBuf, false };
      vars[inputVarIdx] = ep.parseExpr();
      printStrRaw(cmdBuf, true);
      cmdBuf[0] = '\0';
      waitingInput = false;
      runIdx++;
    } else {
      char lineEcho[48];
      snprintf(lineEcho, sizeof(lineEcho), ">%s", cmdBuf);
      printStrRaw(lineEcho, true);
      strncpy(lastCmd, cmdBuf, sizeof(lastCmd) - 1);
      lastCmd[sizeof(lastCmd) - 1] = '\0';
      char execCopy[48];
      strncpy(execCopy, cmdBuf, sizeof(execCopy));
      cmdBuf[0] = '\0';
      handleImmediateLine(execCopy);
    }
  } else if (k >= 32 && k <= 126 && len < sizeof(cmdBuf) - 2) {
    cmdBuf[len] = (char)k;
    cmdBuf[len + 1] = '\0';
  }
  return true;
}

static void basicDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  d.drawStr(2, 17, basicRunning
                   ? tr("TINYBASIC [RUN Esc:Brk]", "TINYBASIC [GRA Esc:Stop]")
                   : tr("TINYBASIC (F1:Help)",    "TINYBASIC (F1:Pomoc)"));
  d.drawHLine(0, 19, 128);

  for (int i = 0; i < 4; i++) {
    d.drawStr(2, 27 + i * 7, appScratch.basic.out[i]);
  }
  // Pokazuj częściowy wiersz (np. znak zachęty INPUT) lub bieżący prompt
  if (curOutCol > 0) {
    d.drawStr(2, 50, curOutLine);
  }

  d.drawHLine(0, 52, 128);
  char prompt[28];
  size_t clen = strlen(cmdBuf);
  const char* vis = (clen > 21) ? (cmdBuf + clen - 21) : cmdBuf;
  snprintf(prompt, sizeof(prompt), ">%s_", vis);
  d.drawStr(2, 61, prompt);

  uiFinishFrame();
}

static const char* const HELP_BASIC_EN[] = {
  "10 PRINT \"HI\" -> RUN",
  "Commands: LIST RUN NEW CLR",
  "Files: SAVE/LOAD/FILES",
  "Stmt: LET INPUT IF THEN",
  "Flow: GOTO GOSUB FOR NEXT",
  "Math: + - * / % RND ABS",
  "Up: Last cmd | Esc: Break"
};
static const char* const HELP_BASIC_PL[] = {
  "10 PRINT \"HI\" -> RUN",
  "Komendy: LIST RUN NEW CLR",
  "Pliki: SAVE/LOAD/FILES",
  "Instr: LET INPUT IF THEN",
  "Skoki: GOTO GOSUB FOR NEXT",
  "Matma: + - * / % RND ABS",
  "^: Ost. kom. | Esc: Stop"
};

const AppScreen screenAppBasic = { basicEnter, basicTick, basicKey, basicDraw, HELP_BASIC_EN, HELP_BASIC_PL, 7 };
