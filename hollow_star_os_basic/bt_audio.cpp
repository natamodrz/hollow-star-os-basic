#include <Arduino.h>
#include <SD.h>
#include <BluetoothAudio.h>
#include "MP3DecoderHelix.h"
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <lwip_wrap.h>
#include <pico/btstack_run_loop_async_context.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "storage_cfg.h"
#include "bt_audio.h"

using namespace libhelix;

// =========================================================
// Naprawiony BTstack Run-Loop dla FreeRTOS SMP (bez burzy alarmów
// i bez rekurencji stosu LWIP przy 5 ms timerze A2DP)
// =========================================================
extern "C" bool __isLWIPThread();

static SemaphoreHandle_t _btRunLoopExitBinary = nullptr;
static __callback_req _btKickBuffer;
static __callback_req _btTimeoutIRQBuffer;
static __callback_req _btIrqKickBuffer;
static bool _btKickPending = false;       // chronione przez cyw43_thread_enter()
static volatile bool _btInWork = false;   // ustawiane wyłącznie na wątku LWIP
static volatile bool _btTimeoutQueued = false;
static volatile bool _btIrqKickQueued = false;
static volatile alarm_id_t _btTimeoutAlarm = -1;

static void do_btstack_work_pending(void *data);

static void do_btstack_timeout_pending(void *data) {
  uint32_t irq = save_and_disable_interrupts();
  _btTimeoutQueued = false;
  restore_interrupts(irq);
  do_btstack_work_pending(data);
}

static void do_btstack_irq_pending(void *data) {
  uint32_t irq = save_and_disable_interrupts();
  _btIrqKickQueued = false;
  restore_interrupts(irq);
  do_btstack_work_pending(data);
}

static int64_t cb_btstack_timeout_worker(alarm_id_t id, void *user_data) {
  (void)id;
  (void)user_data;
  uint32_t irq = save_and_disable_interrupts();
  _btTimeoutAlarm = -1;
  bool shouldQueue = !_btTimeoutQueued;
  _btTimeoutQueued = true;
  restore_interrupts(irq);
  if (shouldQueue) {
    lwip_callback(do_btstack_timeout_pending, NULL, &_btTimeoutIRQBuffer);
  }
  return 0;
}

static void do_btstack_work_pending(void *data) {
  (void)data;
  cyw43_thread_enter();
  if (_btInWork) {
    _btKickPending = true;
    cyw43_thread_exit();
    return;
  }
  _btInWork = true;

  int32_t nextMs = -1;
  for (int pass = 0; pass < 8; pass++) {
    _btKickPending = false;
    btstack_run_loop_base_poll_data_sources();
    btstack_run_loop_base_execute_callbacks();

    uint32_t now = to_ms_since_boot(get_absolute_time());
    btstack_run_loop_base_process_timers(now);

    if (_btKickPending) continue;
    now = to_ms_since_boot(get_absolute_time());
    nextMs = btstack_run_loop_base_get_time_until_timeout(now);
    if (nextMs != 0) break;
  }

  // Zawsze anuluj poprzedni alarm przed zaplanowaniem kolejnego
  uint32_t irq = save_and_disable_interrupts();
  if (_btTimeoutAlarm != -1) {
    cancel_alarm(_btTimeoutAlarm);
    _btTimeoutAlarm = -1;
  }
  restore_interrupts(irq);

  bool scheduleFollowup = false;
  if (nextMs > 0) {
    alarm_id_t aid = add_alarm_in_ms((uint32_t)nextMs, cb_btstack_timeout_worker, NULL, true);
    if (aid > 0) {
      irq = save_and_disable_interrupts();
      _btTimeoutAlarm = aid;
      restore_interrupts(irq);
    }
  } else if (_btKickPending || nextMs == 0) {
    _btKickPending = true;
    scheduleFollowup = true;
  }

  _btInWork = false;
  cyw43_thread_exit();

  if (scheduleFollowup) {
    lwip_callback(do_btstack_work_pending, NULL, &_btKickBuffer);
  }
}

static void kick_btstack_work() {
  if (__get_current_exception() > 0) {
    uint32_t irq = save_and_disable_interrupts();
    bool shouldQueue = !_btIrqKickQueued;
    _btIrqKickQueued = true;
    restore_interrupts(irq);
    if (shouldQueue) {
      lwip_callback(do_btstack_irq_pending, NULL, &_btIrqKickBuffer);
    }
    return;
  }
  if (__isLWIPThread()) {
    if (_btInWork) {
      _btKickPending = true;
    } else {
      do_btstack_work_pending(NULL);
    }
    return;
  }
  cyw43_thread_enter();
  bool queue = !_btKickPending;
  _btKickPending = true;
  cyw43_thread_exit();
  if (queue) {
    lwip_callback(do_btstack_work_pending, NULL, &_btKickBuffer);
  }
}

static void fixed_bt_rl_init(void) {
  if (_btRunLoopExitBinary == nullptr) {
    _btRunLoopExitBinary = xSemaphoreCreateBinary();
  }
  btstack_run_loop_base_init();
}

static void fixed_bt_rl_add_ds(btstack_data_source_t *ds) {
  cyw43_thread_enter();
  btstack_run_loop_base_add_data_source(ds);
  cyw43_thread_exit();
}

static bool fixed_bt_rl_remove_ds(btstack_data_source_t *ds) {
  cyw43_thread_enter();
  bool rc = btstack_run_loop_base_remove_data_source(ds);
  cyw43_thread_exit();
  return rc;
}

static void fixed_bt_rl_enable_ds(btstack_data_source_t *ds, uint16_t cb) {
  cyw43_thread_enter();
  btstack_run_loop_base_enable_data_source_callbacks(ds, cb);
  cyw43_thread_exit();
}

static void fixed_bt_rl_disable_ds(btstack_data_source_t *ds, uint16_t cb) {
  cyw43_thread_enter();
  btstack_run_loop_base_disable_data_source_callbacks(ds, cb);
  cyw43_thread_exit();
}

static void fixed_bt_rl_set_timer(btstack_timer_source_t *ts, uint32_t timeout_in_ms) {
  cyw43_thread_enter();
  ts->timeout = to_ms_since_boot(get_absolute_time()) + timeout_in_ms + 1;
  cyw43_thread_exit();
}

static void fixed_bt_rl_add_timer(btstack_timer_source_t *timer) {
  cyw43_thread_enter();
  btstack_run_loop_base_add_timer(timer);
  cyw43_thread_exit();
  kick_btstack_work();
}

static bool fixed_bt_rl_remove_timer(btstack_timer_source_t *timer) {
  cyw43_thread_enter();
  bool rc = btstack_run_loop_base_remove_timer(timer);
  cyw43_thread_exit();
  return rc;
}

static void fixed_bt_rl_dump_timer(void) {
  cyw43_thread_enter();
  btstack_run_loop_base_dump_timer();
  cyw43_thread_exit();
}

static uint32_t fixed_bt_rl_get_time_ms(void) {
  return to_ms_since_boot(get_absolute_time());
}

static void fixed_bt_rl_execute(void) {
  if (_btRunLoopExitBinary != nullptr) {
    xSemaphoreTake(_btRunLoopExitBinary, portMAX_DELAY);
  }
}

static void fixed_bt_rl_trigger_exit(void) {
  if (_btRunLoopExitBinary != nullptr) {
    xSemaphoreGive(_btRunLoopExitBinary);
  }
}

static void fixed_bt_rl_exec_main(btstack_context_callback_registration_t *reg) {
  cyw43_thread_enter();
  btstack_run_loop_base_add_callback(reg);
  cyw43_thread_exit();
  kick_btstack_work();
}

static void fixed_bt_rl_poll_irq(void) {
  kick_btstack_work();
}

static const btstack_run_loop_t fixed_btstack_run_loop = {
  &fixed_bt_rl_init,
  &fixed_bt_rl_add_ds,
  &fixed_bt_rl_remove_ds,
  &fixed_bt_rl_enable_ds,
  &fixed_bt_rl_disable_ds,
  &fixed_bt_rl_set_timer,
  &fixed_bt_rl_add_timer,
  &fixed_bt_rl_remove_timer,
  &fixed_bt_rl_execute,
  &fixed_bt_rl_dump_timer,
  &fixed_bt_rl_get_time_ms,
  &fixed_bt_rl_poll_irq,
  &fixed_bt_rl_exec_main,
  &fixed_bt_rl_trigger_exit,
};

extern "C" const btstack_run_loop_t *__wrap_btstack_run_loop_async_context_get_instance(async_context_t *freertos_native) {
  (void)freertos_native;
  return &fixed_btstack_run_loop;
}

// =========================================================
// Warstwa Audio A2DP + synchroniczny dekoder arduino-libhelix
// =========================================================
static A2DPSource a2dp;

struct AudioCmd {
  AudioCmdType type;
  int arg;
};

static QueueHandle_t audioCmdQueue = nullptr;
static SemaphoreHandle_t stateMutex = nullptr;
static volatile bool audioDirty = true;
static volatile bool stopDecodeFlag = false;

static AudioUiSnapshot sharedState;
static char trackNames[MAX_TRACKS][TRACK_NAME_LEN];
static char requestedPath[128] = {0};

static bool btInitialized = false;
static File audioFile;
static uint8_t mp3Buf[512];
static int16_t silenceBuf[128] = {0};

// Liczniki diagnostyczne (Serial Monitor 115200)
static volatile uint32_t dbgReadBytes = 0;
static volatile uint32_t dbgConsumed  = 0;
static volatile uint32_t dbgFrames    = 0;
static volatile uint32_t dbgPcmBytes  = 0;
static volatile int dbgSampleRate     = 0;
static volatile int dbgChannels       = 0;

static void markDirty() { __atomic_store_n(&audioDirty, true, __ATOMIC_RELEASE); }

static void setBtStatus(const char* txt) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  strncpy(sharedState.btStatus, txt, sizeof(sharedState.btStatus) - 1);
  sharedState.btStatus[sizeof(sharedState.btStatus) - 1] = '\0';
  xSemaphoreGive(stateMutex);
  markDirty();
}

static const char* baseNamePtr(const char* raw) {
  if (!raw || !raw[0]) return "";
  const char* slash = strrchr(raw, '/');
  return slash ? (slash + 1) : raw;
}

static bool endsWithMp3(const char* name) {
  size_t len = strlen(name);
  if (len < 4) return false;
  return strcasecmp(name + len - 4, ".mp3") == 0;
}

static void writePcmToA2DP(const int16_t *pcmStereo, size_t totalBytes) {
  const uint8_t *ptr = (const uint8_t *)pcmStereo;
  size_t sent = 0;

  while (sent < totalBytes) {
    if (stopDecodeFlag || !btInitialized || !a2dp.connected()) {
      return;
    }

    int avail = a2dp.availableForWrite();
    if (avail >= 4) {
      size_t remain = totalBytes - sent;
      size_t chunk = (size_t)avail;
      if (chunk > remain) chunk = remain;
      chunk &= ~((size_t)3); // pełne ramki stereo (4 bajty = L+R)

      if (chunk > 0) {
        size_t w = a2dp.write(ptr + sent, chunk);
        sent += w;
        dbgPcmBytes += w;
        if (w == 0) vTaskDelay(pdMS_TO_TICKS(2));
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    } else {
      if (uxQueueMessagesWaiting(audioCmdQueue) > 0) return;
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

// Callback dekodera Helix — skaluje w miejscu bez bufora 4.6 KB!
static void pcmCallback(MP3FrameInfo &info, short *pcm_buffer, size_t len, void *ref) {
  (void)ref;
  dbgFrames++;
  dbgSampleRate = info.samprate;
  dbgChannels   = info.nChans;

  if (stopDecodeFlag || !btInitialized || !a2dp.connected() || len == 0) return;

  if (info.nChans == 2) {
    for (size_t i = 0; i < len; i++) {
      pcm_buffer[i] = (short)(((int32_t)pcm_buffer[i] * 3) / 4);
    }
    writePcmToA2DP((const int16_t *)pcm_buffer, len * sizeof(int16_t));
  } else {
    // Mono -> stereo porcjami po 128 próbek (tylko 512 B na stosie)
    int16_t stereoChunk[256];
    size_t idx = 0;
    while (idx < len && !stopDecodeFlag) {
      size_t n = min((size_t)128, len - idx);
      for (size_t i = 0; i < n; i++) {
        int16_t s = (int16_t)(((int32_t)pcm_buffer[idx + i] * 3) / 4);
        stereoChunk[i * 2]     = s;
        stereoChunk[i * 2 + 1] = s;
      }
      writePcmToA2DP(stereoChunk, n * 2 * sizeof(int16_t));
      idx += n;
    }
  }
}

static MP3DecoderHelix mp3dec(pcmCallback);

static void btEnsureInit() {
  if (!btInitialized) {
    setBtStatus("Init BT...");
    Serial.printf("[BT] Init start, freeHeap=%lu\n", (unsigned long)rp2040.getFreeHeap());
    a2dp.setName("HollowStar OS 00:00:00:00:00:00");
    a2dp.setFrequency(44100);
    a2dp.begin();
    btInitialized = true;

    // Poczekaj aż stos HCI przejdzie w stan WORKING przed pierwszym wywołaniem connect/scan
    unsigned long t0 = millis();
    while (hci_get_state() != HCI_STATE_WORKING && (millis() - t0 < 2500UL)) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    Serial.printf("[BT] Init done, freeHeap=%lu\n", (unsigned long)rp2040.getFreeHeap());
  }
}

static void internalMusicStop() {
  stopDecodeFlag = true;
  fsLock();
  if (audioFile) audioFile.close();
  fsUnlock();

  mp3dec.end();

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.musicPlaying = false;
  sharedState.musicPaused = false;
  sharedState.playingIdx = -1;
  sharedState.playingName[0] = '\0';
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalScanSD() {
  internalMusicStop();
  bool ok = storageMountSD();

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.sdReady = ok;
  sharedState.trackCount = 0;
  xSemaphoreGive(stateMutex);

  if (!ok) {
    Serial.println("[SD] Blad montowania karty!");
    markDirty();
    return;
  }

  Serial.println("[SD] Skanowanie katalogu / :");
  fsLock();
  File dir = SD.open("/", FILE_READ);
  if (dir && dir.isDirectory()) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      const char* bn = baseNamePtr(entry.name());
      bool isDir = entry.isDirectory();
      unsigned long sz = isDir ? 0UL : (unsigned long)entry.size();

      char nameCopy[TRACK_NAME_LEN];
      strncpy(nameCopy, bn, sizeof(nameCopy) - 1);
      nameCopy[sizeof(nameCopy) - 1] = '\0';
      entry.close();

      Serial.printf("  - '%s' (%s, %lu B)\n", nameCopy, isDir ? "DIR" : "FILE", sz);

      if (!isDir && nameCopy[0] != '\0' && nameCopy[0] != '.' && endsWithMp3(nameCopy)) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (sharedState.trackCount < MAX_TRACKS) {
          strncpy(trackNames[sharedState.trackCount], nameCopy, TRACK_NAME_LEN - 1);
          trackNames[sharedState.trackCount][TRACK_NAME_LEN - 1] = '\0';
          sharedState.trackCount++;
        }
        xSemaphoreGive(stateMutex);
      }
    }
    dir.close();
  }
  fsUnlock();

  Serial.printf("[SD] Znaleziono MP3: %d\n", sharedState.trackCount);
  markDirty();
}

static void internalOpenAndPlay(const char* fullPath, int listIdx) {
  btEnsureInit();
  stopDecodeFlag = true;

  fsLock();
  if (audioFile) audioFile.close();
  audioFile = fsOpenReadLocked(true, fullPath);
  bool opened = (bool)audioFile && !audioFile.isDirectory();
  unsigned long fsize = opened ? (unsigned long)audioFile.size() : 0UL;
  if (!opened && audioFile) audioFile.close();
  fsUnlock();

  dbgReadBytes = dbgConsumed = dbgFrames = dbgPcmBytes = 0;
  dbgSampleRate = dbgChannels = 0;

  mp3dec.end();
  Serial.printf("[MP3] Open '%s' ok=%d size=%lu heapBefore=%lu\n",
                fullPath, opened ? 1 : 0, fsize, (unsigned long)rp2040.getFreeHeap());

  if (opened) {
    mp3dec.begin();
    Serial.printf("[MP3] Po mp3dec.begin() heapAfter=%lu\n", (unsigned long)rp2040.getFreeHeap());
    stopDecodeFlag = false;
  }

  const char* bn = baseNamePtr(fullPath);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (opened) {
    sharedState.playingIdx = listIdx;
    strncpy(sharedState.playingName, bn, sizeof(sharedState.playingName) - 1);
    sharedState.playingName[sizeof(sharedState.playingName) - 1] = '\0';
    sharedState.musicPlaying = true;
    sharedState.musicPaused = false;
  } else {
    sharedState.playingIdx = -1;
    sharedState.playingName[0] = '\0';
    sharedState.musicPlaying = false;
    sharedState.musicPaused = false;
  }
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalPlayTrack(int idx) {
  char path[TRACK_NAME_LEN + 2] = "/";
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool valid = sharedState.sdReady && idx >= 0 && idx < sharedState.trackCount;
  if (valid) {
    strncpy(path + 1, trackNames[idx], TRACK_NAME_LEN - 1);
    path[TRACK_NAME_LEN] = '\0';
  }
  xSemaphoreGive(stateMutex);
  if (!valid) return;

  internalOpenAndPlay(path, idx);
}

static void internalBtScan() {
  internalMusicStop();

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.btScanning = true;
  sharedState.btDevCount = 0;
  strncpy(sharedState.btStatus, "Init BT...", sizeof(sharedState.btStatus) - 1);
  xSemaphoreGive(stateMutex);
  markDirty();

  btEnsureInit();
  setBtStatus("Skanowanie 5s...");

  auto found = a2dp.scan(BluetoothHCI::any_cod, 5);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.btDevCount = 0;
  for (auto &dev : found) {
    if (sharedState.btDevCount >= MAX_BT_DEVS) break;
    BtDevEntry &e = sharedState.btDevs[sharedState.btDevCount];
    memcpy(e.addr, dev.address(), 6);
    e.rssi = (int8_t)dev.rssi();
    const char* n = dev.name();
    strncpy(e.name, (n && n[0]) ? n : dev.addressString(), sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = '\0';
    sharedState.btDevCount++;
  }
  sharedState.btScanning = false;
  snprintf(sharedState.btStatus, sizeof(sharedState.btStatus), "Znaleziono: %d", sharedState.btDevCount);
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalBtConnectIdx(int idx) {
  uint8_t targetAddr[6];
  char targetName[22] = {0};

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool valid = (idx >= 0 && idx < sharedState.btDevCount);
  if (valid) {
    memcpy(targetAddr, sharedState.btDevs[idx].addr, 6);
    strncpy(targetName, sharedState.btDevs[idx].name, sizeof(targetName) - 1);
  }
  xSemaphoreGive(stateMutex);
  if (!valid) return;

  btEnsureInit();
  if (a2dp.connected()) {
    a2dp.disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  char msg[24];
  snprintf(msg, sizeof(msg), "Lacze: %.14s", targetName);
  setBtStatus(msg);

  bool ok = false;
  {
    // A2DPSource::connect() nie zakłada BluetoothLock wewnątrz biblioteki
    BluetoothLock btLock;
    ok = a2dp.connect(targetAddr);
  }
  if (!ok) {
    setBtStatus("Blad polaczenia");
  }
}

static void taskAudioBT(void *pvParams) {
  (void)pvParams;
  internalScanSD();
  bool lastConn = false;
  unsigned long lastDbgMs = 0;

  for (;;) {
    AudioCmd cmd;
    while (xQueueReceive(audioCmdQueue, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case ACMD_SCAN_SD:
          internalScanSD();
          break;
        case ACMD_PLAY_TRACK:
          internalPlayTrack(cmd.arg);
          break;
        case ACMD_PLAY_PATH: {
          char pathCopy[128];
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          strncpy(pathCopy, requestedPath, sizeof(pathCopy) - 1);
          pathCopy[sizeof(pathCopy) - 1] = '\0';
          xSemaphoreGive(stateMutex);
          internalOpenAndPlay(pathCopy, -1);
          break;
        }
        case ACMD_TOGGLE_PAUSE:
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          if (sharedState.musicPlaying) {
            sharedState.musicPaused = !sharedState.musicPaused;
            markDirty();
          }
          xSemaphoreGive(stateMutex);
          break;
        case ACMD_STOP:
          internalMusicStop();
          break;
        case ACMD_BT_SCAN:
          internalBtScan();
          break;
        case ACMD_BT_CONNECT_IDX:
          internalBtConnectIdx(cmd.arg);
          break;
        case ACMD_BT_DISCONNECT:
          internalMusicStop();
          if (btInitialized) a2dp.disconnect();
          setBtStatus("Rozlaczono");
          break;
        case ACMD_BT_CLEAR_PAIRING:
          internalMusicStop();
          if (btInitialized) a2dp.clearPairing();
          setBtStatus("Pary usuniete");
          break;
      }
    }

    bool nowConn = btInitialized && a2dp.connected();
    if (nowConn != lastConn) {
      lastConn = nowConn;
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      sharedState.btConnected = nowConn;
      strncpy(sharedState.btStatus, nowConn ? "Polaczono!" : "Rozlaczono", sizeof(sharedState.btStatus) - 1);
      sharedState.btStatus[sizeof(sharedState.btStatus) - 1] = '\0';
      xSemaphoreGive(stateMutex);
      markDirty();
    }

    bool playingNow = false;
    bool pausedNow  = false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    playingNow = sharedState.musicPlaying;
    pausedNow  = sharedState.musicPaused;
    xSemaphoreGive(stateMutex);

    bool isStreaming = false;
    if (nowConn && playingNow && !pausedNow) {
      isStreaming = true;
      fsLock();
      int len = audioFile ? audioFile.read(mp3Buf, sizeof(mp3Buf)) : -1;
      fsUnlock();

      if (len > 0) {
        dbgReadBytes += (uint32_t)len;
        int off = 0;
        while (off < len && !stopDecodeFlag) {
          size_t consumed = mp3dec.write(mp3Buf + off, (size_t)(len - off));
          dbgConsumed += (uint32_t)consumed;
          if (consumed == 0) {
            Serial.printf("[MP3] BLAD: mp3dec.write zwrocil 0! freeHeap=%lu\n",
                          (unsigned long)rp2040.getFreeHeap());
            internalMusicStop();
            break;
          }
          off += (int)consumed;
        }
      } else {
        fsLock();
        if (audioFile) audioFile.close();
        fsUnlock();

        int nextIdx = -1;
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (sharedState.playingIdx >= 0 && sharedState.trackCount > 0) {
          nextIdx = (sharedState.playingIdx + 1) % sharedState.trackCount;
        }
        xSemaphoreGive(stateMutex);

        if (nextIdx >= 0) internalPlayTrack(nextIdx);
        else internalMusicStop();
      }

      if (millis() - lastDbgMs >= 1000UL) {
        lastDbgMs = millis();
        Serial.printf("[MP3] read=%lu cons=%lu frames=%lu pcmB=%lu %dHz %dch avail=%d heap=%lu\n",
                      (unsigned long)dbgReadBytes, (unsigned long)dbgConsumed,
                      (unsigned long)dbgFrames, (unsigned long)dbgPcmBytes,
                      dbgSampleRate, dbgChannels, a2dp.availableForWrite(),
                      (unsigned long)rp2040.getFreeHeap());
      }
    } else if (nowConn) {
      int avail = a2dp.availableForWrite();
      if (avail >= 2048) {
        int bursts = 0;
        while (avail >= (int)sizeof(silenceBuf) && bursts < 2) {
          size_t w = a2dp.write((const uint8_t *)silenceBuf, sizeof(silenceBuf));
          if (w == 0) break;
          avail -= (int)w;
          bursts++;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(isStreaming ? 1 : 10));
  }
}

void btAudioStartTask() {
  memset(&sharedState, 0, sizeof(sharedState));
  memset(trackNames, 0, sizeof(trackNames));
  strncpy(sharedState.btStatus, "Wylaczony", sizeof(sharedState.btStatus) - 1);
  sharedState.playingIdx = -1;

  stateMutex = xSemaphoreCreateMutex();
  audioCmdQueue = xQueueCreate(12, sizeof(AudioCmd));

  // Stos 2048 słów (8 KB) + Core 0
  xTaskCreateAffinitySet(taskAudioBT, "AudioBT", 2048, nullptr, 1, (1 << 0), nullptr);
}

void btAudioSendCmd(AudioCmdType type, int arg) {
  if (audioCmdQueue != nullptr) {
    AudioCmd cmd = { type, arg };
    xQueueSend(audioCmdQueue, &cmd, 0);
  }
}

void btAudioPlayPath(const char* sdPath) {
  if (stateMutex == nullptr) return;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  strncpy(requestedPath, sdPath, sizeof(requestedPath) - 1);
  requestedPath[sizeof(requestedPath) - 1] = '\0';
  xSemaphoreGive(stateMutex);
  btAudioSendCmd(ACMD_PLAY_PATH);
}

void btAudioGetSnapshot(AudioUiSnapshot &out) {
  if (stateMutex == nullptr) return;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  out = sharedState;
  xSemaphoreGive(stateMutex);
}

bool btAudioGetTrackName(int idx, char *out, size_t maxLen) {
  if (!out || maxLen == 0 || stateMutex == nullptr) return false;
  out[0] = '\0';
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  bool valid = (idx >= 0 && idx < sharedState.trackCount);
  if (valid) {
    strncpy(out, trackNames[idx], maxLen - 1);
    out[maxLen - 1] = '\0';
  }
  xSemaphoreGive(stateMutex);
  return valid;
}

bool btAudioTakeDirty() {
  return __atomic_exchange_n(&audioDirty, false, __ATOMIC_ACQ_REL);
}
