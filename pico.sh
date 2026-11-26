#!/bin/bash
set -e

SKETCH="./hollow_star_os_basic"
FQBN="rp2040:rp2040:rpipicow:flash=2097152_1048576,freq=240,os=freertos,ipbtstack=ipv4btcble,usbstack=tinyusb"

find_port() {
  ls /dev/cu.usbmodem* 2>/dev/null | head -n 1
}

case "$1" in
  build)
    arduino-cli compile --fqbn "$FQBN" "$SKETCH"
    ;;
  upload)
    PORT=$(find_port)
    if [ -z "$PORT" ]; then
      echo "Nie wykryto portu /dev/cu.usbmodem* (jesli Pico jest w trybie BOOTSEL, wgrywam przez UF2)..."
      arduino-cli upload --fqbn "$FQBN" "$SKETCH"
    else
      arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"
    fi
    ;;
  flash)
    "$0" build
    "$0" upload
    ;;
  monitor)
    PORT=$(find_port)
    [ -z "$PORT" ] && { echo "Brak podlaczonego Pico W!"; exit 1; }
    arduino-cli monitor -p "$PORT" -c baudrate=115200
    ;;
  logs)
    # Krotki 5-sekundowy zrzut logow (bezpieczny dla agenta AI - nie zawiesza terminala)
    PORT=$(find_port)
    [ -z "$PORT" ] && { echo "Brak podlaczonego Pico W!"; exit 1; }
    arduino-cli monitor -p "$PORT" -c baudrate=115200 &
    MON_PID=$!
    sleep 5
    kill $MON_PID 2>/dev/null || true
    ;;
  *)
    echo "Uzycie: ./pico.sh {build|upload|flash|monitor|logs}"
    exit 1
    ;;
esac

