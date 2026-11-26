#pragma once
#include <Arduino.h>

enum AppState : uint8_t {
  APP_MENU,
  APP_TEMP,
  APP_CALC,
  APP_NOTES,
  APP_GAMES_MENU,
  APP_MUSIC,
  APP_SETTINGS_MENU,
  APP_FILES,
  APP_TOOLS_MENU,
  APP_FTP,
  APP_SUSPEND,
  // Gry
  APP_BRICK,
  APP_TETRIS,
  APP_RACING,
  // Ustawienia
  APP_SET_BT,
  APP_SET_WIFI,
  APP_SET_CONTRAST,
  APP_SET_TIME,
  APP_SET_FORMAT,
  APP_SET_ABOUT,
  APP_STATE_COUNT
};

enum ModalMode : uint8_t {
  MODAL_NONE,
  MODAL_WIFI_PASS,
  MODAL_NEW_MD,
  MODAL_CONFIRM_DEL,
  MODAL_CONFIRM_FMT
};

struct AppScreen {
  void (*onEnter)();
  bool (*onTick)(unsigned long now);
  bool (*onKey)(uint8_t k);
  void (*onDraw)();
};
