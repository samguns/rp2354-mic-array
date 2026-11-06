"""Capture a short four-channel WAV through the Zephyr USB shell."""
import argparse
import re
import time
import wave
from pathlib import Path


def read_matching(port, pattern, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        match = re.search(pattern, line)
        if match:
            return match
    raise TimeoutError(f"Device did not respond with {pattern!r}")


def read_pcm(port):
    header = read_matching(port, r"PCM_BEGIN (\d+) (\d+) (\d+)")
    rate, channels, frames = map(int, header.groups())
    if not (1000 <= rate <= 192000 and channels == 4 and 0 < frames <= 48000):
        raise ValueError("Invalid PCM header")
    expected = channels * frames * 2
    payload = bytearray()
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        if line == "PCM_END":
            if len(payload) != expected:
                raise ValueError(f"Truncated recording: {len(payload)} of {expected} bytes")
            return rate, channels, bytes(payload)
        if re.fullmatch(r"[0-9a-f]+", line) and len(line) % 2 == 0:
            payload.extend(bytes.fromhex(line))
            if len(payload) > expected:
                raise ValueError("PCM payload exceeds declared size")
        elif line:
            raise ValueError(f"Unexpected output during PCM transfer: {line[:100]!r}")
    raise TimeoutError("PCM transfer did not finish")


def main():
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial port, e.g. COM7")
    parser.add_argument("output", type=Path)
    parser.add_argument("--ms", type=int, default=250, choices=range(1, 1001), metavar="1..1000")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.5) as port:
        time.sleep(2)
        port.write(b"\x03\r")
        time.sleep(0.5)
        port.reset_input_buffer()
        port.write(b"mic stats\r")
        stats = read_matching(port, r"rate=(\d+) channels=4 ready=(\d+) blocks=\d+ dropped=(\d+) faults=(\d+)")
        rate, ready, dropped, faults = map(int, stats.groups())
        if not ready or faults:
            raise RuntimeError("Capture is not ready or has faulted. Inspect mic stats; reboot after a fault.")
        if dropped:
            print(f"Warning: {dropped} earlier raw blocks dropped; check CPU load.")
        if args.ms * rate // 1000 > 16000:
            raise ValueError(f"RAM recording is limited to {16000*1000//rate} ms at {rate} Hz; use UAC2 for longer recordings")
        port.write(f"mic record {args.ms}\r".encode())
        read_matching(port, r"^RECORDING$")
        deadline = time.monotonic() + 5
        while True:
            port.write(b"mic status\r")
            status = read_matching(port, r"RECORD (DONE|BUSY|INVALID|EMPTY) (\d+) (\d+)")
            if status[1] == "DONE":
                break
            if status[1] != "BUSY" or time.monotonic() > deadline:
                raise RuntimeError(f"Recording failed: {status[0]}")
            time.sleep(0.1)
        port.write(b"mic dump\r")
        rate, channels, pcm = read_pcm(port)
    with wave.open(str(args.output), "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm)
    print(f"Saved {args.output}: {channels} channels, {rate} Hz, {len(pcm) // (2 * channels)} frames")


if __name__ == "__main__":
    main()
