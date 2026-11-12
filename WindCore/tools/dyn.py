"""Breath response of held notes: level and brightness against breath."""
import sys, wc, numpy as np
from scan import measure
params = dict(a.split("=") for a in sys.argv[1:])
notes = [int(v) for v in params.pop("notes", "40,47,53,59,65,71,77,83,89,95").split(",")]
bs = [0.03, 0.06, 0.12, 0.25, 0.5, 0.75, 1.0]
print("breath    " + "          ".join("%4.2f" % b for b in bs) + "    (level dB / centroid in harmonics / cents)")
for n in notes:
    row = []
    for b in bs:
        m = measure(n, params, b)
        row.append("   --   " if m is None else "%5.1f %4.1f %+4.0f" % (m[0], m[3], m[1]))
    print("%-4s " % wc.note_name(n) + " | ".join(row))
