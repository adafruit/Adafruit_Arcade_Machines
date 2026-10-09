#!/bin/bash
# SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
#
# SPDX-License-Identifier: MIT

# Does a Fruit Jam sketch take the upload's 1200-baud reset? (DEVNOTES #170)
#
#   touchtest.sh <sketch.uf2> <seconds> [port]
#
# With the board in BOOTSEL (its RP2350 drive mounted): copies the .uf2,
# waits <seconds> while reading serial, sends the 1200-baud touch the way
# arduino-cli does, and reports whether the drive comes back. A sketch that
# takes the reset leaves the board in BOOTSEL, ready for the next run.
# Needs pyserial. The port defaults to the Fruit Jam's on this bench.
UF2="$1"
WAIT="$2"
PORT="${3:-/dev/cu.usbmodem312401}"
DRIVE=/Volumes/RP2350

cp "$UF2" "$DRIVE/" || exit 1
for i in $(seq 1 30); do [ -e "$PORT" ] && break; sleep 1; done

python3 - "$WAIT" "$PORT" "$DRIVE" <<'PY'
import os, sys, time
import serial

wait, port, drive = float(sys.argv[1]), sys.argv[2], sys.argv[3]
t0 = time.time()
# Keep the port open and drained while waiting, as a serial monitor would.
while time.time() - t0 < wait:
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
        while time.time() - t0 < wait:
            s.read(4096)
        s.close()
    except Exception:
        time.sleep(0.5)
s = serial.Serial(port, 1200)
time.sleep(0.3)
s.close()
for i in range(20):
    if os.path.exists(drive):
        print("after %ds: RESET OK (drive in %d s)" % (wait, i))
        break
    time.sleep(1)
else:
    print("after %ds: RESET IGNORED, port %s" % (wait, os.path.exists(port)))
PY
