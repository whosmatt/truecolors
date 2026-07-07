# truecolors — Timer / Peripheral Strategy

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
- **Runtime-adjustable: 120 / 240 / 480 Hz** (settings dropdown → WS
  `set_pwm_hz` → NVS `pwm_hz`), default **120 Hz** — quietest coil whine, still
  above flicker fusion for a static light. Lower frequency = better buck
  behavior, more dimming range, quieter; higher = more motion robustness.
- Frequency changes are glitch-free while running: the period and all six
  comparators shadow-load on the same TEZ (`update_period_on_empty`), and
  `laser_set_pwm_hz()` orders the writes so a TEZ landing mid-update never
  leaves a compare beyond the counter range (growing period → period first,
  shrinking → comparators first).

### Resolution

The MCPWM group clock runs at **5 MHz** (0.2 µs/tick): the period register is
**16-bit** (max 65,535 ticks), and 120 Hz needs 41,666 — a 10 MHz clock cannot
reach below ~153 Hz. At 480 Hz the period is still 10,416 ticks (~13 bits),
far more brightness resolution than needed.

### Wrap-tick reservation (learned the hard way)

The up-counter counts `[0, period−1]`; a compare value equal to the period tick
is accepted by the driver but **never fires** — the clear action is lost and the
channel latches ON at 100% duty, overlapping its neighbors (observed as bright
flashes at crossfade corners with stretch > 25%). The width math therefore
reserves the last tick so every clear compare stays reachable; a width of
period−1 means "fully on" and laser.c drives it as a DC force level instead of
chopping a one-tick notch into EN.

### Guard interval (`gap`)

Settled at **0**. The common-cathode constraint is thermal, not electrical —
these are three independent bucks, not an H-bridge, so there is no
shoot-through path and back-to-back pulses are fine. The gap stays plumbed
through `laser_math.c` should it ever be needed.

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

### Width floors and the LM3409 keepalive

- A nonzero width below **MIN_ON** (8 µs LM3409 settle floor) snaps up to it.
- **LM3409 low-power shutdown (measured):** a sustained EN low lets the IC's
  VCC bias cap decay below UVLO into 110 µA shutdown; the next enable pays a
  soft restart that shows as a visible fade-in. Normal PWM-dimming lows (≤ one
  period) never trigger it, but tens-of-ms dark states do. Effects that strobe
  from black (discombobulate, audio) set a `keepalive` flag: commanded-dark
  channels then idle at MIN_ON pulses to keep the bias awake. The power switch,
  safety latch, boot gate and brightness-zero still drop to true black.

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
  With invert, the **LEDC duty register maps directly to fan speed**.
- **Measured transfer curve (2026-07-07):** the fan never fully stops — ~84 rpm
  floor at 0% duty (vendor min-speed behavior; PWM line verified held low),
  dead until a **19% knee**, then linear `rpm ≈ 1875·duty − 250` up to 1621 rpm
  at 100%, no hysteresis. The PID output is treated as *cooling demand* and
  remapped onto `[FAN_KNEE_DUTY, 1]` so the loop sees a linear plant
  (see `fancontrol.c`).
- Resolution: a few hundred steps is plenty; e.g. 10-bit at 25 kHz.
- **Boot behavior:** before LEDC init, GPIO6 is Hi-Z, R26 pulls the gate HIGH →
  FET on → fan line held LOW → **fan disabled at boot**. Initialize LEDC at any
  point in startup; fan stays off until commanded.

## 3. Fan tach — PCNT

- **PCNT** unit counting edges on **GPIO7** (FAN_TACH, pulled to 3.3 V).
- `pulses_per_rev = 2` (confirmed on hardware). Count **both edges**
  (4 edges/rev) and integrate a full **1 s window** before computing: at a
  100 ms poll a single pulse is worth 300 rpm of quantization, while the
  both-edge 1 s window resolves 15 rpm — needed to see the ~84 rpm idle floor.
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
  3. Read **0x50** → max current = value × 50 mA; compare against the measured
     draw model `I(V) = 3.68 − 0.106·V` + 0.2 A margin (2.4 A @ 12 V …
     1.55 A @ 20 V). A short budget is a **warning only** — no brightness
     limiting; an actual overdraw trips the supply, and the light recovers to
     its default-off boot state.
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

## Resolved on hardware

- CH224A I²C address: firmware probes 0x22 then 0x23 at init.
- Fan `pulses_per_rev` = 2, confirmed; full transfer curve measured (see §2).
- Guard `gap` = 0 — thermal constraint, not electrical.
- Laser cycle frequency: runtime dropdown 120/240/480 Hz, default 120 Hz
  (coil whine); MCPWM group clock 5 MHz to fit 120 Hz in the 16-bit period.
- LM3409 low-power shutdown on sustained EN low → `keepalive` width floor.

## Open items to confirm on hardware

- MIN_ON_US (LM3409 settle floor) — 8 µs assumed, verify on scope.
