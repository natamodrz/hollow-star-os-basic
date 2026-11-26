#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include "storage_cfg.h"

static const int SD_SCK  = 10;
static const int SD_MOSI = 11;
static const int SD_MISO = 12;
static const int SD_CS   = 13;

SysConfig sysConfig;
volatile bool flashMounted = false;
volatile bool sdMounted    = false;

static SemaphoreHandle_t fsMutex = nullptr;

static void setDefaultConfig() {
  sysConfig.contrast = 20;
  sysConfig.wifiSsid[0] = '\0';
  sysConfig.wifiPass[0] = '\0';
  sysConfig.racingHighScore = 0;
  strncpy(sysConfig.ftpUser, "hollow", sizeof(sysConfig.ftpUser) - 1);
  sysConfig.ftpUser[sizeof(sysConfig.ftpUser) - 1] = '\0';
  strncpy(sysConfig.ftpPass, "hollow", sizeof(sysConfig.ftpPass) - 1);
  sysConfig.ftpPass[sizeof(sysConfig.ftpPass) - 1] = '\0';
  sysConfig.volume = 75;
}

void fsLock() {
  if (fsMutex != nullptr) xSemaphoreTake(fsMutex, portMAX_DELAY);
}

void fsUnlock() {
  if (fsMutex != nullptr) xSemaphoreGive(fsMutex);
}

File fsOpenReadLocked(bool onSD, const char* path) {
  if (onSD) return sdMounted ? SD.open(path, FILE_READ) : File();
  return flashMounted ? LittleFS.open(path, "r") : File();
}

File fsOpenWriteLocked(bool onSD, const char* path) {
  if (onSD) {
    if (!sdMounted) return File();
    if (SD.exists(path)) SD.remove(path);
    return SD.open(path, FILE_WRITE);
  }
  return flashMounted ? LittleFS.open(path, "w") : File();
}

bool fsExistsLocked(bool onSD, const char* path) {
  if (onSD) return sdMounted && SD.exists(path);
  return flashMounted && LittleFS.exists(path);
}

bool fsRemoveLocked(bool onSD, const char* path) {
  if (onSD) return sdMounted && SD.remove(path);
  return flashMounted && LittleFS.remove(path);
}

bool fsMkdirLocked(bool onSD, const char* path) {
  if (onSD) return sdMounted && SD.mkdir(path);
  return flashMounted && LittleFS.mkdir(path);
}

bool fsRmdirLocked(bool onSD, const char* path) {
  if (onSD) return sdMounted && SD.rmdir(path);
  return flashMounted && LittleFS.rmdir(path);
}

static bool spi1Configured = false;

bool storageMountSD() {
  fsLock();
  SD.end(false);
  sdMounted = false;

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  if (!spi1Configured) {
    SPI1.setSCK(SD_SCK);
    SPI1.setTX(SD_MOSI);
    SPI1.setRX(SD_MISO);
    SPI1.begin();
    spi1Configured = true;
  }
  gpio_pull_up(SD_MISO);

  // Wyślij min. 80 taktów zegara przy CS=HIGH, aby wybudzić kartę do trybu SPI
  digitalWrite(SD_CS, HIGH);
  SPI1.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 10; i++) SPI1.transfer(0xFF);
  SPI1.endTransaction();

  // Próba z malejącą prędkością zegara dla przewodów stykowych i modułu Catalex
  static const uint32_t speeds[] = {
    SPI_HALF_SPEED,     // 4 MHz
    SPI_QUARTER_SPEED,  // 2 MHz
    SPI_EIGHTH_SPEED,   // 1 MHz
    SD_SCK_HZ(400000)   // 400 kHz
  };
  for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    sdMounted = SD.begin(SD_CS, speeds[i], SPI1);
    if (sdMounted) {
      Serial.printf("[SD] Zamontowano OK (clk=%lu Hz)\n", (unsigned long)speeds[i]);
      break;
    }
    SD.end(false);
    delay(30);
  }
  if (!sdMounted) {
    Serial.println("[SD] Blad montowania karty SD!");
  }
  fsUnlock();
  return sdMounted;
}

bool storageLoadConfig() {
  setDefaultConfig();
  fsLock();
  if (!flashMounted || !LittleFS.exists("/config.cfg")) {
    fsUnlock();
    return false;
  }
  File f = LittleFS.open("/config.cfg", "r");
  if (!f) {
    fsUnlock();
    return false;
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);

    if (key == "contrast") {
      sysConfig.contrast = constrain(val.toInt(), 0, 63);
    } else if (key == "ssid") {
      strncpy(sysConfig.wifiSsid, val.c_str(), sizeof(sysConfig.wifiSsid) - 1);
      sysConfig.wifiSsid[sizeof(sysConfig.wifiSsid) - 1] = '\0';
    } else if (key == "pass") {
      strncpy(sysConfig.wifiPass, val.c_str(), sizeof(sysConfig.wifiPass) - 1);
      sysConfig.wifiPass[sizeof(sysConfig.wifiPass) - 1] = '\0';
    } else if (key == "racing_hi") {
      sysConfig.racingHighScore = max(0, (int)val.toInt());
    } else if (key == "ftp_user") {
      if (val.length() > 0) {
        strncpy(sysConfig.ftpUser, val.c_str(), sizeof(sysConfig.ftpUser) - 1);
        sysConfig.ftpUser[sizeof(sysConfig.ftpUser) - 1] = '\0';
      }
    } else if (key == "ftp_pass") {
      if (val.length() > 0) {
        strncpy(sysConfig.ftpPass, val.c_str(), sizeof(sysConfig.ftpPass) - 1);
        sysConfig.ftpPass[sizeof(sysConfig.ftpPass) - 1] = '\0';
      }
    } else if (key == "volume") {
      sysConfig.volume = constrain((int)val.toInt(), 0, 100);
    }
  }
  f.close();
  fsUnlock();
  return true;
}

bool storageSaveConfig() {
  fsLock();
  if (!flashMounted) {
    fsUnlock();
    return false;
  }
  File f = LittleFS.open("/config.cfg", "w");
  if (!f) {
    fsUnlock();
    return false;
  }
  f.printf("contrast=%d\n", sysConfig.contrast);
  f.printf("ssid=%s\n", sysConfig.wifiSsid);
  f.printf("pass=%s\n", sysConfig.wifiPass);
  f.printf("racing_hi=%d\n", sysConfig.racingHighScore);
  f.printf("ftp_user=%s\n", sysConfig.ftpUser);
  f.printf("ftp_pass=%s\n", sysConfig.ftpPass);
  f.printf("volume=%d\n", sysConfig.volume);
  f.close();
  fsUnlock();
  return true;
}

void storageInit() {
  if (fsMutex == nullptr) {
    fsMutex = xSemaphoreCreateMutex();
  }
  setDefaultConfig();

  fsLock();
  flashMounted = LittleFS.begin();
  if (!flashMounted) {
    // Pierwsze uruchomienie na czystym flashu -> utwórz LittleFS
    LittleFS.format();
    flashMounted = LittleFS.begin();
  }
  // Utwórz przykładowy plik Markdown jeśli brak notatek
  if (flashMounted && !LittleFS.exists("/start.md")) {
    File f = LittleFS.open("/start.md", "w");
    if (f) {
      f.print("# Hollow Star OS\n## Notatnik MD\n- **F2/Tab**: Podglad\n- **Ctrl+S/F3**: Zapis\n---\n> Gotowy do pracy!\n");
      f.close();
    }
  }
  fsUnlock();

  if (!storageLoadConfig()) {
    storageSaveConfig();
  }
  storageMountSD();
}

static String baseNameOf(const char* raw) {
  String s = raw ? raw : "";
  while (s.endsWith("/") && s.length() > 1) s.remove(s.length() - 1);
  int slash = s.lastIndexOf('/');
  if (slash >= 0) s = s.substring(slash + 1);
  return s;
}

int storageListDir(bool onSD, const char* dirPath, DirEntryInfo* out, int maxItems) {
  int count = 0;
  fsLock();

  if (onSD) {
    if (sdMounted) {
      Dir dir = SDFS.openDir(dirPath);
      while (count < maxItems && dir.next()) {
        String bn = baseNameOf(dir.fileName().c_str());
        if (bn.length() > 0 && bn != "." && bn != ".." && !bn.startsWith(".")) {
          strncpy(out[count].name, bn.c_str(), sizeof(out[count].name) - 1);
          out[count].name[sizeof(out[count].name) - 1] = '\0';
          out[count].isDir = dir.isDirectory();
          out[count].size  = dir.isDirectory() ? 0 : (uint32_t)dir.fileSize();
          count++;
        }
      }
    }
  } else {
    if (flashMounted) {
      Dir dir = LittleFS.openDir(dirPath);
      while (count < maxItems && dir.next()) {
        String bn = baseNameOf(dir.fileName().c_str());
        if (bn.length() > 0 && bn != "." && bn != "..") {
          strncpy(out[count].name, bn.c_str(), sizeof(out[count].name) - 1);
          out[count].name[sizeof(out[count].name) - 1] = '\0';
          out[count].isDir = dir.isDirectory();
          out[count].size  = dir.isDirectory() ? 0 : (uint32_t)dir.fileSize();
          count++;
        }
      }
    }
  }

  fsUnlock();

  // Proste sortowanie: najpierw foldery, potem alfabetycznie
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      bool swapIt = false;
      if (out[i].isDir != out[j].isDir) {
        swapIt = !out[i].isDir && out[j].isDir;
      } else if (strcasecmp(out[i].name, out[j].name) > 0) {
        swapIt = true;
      }
      if (swapIt) {
        DirEntryInfo tmp = out[i];
        out[i] = out[j];
        out[j] = tmp;
      }
    }
  }

  return count;
}

bool storageReadText(bool onSD, const char* path, String &out, size_t maxBytes) {
  out = "";
  fsLock();
  File f = fsOpenReadLocked(onSD, path);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    fsUnlock();
    return false;
  }
  while (f.available() && out.length() < maxBytes) {
    char c = (char)f.read();
    if (c != '\r') out += c;
  }
  f.close();
  fsUnlock();
  return true;
}

bool storageReadText(bool onSD, const char* path, char* out, size_t maxBytes) {
  if (!out || maxBytes == 0) return false;
  out[0] = '\0';
  fsLock();
  File f = fsOpenReadLocked(onSD, path);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    fsUnlock();
    return false;
  }
  size_t pos = 0;
  uint8_t tmp[64];
  while ((pos + 1) < maxBytes) {
    size_t want = min(sizeof(tmp), maxBytes - 1 - pos);
    int n = f.read(tmp, want);
    if (n <= 0) break;
    for (int i = 0; i < n && (pos + 1) < maxBytes; i++) {
      char c = (char)tmp[i];
      if (c != '\r') out[pos++] = c;
    }
  }
  out[pos] = '\0';
  f.close();
  fsUnlock();
  return true;
}

bool storageWriteText(bool onSD, const char* path, const String &content) {
  return storageWriteText(onSD, path, content.c_str());
}

bool storageWriteText(bool onSD, const char* path, const char* content) {
  fsLock();
  File f = fsOpenWriteLocked(onSD, path);
  if (!f) {
    fsUnlock();
    return false;
  }
  if (content) f.print(content);
  f.close();
  fsUnlock();
  return true;
}

bool storageDeletePath(bool onSD, const char* path, bool isDir) {
  fsLock();
  bool ok = isDir ? fsRmdirLocked(onSD, path) : fsRemoveLocked(onSD, path);
  fsUnlock();
  return ok;
}

bool storageExists(bool onSD, const char* path) {
  fsLock();
  bool ok = fsExistsLocked(onSD, path);
  fsUnlock();
  return ok;
}

bool storageGetFlashSpace(uint64_t &freeBytes, uint64_t &totalBytes) {
  freeBytes = 0;
  totalBytes = 0;
  fsLock();
  bool ok = false;
  if (flashMounted) {
    FSInfo info;
    if (LittleFS.info(info)) {
      totalBytes = info.totalBytes;
      freeBytes = (info.totalBytes > info.usedBytes) ? (info.totalBytes - info.usedBytes) : 0ULL;
      ok = true;
    }
  }
  fsUnlock();
  return ok;
}

bool storageGetSDSpace(uint64_t &freeBytes, uint64_t &totalBytes) {
  freeBytes = 0;
  totalBytes = 0;
  fsLock();
  bool ok = false;
  if (sdMounted) {
    FSInfo info;
    if (SDFS.info(info)) {
      totalBytes = info.totalBytes;
      freeBytes = (info.totalBytes > info.usedBytes) ? (info.totalBytes - info.usedBytes) : 0ULL;
      ok = true;
    }
  }
  fsUnlock();
  return ok;
}

bool storageFormatFlash() {
  fsLock();
  LittleFS.end();
  bool ok = LittleFS.format();
  flashMounted = LittleFS.begin();
  fsUnlock();

  if (flashMounted) {
    setDefaultConfig();
    storageSaveConfig();
  }
  return ok && flashMounted;
}

// Bezpieczne czyszczenie partycji FAT32 (zachowuje geometrię karty i zgodność z PC)
static void wipeDirSDLocked(const char* dirPath, int depth) {
  if (depth > 6) return;
  while (true) {
    File dir = SD.open(dirPath, FILE_READ);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    String targetName = "";
    bool targetIsDir = false;
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      String bn = baseNameOf(entry.name());
      if (bn.length() > 0 && bn != "." && bn != "..") {
        targetName = bn;
        targetIsDir = entry.isDirectory();
        entry.close();
        break;
      }
      entry.close();
    }
    dir.close();

    if (targetName.length() == 0) break;

    String full = String(dirPath);
    if (!full.endsWith("/")) full += "/";
    full += targetName;

    if (targetIsDir) {
      wipeDirSDLocked(full.c_str(), depth + 1);
      SD.rmdir(full.c_str());
    } else {
      SD.remove(full.c_str());
    }
  }
}

bool storageFormatSD() {
  if (!sdMounted && !storageMountSD()) {
    return false;
  }
  fsLock();
  if (!sdMounted) {
    fsUnlock();
    return false;
  }
  wipeDirSDLocked("/", 0);
  fsUnlock();
  return true;
}
