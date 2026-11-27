#include <Arduino.h>
#include <U8g2lib.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include "usb_kbd.h"
#include "storage_cfg.h"
#include "bt_audio.h"
#include "net_ftp.h"
#include "src/os/app_types.h"
#include "src/os/ui_common.h"
#include "src/os/ui_menus.h"
#include "src/apps/app_setup.h"
#include "src/apps/app_temp.h"
#include "src/apps/app_calc.h"
#include "src/apps/app_notes.h"
#include "src/apps/app_files.h"
#include "src/apps/app_music.h"
#include "src/apps/app_ftp.h"
#include "src/apps/app_basic.h"
#include "src/apps/app_settings.h"
#include "src/games/game_brick.h"
#include "src/games/game_tetris.h"
#include "src/games/game_racing.h"

// =========================================================
// 1. EKRAN ST7565 (SCL=18, SI=19, CS=17, RS=16, RSE=20)
// =========================================================
// Jeśli obraz jest do góry nogami, zmień U8G2_R0 na U8G2_R2
U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI u8g2(U8G2_R0, 18, 19, 17, 16, 20);

// Tabela ekranów indeksowana przez AppState
static const AppScreen* const SCREENS[APP_STATE_COUNT] = {
  &screenMainMenu,     // APP_MENU
  &screenAppSetup,     // APP_SETUP
  &screenAppTemp,      // APP_TEMP
  &screenAppCalc,      // APP_CALC
  &screenAppNotes,     // APP_NOTES
  &screenGamesMenu,    // APP_GAMES_MENU
  &screenAppMusic,     // APP_MUSIC
  &screenSettingsMenu, // APP_SETTINGS_MENU
  &screenAppFiles,     // APP_FILES
  &screenToolsMenu,    // APP_TOOLS_MENU
  &screenAppFtp,       // APP_FTP
  &screenAppBasic,     // APP_BASIC
  nullptr,             // APP_SUSPEND
  &screenGameBrick,    // APP_BRICK
  &screenGameTetris,   // APP_TETRIS
  &screenGameRacing,   // APP_RACING
  &screenSetBt,        // APP_SET_BT
  &screenSetWifi,      // APP_SET_WIFI
  &screenSetContrast,  // APP_SET_CONTRAST
  &screenSetTime,      // APP_SET_TIME
  &screenSetLang,      // APP_SET_LANG
  &screenSetFormat,    // APP_SET_FORMAT
  &screenSetAbout      // APP_SET_ABOUT
};

static_assert(sizeof(SCREENS) / sizeof(SCREENS[0]) == APP_STATE_COUNT, "SCREENS size mismatch");

void uiSetScreen(AppState next) {
  uiHelpClose();
  ekran = next;
  if (ekran < APP_STATE_COUNT && SCREENS[ekran] && SCREENS[ekran]->onEnter) {
    SCREENS[ekran]->onEnter();
  }
}

static void przerysujAktywny() {
  if (ekran < APP_STATE_COUNT && SCREENS[ekran] && SCREENS[ekran]->onDraw) {
    SCREENS[ekran]->onDraw();
  }
}

void osEnterSuspend() {
  btAudioSendCmd(ACMD_STOP);
  netFtpSendCmd(NCMD_FTP_STOP);

  u8g2.clearBuffer();
  u8g2.drawFrame(2, 2, 124, 60);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(28, 28, tr("SUSPENDED...", "USPIENIE..."));
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(8, 44, tr("Any key wakes display", "Dowolny klawisz wybudza"));
  u8g2.sendBuffer();

  vTaskDelay(pdMS_TO_TICKS(600));
  u8g2.setPowerSave(1);

  uint8_t dummy;
  while (xQueueReceive(keyQueue, &dummy, 0) == pdTRUE) {}
  ekran = APP_SUSPEND;
}

// =========================================================
// 2. RDZEŃ 0: WĄTEK UI (FreeRTOS SMP)
// =========================================================
void setup() {
  Serial.begin(115200);
  Serial.println("=== HOLLOW STAR OS " OS_BRAND_VERSION " (" HSOS_BOARD_TAG " FreeRTOS SMP) ===");
  randomSeed(micros() + analogRead(26));

  usbKbdInitQueue();
  storageInit();

  uiAttachDisplay(&u8g2);
  u8g2.begin();
  u8g2.setContrast(sysConfig.contrast * 4);
  rysujBootSplash(tr("Starting services...", "Uruchamianie uslug..."));

  btAudioStartTask();
  netFtpStartTask();

  vTaskDelay(pdMS_TO_TICKS(900));

  btAudioGetSnapshot(audioSnap);
  netFtpGetSnapshot(netSnap);
  uiSetScreen(sysConfig.setupDone ? APP_MENU : APP_SETUP);
  przerysujAktywny();
}

void loop() {
  if (ekran == APP_SUSPEND) {
    uint8_t wakeKey;
    if (xQueueReceive(keyQueue, &wakeKey, portMAX_DELAY) == pdTRUE) {
      u8g2.setPowerSave(0);
      u8g2.setContrast(sysConfig.contrast * 4);
      uiSetScreen(APP_MENU);
      przerysujAktywny();
    }
    return;
  }

  unsigned long teraz = millis();
  bool needRedraw = false;

  if (btAudioTakeDirty()) { btAudioGetSnapshot(audioSnap); needRedraw = true; }
  if (netFtpTakeDirty())  { netFtpGetSnapshot(netSnap);    needRedraw = true; }

  int nh, nm;
  if (netFtpConsumeSyncedTime(nh, nm)) {
    setClock(nh, nm);
    needRedraw = true;
  }

  if (kbdStatusDirty) { kbdStatusDirty = false; needRedraw = true; }
  if (clockMinuteTicked()) needRedraw = true;
  if (toastUpdateExpiry(teraz)) needRedraw = true;

  if (!uiHelpIsOpen() && ekran < APP_STATE_COUNT && SCREENS[ekran] && SCREENS[ekran]->onTick) {
    if (SCREENS[ekran]->onTick(teraz)) needRedraw = true;
  }

  uint8_t k;
  while (keyQueue != nullptr && xQueueReceive(keyQueue, &k, 0) == pdTRUE) {
    needRedraw = true;
    if (uiHelpIsOpen()) {
      uiHelpHandleKey(k);
      continue;
    }
    if (k == KEY_F1 && modalGetMode() == MODAL_NONE &&
        ekran < APP_STATE_COUNT && SCREENS[ekran] && SCREENS[ekran]->helpCount > 0) {
      const char* const* hl = (sysConfig.lang == 1) ? SCREENS[ekran]->helpLinesPl : SCREENS[ekran]->helpLinesEn;
      if (hl) {
        uiHelpOpen(hl, SCREENS[ekran]->helpCount);
        continue;
      }
    }
    if (ekran < APP_STATE_COUNT && SCREENS[ekran] && SCREENS[ekran]->onKey) {
      SCREENS[ekran]->onKey(k);
    }
    if (ekran == APP_SUSPEND) return;
  }

  if (needRedraw) przerysujAktywny();
  vTaskDelay(pdMS_TO_TICKS(10));
}

// =========================================================
// 3. RDZEŃ 1: WĄTEK USB HOST (FreeRTOS SMP)
// =========================================================
void setup1() {
  usbKbdInitQueue();
  usbKbdSetup1();
}

void loop1() {
  usbKbdLoop1();
}
