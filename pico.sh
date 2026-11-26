#!/bin/bash
set -e

SKETCH="./hollow_star_os_basic"
FQBN_PICOW="rp2040:rp2040:rpipicow:flash=2097152_1048576,freq=240,os=freertos,ipbtstack=ipv4btcble,usbstack=tinyusb"
FQBN_PICO="rp2040:rp2040:rpipico:flash=2097152_1048576,freq=240,os=freertos,ipbtstack=ipv4only,usbstack=tinyusb"

OS_VERSION=$(grep -E '#define OS_BRAND_VERSION' hollow_star_os_basic/src/os/ui_common.h | sed -E 's/.*"([^"]+)".*/\1/')
[ -z "$OS_VERSION" ] && OS_VERSION="v1.1.0"

find_port() {
  ls /dev/cu.usbmodem* 2>/dev/null | head -n 1
}

export_uf2() {
  local board="$1"
  local fqbn="$2"
  local tmp_dir="build/${board}"
  local out_uf2="releases/hollow-star-os-${board}-${OS_VERSION}.uf2"

  mkdir -p "$tmp_dir" releases
  echo ">>> Kompilacja [$board] ($OS_VERSION)..."
  arduino-cli compile --fqbn "$fqbn" --output-dir "$tmp_dir" "$SKETCH"

  local src_uf2
  src_uf2=$(ls "$tmp_dir"/*.uf2 2>/dev/null | head -n 1)
  if [ -n "$src_uf2" ]; then
    cp "$src_uf2" "$out_uf2"
    echo ">>> Zapisano plik UF2: $out_uf2 ($(wc -c < "$out_uf2" | tr -d ' ') B)"
  else
    echo "UWAGA: Nie znaleziono pliku .uf2 w $tmp_dir"
    return 1
  fi
}

case "$1" in
  build)
    case "${2:-picow}" in
      picow)
        arduino-cli compile --fqbn "$FQBN_PICOW" "$SKETCH"
        ;;
      pico)
        arduino-cli compile --fqbn "$FQBN_PICO" "$SKETCH"
        ;;
      all)
        export_uf2 "picow" "$FQBN_PICOW"
        export_uf2 "pico"  "$FQBN_PICO"
        ;;
      *)
        echo "Uzycie: ./pico.sh build [picow|pico|all]"
        exit 1
        ;;
    esac
    ;;
  release)
    export_uf2 "picow" "$FQBN_PICOW"
    export_uf2 "pico"  "$FQBN_PICO"
    ;;
  upload)
    TARGET_FQBN="$FQBN_PICOW"
    [ "${2:-picow}" = "pico" ] && TARGET_FQBN="$FQBN_PICO"
    PORT=$(find_port)
    if [ -z "$PORT" ]; then
      echo "Nie wykryto portu /dev/cu.usbmodem* (jesli Pico jest w trybie BOOTSEL, wgrywam przez UF2)..."
      arduino-cli upload --fqbn "$TARGET_FQBN" "$SKETCH"
    else
      arduino-cli upload -p "$PORT" --fqbn "$TARGET_FQBN" "$SKETCH"
    fi
    ;;
  flash)
    TARGET="${2:-picow}"
    "$0" build "$TARGET"
    "$0" upload "$TARGET"
    ;;
  monitor)
    PORT=$(find_port)
    [ -z "$PORT" ] && { echo "Brak podlaczonego Pico!"; exit 1; }
    arduino-cli monitor -p "$PORT" -c baudrate=115200
    ;;
  logs)
    # Krotki 5-sekundowy zrzut logow (bezpieczny dla agenta AI - nie zawiesza terminala)
    PORT=$(find_port)
    [ -z "$PORT" ] && { echo "Brak podlaczonego Pico!"; exit 1; }
    arduino-cli monitor -p "$PORT" -c baudrate=115200 &
    MON_PID=$!
    sleep 5
    kill $MON_PID 2>/dev/null || true
    ;;
  *)
    echo "Uzycie: ./pico.sh {build [picow|pico|all]|release|upload [picow|pico]|flash [picow|pico]|monitor|logs}"
    exit 1
    ;;
esac
