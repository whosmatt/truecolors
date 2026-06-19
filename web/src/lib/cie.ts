// CIE 1931 spectral locus (2° observer), ~5 nm spacing from 405–700 nm.
// Each entry is the chromaticity (x, y) at that wavelength — the outline of
// the "horseshoe". Drawing connects these points and closes with the purple
// line back to the violet end.
export interface XY {
  x: number;
  y: number;
}

export const SPECTRAL_LOCUS: XY[] = [
  { x: 0.1741, y: 0.0050 }, // 405
  { x: 0.1740, y: 0.0050 }, // 410
  { x: 0.1738, y: 0.0049 }, // 415
  { x: 0.1736, y: 0.0049 }, // 420
  { x: 0.1733, y: 0.0048 }, // 425
  { x: 0.1730, y: 0.0048 }, // 430
  { x: 0.1726, y: 0.0048 }, // 435
  { x: 0.1721, y: 0.0048 }, // 440
  { x: 0.1714, y: 0.0051 }, // 445
  { x: 0.1703, y: 0.0058 }, // 450
  { x: 0.1689, y: 0.0069 }, // 455
  { x: 0.1669, y: 0.0086 }, // 460
  { x: 0.1644, y: 0.0109 }, // 465
  { x: 0.1611, y: 0.0138 }, // 470
  { x: 0.1566, y: 0.0177 }, // 475
  { x: 0.1510, y: 0.0227 }, // 480
  { x: 0.1440, y: 0.0297 }, // 485
  { x: 0.1355, y: 0.0399 }, // 490
  { x: 0.1241, y: 0.0578 }, // 495
  { x: 0.1096, y: 0.0868 }, // 500
  { x: 0.0913, y: 0.1327 }, // 505
  { x: 0.0687, y: 0.2007 }, // 510
  { x: 0.0454, y: 0.2950 }, // 515
  { x: 0.0235, y: 0.4127 }, // 520
  { x: 0.0082, y: 0.5384 }, // 525
  { x: 0.0039, y: 0.6548 }, // 530
  { x: 0.0139, y: 0.7502 }, // 535
  { x: 0.0389, y: 0.8120 }, // 540
  { x: 0.0743, y: 0.8338 }, // 545
  { x: 0.1142, y: 0.8262 }, // 550
  { x: 0.1547, y: 0.8059 }, // 555
  { x: 0.1929, y: 0.7816 }, // 560
  { x: 0.2296, y: 0.7543 }, // 565
  { x: 0.2658, y: 0.7243 }, // 570
  { x: 0.3016, y: 0.6923 }, // 575
  { x: 0.3373, y: 0.6589 }, // 580
  { x: 0.3731, y: 0.6245 }, // 585
  { x: 0.4087, y: 0.5896 }, // 590
  { x: 0.4441, y: 0.5547 }, // 595
  { x: 0.4788, y: 0.5202 }, // 600
  { x: 0.5125, y: 0.4866 }, // 605
  { x: 0.5448, y: 0.4544 }, // 610
  { x: 0.5752, y: 0.4242 }, // 615
  { x: 0.6029, y: 0.3965 }, // 620
  { x: 0.6270, y: 0.3725 }, // 625
  { x: 0.6482, y: 0.3514 }, // 630
  { x: 0.6658, y: 0.3340 }, // 635
  { x: 0.6801, y: 0.3197 }, // 640
  { x: 0.6915, y: 0.3083 }, // 645
  { x: 0.7006, y: 0.2993 }, // 650
  { x: 0.7079, y: 0.2920 }, // 655
  { x: 0.7140, y: 0.2859 }, // 660
  { x: 0.7190, y: 0.2809 }, // 665
  { x: 0.7230, y: 0.2770 }, // 670
  { x: 0.7260, y: 0.2740 }, // 675
  { x: 0.7283, y: 0.2717 }, // 680
  { x: 0.7300, y: 0.2700 }, // 685
  { x: 0.7311, y: 0.2689 }, // 690
  { x: 0.7320, y: 0.2680 }, // 695
  { x: 0.7327, y: 0.2673 }, // 700
];

// Chart domain in chromaticity space. The locus tops out near y=0.834 and
// x=0.733; pad a little so it doesn't touch the SVG edges.
export const XY_DOMAIN = { xMin: 0, xMax: 0.75, yMin: 0, yMax: 0.85 };

// Inner plot insets (viewbox units) reserving a gutter for axis tick labels.
export const CHART_MARGIN = { left: 34, right: 12, top: 12, bottom: 30 };

// Map a chromaticity (x,y) to SVG coordinates within the inset plot area.
// y is flipped because SVG's origin is top-left.
export function xyToSvg(p: XY, size: number): XY {
  const { xMin, xMax, yMin, yMax } = XY_DOMAIN;
  const { left, right, top, bottom } = CHART_MARGIN;
  const w = size - left - right;
  const h = size - top - bottom;
  return {
    x: left + ((p.x - xMin) / (xMax - xMin)) * w,
    y: top + h - ((p.y - yMin) / (yMax - yMin)) * h,
  };
}

// Inverse of xyToSvg: SVG coordinates back to a chromaticity (x,y).
export function svgToXy(p: XY, size: number): XY {
  const { xMin, xMax, yMin, yMax } = XY_DOMAIN;
  const { left, right, top, bottom } = CHART_MARGIN;
  const w = size - left - right;
  const h = size - top - bottom;
  return {
    x: xMin + ((p.x - left) / w) * (xMax - xMin),
    y: yMin + ((top + h - p.y) / h) * (yMax - yMin),
  };
}

// A few spectral-locus points to label with their wavelength (nm).
export interface LocusLabel {
  nm: number;
  p: XY;
}
export function locusLabels(): LocusLabel[] {
  const nms = [460, 480, 500, 520, 540, 560, 580, 600, 620, 700];
  return nms
    .map((nm) => ({ nm, p: SPECTRAL_LOCUS[Math.round((nm - 405) / 5)] }))
    .filter((l): l is LocusLabel => l.p !== undefined);
}

// SVG polygon points string for the spectral-locus horseshoe (closed by the
// purple line back to the start).
export function locusPath(size: number): string {
  return SPECTRAL_LOCUS.map((p) => {
    const s = xyToSvg(p, size);
    return `${s.x.toFixed(2)},${s.y.toFixed(2)}`;
  }).join(' ');
}
