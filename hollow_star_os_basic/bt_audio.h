#pragma once
#include <Arduino.h>

static const int MAX_TRACKS     = 24;
static const int TRACK_NAME_LEN = 64;
static const int MAX_BT_DEVS    = 10;

enum AudioOutMode : uint8_t {
  AOUT_MODE_AUTO = 0, // Jack (GP8 LOW) wyłącza BT i przełącza na PWM; GP8 HIGH -> BT (Pico W)
  AOUT_MODE_JACK = 1, // Wymuszone wyjście słuchawkowe PWM (GP6/GP7), BT wyłączone
  AOUT_MODE_BT   = 2  // Wymuszony Bluetooth A2DP (tylko Pico W)
};

enum AudioActiveSink : uint8_t {
  ASINK_NONE = 0,
  ASINK_JACK = 1,
  ASINK_BT   = 2
};

struct BtDevEntry {
  char name[22];
  uint8_t addr[6];
  int8_t rssi;
};

struct AudioUiSnapshot {
  bool btConnected;
  bool btScanning;
  bool btDisabledByJack;
  char btStatus[24];
  int btDevCount;
  BtDevEntry btDevs[MAX_BT_DEVS];

  bool jackConnected;
  AudioOutMode outMode;
  AudioActiveSink activeSink;
  uint8_t volume;

  bool sdReady;
  int trackCount;
  int playingIdx;
  char playingName[TRACK_NAME_LEN];
  bool musicPlaying;
  bool musicPaused;

  uint32_t filePos;
  uint32_t fileSize;
  uint16_t sampleRate;
  uint16_t bitrateKbps;
  uint32_t elapsedSec;
};

enum AudioCmdType : uint8_t {
  ACMD_SCAN_SD,
  ACMD_PLAY_TRACK,
  ACMD_PLAY_PATH,
  ACMD_TOGGLE_PAUSE,
  ACMD_STOP,
  ACMD_NEXT_TRACK,
  ACMD_PREV_TRACK,
  ACMD_SET_VOLUME,
  ACMD_CYCLE_OUT_MODE,
  ACMD_BT_SCAN,
  ACMD_BT_CONNECT_IDX,
  ACMD_BT_DISCONNECT,
  ACMD_BT_CLEAR_PAIRING
};

void btAudioStartTask();
void btAudioSendCmd(AudioCmdType type, int arg = 0);
void btAudioPlayPath(const char* sdPath);
void btAudioGetSnapshot(AudioUiSnapshot &out);
bool btAudioGetTrackName(int idx, char *out, size_t maxLen);
bool btAudioTakeDirty();
