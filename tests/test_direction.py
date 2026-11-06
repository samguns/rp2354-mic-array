import copy
import itertools
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0,str(Path(__file__).parents[1]/"tools"))
from direction import estimate

PORTS = [[0,25,0],[-25,0,0],[0,-25,0],[25,0,0]]
GEOMETRY = {"microphones":[{"channel":i+1,"nominal_port_mm":p} for i,p in enumerate(PORTS)],
            "coordinate_frame":"exposed-port test frame"}


def synthetic(angle,lateral=.8):
    ux,uy = lateral*math.cos(math.radians(angle)),lateral*math.sin(math.radians(angle))
    pairs = []
    for a,b in itertools.combinations(range(4),2):
        lag = -sum((PORTS[b][i]-PORTS[a][i])*u for i,u in enumerate((ux,uy)))/1000*48000/343
        pairs.append({"channels":[a+1,b+1],"lag_samples":lag,"mean_coherence":.95,
                      "gcc_score":.95,"coherent_band_fraction":.95,"search_boundary":False})
    return {"sample_rate_hz":48000,"pairs":pairs,"clipped_samples":[0]*4}


class DirectionTests(unittest.TestCase):
    def test_azimuth_convention_all_quadrants(self):
        for angle in range(0,360,45):
            r = estimate(synthetic(angle),GEOMETRY)
            self.assertEqual(r["status"],"provisional")
            self.assertLess(abs((r["azimuth_degrees"]-angle+180)%360-180),1e-8)

    def test_legacy_and_missing_rate_rejected(self):
        for rate in (16000,44100,None):
            r = synthetic(45)
            if rate is None: del r["sample_rate_hz"]
            else: r["sample_rate_hz"] = rate
            with self.assertRaisesRegex(ValueError,"sample_rate_hz=48000"):
                estimate(r,GEOMETRY)

    def test_source_truth_not_used(self):
        r = synthetic(135)
        before = estimate(r,GEOMETRY)
        r["source_mm"] = [999,0,0]
        for p in r["pairs"]:
            p.update(expected_samples=999,error_samples=999,consistent=False)
        self.assertEqual(estimate(r,GEOMETRY),before)

    def test_array_normal_has_no_azimuth(self):
        self.assertIsNone(estimate(synthetic(0,0),GEOMETRY)["azimuth_degrees"])

    def test_impossible_pair_not_clamped(self):
        r = synthetic(0);r["pairs"][0]["lag_samples"] = 12
        self.assertEqual(estimate(r,GEOMETRY)["status"],"rejected")

    def test_inconsistent_delays_rejected(self):
        r = synthetic(45);r["pairs"][1]["lag_samples"] = -3
        self.assertEqual(estimate(r,GEOMETRY)["status"],"rejected")

    def test_bad_evidence_and_clipping_rejected(self):
        for field,value in [("mean_coherence",.1),("coherent_band_fraction",.01),("search_boundary",True)]:
            r = synthetic(45);r["pairs"][0][field] = value
            self.assertEqual(estimate(r,GEOMETRY)["status"],"rejected")
        r = synthetic(45);r["clipped_samples"][0] = 1
        self.assertEqual(estimate(r,GEOMETRY)["status"],"rejected")

    def test_missing_or_duplicate_pair_rejected(self):
        r = synthetic(45);r["pairs"].pop()
        with self.assertRaises(ValueError):estimate(r,GEOMETRY)
        r = synthetic(45);r["pairs"][-1] = copy.deepcopy(r["pairs"][0])
        with self.assertRaises(ValueError):estimate(r,GEOMETRY)


if __name__ == "__main__":
    unittest.main()
