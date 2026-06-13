# TrueColors — Timer / Peripheral Strategy

Firmware reference for how the ESP32-S3 hardware peripherals generate all timed
signals. Goal: do everything in hardware, no software-bit-banged PWM. Pin/net
details are in [`hardware-design.md`](hardware-design.md).

Peripheral allocation:

| Subsystem | Peripheral | Why |
|-----------|------------|-----|
| RGB lasers | **MCPWM** (1 group, 1 timer, 3 operators) | phase-shifted non-overlapping pulses + atomic synchronous update |
| Fan speed | **LEDC** (1 timer + 1 channel, 25 kHz) | simple high-res PWM, inverted output |
| Fan tach | **PCNT** | hardware pulse counting, zero CPU |
| Rail voltage | **ADC1** (CH3) + calibration | ADC2 unusable with Wi-Fi |
| PD sink status | **I²C master** (polled) | CH224A status/current registers |

---

## 1. Laser PWM — the core scheme

### Concept: phase-shifted PWM with Σduty ≤ 100%

The requirements — *sequential / non-overlapping*, *common cathode never all-on*,
*stretchable slots (R can be 100% if G=B=0)*, *brightness = on-time* — are all one
waveform: **three phase-offset pulses sharing one timer period**, where the sum of
the three pulse widths (plus guard gaps) is ≤ the period.

- Non-overlap ⇒ common-cathode-safe, for free.
- "Stretch" ⇒ free: any single color can take (almost) the whole period when the
  others are 0; the only rule is Σ ≤ period.
- Brightness of a color = its pulse width / period.

### MCPWM mapping

One MCPWM **timer** sets the period; three **operators** share it, each producing
one pulse via its two comparators (set output HIGH at CMPA, LOW at CMPB):

```
period T  ├─────────────────────────────────────────────┤  (repeats)
R         ███████░                                            high 0 → dR
G                 ░░gap░░███████░                             high dR+gap → dR+gap+dG
B                              ░░gap░░██████░                 high ... → ...+dB
                                            ░░gap░░  (wrap)
```

- R: CMPA = 0,                           CMPB = dR
- G: CMPA = dR + gap,                     CMPB = dR + gap + dG
- B: CMPA = dR + 2·gap + dG,              CMPB = dR + 2·gap + dG + dB
- **Invariant:** `dR + dG + dB + 3·gap ≤ T` (three guard gaps: R→G, G→B, B→wrap).

Outputs are **active-high, NOT inverted** (LM3409 EN: high = laser on).

### Frequency choice

- One timer period = one full R→G→B sequence, so **PWM frequency = RGB cycle
  frequency** (the two collapse into a single waveform — do NOT stack a second
  higher-frequency PWM on top; for a buck driver that only adds settling losses).
- **Default 240–480 Hz.** Above flicker fusion for a static mood light, and low
  enough that each color's single pulse is long, giving the LM3409 buck time to
  settle and reach regulation. Lower frequency = better buck behavior + more
  dimming range; higher = more motion robustness (not needed for a static light).
- Make it a `#define` / config value; start at 480 Hz and tune.

### Resolution

Run the MCPWM group clock at e.g. **10 MHz** (0.1 µs/tick). At 240 Hz the period
is ~41,667 ticks (~15 bits) — far more brightness resolution than needed.

### Guard interval (`gap`)

Inserted between colors so the previous LM3409's output current (inductor +
output cap tail) fully decays before the next color's buck turns on — extra
insurance for the common cathode and against transient overlap.

- Start ~**10–50 µs**; tune by scoping each color's actual current fall time.
- Cost is tiny: at 240 Hz, 3 × 20 µs = 60 µs ≈ 1.4% of the period, so a single
  color still reaches ~98–99% ("effectively 100%").
- Only the gaps adjacent to *active* pulses matter; if a color is 0 you may omit
  its surrounding gap to reclaim time (optional optimization).

### Atomic, glitch-free updates (critical)

Brightness changes must never momentarily push two pulses into overlap. With
MCPWM, **stage all three operators' new CMPA/CMPB and load them synchronously on
the timer zero event (TEZ)** so the three pulses always move as one set and the
`Σ + guards ≤ T` invariant holds every period. Use the MCPWM compare-update-on-
TEZ mechanism; do not update operators one at a time without sync.

### Brightness → on-time pipeline (firmware)

1. Inputs: desired per-color brightness `bR, bG, bB` (0…1, e.g. after gamma).
2. Compute raw widths: `dX = bX × T_available`, where
   `T_available = T − 3·gap`.
3. **Normalize if over budget:** if `dR + dG + dB > T_available`, scale all three
   by `T_available / (dR+dG+dB)` (preserves color ratio) — or clip per policy.
   When the sum is under budget, no scaling: unused time is just idle (all lasers
   off), which is correct.
4. Compute the three (CMPA, CMPB) pairs as above.
5. Stage + sync-load on TEZ.

> Note: normalization couples absolute brightness to the sum. If you want
> "stretch for max brightness" behavior (one color brighter when others are dim),
> that is exactly step 3 with scaling only on overflow — a dim scene leaves the
> bright color at its requested width, a maxed-out white scene shares the period.

### Optional power/thermal cap

Σduty ≤ 100% already guarantees at most one laser on at a time (cathode safe). A
separate firmware policy may further cap the **sum of brightnesses** to stay
within the CH224A current budget (0x50) and array thermal limits.

---

## 2. Fan PWM — LEDC, inverted

- **LEDC**, one timer + one channel, **25 kHz** (inaudible; 40 µs period rides
  through the fan's internal commutation so no stutter), on **GPIO6**.
- Hardware is a **low-side BSS138** (gate-driven): GPIO HIGH → FET on → fan PWM
  line LOW → fan OFF. So **set `ledc_channel_config_t.flags.output_invert = 1`**.
  With invert, the **LEDC duty register maps directly to fan speed**
  (duty 0 = stop, duty max = full). Verified: ~1% duty already spins the fan.
- Resolution: a few hundred steps is plenty; e.g. 10-bit at 25 kHz.
- **Boot behavior:** before LEDC init, GPIO6 is Hi-Z, R26 pulls the gate HIGH →
  FET on → fan line held LOW → **fan disabled at boot**. Initialize LEDC at any
  point in startup; fan stays off until commanded.

## 3. Fan tach — PCNT

- **PCNT** unit counting edges on **GPIO7** (FAN_TACH, pulled to 3.3 V).
- RPM = `(pulses / pulses_per_rev) / Δt × 60`, with `pulses_per_rev = 2` (typical
  PC fan; confirm). Sample over a fixed window (e.g. 1 s) or use a glitch filter.
- Valid because the fan supply is constant 5 V (not chopped) — tach references a
  stable ground.

## 4. Rail voltage — ADC1

- **ADC1_CH3 / GPIO4**, 12 dB attenuation, eFuse calibration (`adc_cali`).
- `V_rail = V_adc × 7.8` (68k/10k divider; C40 = 100 nF filter).
- Oversample (e.g. average N samples). ADC1 mandatory — ADC2 is blocked while
  Wi-Fi is on. Use as a sanity check on the actual 20 V rail (sag detection),
  complementary to the CH224A's negotiated-gear info.

## 5. PD sink (CH224A) — I²C polling

- **I²C master**, SDA = GPIO8, SCL = GPIO9, 400 kHz, address **0x22/0x23**
  (confirm), **single-byte transactions only**.
- Startup gate sequence before enabling lasers:
  1. (20 V already requested by the CFG1 strap.)
  2. Poll **0x09** until bit3 (PD) = 1 → handshake good.
  3. Read **0x50** → max current = value × 50 mA; verify it covers planned laser
     power at 20 V.
  4. Confirm ADC rail in range.
  5. Then start MCPWM laser outputs.
- Re-poll 0x09 / 0x50 periodically at runtime. There is **no hardware PG
  interlock** — this poll + the ADC are the only power-fault detection, so on a
  failed check, command all laser widths to 0 (and rely on the EN pulldowns).

---

## Init order (suggested)

1. GPIO defaults / ensure EN pulldowns effective (lasers off).
2. LEDC fan (optional: set a sane speed early to quiet the full-speed boot state).
3. I²C + CH224A startup gate (PD good, current budget, ADC rail check).
4. MCPWM timer + 3 operators, TEZ-synced compare updates, start with all widths 0.
5. Begin the brightness control loop (gamma → widths → normalize → sync-load).

## Open items to confirm on hardware

- CH224A I²C address: **0x22 vs 0x23**.
- Fan `pulses_per_rev` for RPM math.
- Guard `gap` value — scope per-color current fall time on the LM3409 outputs.
- Laser cycle frequency final value (start 480 Hz) and the practical low-end
  dimming floor set by the LM3409 settle time.
