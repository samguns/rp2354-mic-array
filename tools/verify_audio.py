"""Verify actual I2S GPIO framing and record the acoustic result over USB CDC."""
import argparse
import array
import json
import math
import re
import struct
import time
import wave
from pathlib import Path

from record import read_matching, read_pcm


def decode_i2s(words, audio_format="i2s"):
    """Decode one-bit-delayed I2S from samples taken on BCLK rising edges."""
    triples = [(word >> shift) & 7 for word in words for shift in range(21, -1, -3)]
    if not all(t & 2 for t in triples):
        raise ValueError("Probe sampled BCLK low")
    edges = [i for i in range(1, len(triples)) if (triples[i] ^ triples[i-1]) & 4]
    periods = [b - a for a, b in zip(edges, edges[1:])]
    if not periods or any(p != 32 for p in periods):
        raise ValueError(f"LRCLK slot lengths are not all 32 bits: {sorted(set(periods))}")
    decoded = []
    for edge in edges:
        delay = 1 if audio_format == "i2s" else 0
        if edge + delay + 32 > len(triples):
            break
        value = 0
        for t in triples[edge+delay:edge+delay+32]:
            value = (value << 1) | (t & 1)
        if value & 0xffff:
            raise ValueError("Nonzero low 16 bits: PCM16 was not transmitted in the MSBs")
        sample = struct.unpack(">h", struct.pack(">H", value >> 16))[0]
        decoded.append((int(bool(triples[edge] & 4)), sample))
    if len(decoded) < 100:
        raise ValueError("Too few decoded slots")
    return decoded


def validate_tone(decoded, frequency, peak=512):
    # Probe is armed before playback; the first full slot is right frame 0.
    maximum_error = 0
    for index, (channel, sample) in enumerate(decoded):
        word_index = index + 1
        frame = word_index // 2
        if channel != word_index % 2:
            raise ValueError("Left/right sequence incorrect")
        gain = max(0, min(frame, 15872 - frame, 256)) / 256
        expected = peak * math.sin(2 * math.pi * frequency * frame / 16000) * gain
        maximum_error = max(maximum_error, abs(sample - expected))
    # Small interpolation and PCM integer rounding errors are expected.
    if maximum_error > 5:
        raise ValueError(f"I2S samples differ from the requested sine: max error {maximum_error:.2f}")
    return {"slots": len(decoded), "bits_per_slot": 32, "maximum_pcm_error": maximum_error}


def spectrum(pcm, rate, frequency):
    samples = array.array("h", pcm)
    results = []
    for channel in range(4):
        # Exclude fade-in, startup, and recording boundaries.
        x = samples[channel::4][2000:14000]
        mean = sum(x) / len(x)
        window = [0.5 - 0.5 * math.cos(2 * math.pi * n / (len(x)-1)) for n in range(len(x))]
        def amplitude(f):
            re_part = sum((v-mean)*w*math.cos(2*math.pi*f*n/rate) for n,(v,w) in enumerate(zip(x,window)))
            im_part = sum((v-mean)*w*math.sin(2*math.pi*f*n/rate) for n,(v,w) in enumerate(zip(x,window)))
            return 2 * math.hypot(re_part, im_part) / sum(window)
        fundamental = amplitude(frequency)
        third = amplitude(3 * frequency)
        results.append({"channel": channel+1,
                        "rms_pcm": math.sqrt(sum((v-mean)**2 for v in x)/len(x)),
                        "fundamental_pcm": fundamental, "third_harmonic_pcm": third,
                        "third_relative_db": 20*math.log10(max(third, 1e-12)/max(fundamental, 1e-12))})
    return results


def run(port_name, output, frequencies, peak=512, audio_format="i2s"):
    import serial
    output.mkdir(parents=True, exist_ok=True)
    report = {"settings": {"peak_pcm16": peak, "format": audio_format, "frequencies_hz": frequencies}}
    with serial.Serial(port_name, 115200, timeout=0.3) as port:
        port.write(b"\x03\r")
        time.sleep(0.3)
        port.reset_input_buffer()
        for frequency in frequencies:
            port.write(b"mic record 1000\r")
            read_matching(port, r"^RECORDING$")
            port.write(f"speaker verify {frequency} {peak} {audio_format}\r".encode())
            count = int(read_matching(port, r"I2S_BEGIN (\d+)", 5)[1])
            hex_lines = []
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                line = port.readline().decode("ascii", errors="replace").strip()
                if line == "I2S_END":
                    break
                if line:
                    if not re.fullmatch(r"[0-9a-f]+", line) or len(line) % 8:
                        raise ValueError(f"Malformed probe output: {line[:80]}")
                    hex_lines.append(line)
            else:
                raise TimeoutError("I2S probe transfer timed out")
            text = "".join(hex_lines)
            if count != 2048 or len(text) != count * 8:
                raise ValueError("Incomplete I2S probe transfer")
            words = [int(text[i:i+8],16) for i in range(0,len(text),8)]
            (output / f"i2s-{frequency}.json").write_text(json.dumps(words))
            digital = validate_tone(decode_i2s(words, audio_format), frequency, peak)
            read_matching(port, r"Speaker test complete")
            port.write(b"mic status\r")
            read_matching(port, r"RECORD DONE \d+ \d+")
            port.write(b"mic dump\r")
            rate, channels, pcm = read_pcm(port)
            with wave.open(str(output / f"tone-{frequency}.wav"), "wb") as wav:
                wav.setnchannels(channels); wav.setsampwidth(2); wav.setframerate(rate); wav.writeframes(pcm)
            actual_rate = rate
            acoustic = spectrum(pcm, actual_rate, frequency)
            report[str(frequency)] = {"digital": digital, "acoustic": acoustic}
            print(f"{frequency} Hz: I2S PASS; {digital}", flush=True)
            print(json.dumps(acoustic), flush=True)
        port.write(b"mic stats\r")
        health = read_matching(port, r"rate=\d+ channels=4 ready=\d+ blocks=\d+ dropped=\d+ faults=\d+ max_process_us=\d+")[0]
        report["health"] = health
        print(health, flush=True)
    (output / "report.json").write_text(json.dumps(report, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--output", type=Path, default=Path("build-tests/audio-validation"))
    parser.add_argument("--frequencies", type=int, nargs="+", default=[500,750,1000])
    parser.add_argument("--peak", type=int, default=512, help="PCM16 peak, 0..512; zero tests digital silence")
    parser.add_argument("--format", choices=["i2s", "left"], default="i2s")
    args = parser.parse_args()
    if any(f < 200 or f > 2000 for f in args.frequencies):
        parser.error("Frequencies must be 200..2000 Hz")
    if not 0 <= args.peak <= 512:
        parser.error("Peak must be 0..512")
    run(args.port, args.output, args.frequencies, args.peak, args.format)
