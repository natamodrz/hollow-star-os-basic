#include "game_tetris.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"

static const int T_COLS = 10, T_ROWS = 13, T_BX = 6, T_BY = 11;
static uint8_t tKsztalt[4][4], tNast[4][4];
static int tW = 0, tH = 0, tNW = 0, tNH = 0, tX = 0, tY = 0, tPunkty = 0, tLinie = 0;
static bool tKoniec = false;
static unsigned long tCzasSpadku = 0;

static const uint8_t TETROMINO[7][4][4] = {
  {{1,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
  {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
  {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
  {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
  {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
  {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
  {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}
};
static const uint8_t TET_DIM[7][2] = {{4,1},{2,2},{3,2},{3,2},{3,2},{3,2},{3,2}};

static void tetrisLosuj(uint8_t dst[4][4], int &w, int &h) {
  int idx = random(7);
  w = TET_DIM[idx][0];
  h = TET_DIM[idx][1];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) dst[r][c] = TETROMINO[idx][r][c];
}

static bool tetrisKolizja(uint8_t k[4][4], int w, int h, int x, int y) {
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (k[r][c]) {
        int nx = x + c, ny = y + r;
        if (nx < 0 || nx >= T_COLS || ny >= T_ROWS) return true;
        if (ny >= 0 && appScratch.tetris.plansza[ny][nx]) return true;
      }
  return false;
}

static void tetrisNowyKlocek() {
  memcpy(tKsztalt, tNast, sizeof(tKsztalt));
  tW = tNW;
  tH = tNH;
  tetrisLosuj(tNast, tNW, tNH);
  tX = (T_COLS - tW) / 2;
  tY = 0;
  if (tetrisKolizja(tKsztalt, tW, tH, tX, tY)) tKoniec = true;
}

static void tetrisEnter() {
  memset(appScratch.tetris.plansza, 0, sizeof(appScratch.tetris.plansza));
  tPunkty = 0;
  tLinie = 0;
  tKoniec = false;
  tetrisLosuj(tNast, tNW, tNH);
  tetrisNowyKlocek();
  tCzasSpadku = millis();
}

static void tetrisOsadz() {
  for (int r = 0; r < tH; r++)
    for (int c = 0; c < tW; c++)
      if (tKsztalt[r][c] && tY + r >= 0 && tY + r < T_ROWS)
        appScratch.tetris.plansza[tY + r][tX + c] = 1;

  int usuniete = 0;
  for (int r = T_ROWS - 1; r >= 0; r--) {
    bool pelny = true;
    for (int c = 0; c < T_COLS; c++) {
      if (!appScratch.tetris.plansza[r][c]) { pelny = false; break; }
    }
    if (pelny) {
      usuniete++;
      for (int rr = r; rr > 0; rr--)
        for (int c = 0; c < T_COLS; c++)
          appScratch.tetris.plansza[rr][c] = appScratch.tetris.plansza[rr - 1][c];
      for (int c = 0; c < T_COLS; c++) appScratch.tetris.plansza[0][c] = 0;
      r++;
    }
  }
  if (usuniete > 0) {
    const int pkt[5] = {0, 100, 300, 500, 800};
    tLinie += usuniete;
    tPunkty += pkt[min(usuniete, 4)];
  }
  tetrisNowyKlocek();
}

static void tetrisKrokWDol() {
  if (!tetrisKolizja(tKsztalt, tW, tH, tX, tY + 1)) tY++;
  else tetrisOsadz();
}

static void tetrisObroc() {
  uint8_t obr[4][4] = {0};
  int nw = tH, nh = tW;
  for (int r = 0; r < tH; r++)
    for (int c = 0; c < tW; c++) obr[c][tH - 1 - r] = tKsztalt[r][c];
  const int kicks[4] = {0, -1, 1, -2};
  for (int i = 0; i < 4; i++) {
    if (!tetrisKolizja(obr, nw, nh, tX + kicks[i], tY)) {
      memcpy(tKsztalt, obr, sizeof(obr));
      tW = nw;
      tH = nh;
      tX += kicks[i];
      return;
    }
  }
}

static bool tetrisTick(unsigned long now) {
  if (tKoniec) return false;
  unsigned long intv = max(140UL, (unsigned long)(600 - tLinie * 25));
  if (now - tCzasSpadku >= intv) {
    tCzasSpadku = now;
    tetrisKrokWDol();
    return true;
  }
  return false;
}

static bool tetrisKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_GAMES_MENU); return true; }
  if (tKoniec) {
    if (k == '\n' || k == ' ') tetrisEnter();
    return true;
  }
  unsigned long now = millis();
  if (k == KEY_LEFT && !tetrisKolizja(tKsztalt, tW, tH, tX - 1, tY)) tX--;
  else if (k == KEY_RIGHT && !tetrisKolizja(tKsztalt, tW, tH, tX + 1, tY)) tX++;
  else if (k == KEY_UP) tetrisObroc();
  else if (k == KEY_DOWN) { tetrisKrokWDol(); tCzasSpadku = now; }
  else if (k == ' ' || k == '\n') {
    while (!tetrisKolizja(tKsztalt, tW, tH, tX, tY + 1)) tY++;
    tetrisOsadz();
    tCzasSpadku = now;
  }
  return true;
}

static void tetrisDraw() {
  U8G2 &d = uiDisplay();
  char buf[20];
  d.clearBuffer();
  rysujPasekStanu();
  d.drawFrame(T_BX - 2, T_BY - 1, T_COLS * 4 + 3, T_ROWS * 4 + 2);
  for (int r = 0; r < T_ROWS; r++)
    for (int c = 0; c < T_COLS; c++)
      if (appScratch.tetris.plansza[r][c]) d.drawBox(T_BX + c * 4, T_BY + r * 4, 3, 3);
  for (int r = 0; r < tH; r++)
    for (int c = 0; c < tW; c++)
      if (tKsztalt[r][c] && tY + r >= 0) d.drawBox(T_BX + (tX + c) * 4, T_BY + (tY + r) * 4, 3, 3);

  d.setFont(u8g2_font_5x7_tr);
  snprintf(buf, sizeof(buf), "PKT:%d", tPunkty); d.drawStr(52, 18, buf);
  snprintf(buf, sizeof(buf), "LIN:%d", tLinie);  d.drawStr(52, 27, buf);
  d.drawStr(52, 37, "NAST:");
  for (int r = 0; r < tNH; r++)
    for (int c = 0; c < tNW; c++)
      if (tNast[r][c]) d.drawBox(84 + c * 4, 31 + r * 4, 3, 3);
  d.drawStr(52, 53, "</> ^Obr");
  d.drawStr(52, 62, "vDol Spc:Z");
  if (tKoniec) rysujPopupKoniecGry("KONIEC GRY!");
  uiFinishFrame();
}

static const char* const HELP_TETRIS[] = {
  "</>: Przesun klocek",
  "^: Obroc klocek",
  "v: Przyspiesz opadanie",
  "Spc/Ent: Zrzut natychm.",
  "Esc: Menu gier | F1: Pom."
};

const AppScreen screenGameTetris = { tetrisEnter, tetrisTick, tetrisKey, tetrisDraw, HELP_TETRIS, 5 };
