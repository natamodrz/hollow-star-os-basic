#include "app_notes.h"
#include "app_files.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"

enum NoteMode : uint8_t {
  NOTE_SETUP,
  NOTE_EDIT,
  NOTE_PREVIEW
};

static NoteMode noteMode = NOTE_EDIT;
static bool noteOpenedViaSetup = false;
static char notePath[128] = "/start.md";
static bool noteOnSD = false;
static int  noteScroll = 0;
static int  mdLineCount = 0;
static AppState noteReturnApp = APP_MENU;

// Stan kreatora lokalizacji i nazwy pliku
static int  setupSel = 0; // 0=Dysk, 1=Folder, 2=Nazwa, 3=Utworz/Edytuj
static bool setupOnSD = false;
static char setupDir[8][24];
static int  setupDirCount = 1;
static int  setupDirIdx = 0;
static char setupFileName[32] = "";

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

static void loadSetupDirs() {
  setupDirCount = 1;
  strncpy(setupDir[0], "/", sizeof(setupDir[0]) - 1);
  setupDir[0][sizeof(setupDir[0]) - 1] = '\0';

  if (setupOnSD && !sdMounted) {
    storageMountSD();
  }
  DirEntryInfo *entries = appScratch.files.items;
  int n = storageListDir(setupOnSD, "/", entries, MAX_DIR_ENTRIES);
  for (int i = 0; i < n && setupDirCount < 8; i++) {
    if (entries[i].isDir) {
      snprintf(setupDir[setupDirCount], sizeof(setupDir[0]), "/%.20s", entries[i].name);
      setupDirCount++;
    }
  }
  if (setupDirIdx >= setupDirCount) setupDirIdx = 0;
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

void appNotesOpenSetup(AppState returnApp) {
  noteOpenedViaSetup = true;
  noteReturnApp = returnApp;
  setupSel = 2; // Zaczynamy od nazwy lub użytkownik może przejść ^ na dysk/folder
  setupOnSD = false;
  setupDirIdx = 0;
  setupFileName[0] = '\0';
  loadSetupDirs();
  noteMode = NOTE_SETUP;
  uiSetScreen(APP_NOTES);
}

void appNotesOpenFile(bool onSD, const char* path, AppState returnApp, bool startInPreview) {
  noteOpenedViaSetup = false;
  noteOnSD = onSD;
  strncpy(notePath, path ? path : "/start.md", sizeof(notePath) - 1);
  notePath[sizeof(notePath) - 1] = '\0';
  noteReturnApp = returnApp;
  appScratch.notes.buf[0] = '\0';
  storageReadText(onSD, notePath, appScratch.notes.buf, sizeof(appScratch.notes.buf));
  noteMode = (startInPreview && appScratch.notes.buf[0] != '\0') ? NOTE_PREVIEW : NOTE_EDIT;
  noteScroll = 0;
  if (noteMode == NOTE_PREVIEW) rebuildMarkdownLines();
  uiSetScreen(APP_NOTES);
}

static void launchEmptyNoteFromSetup() {
  if (setupFileName[0] == '\0') {
    setupSel = 2;
    showToast("Podaj nazwe pliku!");
    return;
  }
  if (setupOnSD && !sdMounted && !storageMountSD()) {
    showToast("Brak karty SD!");
    return;
  }
  buildSetupTargetPath(notePath, sizeof(notePath));
  noteOnSD = setupOnSD;
  appScratch.notes.buf[0] = '\0'; // Pusty plik .md gotowy do edycji
  noteScroll = 0;
  noteMode = NOTE_EDIT;
}

static void launchExistingNoteFromSetup() {
  if (setupFileName[0] == '\0') {
    setupSel = 2;
    showToast("Podaj nazwe pliku!");
    return;
  }
  char full[128];
  buildSetupTargetPath(full, sizeof(full));
  if (!storageExists(setupOnSD, full)) {
    showToast("Plik nie istnieje!");
    return;
  }
  noteOnSD = setupOnSD;
  strncpy(notePath, full, sizeof(notePath) - 1);
  notePath[sizeof(notePath) - 1] = '\0';
  appScratch.notes.buf[0] = '\0';
  storageReadText(noteOnSD, notePath, appScratch.notes.buf, sizeof(appScratch.notes.buf));
  noteScroll = 0;
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

static bool setupKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(noteReturnApp);
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
  if (k == KEY_F2) {
    setupOnSD = true;
    bool ok = storageMountSD();
    setupDirIdx = 0;
    loadSetupDirs();
    showToast(ok ? "SD odswiezona!" : "Brak karty SD!");
    return true;
  }
  if (k == KEY_F3) {
    launchExistingNoteFromSetup();
    return true;
  }

  if (setupSel == 0) {
    if (k == KEY_LEFT || k == KEY_RIGHT || k == '\n' || k == ' ') {
      setupOnSD = !setupOnSD;
      setupDirIdx = 0;
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

  // setupSel == 3: przycisk startu
  if (k == '\n' || k == ' ') {
    launchEmptyNoteFromSetup();
  }
  return true;
}

static bool notesKey(uint8_t k) {
  if (noteMode == NOTE_SETUP) {
    return setupKey(k);
  }

  if (k == KEY_ESC) {
    if (noteOpenedViaSetup) {
      noteMode = NOTE_SETUP;
      loadSetupDirs();
    } else {
      uiSetScreen(noteReturnApp);
      if (noteReturnApp == APP_FILES) appFilesRefresh();
    }
    return true;
  }
  if (k == KEY_F3 || k == KEY_CTRL_S) {
    bool ok = storageWriteText(noteOnSD, notePath, appScratch.notes.buf);
    showToast(ok ? "Zapisano plik!" : "Blad zapisu!");
    return true;
  }
  if (k == KEY_F2 || k == KEY_TAB) {
    noteMode = (noteMode == NOTE_PREVIEW) ? NOTE_EDIT : NOTE_PREVIEW;
    if (noteMode == NOTE_PREVIEW) {
      noteScroll = 0;
      rebuildMarkdownLines();
    }
    return true;
  }
  if (noteMode == NOTE_PREVIEW) {
    if (k == KEY_UP && noteScroll > 0) noteScroll--;
    else if (k == KEY_DOWN && noteScroll < max(0, mdLineCount - 4)) noteScroll++;
    return true;
  }

  size_t len = strlen(appScratch.notes.buf);
  if (k == KEY_DEL) {
    appScratch.notes.buf[0] = '\0';
  } else if (k == '\b') {
    if (len > 0) appScratch.notes.buf[len - 1] = '\0';
  } else if (k == '\n' || (k >= 32 && k <= 126)) {
    if (len < 1000) {
      appScratch.notes.buf[len] = (char)k;
      appScratch.notes.buf[len + 1] = '\0';
    }
  }
  return true;
}

static void drawSetup() {
  U8G2 &d = uiDisplay();
  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(2, 17, "NOWY PLIK .MD (WYBOR)");
  d.drawHLine(0, 19, 128);

  char line[28];

  // Wiersz 0: Dysk
  if (setupOnSD) snprintf(line, sizeof(line), "Dysk  : <SD %s>", sdMounted ? "OK" : "BRAK");
  else           snprintf(line, sizeof(line), "Dysk  : <Flash LittleFS>");
  if (setupSel == 0) {
    d.drawBox(0, 21, 128, 8);
    d.setDrawColor(0); d.drawStr(2, 28, line); d.setDrawColor(1);
  } else {
    d.drawStr(2, 28, line);
  }

  // Wiersz 1: Folder
  snprintf(line, sizeof(line), "Folder: <%.16s>", setupDir[setupDirIdx]);
  if (setupSel == 1) {
    d.drawBox(0, 29, 128, 8);
    d.setDrawColor(0); d.drawStr(2, 36, line); d.setDrawColor(1);
  } else {
    d.drawStr(2, 36, line);
  }

  // Wiersz 2: Nazwa
  if (setupFileName[0]) {
    snprintf(line, sizeof(line), "Nazwa : %.13s%s_",
             setupFileName, hasMdOrTxtExt(setupFileName) ? "" : ".md");
  } else {
    snprintf(line, sizeof(line), "Nazwa : _ (wpisz)");
  }
  if (setupSel == 2) {
    d.drawBox(0, 37, 128, 8);
    d.setDrawColor(0); d.drawStr(2, 44, line); d.setDrawColor(1);
  } else {
    d.drawStr(2, 44, line);
  }

  // Wiersz 3: Przycisk startu
  const char* btn = "[ Edytuj pusty plik ]";
  if (setupSel == 3) {
    d.drawBox(0, 45, 128, 8);
    d.setDrawColor(0); d.drawStr(10, 52, btn); d.setDrawColor(1);
  } else {
    d.drawStr(10, 52, btn);
  }

  d.drawHLine(0, 54, 128);
  d.drawStr(2, 62, "Ent:Nowy F3:Otw F2:SD F1");
}

static void notesDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (noteMode == NOTE_SETUP) {
    drawSetup();
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
    d.drawStr(2, 62, "MD:Podg Tab:Edyt F1:Pom");
  } else {
    char lines[4][26];
    int total = 0;
    char cur[26];
    int curLen = 0;

    auto pushLine = [&]() {
      cur[curLen] = '\0';
      int slot = total % 4;
      memcpy(lines[slot], cur, curLen + 1);
      total++;
      curLen = 0;
    };

    const char* txt = appScratch.notes.buf;
    for (size_t i = 0; txt[i]; i++) {
      char c = txt[i];
      if (c == '\n') {
        pushLine();
      } else {
        cur[curLen++] = c;
        if (curLen >= 24) pushLine();
      }
    }
    cur[curLen++] = '_';
    pushLine();

    int visible = min(4, total);
    int firstIdx = total - visible;
    int y = 19;
    for (int i = 0; i < visible; i++) {
      d.drawStr(2, y, lines[(firstIdx + i) % 4]);
      y += 10;
    }
    d.drawHLine(0, 53, 128);
    d.drawStr(2, 62, "^S/F3:Zap Tab:MD F1:Pom");
  }

  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_NOTES[] = {
  "Wybor: ^/v pola </> dysk",
  "Wybor: Ent=pusty F3=otw.",
  "Wybor: F2=montuj SD",
  "Edytor: Ctrl+S/F3 Zapisz",
  "Edytor: Tab/F2 Podgl. MD",
  "Edytor: Del=wyczysc caly",
  "Esc: Wstecz / Menu"
};

const AppScreen screenAppNotes = { nullptr, nullptr, notesKey, notesDraw, HELP_NOTES, 7 };
