# Wielka Migracja — Prompt Startowy dla Agenta 1 (`hollow-star-os-basic`)

> **Jak użyć tego pliku:** Skopiuj poniższy blok promptu jako pierwszą wiadomość w nowej sesji lokalnego agenta na **CachyOS / Arch Linux** (np. OpenCode, Cline, Roo Code, Aider z lokalnym modelem LLM).

---

```text
Cześć! Przeszliśmy właśnie „Wielką Migrację” z macOS na lokalne środowisko na CachyOS (Arch Linux) z GPU NVIDIA RTX 5060 Ti 16 GiB VRAM. Od teraz przejmujesz rolę **Agenta 1 — Hollow Star OS Basic** w tym repozytorium (`hollow-star-os-basic`).

### 1. Obowiązkowe pliki do przeczytania na start (zanim wykonasz jakiekolwiek zmiany)
Przeczytaj dokładnie w tej kolejności:
1. `AGENTS.md` (zasady projektu, konfiguracja płytki RP2040 / Pico W, reguła izolacji nagłówków, reguła stałej synchronizacji pamięci)
2. `folder_structure.txt` (układ katalogów projektu)
3. Współdzielona pamięć ekosystemu (global + local):
   - `/Users/nataniel/project/memory/INDEX.md`
   - `/Users/nataniel/project/memory/CONVENTIONS.md`
   - `/Users/nataniel/project/memory/FEATURES_AND_UX.md`
   - `/Users/nataniel/project/memory/PROJECT_BASIC.md`
   - (oraz pobieżnie `PROJECT_PLUS.md` i `PROJECT_STANDARD.md` dla pełnego kontekstu ekosystemu)
4. Zgłoszenia błędów i zrzuty:
   - `/Users/nataniel/project/bugs/hollow-star-os-basic/` (wszystkie rozwiązane i zweryfikowane bugi przenoś do podkatalogu `resolved/`)
   - `/Users/nataniel/project/demo photos/` (sprawdź, czy są nowe zdjęcia z testów sprzętowych)

*(Uwaga systemowa: jeśli na Linuksie `/Users/nataniel` wskazuje przez symlink na `$HOME`, powyższe ścieżki działają natywnie; w przeciwnym razie użyj `../memory/` i `../bugs/` względem katalogu `~/project`.)*

### 2. Aktualny stan projektu (przed migracją)
- **Repozytorium:** `hollow-star-os-basic` (gałąź `main`, ostatni znany commit przed migracją: `5ec341d`, wersja **`v1.3.0`**)
- **Sprzęt docelowy:** Raspberry Pi Pico / Pico W (RP2040, ARM Cortex-M0+, Arduino-Pico + FreeRTOS SMP, 240 MHz, Flash 2 MB: 1 MB Sketch / 1 MB LittleFS)
- **Styl UX (unikalny w ekosystemie):** Tylko `Basic` zachowuje **retro monochromatyczny wygląd** (ograniczenia sprzętowe LCD 128×64 — ST7920), podczas gdy `Plus` i `Standard` przeszły na styl Golden Yellow (`#F4B41A` / `#FFD166` na `#131926`).
- **Najważniejsze funkcje już działające w `v1.3.0`:**
  - Dual-core FreeRTOS SMP (Core 0: shell/UI, Core 1: TinyUSB Host, klawiatura USB HID, opcjonalnie BTstack)
  - Hierarchiczny VFS na LittleFS, wbudowany TinyBASIC z grafiką, Tree File Explorer, Markdown Notes Reader/Editor, komendy sysinfo/GPIO
- **Komendy budowania i weryfikacji (Linux):**
  - Kompilacja: `./pico.sh build` (wymaga `arduino-cli` + core `rp2040:rp2040` i bibliotek wg `AGENTS.md`)
  - Wgrywanie i logi UART na Pico: `./pico.sh flash` / `./pico.sh logs`

### 3. Krytyczne zasady pracy
- Przestrzegaj bezwzględnie `AGENTS.md` (m.in. pamiętaj o izolacji nagłówków przy FreeRTOS/TinyUSB/BTstack oraz utrzymywaniu czystości drzewa plików).
- **Stała synchronizacja pamięci:** Przy każdym zadaniu sprawdzaj `/Users/nataniel/project/memory/` i aktualizuj `PROJECT_BASIC.md` (oraz `FEATURES_AND_UX.md` jeśli zmieniasz wspólne UX) po każdym większym kamieniu milowym — bez generowania ręcznych promptów synchronizujących, bo pozostali agenci robią to samo automatycznie.
- Nie zgaduj wyników kompilacji ani testów — po zmianach uruchom `./pico.sh build` (lub odpowiednią weryfikację dostępną w środowisku).

### 4. Pierwsze zadanie rozruchowe
1. Sprawdź środowisko na CachyOS (`arduino-cli version`, zainstalowane core `rp2040:rp2040`, dostępność narzędzi w `PATH`) oraz uruchom `./pico.sh build`, żeby potwierdzić, że kompilacja na Linuksie działa bez błędów.
2. Przeczytaj wszystkie pliki z sekcji 1 i sprawdź `git status` / `git log -n 5`.
3. Zamelduj krótko:
   - czy środowisko kompilacji `hollow-star-os-basic` na Linuksie działa,
   - czy w `bugs/hollow-star-os-basic/` lub `demo photos/` pojawiło się coś nowego,
   - jakie proponujesz kolejne kroki dla `Hollow Star OS Basic` po `v1.3.0`.
```
