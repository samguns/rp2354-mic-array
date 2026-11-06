"""Compile the production C estimator and compare it with independent host DSP."""
import array
import cmath
import itertools
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from delay_check import fft, estimate_pair
from direction import estimate

PORTS = [[0,25,0],[-25,0,0],[0,-25,0],[25,0,0]]
GEOMETRY = {'coordinate_frame':'exposed ports','microphones':[
    {'channel':i+1,'nominal_port_mm':p} for i,p in enumerate(PORTS)]}


class OnboardDoaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('gcc'):
            raise unittest.SkipTest('Native gcc is needed for production C comparison')
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        cls.exe = cls.root/'doa.exe'
        repo = Path(__file__).parents[1]
        subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(repo/'src'),
                        str(repo/'tests/doa_runner.c'),str(repo/'src/doa.c'),'-lm','-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_c(self, signals):
        pcm = array.array('h',(round(signals[c][i]) for i in range(8192) for c in range(4)))
        path = self.root/'input.pcm';path.write_bytes(pcm.tobytes())
        return json.loads(subprocess.check_output([str(self.exe),str(path)],text=True))

    def broadband(self, angle, lateral=.8):
        n=16384;rng=random.Random(48);spectrum=[0j]*n
        for k in range(math.ceil(400*n/48000),math.floor(3200*n/48000)+1):
            spectrum[k]=cmath.exp(2j*math.pi*rng.random());spectrum[-k]=spectrum[k].conjugate()
        ux,uy=lateral*math.cos(math.radians(angle)),lateral*math.sin(math.radians(angle))
        signals=[]
        for x,y,z in PORTS:
            delay=-(x*ux+y*uy)/1000/343*48000
            shifted=[v*cmath.exp(-2j*math.pi*(k if k<n//2 else k-n)*delay/n)
                     for k,v in enumerate(spectrum)]
            signals.append([v.real for v in fft(shifted,True)][:8192])
        gain=4000/max(abs(v) for s in signals for v in s)
        return [[round(v*gain) for v in s] for s in signals]

    def test_quadrants_and_host_agreement(self):
        for angle in (0,90,180,270,37):
            signals=self.broadband(angle);c=self.run_c(signals)
            self.assertEqual(c['rejected'],0)
            self.assertLess(abs((c['angle']-angle+180)%360-180),.5)
            pairs=[]
            for p,(a,b) in enumerate(itertools.combinations(range(4),2)):
                bound=math.dist(PORTS[a],PORTS[b])/1000/343*48000
                h=estimate_pair(signals[a],signals[b],max_lag=bound,window_size=2048)
                self.assertAlmostEqual(c['pairs'][p]['lag_samples'],h['lag_samples'],delta=.03)
                self.assertAlmostEqual(c['pairs'][p]['gcc_score'],h['gcc_score'],delta=.003)
                self.assertAlmostEqual(c['pairs'][p]['mean_coherence'],h['mean_coherence'],delta=.003)
                h['channels']=[a+1,b+1];pairs.append(h)
            h=estimate({'sample_rate_hz':48000,'pairs':pairs},GEOMETRY)
            self.assertEqual(h['status'],'provisional')
            self.assertLess(abs((c['angle']-h['azimuth_degrees']+180)%360-180),.3)

    def test_reject_silence_tone_clipping_and_normal(self):
        self.assertNotEqual(self.run_c([[0]*8192 for _ in range(4)])['rejected']&1,0)
        tone=[round(1000*math.sin(2*math.pi*1000*i/48000)) for i in range(8192)]
        self.assertNotEqual(self.run_c([tone]*4)['rejected']&4,0)
        self.assertNotEqual(self.run_c(self.broadband(0,0))['rejected']&32,0)
        s=self.broadband(45);s[0][100]=32767
        self.assertNotEqual(self.run_c(s)['rejected']&2,0)

    def test_reject_impossible_delays_and_incoherent_noise(self):
        s=self.broadband(0);s[3]=[0]*12+s[3][:-12]
        self.assertNotEqual(self.run_c(s)['rejected']&8,0)
        rng=random.Random(10)
        s=[[rng.randrange(-1000,1001) for _ in range(8192)] for _ in range(4)]
        self.assertNotEqual(self.run_c(s)['rejected']&4,0)
