r"""
CYD-ScreenCapture — no-reset serial screenshot capture for ESP32 CYD projects.

Double-click: choose a screen from the menu, then capture it.
Command line:
  python screenshot.py [COM_PORT] [SCREEN] [OUTPUT_FILE]

Examples:
  python screenshot.py
  python screenshot.py COM11 0
  python screenshot.py COM11 8 scanner.png
  python screenshot.py solar.png

The SCREEN argument can be a raw number (0-9) or a named screen if your
project defines them. The target firmware maps byte commands however it wants.
Default port: COM11
"""

import os
import re
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial is not installed. Run: python -m pip install pyserial")
    sys.exit(1)

BAUD = 115200
DEFAULT_PORT = "COM10"
W, H = 320, 240
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

COM_RE = re.compile(r"^COM\d+$", re.IGNORECASE)


def looks_like_output(value):
    if not value:
        return False
    return any(ch in value for ch in "/.") or value.lower().endswith(("bmp", "png", "raw"))


def parse_args(argv):
    port = DEFAULT_PORT
    screen = None
    outfile = None
    args = list(argv)

    if args and COM_RE.match(args[0]):
        port = args.pop(0).upper()

    if args:
        # If it looks like a filename, treat as output
        if looks_like_output(args[0]):
            outfile = args.pop(0)
        else:
            screen = args.pop(0)

    if args:
        outfile = args.pop(0)

    if args:
        raise ValueError("Too many arguments.")

    return port, screen, outfile


def ask_interactive():
    print("CYD Screen Capture")
    port = input(f"COM port [{DEFAULT_PORT}]: ").strip() or DEFAULT_PORT
    print("\nEnter screen number (0-9) or leave blank for current screen:")
    screen = input("Screen [current]: ").strip() or None
    outfile = input("Output file [screenshot.png]: ").strip() or "screenshot.png"
    return port.upper(), screen, outfile


def read_exact(ser, n, timeout=30):
    data = b""
    deadline = time.time() + timeout
    while len(data) < n and time.time() < deadline:
        chunk = ser.read(min(4096, n - len(data)))
        if chunk:
            data += chunk
            pct = len(data) * 100 // n
            print(f"  {pct}%", end="\r")
    return data


def wait_for_marker(ser, markers, timeout=20):
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        c = ser.read(1)
        if not c:
            continue
        buf += c
        for marker in markers:
            if buf.endswith(marker):
                return marker, buf
        if len(buf) > 512:
            buf = buf[-512:]
    return None, buf


def write_png(filename, pixels_bgr24):
    """Write a minimal 24-bit BMP (the device sends RGB332 which we convert)."""
    if not os.path.isabs(filename):
        filename = os.path.join(BASE_DIR, filename)
    folder = os.path.dirname(filename)
    if folder:
        os.makedirs(folder, exist_ok=True)

    filesize = 54 + W * H * 3
    hdr = bytearray(54)
    hdr[0:2] = b"BM"
    hdr[2:6] = struct.pack("<I", filesize)
    hdr[10] = 54
    hdr[14] = 40
    hdr[18:22] = struct.pack("<I", W)
    hdr[22:26] = struct.pack("<i", -H)
    hdr[26:28] = struct.pack("<H", 1)
    hdr[28:30] = struct.pack("<H", 24)
    with open(filename, "wb") as f:
        f.write(hdr)
        f.write(pixels_bgr24)


def open_serial_no_reset(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 0.25
    ser.write_timeout = 5
    ser.rtscts = False
    ser.dsrdtr = False

    # Hold DTR/RTS deasserted to avoid triggering ESP32 bootloader
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.setDTR(False)
    ser.setRTS(False)
    return ser


def capture(port, screen, outfile):
    print(f"Opening {port} without reset...")
    ser = open_serial_no_reset(port)

    try:
        time.sleep(0.2)
        ser.reset_input_buffer()

        print("Checking device...")
        ser.write(b"R")
        ready, _ = wait_for_marker(ser, [b"READY"], timeout=2)
        if ready:
            print("Device ready.")
        else:
            print("No READY reply; continuing anyway...")

        ser.reset_input_buffer()

        if screen is not None:
            # Accept raw numbers (0-9) or named screens
            try:
                n = int(screen)
                cmd = str(n).encode()
            except ValueError:
                # Fallback: treat as raw byte command
                cmd = screen.encode() if len(screen) == 1 else screen[0].encode()
            print(f"Sending screen command: {cmd}")
            ser.write(cmd)
            ser.flush()
            time.sleep(0.6)
            ser.reset_input_buffer()
        else:
            print("Capturing current screen...")

        ser.write(b"S")
        ser.flush()
        marker, _ = wait_for_marker(ser, [b"RGB332:", b"OOM:"], timeout=20)

        if marker is None:
            raise RuntimeError("No screenshot response from device.")
        if marker == b"OOM:":
            info = ser.readline().decode("ascii", errors="replace").strip()
            raise RuntimeError(f"Device out of RAM: {info}")

        start = time.time()
        total = W * H
        data = read_exact(ser, total)
        if len(data) < total:
            raise RuntimeError(f"Transfer stalled at {len(data)}/{total} bytes.")
        elapsed = time.time() - start
        print(f"  Done in {elapsed:.1f}s          ")
    finally:
        ser.close()

    # RGB332 → 24-bit BGR (BMP format)
    pixels = bytearray(W * H * 3)
    for i, c in enumerate(data):
        r3 = (c >> 5) & 0x07
        g3 = (c >> 2) & 0x07
        b2 = c & 0x03
        r8 = (r3 << 5) | (r3 << 2) | (r3 >> 1)
        g8 = (g3 << 5) | (g3 << 2) | (g3 >> 1)
        b8 = (b2 << 6) | (b2 << 4) | (b2 << 2) | b2
        pixels[i*3+0] = b8
        pixels[i*3+1] = g8
        pixels[i*3+2] = r8

    write_png(outfile, pixels)
    print(f"Saved {outfile}")


def main():
    interactive = len(sys.argv) == 1 and sys.stdin.isatty()
    try:
        if interactive:
            port, screen, outfile = ask_interactive()
        else:
            port, screen, outfile = parse_args(sys.argv[1:])
        capture(port, screen, outfile)
    except Exception as exc:
        print(f"Error: {exc}")
        if not interactive:
            sys.exit(1)
    finally:
        if interactive:
            input("Press Enter to close...")


if __name__ == "__main__":
    main()
