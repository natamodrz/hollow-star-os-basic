#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "../../storage_cfg.h"
#include "../../bt_audio.h"
#include "../../net_ftp.h"
#include "app_types.h"

#define OS_BRAND_NAME    "HOLLOW STAR OS"
#define OS_BRAND_EDITION "BASIC"
#define OS_BRAND_VERSION "v1.1"

enum MdStyle : uint8_t {
  MD_NORMAL,
  MD_H1,
  MD_H2,
  MD_BULLET,
  MD_QUOTE,
  MD_HR
};

struct MdLine {
  char text[26];
  MdStyle style;
};

struct RacingEnemy {
  bool active;
  int8_t lane;
  int8_t y;
};

// Współdzielony obszar RAM dla aktualnie otwartej aplikacji/gry
union AppScratchRam {
  struct {
    DirEntryInfo items[MAX_DIR_ENTRIES];
  } files;
  struct {
    char buf[1025];
    MdLine lines[48];
  } notes;
  struct {
    uint8_t cegly[4][8];
  } brick;
  struct {
    uint8_t plansza[13][10];
  } tetris;
  struct {
    RacingEnemy enemies[4];
  } racing;
};

extern AppScratchRam appScratch;
extern AudioUiSnapshot audioSnap;
extern NetUiSnapshot   netSnap;
extern AppState ekran;

// Ekran (inicjowany w .ino)
void uiAttachDisplay(U8G2 *disp);
U8G2& uiDisplay();
void uiSetScreen(AppState next);

// Zegar systemowy
void getClock(int &h, int &m);
void setClock(int h, int m);
bool clockMinuteTicked();

// Powiadomienia (toast)
void showToast(const char* msg, unsigned long ms = 1400);
bool toastUpdateExpiry(unsigned long now);
void rysujToastJesliAktywny();

// Okna dialogowe (modal)
ModalMode modalGetMode();
void modalClose();
void modalOpenInput(ModalMode mode, const char* title, const char* initial = "");
void modalOpenConfirm(ModalMode mode, const char* title);
const char* modalGetInput();
bool modalHandleKey(uint8_t k, bool &confirmedOut, bool &submittedOut);
void rysujModal();

// Branding i wspólny pasek stanu (wyłącznie ostre kształty, brak zaokrągleń)
void rysujHollowStar(int cx, int cy, bool smallIcon);
void rysujBootSplash(const char* statusLine);
void rysujPasekStanu();
void rysujPopupKoniecGry(const char* title);
