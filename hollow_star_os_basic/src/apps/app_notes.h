#pragma once
#include "../os/app_types.h"

extern const AppScreen screenAppNotes;

void appNotesOpenSetup(AppState returnApp = APP_MENU);
void appNotesOpenFile(bool onSD, const char* path, AppState returnApp, bool startInPreview);
