#pragma once
#include <Arduino.h>
#include <FreeRTOS.h>
#include <queue.h>

enum SpecialKey : uint8_t {
  KEY_TAB    = '\t',
  KEY_CTRL_S = 19,
  KEY_ESC    = 27,
  KEY_DEL    = 127,
  KEY_UP     = 128,
  KEY_DOWN   = 129,
  KEY_LEFT   = 130,
  KEY_RIGHT  = 131,
  KEY_F1     = 132,
  KEY_F2     = 133,
  KEY_F3     = 134
};

extern QueueHandle_t keyQueue;
extern volatile bool holdLeft;
extern volatile bool holdRight;
extern volatile bool holdUp;
extern volatile bool kbdConnected;
extern volatile bool kbdStatusDirty;

void usbKbdInitQueue();
void usbKbdSetup1();
void usbKbdLoop1();
