#pragma once
#include "app_types.h"

extern const AppScreen screenMainMenu;
extern const AppScreen screenGamesMenu;
extern const AppScreen screenToolsMenu;
extern const AppScreen screenSettingsMenu;

// Wywoływane z menu przy wyborze Uśpienia (implementowane w głównym .ino)
void osEnterSuspend();
