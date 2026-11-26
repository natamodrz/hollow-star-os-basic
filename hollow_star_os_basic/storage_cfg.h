#pragma once
#include <Arduino.h>
#include <FS.h>

static const int MAX_DIR_ENTRIES = 32;

struct SysConfig {
  int contrast;
  char wifiSsid[36];
  char wifiPass[64];
  int racingHighScore;
  char ftpUser[24];
  char ftpPass[24];
  int volume;
  uint8_t lang;      // 0 = English, 1 = Polski
  bool setupDone;
};

struct DirEntryInfo {
  char name[64];
  uint32_t size;
  bool isDir;
};

extern SysConfig sysConfig;
extern volatile bool flashMounted;
extern volatile bool sdMounted;

void storageInit();
void fsLock();
void fsUnlock();

bool storageMountSD();
bool storageLoadConfig();
bool storageSaveConfig();

// Niskopoziomowe helpery (wywoływać przy założonym fsLock!)
File fsOpenReadLocked(bool onSD, const char* path);
File fsOpenWriteLocked(bool onSD, const char* path);
bool fsExistsLocked(bool onSD, const char* path);
bool fsRemoveLocked(bool onSD, const char* path);
bool fsMkdirLocked(bool onSD, const char* path);
bool fsRmdirLocked(bool onSD, const char* path);

// Bezpieczne operacje wysokopoziomowe (same zakładają fsLock)
int  storageListDir(bool onSD, const char* dirPath, DirEntryInfo* out, int maxItems);
bool storageExists(bool onSD, const char* path);
bool storageReadText(bool onSD, const char* path, String &out, size_t maxBytes = 1024);
bool storageReadText(bool onSD, const char* path, char* out, size_t maxBytes);
bool storageWriteText(bool onSD, const char* path, const String &content);
bool storageWriteText(bool onSD, const char* path, const char* content);
bool storageDeletePath(bool onSD, const char* path, bool isDir);

// Wolne miejsce na nośnikach
bool storageGetFlashSpace(uint64_t &freeBytes, uint64_t &totalBytes);
bool storageGetSDSpace(uint64_t &freeBytes, uint64_t &totalBytes);

// Formatowanie
bool storageFormatFlash();
bool storageFormatSD();
