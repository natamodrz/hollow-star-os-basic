#include "app_music.h"
#include "../os/ui_common.h"
#include "../../usb_kbd.h"
#include "../../bt_audio.h"

static int trackSel = 0;

static bool musicKey(uint8_t k) {
  if (k == KEY_ESC) { uiSetScreen(APP_MENU); return true; }
  if (k == 'r' || k == 'R') { btAudioSendCmd(ACMD_SCAN_SD); return true; }
  if (k == 's' || k == 'S') { btAudioSendCmd(ACMD_STOP); return true; }
  if (audioSnap.trackCount > 0) {
    if (k == KEY_UP)        trackSel = (trackSel + audioSnap.trackCount - 1) % audioSnap.trackCount;
    else if (k == KEY_DOWN) trackSel = (trackSel + 1) % audioSnap.trackCount;
    else if (k == '\n')     btAudioSendCmd(ACMD_PLAY_TRACK, trackSel);
    else if (k == ' ')      btAudioSendCmd(ACMD_TOGGLE_PAUSE);
  }
  return true;
}

static void musicDraw() {
  U8G2 &d = uiDisplay();
  d.clearBuffer();
  rysujPasekStanu();
  d.setFont(u8g2_font_5x7_tr);

  if (!audioSnap.sdReady) {
    d.drawStr(4, 24, "Brak karty SD (FAT32)!");
    d.drawStr(4, 62, "R:Odsw  F1:Pom  Esc:M");
    uiFinishFrame();
    return;
  }
  if (audioSnap.trackCount == 0) {
    d.drawStr(4, 24, "Brak plikow .mp3 na /");
    d.drawStr(4, 62, "R:Odsw  F1:Pom  Esc:M");
    uiFinishFrame();
    return;
  }

  if (trackSel >= audioSnap.trackCount) trackSel = audioSnap.trackCount - 1;
  int startIdx = max(0, min(trackSel - 1, audioSnap.trackCount - 4));
  for (int i = 0; i < 4 && (startIdx + i) < audioSnap.trackCount; i++) {
    int idx = startIdx + i;
    int y = 12 + i * 10;

    char nameBuf[TRACK_NAME_LEN];
    btAudioGetTrackName(idx, nameBuf, sizeof(nameBuf));
    size_t nlen = strlen(nameBuf);
    if (nlen >= 4 && strcasecmp(nameBuf + nlen - 4, ".mp3") == 0) {
      nameBuf[nlen - 4] = '\0';
    }

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
  const char* st = !audioSnap.btConnected ? "Brak BT!" :
                   (audioSnap.musicPlaying ? (audioSnap.musicPaused ? "PAUZA" : "GRA") : "STOP");
  d.drawStr(2, 62, st);
  d.drawStr(40, 62, "Ent:G Spc:P F1:Pom");
  uiFinishFrame();
}

static const char* const HELP_MUSIC[] = {
  "^ / v: Wybor utworu",
  "Enter: Odtwarzaj utwor",
  "Spacja: Pauza / Wznow",
  "S: Stop odtwarzania",
  "R: Skanuj karte SD",
  "Esc: Menu glowne"
};

const AppScreen screenAppMusic = { nullptr, nullptr, musicKey, musicDraw, HELP_MUSIC, 6 };
