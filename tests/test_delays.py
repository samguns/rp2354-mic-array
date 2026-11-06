import array
import json
import tempfile
import wave
import cmath
import math
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).parents[1]/"tools"))
from delay_check import fft, estimate_pair, expected_lag, analyze
from direction import estimate


class DelayTests(unittest.TestCase):
    def test_fft_roundtrip(self):
        x = [complex(i%7,i%3) for i in range(64)]
        self.assertLess(max(abs(a-b) for a,b in zip(x,fft(fft(x),True))),1e-10)

    def test_fractional_delay_and_sign(self):
        n = 65536
        rng = random.Random(4)
        spectrum = [0j]*n
        for k in range(math.ceil(400*n/48000),math.floor(3200*n/48000)+1):
            spectrum[k] = cmath.exp(2j*math.pi*rng.random())
            spectrum[-k] = spectrum[k].conjugate()
        a = [v.real for v in fft(spectrum,True)]
        for delay in (-2.2,0,1.65):
            shifted = [v*cmath.exp(-2j*math.pi*(k if k<n//2 else k-n)*delay/n)
                       for k,v in enumerate(spectrum)]
            b = [v.real for v in fft(shifted,True)]
            result = estimate_pair(a,b)
            self.assertAlmostEqual(result["lag_samples"],delay,delta=.03)
            self.assertGreater(result["mean_coherence"],.95)

    def test_48k_fractional_delay_and_wav_direction(self):
        rate,n = 48000,65536
        rng = random.Random(48)
        spectrum = [0j]*n
        for k in range(math.ceil(400*n/rate),math.floor(3200*n/rate)+1):
            spectrum[k] = cmath.exp(2j*math.pi*rng.random())
            spectrum[-k] = spectrum[k].conjugate()
        ports = [[0,25,0],[-25,0,0],[0,-25,0],[25,0,0]]
        angle = math.radians(37)
        ux,uy = .8*math.cos(angle),.8*math.sin(angle)
        signals = []
        for x,y,z in ports:
            delay = -(x*ux+y*uy)/1000/343*rate
            shifted = [v*cmath.exp(-2j*math.pi*(k if k<n//2 else k-n)*delay/n)
                       for k,v in enumerate(spectrum)]
            signals.append([v.real for v in fft(shifted,True)])
        scale = 4000/max(abs(x) for signal in signals for x in signal)
        pcm = array.array('h',(round(signals[c][i]*scale) for i in range(n) for c in range(4)))
        if sys.byteorder != 'little': pcm.byteswap()
        geometry = {"microphones":[{"channel":i+1,"nominal_port_mm":p} for i,p in enumerate(ports)],
                    "coordinate_frame":"synthetic exposed-port frame"}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'array.wav'
            with wave.open(str(path),'wb') as w:
                w.setparams((4,2,rate,0,'NONE','not compressed'));w.writeframes(pcm.tobytes())
            report = analyze(path,None,geometry)
            saved = json.loads(path.with_suffix('.delays.json').read_text())
            self.assertEqual(saved['sample_rate_hz'],48000)
            self.assertEqual(report['assessment'],'consistent')
            for pair in report['pairs']:
                a,b = [ports[c-1] for c in pair['channels']]
                expected = -((b[0]-a[0])*ux+(b[1]-a[1])*uy)/1000/343*rate
                self.assertAlmostEqual(pair['lag_samples'],expected,delta=.08)
                self.assertAlmostEqual(pair['lag_us'],pair['lag_samples']/rate*1e6)
            result = estimate(report,geometry)
            self.assertEqual(result['status'],'provisional')
            self.assertAlmostEqual(result['azimuth_degrees'],37,delta=.5)

    def test_48k_delay_sign_and_tone_rejection(self):
        for delay in (-6,5):
            rng = random.Random(9)
            a = [rng.uniform(-1,1) for _ in range(40960)]
            b = ([0]*delay+a)[:len(a)] if delay>=0 else a[-delay:]+[0]*(-delay)
            result = estimate_pair(a,b,rate=48000,max_lag=7)
            self.assertAlmostEqual(result['lag_samples'],delay,delta=.05)
        tone = [math.sin(2*math.pi*1000*i/48000) for i in range(32768)]
        self.assertLess(estimate_pair(tone,tone,rate=48000)['coherent_band_fraction'],.4)

    def test_right_source_arrives_at_right_before_left(self):
        # Exposed-port CH2 left, CH4 right.
        lag = expected_lag([-25,0,0],[25,0,0],[400,0,100])
        self.assertLess(lag,-2)

    def test_16k_input_rejected(self):
        with self.assertRaisesRegex(ValueError,"48000"):
            estimate_pair([],[],rate=16000)
        with self.assertRaisesRegex(ValueError,"48000"):
            expected_lag([0,0,0],[1,0,0],[10,0,0],rate=16000)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'legacy.wav'
            with wave.open(str(path),'wb') as w:
                w.setparams((4,2,16000,0,'NONE','not compressed'));w.writeframes(bytes(8))
            with self.assertRaisesRegex(ValueError,"48 kHz"):
                analyze(path,None,{})

    def test_processed_mono_wav_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'mono_slots.wav'
            with wave.open(str(path),'wb') as w:
                w.setparams((4,2,48000,0,'NONE','not compressed'))
                w.writeframes(bytes(48000*8))
            with self.assertRaisesRegex(ValueError,"Identical channels"):
                analyze(path,None,{})

    def test_silence_rejected(self):
        with self.assertRaisesRegex(ValueError,"No broadband"):
            estimate_pair([0]*32768,[0]*32768)

    def test_single_tone_has_insufficient_band_coverage(self):
        signal = [math.sin(2*math.pi*1000*n/48000) for n in range(32768)]
        result = estimate_pair(signal,signal)
        self.assertLess(result["coherent_band_fraction"],.4)


if __name__ == "__main__":
    unittest.main()
