#include "app_notes.h"
#include "app_files.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"

static char notePath[128] = "/start.md";
static bool noteOnSD = false;
static bool notePreview = false;
static int  noteScroll = 0;
static int  mdLineCount = 0;
static AppState noteReturnApp = APP_MENU;

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

void appNotesOpenFile(bool onSD, const char* path, AppState returnApp, bool startInPreview) {
  noteOnSD = onSD;
  strncpy(notePath, path ? path : "/start.md", sizeof(notePath) - 1);
  notePath[sizeof(notePath) - 1] = '\0';
  noteReturnApp = returnApp;
  appScratch.notes.buf[0] = '\0';
  storageReadText(onSD, notePath, appScratch.notes.buf, sizeof(appScratch.notes.buf));
  notePreview = startInPreview && (appScratch.notes.buf[0] != '\0');
  noteScroll = 0;
  if (notePreview) rebuildMarkdownLines();
  uiSetScreen(APP_NOTES);
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

static bool notesKey(uint8_t k) {
  if (k == KEY_ESC) {
    uiSetScreen(noteReturnApp);
    if (noteReturnApp == APP_FILES) appFilesRefresh();
    return true;
  }
  if (k == KEY_F3 || k == KEY_CTRL_S) {
    bool ok = storageWriteText(noteOnSD, notePath, appScratch.notes.buf);
    showToast(ok ? "Zapisano plik!" : "Blad zapisu!");
    return true;
  }
  if (k == KEY_F2 || k == KEY_TAB) {
    notePreview = !notePreview;
    if (notePreview) {
      noteScroll = 0;
      rebuildMarkdownLines();
    }
    return true;
  }
  if (notePreview) {
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

static void notesDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (notePreview) {
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
    // Ostatnie 4 zawinięte wiersze w małym buforze na stosie (bez String w .bss)
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
  "Ctrl+S lub F3: Zapisz",
  "Tab lub F2: Podglad MD",
  "^ / v: Przewin (podglad)",
  "Bksp: Usun znak",
  "Del: Wyczysc cala tresc",
  "Esc: Powrot",
  "F1: Skroty klawiszowe"
};

const AppScreen screenAppNotes = { nullptr, nullptr, notesKey, notesDraw, HELP_NOTES, 7 };
