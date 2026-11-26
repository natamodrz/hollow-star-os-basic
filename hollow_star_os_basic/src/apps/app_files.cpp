#include "app_files.h"
#include "app_notes.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../bt_audio.h"

static bool fmOnSD = false;
static char fmPath[128] = "/";
static int  fmCount = 0;
static int  fmSel = 0;

static void buildFullPath(const char* name, char* out, size_t cap) {
  if (strcmp(fmPath, "/") == 0) snprintf(out, cap, "/%s", name);
  else                          snprintf(out, cap, "%s/%s", fmPath, name);
}

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

void appFilesRefresh() {
  if (fmOnSD && !sdMounted) storageMountSD();
  fmCount = storageListDir(fmOnSD, fmPath, appScratch.files.items, MAX_DIR_ENTRIES);
  if (fmSel >= fmCount) fmSel = max(0, fmCount - 1);
}

void appFilesResetRoot() {
  strcpy(fmPath, "/");
  fmSel = 0;
  appFilesRefresh();
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
        char full[128], initial[80];
        buildFullPath(fn, full, sizeof(full));
        snprintf(initial, sizeof(initial), "# %s\n", rawName);
        storageWriteText(fmOnSD, full, initial);
        modalClose();
        appNotesOpenFile(fmOnSD, full, APP_FILES, false);
        return true;
      }
      modalClose();
    } else if (confirmed && modalGetMode() == MODAL_CONFIRM_DEL) {
      if (fmSel < fmCount) {
        char full[128];
        buildFullPath(appScratch.files.items[fmSel].name, full, sizeof(full));
        bool ok = storageDeletePath(fmOnSD, full, appScratch.files.items[fmSel].isDir);
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
  } else if (k == '\b') {
    if (strcmp(fmPath, "/") != 0) {
      char* slash = strrchr(fmPath, '/');
      if (slash && slash != fmPath) *slash = '\0';
      else                          strcpy(fmPath, "/");
      fmSel = 0;
      appFilesRefresh();
    }
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
    snprintf(title, sizeof(title), "%s %.18s", tr("Del:", "Usun:"), appScratch.files.items[fmSel].name);
    modalOpenConfirm(MODAL_CONFIRM_DEL, title);
  } else if (fmCount > 0) {
    if (k == KEY_UP)        fmSel = (fmSel + fmCount - 1) % fmCount;
    else if (k == KEY_DOWN) fmSel = (fmSel + 1) % fmCount;
    else if (k == '\n') {
      char full[128];
      const DirEntryInfo &item = appScratch.files.items[fmSel];
      buildFullPath(item.name, full, sizeof(full));
      if (item.isDir) {
        strncpy(fmPath, full, sizeof(fmPath) - 1);
        fmPath[sizeof(fmPath) - 1] = '\0';
        fmSel = 0;
        appFilesRefresh();
      } else if (isTextEditable(item.name)) {
        appNotesOpenFile(fmOnSD, full, APP_FILES, hasExtCI(item.name, ".md"));
      } else if (hasExtCI(item.name, ".mp3") && fmOnSD) {
        btAudioPlayPath(full);
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

  char hdr[28];
  snprintf(hdr, sizeof(hdr), "%s:%.21s", fmOnSD ? "SD" : "FL", fmPath);
  d.drawStr(2, 18, hdr);
  d.drawHLine(0, 20, 128);

  if (fmCount == 0) {
    d.drawStr(4, 34, (fmOnSD && !sdMounted)
                     ? tr("No SD card!", "Brak karty SD!")
                     : tr("(Empty folder)", "(Pusty katalog)"));
  } else {
    int startIdx = max(0, min(fmSel - 1, fmCount - 3));
    for (int i = 0; i < 3 && (startIdx + i) < fmCount; i++) {
      int idx = startIdx + i;
      int y = 22 + i * 10;
      const DirEntryInfo &item = appScratch.files.items[idx];
      char line[28];
      if (item.isDir) {
        snprintf(line, sizeof(line), "[%.22s]", item.name);
      } else {
        snprintf(line, sizeof(line), "%-18.18s %luB", item.name, (unsigned long)item.size);
      }
      if (idx == fmSel) {
        d.drawBox(0, y, 128, 9);
        d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
      } else {
        d.drawStr(2, y + 7, line);
      }
    }
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Tab:Drv N:New F1:Help", "Tab:Dysk N:Nowy F1:Pom"));

  if (modalGetMode() != MODAL_NONE) rysujModal();
  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_FILES_EN[] = {
  "^ / v: Select item",
  "Enter: Open / Play MP3",
  "Tab: Switch Flash / SD",
  "N or F2: New .md file",
  "R: Refresh / Mount SD",
  "Del: Delete | Bksp: Up",
  "Esc: Main menu"
};
static const char* const HELP_FILES_PL[] = {
  "^ / v: Wybor pliku/kat.",
  "Enter: Otworz / Graj MP3",
  "Tab: Zmien Flash / SD",
  "N lub F2: Nowy plik .md",
  "R: Odswiez / montuj SD",
  "Del: Usun | Bksp: Wstecz",
  "Esc: Menu glowne"
};

const AppScreen screenAppFiles = { nullptr, nullptr, filesKey, filesDraw, HELP_FILES_EN, HELP_FILES_PL, 7 };
