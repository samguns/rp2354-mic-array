"""Record synchronized PCM16x4 over USB, rejecting gaps and corruption."""
import argparse
import array
import json
import math
import struct
import sys
import time
import wave
import zlib
from pathlib import Path
from record import read_matching

MAGIC = b"P2PCM001"
HEADER = struct.Struct("<8s7I")


def exact(port, size):
    data = bytearray()
    while len(data) < size:
        part = port.read(size - len(data))
        if not part:
            raise TimeoutError("Incomplete USB stream")
        data.extend(part)
    return bytes(data)


def packets(port, sample_rate=16000):
    # Only the command echo/prompt may precede the first packet.
    prefix = bytearray()
    while not prefix.endswith(MAGIC):
        prefix.extend(exact(port, 1))
        if len(prefix) > 4096:
            raise ValueError("Stream header not found")
    expected = None
    dropped_start = None
    short_packet = False
    while True:
        header = MAGIC + exact(port, HEADER.size - len(MAGIC))
        magic, sequence, rate, frames, dropped, faults, error, crc = HEADER.unpack(header)
        if magic != MAGIC or rate != sample_rate or not 0 <= frames <= 128:
            raise ValueError("Invalid stream header")
        if short_packet and frames:
            raise ValueError("Data after partial final block")
        short_packet = 0 < frames < 128
        if dropped_start is None:
            dropped_start = dropped
        if dropped != dropped_start or faults or error:
            raise ValueError("Capture drop, hardware fault or USB queue overflow")
        if expected is not None and sequence != expected:
            raise ValueError(f"Missing/reordered block: expected {expected}, got {sequence}")
        expected = (sequence + 1) & 0xffffffff
        payload = exact(port, frames * 8)
        if zlib.crc32(payload) != crc:
            raise ValueError("PCM checksum mismatch")
        if frames == 0:
            return
        yield sequence, payload
        if exact(port, len(MAGIC)) != MAGIC:
            raise ValueError("Lost packet framing")


def record(port_name, output, seconds, rate=48000):
    if rate == 48000:
        raise ValueError("Continuous 48 kHz CDC recording is unavailable; use tools/uac_wasapi.py instead")
    import serial
    partial = output.with_suffix(".partial.wav")
    totals = [0] * 4
    squares = [0] * 4
    peaks = [0] * 4
    clipped = [0] * 4
    frames = blocks = 0
    first = last = None
    started = time.monotonic()
    with serial.Serial(port_name, 115200, timeout=5) as port:
        if hasattr(port, "set_buffer_size"):
            port.set_buffer_size(rx_size=262144, tx_size=4096)
        port.write(b"\x03\r")
        time.sleep(0.3)
        port.reset_input_buffer()
        # Complete an ordinary shell response before enabling binary capture.
        # CDC's first-TX echo mitigation can delay delivery by 100 ms, longer
        # than the 48 kHz stream queue; it must finish before capture starts.
        port.write(b"mic stats\r")
        health = read_matching(port,r"rate=(\d+) channels=4 ready=(\d+) blocks=\d+ dropped=\d+ faults=(\d+)")
        if tuple(map(int,health.groups())) != (rate,1,0):
            raise ValueError("Capture not ready, faulted or sample rate mismatched")
        port.write(f"mic stream {seconds}\r".encode())
        with wave.open(str(partial), "wb") as wav:
            wav.setparams((4, 2, rate, 0, "NONE", "not compressed"))
            for sequence, payload in packets(port,rate):
                wav.writeframesraw(payload)
                samples = array.array("h", payload)
                if sys.byteorder != "little":
                    samples.byteswap()
                for ch in range(4):
                    x = samples[ch::4]
                    totals[ch] += sum(x)
                    squares[ch] += sum(v*v for v in x)
                    peaks[ch] = max(peaks[ch], max(abs(v) for v in x))
                    clipped[ch] += sum(v in (-32768, 32767) for v in x)
                frames += len(payload)//8
                blocks += 1
                if first is None:
                    first = sequence
                last = sequence
                if blocks % 1250 == 0:
                    print(f"Recorded {frames / rate:.0f} s; sequence {sequence}", flush=True)
    if frames != seconds * rate:
        raise ValueError(f"Unexpected length: {frames} frames")
    channels = []
    for ch in range(4):
        dc = totals[ch] / frames
        rms = math.sqrt(max(0, squares[ch] / frames - dc*dc))
        channels.append({"channel": ch+1, "dc_pcm": dc, "rms_pcm": rms,
                         "rms_dbfs": 20*math.log10(max(rms, 1e-12)/32768),
                         "peak_pcm": peaks[ch], "clipped_samples": clipped[ch]})
    report = {"rate": rate, "channels": 4, "frames": frames, "blocks": blocks,
              "first_sequence": first, "last_sequence": last,
              "wall_seconds": time.monotonic()-started, "integrity": "PASS",
              "channel_levels": channels}
    partial.replace(output)
    output.with_suffix(".json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", type=Path)
    parser.add_argument("--rate", type=int, choices=[16000,48000], default=48000)
    parser.add_argument("--seconds", type=int, default=60)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error("Duration must be 1..3600 seconds")
    record(args.port, args.output, args.seconds, args.rate)
