#include <Arduino.h>
#include <SD.h>
#include <PWMAudio.h>
#include "src/os/app_types.h"
#if HSOS_HAS_WIRELESS
  #include <BluetoothAudio.h>
  #include <lwip_wrap.h>
  #include <pico/btstack_run_loop_async_context.h>
#endif
#include "MP3DecoderHelix.h"
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include "storage_cfg.h"
#include "bt_audio.h"

using namespace libhelix;

// =========================================================
// Piny gniazda słuchawkowego PWM Stereo + Jack Detect
// =========================================================
static const int PWM_AUDIO_L     = 6; // GP6 (parzysty pin, PWM3 A = Lewy)
static const int PWM_AUDIO_R     = 7; // GP7 (PWM3 B = Prawy)
static const int JACK_DETECT_PIN = 8; // GP8 (INPUT_PULLUP, LOW = wpięty wtyk 3.5 mm)

#if HSOS_HAS_WIRELESS
// =========================================================
// Naprawiony BTstack Run-Loop dla FreeRTOS SMP (bez burzy alarmów
// i bez rekurencji stosu LWIP przy 5 ms timerze A2DP)
// =========================================================
extern "C" bool __isLWIPThread();

static SemaphoreHandle_t _btRunLoopExitBinary = nullptr;
static __callback_req _btKickBuffer;
static __callback_req _btTimeoutIRQBuffer;
static __callback_req _btIrqKickBuffer;
static bool _btKickPending = false;
static volatile bool _btInWork = false;
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

static A2DPSource a2dp;
static bool btInitialized = false;
static int16_t btStage[256];       // Ściśle 512 B (256 int16_t) -> omija błąd zawijania w A2DPSource::write()
static size_t  btStageSamples = 0;
static int16_t btSilence512[256] = {0};
static int16_t resPrevL = 0;
static int16_t resPrevR = 0;
static uint32_t resPhaseQ16 = 0;
#endif // HSOS_HAS_WIRELESS

// =========================================================
// Wyjście PWM Stereo (Headphone Jack) + wspólny stan audio
// =========================================================
static PWMAudio pwmAudio(PWM_AUDIO_L, true);
static bool pwmRunning = false;
static int  pwmRate = 44100;

struct AudioCmd {
  AudioCmdType type;
  int arg;
};

static QueueHandle_t audioCmdQueue = nullptr;
static SemaphoreHandle_t stateMutex = nullptr;
static volatile bool audioDirty = true;
static volatile bool stopDecodeFlag = false;
static volatile uint8_t activeVolumePct = 75;

static AudioUiSnapshot sharedState;
static char trackNames[MAX_TRACKS][TRACK_NAME_LEN];
static char requestedPath[128] = {0};

static File audioFile;
static uint8_t mp3Buf[2048];
static AudioOutMode currentOutMode = AOUT_MODE_AUTO;
static AudioActiveSink currentSink = ASINK_NONE;
static bool debouncedJack = false;
static bool lastRawJack = false;
static unsigned long jackDebounceMs = 0;

// Postęp odtwarzania
static uint32_t playedFramesTotal = 0;
static int      currentTrackRate  = 44100;
static int      currentTrackKbps  = 0;
static uint32_t currentFileSize   = 0;
static uint32_t currentFilePos    = 0;

static void markDirty() { __atomic_store_n(&audioDirty, true, __ATOMIC_RELEASE); }

static uint16_t calcVolumeQ15(uint8_t volPct) {
  if (volPct == 0) return 0;
  if (volPct >= 100) return 32768;
  uint32_t v = volPct;
  uint32_t q15 = (v * v * 32768UL) / 10000UL;
  if (q15 == 0) q15 = 128;
  return (uint16_t)min((uint32_t)32768, q15);
}

static inline int16_t applyVolumeQ15(int32_t s, uint16_t q15) {
  int32_t out = (s * (int32_t)q15) >> 15;
  if (out > 32767) return 32767;
  if (out < -32768) return -32768;
  return (int16_t)out;
}

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

static void pwmStopOutput() {
  if (pwmRunning) {
    pwmAudio.end();
    pwmRunning = false;
  }
  pinMode(PWM_AUDIO_L, INPUT);
  pinMode(PWM_AUDIO_R, INPUT);
}

static bool pwmEnsureStarted(int sampleRate) {
  if (sampleRate < 8000 || sampleRate > 48000) sampleRate = 44100;
  if (!pwmRunning) {
    pwmAudio.setPin(PWM_AUDIO_L);
    pwmAudio.setStereo(true);
    pwmAudio.setBuffers(12, 256); // 3072 próbki stereo (~70 ms przy 44.1 kHz)
    pwmAudio.setPWMFrequency(62500);
    pwmAudio.setFrequency(sampleRate);
    pwmRunning = pwmAudio.begin();
    pwmRate = sampleRate;
  } else if (pwmRate != sampleRate) {
    pwmAudio.setFrequency(sampleRate);
    pwmRate = sampleRate;
  }
  return pwmRunning;
}

static bool sinkReadyForAudio() {
  if (currentSink == ASINK_JACK) return true;
#if HSOS_HAS_WIRELESS
  if (currentSink == ASINK_BT) return btInitialized && a2dp.connected();
#endif
  return false;
}

static bool sinkNeedsData() {
  if (currentSink == ASINK_JACK) {
    return !pwmRunning || (pwmAudio.availableForWrite() >= 1024);
  }
#if HSOS_HAS_WIRELESS
  if (currentSink == ASINK_BT && btInitialized && a2dp.connected()) {
    return a2dp.availableForWrite() >= 2048;
  }
#endif
  return false;
}

#if HSOS_HAS_WIRELESS
static void pushBtStereo(int16_t l, int16_t r) {
  btStage[btStageSamples++] = l;
  btStage[btStageSamples++] = r;
  if (btStageSamples < 256) return;

  while (!stopDecodeFlag && btInitialized && a2dp.connected() && currentSink == ASINK_BT) {
    if (a2dp.availableForWrite() >= 512) {
      a2dp.write((const uint8_t *)btStage, 512);
      btStageSamples = 0;
      return;
    }
    if (uxQueueMessagesWaiting(audioCmdQueue) > 0) return;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  btStageSamples = 0;
}
#endif

static void writeStereoToPwm(const int16_t *pcm, size_t stereoFrames, int sampleRate, uint16_t gainQ15) {
  if (!pwmEnsureStarted(sampleRate)) return;
  size_t idx = 0;
  while (idx < stereoFrames && !stopDecodeFlag && currentSink == ASINK_JACK) {
    int availBytes = pwmAudio.availableForWrite();
    size_t availFrames = (availBytes >= 4) ? (size_t)(availBytes / 4) : 0;
    if (availFrames > 0) {
      size_t n = min(availFrames, stereoFrames - idx);
      for (size_t i = 0; i < n; i++) {
        int16_t l = applyVolumeQ15(pcm[(idx + i) * 2], gainQ15);
        int16_t r = applyVolumeQ15(pcm[(idx + i) * 2 + 1], gainQ15);
        pwmAudio.write(l, false);
        pwmAudio.write(r, false);
      }
      idx += n;
    } else {
      if (uxQueueMessagesWaiting(audioCmdQueue) > 0) return;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

static void writeMonoToPwm(const int16_t *pcmMono, size_t frames, int sampleRate, uint16_t gainQ15) {
  if (!pwmEnsureStarted(sampleRate)) return;
  size_t idx = 0;
  while (idx < frames && !stopDecodeFlag && currentSink == ASINK_JACK) {
    int availBytes = pwmAudio.availableForWrite();
    size_t availFrames = (availBytes >= 4) ? (size_t)(availBytes / 4) : 0;
    if (availFrames > 0) {
      size_t n = min(availFrames, frames - idx);
      for (size_t i = 0; i < n; i++) {
        int16_t s = applyVolumeQ15(pcmMono[idx + i], gainQ15);
        pwmAudio.write(s, false);
        pwmAudio.write(s, false);
      }
      idx += n;
    } else {
      if (uxQueueMessagesWaiting(audioCmdQueue) > 0) return;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

static void pcmCallback(MP3FrameInfo &info, short *pcm_buffer, size_t len, void *ref) {
  (void)ref;
  if (stopDecodeFlag || len == 0 || info.samprate <= 0) return;

  size_t frames = (info.nChans == 2) ? (len / 2) : len;
  playedFramesTotal += (uint32_t)frames;
  currentTrackRate = info.samprate;
  if (info.bitrate > 0) currentTrackKbps = info.bitrate / 1000;

  uint16_t gainQ15 = calcVolumeQ15(activeVolumePct);

  if (currentSink == ASINK_JACK) {
    if (info.nChans == 2) {
      writeStereoToPwm((const int16_t *)pcm_buffer, frames, info.samprate, gainQ15);
    } else {
      writeMonoToPwm((const int16_t *)pcm_buffer, frames, info.samprate, gainQ15);
    }
    return;
  }

#if HSOS_HAS_WIRELESS
  if (currentSink == ASINK_BT && btInitialized && a2dp.connected()) {
    if (info.samprate == 44100) {
      if (info.nChans == 2) {
        for (size_t i = 0; i < frames && !stopDecodeFlag && currentSink == ASINK_BT; i++) {
          int16_t l = applyVolumeQ15(pcm_buffer[i * 2], gainQ15);
          int16_t r = applyVolumeQ15(pcm_buffer[i * 2 + 1], gainQ15);
          pushBtStereo(l, r);
        }
      } else {
        for (size_t i = 0; i < frames && !stopDecodeFlag && currentSink == ASINK_BT; i++) {
          int16_t s = applyVolumeQ15(pcm_buffer[i], gainQ15);
          pushBtStereo(s, s);
        }
      }
    } else {
      // Prosty resampler liniowy do 44100 Hz wymaganego przez A2DP SBC
      uint32_t stepQ16 = (uint32_t)(((uint64_t)info.samprate << 16) / 44100ULL);
      if (stepQ16 == 0) stepQ16 = 65536UL;
      for (size_t i = 0; i < frames && !stopDecodeFlag && currentSink == ASINK_BT; i++) {
        int16_t curL = applyVolumeQ15(info.nChans == 2 ? pcm_buffer[i * 2] : pcm_buffer[i], gainQ15);
        int16_t curR = applyVolumeQ15(info.nChans == 2 ? pcm_buffer[i * 2 + 1] : pcm_buffer[i], gainQ15);
        while (resPhaseQ16 < 65536UL && !stopDecodeFlag && currentSink == ASINK_BT) {
          int32_t frac = (int32_t)(resPhaseQ16 & 0xFFFFUL);
          int16_t outL = (int16_t)(resPrevL + (((int32_t)(curL - resPrevL) * frac) >> 16));
          int16_t outR = (int16_t)(resPrevR + (((int32_t)(curR - resPrevR) * frac) >> 16));
          pushBtStereo(outL, outR);
          resPhaseQ16 += stepQ16;
        }
        resPhaseQ16 -= 65536UL;
        resPrevL = curL;
        resPrevR = curR;
      }
    }
  }
#endif
}

static MP3DecoderHelix mp3dec(pcmCallback);

#if HSOS_HAS_WIRELESS
static void btEnsureInit() {
  if (!btInitialized) {
    setBtStatus("Init BT...");
    Serial.printf("[BT] Init start, freeHeap=%lu\n", (unsigned long)rp2040.getFreeHeap());
    a2dp.setName("HollowStar OS 00:00:00:00:00:00");
    a2dp.setFrequency(44100);
    a2dp.setBufferSize(12288); // 24 KB (~139 ms bufora anty-przycięciowego)
    a2dp.begin();
    btInitialized = true;

    unsigned long t0 = millis();
    while (hci_get_state() != HCI_STATE_WORKING && (millis() - t0 < 2500UL)) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    Serial.printf("[BT] Init done, freeHeap=%lu\n", (unsigned long)rp2040.getFreeHeap());
  }
}
#endif

static AudioActiveSink computeTargetSink(bool jackNow, AudioOutMode mode) {
#if HSOS_HAS_WIRELESS
  if (mode == AOUT_MODE_JACK) return ASINK_JACK;
  if (mode == AOUT_MODE_BT)   return ASINK_BT;
  return jackNow ? ASINK_JACK : ASINK_BT;
#else
  if (mode == AOUT_MODE_JACK) return ASINK_JACK;
  return jackNow ? ASINK_JACK : ASINK_NONE;
#endif
}

static void syncAudioRouting(bool forceApply = false) {
  AudioActiveSink nextSink = computeTargetSink(debouncedJack, currentOutMode);
  if (!forceApply && nextSink == currentSink) return;

  AudioActiveSink prevSink = currentSink;
  currentSink = nextSink;

#if HSOS_HAS_WIRELESS
  bool jackBlocksBt = (currentSink == ASINK_JACK);
  if (jackBlocksBt) {
    if (btInitialized) {
      if (a2dp.connected()) {
        a2dp.disconnect();
        vTaskDelay(pdMS_TO_TICKS(150));
      }
      {
        BluetoothLock btLock;
        gap_discoverable_control(0);
        gap_connectable_control(0);
      }
    }
    btStageSamples = 0;
  } else {
    pwmStopOutput();
    if (btInitialized) {
      BluetoothLock btLock;
      gap_discoverable_control(1);
      gap_connectable_control(1);
    }
  }
#else
  if (currentSink != ASINK_JACK) {
    pwmStopOutput();
  }
#endif

  if (prevSink == ASINK_JACK && currentSink != ASINK_JACK) {
    // Wyjęto wtyk słuchawkowy podczas gry -> wstrzymaj odtwarzanie jeśli brak BT
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (sharedState.musicPlaying && !sinkReadyForAudio()) {
      sharedState.musicPaused = true;
    }
    xSemaphoreGive(stateMutex);
    pwmStopOutput();
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.jackConnected = debouncedJack;
  sharedState.outMode = currentOutMode;
  sharedState.activeSink = currentSink;
#if HSOS_HAS_WIRELESS
  sharedState.btDisabledByJack = (currentSink == ASINK_JACK);
  if (currentSink == ASINK_JACK) {
    sharedState.btConnected = false;
    sharedState.btScanning = false;
    strncpy(sharedState.btStatus, "Disabled (Jack)", sizeof(sharedState.btStatus) - 1);
  } else {
    bool c = btInitialized && a2dp.connected();
    sharedState.btConnected = c;
    strncpy(sharedState.btStatus, c ? "Connected" : (btInitialized ? "Disconnected" : "Off"),
            sizeof(sharedState.btStatus) - 1);
  }
#else
  sharedState.btDisabledByJack = true;
  sharedState.btConnected = false;
  strncpy(sharedState.btStatus, "No BT (Pico)", sizeof(sharedState.btStatus) - 1);
#endif
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalMusicStop() {
  stopDecodeFlag = true;
  pwmStopOutput();
#if HSOS_HAS_WIRELESS
  btStageSamples = 0;
#endif
  fsLock();
  if (audioFile) audioFile.close();
  fsUnlock();

  mp3dec.end();
  playedFramesTotal = 0;
  currentTrackRate = 44100;
  currentTrackKbps = 0;
  currentFileSize = 0;
  currentFilePos = 0;

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.musicPlaying = false;
  sharedState.musicPaused = false;
  sharedState.playingIdx = -1;
  sharedState.playingName[0] = '\0';
  sharedState.filePos = 0;
  sharedState.fileSize = 0;
  sharedState.bitrateKbps = 0;
  sharedState.elapsedSec = 0;
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalScanSD(bool forceRemount) {
  internalMusicStop();
  bool ok = (sdMounted && !forceRemount) ? true : storageMountSD();

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
  Dir dir = SDFS.openDir("/");
  while (dir.next()) {
    const char* bn = baseNamePtr(dir.fileName().c_str());
    bool isDir = dir.isDirectory();

    char nameCopy[TRACK_NAME_LEN];
    strncpy(nameCopy, bn, sizeof(nameCopy) - 1);
    nameCopy[sizeof(nameCopy) - 1] = '\0';

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
  fsUnlock();

  Serial.printf("[SD] Znaleziono MP3: %d\n", sharedState.trackCount);
  markDirty();
}

static void internalOpenAndPlay(const char* fullPath, int listIdx) {
  stopDecodeFlag = true;

#if HSOS_HAS_WIRELESS
  if (currentSink == ASINK_BT) {
    btEnsureInit();
  }
  btStageSamples = 0;
  resPrevL = resPrevR = 0;
  resPhaseQ16 = 0;
#endif

  fsLock();
  if (audioFile) audioFile.close();
  audioFile = fsOpenReadLocked(true, fullPath);
  bool opened = (bool)audioFile && !audioFile.isDirectory();
  uint32_t fsize = opened ? (uint32_t)audioFile.size() : 0UL;
  uint32_t dataStart = 0;

  // Szybkie pominięcie nagłówka ID3v2 (okładki i metadane), aby odtwarzacz startował natychmiast
  if (opened && fsize >= 10) {
    uint8_t id3[10];
    int n = audioFile.read(id3, 10);
    if (n == 10 && id3[0] == 'I' && id3[1] == 'D' && id3[2] == '3') {
      uint32_t tagSz = ((uint32_t)(id3[6] & 0x7F) << 21) |
                       ((uint32_t)(id3[7] & 0x7F) << 14) |
                       ((uint32_t)(id3[8] & 0x7F) << 7)  |
                       ((uint32_t)(id3[9] & 0x7F));
      if (id3[5] & 0x10) tagSz += 10;
      dataStart = min(fsize, 10UL + tagSz);
      audioFile.seek(dataStart);
    } else {
      audioFile.seek(0);
    }
  }
  if (!opened && audioFile) audioFile.close();
  fsUnlock();

  mp3dec.end();
  playedFramesTotal = 0;
  currentTrackRate = 44100;
  currentTrackKbps = 0;
  currentFileSize = fsize;
  currentFilePos = dataStart;

  if (opened) {
    mp3dec.begin();
    stopDecodeFlag = false;
  } else {
    pwmStopOutput();
  }

  const char* bn = baseNamePtr(fullPath);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (opened) {
    sharedState.playingIdx = listIdx;
    strncpy(sharedState.playingName, bn, sizeof(sharedState.playingName) - 1);
    sharedState.playingName[sizeof(sharedState.playingName) - 1] = '\0';
    sharedState.musicPlaying = true;
    sharedState.musicPaused = false;
    sharedState.filePos = currentFilePos;
    sharedState.fileSize = currentFileSize;
    sharedState.sampleRate = 44100;
    sharedState.bitrateKbps = 0;
    sharedState.elapsedSec = 0;
  } else {
    sharedState.playingIdx = -1;
    sharedState.playingName[0] = '\0';
    sharedState.musicPlaying = false;
    sharedState.musicPaused = false;
    sharedState.filePos = 0;
    sharedState.fileSize = 0;
    sharedState.elapsedSec = 0;
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

#if HSOS_HAS_WIRELESS
static void internalBtScan() {
  if (currentSink == ASINK_JACK) {
    setBtStatus("Unplug jack!");
    return;
  }
  internalMusicStop();

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.btScanning = true;
  sharedState.btDevCount = 0;
  strncpy(sharedState.btStatus, "Init BT...", sizeof(sharedState.btStatus) - 1);
  xSemaphoreGive(stateMutex);
  markDirty();

  btEnsureInit();
  setBtStatus("Scanning 5s...");

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
  snprintf(sharedState.btStatus, sizeof(sharedState.btStatus), "Found: %d", sharedState.btDevCount);
  xSemaphoreGive(stateMutex);
  markDirty();
}

static void internalBtConnectIdx(int idx) {
  if (currentSink == ASINK_JACK) {
    setBtStatus("Unplug jack!");
    return;
  }

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
  snprintf(msg, sizeof(msg), "Conn: %.14s", targetName);
  setBtStatus(msg);

  bool ok = false;
  {
    BluetoothLock btLock;
    ok = a2dp.connect(targetAddr);
  }
  if (!ok) {
    setBtStatus("Connect error");
  }
}
#endif // HSOS_HAS_WIRELESS

static void taskAudioBT(void *pvParams) {
  (void)pvParams;

  pinMode(JACK_DETECT_PIN, INPUT_PULLUP);
  vTaskDelay(pdMS_TO_TICKS(20));
  bool initRaw = (digitalRead(JACK_DETECT_PIN) == LOW);
  debouncedJack = initRaw;
  lastRawJack = initRaw;
  jackDebounceMs = millis();

  activeVolumePct = (uint8_t)constrain(sysConfig.volume, 0, 100);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  sharedState.volume = activeVolumePct;
  xSemaphoreGive(stateMutex);

  syncAudioRouting(true);
  internalScanSD(false);

  bool lastConn = false;
  unsigned long lastUiTelemetryMs = 0;

  for (;;) {
    // 1. Obsługa detekcji gniazda słuchawkowego (debounce 60 ms)
    bool rawJack = (digitalRead(JACK_DETECT_PIN) == LOW);
    if (rawJack != lastRawJack) {
      lastRawJack = rawJack;
      jackDebounceMs = millis();
    } else if (rawJack != debouncedJack && (millis() - jackDebounceMs >= 60UL)) {
      debouncedJack = rawJack;
      Serial.printf("[AUDIO] Jack %s\n", debouncedJack ? "WPIETY (LOW)" : "WYJETY (HIGH)");
      syncAudioRouting(false);
    }

    // 2. Obsługa kolejki komend
    AudioCmd cmd;
    while (xQueueReceive(audioCmdQueue, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case ACMD_SCAN_SD:
          internalScanSD(true);
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
            if (sharedState.musicPaused) pwmStopOutput();
            markDirty();
          }
          xSemaphoreGive(stateMutex);
          break;
        case ACMD_STOP:
          internalMusicStop();
          break;
        case ACMD_NEXT_TRACK: {
          int nextIdx = -1;
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          if (sharedState.trackCount > 0) {
            nextIdx = (sharedState.playingIdx >= 0) ? ((sharedState.playingIdx + 1) % sharedState.trackCount) : 0;
          }
          xSemaphoreGive(stateMutex);
          if (nextIdx >= 0) internalPlayTrack(nextIdx);
          break;
        }
        case ACMD_PREV_TRACK: {
          int prevIdx = -1;
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          if (sharedState.trackCount > 0) {
            prevIdx = (sharedState.playingIdx >= 0)
                      ? ((sharedState.playingIdx + sharedState.trackCount - 1) % sharedState.trackCount)
                      : 0;
          }
          xSemaphoreGive(stateMutex);
          if (prevIdx >= 0) internalPlayTrack(prevIdx);
          break;
        }
        case ACMD_SET_VOLUME: {
          int v = constrain(cmd.arg, 0, 100);
          activeVolumePct = (uint8_t)v;
          sysConfig.volume = v;
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          sharedState.volume = (uint8_t)v;
          xSemaphoreGive(stateMutex);
          markDirty();
          break;
        }
        case ACMD_CYCLE_OUT_MODE: {
#if HSOS_HAS_WIRELESS
          currentOutMode = (AudioOutMode)(((int)currentOutMode + 1) % 3);
#else
          currentOutMode = (currentOutMode == AOUT_MODE_AUTO) ? AOUT_MODE_JACK : AOUT_MODE_AUTO;
#endif
          syncAudioRouting(true);
          break;
        }
        case ACMD_BT_SCAN:
#if HSOS_HAS_WIRELESS
          internalBtScan();
#endif
          break;
        case ACMD_BT_CONNECT_IDX:
#if HSOS_HAS_WIRELESS
          internalBtConnectIdx(cmd.arg);
#endif
          break;
        case ACMD_BT_DISCONNECT:
#if HSOS_HAS_WIRELESS
          if (currentSink == ASINK_BT) internalMusicStop();
          if (btInitialized) a2dp.disconnect();
          if (currentSink != ASINK_JACK) setBtStatus("Disconnected");
#endif
          break;
        case ACMD_BT_CLEAR_PAIRING:
#if HSOS_HAS_WIRELESS
          if (currentSink == ASINK_BT) internalMusicStop();
          if (btInitialized) a2dp.clearPairing();
          if (currentSink != ASINK_JACK) setBtStatus("Pairing cleared");
#endif
          break;
      }
    }

#if HSOS_HAS_WIRELESS
    bool nowConn = (currentSink == ASINK_BT) && btInitialized && a2dp.connected();
    if (nowConn != lastConn) {
      lastConn = nowConn;
      if (!nowConn && currentSink == ASINK_BT) {
        btStageSamples = 0;
      }
      xSemaphoreTake(stateMutex, portMAX_DELAY);
      sharedState.btConnected = nowConn;
      if (currentSink != ASINK_JACK) {
        strncpy(sharedState.btStatus, nowConn ? "Connected" : "Disconnected", sizeof(sharedState.btStatus) - 1);
        sharedState.btStatus[sizeof(sharedState.btStatus) - 1] = '\0';
      }
      xSemaphoreGive(stateMutex);
      markDirty();
    }
#else
    (void)lastConn;
#endif

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool playingNow = sharedState.musicPlaying;
    bool pausedNow  = sharedState.musicPaused;
    xSemaphoreGive(stateMutex);

    bool isStreaming = false;
    if (playingNow && !pausedNow && sinkReadyForAudio()) {
      isStreaming = true;
      for (int burst = 0; burst < 4 && !stopDecodeFlag; burst++) {
        if (!sinkNeedsData()) break;
        if (uxQueueMessagesWaiting(audioCmdQueue) > 0) break;

        fsLock();
        int len = audioFile ? audioFile.read(mp3Buf, sizeof(mp3Buf)) : -1;
        uint32_t pos = audioFile ? (uint32_t)audioFile.position() : currentFileSize;
        fsUnlock();

        if (len > 0) {
          currentFilePos = pos;
          int off = 0;
          while (off < len && !stopDecodeFlag && sinkReadyForAudio()) {
            size_t chunk = min((size_t)512, (size_t)(len - off));
            size_t consumed = mp3dec.write(mp3Buf + off, chunk);
            if (consumed == 0) {
              Serial.printf("[MP3] BLAD: mp3dec.write zwrocil 0! freeHeap=%lu\n",
                            (unsigned long)rp2040.getFreeHeap());
              internalMusicStop();
              break;
            }
            off += (int)consumed;
            if (uxQueueMessagesWaiting(audioCmdQueue) > 0) break;
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
          break;
        }
      }

      if (millis() - lastUiTelemetryMs >= 500UL) {
        lastUiTelemetryMs = millis();
        uint32_t sec = (currentTrackRate > 0) ? (playedFramesTotal / (uint32_t)currentTrackRate) : 0;
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        sharedState.filePos = currentFilePos;
        sharedState.fileSize = currentFileSize;
        sharedState.sampleRate = (uint16_t)currentTrackRate;
        sharedState.bitrateKbps = (uint16_t)currentTrackKbps;
        sharedState.elapsedSec = sec;
        xSemaphoreGive(stateMutex);
        markDirty();
      }
    }
#if HSOS_HAS_WIRELESS
    else if (currentSink == ASINK_BT && btInitialized && a2dp.connected()) {
      // Karmi A2DP wyrównaną ciszą 512B przy pauzie lub zatrzymaniu
      if (btStageSamples > 0) {
        while (btStageSamples < 256) btStage[btStageSamples++] = 0;
        if (a2dp.availableForWrite() >= 512) {
          a2dp.write((const uint8_t *)btStage, 512);
          btStageSamples = 0;
        }
      } else {
        int avail = a2dp.availableForWrite();
        if (avail >= 4096) {
          int bursts = 0;
          while (avail >= (int)sizeof(btSilence512) && bursts < 2) {
            size_t w = a2dp.write((const uint8_t *)btSilence512, sizeof(btSilence512));
            if (w < sizeof(btSilence512)) break;
            avail -= (int)w;
            bursts++;
          }
        }
      }
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(isStreaming ? 2 : 12));
  }
}

void btAudioStartTask() {
  memset(&sharedState, 0, sizeof(sharedState));
  memset(trackNames, 0, sizeof(trackNames));
#if HSOS_HAS_WIRELESS
  strncpy(sharedState.btStatus, "Off", sizeof(sharedState.btStatus) - 1);
#else
  strncpy(sharedState.btStatus, "No BT (Pico)", sizeof(sharedState.btStatus) - 1);
#endif
  sharedState.playingIdx = -1;
  sharedState.volume = (uint8_t)constrain(sysConfig.volume, 0, 100);
  sharedState.outMode = AOUT_MODE_AUTO;

  stateMutex = xSemaphoreCreateMutex();
  audioCmdQueue = xQueueCreate(16, sizeof(AudioCmd));

  // Wyższy priorytet (2), aby rysowanie LCD przez SW SPI (UI prio 1) nie powodowało zacięć audio
  xTaskCreateAffinitySet(taskAudioBT, "AudioBT", 2304, nullptr, 2, (1 << 0), nullptr);
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
