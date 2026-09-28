#!/usr/bin/env python3
"""Run one automated showreel test on the badge and collect the results.

The app must be running (normally `make cycle` has just installed and
started it). This script:

  1. connects to the debug console, pinging until the app announces itself
     (reconnecting as needed -- after `make run` that can take ~10 s),
  2. refuses to test a badge running another build (git id mismatch),
  3. sends `RUN <test>` and collects the CRC-framed records until END,
  4. waits for the app to return to the launcher by itself (BYE),
  5. for shot tests: stores the framebuffer hashes the badge reports as
     references, or compares them against the stored ones -- no transfer
     needed; the PNGs stay on the badge's SD card and are downloaded only
     with --fetch (BadgeLink is slow: over a minute per image),
  6. writes results/<UTC>-<test>-<scene>.{log,json} (+ images) and prints a
     summary.

Tests (see main/devtest.h):
    perf  scene=<name> [secs=<whole seconds>]
    shots scene=<name> ms=<t1>,<t2>,...

Exit codes: 0 ok, 1 link / timeout, 2 crash or error on the badge,
3 test reported bad, 4 image mismatch against references, 5 usage.

Structure after tanmatsu-idf6tests' tools/sdtest.py (itself after
tanmatsu-fonttest's testrun.py), including the two load-bearing read_line()
details.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import serial
except ImportError:
    print("pyserial missing (run inside the ESP-IDF environment: source $IDF_SOURCE)", file=sys.stderr)
    sys.exit(5)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REFS_DIR = os.path.join(ROOT, "tests", "refs")
MANIFEST = os.path.join(REFS_DIR, "manifest.json")
BL_RETRY = os.path.join(HERE, "badgelink_retry.sh")
BL_SH = os.path.join(ROOT, "badgelink", "tools", "badgelink.sh")

# The record framing; the app's REPORT_PREFIX (report.h) must match, and
# --prefix rebuilds this for an app that changed it.
RECORD = re.compile(r"^@@SR-([A-Z]+)@@ (.*?) @@([0-9a-f]{8})@@\s*$")


def set_prefix(prefix):
    global RECORD
    RECORD = re.compile(r"^@@" + re.escape(prefix) + r"-([A-Z]+)@@ (.*?) @@([0-9a-f]{8})@@\s*$")
CRASH = re.compile(r"Guru Meditation|abort\(\) was called|Backtrace:|rst:0x|ESP-ROM:|assert failed|Stack smashing")

EXIT_OK, EXIT_LINK, EXIT_CRASH, EXIT_BAD, EXIT_MISMATCH, EXIT_USAGE = 0, 1, 2, 3, 4, 5


# --- Console ---------------------------------------------------------------

def read_line(port, buffer):
    """One complete line or None. Drain the buffer before reading again, and
    read what is waiting rather than a fixed count: getting either wrong made
    the host fall behind until the badge's USB FIFO filled and output was
    silently dropped (fonttest testrun.py)."""
    if b"\n" not in buffer:
        chunk = port.read(max(1, port.in_waiting))
        if not chunk:
            return None
        buffer.extend(chunk)
        if b"\n" not in buffer:
            return None
    line, _, rest = bytes(buffer).partition(b"\n")
    buffer.clear()
    buffer.extend(rest)
    return line.decode("utf-8", errors="replace").rstrip("\r")


def parse_record(line):
    match = RECORD.match(line)
    if not match:
        return None, None
    payload = match.group(2)
    if zlib.crc32(payload.encode()) != int(match.group(3), 16):
        return "CORRUPT", None
    try:
        return match.group(1), json.loads(payload)
    except json.JSONDecodeError:
        return "CORRUPT", None


def open_console(url, timeout=1):
    """Open the debug console and leave it the way `idf.py monitor` does:
    DTR and RTS both deasserted. With pyserial's default (both asserted for
    the whole session) the host's writes did not reach the badge until the
    session closed and the tty dropped the lines (F-16/F-18). Order matters:
    RTS off first, then DTR -- DTR off while RTS is still on resets a
    USB-Serial-JTAG chip. Setting them before open() gets that order wrong
    (pyserial's rfc2217 open sends DTR first)."""
    port = serial.serial_for_url(url, baudrate=115200, timeout=timeout, do_not_open=True)
    port.open()
    port.rts = False
    port.dtr = False
    return port


def wait_ready(port, timeout, log):
    """Wait for the app's READY banner WITHOUT sending anything first: the
    listener emits READY only once its USB-serial/JTAG driver is installed,
    and bytes that reach the badge before that (e.g. PINGs sent while it
    boots) left its receive side stuck (F-16)."""
    deadline = time.time() + timeout
    buffer = bytearray()
    while time.time() < deadline:
        line = read_line(port, buffer)
        if line is None:
            continue
        log.write(line + "\n")
        kind, rec = parse_record(line)
        if kind in ("READY", "PONG"):
            return rec
    return None


def connect(url, total_timeout, log):
    """Open the console and wait for READY/PONG, reopening as needed: the
    rfc2217 proxy may refuse or drop the first connections after an app
    start, and the app needs a few seconds to boot."""
    deadline = time.time() + total_timeout
    attempt = 0
    last = None
    while time.time() < deadline:
        attempt += 1
        port = None
        try:
            port = open_console(url)
            ready = wait_ready(port, min(10.0, max(1.0, deadline - time.time())), log)
            if ready is not None:
                return port, ready
            last = "no READY/PONG"
        except Exception as exc:  # noqa: BLE001 - pyserial raises several types
            last = exc
        if port is not None:
            try:
                port.close()
            except Exception:  # noqa: BLE001
                pass
        print(f"  connect attempt {attempt}: {last}", file=sys.stderr)
        time.sleep(2.0)
    return None, None


def local_git_id():
    """The id the build embeds (CMakeLists.txt: git describe --always --dirty --abbrev=10)."""
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty", "--abbrev=10"], cwd=ROOT,
                             capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


# --- BadgeLink ---------------------------------------------------------------

def badgelink(conn, *args, retry=True, quiet=False):
    cmd = ([BL_RETRY] if retry else [BL_SH]) + conn.split() + list(args)
    res = subprocess.run(cmd, cwd=ROOT, capture_output=quiet, text=True)
    return res.returncode == 0


def ensure_badgelink_mode(port_url, conn, log, tries=6, waits=5):
    """Same as `make mode_badgelink`: probe, else ask for it on the console."""
    if badgelink(conn, "fs", "list", "/sd", retry=False, quiet=True):
        return True
    for attempt in range(1, tries + 1):
        print(f"  requesting BadgeLink mode (attempt {attempt})...")
        try:
            s = open_console(port_url)
            s.write(b"BADGELINK\n")
            s.flush()
            log.write(s.read(256).decode(errors="replace"))
            s.close()
        except Exception as exc:  # noqa: BLE001
            log.write(f"### BADGELINK request failed: {exc}\n")
        for _ in range(waits):
            time.sleep(1)
            if badgelink(conn, "fs", "list", "/sd", retry=False, quiet=True):
                return True
    return False


# --- Images ------------------------------------------------------------------

def load_manifest():
    try:
        with open(MANIFEST, encoding="utf-8") as fh:
            return json.load(fh)
    except FileNotFoundError:
        return {}


def save_manifest(manifest):
    os.makedirs(REFS_DIR, exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)
        fh.write("\n")


def diff_images(ref_path, act_path, out_path):
    """3-panel PNG (reference | actual | amplified difference) plus statistics."""
    import png_diff

    rw, rh, ref = png_diff.read_png(ref_path)
    aw, ah, act = png_diff.read_png(act_path)
    if (rw, rh) != (aw, ah):
        return {"error": f"size {aw}x{ah} != reference {rw}x{rh}"}
    pw, ph, panel, verdict = png_diff.diff_panels(ref, act, rw, rh)
    png_diff.write_png(out_path, pw, ph, panel)
    return verdict


# --- Main --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("PORT"), help="debug console (rfc2217://... or device)")
    ap.add_argument("--badgelink-conn", default=None, help='e.g. "--tcp localhost:4003"')
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "results"))
    ap.add_argument("--connect-timeout", type=float, default=12,
                    help="the app answers within ~10 s of starting (proxy restart included), or not at all")
    ap.add_argument("--run-timeout", type=float, default=120)
    ap.add_argument("--stall-timeout", type=float, default=10)
    ap.add_argument("--prefix", default="SR", help="record prefix, as the app's REPORT_PREFIX (default SR)")
    ap.add_argument("--no-git-check", action="store_true", help="test whatever build the badge runs")
    ap.add_argument("--capture-refs", action="store_true", help="store the shots as the new references")
    ap.add_argument("--compare", action="store_true", help="compare the shots against the references")
    ap.add_argument("--reason", default="", help="note stored with --capture-refs")
    ap.add_argument("--fetch", action="store_true",
                    help="download the shot PNGs over BadgeLink (slow: over a minute each)")
    ap.add_argument("--no-recover", action="store_true")
    ap.add_argument("--reset", action="store_true",
                    help="hard-reset the badge first, as idf.py monitor does (it may then boot the launcher "
                         "rather than the app)")
    ap.add_argument("test", nargs="+", help='e.g. perf scene=turntable secs=20')
    args = ap.parse_args()
    if args.prefix != "SR":
        set_prefix(args.prefix)

    if not args.port:
        print("no --port and no $PORT", file=sys.stderr)
        return EXIT_USAGE
    bl_port = os.environ.get("BADGELINKPORT", "")
    conn = args.badgelink_conn or (f"--tcp {bl_port}" if ":" in bl_port else f"--port {bl_port}")

    test_line = " ".join(args.test)
    kind_of_test = args.test[0]
    params = dict(p.split("=", 1) for p in args.test[1:] if "=" in p)
    scene = params.get("scene", "?")

    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_id = f"{stamp}-{kind_of_test}-{scene}"
    run_dir = os.path.join(args.out_dir, run_id)
    os.makedirs(run_dir, exist_ok=True)
    log = open(os.path.join(run_dir, "console.log"), "w", encoding="utf-8", buffering=1)  # line-buffered: survives a kill

    result = {
        "meta": {"host_time": stamp, "test": test_line, "local_git": local_git_id()},
        "ready": None, "hello": None, "begin": None, "perf": [], "shotperf": [], "shots": [], "end": None,
        "corrupt_lines": 0, "crash_lines": [], "images": {}, "verdict": None,
    }

    def finish(verdict, code):
        result["verdict"] = verdict
        if verdict in ("crash", "link-lost", "stalled", "timeout", "no-connection") and not args.no_recover:
            sys.path.insert(0, HERE)
            from recover import recover
            log.write(f"### recovery after {verdict}\n")
            result["recovery"] = recover(args.port, 60, log)
        log.close()
        with open(os.path.join(run_dir, "result.json"), "w", encoding="utf-8") as fh:
            json.dump(result, fh, indent=1)
        print_summary(result)
        print(f"\nverdict: {verdict}  ->  {run_dir}/")
        return code

    # 1-2. Optional first contact the way `idf.py monitor` makes it (hard
    # reset). Not the default: after a reset the badge sometimes boots the
    # launcher instead of the app (F-18). `make cycle` starts the app itself.
    if args.reset:
        from recover import hard_reset
        print(f"Resetting the badge via {args.port} (as idf.py monitor does)...")
        try:
            hard_reset(args.port)
        except Exception as exc:  # noqa: BLE001
            print(f"reset failed: {exc}", file=sys.stderr)
            return finish("no-connection", EXIT_LINK)
    print(f"Connecting to {args.port}...")
    port, ready = connect(args.port, args.connect_timeout, log)
    if port is None:
        return finish("no-connection", EXIT_LINK)
    result["ready"] = ready
    print(f"App: {ready.get('app')} git {ready.get('git')} built {ready.get('built')} "
          f"engine {ready.get('engine')} scene {ready.get('scene')}")
    want = result["meta"]["local_git"]
    if not args.no_git_check and want and ready.get("git") != want:
        print(f"Badge runs build {ready.get('git')!r}, this checkout is {want!r} -- "
              "install first (make cycle), or --no-git-check", file=sys.stderr)
        port.close()
        return finish("wrong-build", EXIT_USAGE)

    # 3. Run. Lead with a burst of bare newlines: they terminate whatever
    # stale half-line the app's line buffer may hold (each becomes an
    # ignored empty line or an "unknown command"), so the RUN parses clean.
    # Repeat newlines + RUN until the app answers BEGIN.
    run_cmd = b"\n" * 64 + f"RUN {test_line}\n".encode()
    port.write(run_cmd)
    port.flush()
    sends, last_send = 1, time.time()

    started = last_line = time.time()
    buffer = bytearray()
    crash_at = None
    verdict = code = None
    while verdict is None:
        now = time.time()
        if crash_at is not None and now - crash_at > 5:
            verdict, code = "crash", EXIT_CRASH
            break
        if now - started > args.run_timeout:
            verdict, code = "timeout", EXIT_LINK
            break
        if now - last_line > args.stall_timeout:
            verdict, code = "stalled", EXIT_LINK
            break
        if result["begin"] is None and now - last_send > 2.0:
            if sends >= 5:
                verdict, code = "no-begin", EXIT_LINK
                break
            port.write(run_cmd)
            port.flush()
            sends, last_send = sends + 1, now
            log.write(f"### RUN resent ({sends})\n")
        try:
            line = read_line(port, buffer)
        except Exception as exc:  # noqa: BLE001 - the link drops when the badge resets
            log.write(f"### link error: {exc}\n")
            verdict, code = ("crash", EXIT_CRASH) if crash_at else ("link-lost", EXIT_LINK)
            break
        if line is None:
            continue
        last_line = now
        log.write(line + "\n")
        kind, rec = parse_record(line)
        if kind is None:
            if CRASH.search(line):
                result["crash_lines"].append(line)
                crash_at = crash_at or now
            continue
        if kind == "CORRUPT":
            result["corrupt_lines"] += 1
        elif kind == "HELLO":
            result["hello"] = rec
        elif kind == "BEGIN":
            result["begin"] = rec
            print(f"Running: {rec}")
        elif kind == "PERF":
            result["perf"].append(rec)
            # The phase names are the app's (profile.h), so show the two
            # biggest rather than assuming any particular ones exist.
            top = sorted(rec.get("ph", {}).items(), key=lambda kv: -kv[1])[:2]
            phases = "  ".join(f"{k} {v:6.2f}" for k, v in top)
            print(f"  t={rec['t']:6.2f} {rec['shot'] or '-':<12} {rec['fps']:5.1f} fps  {phases}"
                  f"  tris {rec.get('tris', 0)} ttris {rec.get('ttris', 0)}")
        elif kind == "SHOTPERF":
            result["shotperf"].append(rec)
        elif kind == "SHOT":
            result["shots"].append(rec)
            print(f"  shot {rec['i']}: t={rec['t']:.3f} {rec['path']} fnv {rec['fnv']} ok={rec['ok']}")
        elif kind == "END":
            result["end"] = rec
            status = rec.get("status")
            verdict, code = {"ok": ("ok", EXIT_OK), "bad": ("bad", EXIT_BAD)}.get(status, ("error", EXIT_CRASH))

    # 4. The app returns to the launcher by itself after END; wait for BYE.
    if verdict in ("ok", "bad", "error"):
        deadline = time.time() + 5
        try:
            while time.time() < deadline:
                line = read_line(port, buffer)
                if line is None:
                    continue
                log.write(line + "\n")
                if parse_record(line)[0] == "BYE":
                    break
        except Exception:  # noqa: BLE001 - the link drops as the badge restarts
            pass
    try:
        port.close()
    except Exception:  # noqa: BLE001
        pass
    if verdict != "ok" or not result["shots"]:
        return finish(verdict, code)

    # 5. References: compared by the framebuffer hash the badge reports, so
    # nothing has to be transferred. BadgeLink is slow (well over a minute
    # per 1.1 MB screenshot), so images are fetched only on request.
    manifest = load_manifest()
    mismatches = 0
    for s in result["shots"]:
        name = os.path.basename(s["path"])
        info = result["images"].setdefault(s["path"], {})
        if args.capture_refs:
            manifest[name] = {"fnv": s["fnv"], "git": ready.get("git"), "engine": ready.get("engine"),
                              "captured": stamp, "reason": args.reason}
            info["ref"] = "captured"
        elif args.compare:
            ref = manifest.get(name)
            if ref is None:
                info["ref"] = "no reference"
                mismatches += 1
            elif ref["fnv"] == s["fnv"]:
                info["ref"] = "identical"
            else:
                info["ref"] = "DIFFERENT"
                mismatches += 1
        if info.get("ref"):
            print(f"  {name}: {info['ref']} (fnv {s['fnv']})")
    if args.capture_refs:
        save_manifest(manifest)

    # 6. Images, only when asked for (--fetch). They stay on the badge's SD
    # card either way (overwritten by the next run of the same instant).
    if args.fetch:
        print("Fetching shots over BadgeLink (slow)...")
        if not ensure_badgelink_mode(args.port, conn, log):
            print("could not get the badge into BadgeLink mode", file=sys.stderr)
            return finish("no-badgelink", EXIT_LINK)
        for s in result["shots"]:
            name = os.path.basename(s["path"])
            info = result["images"][s["path"]]
            local = os.path.join(run_dir, name)
            if not badgelink(conn, "fs", "download", s["path"], local, quiet=True):
                info["error"] = "download failed"
                print(f"  download failed: {s['path']}", file=sys.stderr)
                continue
            info["local"] = local
            if args.capture_refs:
                os.makedirs(REFS_DIR, exist_ok=True)
                shutil.copyfile(local, os.path.join(REFS_DIR, name))
            ref_png = os.path.join(REFS_DIR, name)
            if info.get("ref") == "DIFFERENT" and os.path.exists(ref_png):
                info["diff"] = diff_images(ref_png, local, os.path.join(run_dir, "diff_" + name))
                print(f"  {name}: {info['diff']}")
            print(f"  fetched {local}")

    if mismatches:
        return finish("mismatch", EXIT_MISMATCH)
    return finish("ok", EXIT_OK)


def print_summary(result):
    print()
    if result["shotperf"]:
        print(f"{'shot':<16}{'frames':>7}{'fps':>7}{'frame max':>10}{'rast':>8}{'rast max':>9}"
              f"{'tris':>7}{'ttris':>7}{'lines':>7}")
        for s in result["shotperf"]:
            print(f"{s['shot']:<16}{s['frames']:>7}{s['fps']:>7.2f}{s['frame_ms_max']:>10.2f}{s['rast_ms']:>8.2f}"
                  f"{s['rast_ms_max']:>9.2f}{s['tris']:>7.1f}{s['ttris']:>7.1f}{s['lines']:>7.1f}")
    if result["perf"]:
        last = result["perf"][-1]
        print(f"SRAM free {last['sram'] // 1024} KiB, largest block {last['sram_big'] // 1024} KiB")
    if result["corrupt_lines"]:
        print(f"corrupt record lines: {result['corrupt_lines']}")
    for line in result["crash_lines"][:5]:
        print(f"CRASH: {line}")


if __name__ == "__main__":
    sys.exit(main())
