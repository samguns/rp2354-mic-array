"""Extract microphone port geometry and net mapping from the supplied EasyEDA Pro export."""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path

MIL_MM = 0.0254


def documents(path):
    docs = []
    current = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if "||" not in line:
            continue
        head, body = line.split("||", 1)
        if not body.rstrip("|").strip():
            continue  # Deleted-record tombstone in the export.
        head, body = json.loads(head), json.loads(body.rstrip("|"))
        if head["type"] == "DOCHEAD":
            current = {"head": body, "rows": []}
            docs.append(current)
        if current is not None:
            current["rows"].append((number, head, body))
    return docs


def attributes(doc):
    result = {}
    for _, head, body in doc["rows"]:
        if head["type"] == "ATTR":
            result.setdefault(body.get("parentId"), {})[body["key"]] = body.get("value")
    return result


def transform(component, x, y):
    angle = math.radians(component["angle"])
    return (component["x"] + math.cos(angle)*x - math.sin(angle)*y,
            component["y"] + math.sin(angle)*x + math.cos(angle)*y)


def extract(path):
    docs = documents(path)
    by_id = {d["head"]["uuid"]: d for d in docs}
    pcb = next(d for d in docs if d["head"]["docType"] == "PCB")
    attrs = attributes(pcb)
    components = {attrs.get(h.get("id"), {}).get("Designator"): (line, h, b)
                  for line, h, b in pcb["rows"] if h["type"] == "COMPONENT"}
    nets = {}
    for line, head, body in pcb["rows"]:
        if head["type"] == "PAD_NET":
            _, component, pad, _ = json.loads(head["id"])
            nets[component, pad] = (body["padNet"], line)
    pico_id = components["U5"][1]["id"]
    symbol = next(d for d in docs if d["head"]["docType"] == "SYMBOL"
                  and any(b.get("value", "").startswith("GP7_")
                          for _, h, b in d["rows"] if h["type"] == "ATTR"))
    pin_names = {a["Pin Number"]: a["Pin Name"] for a in attributes(symbol).values()
                 if "Pin Number" in a and "Pin Name" in a}
    mics = []
    for designator in ("U1", "U2", "U3", "U4"):
        line, head, component = components[designator]
        if component["layerId"] != 1:
            raise ValueError("Bottom-side placement requires an explicit mirror transform")
        footprint_id = attrs[head["id"]]["Footprint"]
        footprint = by_id[footprint_id]
        # Port is the document-layer circle concentric with the ground-ring
        # clearance. Exclude the separate pin-one marking circle.
        circles = [(ln, b["path"]) for ln, h, b in footprint["rows"]
                   if h["type"] == "POLY" and b.get("layerId") == 13
                   and b.get("path", [])[:1] == ["CIRCLE"]]
        clearances = [b["path"] for _, h, b in footprint["rows"]
                      if h["type"] == "FILL" and b.get("layerId") == 12
                      and b.get("path", [])[:1] == ["CIRCLE"]]
        matches = [(ln, circle) for ln, circle in circles
                   if any(circle[1:3] == clearance[1:3] for clearance in clearances)]
        if len(matches) != 1:
            raise ValueError("Cannot uniquely identify acoustic port")
        port_line, circle = matches[0]
        x, y = transform(component, *circle[1:3])
        net, net_line = nets[head["id"], "1"]
        pico_pins = [pad for (cid, pad), (name, _) in nets.items()
                     if cid == pico_id and name == net]
        if len(pico_pins) != 1:
            raise ValueError("Ambiguous Pico connection")
        gpio = int(pin_names[pico_pins[0]].split("_")[0][2:])
        channel = gpio - 6
        # Independent rotation check against an actual routed DATA pad endpoint.
        pad = next(b for _, h, b in footprint["rows"]
                   if h["type"] == "PAD" and b["num"] == "1")
        px, py = transform(component, pad["centerX"], pad["centerY"])
        endpoints = [(b[k+"X"], b[k+"Y"], ln)
                     for ln, h, b in pcb["rows"]
                     if h["type"] in ("LINE", "ARC") and b.get("netName") == net
                     for k in ("start", "end") if k+"X" in b]
        nearest = min(endpoints, key=lambda e: math.hypot(e[0]-px, e[1]-py))
        if math.hypot(nearest[0]-px, nearest[1]-py) > 0.01:
            raise ValueError("Transformed DATA pad does not match routed endpoint")
        mics.append({"designator": designator, "channel": channel, "gpio": gpio,
                     "net": net, "pico_header_pin": int(pico_pins[0]),
                     "component_origin_mil": [component["x"], component["y"]],
                     "rotation_degrees": component["angle"],
                     "footprint_id": footprint_id,
                     "port_local_mil": circle[1:3],
                     "component_side_port_mm": [round(x*MIL_MM, 7), round(y*MIL_MM, 7), 0.0],
                     "port_mm": [round(-x*MIL_MM, 7), round(y*MIL_MM, 7), 0.0],
                     "nominal_port_mm": [round(-x*MIL_MM), round(y*MIL_MM), 0],
                     "evidence_lines": {"component": line, "port": port_line,
                                        "data_net": net_line, "routed_pad": nearest[2]}})
    pairs = []
    for a, b in itertools.combinations(mics, 2):
        distance = math.dist(a["port_mm"], b["port_mm"])
        pairs.append({"channels": [a["channel"], b["channel"]], "distance_mm": distance,
                      "max_delay_us_at_343_m_s": distance/343*1000,
                      "max_delay_samples_at_16k": distance/343*16})
    return {"source": path.as_posix(), "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "pcb_uuid": pcb["head"]["uuid"], "units": "mm", "source_unit_mm": MIL_MM,
            "coordinate_frame": "Exposed-port underside view, same top edge up; USB-C upper right, MAX98357A lower right; origin PCB (0,0), +X right, +Y up, +Z toward viewer; underside X = -CAD X, Y = CAD Y; common port plane Z=0",
            "status": "CAD-derived nominal geometry; assembled hole centers not measured; acoustic mapping validation recorded separately in docs/channel-validation.md",
            "microphones": mics, "pairs": pairs}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?", default=Path("ProDoc_pico2-mic-array_2026-10-13/ProDoc_pico2-mic-array_2026-10-13.epru"))
    parser.add_argument("--output", type=Path, default=Path("docs/array-geometry.json"))
    args = parser.parse_args()
    result = extract(args.source)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    for mic in result["microphones"]:
        print(f"CH{mic['channel']} {mic['designator']} GP{mic['gpio']} {mic['port_mm']} mm")
