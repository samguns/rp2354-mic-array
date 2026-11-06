"""Prototype azimuth estimation from broadband four-channel audio or measured delays."""
import argparse
import json
import math
from pathlib import Path


def estimate(report,geometry):
    # No source position, predicted delay, or source-based consistency flag is
    # consumed here. Direction depends only on measured delays and geometry.
    rate = report.get("sample_rate_hz")
    if rate != 48000:
        raise ValueError("Delay report must explicitly specify sample_rate_hz=48000")
    ports = {m["channel"]:m["nominal_port_mm"] for m in geometry["microphones"]}
    reasons = []
    rows = []
    seen = set()
    for pair in report["pairs"]:
        a,b = pair["channels"]
        if a == b or a not in ports or b not in ports or (min(a,b),max(a,b)) in seen:
            raise ValueError("Invalid or duplicate pair")
        seen.add((min(a,b),max(a,b)))
        lag = pair["lag_samples"]
        if not all(math.isfinite(pair[key]) for key in
                   ("lag_samples","mean_coherence","gcc_score","coherent_band_fraction")):
            raise ValueError("Nonfinite delay or quality metric")
        if not 0 <= pair["mean_coherence"] <= 1.000001:
            raise ValueError("Invalid coherence")
        dx,dy = [(ports[b][i]-ports[a][i])/1000 for i in (0,1)]
        bound = math.hypot(dx,dy)*rate/343
        if abs(lag)>bound+.45:
            reasons.append(f"CH{a}-CH{b} exceeds physical delay bound")
        if (pair["mean_coherence"]<.5 or pair["gcc_score"]<.5
                or pair["coherent_band_fraction"]<.4 or pair["search_boundary"]):
            reasons.append(f"CH{a}-CH{b} has insufficient broadband evidence")
        rows.append((-dx*rate/343,-dy*rate/343,lag,pair["mean_coherence"],a,b))
    if len(seen)!=6:
        raise ValueError("All six distinct microphone pairs are required")
    if any(report.get("clipped_samples",[])):
        reasons.append("Clipped audio")
    xx = sum(w*x*x for x,y,t,w,a,b in rows)
    xy = sum(w*x*y for x,y,t,w,a,b in rows)
    yy = sum(w*y*y for x,y,t,w,a,b in rows)
    xt = sum(w*x*t for x,y,t,w,a,b in rows)
    yt = sum(w*y*t for x,y,t,w,a,b in rows)
    determinant = xx*yy-xy*xy
    if determinant<=1e-12:
        raise ValueError("Degenerate geometry or weights")
    ux,uy = (xt*yy-yt*xy)/determinant,(yt*xx-xt*xy)/determinant
    residuals = [{"channels":[a,b],"measured_samples":t,
                  "fitted_samples":x*ux+y*uy,"residual_samples":t-x*ux-y*uy}
                 for x,y,t,w,a,b in rows]
    rms = math.sqrt(sum(r["residual_samples"]**2 for r in residuals)/6)
    maximum = max(abs(r["residual_samples"]) for r in residuals)
    lateral = math.hypot(ux,uy)
    if rms>.45+1e-9 or maximum>.9+1e-9:
        reasons.append("Pair delays disagree with a common direction")
    if lateral<.25:
        reasons.append("Source too close to array normal for stable azimuth")
    if lateral>1.05:
        reasons.append("Fitted direction exceeds unit-vector bound")
    accepted = not reasons
    return {"sample_rate_hz":rate,"status":"provisional" if accepted else "rejected",
            "azimuth_degrees":math.degrees(math.atan2(uy,ux))%360 if accepted else None,
            "confidence":"provisional" if accepted else "insufficient",
            "rejection_reasons":reasons,"in_plane_direction_vector":[ux,uy],
            "lateral_magnitude":lateral,"fit_rms_samples":rms,
            "max_residual_samples":maximum,"fit_rms_us":rms/rate*1e6,
            "max_residual_us":maximum/rate*1e6,"pair_fit":residuals,
            "coordinate_frame":geometry["coordinate_frame"],
            "azimuth_convention":"0 right, 90 top, 180 left, 270 bottom in exposed-port view",
            "limitations":"Far-field plane-wave prototype; confidence is a heuristic, not a probability. No calibrated angle accuracy, range, or hemisphere/elevation claim. No pair discarded or clamped."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--delays",type=Path)
    group.add_argument("--wav",type=Path)
    group.add_argument("--capture",type=Path,help="Record three seconds to this WAV first")
    parser.add_argument("--output",type=Path)
    args = parser.parse_args()
    geometry = json.loads(Path("docs/array-geometry.json").read_text(encoding="utf-8"))
    if args.delays:
        report = json.loads(args.delays.read_text(encoding="utf-8"))
        source = args.delays
    else:
        from delay_check import analyze
        source = args.wav or args.capture
        if args.capture:
            source.parent.mkdir(parents=True,exist_ok=True)
            from uac_wasapi import capture
            capture(source,3,48000)
        report = analyze(source,None,geometry,write_report=False)
    result = estimate(report,geometry)
    result["input"] = str(source)
    output = args.output or source.with_suffix(".direction.json")
    output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,indent=2))
