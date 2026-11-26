#include "game_racing.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"

static const int R_ROWS = 13, R_BX = 6, R_BY = 11;
static const uint8_t CAR_SPRITE[4][3] = {
  {0, 1, 0},
  {1, 1, 1},
  {0, 1, 0},
  {1, 0, 1}
};

static int rPlayerLane = 1, rScore = 0, rHighScore = 0, rSpeed = 1;
static int rBorderPhase = 0, rSpawnCooldown = 0, rLastLane = -1;
static bool rGameOver = false;
static unsigned long rLastTick = 0;

static void racingSaveRecordIfNeeded() {
  if (rScore > sysConfig.racingHighScore) {
    sysConfig.racingHighScore = rScore;
    rHighScore = rScore;
    storageSaveConfig();
  }
}

static bool racingCheckCollision() {
  for (int i = 0; i < 4; i++) {
    const RacingEnemy &e = appScratch.racing.enemies[i];
    if (e.active && e.lane == rPlayerLane && e.y + 3 >= 9 && e.y <= 12) {
      return true;
    }
  }
  return false;
}

static void racingEnter() {
  rPlayerLane = 1;
  rScore = 0;
  rSpeed = 1;
  rBorderPhase = 0;
  rSpawnCooldown = 1;
  rLastLane = -1;
  rGameOver = false;
  rHighScore = sysConfig.racingHighScore;
  for (int i = 0; i < 4; i++) appScratch.racing.enemies[i].active = false;
  rLastTick = millis();
}

static void racingTick() {
  if (rGameOver) return;
  rBorderPhase = (rBorderPhase + 1) & 3;

  int minActiveY = 99;
  for (int i = 0; i < 4; i++) {
    RacingEnemy &e = appScratch.racing.enemies[i];
    if (!e.active) continue;
    e.y++;
    if (e.y >= R_ROWS) {
      e.active = false;
      rScore += 10;
      if (rScore > rHighScore) rHighScore = rScore;
      rSpeed = min(9, 1 + rScore / 80);
    } else if (e.y < minActiveY) {
      minActiveY = e.y;
    }
  }

  if (racingCheckCollision()) {
    rGameOver = true;
    racingSaveRecordIfNeeded();
    return;
  }

  if (rSpawnCooldown > 0) {
    rSpawnCooldown--;
    return;
  }

  // Gwarantowany odstęp pionowy min. 6 pustych wierszy między autami (wysokość auta = 4),
  // dzięki czemu gracz zawsze zdąży przejechać między dowolnymi pasami (brak pułapek).
  const int minNextY = (rSpeed >= 6) ? 6 : 7;
  if (minActiveY < minNextY) return;

  for (int i = 0; i < 4; i++) {
    if (!appScratch.racing.enemies[i].active) {
      int lane = random(3);
      if (lane == rLastLane && random(3) != 0) {
        lane = (lane + 1 + random(2)) % 3;
      }
      appScratch.racing.enemies[i].active = true;
      appScratch.racing.enemies[i].lane = (int8_t)lane;
      appScratch.racing.enemies[i].y = -4;
      rLastLane = lane;
      rSpawnCooldown = (rSpeed >= 6) ? 1 : random(1, 3);
      break;
    }
  }
}

static bool racingTickCheck(unsigned long now) {
  if (rGameOver) return false;
  unsigned long intv = max(75UL, (unsigned long)(280 - (rSpeed - 1) * 24));
  if (holdUp) intv = max(55UL, intv / 2);
  if (now - rLastTick >= intv) {
    rLastTick = now;
    racingTick();
    return true;
  }
  return false;
}

static bool racingKey(uint8_t k) {
  if (k == KEY_ESC) {
    racingSaveRecordIfNeeded();
    uiSetScreen(APP_GAMES_MENU);
    return true;
  }
  if (rGameOver) {
    if (k == '\n' || k == ' ') racingEnter();
    return true;
  }
  if (k == KEY_LEFT && rPlayerLane > 0) rPlayerLane--;
  else if (k == KEY_RIGHT && rPlayerLane < 2) rPlayerLane++;

  if (racingCheckCollision()) {
    rGameOver = true;
    racingSaveRecordIfNeeded();
  }
  return true;
}

static void racingDrawCar(int lane, int topRow) {
  U8G2 &d = uiDisplay();
  int baseCol = 1 + lane * 3;
  for (int r = 0; r < 4; r++) {
    int gy = topRow + r;
    if (gy < 0 || gy >= R_ROWS) continue;
    for (int c = 0; c < 3; c++) {
      if (CAR_SPRITE[r][c]) d.drawBox(R_BX + (baseCol + c) * 4, R_BY + gy * 4, 3, 3);
    }
  }
}

static void racingDraw() {
  U8G2 &d = uiDisplay();
  char buf[20];
  d.clearBuffer();
  rysujPasekStanu();
  d.drawFrame(R_BX - 2, R_BY - 1, 11 * 4 + 3, R_ROWS * 4 + 2);
  for (int r = 0; r < R_ROWS; r++) {
    if (((r - rBorderPhase) & 3) != 0) {
      d.drawBox(R_BX, R_BY + r * 4, 3, 3);
      d.drawBox(R_BX + 10 * 4, R_BY + r * 4, 3, 3);
    }
  }
  for (int i = 0; i < 4; i++) {
    const RacingEnemy &e = appScratch.racing.enemies[i];
    if (e.active) racingDrawCar(e.lane, e.y);
  }
  racingDrawCar(rPlayerLane, 9);

  d.setFont(u8g2_font_5x7_tr);
  d.drawStr(56, 18, tr("RACING", "WYSCIGI"));
  snprintf(buf, sizeof(buf), "%s:%d", tr("PTS", "PKT"), rScore);     d.drawStr(56, 28, buf);
  snprintf(buf, sizeof(buf), "%s:%d", tr("HI",  "REK"), rHighScore); d.drawStr(56, 37, buf);
  snprintf(buf, sizeof(buf), "SPD:%d", rSpeed);                      d.drawStr(56, 46, buf);
  d.drawStr(56, 55, tr("</> Lane", "</> Pas"));
  d.drawStr(56, 63, "^ Turbo");
  if (rGameOver) rysujPopupKoniecGry(tr("CRASH!", "WYPADEK!"));
  uiFinishFrame();
}

static const char* const HELP_RACING_EN[] = {
  "</>: Change lane",
  "^ (hold): Turbo speed",
  "Ent/Spc: Restart (over)",
  "Esc: Games menu",
  "F1: Pause & shortcuts"
};
static const char* const HELP_RACING_PL[] = {
  "</>: Zmiana pasa",
  "^ (trzymaj): Turbo",
  "Ent/Spc: Nowa gra (koniec)",
  "Esc: Menu gier",
  "F1: Pauza i skroty"
};

const AppScreen screenGameRacing = { racingEnter, racingTickCheck, racingKey, racingDraw, HELP_RACING_EN, HELP_RACING_PL, 5 };
