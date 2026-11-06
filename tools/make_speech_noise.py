"""Build a deterministic phone-playback reference for stationary NR tests."""
import argparse
import array
import json
import math
from pathlib import Path
import random
import wave


def make(source, output):
    rate = 48000
    with wave.open(str(source), 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth()) != (1, 2):
            raise ValueError('Speech source must be mono PCM16')
        source_rate = wav.getframerate()
        samples = array.array('h', wav.readframes(wav.getnframes()))
    speech = []
    for n in range(round(len(samples)*rate/source_rate)):
        position = n*source_rate/rate
        i = min(int(position), len(samples)-1)
        fraction = position-i
        speech.append(samples[i]*(1-fraction)+samples[min(i+1, len(samples)-1)]*fraction)
    rms = math.sqrt(sum(x*x for x in speech)/len(speech))
    speech = [x/rms*.10 for x in speech]
    rng = random.Random(20261016)
    low = high = 0.0
    a = 1-math.exp(-2*math.pi*3500/rate)
    b = 1-math.exp(-2*math.pi*200/rate)
    noise = []
    for _ in range(5*rate):
        low += a*(rng.uniform(-1, 1)-low)
        high += b*(low-high)
        noise.append(low-high)
    rms = math.sqrt(sum(x*x for x in noise)/len(noise))
    noise = [x/rms*.07 for x in noise]
    output_samples = array.array('f')
    segments = []
    for label, count in [('noise_lead_in', 30*rate), ('speech_plus_noise_1', len(speech)),
                         ('noise_only', 5*rate), ('speech_plus_noise_2', len(speech)),
                         ('noise_tail', 5*rate), ('silence', 5*rate)]:
        start = len(output_samples)
        segments.append(dict(label=label, start_seconds=start/rate, duration_seconds=count/rate))
        for n in range(count):
            value = 0 if label == 'silence' else noise[(start+n) % len(noise)]
            if label.startswith('speech_plus'):
                value += speech[n]
            # Fade noise onset/offset over 20 ms; speech is unchanged.
            if label == 'noise_lead_in':
                value *= min(1, n/(.02*rate))
            if label == 'noise_tail':
                value *= min(1, (count-1-n)/(.02*rate))
            output_samples.append(value)
    peak = max(map(abs, output_samples))
    gain = .8/max(peak, .8)
    pcm = array.array('h', (round(x*gain*32767) for x in output_samples))
    output.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(output), 'wb') as wav:
        wav.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(pcm.tobytes())
    report = dict(rate=rate, duration_seconds=len(pcm)/rate, peak_pcm16=max(map(abs, pcm)),
                  source_speech=str(source), noise_seed=20261016,
                  nominal_clip_rms_speech_to_noise_db=20*math.log10(.10/.07),
                  segments=segments)
    output.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('speech', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    make(args.speech, args.output)
