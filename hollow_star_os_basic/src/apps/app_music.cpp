#include "app_music.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../storage_cfg.h"
#include "../../bt_audio.h"

static int trackSel = 0;
static bool playerView = false;
static unsigned long lastAnimMs = 0;
static uint8_t animFrame = 0;
static bool volSavePending = false;
static unsigned long lastVolMs = 0;

static void adjustVolume(int delta) {
  int nv = constrain((int)audioSnap.volume + delta, 0, 100);
  btAudioSendCmd(ACMD_SET_VOLUME, nv);
  audioSnap.volume = (uint8_t)nv;
  sysConfig.volume = nv;
  volSavePending = true;
  lastVolMs = millis();
}

static void musicEnter() {
  lastAnimMs = millis();
  playerView = audioSnap.musicPlaying;
}

static bool musicTick(unsigned long now) {
  if (volSavePending && (now - lastVolMs >= 1500UL)) {
    volSavePending = false;
    storageSaveConfig();
  }
  if (playerView && audioSnap.musicPlaying && !audioSnap.musicPaused) {
    if (now - lastAnimMs >= 250UL) {
      lastAnimMs = now;
      animFrame++;
      return true;
    }
  }
  return false;
}

static bool musicKey(uint8_t k) {
  if (k == KEY_ESC) {
    if (volSavePending) {
      volSavePending = false;
      storageSaveConfig();
    }
    uiSetScreen(APP_MENU);
    return true;
  }
  if (k == 'r' || k == 'R') {
    btAudioSendCmd(ACMD_SCAN_SD);
    showToast(tr("Scanning SD card...", "Skanuje karte SD..."));
    return true;
  }
  if (k == 's' || k == 'S') {
    btAudioSendCmd(ACMD_STOP);
    return true;
  }
  if (k == 'o' || k == 'O') {
    btAudioSendCmd(ACMD_CYCLE_OUT_MODE);
#if HSOS_HAS_WIRELESS
    int nextM = ((int)audioSnap.outMode + 1) % 3;
    const char* lbl = (nextM == AOUT_MODE_AUTO) ? tr("Out: AUTO (Jack->BT)", "Wyjscie: AUTO (Jack->BT)")
                    : (nextM == AOUT_MODE_JACK) ? tr("Out: JACK (PWM)",      "Wyjscie: JACK (PWM)")
                                                : tr("Out: BLUETOOTH",       "Wyjscie: BLUETOOTH");
#else
    int nextM = (audioSnap.outMode == AOUT_MODE_AUTO) ? AOUT_MODE_JACK : AOUT_MODE_AUTO;
    const char* lbl = (nextM == AOUT_MODE_JACK) ? tr("Out: JACK (Forced)", "Wyjscie: JACK (Wymusz.)")
                                                : tr("Out: AUTO (Pin GP8)", "Wyjscie: AUTO (Pin GP8)");
#endif
    showToast(lbl);
    return true;
  }
  if (k == KEY_LEFT || k == '-' || k == '_') {
    adjustVolume(-5);
    return true;
  }
  if (k == KEY_RIGHT || k == '+' || k == '=') {
    adjustVolume(+5);
    return true;
  }
  if (k == KEY_TAB) {
    playerView = !playerView;
    animFrame = 0;
    return true;
  }
  if (k == ' ') {
    if (audioSnap.musicPlaying) btAudioSendCmd(ACMD_TOGGLE_PAUSE);
    else if (audioSnap.trackCount > 0) {
      btAudioSendCmd(ACMD_PLAY_TRACK, trackSel);
      playerView = true;
    }
    return true;
  }

  if (audioSnap.trackCount > 0) {
    if (k == KEY_UP) {
      if (playerView) {
        trackSel = (trackSel + audioSnap.trackCount - 1) % audioSnap.trackCount;
        btAudioSendCmd(ACMD_PREV_TRACK);
      } else {
        trackSel = (trackSel + audioSnap.trackCount - 1) % audioSnap.trackCount;
      }
    } else if (k == KEY_DOWN) {
      if (playerView) {
        trackSel = (trackSel + 1) % audioSnap.trackCount;
        btAudioSendCmd(ACMD_NEXT_TRACK);
      } else {
        trackSel = (trackSel + 1) % audioSnap.trackCount;
      }
    } else if (k == '\n') {
      if (playerView && audioSnap.musicPlaying) {
        btAudioSendCmd(ACMD_TOGGLE_PAUSE);
      } else {
        btAudioSendCmd(ACMD_PLAY_TRACK, trackSel);
        playerView = true;
        animFrame = 0;
      }
    }
  }
  return true;
}

static void stripMp3Ext(char *s) {
  size_t len = strlen(s);
  if (len >= 4 && strcasecmp(s + len - 4, ".mp3") == 0) {
    s[len - 4] = '\0';
  }
}

static void drawEqualizerBox(U8G2 &d, int x, int y, bool playing, bool paused) {
  d.drawFrame(x, y, 20, 20);
  static const uint8_t heights[8][4] = {
    { 5, 13,  8, 11 },
    { 9, 10, 14,  6 },
    {14,  7, 11, 13 },
    {10, 15,  6,  9 },
    { 6, 12, 15,  7 },
    {12,  8, 10, 15 },
    {15,  9,  5, 10 },
    { 8, 14, 12,  6 }
  };
  for (int c = 0; c < 4; c++) {
    int bh = 2;
    if (playing && !paused) bh = heights[animFrame & 7][c];
    else if (playing && paused) bh = 5;
    int bx = x + 3 + c * 4;
    int by = y + 18 - bh;
    d.drawBox(bx, by, 3, bh);
  }
}

static void drawMusicTopBar(U8G2 &d) {
  d.setFont(u8g2_font_5x7_tr);
  const char* outBadge = (audioSnap.activeSink == ASINK_JACK)
                         ? ((audioSnap.outMode == AOUT_MODE_JACK) ? "JACK*" : "JACK")
                         : (audioSnap.activeSink == ASINK_BT)
                         ? ((audioSnap.outMode == AOUT_MODE_BT) ? "BT*" : "BT")
                         : tr("NONE", "BRAK");
  d.drawBox(2, 11, 29, 8);
  d.setDrawColor(0);
  d.drawStr(4, 18, outBadge);
  d.setDrawColor(1);

  const char* st = audioSnap.musicPlaying
                   ? (audioSnap.musicPaused ? tr("PAUSE", "PAUZA") : tr("PLAY", "GRA"))
                   : "STOP";
  if (audioSnap.activeSink == ASINK_BT && !audioSnap.btConnected) st = "NO BT";
  else if (audioSnap.activeSink == ASINK_NONE) st = "NO JK";
  d.drawStr(34, 18, st);

  d.drawFrame(68, 12, 26, 6);
  int vw = (audioSnap.volume * 22) / 100;
  if (vw > 0) d.drawBox(70, 14, vw, 2);

  char vbuf[10];
  snprintf(vbuf, sizeof(vbuf), "%d%%", (int)audioSnap.volume);
  d.drawStr(97, 18, vbuf);
  d.drawHLine(0, 20, 128);
}

static void drawNowPlaying(U8G2 &d) {
  drawEqualizerBox(d, 2, 22, audioSnap.musicPlaying, audioSnap.musicPaused);

  char title[TRACK_NAME_LEN];
  if (audioSnap.musicPlaying && audioSnap.playingName[0]) {
    strncpy(title, audioSnap.playingName, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
  } else if (audioSnap.trackCount > 0) {
    if (trackSel >= audioSnap.trackCount) trackSel = audioSnap.trackCount - 1;
    btAudioGetTrackName(trackSel, title, sizeof(title));
  } else {
    snprintf(title, sizeof(title), "%s", tr("No track", "Brak utworu"));
  }
  stripMp3Ext(title);

  size_t tlen = strlen(title);
  int off = 0;
  if (tlen > 16 && audioSnap.musicPlaying && !audioSnap.musicPaused) {
    int span = (int)tlen - 16 + 4;
    int step = (int)(animFrame / 2) % span;
    off = min(step, (int)tlen - 16);
  }
  char visTitle[18];
  strncpy(visTitle, title + off, 16);
  visTitle[16] = '\0';
  d.setFont(u8g2_font_6x10_tr);
  d.drawStr(25, 31, visTitle);

  d.setFont(u8g2_font_5x7_tr);
  char sub[24];
  if (audioSnap.activeSink == ASINK_NONE) {
    snprintf(sub, sizeof(sub), "%s", tr("Plug jack or [O]", "Wepnij jack lub [O]"));
  } else if (audioSnap.activeSink == ASINK_BT && !audioSnap.btConnected) {
    snprintf(sub, sizeof(sub), "%s", tr("Conn BT / jack", "Polacz BT / jack"));
  } else if (audioSnap.musicPlaying) {
    int khz = audioSnap.sampleRate / 1000;
    if (audioSnap.bitrateKbps > 0) {
      snprintf(sub, sizeof(sub), "%dkHz %ukbps", khz, (unsigned)audioSnap.bitrateKbps);
    } else {
      snprintf(sub, sizeof(sub), "%dkHz %s", khz, (audioSnap.activeSink == ASINK_JACK) ? "PWM" : "A2DP");
    }
  } else {
    snprintf(sub, sizeof(sub), "%s %d/%d", tr("Track", "Utwor"), trackSel + 1, audioSnap.trackCount);
  }
  d.drawStr(25, 40, sub);

  char tStr[10];
  uint32_t sec = audioSnap.elapsedSec;
  snprintf(tStr, sizeof(tStr), "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  d.drawStr(2, 51, tStr);

  d.drawFrame(31, 45, 70, 6);
  if (audioSnap.musicPlaying && audioSnap.fileSize > 0) {
    uint32_t pw = (uint32_t)(((uint64_t)audioSnap.filePos * 66ULL) / audioSnap.fileSize);
    if (pw > 66) pw = 66;
    if (pw > 0) d.drawBox(33, 47, (u8g2_uint_t)pw, 2);
  }

  int dispIdx = (audioSnap.playingIdx >= 0) ? (audioSnap.playingIdx + 1) : (trackSel + 1);
  snprintf(sub, sizeof(sub), "%d/%d", dispIdx, max(1, audioSnap.trackCount));
  d.drawStr(104, 51, sub);

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Spc:|| </>:Vol Tab:List F1", "Spc:|| </>:Vol Tab:Lis F1"));
}

static void drawPlaylist(U8G2 &d) {
  if (trackSel >= audioSnap.trackCount) trackSel = audioSnap.trackCount - 1;
  int startIdx = max(0, min(trackSel - 1, audioSnap.trackCount - 3));
  for (int i = 0; i < 3 && (startIdx + i) < audioSnap.trackCount; i++) {
    int idx = startIdx + i;
    int y = 22 + i * 10;

    char nameBuf[TRACK_NAME_LEN];
    btAudioGetTrackName(idx, nameBuf, sizeof(nameBuf));
    stripMp3Ext(nameBuf);

    const char* pref = (idx == audioSnap.playingIdx && audioSnap.musicPlaying)
                       ? (audioSnap.musicPaused ? "|| " : "> ") : "  ";
    char line[26];
    snprintf(line, sizeof(line), "%s%.21s", pref, nameBuf);

    if (idx == trackSel) {
      d.drawBox(0, y, 128, 9);
      d.setDrawColor(0); d.drawStr(2, y + 7, line); d.setDrawColor(1);
    } else {
      d.drawStr(2, y + 7, line);
    }
  }

  d.drawHLine(0, 53, 128);
  d.drawStr(2, 62, tr("Ent:Ply </>:Vol Tab:Plyr?", "Ent:G </>:Vol Tab:Odt F1"));
}

static void musicDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  drawMusicTopBar(d);

  if (!audioSnap.sdReady) {
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(4, 34, tr("No SD card (FAT32)!", "Brak karty SD (FAT32)!"));
    d.drawHLine(0, 53, 128);
    d.drawStr(2, 62, tr("R:Mount  Esc:M  F1:Help", "R:Montuj  Esc:M  F1:Pom"));
    rysujToastJesliAktywny();
    uiFinishFrame();
    return;
  }
  if (audioSnap.trackCount == 0) {
    d.setFont(u8g2_font_5x7_tr);
    d.drawStr(4, 34, tr("No .mp3 files in /", "Brak plikow .mp3 na /"));
    d.drawHLine(0, 53, 128);
    d.drawStr(2, 62, tr("R:Rescan  Esc:M  F1:Help", "R:Odswiez  Esc:M  F1:Pom"));
    rysujToastJesliAktywny();
    uiFinishFrame();
    return;
  }

  if (playerView) drawNowPlaying(d);
  else            drawPlaylist(d);

  rysujToastJesliAktywny();
  uiFinishFrame();
}

static const char* const HELP_MUSIC_EN[] = {
  "^ / v: Track (List/Skip)",
  "Enter: Play / Pause",
  "Space: Pause | S: Stop",
  "</> or -/+: Volume",
  "Tab: Player / Playlist",
  "O: Output AUTO/JACK/BT",
  "R: Rescan SD | Esc: Menu"
};
static const char* const HELP_MUSIC_PL[] = {
  "^ / v: Utwor (Lista/Next)",
  "Enter: Graj / Pauza",
  "Spacja: Pauza | S: Stop",
  "</> lub -/+: Glosnosc",
  "Tab: Odtwarzacz / Lista",
  "O: Wyjscie AUTO/JACK/BT",
  "R: Skanuj SD | Esc: Menu"
};

const AppScreen screenAppMusic = { musicEnter, musicTick, musicKey, musicDraw, HELP_MUSIC_EN, HELP_MUSIC_PL, 7 };
