#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"
#include "usb_kbd.h"

#define HOST_PIN_DP 2
static Adafruit_USBH_Host USBHost;
static uint8_t const keycode2ascii[128][2] = { HID_KEYCODE_TO_ASCII };

static StaticQueue_t keyQueueStruct;
static uint8_t keyQueueStorage[32];
QueueHandle_t keyQueue = nullptr;
volatile bool holdLeft  = false;
volatile bool holdRight = false;
volatile bool holdUp    = false;
volatile bool kbdConnected   = false;
volatile bool kbdStatusDirty = true;

void usbKbdInitQueue() {
  taskENTER_CRITICAL();
  if (keyQueue == nullptr) {
    keyQueue = xQueueCreateStatic(32, sizeof(uint8_t), keyQueueStorage, &keyQueueStruct);
  }
  taskEXIT_CRITICAL();
}

static void pushKey(uint8_t k) {
  if (keyQueue != nullptr) {
    xQueueSend(keyQueue, &k, 0);
  }
}

static bool findKeyInReport(hid_keyboard_report_t const *report, uint8_t keycode) {
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keycode[i] == keycode) return true;
  }
  return false;
}

static void processKbdReport(hid_keyboard_report_t const *report) {
  static hid_keyboard_report_t prev_report = { 0, 0, { 0 } };

  holdLeft  = findKeyInReport(report, HID_KEY_ARROW_LEFT);
  holdRight = findKeyInReport(report, HID_KEY_ARROW_RIGHT);
  holdUp    = findKeyInReport(report, HID_KEY_ARROW_UP);

  bool isShift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
  bool isCtrl  = report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL  | KEYBOARD_MODIFIER_RIGHTCTRL);

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t kc = report->keycode[i];
    if (kc && !findKeyInReport(&prev_report, kc)) {
      if (isCtrl && kc == HID_KEY_S)      pushKey(KEY_CTRL_S);
      else if (kc == HID_KEY_ARROW_UP)    pushKey(KEY_UP);
      else if (kc == HID_KEY_ARROW_DOWN)  pushKey(KEY_DOWN);
      else if (kc == HID_KEY_ARROW_LEFT)  pushKey(KEY_LEFT);
      else if (kc == HID_KEY_ARROW_RIGHT) pushKey(KEY_RIGHT);
      else if (kc == HID_KEY_ESCAPE)      pushKey(KEY_ESC);
      else if (kc == HID_KEY_TAB)         pushKey(KEY_TAB);
      else if (kc == HID_KEY_F1)          pushKey(KEY_F1);
      else if (kc == HID_KEY_F2)          pushKey(KEY_F2);
      else if (kc == HID_KEY_F3)          pushKey(KEY_F3);
      else if (kc == HID_KEY_BACKSPACE)   pushKey('\b');
      else if (kc == HID_KEY_DELETE)      pushKey(KEY_DEL);
      else if (kc == HID_KEY_ENTER || kc == HID_KEY_KEYPAD_ENTER) pushKey('\n');
      else if (kc < 128) {
        uint8_t ch = keycode2ascii[kc][isShift ? 1 : 0];
        if (ch >= 32 && ch <= 126) pushKey(ch);
      }
    }
  }
  prev_report = *report;
}

extern "C" {
  void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
      kbdConnected = true;
      kbdStatusDirty = true;
      tuh_hid_receive_report(dev_addr, instance);
    }
  }

  void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
    kbdConnected = false;
    holdLeft = holdRight = holdUp = false;
    kbdStatusDirty = true;
  }

  void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    (void)len;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
      processKbdReport((hid_keyboard_report_t const *)report);
    }
    tuh_hid_receive_report(dev_addr, instance);
  }
}

void usbKbdSetup1() {
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  pio_cfg.pin_dp = HOST_PIN_DP; // GP2 = D+, GP3 = D-
  pio_cfg.tx_ch  = 11;          // Wysoki kanał DMA, aby uniknąć kolizji z CYW43/SPI1
  USBHost.configure_pio_usb(1, &pio_cfg);
  USBHost.begin(1);
}

void usbKbdLoop1() {
  USBHost.task();
  vTaskDelay(pdMS_TO_TICKS(1));
}
