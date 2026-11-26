#include "game_brick.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"

static const int BB_COLS = 8, BB_ROWS = 4;
static const int BB_BW = 14, BB_BH = 3, BB_GAP = 2, BB_TOP = 19;
static const int BB_PW = 24, BB_PH = 3, BB_PY = 60, BB_BS = 3;

static float bbPx = 52.0f, bbBx = 62.0f, bbBy = 57.0f, bbVx = 1.4f, bbVy = -1.5f;
static int bbPunkty = 0, bbZycia = 3, bbPoziom = 1;
static bool bbCzeka = true, bbKoniec = false;
static unsigned long bbKlatka = 0;

static void bbResetPilki() {
  bbCzeka = true;
  bbBx = bbPx + (BB_PW - BB_BS) / 2.0f;
  bbBy = BB_PY - BB_BS;
  float predkosc = min(2.3f, 1.35f + (bbPoziom - 1) * 0.2f);
  bbVx = ((random(2) == 0) ? -1 : 1) * (predkosc * 0.75f);
  bbVy = -predkosc;
}

static void bbNowyPoziom() {
  for (int r = 0; r < BB_ROWS; r++)
    for (int c = 0; c < BB_COLS; c++) appScratch.brick.cegly[r][c] = 1;
  bbResetPilki();
}

static void brickEnter() {
  bbPx = (128 - BB_PW) / 2.0f;
  bbPunkty = 0;
  bbZycia = 3;
  bbPoziom = 1;
  bbKoniec = false;
  bbNowyPoziom();
  bbKlatka = millis();
}

static void bbAktualizuj() {
  if (bbKoniec) return;
  if (holdLeft)  bbPx = max(0.0f, bbPx - 3.5f);
  if (holdRight) bbPx = min((float)(128 - BB_PW), bbPx + 3.5f);
  if (bbCzeka) {
    bbBx = bbPx + (BB_PW - BB_BS) / 2.0f;
    bbBy = BB_PY - BB_BS;
    return;
  }

  float prevX = bbBx;
  bbBx += bbVx;
  bbBy += bbVy;

  if (bbBx <= 0) { bbBx = 0; bbVx = abs(bbVx); }
  else if (bbBx + BB_BS >= 128) { bbBx = 128 - BB_BS; bbVx = -abs(bbVx); }
  if (bbBy <= 17) { bbBy = 17; bbVy = abs(bbVy); }

  if (bbVy > 0 && bbBy + BB_BS >= BB_PY && bbBy <= BB_PY + BB_PH &&
      bbBx + BB_BS >= bbPx && bbBx <= bbPx + BB_PW) {
    bbBy = BB_PY - BB_BS;
    float przes = ((bbBx + BB_BS / 2.0f) - (bbPx + BB_PW / 2.0f)) / (BB_PW / 2.0f);
    bbVx = constrain(przes * 2.2f, -2.3f, 2.3f);
    if (abs(bbVx) < 0.4f) bbVx = (bbVx >= 0) ? 0.5f : -0.5f;
    bbVy = -abs(bbVy);
  }

  bool trafiono = false;
  int pozostalo = 0;
  for (int r = 0; r < BB_ROWS; r++) {
    for (int c = 0; c < BB_COLS; c++) {
      if (!appScratch.brick.cegly[r][c]) continue;
      pozostalo++;
      if (trafiono) continue;
      int cx = 1 + c * (BB_BW + BB_GAP), cy = BB_TOP + r * (BB_BH + 1);
      if (bbBx + BB_BS > cx && bbBx < cx + BB_BW && bbBy + BB_BS > cy && bbBy < cy + BB_BH) {
        appScratch.brick.cegly[r][c] = 0;
        bbPunkty += 10;
        pozostalo--;
        trafiono = true;
        if (prevX + BB_BS <= cx || prevX >= cx + BB_BW) bbVx = -bbVx;
        else bbVy = -bbVy;
      }
    }
  }

  if (pozostalo == 0) {
    bbPoziom++;
    bbPunkty += 50;
    bbNowyPoziom();
    return;
  }
  if (bbBy > 63) {
    bbZycia--;
    if (bbZycia <= 0) bbKoniec = true;
    else bbResetPilki();
  }
}

static bool brickTick(unsigned long now) {
  if (now - bbKlatka >= 35UL) {
    bbKlatka = now;
    bbAktualizuj();
    return true;
  }
  return false;
}

static bool brickKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_GAMES_MENU); return true; }
  if (bbKoniec && (k == '\n' || k == ' ')) { brickEnter(); return true; }
  if (bbCzeka && (k == '\n' || k == ' ' || k == KEY_UP)) { bbCzeka = false; return true; }
  return true;
}

static void brickDraw() {
  U8G2 &d = uiDisplay();
  char buf[24];
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);
  snprintf(buf, sizeof(buf), "%s:%d  L%d  %s:%d",
           tr("S", "P"), bbPunkty, bbPoziom, tr("H", "Z"), bbZycia);
  d.drawStr(2, 16, buf);

  for (int r = 0; r < BB_ROWS; r++) {
    for (int c = 0; c < BB_COLS; c++) {
      if (appScratch.brick.cegly[r][c]) {
        int cx = 1 + c * (BB_BW + BB_GAP), cy = BB_TOP + r * (BB_BH + 1);
        if ((r + c) % 2 == 0) d.drawBox(cx, cy, BB_BW, BB_BH);
        else                  d.drawFrame(cx, cy, BB_BW, BB_BH);
      }
    }
  }
  d.drawBox((int)bbPx, BB_PY, BB_PW, BB_PH);
  d.drawBox((int)bbBx, (int)bbBy, BB_BS, BB_BS);
  if (bbCzeka && !bbKoniec) d.drawStr(14, 50, tr("[Space] Start </>", "[Spacja] Start </>"));
  if (bbKoniec) rysujPopupKoniecGry(tr("GAME OVER!", "KONIEC GRY!"));
  uiFinishFrame();
}

static const char* const HELP_BRICK_EN[] = {
  "</> (hold): Move paddle",
  "Spc/Ent/^: Launch ball",
  "Ent/Spc: Restart (over)",
  "Esc: Games menu",
  "F1: Pause & shortcuts"
};
static const char* const HELP_BRICK_PL[] = {
  "</> (trzymaj): Paletka",
  "Spc/Ent/^: Wypusc pilke",
  "Ent/Spc: Nowa gra (koniec)",
  "Esc: Menu gier",
  "F1: Pauza i skroty"
};

const AppScreen screenGameBrick = { brickEnter, brickTick, brickKey, brickDraw, HELP_BRICK_EN, HELP_BRICK_PL, 5 };
