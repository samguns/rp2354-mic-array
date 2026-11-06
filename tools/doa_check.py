"""Capture a retained onboard DOA frame and compare the same samples on the host."""
import argparse
import array
import itertools
import json
import math
from pathlib import Path
import re
import sys
import time
import wave
from delay_check import estimate_pair
from direction import estimate
from raw_pdm import read_hex


def command(port,text,timeout=10):
    port.write((text+'\r').encode())
    deadline=time.monotonic()+timeout;data=b''
    while time.monotonic()<deadline:
        data+=port.read(4096)
        # Ctrl-C/CR can leave an earlier prompt in the CDC stream. Wait for
        # the complete result before accepting its following prompt.
        end=data.rfind(b'DOA_REJECT bits:')
        if end>=0 and data.rfind(b'uart:~$')>end:
            return data.decode('ascii',errors='replace')
    raise TimeoutError(f'Shell command timed out: {text}')


def parse_onboard(text):
    pairs=[]
    for line in text.splitlines():
        match=re.search(r'DOA_PAIR (\d) (\d) lag_milli_samples=(-?\d+) score_milli=(-?\d+) coherence_milli=(\d+) band_milli=(\d+)',line)
        if match:
            a,b,lag,score,coherence,band=map(int,match.groups())
            pairs.append(dict(channels=[a,b],lag_samples=lag/1000,gcc_score=score/1000,
                              mean_coherence=coherence/1000,coherent_band_fraction=band/1000))
    match=re.search(r'DOA_RESULT status=(\w+) angle_mdeg=(-?\d+) reject_mask=(\d+)',text)
    if len(pairs)!=6 or not match:
        raise ValueError('Missing onboard DOA result: '+text[-600:])
    return dict(status=match[1],angle_degrees=int(match[2])/1000 if int(match[2])>=0 else None,
                reject_mask=int(match[3]),pairs=pairs)


def compare(pcm,onboard,geometry):
    signals=[pcm[c::4] for c in range(4)];pairs=[];errors=[]
    ports=[m['nominal_port_mm'] for m in geometry['microphones']]
    for p,(a,b) in enumerate(itertools.combinations(range(4),2)):
        bound=math.dist(ports[a],ports[b])/1000/343*48000
        result=estimate_pair(signals[a],signals[b],max_lag=bound,window_size=2048)
        result['channels']=[a+1,b+1];pairs.append(result)
        errors.append(abs(result['lag_samples']-onboard['pairs'][p]['lag_samples']))
    host=estimate(dict(sample_rate_hz=48000,pairs=pairs,
                       clipped_samples=[sum(v in (-32768,32767) for v in s) for s in signals]),geometry)
    return dict(onboard=onboard,host=host,max_pair_difference_samples=max(errors),
                comparison='PASS' if max(errors)<.05 else 'needs_review',
                note='Same 8192 raw frames; four 2048-point Hann windows, 400–3200 Hz. Numerical agreement does not validate acoustic direction accuracy.')


def capture(port_name,output):
    import serial
    output.parent.mkdir(parents=True,exist_ok=True)
    with serial.Serial(port_name,115200,timeout=.1) as port:
        port.set_buffer_size(rx_size=262144)
        port.write(b'\x03\r');time.sleep(.3);port.reset_input_buffer()
        text=command(port,'doa capture')
        onboard=parse_onboard(text)
        output.with_suffix('.shell.txt').write_text(text,encoding='utf-8')
        port.write(b'doa dump\r')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            line=port.readline().decode('ascii',errors='replace').strip()
            if line.startswith('DOA_PCM_BEGIN'):
                if line!='DOA_PCM_BEGIN rate=48000 channels=4 frames=8192':
                    raise ValueError('Unexpected DOA frame geometry: '+line)
                break
            if 'Run doa capture' in line:
                raise ValueError('Close UAC recording apps before dumping a DOA frame')
        else:
            raise TimeoutError('DOA export did not start')
        values=read_hex(port,'DOA_PCM_END',4,8192*4)
    pcm=array.array('h',(v if v<32768 else v-65536 for v in values))
    little=array.array('h',pcm)
    if sys.byteorder!='little':little.byteswap()
    with wave.open(str(output),'wb') as wav:
        wav.setparams((4,2,48000,0,'NONE','not compressed'));wav.writeframes(little.tobytes())
    geometry=json.loads((Path(__file__).resolve().parents[1]/'docs/array-geometry.json').read_text(encoding='utf-8'))
    report=compare(pcm,onboard,geometry)
    output.with_suffix('.doa.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM14')
    parser.add_argument('--output',type=Path,default=Path('build-tests/doa/host-comparison.wav'))
    args=parser.parse_args();capture(args.port,args.output)
