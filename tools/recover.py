#!/usr/bin/env python3
"""Get the badge back to the launcher after a crash or hang.

Resets the chip over the debug console the way `make monitor` does
(DTR/RTS). If the showreel comes up again and announces itself
(READY/PONG), it is sent EXIT so it returns to the launcher. If nothing
answers within the timeout, the badge is assumed to be in the launcher.

From tanmatsu-idf6tests' tools/recover.py. Needs the ESP-IDF environment
(esp_pylib, esp_idf_monitor): source $IDF_SOURCE first.

Exit codes: 0 app answered and was sent EXIT, 4 reset done but no app
answered (probably in the launcher), 1 could not reset.
"""

import argparse
import os
import sys
import time

import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from testrun import connect, parse_record, read_line  # noqa: E402


def hard_reset(url):
    from esp_idf_monitor.base.chip_specific_config import get_chip_config
    from esp_pylib.serial_reset import hard_reset as pylib_hard_reset

    port = serial.serial_for_url(url, baudrate=115200, timeout=1, do_not_open=True)
    # Same order as esp_idf_monitor's SerialReader.open_serial(reset=True).
    port.rts = False
    port.dtr = False
    port.open()
    port.rts = True
    port.dtr = True
    pylib_hard_reset(port, hold_delay=get_chip_config("esp32p4")["reset"])
    time.sleep(0.2)
    port.close()


def recover(url, timeout=60, log=None):
    log = log or open(os.devnull, "w")
    print(f"Recovery: resetting the badge via {url}...")
    try:
        hard_reset(url)
    except Exception as exc:  # noqa: BLE001
        print(f"Recovery: reset failed: {exc}", file=sys.stderr)
        return 1
    port, ready = connect(url, timeout, log)
    if port is None:
        print("Recovery: no app answered after the reset (probably in the launcher)")
        return 4
    print(f"Recovery: {ready.get('app')} answered, sending EXIT")
    buffer = bytearray()
    try:
        port.write(b"EXIT\n")
        port.flush()
        deadline = time.time() + 5
        while time.time() < deadline:
            line = read_line(port, buffer)
            if line is not None and parse_record(line)[0] == "BYE":
                break
    except Exception:  # noqa: BLE001 - the link drops as the badge restarts
        pass
    try:
        port.close()
    except Exception:  # noqa: BLE001
        pass
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("PORT"))
    ap.add_argument("--timeout", type=float, default=60)
    args = ap.parse_args()
    return recover(args.port, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
