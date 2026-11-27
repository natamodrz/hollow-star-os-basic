#include "app_files.h"
#include "app_notes.h"
#include "app_basic.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../bt_audio.h"
#include <new>

static bool fmOnSD = true;
static int  fmCount = 0;
static int  fmSel = 0;
static char expandedDirs[6][84];
static int  expandedCount = 0;

static bool hasExtCI(const char* name, const char* ext) {
  size_t nl = strlen(name), el = strlen(ext);
  if (nl < el) return false;
  return strcasecmp(name + nl - el, ext) == 0;
}

static bool isTextEditable(const char* name) {
  return hasExtCI(name, ".md")  || hasExtCI(name, ".txt") ||
         hasExtCI(name, ".cfg") || hasExtCI(name, ".log") ||
         hasExtCI(name, ".ini") || hasExtCI(name, ".csv") ||
         hasExtCI(name, ".json");
}

static bool isDirExpanded(const char* fullPath) {
  for (int i = 0; i < expandedCount; i++) {
    if (strcmp(expandedDirs[i], fullPath) == 0) return true;
  }
  return false;
}

static void toggleDirExpanded(const char* fullPath) {
  for (int i = 0; i < expandedCount; i++) {
    if (strcmp(expandedDirs[i], fullPath) == 0) {
      for (int j = i; j < expandedCount - 1; j++) {
        memcpy(expandedDirs[j], expandedDirs[j + 1], sizeof(expandedDirs[0]));
      }
      expandedCount--;
      return;
    }
  }
  if (expandedCount < 6) {
    strncpy(expandedDirs[expandedCount], fullPath, sizeof(expandedDirs[0]) - 1);
    expandedDirs[expandedCount][sizeof(expandedDirs[0]) - 1] = '\0';
    expandedCount++;
  }
}

static void buildTreeRecursive(const char* dirPath, uint8_t depth, uint8_t contMask) {
  if (depth >= 3 || fmCount >= 28) return;
  DirEntryInfo *entries = new (std::nothrow) DirEntryInfo[16];
  if (!entries) return;

  int n = storageListDir(fmOnSD, dirPath, entries, 16);
  for (int i = 0; i < n && fmCount < 28; i++) {
    bool isLast = (i == n - 1);
    FileTreeNode &node = appScratch.files.tree[fmCount++];
    strncpy(node.name, entries[i].name, sizeof(node.name) - 1);
    node.name[sizeof(node.name) - 1] = '\0';

    if (strcmp(dirPath, "/") == 0) snprintf(node.fullPath, sizeof(node.fullPath), "/%s", entries[i].name);
    else                           snprintf(node.fullPath, sizeof(node.fullPath), "%s/%s", dirPath, entries[i].name);

    node.size = entries[i].size;
    node.isDir = entries[i].isDir;
    node.depth = depth;
    node.expanded = entries[i].isDir && isDirExpanded(node.fullPath);
    node.contMask = contMask;
    node.isLast = isLast;

    if (node.expanded) {
      uint8_t nextMask = contMask;
      if (!isLast && depth < 7) nextMask |= (uint8_t)(1U << depth);
      buildTreeRecursive(node.fullPath, depth + 1, nextMask);
    }
  }
  delete[] entries;
}

void appFilesRefresh() {
  if (fmOnSD && !sdMounted) storageMountSD();
  fmCount = 0;
  buildTreeRecursive("/", 0, 0);
  if (fmSel >= fmCount) fmSel = max(0, fmCount - 1);
}

void appFilesResetRoot() {
  expandedCount = 0;
  fmSel = 0;
  appFilesRefresh();
}

static void getTargetDirForNew(char* out, size_t cap) {
  if (fmCount > 0 && fmSel < fmCount) {
    const FileTreeNode &node = appScratch.files.tree[fmSel];
    if (node.isDir) {
      snprintf(out, cap, "%s", node.fullPath);
      return;
    }
    const char* slash = strrchr(node.fullPath, '/');
    if (slash && slash != node.fullPath) {
      size_t len = min((size_t)(slash - node.fullPath), cap - 1);
      memcpy(out, node.fullPath, len);
      out[len] = '\0';
      return;
    }
  }
  snprintf(out, cap, "/");
}

static bool filesKey(uint8_t k) {
  if (modalGetMode() != MODAL_NONE) {
    bool confirmed = false, submitted = false;
    modalHandleKey(k, confirmed, submitted);
    if (submitted && modalGetMode() == MODAL_NEW_MD) {
      const char* rawName = modalGetInput();
      if (rawName[0]) {
        char fn[72];
        if (hasExtCI(rawName, ".md") || hasExtCI(rawName, ".txt")) {
          snprintf(fn, sizeof(fn), "%s", rawName);
        } else {
          snprintf(fn, sizeof(fn), "%s.md", rawName);
        }
        char dirBase[84], full[128], initial[80];
        getTargetDirForNew(dirBase, sizeof(dirBase));
        if (strcmp(dirBase, "/") == 0) snprintf(full, sizeof(full), "/%s", fn);
        else                           snprintf(full, sizeof(full), "%s/%s", dirBase, fn);
        snprintf(initial, sizeof(initial), "# %s\n", rawName);
        storageWriteText(fmOnSD, full, initial);
        modalClose();
        appNotesOpenFile(fmOnSD, full, APP_FILES, false);
        return true;
      }
      modalClose();
    } else if (confirmed && modalGetMode() == MODAL_CONFIRM_DEL) {
      if (fmSel < fmCount) {
        const FileTreeNode &node = appScratch.files.tree[fmSel];
        bool ok = storageDeletePath(fmOnSD, node.fullPath, node.isDir);
        showToast(ok ? tr("Deleted!", "Usunieto!") : tr("Delete error", "Blad usuwania"));
        appFilesRefresh();
      }
      modalClose();
    }
    return true;
  }

  if (k == KEY_ESC) {
    uiSetScreen(APP_MENU);
  } else if (k == KEY_TAB) {
    fmOnSD = !fmOnSD;
    appFilesResetRoot();
  } else if (k == KEY_F2 || k == 'n' || k == 'N') {
    modalOpenInput(MODAL_NEW_MD, tr("New file (.md):", "Nowy plik (.md):"));
  } else if (k == 'r' || k == 'R') {
    if (fmOnSD) {
      bool ok = storageMountSD();
      showToast(ok ? tr("SD mounted!", "SD odswiezona!") : tr("No SD card!", "Brak karty SD!"));
    } else {
      showToast(tr("Flash refreshed", "Odswiezono Flash"));
    }
    appFilesRefresh();
  } else if (k == KEY_DEL && fmCount > 0) {
    char title[26];
    snprintf(title, sizeof(title), "%s %.18s", tr("Del:", "Usun:"), appScratch.files.tree[fmSel].name);
    modalOpenConfirm(MODAL_CONFIRM_DEL, title);
  } else if (fmCount > 0) {
    if (k == KEY_UP) {
      fmSel = (fmSel + fmCount - 1) % fmCount;
    } else if (k == KEY_DOWN) {
      fmSel = (fmSel + 1) % fmCount;
    } else if (k == KEY_LEFT) {
      const FileTreeNode &node = appScratch.files.tree[fmSel];
      if (node.isDir && node.expanded) {
        toggleDirExpanded(node.fullPath);
        appFilesRefresh();
      } else if (node.depth > 0) {
        for (int p = fmSel - 1; p >= 0; p--) {
          if (appScratch.files.tree[p].depth < node.depth) {
            fmSel = p;
            break;
          }
        }
      }
    } else if (k == KEY_RIGHT) {
      const FileTreeNode &node = appScratch.files.tree[fmSel];
      if (node.isDir && !node.expanded) {
        toggleDirExpanded(node.fullPath);
        appFilesRefresh();
      }
    } else if (k == '\n') {
      const FileTreeNode &node = appScratch.files.tree[fmSel];
      if (node.isDir) {
        toggleDirExpanded(node.fullPath);
        appFilesRefresh();
      } else if (hasExtCI(node.name, ".bas")) {
        appBasicLoadAndRun(fmOnSD, node.fullPath);
      } else if (isTextEditable(node.name)) {
        appNotesOpenFile(fmOnSD, node.fullPath, APP_FILES, hasExtCI(node.name, ".md"));
      } else if (hasExtCI(node.name, ".mp3") && fmOnSD) {
        btAudioPlayPath(node.fullPath);
        showToast(tr("Playing MP3...", "Odtwarzam MP3..."));
      } else {
        showToast(tr("Unsupported file", "Nieobslugiwany format"));
      }
    }
  }
  return true;
}

static void filesDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (fmOnSD) rysujIkoneKartaSD(2, 11);
  else        rysujIkoneCzip(2, 11);
  char hdr[24];
  snprintf(hdr, sizeof(hdr), "%s (%d)", fmOnSD ? "SD:" : "FL:", fmCount);
  d.drawStr(11, 18, hdr);
  d.drawHLine(0, 20, 128);

  if (fmCount == 0) {
    d.drawStr(4, 34, (fmOnSD && !sdMounted)
                     ? tr("No SD card!", "Brak karty SD!")
                     : tr("(Empty drive)", "(Pusty dysk)"));
  } else {
    int startIdx = max(0, min(fmSel - 1, fmCount - 4));
    for (int i = 0; i < 4 && (startIdx + i) < fmCount; i++) {
      int idx = startIdx + i;
      int y = 21 + i * 8;
      const FileTreeNode &node = appScratch.files.tree[idx];

      if (idx == fmSel) {
        d.drawBox(0, y, 128, 8);
        d.setDrawColor(0);
      }

      // Rysuj pionowe gałęzie przodków |
      for (uint8_t lv = 0; lv < node.depth; lv++) {
        if (node.contMask & (1U << lv)) {
          d.drawVLine(3 + lv * 10, y, 8);
        }
      }

      // Rysuj łącznik |--- dla bieżącego węzła
      int cx = 3 + node.depth * 10;
      d.drawVLine(cx, y, node.isLast ? 5 : 8);
      d.drawHLine(cx, y + 4, 5);

      // Ikona (Folder / Muzyka / Plik)
      int ix = cx + 6;
      if (node.isDir)                        rysujIkoneFolder(ix, y + 1);
      else if (hasExtCI(node.name, ".mp3"))  rysujIkoneMuzyka(ix, y + 1);
      else                                   rysujIkonePlik(ix, y + 1);

      int tx = ix + 9;
      int maxChars = max(4, (127 - tx) / 5);
      char line[24];
      if (node.isDir) snprintf(line, sizeof(line), "%.*s/", maxChars - 1, node.name);
      else            snprintf(line, sizeof(line), "%.*s",  maxChars, node.name);
      d.drawStr(tx, y + 7, line);

      d.setDrawColor(1);
    }
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Tab:Drv </>:Tree F1:Help", "Tab:Dysk </>:Drz F1:Pom"));

  if (modalGetMode() != MODAL_NONE) rysujModal();
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_FILES_EN[] = {
  "^ / v: Select node",
  "Ent / >: Expand / Open",
  "<: Collapse / Parent",
  "Tab: Switch SD / Flash",
  "N or F2: New .md file",
  "R: Mount SD | Del: Del",
  "Esc: Main menu"
};
static const char* const HELP_FILES_PL[] = {
  "^ / v: Wybor elementu",
  "Ent / >: Rozwin / Otworz",
  "<: Zwin / Katalog wyzej",
  "Tab: Zmien SD / Flash",
  "N lub F2: Nowy plik .md",
  "R: Montuj SD | Del: Usun",
  "Esc: Menu glowne"
};

const AppScreen screenAppFiles = { nullptr, nullptr, filesKey, filesDraw, HELP_FILES_EN, HELP_FILES_PL, 7 };
