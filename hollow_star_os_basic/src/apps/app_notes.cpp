#include "app_notes.h"
#include "app_files.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"

enum NoteMode : uint8_t {
  NOTE_ASK,
  NOTE_LOAD,
  NOTE_NEW_SETUP,
  NOTE_EDIT,
  NOTE_PREVIEW,
  NOTE_TUTORIAL
};

static NoteMode noteMode = NOTE_EDIT;
static NoteMode tutReturnMode = NOTE_EDIT;
static bool noteOpenedViaSetup = false;
static bool noteFromLoadBrowser = false;
static char notePath[128] = "/start.md";
static bool noteOnSD = false;
static int  noteScroll = 0;
static int  mdLineCount = 0;
static AppState noteReturnApp = APP_MENU;

// Wybór początkowy: Nowy vs Załaduj
static int askSel = 0;

// Przeglądarka plików dla opcji Załaduj
static bool loadOnSD = true;
static char loadPath[96] = "/";
static int  loadSel = 0;
static int  loadCount = 0;

// Kreator Nowy plik (domyślnie SD /docs/)
static int  setupSel = 2; // 0=Dysk, 1=Folder, 2=Nazwa, 3=Start
static bool setupOnSD = true;
static char setupDir[8][24];
static int  setupDirCount = 1;
static int  setupDirIdx = 0;
static char setupFileName[32] = "";

// Kursor edytora + przewijanie
static size_t noteCursor = 0;
static int    editTopRow = 0;
static bool   cursorVisible = true;
static unsigned long lastBlinkMs = 0;

// Tutorial Markdown pod F2
static int tutScroll = 0;

static bool hasMdOrTxtExt(const char* s) {
  size_t len = strlen(s);
  if (len >= 3 && strcasecmp(s + len - 3, ".md") == 0) return true;
  if (len >= 4 && strcasecmp(s + len - 4, ".txt") == 0) return true;
  return false;
}

static bool isValidFilenameChar(uint8_t k) {
  return (k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') ||
         (k >= '0' && k <= '9') || k == '_' || k == '-' || k == '.';
}

static void resetEditorCursor(size_t pos) {
  size_t len = strlen(appScratch.notes.buf);
  noteCursor = min(pos, len);
  editTopRow = 0;
  cursorVisible = true;
  lastBlinkMs = millis();
}

static void refreshLoadBrowser() {
  if (loadOnSD && !sdMounted) storageMountSD();
  loadCount = storageListDir(loadOnSD, loadPath, appScratch.files.items, MAX_DIR_ENTRIES);
  if (loadSel >= loadCount) loadSel = max(0, loadCount - 1);
}

static void loadSetupDirs() {
  setupDirCount = 0;
  if (setupOnSD) {
    if (!sdMounted) storageMountSD();
    strncpy(setupDir[setupDirCount++], "/docs", sizeof(setupDir[0]) - 1);
    strncpy(setupDir[setupDirCount++], "/", sizeof(setupDir[0]) - 1);
  } else {
    strncpy(setupDir[setupDirCount++], "/", sizeof(setupDir[0]) - 1);
    strncpy(setupDir[setupDirCount++], "/docs", sizeof(setupDir[0]) - 1);
  }
  for (int i = 0; i < setupDirCount; i++) setupDir[i][sizeof(setupDir[0]) - 1] = '\0';

  DirEntryInfo *entries = appScratch.files.items;
  int n = storageListDir(setupOnSD, "/", entries, MAX_DIR_ENTRIES);
  for (int i = 0; i < n && setupDirCount < 8; i++) {
    if (entries[i].isDir && strcasecmp(entries[i].name, "docs") != 0) {
      snprintf(setupDir[setupDirCount], sizeof(setupDir[0]), "/%.20s", entries[i].name);
      setupDirCount++;
    }
  }
  setupDirIdx = 0;
}

static void buildSetupTargetPath(char *out, size_t cap) {
  char fn[40];
  if (hasMdOrTxtExt(setupFileName)) {
    snprintf(fn, sizeof(fn), "%s", setupFileName);
  } else {
    snprintf(fn, sizeof(fn), "%s.md", setupFileName);
  }
  const char* folder = setupDir[setupDirIdx];
  if (strcmp(folder, "/") == 0) snprintf(out, cap, "/%s", fn);
  else                          snprintf(out, cap, "%s/%s", folder, fn);
}

static void rebuildMarkdownLines() {
  mdLineCount = 0;
  const char* buf = appScratch.notes.buf;
  size_t len = strlen(buf);
  size_t pos = 0;

  while (pos <= len && mdLineCount < 48) {
    const char* nl = strchr(buf + pos, '\n');
    size_t lineLen = nl ? (size_t)(nl - (buf + pos)) : (len - pos);
    char raw[128];
    size_t copyLen = min(lineLen, sizeof(raw) - 1);
    memcpy(raw, buf + pos, copyLen);
    raw[copyLen] = '\0';
    pos = nl ? (size_t)(nl - buf + 1) : (len + 1);

    MdStyle st = MD_NORMAL;
    int maxChars = 24;
    const char* txt = raw;

    if (strcmp(raw, "---") == 0 || strcmp(raw, "***") == 0) {
      appScratch.notes.lines[mdLineCount].text[0] = '\0';
      appScratch.notes.lines[mdLineCount].style = MD_HR;
      mdLineCount++;
      continue;
    } else if (strncmp(raw, "# ", 2) == 0) {
      st = MD_H1; txt = raw + 2; maxChars = 20;
    } else if (strncmp(raw, "## ", 3) == 0) {
      st = MD_H2; txt = raw + 3; maxChars = 22;
    } else if (strncmp(raw, "- ", 2) == 0 || strncmp(raw, "* ", 2) == 0) {
      st = MD_BULLET; txt = raw + 2; maxChars = 22;
    } else if (strncmp(raw, "> ", 2) == 0) {
      st = MD_QUOTE; txt = raw + 2; maxChars = 22;
    }

    size_t tlen = strlen(txt);
    if (tlen == 0) {
      appScratch.notes.lines[mdLineCount].text[0] = '\0';
      appScratch.notes.lines[mdLineCount].style = MD_NORMAL;
      mdLineCount++;
      continue;
    }

    size_t off = 0;
    bool firstChunk = true;
    while (off < tlen && mdLineCount < 48) {
      size_t chunk = min(tlen - off, (size_t)maxChars);
      memcpy(appScratch.notes.lines[mdLineCount].text, txt + off, chunk);
      appScratch.notes.lines[mdLineCount].text[chunk] = '\0';
      appScratch.notes.lines[mdLineCount].style = firstChunk ? st : (st == MD_BULLET ? MD_NORMAL : st);
      mdLineCount++;
      off += chunk;
      firstChunk = false;
    }
  }
  if (noteScroll > max(0, mdLineCount - 4)) noteScroll = max(0, mdLineCount - 4);
}

// Mapa zawiniętych wierszy do nawigacji i migającego kursora
struct EditRow {
  uint16_t start;
  uint16_t len;
};

static int computeEditRows(const char* buf, EditRow* rows, int maxRows) {
  int count = 0;
  size_t len = strlen(buf);
  size_t i = 0;
  uint16_t rowStart = 0;
  uint16_t col = 0;

  while (i < len && count < maxRows) {
    char c = buf[i];
    if (c == '\n') {
      rows[count++] = { rowStart, col };
      i++;
      rowStart = (uint16_t)i;
      col = 0;
    } else {
      col++;
      i++;
      if (col >= 24) {
        rows[count++] = { rowStart, col };
        rowStart = (uint16_t)i;
        col = 0;
      }
    }
  }
  if (count < maxRows) {
    rows[count++] = { rowStart, col };
  }
  return count;
}

static void findCursorRowCol(const EditRow* rows, int rowCount, size_t cursor, int &outRow, int &outCol) {
  outRow = 0;
  outCol = 0;
  for (int r = 0; r < rowCount; r++) {
    uint16_t s = rows[r].start;
    uint16_t e = s + rows[r].len;
    if (cursor >= s && cursor <= e) {
      // Jeśli kursor na zawiniętym końcu 24 zn., przejdź do początku kolejnego wiersza (jeśli istnieje)
      if (cursor == e && rows[r].len == 24 && (r + 1) < rowCount && rows[r + 1].start == e) {
        outRow = r + 1;
        outCol = 0;
      } else {
        outRow = r;
        outCol = (int)(cursor - s);
      }
      return;
    }
  }
  if (rowCount > 0) {
    outRow = rowCount - 1;
    outCol = (int)rows[outRow].len;
  }
}

void appNotesOpenSetup(AppState returnApp) {
  noteOpenedViaSetup = true;
  noteFromLoadBrowser = false;
  noteReturnApp = returnApp;
  askSel = 0;
  noteMode = NOTE_ASK;
  uiSetScreen(APP_NOTES);
}

void appNotesOpenFile(bool onSD, const char* path, AppState returnApp, bool startInPreview) {
  noteOpenedViaSetup = false;
  noteFromLoadBrowser = false;
  noteOnSD = onSD;
  strncpy(notePath, path ? path : "/start.md", sizeof(notePath) - 1);
  notePath[sizeof(notePath) - 1] = '\0';
  noteReturnApp = returnApp;
  appScratch.notes.buf[0] = '\0';
  storageReadText(onSD, notePath, appScratch.notes.buf, sizeof(appScratch.notes.buf));
  noteMode = (startInPreview && appScratch.notes.buf[0] != '\0') ? NOTE_PREVIEW : NOTE_EDIT;
  noteScroll = 0;
  resetEditorCursor(0);
  if (noteMode == NOTE_PREVIEW) rebuildMarkdownLines();
  uiSetScreen(APP_NOTES);
}

static void openTutorial() {
  tutReturnMode = noteMode;
  tutScroll = 0;
  noteMode = NOTE_TUTORIAL;
}

static void launchEmptyNoteFromSetup() {
  if (setupFileName[0] == '\0') {
    setupSel = 2;
    showToast(tr("Enter filename!", "Podaj nazwe pliku!"));
    return;
  }
  if (setupOnSD && !sdMounted && !storageMountSD()) {
    showToast(tr("No SD card!", "Brak karty SD!"));
    return;
  }
  buildSetupTargetPath(notePath, sizeof(notePath));
  noteOnSD = setupOnSD;
  noteFromLoadBrowser = false;
  appScratch.notes.buf[0] = '\0';
  noteScroll = 0;
  resetEditorCursor(0);
  noteMode = NOTE_EDIT;
}

static void drawMdSpan(int x, int y, const char* txt) {
  U8G2 &d = uiDisplay();
  bool bold = false;
  int cx = x;
  for (int i = 0; txt[i] && cx < 124; i++) {
    if (txt[i] == '*' && txt[i + 1] == '*') { bold = !bold; i++; continue; }
    char ch[2] = { txt[i], '\0' };
    d.drawStr(cx, y, ch);
    if (bold) d.drawStr(cx + 1, y, ch);
    cx += 5;
  }
}

static bool notesTick(unsigned long now) {
  if (noteMode == NOTE_EDIT && (now - lastBlinkMs >= 450UL)) {
    lastBlinkMs = now;
    cursorVisible = !cursorVisible;
    return true;
  }
  return false;
}

static bool askKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(noteReturnApp); return true; }
  if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) {
    askSel = 1 - askSel;
    return true;
  }
  if (k == '\n' || k == ' ') {
    if (askSel == 0) {
      setupOnSD = true;
      setupFileName[0] = '\0';
      setupSel = 2;
      loadSetupDirs();
      noteMode = NOTE_NEW_SETUP;
    } else {
      loadOnSD = sdMounted;
      strcpy(loadPath, "/");
      loadSel = 0;
      refreshLoadBrowser();
      noteMode = NOTE_LOAD;
    }
    return true;
  }
  return true;
}

static bool loadKey(uint8_t k) {
  if (k == KEY_ESC) {
    noteMode = NOTE_ASK;
    return true;
  }
  if (k == KEY_TAB) {
    loadOnSD = !loadOnSD;
    strcpy(loadPath, "/");
    loadSel = 0;
    refreshLoadBrowser();
    return true;
  }
  if (k == 'r' || k == 'R') {
    if (loadOnSD) {
      bool ok = storageMountSD();
      showToast(ok ? tr("SD mounted!", "SD odswiezona!") : tr("No SD card!", "Brak karty SD!"));
    }
    refreshLoadBrowser();
    return true;
  }
  if (k == '\b') {
    if (strcmp(loadPath, "/") != 0) {
      char* slash = strrchr(loadPath, '/');
      if (slash && slash != loadPath) *slash = '\0';
      else                            strcpy(loadPath, "/");
      loadSel = 0;
      refreshLoadBrowser();
    }
    return true;
  }
  if (loadCount > 0) {
    if (k == KEY_UP)        loadSel = (loadSel + loadCount - 1) % loadCount;
    else if (k == KEY_DOWN) loadSel = (loadSel + 1) % loadCount;
    else if (k == '\n') {
      const DirEntryInfo &item = appScratch.files.items[loadSel];
      char full[128];
      if (strcmp(loadPath, "/") == 0) snprintf(full, sizeof(full), "/%s", item.name);
      else                            snprintf(full, sizeof(full), "%s/%s", loadPath, item.name);

      if (item.isDir) {
        strncpy(loadPath, full, sizeof(loadPath) - 1);
        loadPath[sizeof(loadPath) - 1] = '\0';
        loadSel = 0;
        refreshLoadBrowser();
      } else {
        noteOnSD = loadOnSD;
        noteFromLoadBrowser = true;
        strncpy(notePath, full, sizeof(notePath) - 1);
        notePath[sizeof(notePath) - 1] = '\0';
        appScratch.notes.buf[0] = '\0';
        storageReadText(noteOnSD, notePath, appScratch.notes.buf, sizeof(appScratch.notes.buf));
        noteScroll = 0;
        resetEditorCursor(0);
        noteMode = NOTE_EDIT;
      }
    }
  }
  return true;
}

static bool setupKey(uint8_t k) {
  if (k == KEY_ESC) {
    noteMode = NOTE_ASK;
    return true;
  }
  if (k == KEY_UP) {
    setupSel = (setupSel + 3) % 4;
    return true;
  }
  if (k == KEY_DOWN || k == KEY_TAB) {
    setupSel = (setupSel + 1) % 4;
    return true;
  }
  if (k == KEY_F3) {
    setupOnSD = true;
    bool ok = storageMountSD();
    loadSetupDirs();
    showToast(ok ? tr("SD mounted!", "SD odswiezona!") : tr("No SD card!", "Brak karty SD!"));
    return true;
  }

  if (setupSel == 0) {
    if (k == KEY_LEFT || k == KEY_RIGHT || k == '\n' || k == ' ') {
      setupOnSD = !setupOnSD;
      loadSetupDirs();
    }
    return true;
  }
  if (setupSel == 1) {
    if (k == KEY_LEFT)       setupDirIdx = (setupDirIdx + setupDirCount - 1) % setupDirCount;
    else if (k == KEY_RIGHT) setupDirIdx = (setupDirIdx + 1) % setupDirCount;
    else if (k == '\n')      setupSel = 2;
    return true;
  }
  if (setupSel == 2) {
    size_t len = strlen(setupFileName);
    if (k == '\b') {
      if (len > 0) setupFileName[len - 1] = '\0';
    } else if (k == KEY_DEL) {
      setupFileName[0] = '\0';
    } else if (k == '\n') {
      launchEmptyNoteFromSetup();
    } else if (isValidFilenameChar(k) && len < 26) {
      setupFileName[len] = (char)k;
      setupFileName[len + 1] = '\0';
    }
    return true;
  }

  if (k == '\n' || k == ' ') {
    launchEmptyNoteFromSetup();
  }
  return true;
}

static bool notesKey(uint8_t k) {
  if (noteMode == NOTE_TUTORIAL) {
    static const int TUT_MAX_SCROLL = 6;
    if (k == KEY_ESC || k == KEY_F2 || k == '\n' || k == ' ') {
      noteMode = tutReturnMode;
    } else if (k == KEY_UP && tutScroll > 0) {
      tutScroll--;
    } else if (k == KEY_DOWN && tutScroll < TUT_MAX_SCROLL) {
      tutScroll++;
    }
    return true;
  }

  if (k == KEY_F2) {
    openTutorial();
    return true;
  }

  if (noteMode == NOTE_ASK)       return askKey(k);
  if (noteMode == NOTE_LOAD)      return loadKey(k);
  if (noteMode == NOTE_NEW_SETUP) return setupKey(k);

  if (k == KEY_ESC) {
    if (noteFromLoadBrowser) {
      refreshLoadBrowser();
      noteMode = NOTE_LOAD;
    } else if (noteOpenedViaSetup) {
      loadSetupDirs();
      noteMode = NOTE_NEW_SETUP;
    } else {
      uiSetScreen(noteReturnApp);
      if (noteReturnApp == APP_FILES) appFilesRefresh();
    }
    return true;
  }

  if (k == KEY_F3 || k == KEY_CTRL_S) {
    if (noteOnSD && !sdMounted) storageMountSD();
    // Automatycznie utwórz /docs jeśli plik jest zapisywany w /docs/...
    if (strncmp(notePath, "/docs/", 6) == 0 && !storageExists(noteOnSD, "/docs")) {
      fsLock();
      fsMkdirLocked(noteOnSD, "/docs");
      fsUnlock();
    }
    bool ok = storageWriteText(noteOnSD, notePath, appScratch.notes.buf);
    showToast(ok ? tr("File saved!", "Zapisano plik!") : tr("Save error!", "Blad zapisu!"));
    return true;
  }

  if (k == KEY_TAB) {
    noteMode = (noteMode == NOTE_PREVIEW) ? NOTE_EDIT : NOTE_PREVIEW;
    if (noteMode == NOTE_PREVIEW) {
      noteScroll = 0;
      rebuildMarkdownLines();
    } else {
      cursorVisible = true;
      lastBlinkMs = millis();
    }
    return true;
  }

  if (noteMode == NOTE_PREVIEW) {
    if (k == KEY_UP && noteScroll > 0) noteScroll--;
    else if (k == KEY_DOWN && noteScroll < max(0, mdLineCount - 4)) noteScroll++;
    return true;
  }

  // Tryb edycji NOTE_EDIT z pełną nawigacją i kursorem
  char* buf = appScratch.notes.buf;
  size_t len = strlen(buf);
  if (noteCursor > len) noteCursor = len;

  cursorVisible = true;
  lastBlinkMs = millis();

  if (k == KEY_LEFT) {
    if (noteCursor > 0) noteCursor--;
  } else if (k == KEY_RIGHT) {
    if (noteCursor < len) noteCursor++;
  } else if (k == KEY_UP || k == KEY_DOWN) {
    EditRow rows[96];
    int rowCount = computeEditRows(buf, rows, 96);
    int curR = 0, curC = 0;
    findCursorRowCol(rows, rowCount, noteCursor, curR, curC);
    if (k == KEY_UP && curR > 0) {
      int targetR = curR - 1;
      noteCursor = rows[targetR].start + min((uint16_t)curC, rows[targetR].len);
    } else if (k == KEY_DOWN && (curR + 1) < rowCount) {
      int targetR = curR + 1;
      noteCursor = rows[targetR].start + min((uint16_t)curC, rows[targetR].len);
    }
  } else if (k == '\b') {
    if (noteCursor > 0) {
      memmove(buf + noteCursor - 1, buf + noteCursor, len - noteCursor + 1);
      noteCursor--;
    }
  } else if (k == KEY_DEL) {
    if (noteCursor < len) {
      memmove(buf + noteCursor, buf + noteCursor + 1, len - noteCursor);
    }
  } else if (k == '\n' || (k >= 32 && k <= 126)) {
    if (len < 1000) {
      memmove(buf + noteCursor + 1, buf + noteCursor, len - noteCursor + 1);
      buf[noteCursor] = (char)k;
      noteCursor++;
    }
  }
  return true;
}

static void drawAsk() {
  U8G2 &d = uiDisplay();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("NOTES (.MD) - START", "NOTATNIK (.MD) - START"));
  d.drawHLine(0, 20, 128);

  const char* opts[2] = {
    tr("[ New Note (.md) ]", "[ Nowy plik (.md) ]"),
    tr("[ Load Existing ]",  "[ Zaladuj istniejacy ]")
  };
  for (int i = 0; i < 2; i++) {
    int y = 24 + i * 12;
    if (i == askSel) {
      d.drawBox(0, y, 128, 10);
      d.setDrawColor(0); d.drawStr(6, y + 8, opts[i]); d.setDrawColor(1);
    } else {
      d.drawStr(6, y + 8, opts[i]);
    }
  }
  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Ent:Open  F2:MD Tut  F1:?", "Ent:Otw  F2:Tut MD  F1:?"));
}

static void drawLoadBrowser() {
  U8G2 &d = uiDisplay();
  d.setFont(u8g2_font_5x7_tr);
  if (loadOnSD) rysujIkoneKartaSD(2, 11);
  else          rysujIkoneCzip(2, 11);

  char hdr[26];
  snprintf(hdr, sizeof(hdr), "%s:%.19s", loadOnSD ? "SD" : "FL", loadPath);
  d.drawStr(11, 18, hdr);
  d.drawHLine(0, 20, 128);

  if (loadCount == 0) {
    d.drawStr(4, 34, (loadOnSD && !sdMounted)
                     ? tr("No SD card!", "Brak karty SD!")
                     : tr("(Empty folder)", "(Pusty katalog)"));
  } else {
    int startIdx = max(0, min(loadSel - 1, loadCount - 3));
    for (int i = 0; i < 3 && (startIdx + i) < loadCount; i++) {
      int idx = startIdx + i;
      int y = 22 + i * 10;
      const DirEntryInfo &item = appScratch.files.items[idx];
      if (idx == loadSel) {
        d.drawBox(0, y, 128, 9);
        d.setDrawColor(0);
      }
      if (item.isDir) rysujIkoneFolder(2, y + 1);
      else            rysujIkonePlik(2, y + 1);

      char line[24];
      snprintf(line, sizeof(line), "%.20s%s", item.name, item.isDir ? "/" : "");
      d.drawStr(11, y + 7, line);
      d.setDrawColor(1);
    }
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Ent:Open Tab:Drv F2:Tut", "Ent:Otw Tab:Dysk F2:Tut"));
}

static void drawNewSetup() {
  U8G2 &d = uiDisplay();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 17, tr("NEW .MD FILE (SETUP)", "NOWY PLIK .MD (WYBOR)"));
  d.drawHLine(0, 19, 128);

  char line[28];

  // Wiersz 0: Dysk + ikona
  if (setupOnSD) snprintf(line, sizeof(line), "%s <SD %s>", tr("Drv :", "Dysk:"), sdMounted ? "OK" : tr("NO", "BRAK"));
  else           snprintf(line, sizeof(line), "%s <Flash LittleFS>", tr("Drv :", "Dysk:"));
  if (setupSel == 0) { d.drawBox(0, 21, 128, 8); d.setDrawColor(0); }
  if (setupOnSD) rysujIkoneKartaSD(2, 21);
  else           rysujIkoneCzip(2, 21);
  d.drawStr(11, 28, line);
  d.setDrawColor(1);

  // Wiersz 1: Folder
  snprintf(line, sizeof(line), "Folder: <%.15s>", setupDir[setupDirIdx]);
  if (setupSel == 1) {
    d.drawBox(0, 29, 128, 8);
    d.setDrawColor(0); d.drawStr(2, 36, line); d.setDrawColor(1);
  } else {
    d.drawStr(2, 36, line);
  }

  // Wiersz 2: Nazwa
  if (setupFileName[0]) {
    snprintf(line, sizeof(line), "%s %.13s%s_", tr("Name  :", "Nazwa :"),
             setupFileName, hasMdOrTxtExt(setupFileName) ? "" : ".md");
  } else {
    snprintf(line, sizeof(line), "%s _ %s", tr("Name  :", "Nazwa :"), tr("(type)", "(wpisz)"));
  }
  if (setupSel == 2) {
    d.drawBox(0, 37, 128, 8);
    d.setDrawColor(0); d.drawStr(2, 44, line); d.setDrawColor(1);
  } else {
    d.drawStr(2, 44, line);
  }

  // Wiersz 3: Start
  const char* btn = tr("[ Edit empty .md file ]", "[ Edytuj pusty plik ]");
  if (setupSel == 3) {
    d.drawBox(0, 45, 128, 8);
    d.setDrawColor(0); d.drawStr(6, 52, btn); d.setDrawColor(1);
  } else {
    d.drawStr(6, 52, btn);
  }

  d.drawHLine(0, 54, 128);
  d.drawStr(2, 62, tr("Ent:New F2:Tut F3:SD F1", "Ent:Nowy F2:Tut F3:SD F1"));
}

static void drawTutorial() {
  U8G2 &d = uiDisplay();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 18, tr("MARKDOWN TUTORIAL [F2]", "TUTORIAL MARKDOWN [F2]"));
  d.drawHLine(0, 20, 128);

  static const char* const TUT_EN[] = {
    "# Heading    -> H1 bar",
    "## Subhead   -> Underlined",
    "**Bold**     -> Bold text",
    "- List item  -> Bullet",
    "> Quote      -> Quote bar",
    "---          -> Divider",
    "Tab: Toggle Preview/Edit",
    "Ctrl+S / F3: Save file",
    "Arrows: Move | Del: Del",
    "Esc / F2: Close tutorial"
  };
  static const char* const TUT_PL[] = {
    "# Tytul      -> Naglowek",
    "## Podtytul  -> Podkresl.",
    "**Tekst**    -> Pogrub.",
    "- Element    -> Lista",
    "> Cytat      -> Pasek cyt.",
    "---          -> Linia",
    "Tab: Podglad MD / Edycja",
    "Ctrl+S / F3: Zapisz plik",
    "Strzalki: Ruch | Del: Us.",
    "Esc / F2: Zamknij tutorial"
  };

  const char* const* lines = (sysConfig.lang == 1) ? TUT_PL : TUT_EN;
  for (int i = 0; i < 4 && (tutScroll + i) < 10; i++) {
    d.drawStr(2, 29 + i * 8, lines[tutScroll + i]);
  }
  d.drawHLine(0, 54, 128);
  d.drawStr(2, 62, tr("^/v:Scroll  F2/Esc:Back", "^/v:Przewin  F2/Esc:Wst"));
}

static void drawEditor() {
  U8G2 &d = uiDisplay();
  const char* buf = appScratch.notes.buf;
  EditRow rows[96];
  int rowCount = computeEditRows(buf, rows, 96);
  int curR = 0, curC = 0;
  findCursorRowCol(rows, rowCount, noteCursor, curR, curC);

  if (curR < editTopRow) editTopRow = curR;
  else if (curR >= editTopRow + 4) editTopRow = curR - 3;

  int y = 19;
  for (int i = 0; i < 4 && (editTopRow + i) < rowCount; i++) {
    int r = editTopRow + i;
    char line[26];
    uint16_t l = min(rows[r].len, (uint16_t)24);
    memcpy(line, buf + rows[r].start, l);
    line[l] = '\0';
    d.drawStr(2, y, line);

    if (r == curR && cursorVisible) {
      int cx = 2 + curC * 5;
      d.drawBox(cx, y - 7, 5, 8);
      if (curC < (int)l) {
        char ch[2] = { line[curC], '\0' };
        d.setDrawColor(0);
        d.drawStr(cx, y, ch);
        d.setDrawColor(1);
      }
    }
    y += 10;
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("^S/F3:Sav Tab:MD F2:Tut", "^S/F3:Zap Tab:MD F2:Tut"));
}

static void notesDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (noteMode == NOTE_TUTORIAL) {
    drawTutorial();
    uiFinishFrame();
    return;
  }
  if (noteMode == NOTE_ASK) {
    drawAsk();
    rysujToastJesliAktywny();
    uiFinishFrame();
    return;
  }
  if (noteMode == NOTE_LOAD) {
    drawLoadBrowser();
    rysujToastJesliAktywny();
    uiFinishFrame();
    return;
  }
  if (noteMode == NOTE_NEW_SETUP) {
    drawNewSetup();
    rysujToastJesliAktywny();
    uiFinishFrame();
    return;
  }

  if (noteMode == NOTE_PREVIEW) {
    int y = 18;
    for (int i = 0; i < 4 && (noteScroll + i) < mdLineCount; i++) {
      const MdLine &ln = appScratch.notes.lines[noteScroll + i];
      if (ln.style == MD_HR) {
        d.drawHLine(4, y - 3, 120);
      } else if (ln.style == MD_H1) {
        d.drawBox(0, y - 8, 128, 10);
        d.setDrawColor(0);
        d.drawStr(3, y, ln.text);
        d.setDrawColor(1);
      } else if (ln.style == MD_H2) {
        drawMdSpan(2, y - 1, ln.text);
        d.drawHLine(2, y + 1, min(124, (int)strlen(ln.text) * 5 + 2));
      } else if (ln.style == MD_BULLET) {
        d.drawBox(2, y - 4, 3, 3);
        drawMdSpan(8, y, ln.text);
      } else if (ln.style == MD_QUOTE) {
        d.drawVLine(2, y - 7, 9);
        drawMdSpan(6, y, ln.text);
      } else {
        drawMdSpan(2, y, ln.text);
      }
      y += 10;
    }
    d.drawHLine(0, 53, 128);
    d.drawStr(2, 62, tr("MD:View Tab:Edit F2:Tut", "MD:Podg Tab:Edyt F2:Tut"));
  } else {
    drawEditor();
  }

  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_NOTES_EN[] = {
  "F2: Markdown tutorial",
  "Tab: Toggle MD Preview",
  "Ctrl+S / F3: Save file",
  "Arrows: Navigate cursor",
  "Bksp/Del: Delete char",
  "Setup: SD /docs/ default",
  "Esc: Back / Menu"
};
static const char* const HELP_NOTES_PL[] = {
  "F2: Tutorial Markdown",
  "Tab: Podglad MD / Edycja",
  "Ctrl+S / F3: Zapisz plik",
  "Strzalki: Ruch kursora",
  "Bksp/Del: Usun znak",
  "Nowy: Domyslnie SD /docs",
  "Esc: Wstecz / Menu"
};

const AppScreen screenAppNotes = { nullptr, notesTick, notesKey, notesDraw, HELP_NOTES_EN, HELP_NOTES_PL, 7 };
