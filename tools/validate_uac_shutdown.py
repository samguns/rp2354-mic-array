"""Windows hardware regression: stop playback repeatedly during continuous capture.

Requires an idle board, a free shell port and exclusive access to its audio
endpoints. Uses silent playback and the temporary UAC diagnostic pattern.
Does not save settings. Any recovery, discontinuity or pattern error fails.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import wave

import serial

from verify_uac_pattern import verify

TOOLS = Path(__file__).resolve().parent


def shell(port, command):
    with serial.Serial(port, 115200, timeout=.2) as device:
        device.write(b'\x03\r')
        time.sleep(.1)
        device.reset_input_buffer()
        device.write((command + '\r').encode())
        device.flush()
        time.sleep(.35)
        return device.read_all().decode(errors='replace')


def validate(port, cycles, output):
    output.mkdir(parents=True, exist_ok=True)
    recording = output/'capture.wav'
    if recording.exists() or recording.with_suffix('.partial.wav').exists():
        raise RuntimeError('Use a new output directory to preserve previous evidence')
    for command in ['uacstats', 'spkstats']:
        if not re.search(r'\benabled=0\b', shell(port, command)):
            raise RuntimeError('Close existing audio streams before testing')
    if 'UAC_PATTERN ON' not in shell(port, 'uacpattern on'):
        raise RuntimeError('Could not enable diagnostic pattern')
    capture = None
    try:
        with (output/'capture.log').open('w') as log:
            capture = subprocess.Popen([
                sys.executable, '-u', str(TOOLS/'uac_wasapi.py'), str(recording),
                '--seconds', str(cycles*2 + 6)], stdout=log, stderr=subprocess.STDOUT)
            time.sleep(1)
            for cycle in range(cycles):
                started = time.monotonic()
                playback = subprocess.run([
                    sys.executable, str(TOOLS/'uac_playback.py'), '--seconds', '1',
                    '--peak', '0', '--output', str(output/f'playback-{cycle+1:03}.json')],
                    capture_output=True, text=True, timeout=15)
                (output/f'playback-{cycle+1:03}.log').write_text(playback.stdout + playback.stderr)
                if playback.returncode or capture.poll() is not None:
                    raise RuntimeError(f'Audio stopped at cycle {cycle+1}; inspect logs')
                print(f'Playback stop {cycle+1}/{cycles}', flush=True)
                time.sleep(max(0, 2 - (time.monotonic() - started)))
            capture.wait(timeout=15)
            if capture.returncode:
                raise RuntimeError('Capture failed; inspect capture.log')

        data = json.loads(recording.with_suffix('.json').read_text())
        if data['device_position_gaps'] or data['packet_flags']:
            raise RuntimeError('Capture reported discontinuities or packet flags')
        if data['frames'] != (cycles*2 + 6)*48000:
            raise RuntimeError('Incomplete recording')
        with wave.open(str(recording), 'rb') as audio:
            result = verify(audio.readframes(audio.getnframes()))
        health = {command: shell(port, command) for command in
                  ['mic stats', 'uacstats', 'spkstats']}
        (output/'health.json').write_text(json.dumps(health, indent=2))
        for text in health.values():
            for key in ['dropped', 'faults', 'underruns', 'overruns', 'gaps',
                        'send_errors', 'pool_errors', 'usb_recoveries']:
                match = re.search(r'\b' + key + r'=(\d+)', text)
                if match and int(match[1]):
                    raise RuntimeError(f'Firmware reported {key}={match[1]}')
        for command in ['uacstats', 'spkstats']:
            if not re.search(r'\benabled=0\b', health[command]):
                raise RuntimeError(f'{command}: terminal remained active')
        if not re.search(r'\busb_recoveries=0\b', health['uacstats']):
            raise RuntimeError('Missing or nonzero USB recovery counter')
        if 'ready=1' not in health['mic stats']:
            raise RuntimeError('Microphone acquisition is not ready')
        result['playback_stops'] = cycles
    finally:
        if capture is not None and capture.poll() is None:
            capture.terminate()
            capture.wait(timeout=5)
        # Give exceptional device recovery time to re-enumerate before cleanup.
        for attempt in range(10):
            try:
                if 'UAC_PATTERN OFF' in shell(port, 'uacpattern off'):
                    break
            except serial.SerialException:
                pass
            time.sleep(1)
        else:
            print('Could not clear temporary pattern; reboot the board.', file=sys.stderr)
            if sys.exc_info()[0] is None:
                raise RuntimeError('Diagnostic-pattern cleanup failed')
    (output/'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--cycles', type=int, default=30)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 1000:
        parser.error('--cycles must be between 1 and 1000')
    validate(args.port, args.cycles, args.output)
