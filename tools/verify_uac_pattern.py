"""Verify UAC diagnostic channel order and every frame after startup silence."""
import argparse
import array
import json
import sys
import wave
from pathlib import Path


def verify(pcm):
    samples = array.array('H',pcm)
    if sys.byteorder != 'little': samples.byteswap()
    if len(samples)%4: raise ValueError('Incomplete four-channel frame')
    first = None;previous = None;errors = gaps = 0;zero_frames = 0
    for i in range(len(samples)//4):
        a,b,c,d = samples[i*4:i*4+4]
        if first is None and not any((a,b,c,d)):
            zero_frames += 1;continue
        if first is None: first = i
        if (b,c,d)!=(a^0x1357,a^0x2468,a^0x55aa): errors += 1
        if previous is not None and a != (previous+1)&65535: gaps += 1
        previous = a
    if first is None or zero_frames>3200 or errors or gaps:
        raise ValueError(f'Pattern failed: startup={zero_frames}, channel_errors={errors}, sequence_errors={gaps}')
    return {'result':'PASS','startup_silence_frames':zero_frames,
            'validated_frames':len(samples)//4-first,'channel_errors':errors,'sequence_errors':gaps}


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__);parser.add_argument('wav',type=Path);args = parser.parse_args()
    with wave.open(str(args.wav),'rb') as wav:
        if (wav.getnchannels(),wav.getsampwidth())!=(4,2) or wav.getframerate() not in (16000,48000):
            raise ValueError('Expected four-channel PCM16 at 16 or 48 kHz')
        result = verify(wav.readframes(wav.getnframes()))
    args.wav.with_suffix('.pattern.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))
