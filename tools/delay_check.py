"""Broadband pair-delay validation using the exposed-port coordinate frame."""
import argparse
import array
import cmath
import itertools
import json
import math
import random
import sys
import wave
from pathlib import Path


def fft(values, inverse=False):
    z = list(values)
    n = len(z)
    if not n or n & (n-1):
        raise ValueError("FFT size must be a power of two")
    j = 0
    for i in range(1,n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            z[i],z[j] = z[j],z[i]
    size = 2
    while size <= n:
        step = cmath.exp((2j if inverse else -2j)*math.pi/size)
        for start in range(0,n,size):
            phase = 1
            for k in range(size//2):
                v = phase*z[start+k+size//2]
                u = z[start+k]
                z[start+k],z[start+k+size//2] = u+v,u-v
                phase *= step
        size *= 2
    return [v/n for v in z] if inverse else z


def make_reference(path):
    rate,n = 48000,65536
    rng = random.Random(20261014)
    spectrum = [0j]*n
    for k in range(math.ceil(350*n/rate),math.floor(3500*n/rate)+1):
        spectrum[k] = cmath.exp(2j*math.pi*rng.random())
        spectrum[-k] = spectrum[k].conjugate()
    signal = [v.real for v in fft(spectrum,True)]
    scale = 8192/max(abs(v) for v in signal)
    audio = array.array("h")
    count = 30*rate
    for i in range(count):
        fade = min(1,i/480,(count-1-i)/480)
        audio.append(round(signal[i % n]*scale*fade))
    if sys.byteorder != "little":
        audio.byteswap()
    with wave.open(str(path),"wb") as wav:
        wav.setparams((1,2,rate,0,"NONE","not compressed"))
        wav.writeframes(audio.tobytes())


def estimate_pair(a,b,rate=48000,max_lag=None,window_size=8192):
    """Positive lag means B arrives later than A: corr(A[n], B[n+lag])."""
    if rate != 48000:
        raise ValueError("Expected sample rate 48000 Hz")
    if max_lag is None:
        max_lag = 9
    n = window_size
    if n not in (2048,8192):
        raise ValueError("Supported FFT windows are 2048 (onboard comparison) or 8192")
    window = [.5-.5*math.cos(2*math.pi*i/(n-1)) for i in range(n)]
    bins = list(range(math.ceil(400*n/rate),math.floor(3200*n/rate)+1))
    cross = [0j]*len(bins)
    power_a,power_b = [0.0]*len(bins),[0.0]*len(bins)
    starts = list(range(0,min(len(a),len(b))-n+1,n))
    if len(starts) < 4:
        raise ValueError("Need at least four analysis windows")
    # Up to 16 non-overlapping windows spread across the recording.
    starts = starts[::max(1,len(starts)//16)][:16]
    for start in starts:
        x,y = a[start:start+n],b[start:start+n]
        mx,my = sum(x)/n,sum(y)/n
        X = fft([(v-mx)*w for v,w in zip(x,window)])
        Y = fft([(v-my)*w for v,w in zip(y,window)])
        for i,k in enumerate(bins):
            cross[i] += X[k].conjugate()*Y[k]
            power_a[i] += abs(X[k])**2
            power_b[i] += abs(Y[k])**2
    floor = max(abs(v) for v in cross)*.01
    if floor <= 0:
        raise ValueError("No broadband signal")
    # Regularized GCC-PHAT, with low-coherence bins downweighted.
    weights = [abs(v)**2/max(pa*pb,1e-30) for v,pa,pb in zip(cross,power_a,power_b)]
    normalized = [v/max(abs(v),floor)*w for v,w in zip(cross,weights)]
    def score(lag):
        return sum((v*cmath.exp(2j*math.pi*k*lag/n)).real
                   for k,v in zip(bins,normalized))/max(sum(weights),1e-30)
    grid = [i/32 for i in range(-math.ceil((max_lag+3)*32),math.ceil((max_lag+3)*32)+1)]
    scores = [score(lag) for lag in grid]
    best = max(range(len(grid)),key=lambda i:scores[i])
    lag = grid[best]
    if 0 < best < len(grid)-1:
        lo,mid,hi = scores[best-1:best+2]
        if lo-2*mid+hi:
            lag += .5*(lo-hi)/(lo-2*mid+hi)/32
    return {"lag_samples":lag,"lag_us":lag/rate*1e6,
            "gcc_score":scores[best],"mean_coherence":sum(weights)/len(weights),
            "coherent_band_fraction":sum(w >= .5 and abs(v) > floor for w,v in zip(weights,cross))/len(weights),
            "search_boundary":best in (0,len(grid)-1),"windows":len(starts)}


def expected_lag(a,b,source,rate=48000,speed=343):
    if rate != 48000:
        raise ValueError("Expected sample rate 48000 Hz")
    return (math.dist(b,source)-math.dist(a,source))/1000/speed*rate


def analyze(path,source,geometry,write_report=True):
    with wave.open(str(path),"rb") as wav:
        rate = wav.getframerate()
        if (wav.getnchannels(),wav.getsampwidth()) != (4,2) or rate != 48000:
            raise ValueError("Expected four-channel PCM16 at 48 kHz")
        pcm = array.array("h",wav.readframes(wav.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    trim = rate//4
    signals = [pcm[ch::4][trim:-trim] for ch in range(4)]
    if all(signals[0] == signal for signal in signals[1:]):
        raise ValueError("Identical channels: use raw array capture with NR and beamforming off")
    ports = [m["nominal_port_mm"] for m in geometry["microphones"]]
    results = []
    for a,b in itertools.combinations(range(4),2):
        bound = math.dist(ports[a],ports[b])/1000/343*rate
        result = estimate_pair(signals[a],signals[b],rate=rate,max_lag=bound)
        predicted = expected_lag(ports[a],ports[b],source,rate=rate) if source is not None else None
        result.update({"channels":[a+1,b+1],"expected_samples":predicted,
                       "error_samples":result["lag_samples"]-predicted if predicted is not None else None,
                       "physical_bound_samples":bound})
        result["consistent"] = ((predicted is None or abs(result["error_samples"]) <= 1.2)
                                and abs(result["lag_samples"]) <= bound+.45
                                and result["mean_coherence"] >= .5
                                and result["coherent_band_fraction"] >= .4
                                and result["gcc_score"] >= .5
                                and not result["search_boundary"])
        results.append(result)
    clipped = [sum(v in (-32768,32767) for v in pcm[ch::4]) for ch in range(4)]
    lags = {tuple(p["channels"]):p["lag_samples"] for p in results}
    closure = [{"channels":[a,b,c],
                "residual_samples":lags[a,b]+lags[b,c]-lags[a,c]}
               for a,b,c in itertools.combinations(range(1,5),3)]
    report = {"sample_rate_hz":rate,"source_mm":source,"coordinate_frame":geometry["coordinate_frame"],
              "positive_lag":"second channel arrives later than first",
              "sound_speed_m_s":343,"clipped_samples":clipped,
              "assessment":"consistent" if all(p["consistent"] for p in results) and not any(clipped) else "needs_review",
              "pairs":results,"triangle_closure":closure,
              "note":"Screening tolerance 25 us; source location approximate, room reflections uncalibrated. No azimuth claim or channel correction applied."}
    if write_report:
        path.with_suffix(".delays.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make-reference",type=Path)
    parser.add_argument("--source-mm",nargs=3,type=float,metavar=("X","Y","Z"))
    parser.add_argument("--input",type=Path)
    parser.add_argument("--output",type=Path,default=Path("build-tests/delay-validation/center.wav"))
    args = parser.parse_args()
    if args.make_reference:
        make_reference(args.make_reference)
        print(f"Saved {args.make_reference}: 30 s, 350-3500 Hz broadband reference")
    else:
        if args.source_mm is None:
            parser.error("Specify approximate --source-mm X Y Z in exposed-port coordinates")
        path = args.input or args.output
        if args.input is None:
            path.parent.mkdir(parents=True,exist_ok=True)
            from uac_wasapi import capture
            capture(path,3,48000)
        geometry = json.loads(Path("docs/array-geometry.json").read_text(encoding="utf-8"))
        print(json.dumps(analyze(path,args.source_mm,geometry),indent=2))
