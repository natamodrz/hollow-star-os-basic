#pragma once
#include <Arduino.h>

static const int MAX_WIFI_APS = 10;

struct WifiApEntry {
  char ssid[24];
  int8_t rssi;
};

struct NetUiSnapshot {
  bool wifiConnected;
  bool wifiBusy;
  int8_t wifiRssi;
  char wifiStatus[26];
  char ipStr[18];
  int apCount;
  WifiApEntry aps[MAX_WIFI_APS];

  bool ntpSynced;
  bool ntpTimeReady;
  int ntpHour;
  int ntpMin;

  bool ftpRunning;
  bool ftpUseSD;
  char ftpStatus[26];
};

enum NetCmdType : uint8_t {
  NCMD_WIFI_SCAN,
  NCMD_WIFI_CONNECT_SAVED,
  NCMD_WIFI_DISCONNECT,
  NCMD_NTP_SYNC,
  NCMD_FTP_START,   // arg: 0 = Flash (LittleFS), 1 = SD
  NCMD_FTP_STOP,
  NCMD_FTP_SET_DRIVE // arg: 0 = Flash (LittleFS), 1 = SD
};

void netFtpStartTask();
void netFtpSendCmd(NetCmdType type, int arg = 0);
void netFtpReloadCreds();
void netFtpGetSnapshot(NetUiSnapshot &out);
bool netFtpTakeDirty();
bool netFtpConsumeSyncedTime(int &hour, int &minute);
