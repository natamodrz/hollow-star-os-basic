#pragma once
#include <Arduino.h>

static const int MAX_TRACKS     = 24;
static const int TRACK_NAME_LEN = 64;
static const int MAX_BT_DEVS    = 10;

struct BtDevEntry {
  char name[22];
  uint8_t addr[6];
  int8_t rssi;
};

struct AudioUiSnapshot {
  bool btConnected;
  bool btScanning;
  char btStatus[24];
  int btDevCount;
  BtDevEntry btDevs[MAX_BT_DEVS];

  bool sdReady;
  int trackCount;
  int playingIdx;
  char playingName[TRACK_NAME_LEN];
  bool musicPlaying;
  bool musicPaused;
};

enum AudioCmdType : uint8_t {
  ACMD_SCAN_SD,
  ACMD_PLAY_TRACK,
  ACMD_PLAY_PATH,
  ACMD_TOGGLE_PAUSE,
  ACMD_STOP,
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
