// Color science for the TrueColors laser device.
//
// All RGB values here are *device-native* normalized floats 0..1, representing
// the relative drive of each laser. Chromaticity is in CIE 1931 xy. Brightness
// is a separate global and never part of these conversions — the inverse map
// normalizes so the peak channel is 1 (chromaticity only).
import type { XY } from './cie';

export interface RGB {
  r: number;
  g: number;
  b: number;
}

export interface HSV {
  h: number; // 0..1 (hue)
  s: number; // 0..1
  v: number; // 0..1
}

// Device primary chromaticities (CIE 1931 2°) — the gamut triangle vertices.
export const PRIMARIES = {
  r: { x: 0.719, y: 0.281 } as XY, // 643 nm
  g: { x: 0.074, y: 0.834 } as XY, // 525 nm
  b: { x: 0.136, y: 0.04 } as XY, // 465 nm
};

// Per-laser luminous weights (calibration constants). Tweak to match the real
// module — these scale each primary's relative luminance in the forward map.
export const k = { r: 0.21, g: 0.72, b: 0.07 };

const clamp01 = (v: number): number => (v < 0 ? 0 : v > 1 ? 1 : v);

// Convert a single primary's (x, y, Y) to XYZ tristimulus.
function xyYToXYZ(p: XY, Y: number): [number, number, number] {
  if (p.y <= 0) return [0, 0, 0];
  const X = (p.x / p.y) * Y;
  const Z = ((1 - p.x - p.y) / p.y) * Y;
  return [X, Y, Z];
}

// Forward map: device RGB (0..1) -> chromaticity xy.
// Each primary contributes Y_c = k_c * value_c; sum XYZ, reproject to xy.
export function rgbToXy(rgb: RGB): XY {
  const [Xr, Yr, Zr] = xyYToXYZ(PRIMARIES.r, k.r * rgb.r);
  const [Xg, Yg, Zg] = xyYToXYZ(PRIMARIES.g, k.g * rgb.g);
  const [Xb, Yb, Zb] = xyYToXYZ(PRIMARIES.b, k.b * rgb.b);
  const X = Xr + Xg + Xb;
  const Y = Yr + Yg + Yb;
  const Z = Zr + Zg + Zb;
  const sum = X + Y + Z;
  if (sum <= 0) {
    // No light — fall back to white point so the dot has a sane position.
    return { x: 0.3127, y: 0.329 };
  }
  return { x: X / sum, y: Y / sum };
}

// Barycentric coordinates of point p within triangle (a, b, c).
function barycentric(p: XY, a: XY, b: XY, c: XY): [number, number, number] {
  const v0x = b.x - a.x;
  const v0y = b.y - a.y;
  const v1x = c.x - a.x;
  const v1y = c.y - a.y;
  const v2x = p.x - a.x;
  const v2y = p.y - a.y;
  const den = v0x * v1y - v1x * v0y;
  if (Math.abs(den) < 1e-12) return [1, 0, 0];
  const w1 = (v2x * v1y - v1x * v2y) / den; // weight of b
  const w2 = (v0x * v2y - v2x * v0y) / den; // weight of c
  const w0 = 1 - w1 - w2; // weight of a
  return [w0, w1, w2];
}

// Inverse map: chromaticity xy -> device RGB (0..1), peak channel = 1.
// Out-of-gamut points have a negative barycentric weight; clamp to 0 and
// renormalize (projects the point onto the gamut boundary).
export function xyToRgb(p: XY): RGB {
  let [wr, wg, wb] = barycentric(p, PRIMARIES.r, PRIMARIES.g, PRIMARIES.b);
  wr = wr < 0 ? 0 : wr;
  wg = wg < 0 ? 0 : wg;
  wb = wb < 0 ? 0 : wb;
  const wsum = wr + wg + wb;
  if (wsum <= 0) return { r: 1, g: 1, b: 1 };
  wr /= wsum;
  wg /= wsum;
  wb /= wsum;
  // A primary's barycentric weight in xy equals its share of X+Y+Z = Y/y, so
  // recover Y_c = w_c * y_c, then drive = Y_c / k_c. This is the exact inverse
  // of the forward map (rgbToXy reprojects Y_c/y_c), so the dot tracks clicks.
  let r = (wr * PRIMARIES.r.y) / k.r;
  let g = (wg * PRIMARIES.g.y) / k.g;
  let b = (wb * PRIMARIES.b.y) / k.b;
  const peak = Math.max(r, g, b);
  if (peak <= 0) return { r: 1, g: 1, b: 1 };
  r /= peak;
  g /= peak;
  b /= peak;
  return { r: clamp01(r), g: clamp01(g), b: clamp01(b) };
}

// True if a chromaticity lies inside (or on) the gamut triangle.
export function inGamut(p: XY): boolean {
  const [wr, wg, wb] = barycentric(p, PRIMARIES.r, PRIMARIES.g, PRIMARIES.b);
  const eps = -1e-9;
  return wr >= eps && wg >= eps && wb >= eps;
}

// Clamp a chromaticity to the nearest point on/inside the gamut triangle.
export function clampToGamut(p: XY): XY {
  if (inGamut(p)) return p;
  const tri = [PRIMARIES.r, PRIMARIES.g, PRIMARIES.b];
  let best: XY = tri[0];
  let bestD = Infinity;
  for (let i = 0; i < 3; i++) {
    const a = tri[i];
    const b = tri[(i + 1) % 3];
    const cand = closestPointOnSegment(p, a, b);
    const d = (cand.x - p.x) ** 2 + (cand.y - p.y) ** 2;
    if (d < bestD) {
      bestD = d;
      best = cand;
    }
  }
  return best;
}

function closestPointOnSegment(p: XY, a: XY, b: XY): XY {
  const abx = b.x - a.x;
  const aby = b.y - a.y;
  const apx = p.x - a.x;
  const apy = p.y - a.y;
  const len2 = abx * abx + aby * aby;
  let t = len2 > 0 ? (apx * abx + apy * aby) / len2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return { x: a.x + t * abx, y: a.y + t * aby };
}

// HSV -> device RGB (standard sextant formulas, device RGB as the space).
export function hsvToRgb(hsv: HSV): RGB {
  const h = ((hsv.h % 1) + 1) % 1;
  const s = clamp01(hsv.s);
  const v = clamp01(hsv.v);
  const i = Math.floor(h * 6);
  const f = h * 6 - i;
  const p = v * (1 - s);
  const q = v * (1 - f * s);
  const t = v * (1 - (1 - f) * s);
  switch (i % 6) {
    case 0:
      return { r: v, g: t, b: p };
    case 1:
      return { r: q, g: v, b: p };
    case 2:
      return { r: p, g: v, b: t };
    case 3:
      return { r: p, g: q, b: v };
    case 4:
      return { r: t, g: p, b: v };
    default:
      return { r: v, g: p, b: q };
  }
}

// Device RGB -> HSV.
export function rgbToHsv(rgb: RGB): HSV {
  const r = clamp01(rgb.r);
  const g = clamp01(rgb.g);
  const b = clamp01(rgb.b);
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const d = max - min;
  let h = 0;
  if (d > 0) {
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
    if (h < 0) h += 1;
  }
  const s = max <= 0 ? 0 : d / max;
  return { h, s, v: max };
}

// CSS color for previews. Applies a light gamma so the swatch reads roughly
// like the perceived laser color; not part of the device pipeline.
export function rgbToCss(rgb: RGB): string {
  const g = (v: number) => Math.round(clamp01(v) ** (1 / 2.2) * 255);
  return `rgb(${g(rgb.r)}, ${g(rgb.g)}, ${g(rgb.b)})`;
}
