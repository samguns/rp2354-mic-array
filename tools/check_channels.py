"""Capture/analyze near-port 1 kHz channel mapping in the exposed-port view."""
import argparse
import array
import json
import math
import sys
import wave
from pathlib import Path

EXPECTED = {"top": 1, "left": 2, "bottom": 3, "right": 4, "center": None}


def analyze(path, position):
    with wave.open(str(path), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (4, 2, 16000):
            raise ValueError("Expected four-channel PCM16 at 16 kHz")
        pcm = array.array("h", wav.readframes(wav.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    channels = []
    for ch in range(4):
        x = pcm[ch::4][4000:-4000]
        if len(x) < 8000:
            raise ValueError("Need at least one second of audio")
        dc = sum(x)/len(x)
        x = [v-dc for v in x]
        weights = [.5-.5*math.cos(2*math.pi*i/(len(x)-1)) for i in range(len(x))]
        def amplitude(frequency):
            re = sum(v*w*math.cos(2*math.pi*frequency*i/16000) for i,(v,w) in enumerate(zip(x,weights)))
            im = sum(v*w*math.sin(2*math.pi*frequency*i/16000) for i,(v,w) in enumerate(zip(x,weights)))
            return 2*math.hypot(re,im)/sum(weights)
        tone = amplitude(1000)
        nearby_noise = max(amplitude(f) for f in (930, 960, 1040, 1070))
        channels.append({"channel": ch+1, "tone_amplitude_pcm": tone,
                         "tone_dbfs": 20*math.log10(max(tone,1e-12)/32768),
                         "tone_above_nearby_bins_db": 20*math.log10(max(tone,1e-12)/max(nearby_noise,1e-12)),
                         "rms_pcm": math.sqrt(sum(v*v for v in x)/len(x)),
                         "clipped_samples": sum(v in (-32768,32767) for v in pcm[ch::4])})
    ordered = sorted(channels, key=lambda c:c["tone_amplitude_pcm"], reverse=True)
    margin = ordered[0]["tone_dbfs"]-ordered[1]["tone_dbfs"]
    clear = margin >= 3 and min(c["tone_above_nearby_bins_db"] for c in channels) >= 10
    clear = clear and not any(c["clipped_samples"] for c in channels)
    expected = EXPECTED[position]
    assessment = ("consistent" if expected == ordered[0]["channel"] else "mismatch") if clear else "inconclusive"
    mean_db = sum(c["tone_dbfs"] for c in channels)/4
    for channel in channels:
        channel["relative_to_channel_mean_db"] = channel["tone_dbfs"]-mean_db
    if position == "center":
        usable = min(c["tone_above_nearby_bins_db"] for c in channels) >= 20
        usable = usable and not any(c["clipped_samples"] for c in channels)
        assessment = "usable_level_comparison" if usable else "inconclusive"
    result = {"source_position": position, "expected_channel": expected,
              "strongest_channel": ordered[0]["channel"], "lead_db": margin,
              "assessment": assessment,
              "channel_spread_db": ordered[0]["tone_dbfs"]-ordered[-1]["tone_dbfs"],
              "note": "Acoustic level comparison; source placement, reflections and sensitivity affect levels. No delay or gain calibration applied.",
              "channels": channels}
    path.with_suffix(".mapping.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("position", choices=EXPECTED)
    parser.add_argument("--port", default="COM14")
    parser.add_argument("--input", type=Path, help="Analyze an existing WAV instead of recording")
    parser.add_argument("--output", type=Path, default=Path("build-tests/channel-mapping"))
    args = parser.parse_args()
    path = args.input
    if path is None:
        from stream import record
        args.output.mkdir(parents=True, exist_ok=True)
        path = args.output / (args.position+".wav")
        record(args.port, path, 3)
    print(json.dumps(analyze(path,args.position),indent=2))
