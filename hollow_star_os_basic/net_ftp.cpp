#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <new>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>
#include "storage_cfg.h"
#include "net_ftp.h"

static const uint16_t FTP_CTRL_PORT = 21;
static const uint16_t FTP_DATA_PORT = 50009; // 195 * 256 + 89

struct NetCmd {
  NetCmdType type;
  int arg;
};

static QueueHandle_t netCmdQueue = nullptr;
static SemaphoreHandle_t netMutex = nullptr;
static volatile bool netDirty = true;
static NetUiSnapshot netState;

// Obiekty serwera FTP (używane wyłącznie w TaskNet)
static WiFiServer ftpCtrlSrv(FTP_CTRL_PORT);
static WiFiServer ftpDataSrv(FTP_DATA_PORT);
static WiFiClient ftpCtrlCli;
static String ftpCmdLine = "";
static String ftpCwd = "/";
static bool ftpActive = false;
static bool ftpOnSD = false;
static uint8_t ftpBuf[512];

static void markNetDirty() { netDirty = true; }

static void setWifiStatus(const char* txt, bool busy = false) {
  xSemaphoreTake(netMutex, portMAX_DELAY);
  netState.wifiBusy = busy;
  strncpy(netState.wifiStatus, txt, sizeof(netState.wifiStatus) - 1);
  netState.wifiStatus[sizeof(netState.wifiStatus) - 1] = '\0';
  xSemaphoreGive(netMutex);
  markNetDirty();
}

static void setFtpStatus(const char* txt) {
  xSemaphoreTake(netMutex, portMAX_DELAY);
  strncpy(netState.ftpStatus, txt, sizeof(netState.ftpStatus) - 1);
  netState.ftpStatus[sizeof(netState.ftpStatus) - 1] = '\0';
  xSemaphoreGive(netMutex);
  markNetDirty();
}

static void internalNtpSync() {
  if (WiFi.status() != WL_CONNECTED) {
    setWifiStatus("Brak Wi-Fi dla NTP");
    return;
  }
  setWifiStatus("Sync NTP...", true);

  // Strefa czasowa Polska (CET/CEST z automatyczną zmianą czasu)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  NTP.begin("pool.ntp.org", "time.nist.gov");

  unsigned long t0 = millis();
  time_t now = time(nullptr);
  while (now < 1700000000UL && (millis() - t0 < 6000UL)) {
    vTaskDelay(pdMS_TO_TICKS(200));
    now = time(nullptr);
  }

  if (now >= 1700000000UL) {
    struct tm ti;
    localtime_r(&now, &ti);
    xSemaphoreTake(netMutex, portMAX_DELAY);
    netState.ntpSynced = true;
    netState.ntpHour = ti.tm_hour;
    netState.ntpMin  = ti.tm_min;
    netState.ntpTimeReady = true;
    netState.wifiBusy = false;
    snprintf(netState.wifiStatus, sizeof(netState.wifiStatus), "NTP OK %02d:%02d", ti.tm_hour, ti.tm_min);
    xSemaphoreGive(netMutex);
    markNetDirty();
  } else {
    setWifiStatus("Blad NTP (timeout)");
  }
}

static void internalWifiConnect() {
  if (sysConfig.wifiSsid[0] == '\0') {
    setWifiStatus("Brak zapisanej sieci");
    return;
  }
  setWifiStatus("Lacze z Wi-Fi...", true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(sysConfig.wifiSsid, sysConfig.wifiPass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 12000UL)) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    int8_t rssi = (int8_t)constrain(WiFi.RSSI(), -127, 0);
    xSemaphoreTake(netMutex, portMAX_DELAY);
    netState.wifiConnected = true;
    netState.wifiBusy = false;
    netState.wifiRssi = rssi;
    strncpy(netState.ipStr, ip.c_str(), sizeof(netState.ipStr) - 1);
    snprintf(netState.wifiStatus, sizeof(netState.wifiStatus), "IP: %.18s", ip.c_str());
    xSemaphoreGive(netMutex);
    markNetDirty();

    // Automatyczna synchronizacja czasu po połączeniu
    internalNtpSync();
  } else {
    xSemaphoreTake(netMutex, portMAX_DELAY);
    netState.wifiConnected = false;
    netState.wifiRssi = -127;
    netState.ipStr[0] = '\0';
    xSemaphoreGive(netMutex);
    setWifiStatus("Nie polaczono");
  }
}

static void internalWifiScan() {
  setWifiStatus("Skanuje Wi-Fi...", true);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();

  xSemaphoreTake(netMutex, portMAX_DELAY);
  netState.apCount = 0;
  for (int i = 0; i < n && netState.apCount < MAX_WIFI_APS; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    strncpy(netState.aps[netState.apCount].ssid, s.c_str(), sizeof(netState.aps[0].ssid) - 1);
    netState.aps[netState.apCount].ssid[sizeof(netState.aps[0].ssid) - 1] = '\0';
    netState.aps[netState.apCount].rssi = (int8_t)WiFi.RSSI(i);
    netState.apCount++;
  }
  netState.wifiBusy = false;
  snprintf(netState.wifiStatus, sizeof(netState.wifiStatus), "Sieci: %d", netState.apCount);
  xSemaphoreGive(netMutex);

  WiFi.scanDelete();
  markNetDirty();
}

// --- Lekki serwer FTP (PASV, RFC 959) ---
static String ftpResolvePath(const String &arg) {
  String p = arg;
  p.trim();
  if (p.length() == 0) return ftpCwd;
  String full;
  if (p.startsWith("/")) full = p;
  else {
    full = ftpCwd;
    if (!full.endsWith("/")) full += "/";
    full += p;
  }
  while (full.length() > 1 && full.endsWith("/")) full.remove(full.length() - 1);
  return full;
}

static WiFiClient ftpAcceptData() {
  unsigned long t0 = millis();
  while (millis() - t0 < 2500UL) {
    WiFiClient dc = ftpDataSrv.accept();
    if (dc) return dc;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return WiFiClient();
}

static void ftpStart(bool useSD) {
  if (WiFi.status() != WL_CONNECTED) {
    setFtpStatus("Najpierw polacz Wi-Fi");
    return;
  }
  if (useSD && !sdMounted) storageMountSD();
  ftpOnSD = useSD;
  ftpCwd = "/";
  ftpCmdLine = "";
  ftpCtrlSrv.begin();
  ftpDataSrv.begin();
  ftpActive = true;

  xSemaphoreTake(netMutex, portMAX_DELAY);
  netState.ftpRunning = true;
  netState.ftpUseSD = useSD;
  strncpy(netState.ftpStatus, "Dziala (port 21)", sizeof(netState.ftpStatus) - 1);
  xSemaphoreGive(netMutex);
  markNetDirty();
}

static void ftpStop() {
  if (ftpCtrlCli) ftpCtrlCli.stop();
  ftpActive = false;

  xSemaphoreTake(netMutex, portMAX_DELAY);
  netState.ftpRunning = false;
  strncpy(netState.ftpStatus, "Zatrzymany", sizeof(netState.ftpStatus) - 1);
  xSemaphoreGive(netMutex);
  markNetDirty();
}

static void ftpHandleCommand(const String &raw) {
  int sp = raw.indexOf(' ');
  String cmd = (sp > 0) ? raw.substring(0, sp) : raw;
  String arg = (sp > 0) ? raw.substring(sp + 1) : "";
  cmd.toUpperCase();
  arg.trim();

  if (cmd == "USER") {
    ftpCtrlCli.print("331 Podaj haslo\r\n");
  } else if (cmd == "PASS") {
    ftpCtrlCli.print("230 Zalogowano\r\n");
  } else if (cmd == "SYST") {
    ftpCtrlCli.print("215 UNIX Type: L8\r\n");
  } else if (cmd == "FEAT") {
    ftpCtrlCli.print("211-Features:\r\n PASV\r\n SIZE\r\n211 End\r\n");
  } else if (cmd == "TYPE" || cmd == "OPTS" || cmd == "NOOP") {
    ftpCtrlCli.print("200 OK\r\n");
  } else if (cmd == "PWD" || cmd == "XPWD") {
    ftpCtrlCli.printf("257 \"%s\" is current directory\r\n", ftpCwd.c_str());
  } else if (cmd == "CDUP") {
    int slash = ftpCwd.lastIndexOf('/');
    ftpCwd = (slash > 0) ? ftpCwd.substring(0, slash) : "/";
    ftpCtrlCli.print("250 OK\r\n");
  } else if (cmd == "CWD") {
    if (arg == "..") {
      int slash = ftpCwd.lastIndexOf('/');
      ftpCwd = (slash > 0) ? ftpCwd.substring(0, slash) : "/";
      ftpCtrlCli.print("250 OK\r\n");
    } else {
      String np = ftpResolvePath(arg);
      bool ok = (np == "/");
      if (!ok) {
        fsLock();
        File f = fsOpenReadLocked(ftpOnSD, np.c_str());
        if (f && f.isDirectory()) ok = true;
        if (f) f.close();
        fsUnlock();
      }
      if (ok) { ftpCwd = np; ftpCtrlCli.print("250 OK\r\n"); }
      else    { ftpCtrlCli.print("550 Brak katalogu\r\n"); }
    }
  } else if (cmd == "PASV") {
    IPAddress ip = WiFi.localIP();
    ftpCtrlCli.printf("227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n",
                      ip[0], ip[1], ip[2], ip[3], FTP_DATA_PORT / 256, FTP_DATA_PORT % 256);
  } else if (cmd == "LIST" || cmd == "NLST") {
    WiFiClient dc = ftpAcceptData();
    if (!dc) { ftpCtrlCli.print("425 Brak polaczenia danych\r\n"); return; }

    DirEntryInfo *entries = new (std::nothrow) DirEntryInfo[MAX_DIR_ENTRIES];
    if (!entries) {
      dc.stop();
      ftpCtrlCli.print("451 Brak pamieci\r\n");
      return;
    }

    ftpCtrlCli.print("150 Lista plikow\r\n");
    int n = storageListDir(ftpOnSD, ftpCwd.c_str(), entries, MAX_DIR_ENTRIES);
    for (int i = 0; i < n; i++) {
      if (cmd == "NLST") {
        dc.printf("%s\r\n", entries[i].name);
      } else if (entries[i].isDir) {
        dc.printf("drwxr-xr-x 1 pico pico 0 Jan 01 00:00 %s\r\n", entries[i].name);
      } else {
        dc.printf("-rw-r--r-- 1 pico pico %lu Jan 01 00:00 %s\r\n",
                  (unsigned long)entries[i].size, entries[i].name);
      }
    }
    delete[] entries;
    dc.stop();
    ftpCtrlCli.print("226 Gotowe\r\n");
  } else if (cmd == "RETR") {
    String fp = ftpResolvePath(arg);
    fsLock();
    File f = fsOpenReadLocked(ftpOnSD, fp.c_str());
    bool ok = f && !f.isDirectory();
    if (!ok && f) f.close();
    fsUnlock();

    if (!ok) { ftpCtrlCli.print("550 Brak pliku\r\n"); return; }
    WiFiClient dc = ftpAcceptData();
    if (!dc) { fsLock(); f.close(); fsUnlock(); ftpCtrlCli.print("425 Blad danych\r\n"); return; }

    setFtpStatus("Pobieranie...");
    ftpCtrlCli.print("150 Wysylam plik\r\n");
    while (dc.connected()) {
      fsLock();
      int len = f.read(ftpBuf, sizeof(ftpBuf));
      fsUnlock();
      if (len <= 0) break;
      dc.write(ftpBuf, len);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    fsLock(); f.close(); fsUnlock();
    dc.stop();
    ftpCtrlCli.print("226 Transfer zakonczony\r\n");
    setFtpStatus("Klient polaczony");
  } else if (cmd == "STOR") {
    String fp = ftpResolvePath(arg);
    fsLock();
    File f = fsOpenWriteLocked(ftpOnSD, fp.c_str());
    fsUnlock();

    if (!f) { ftpCtrlCli.print("550 Nie mozna zapisac\r\n"); return; }
    WiFiClient dc = ftpAcceptData();
    if (!dc) { fsLock(); f.close(); fsUnlock(); ftpCtrlCli.print("425 Blad danych\r\n"); return; }

    setFtpStatus("Wysylanie...");
    ftpCtrlCli.print("150 Odbieram plik\r\n");
    unsigned long idleT0 = millis();
    while (dc.connected() || dc.available()) {
      int av = dc.available();
      if (av > 0) {
        int len = dc.read(ftpBuf, min(av, (int)sizeof(ftpBuf)));
        if (len > 0) {
          fsLock(); f.write(ftpBuf, len); fsUnlock();
          idleT0 = millis();
        }
      } else {
        if (millis() - idleT0 > 3000UL) break;
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }
    fsLock(); f.close(); fsUnlock();
    dc.stop();
    ftpCtrlCli.print("226 Zapisano\r\n");
    setFtpStatus("Klient polaczony");
  } else if (cmd == "DELE") {
    String fp = ftpResolvePath(arg);
    fsLock(); bool ok = fsRemoveLocked(ftpOnSD, fp.c_str()); fsUnlock();
    ftpCtrlCli.print(ok ? "250 Usunieto\r\n" : "550 Blad usuwania\r\n");
  } else if (cmd == "MKD" || cmd == "XMKD") {
    String fp = ftpResolvePath(arg);
    fsLock(); bool ok = fsMkdirLocked(ftpOnSD, fp.c_str()); fsUnlock();
    ftpCtrlCli.print(ok ? "257 Utworzono\r\n" : "550 Blad MKD\r\n");
  } else if (cmd == "RMD" || cmd == "XRMD") {
    String fp = ftpResolvePath(arg);
    fsLock(); bool ok = fsRmdirLocked(ftpOnSD, fp.c_str()); fsUnlock();
    ftpCtrlCli.print(ok ? "250 Usunieto katalog\r\n" : "550 Blad RMD\r\n");
  } else if (cmd == "SIZE") {
    String fp = ftpResolvePath(arg);
    fsLock();
    File f = fsOpenReadLocked(ftpOnSD, fp.c_str());
    long sz = (f && !f.isDirectory()) ? (long)f.size() : -1;
    if (f) f.close();
    fsUnlock();
    if (sz >= 0) ftpCtrlCli.printf("213 %ld\r\n", sz);
    else         ftpCtrlCli.print("550 Brak pliku\r\n");
  } else if (cmd == "QUIT") {
    ftpCtrlCli.print("221 Do zobaczenia\r\n");
    ftpCtrlCli.stop();
    setFtpStatus("Dziala (port 21)");
  } else {
    ftpCtrlCli.print("502 Nieobslugiwane\r\n");
  }
}

static void ftpPoll() {
  if (!ftpActive) return;

  if (!ftpCtrlCli || !ftpCtrlCli.connected()) {
    WiFiClient nc = ftpCtrlSrv.accept();
    if (nc) {
      ftpCtrlCli = nc;
      ftpCmdLine = "";
      ftpCwd = "/";
      ftpCtrlCli.print("220 Hollow Star OS FTP Ready\r\n");
      setFtpStatus("Klient polaczony");
    }
    return;
  }

  while (ftpCtrlCli.available()) {
    char c = (char)ftpCtrlCli.read();
    if (c == '\r') continue;
    if (c == '\n') {
      ftpCmdLine.trim();
      if (ftpCmdLine.length() > 0) ftpHandleCommand(ftpCmdLine);
      ftpCmdLine = "";
      if (!ftpCtrlCli.connected()) {
        setFtpStatus("Dziala (port 21)");
        break;
      }
    } else if (ftpCmdLine.length() < 96) {
      ftpCmdLine += c;
    }
  }
}

static void taskNet(void *pvParams) {
  (void)pvParams;

  // Auto-łączenie z zapisaną siecią Wi-Fi przy starcie
  if (sysConfig.wifiSsid[0] != '\0') {
    vTaskDelay(pdMS_TO_TICKS(500));
    internalWifiConnect();
  }

  unsigned long lastRssiMs = 0;

  for (;;) {
    NetCmd cmd;
    while (xQueueReceive(netCmdQueue, &cmd, 0) == pdTRUE) {
      switch (cmd.type) {
        case NCMD_WIFI_SCAN:
          internalWifiScan();
          break;
        case NCMD_WIFI_CONNECT_SAVED:
          internalWifiConnect();
          break;
        case NCMD_WIFI_DISCONNECT:
          ftpStop();
          WiFi.disconnect();
          xSemaphoreTake(netMutex, portMAX_DELAY);
          netState.wifiConnected = false;
          netState.wifiRssi = -127;
          netState.ipStr[0] = '\0';
          xSemaphoreGive(netMutex);
          setWifiStatus("Rozlaczono");
          break;
        case NCMD_NTP_SYNC:
          internalNtpSync();
          break;
        case NCMD_FTP_START:
          ftpStart(cmd.arg != 0);
          break;
        case NCMD_FTP_STOP:
          ftpStop();
          break;
      }
    }

    unsigned long nowMs = millis();
    if (nowMs - lastRssiMs >= 4000UL) {
      lastRssiMs = nowMs;
      bool linkUp = (WiFi.status() == WL_CONNECTED);
      if (linkUp) {
        int8_t rssi = (int8_t)constrain(WiFi.RSSI(), -127, 0);
        xSemaphoreTake(netMutex, portMAX_DELAY);
        bool changed = !netState.wifiConnected || (abs((int)netState.wifiRssi - (int)rssi) >= 4);
        netState.wifiConnected = true;
        netState.wifiRssi = rssi;
        xSemaphoreGive(netMutex);
        if (changed) markNetDirty();
      } else if (netState.wifiConnected) {
        ftpStop();
        xSemaphoreTake(netMutex, portMAX_DELAY);
        netState.wifiConnected = false;
        netState.wifiRssi = -127;
        netState.ipStr[0] = '\0';
        xSemaphoreGive(netMutex);
        setWifiStatus("Rozlaczono");
      }
    }

    ftpPoll();
    vTaskDelay(pdMS_TO_TICKS(ftpActive ? 5 : 25));
  }
}

void netFtpStartTask() {
  memset(&netState, 0, sizeof(netState));
  netState.wifiRssi = -127;
  strncpy(netState.wifiStatus, "Niepolaczono", sizeof(netState.wifiStatus) - 1);
  strncpy(netState.ftpStatus,  "Zatrzymany",   sizeof(netState.ftpStatus) - 1);

  netMutex = xSemaphoreCreateMutex();
  netCmdQueue = xQueueCreate(8, sizeof(NetCmd));
  xTaskCreateAffinitySet(taskNet, "NetFTP", 2048, nullptr, 1, (1 << 0), nullptr);
}

void netFtpSendCmd(NetCmdType type, int arg) {
  if (netCmdQueue != nullptr) {
    NetCmd cmd = { type, arg };
    xQueueSend(netCmdQueue, &cmd, 0);
  }
}

void netFtpGetSnapshot(NetUiSnapshot &out) {
  if (netMutex == nullptr) return;
  xSemaphoreTake(netMutex, portMAX_DELAY);
  out = netState;
  xSemaphoreGive(netMutex);
}

bool netFtpTakeDirty() {
  return __atomic_exchange_n(&netDirty, false, __ATOMIC_ACQ_REL);
}

bool netFtpConsumeSyncedTime(int &hour, int &minute) {
  if (netMutex == nullptr) return false;
  bool ready = false;
  xSemaphoreTake(netMutex, portMAX_DELAY);
  if (netState.ntpTimeReady) {
    hour = netState.ntpHour;
    minute = netState.ntpMin;
    netState.ntpTimeReady = false;
    ready = true;
  }
  xSemaphoreGive(netMutex);
  return ready;
}
