#!/usr/bin/env python3
"""Generate golden-vector headers from golden.json.

golden.json is canonical; the shipped golden.h emits bare `0f` literals, which
are not valid C.

  test/golden_frames.h    frames + expected windows and int8 inputs (host test)
  selftest_window.h       two windows + expected outputs (device, ~4 KB)

Cases with a null centre come from other takes and are checked from window_raw
rather than through the ring.
"""
import json
import sys
from pathlib import Path


def lit(v):
    s = f"{float(v):.9g}"
    if not any(c in s for c in ".eE") and "inf" not in s and "nan" not in s:
        s += ".0"
    return s + "f"


def arr(name, vals, per_line=8, typ="float"):
    fmt = lit if typ == "float" else (lambda v: str(int(v)))
    out = [f"static const {typ} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per_line):
        out.append("    " + ", ".join(fmt(v) for v in vals[i:i + per_line]) + ",")
    out.append("};")
    return out


def main(src, host_out, dev_out):
    g = json.loads(Path(src).read_text())
    frames = g["frames"]
    cases = g["cases"]
    mean = g["normalisation"]["mean"]
    scale = g["normalisation"]["scale"]
    qs = g["input_quant"]["scale"]

    L = ["// Generated from golden.json by tools/gen_golden.py -- do not edit.",
         "#pragma once", "",
         f"#define GOLDEN_NFRAMES {len(frames)}",
         f"#define GOLDEN_FEATS   {len(g['feature_order'])}",
         f"#define GOLDEN_INPUTS  {g['context']['inputs']}",
         f"#define GOLDEN_NCASES  {len(cases)}",
         f"#define GOLDEN_IN_ZERO ({g['input_quant']['zero_point']})", ""]
    L += arr("kGoldenFrames", [v for f in frames for v in f], 12)
    L.append("")
    # Per-model, and stale copies fail silently, so they are generated too.
    L += arr("kGoldenNormMean", mean, 6)
    L += arr("kGoldenQuantMul", [1.0 / (s * qs) for s in scale], 6)
    L.append("")
    for i, c in enumerate(cases):
        centre = c["centre_in_frames"]
        L.append(f"// {c['label']}: " +
                 (f"centre block {centre}" if centre is not None else "window only"))
        L.append(f"#define GOLDEN_CENTRE_{i} {-1 if centre is None else centre}")
        L += arr(f"kGoldenWindow{i}", c["window_raw"], 8)
        L += arr(f"kGoldenInt8_{i}", c["input_int8"], 16, "signed char")
        L.append("")
    Path(host_out).write_text("\n".join(L) + "\n")

    # Device: a music case and the silence case, so a boot check covers both
    # ends of the music head.
    want = [c for c in cases if c["label"] in ("beat", "silence")]
    D = ["// Generated from golden.json by tools/gen_golden.py -- do not edit.",
         "#pragma once", "", f"#define SELFTEST_N {len(want)}", ""]
    for i, c in enumerate(want):
        o = c["output_int8_dequantised"]
        D.append(f"// {c['label']}")
        D += arr(f"kSelftestWindow{i}", c["window_raw"], 6)
        D.append(f"static const float kSelftestBeat{i}   = {lit(o['beat'][0])};")
        D.append(f"static const float kSelftestOffset{i} = {lit(o['beat_offset'][0])};")
        D.append(f"static const float kSelftestMusic{i}  = {lit(o['music'][0])};")
        D.append("")
    D.append("static const float *const kSelftestWindows[SELFTEST_N] = {" +
             ", ".join(f"kSelftestWindow{i}" for i in range(len(want))) + "};")
    for f in ("Beat", "Offset", "Music"):
        D.append(f"static const float kSelftest{f}[SELFTEST_N] = {{" +
                 ", ".join(f"kSelftest{f}{i}" for i in range(len(want))) + "};")
    Path(dev_out).write_text("\n".join(D) + "\n")
    print(f"host: {len(frames)} frames, {len(cases)} cases")
    for i, c in enumerate(want):
        o = c["output_int8_dequantised"]
        print(f"dev : {c['label']:9} music={o['music'][0]:.4f} beat={o['beat'][0]:.4f}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
