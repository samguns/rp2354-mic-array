"""Capture raw PDM and compare an independent bitwise decimator with firmware PCM."""
import argparse
import json
import math
import time
from pathlib import Path
from record import read_matching


def read_hex(port, end_marker, digits, count):
    data = ""
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        if line == end_marker:
            if len(data) != digits * count:
                raise ValueError("Truncated raw export")
            return [int(data[i:i+digits],16) for i in range(0,len(data),digits)]
        if line:
            if any(c not in "0123456789abcdef" for c in line) or len(line)%digits:
                raise ValueError(f"Invalid raw export line: {line[:80]}")
            data += line
            if len(data) > digits * count:
                raise ValueError("Oversized raw export")
    raise TimeoutError("Raw export did not finish")


def analyse(words, firmware_pcm, pdm_hz, ratio):
    if ratio == 64:
        from pdm48_reference import decode
        decoded48 = decode(words)
    integrators = [[0,0,0] for _ in range(4)]
    delays = [[0,0,0] for _ in range(4)]
    reconstructed = []
    ones = [0]*4
    transitions = [0]*4
    previous = [None]*4
    differences = [0]*3
    ticks = 0
    for word in words:
        for bit in range(8):
            nibble = (word >> (4*bit)) & 15
            for channel in range(4):
                v = (nibble >> channel) & 1
                ones[channel] += v
                if previous[channel] is not None:
                    transitions[channel] += v != previous[channel]
                previous[channel] = v
                if channel:
                    differences[channel-1] += v != (nibble & 1)
                i = integrators[channel]
                i[0] += 2*v-1; i[1] += i[0]; i[2] += i[1]
            ticks += 1
            if ticks % ratio == 0:
                for channel in range(4):
                    value = integrators[channel][2]
                    for k in range(3):
                        old = value; value -= delays[channel][k]; delays[channel][k] = old
                    divisor = ratio**3 // 32768
                    sample = value//divisor if value >= 0 else -((-value)//divisor)
                    reconstructed.append(max(-32768,min(32767,sample)))
    if ratio == 64:
        reconstructed = decoded48
    if len(firmware_pcm) != len(reconstructed):
        raise ValueError("PCM frame count mismatch")
    # A CIC3 has finite memory over three decimated frames. Discard four
    # frames because the snapshot starts with unknown firmware filter state.
    errors = [abs(a-b) for a,b in zip(reconstructed[(512 if ratio==64 else 16):],firmware_pcm[(512 if ratio==64 else 16):])]
    frames = ticks//ratio
    channels = []
    rate = pdm_hz/ratio
    for c in range(4):
        x = reconstructed[c::4][8:]
        def amplitude(f):
            re = sum(v*math.cos(2*math.pi*f*n/rate) for n,v in enumerate(x))
            im = sum(v*math.sin(2*math.pi*f*n/rate) for n,v in enumerate(x))
            return 2*math.hypot(re,im)/len(x)
        channels.append({"channel":c+1,"ones_fraction":ones[c]/ticks,
                         "transition_fraction":transitions[c]/(ticks-1),
                         "amplitude_1000_pcm":amplitude(1000),"amplitude_3000_pcm":amplitude(3000)})
    return {"frames":frames,"pcm_max_error_after_settling":max(errors),
            "different_from_channel1":[v/ticks for v in differences],"channels":channels}


def capture(port_name, output):
    import serial
    output.mkdir(parents=True,exist_ok=True)
    with serial.Serial(port_name,115200,timeout=.3) as port:
        port.write(b"\x03\r");time.sleep(.3);port.reset_input_buffer()
        port.write(b"mic raw\r")
        clock = read_matching(port,r"PDM_CLOCK edges=(\d+) elapsed_us=(\d+)")
        edges,elapsed = map(int,clock.groups())
        header = read_matching(port,r"RAW_BEGIN (\d+) (\d+) (\d+)")
        pdm_hz,ratio,count = map(int,header.groups())
        if (count,ratio) not in ((6144,96),(4096,64)): raise ValueError("Unexpected raw geometry")
        words = read_hex(port,"RAW_END",8,count)
        count_pcm = int(read_matching(port,r"RAW_PCM_BEGIN (\d+)")[1])
        if count_pcm != 2048: raise ValueError("Unexpected PCM geometry")
        pcm = read_hex(port,"RAW_PCM_END",4,count_pcm)
        pcm = [v if v<32768 else v-65536 for v in pcm]
        phase_count = int(read_matching(port,r"PHASE_BEGIN (\d+)")[1])
        if phase_count != 2048: raise ValueError("Unexpected phase geometry")
        phase_words = read_hex(port,"PHASE_END",8,phase_count)
        trace_header = read_matching(port,r"CLOCK_TRACE_BEGIN (\d+) (\d+)")
        trace_count,sys_hz = map(int,trace_header.groups())
        if trace_count != 2048: raise ValueError("Unexpected clock trace geometry")
        trace_words = read_hex(port,"CLOCK_TRACE_END",8,trace_count)
        port.write(b"mic stats\r")
        health = read_matching(port,r"rate=\d+ channels=4 ready=\d+ blocks=\d+ dropped=\d+ faults=\d+ max_process_us=\d+")[0]
    (output/"raw.json").write_text(json.dumps({"pdm_hz":pdm_hz,"ratio":ratio,"words":words,"pcm":pcm}))
    result = analyse(words,pcm,pdm_hz,ratio)
    phase_pairs = [( (w >> shift)&15, (w >> (shift+4))&15)
                   for w in phase_words for shift in range(0,32,8)]
    phase_results=[]
    for c in range(4):
        low=[(a>>c)&1 for a,b in phase_pairs]
        high=[(b>>c)&1 for a,b in phase_pairs]
        phase_results.append({"channel":c+1,"low_ones":sum(low)/len(low),"high_ones":sum(high)/len(high),
                              "low_vs_high_difference":sum(a!=b for a,b in zip(low,high))/len(low),
                              "low_vs_previous_high_difference":sum(a!=b for a,b in zip(low[1:],high[:-1]))/(len(low)-1)})
    result["phases"]=phase_results
    (output/"phase.json").write_text(json.dumps(phase_words))
    clock_bits=[(w>>b)&1 for w in trace_words for b in range(32)]
    runs=[]
    level=clock_bits[0];length=0
    for v in clock_bits:
        if v==level: length+=1
        else: runs.append((level,length));level=v;length=1
    # Exclude partial runs at both ends.
    runs=runs[1:]
    highs=[n for v,n in runs if v];lows=[n for v,n in runs if not v]
    if not highs or not lows: raise ValueError("Clock trace is stuck")
    periods=[runs[i][1]+runs[i+1][1] for i in range(len(runs)-1)]
    result["clock_trace"]={"sys_hz":sys_hz,"high_cycles":sorted(set(highs)),
                           "low_cycles":sorted(set(lows)),"period_cycles":sorted(set(periods)),
                           "duty_fraction":(sum(highs)/len(highs))/((sum(highs)/len(highs))+(sum(lows)/len(lows))),
                           "mean_hz":sys_hz/(sum(periods)/len(periods))}
    (output/"clock-trace.json").write_text(json.dumps(trace_words))
    result["clock"]={"edges":edges,"elapsed_us":elapsed,"observed_hz":edges*1e6/elapsed}
    result["health"]=health
    (output/"analysis.json").write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2),flush=True)
    if result["pcm_max_error_after_settling"] > (1 if ratio==64 else 0):
        raise RuntimeError("Firmware PCM differs from raw-bit reconstruction")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--output",type=Path,default=Path("build-tests/raw-pdm"))
    args=parser.parse_args()
    capture(args.port,args.output)
