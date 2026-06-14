# TrueColors — Hardware Design Reference

Reference for firmware development. Describes the board, power tree, and every
ESP32 connection. Authoritative source is [`pcb/truecolors_driver.kicad_sch`](../pcb/truecolors_driver.kicad_sch);
this document reflects that schematic as built. See
[`timer-strategy.md`](timer-strategy.md) for the peripheral/timing plan.

## Project summary

110% BT.2020 mood light driven by a **Numb12 RGB laser array** (common cathode).
Each color is powered by its own **LM3409** P-channel buck LED driver (one
hierarchical sheet each: `driverR`, `driverG`, `driverB`, all instances of
`numb12_driver.kicad_sch`). The ESP32-S3 dims each color by PWM-ing the driver's
`EN`/PWM input.

**Hard constraint:** the laser array has a **common cathode and is NOT rated for
all diodes on simultaneously.** Firmware must guarantee the three colors are
driven **sequentially / non-overlapping** at all times — including during
brightness updates. This is enforced by the MCPWM scheme in
[`timer-strategy.md`](timer-strategy.md).

## MCU

**ESP32-S3-WROOM-1 (N8R8)** — 8 MB flash, 8 MB **octal** PSRAM.

- Octal PSRAM consumes **GPIO35, 36, 37** (datasheet Table 3-1 footnote b) — do
  not use. GPIO33/34 are not bonded out.
- Strapping pins (avoid for I/O that matters at boot): **GPIO0, 3, 45, 46**.
- Not 5 V tolerant — abs max ~3.6 V on any GPIO. Every pull-up that touches the
  ESP32 goes to **3.3 V**, never to 5 V or VBUS/20 V.
- Native USB-Serial-JTAG on GPIO19/20 is used for the USB port, so pad-JTAG
  (GPIO39/40 = MTCK/MTDO) is free for reuse as laser enables.

## Power tree

```
USB-C ──(PD sink: CH224A)──► +20V ──(AP63205 buck)──► +5V ──(AMS1117 LDO)──► +3.3V
         CFG1 = 120k → 20V        U1                    U4
```

| Rail  | Source | Notes |
|-------|--------|-------|
| +20 V | USB-C VBUS, negotiated by **CH224A (U2)** PD sink | Feeds all three LM3409 laser drivers (VIN) and the +5 V buck |
| +5 V  | **AP63205WU-7 (U1)** buck, L4 = 4.7 µH | Fan supply + LDO input |
| +3.3 V| **AMS1117-3.3 (U4)** LDO | ESP32, pull-ups, sensor logic |

The 20 V rail establishes from the **CFG1 = 120 kΩ → GND** strap on the CH224A at
power-on, independent of firmware (single-resistor mode also auto-enables I²C on
CFG2/CFG3). Lasers are held off by their EN pulldowns until firmware drives them.

## Pin map (final, verified against schematic)

| Net | GPIO | Module pin | Peripheral (firmware) | Electrical notes |
|-----|------|-----------|------------------------|------------------|
| EN_R | GPIO40 | 33 | MCPWM op (laser R) | active-high; 10k pulldown; → driverR PWM |
| EN_G | GPIO39 | 32 | MCPWM op (laser G) | active-high; 10k pulldown; → driverG PWM |
| EN_B | GPIO38 | 31 | MCPWM op (laser B) | active-high; 10k pulldown; → driverB PWM |
| VIN_ADC | GPIO4 | 4 | ADC1_CH3 | 20 V rail sense via divider (below) |
| FAN_PWM | GPIO6 | 6 | LEDC, 25 kHz | **inverted output** (low-side BSS138) |
| FAN_TACH | GPIO7 | 7 | PCNT | 10k (R25) pull-up to 3.3 V |
| CH224A_SDA | GPIO8 | 12 | I²C master SDA | 2.2k (R22) pull-up to 3.3 V |
| CH224A_SCL | GPIO9 | 17 | I²C master SCL | 2.2k (R23) pull-up to 3.3 V |
| IO0 / BOOT | GPIO0 | 27 | boot strap / button | SW1 + 10k (R16) pull-up to 3.3 V |
| TX / RX | GPIO43/44 | 37/36 | UART0 console | CN1 programming header |
| USB D-/D+ | GPIO19/20 | 13/14 | USB-Serial-JTAG | USB-C DM/DP |
| MIC_CLK | GPIO12 | 20 | I²S PDM CLK | no series R (short trace); → MIC1 CLK |
| MIC_DATA | GPIO13 | 21 | I²S PDM DATA RX | no series R (short trace); ← MIC1 DATA |
| NTC_ADC | GPIO10 | 18 | ADC1_CH9 | laser module NTC sense via divider (below) |
| LED_DATA | GPIO11 | 19 | RMT TX | WS2812B-B data |


## Subsystems

### Laser drivers (R / G / B) — LM3409

- Each color = one LM3409MY P-channel buck on its own sheet, `VIN = +20 V`,
  output to the laser, common cathode to GND.
- The `EN`/PWM input is **directly 3.3 V compatible and active-high** (high =
  laser current on). Driven by MCPWM — see [`timer-strategy.md`](timer-strategy.md).
- **Boot safety:** each EN net has a **10 kΩ pulldown** so the laser is OFF while
  the GPIO is Hi-Z (reset / flashing / before firmware init).
- The LM3409 dims by gating EN; each rising edge restarts the converter, which
  needs settle time. This sets a **dimming floor** (shortest useful on-time ≈
  converter settle time) and argues for a **low** PWM/cycle frequency — see the
  timing doc for the rationale.

### Fan — 4-wire (5-pin) PC fan, run on 5 V via low-side PWM

Connector **CN2 = Molex 47053-1000** fan header. The fan is a 12 V 4-wire PC fan
run on **+5 V** (validated: smooth control, starts at ~1% duty).

- **CN2 pin 1** = GND, **pin 2** = +5 V, **pin 3** = TACH, **pin 4** = PWM control.
- The fan's PWM input does **not** meet Intel logic levels — measured idle ≈
  **1.58 V** on 5 V supply (≈3.3 V on 12 V). Pulling it to GND stops the fan.
- **Control = low-side switch via BSS138 (Q4):**
  - Gate ← FAN_PWM (GPIO6), with **R26 pull-up to 3.3 V** (boot disabled).
  - Source → GND.
  - Drain → CN2 pin 4.
  - The fan's own internal pull-up provides the idle-HIGH (no external pull-up).
- **Inverted logic:** GPIO6 HIGH → FET on → fan PWM line LOW → fan OFF.
  Firmware sets `output_invert` so the LEDC duty register maps directly to fan
  speed (see timing doc).
- **Boot behavior:** while GPIO6 is Hi-Z, R26 pulls the gate HIGH → FET on →
  fan PWM line held LOW → **fan disabled at boot** until firmware takes over.
- **TACH (CN2 pin 3):** open-collector, ~2 pulses/rev, pulled up to 3.3 V (R25
  10k). Valid because the fan supply is constant 5 V (not chopped). Read with PCNT.
- The fan's 5th pin (presence-detect to GND) is **not used**.

### 20 V rail measurement — ADC

- Divider: **R21 = 68 kΩ (top, from +20 V)** / **R20 = 10 kΩ (bottom)**, with
  **C40 = 100 nF** across the bottom for filtering + ADC sample charge reservoir.
- Scale factor: `V_rail = V_adc × (68 + 10) / 10 = V_adc × 7.8`.
  20 V → 2.56 V at the ADC; clips near ~24 V (≈21% headroom).
- Read on **ADC1_CH3 (GPIO4)** — ADC1 only, since ADC2 is unusable while Wi-Fi is
  active. Use 12 dB attenuation, eFuse calibration, oversample.
- This is the only **measured** rail voltage; the CH224A reports negotiated gear
  and current capability but not actual VBUS, so this divider is not redundant.

### Microphone — MSM261DGT003 PDM MEMS (MIC1)

- **MSM261DGT003**: omnidirectional, top-ported, 4×2×1.1 mm LGA, VDD = 3.3 V
  (1.6–3.6 V range; 670 µA typical in standard mode).
- **MIC_CLK (GPIO12)** → CLK; **MIC_DATA (GPIO13)** ← DATA. No series resistors
  (traces < 5 mm; ESD diodes also omitted for this enclosed prototype).
- **L/R (pin 6) = GND** → left-channel / Mic(Low): mic asserts DATA on CLK falling
  edge, host latches on rising edge. Matches ESP-IDF I²S PDM-RX default.
- **VDD decoupling**: 100 nF + 10 µF ceramic (C41, C42) immediately at pin 1.
- **Clock**: standard performance mode requires 1.1–4.0 MHz. Use ~3.072 MHz
  (48 kHz × 64); drive from ESP32-S3 I²S peripheral in PDM-RX mode.
- Mode is set by clock frequency alone — sleep ≤ 50 kHz, low-power 150–900 kHz,
  standard 1.1–4.0 MHz. No control register.
- **Top-ported package**: acoustic port faces away from PCB

### Status LED — WS2812B-B (LED1)

- Single WS2812B-B RGB LED; VDD = **+5 V** (from the +5 V rail), GND.
- **LED_DATA (GPIO11, module pin 19)** → data in. Driven by **RMT TX** peripheral
  (one of 4 available channels; none previously allocated).
- Firmware: ESP-IDF `led_strip` driver (RMT backend). For static color, write once
  at init; LED holds state until power-cycled. Reset pulse ≥ 50 µs handled
  automatically by the driver.

### Laser module NTC — temperature monitoring

- **10 kΩ NTC, B = 3950**, mounted on the Numb12 laser module (nominal operating
  temperature 45 °C).
- Read on **ADC1_CH9 (GPIO10, module pin 18)** — ADC1 only (ADC2 unusable with
  Wi-Fi active). GPIO10 is the last free ADC1 pin; sits adjacent to MIC_CLK/DATA.
- **Divider**: 10 kΩ pullup to 3.3 V (near ESP32) / NTC lower leg to GND.
  Midpoint ≈ 1.0 V at 45 °C, well within 12 dB attenuation range.
- **Filter cap**: 100 nF across NTC at the board-edge connector (NTC cable entry),
  not at the ESP32. Filters cable-induced noise at the source. No second cap at the
  ESP32 end — without series impedance between them the two caps would simply be in
  parallel, losing the entry-point filtering benefit.
- Measurement range: −14 °C to +114 °C; resolution ~0.03 °C/LSB at 45 °C.
- ADC config: same as VIN_ADC — 12 dB attenuation, eFuse calibration, oversample.
- Temperature conversion (B-value equation):
  ```c
  float r_ntc  = 10000.0f * v_adc / (3.3f - v_adc);
  float temp_k = 3950.0f / (logf(r_ntc / 10000.0f) + 3950.0f / 298.15f);
  float temp_c = temp_k - 273.15f;
  ```
- **Safety interlock**: add NTC in-range check to the laser-enable gate alongside
  PD handshake and VIN ADC checks (see safety invariant 2).

### USB-C PD sink — CH224A (I²C-only)

- Negotiates **20 V** from the USB-C source. CFG1 = 120 kΩ → GND selects 20 V and
  enables I²C; PG pin is **not connected** (monitoring is via I²C polling only).
- I²C: 400 kHz, **7-bit address 0x22 or 0x23** (confirm on hardware), **single-
  byte read/write only** (no auto-increment).
- Registers used by firmware:
  - **0x09** (RO, status): bit3 = PD handshake good; bits 0/1/2 = BC/QC2/QC3;
    bit5 = EPR exist; bit6 = AVS exist.
  - **0x50** (RO, current): max available current in the negotiated gear, unit
    **50 mA/count** (valid only after handshake).
  - **0x0A** (WO, voltage): request gear — `0`=5 V, `1`=9 V, `2`=12 V, `3`=15 V,
    `4`=20 V, `5`=28 V. Not required (CFG1 strap already selects 20 V), but
    available to re-assert/override.
- **No hardware fault interlock** (PG dropped): firmware must gate laser enable on
  I²C checks (PD good + adequate current) and the ADC reading, and re-poll
  periodically.

## Safety invariants for firmware

1. **Never drive two laser EN lines high at once** (common cathode) — guaranteed
   by the MCPWM single-timer scheme with Σ(on-time) + guards ≤ period and atomic
   updates. See [`timer-strategy.md`](timer-strategy.md).
2. **Lasers off until power is confirmed:** before enabling MCPWM laser outputs,
   confirm CH224A PD handshake (0x09 bit3) + sufficient current (0x50) + ADC rail
   in range + NTC temperature in range. The 10k EN pulldowns hold lasers off until
   then; re-check NTC periodically and disable on over-temperature.
3. **All ESP32-facing pull-ups go to 3.3 V**, never 5 V/20 V (not 5 V tolerant).
4. Keep total laser power within the CH224A-reported current budget (0x50 ×50 mA
   at 20 V).
