# Ringer Driver, Version 2 (PhotoMOS H-bridge, 150–160 V)

Replaces the DRV8825 ring driver in `doc/initial_design.md` (section "Ring driver") and the stage 11 plan in
`doc/validate_board_plan.md`. Written 2026-09-27 from bench measurements and a design discussion on the same day. Power chain (series resistor and
supercapacitors, section 4.2) revised 2026-09-28.

**Status:** the ringer's mechanics and polarity are **measured** (section 2). The circuit (section 4) is **Proposed**:
parts are chosen but not yet bought or built. The build plan (section 6) has not been run.

**Why a new driver.** The DRV8825 at 38 V rang the bell only from 5 to 11 Hz, and weakly; at 20 Hz it rattled and at
25 Hz it barely tapped (`validation_log.md`, 2026-09-25). The coils are about 34 H in series, so at 38 V the current rises
too slowly to build full force in a 25 ms half-cycle (di/dt = V/L, about 1.1 mA/ms). The original ring supply was about
90 V RMS (127 V peak) at 20 Hz. The DRV8825 cannot go above 45 V, and every H-bridge driver found in earlier searches
scaled voltage, current and size together. This design uses parts made for **high voltage at low current**: a nixie-tube
boost module and optically isolated MOSFET relays (PhotoMOS).

## 1. Names used in this document

| Name | Meaning |
|---|---|
| **Bell 1** | The bell the hammer rests against when no current flows. |
| **Bell 2** | The other bell. |
| **Coil A, coil B** | The two coils. At rest the armature (the iron rocker bar) touches **coil B's** core. |
| **A-black, A-yellow, B-black, B-yellow** | The coil leads, by coil and wire color. Nothing is assumed about polarity from position; polarity was measured (section 2.3). |
| **Forward** | Drive that pulls the armature from coil B to coil A, swinging the hammer from bell 1 to bell 2. |
| **Reverse** | Drive that pulls the armature back toward coil B, swinging the hammer into bell 1. |
| **Press / release** | Moving the armature by hand from coil B to coil A, and letting it go back. |

## 2. The ringer

### 2.1 What it is

The donor is a Kellogg phone (Kellogg Switchboard & Supply Co., no model number). The ringer is a **biased, polarized
ringer**:

- **Two brass bells** with a hammer (a small brass ball) on a **flexible arm** between them.
- The arm is fixed to the middle of an **iron armature** that rocks across the ends of **two electromagnet cores**. A
  **permanent magnet** puts a steady flux through the armature and cores.
- Current in the coils adds to the magnet's flux at one core and subtracts at the other, so the armature is pulled toward
  one core or the other **depending on the current's direction**. So polarity matters.
- A **bias spring** holds the hammer **against bell 1** at rest. This is the intended design: it damps bell 1 and holds
  the hammer firmly, so that dial pulses and hook-switch transients on the old line could not make the bell click or
  tinkle. Only a sustained ringing signal moves it.
- A **screw** looks like it adjusts the spring tension, and there are others. **Leave them at the factory setting**
  (mark them before turning any).
- The phone's network had a capacitor marked **"225"** (2.2 µF), almost certainly the ringer's series capacitor on the
  line (`initial_design.md`). It is not used in this design, but see section 7.1.

### 2.2 How it rings

- **Forward stroke (to bell 2).** The armature is pulled across until it stops against coil A's core. The hammer does
  **not** touch bell 2 in that held position: the flexible arm carries on by momentum, strikes bell 2, and bounces back
  off it, so bell 2 rings freely. **The strike depends on the hammer's speed when the armature stops, not on holding
  force.** A short, strong pulse is what matters.
- **Return stroke (to bell 1).** When the forward drive stops, the bias spring and the bounce bring the armature back. By
  hand (press and release at 1 to 2 per second) the spring alone does **not** strike bell 1 hard; the hammer just settles
  back against it. The original 20 Hz AC drove the armature in both directions every cycle, so the **reverse half-cycle is
  what makes bell 1 ring loudly**.
- **During ringing**, each reverse stroke throws the hammer into bell 1 and the next forward stroke pulls it away within
  about 25 ms, so bell 1 rings in between.
- **When a burst ends**, the hammer settles onto bell 1 and cuts it off. That "ring ... click, quiet" ending is authentic
  and comes free.

### 2.3 Measurements (2026-09-26 and 2026-09-27)

**Each coil, alone** (the lead that joined them was cut so each coil can be measured and wired separately):

| Quantity | Value | How |
|---|---|---|
| DC resistance | about 2000 Ω (marked "2000") | multimeter |
| At 100 Hz | L 17 H, Q 1.9, θ 62°, R 20 kΩ, X 8 kΩ, D 0.53 | LCR meter, as read. The equivalent-circuit mode (series or parallel) was not recorded, so these do not all fit one series model; use L and the DC resistance. |
| Both coils | identical readings | |

**Flick test (generator polarity).** Scope across one coil at a time, nothing else connected, armature moved by hand:

| Scope + | Scope − | Press (B → A) | Release (A → B) | Peak |
|---|---|---|---|---|
| A-yellow | A-black | negative spike | positive spike | 2 to 3 V, depends on speed |
| B-yellow | B-black | positive spike | negative spike | 2 to 3 V, depends on speed |

Rule: the polarity a coil **generates** during a motion is the polarity to **apply** to drive that motion. So:

| Coil | Forward (to bell 2): apply + to | Reverse (to bell 1): apply + to |
|---|---|---|
| A | **A-black** | A-yellow |
| B | **B-yellow** | B-black |

Both coils give similar peaks, so each is about equally strong and each can drive the armature on its own.

**Factory wiring:** the cut lead originally joined **A-yellow to B-yellow**. With the yellows joined, **+ on A-black and −
on B-black is forward**; the reverse polarity is reverse. This matches the flick test.

**Static DC test (2026-09-27).** Bench supply 55 V, current limit 25 mA, + on A-black, − on B-black, yellows joined:

- The hammer left bell 1, struck bell 2, and the armature was **held against coil A** with the hammer clear of bell 2.
- Steady current **13 mA at 55 V**, so the series pair is about **4.2 kΩ**, as expected from two 2 kΩ coils.
- So 13 mA of steady current is enough force to pull the armature over. At 20 Hz the current never reaches its steady
  value, which is why the drive needs much more voltage than 55 V.

This test also shows the coils are fine. The last DRV8825 entry in `validation_log.md` (2026-09-26: "no wiring
combination rang at all") was therefore a driver or supply fault, not the bell.

**Derived numbers** (estimates, for sizing):

- Time constant L/R is about **8.5 ms** for one coil, and about the same for the pair (roughly 34 H over 4 kΩ; more if the
  coils are strongly coupled). A 20 Hz half-cycle is 25 ms.
- Current rises at about V / L at first: **1.1 mA/ms at 38 V, 4.4 mA/ms at 150 V** (series pair, 34 H).
- Steady current at 150 V would be about 36 mA; at 160 V about 38 mA. The on-time is short, so real peaks stay lower.
- Energy stored in the coils at 30 mA is about ½ L I² ≈ 15 mJ. It has to be dumped at every turn-off (section 4.4).
- **Power while ringing** is about 1.5 to 2 W (the original was about 90 V × 15 mA). Averaged over the US cadence (2 s on,
  4 s off) it is about 0.5 to 0.7 W.

### 2.4 Ideas that were checked and set aside

- **"An H-bridge (negative voltage) is not needed."** It is needed for a loud two-bell ring, because the reverse stroke
  makes bell 1 ring (section 2.2). The split-coil option (section 7.3) gives both directions without reversing any
  voltage, but it has its own problem.
- **"Both coils are needed."** Each coil alone can drive the armature (flick test). The original series wiring is used
  first because it is how the ringer was designed to work.
- **"90 V AC is needed."** Not as such. What is needed is enough voltage to build the current quickly in a short pulse,
  which comes out at about 150 to 160 V DC in pulses.

## 3. What we want

| Requirement | Target | Source |
|---|---|---|
| Sound | An **authentic, loud ring**: both bells struck on every cycle, no rattle | user |
| Frequency | Set by the region profile. **US: 20 Hz.** Other regions: **not verified**; research later | `tones.md` section 6 |
| Cadence | US 2 s on / 4 s off; UK double ring 0.4 on, 0.2 off, 0.4 on, 2.0 off | `tones.md` sections 3 and 6 |
| Region profiles | If the bell only works well at one frequency, lock the phone's other historical features to that region | user |
| Ring trip | Ringing stops at once when the handset is lifted, even mid-pulse | `tones.md` section 4 |
| Boot safety | Silent at power-up, reset and firmware crash | stage 11 |
| Interference | No false dial pulses, no hook changes, no audible noise in the earpiece or microphone while ringing | stages 7 and 10 |
| Power | From the board's USB-fed charger rail only (the battery connector), through a series resistor into supercapacitors. **No battery**: the board has no low-voltage cutoff, and a plugged-in phone is more authentic. It must be safe in **any** order of plugging, unplugging and connecting, with no manual steps | user, 2026-09-27 |
| USB current | The ringer must never draw a large surge from USB. A 1.5 F supercapacitor wired straight to the battery pins **destroyed a USB hub** | user, 2026-09-27 |
| Construction | **Off-the-shelf modules and through-hole parts on perfboard.** No custom PCB | user |
| GPIOs | **Two**: IO22 and IO19, the pins freed by removing the DRV8825 (STEP and nENABLE) | `audio_kit_v2.2.md` section 6 |
| Measurement | Infer current from voltages and behavior. **No current-probe or current-amplifier project** | user |

## 4. The circuit

### 4.1 Overview

```
 board battery connector (charger rail, about 4.2 V; no battery fitted)
        │
   R_s: 4 × 39 Ω ½ W in parallel (9.75 Ω)       limits the current drawn from USB
        │
   supercap node ──── C_s: 2 × 1.5 F / 5.5 V supercapacitors in parallel (3 F) ── GND
        │
   XL6019 boost module (already owned), set to 10 V
        │
   10 V rail ──────── C_10: 3 × 1500 µF / 16 V in parallel ── GND
        │
   MAX1771 nixie boost module, 5-12 V in, 150-220 V out (set 150 V; 160 V at most)
        │
   HV rail ──┬── C1, C2: 2 × 150 µF / 450 V in parallel ── GND
             │   (no separate bleeder: the module's feedback resistors drain them; measured in R1)
             │
     ┌───────┴───────────────┐
     │                       │
  U1 ch1 (SW1)            U1 ch2 (SW3)          U1, U2 = ASSR-4128 (dual PhotoMOS, 400 V, 100 mA)
     │                       │
     X ── A-black [coil A] A-yellow ═ B-yellow [coil B] B-black ── Y
     │         Z1 (P6KE180CA) from X to Y         │
  U2 ch1 (SW2)            U2 ch2 (SW4)
     │                       │
     └───────┬───────────────┘
             │
          R_feed 100 Ω        (low side: fault limit and current view)
             │
            GND  (board GND)
```

| Direction | Switches on | Coil polarity |
|---|---|---|
| **Forward** (bell 2) | SW1 + SW4 | X (A-black) +, Y (B-black) − |
| **Reverse** (bell 1) | SW3 + SW2 | Y (B-black) +, X (A-black) − |
| Off | none | coil current decays through Z1 |

### 4.2 Power chain

- **Source:** the board's battery connector (the charger's battery-side rail, about 4.2 V), the only place USB power is
  reachable without soldering to the board. No battery is fitted. **These pins do not limit the current they pass from
  USB when their voltage is low**: a 1.5 F supercapacitor connected straight to them destroyed a USB hub (2026-09-27).
  The path is not known (perhaps the power-path MOSFET Q1). So nothing with large capacitance may connect to them
  directly; everything goes through R_s.
- **R_s, the series resistor:** four 39 Ω ½ W resistors in parallel, 9.75 Ω. It sets the most the ringer can ever draw
  from the pins: 4.2 V ÷ 9.75 Ω ≈ **0.43 A**, at plug-in with the supercapacitors empty, falling as they charge. Each
  resistor then dissipates at most 4.2² ÷ 39 ≈ 0.45 W, for a few seconds. **Keep each resistor 39 Ω or more** (33 Ω would
  be 0.53 W each); change the total by changing how many are in parallel (table below). R_s also shows the current: the
  voltage across it ÷ 9.75 (1 V is about 0.1 A), measured with the multimeter.
- **C_s, the supercapacitors:** two 1.5 F / 5.5 V (owned) in parallel, 3 F, on the supercap node. They supply the
  ringing peaks, and R_s refills them between bursts. From 4.2 V down to 3.3 V they give ½ × 3 × (4.2² − 3.3²) ≈ **10 J**.
  After a plug-in with them empty they take about 45 s to reach 3.3 V (time constant 9.75 Ω × 3 F ≈ 29 s).
- **The limit this sets:** R_s can only deliver a small steady power. With the node at about 3.7 V it passes
  (4.2 − 3.7) ÷ 9.75 ≈ 50 mA, about **0.2 W**. A burst of ringing takes more than that, and the difference comes out of
  C_s. So the ringer can ring for a limited number of cadence cycles, then must pause while C_s refills. A rough
  estimate: 3 to 6 J from the node per US cycle (2 s ringing, 4 s silence; section 2.3's 1.5 to 2 W while ringing, and
  about 70 % efficiency through both modules), of which R_s supplies about 1.2 J. So C_s loses 2 to 5 J per cycle and
  lasts **about 2 to 5 cycles (12 to 30 s)**; a call rings about 25 to 30 s before voicemail answers. R5 measures the
  real number. When C_s runs down, the XL6019 drops out and the bell weakens; nothing is harmed
  and the board keeps running from USB.

  | R_s (39 Ω each) | Total | Most current from USB | Steady power at 3.7 V | Use |
  |---|---|---|---|---|
  | 4 in parallel | 9.75 Ω | 0.43 A | about 0.2 W | **start here**; safe on a bus-powered hub |
  | 6 in parallel | 6.5 Ω | 0.65 A | about 0.28 W | on a Mac port or a 2 A adapter, if R5 needs more rings |
  | 8 in parallel | 4.9 Ω | 0.86 A | about 0.38 W | 2 A adapter only |

- **Start-up:** after a plug-in, if the MAX1771 starts charging C1 and C2 before C_s is charged, it asks for more power
  than R_s can give, and the node can sit near the XL6019's drop-out for a while. R1 step 8 checks this. If it happens,
  delay the MAX1771 with its SHDN pin (a resistor-capacitor delay, or a GPIO).
- **XL6019** (owned, 3-35 V in, 5-40 V out): set to **10 V**. It is still set to about 38 V from the DRV8825 tests;
  **turn it down before connecting the MAX1771**, which is rated 5-12 V in. The MAX1771 switches an external MOSFET whose
  gate drive equals its input voltage, so 10 V gives it proper gate drive; 4.2 V directly might only half turn it on.
- **MAX1771 nixie module** (Amazon, about $10, "DC 5V-12V to 170V 150V-220V ... MAX1771 with Off Function"): output
  adjustable 150 to 220 V. Its **SHDN** pin is active-high on the chip (high = off). Leave it at the "on" level with no
  GPIO (check which level that is on this board, section 6 step R1). A GPIO on SHDN, so that high voltage exists only while
  ringing, is a later upgrade if a pin frees up. It would also stop the module's idle switching during calls (section
  4.8) and could delay start-up (above).
- **C_10, the 10 V capacitors:** three 1500 µF / 16 V (owned) in parallel, 4500 µF, on the XL6019 output. They steady
  the MAX1771's input. They sit after R_s, so charging them never reaches USB. Remove any large capacitor from the
  XL6019 **input** (such as the 3300 µF used before); keep its 10 µF ceramic.
- **C1 and C2, 2 × 150 µF / 450 V** (owned) in parallel carry the burst. At 160 V they hold about 3.8 J and give about
  1.7 J while sagging to 120 V.
- **No separate bleeder.** The MAX1771 module's feedback resistors sit across its output and drain C1 and C2 after the
  power goes. R1 step 6 measures how long that takes; **that time is the wait before touching anything**. If it is more
  than about an hour, fit two 470 kΩ ½ W resistors in series across C1 (about 27 mW at 160 V; below 30 V in about 8 minutes with both capacitors).
  C1 and C2 must stay wired directly to the module's output, never through a connector, or nothing drains them.
- **Budget from USB:** the board itself plus at most the R_s current (0.43 A at plug-in, about 50 mA once running). See
  section 6 step R8 for the rail check.

### 4.3 The H-bridge

- **Switches:** two **Broadcom ASSR-4128-002E**: 2 Form A (two normally-open channels), MOSFET output (AC/DC), 400 V,
  100 mA, 35 Ω maximum on-resistance, DIP-8, each channel with its own LED input and its own two output pins. About $3.60.
  Buy three or four; high-voltage experiments kill parts.
- **U1 holds both high-side switches** (SW1, SW3) and **U2 both low-side switches** (SW2, SW4), so each chip's two
  outputs sit at similar voltages. Take the pinout from the datasheet (AV02-0218EN).
- **Why PhotoMOS:** each switch is isolated from its LED, so the high-side switches need no gate driver, bootstrap or level
  shifter. That removes the part of an H-bridge that forced the big driver chips.
- **Worst-case switch voltage** is about the supply plus the TVS clamp: 160 + about 190 V ≈ 350 V, under the 400 V
  rating (section 4.4).
- **Do not substitute** Form B (normally closed) relays, triac-output "AC" or zero-cross SSRs (they never turn off on DC),
  or phototransistor optocouplers (voltage too low).

### 4.4 Turn-off: the TVS, and the voltage window it sets

When all switches open, the coils' stored energy (about 15 mJ) must go somewhere. The PhotoMOS outputs have no diode path
for it, so without a clamp the voltage would rise until the switches break down.

- **Z1: P6KE180CA** (bidirectional TVS) directly across X and Y. It clamps both polarities and dumps the energy quickly,
  so the current falls in a few milliseconds and the armature is released for the next stroke. That is what the old
  DRV8825 wiring lacked if its diodes held the current up (a slow decay fights the spring's return).
- **Its ratings** (typical for the part; check the datasheet): standoff voltage (negligible leakage) about **154 V**,
  breakdown **171 to 189 V**. At our tens of milliamps it clamps near the breakdown voltage, about **180 to 190 V**.
- **Z1 sets the supply window: 150 to 160 V.**

  | Limit | Why | Result |
  |---|---|---|
  | Z1 must stay off while driving | Z1 sees the drive voltage on every pulse: the supply minus about 5 to 10 V (two switches at 35 Ω and R_feed, at about 40 mA). That must stay below the standoff voltage. | supply ≤ about 160 V |
  | Switches below 400 V | At turn-off an open switch sees about the supply plus the clamp voltage | 160 + about 190 ≈ 350 V: OK |
  | MAX1771 minimum | Its output adjusts from 150 V up | supply ≥ 150 V |

- The clamp voltage (about 185 V) is above the supply, so the current falls faster at turn-off than it rose.
- It dissipates roughly 0.5 to 1 W while ringing (about 15 mJ, 40 times a second at 20 Hz), well within its rating on a
  2 s burst. Expect it to get warm.
- **Do not use a plain flyback diode.** It would keep the current circulating for tens of milliseconds.

### 4.5 LED drive: two GPIOs with a hardware interlock

The four LEDs are wired **between** the two GPIOs, each with its own resistor:

```
IO22 (GPIO_F) ──┬── 470 Ω ──►|── U1 ch1 LED (SW1) ──┬── IO19 (GPIO_R)
                ├── 470 Ω ──►|── U2 ch2 LED (SW4) ──┤
                ├── 470 Ω ──|◄── U1 ch2 LED (SW3) ──┤
                └── 470 Ω ──|◄── U2 ch1 LED (SW2) ──┘
      ►|  = LED anode on the left;  |◄ = LED anode on the right
```

| IO22 | IO19 | Result | Board LED lit |
|---|---|---|---|
| 1 | 0 | **forward** (SW1 + SW4) | LED5 (IO19 low) |
| 0 | 1 | **reverse** (SW3 + SW2) | LED4 (IO22 low) |
| 1 | 1 | **off (idle state)** | none |
| 0 | 0 | off | both |

- **Both sides of the bridge can never be on together:** opposite LEDs cannot both conduct, so no firmware bug can short
  the HV rail through one side of the bridge. R_feed limits the current if a switch fails.
- **Boot safety comes from the wiring:** at reset both pins float (and both read 1 at boot, `validation_log.md`
  2026-09-19), so no LED current flows. Verify in step R2.
- **Resistor 470 Ω.** LED current is about (3.3 − 1.3) / 470 ≈ 4.3 mA. The
  datasheet's switching threshold is 0.5 mA, and it recommends 5 mA for full performance across temperature; at room
  temperature inside the phone 4.3 mA is ample. Each GPIO carries two LEDs (about 8.5 mA) plus the board LED on the low pin,
  keeping it under the board's **12 mA per pin** limit (`audio_kit_v2.2.md` section 2). With 330 Ω the pins would carry
  about 13 mA. The resistors also serve as the 10 to 100 Ω series resistors the board spec recommends.
- **Check the LED current without a current meter:** measure the voltage across one 470 Ω resistor while a direction is
  on. Current = V / 470 (2.0 V is 4.3 mA).
- **IO19 is slow** (its pad takes about 38 µs to follow a change, `audio_kit_v2.2.md` section 6). That does not matter here:
  the ringer's timing is in milliseconds.
- **Remove the DRV8825 and its 10 kΩ pull-up on IO19.** The DRV8825 also pulled IO22 low (`validation_log.md`,
  2026-09-25); without it IO22 idles high again.

### 4.6 R_feed and seeing the current

R_feed (100 Ω) sits between the bottom of the bridge and GND, so every drive current passes through it:

- **Fault limit:** if a switch fails short with the other side's switch on, the current is limited to about 1.7 A until
  the resistor fails open. A cheap fuse.
- **Current view, with no current probe:** the voltage across it is 0.1 V per mA (20 mA reads 2 V), referenced to board
  GND. Put the scope ground on GND and the probe on the top of R_feed. This is not the current-amplifier problem from past
  projects: the currents here are tens of milliamps at 20 Hz, so one resistor gives volt-level signals. It is optional;
  the plan works without it.
- The current that circulates through Z1 after turn-off does not pass through R_feed, so R_feed shows only the driven
  part of each pulse.

### 4.7 Scope safety on this circuit

- **Probes: a pair of Hantek PP-150** (switchable x1/x10; x10: 100 MHz, 10 MΩ, about 20 pF, rated 600 V DC + peak AC;
  x1: 6 MHz, 1 MΩ, 85 to 115 pF, rated 200 V; compensation range 20 to 40 pF).
- **Compensate each probe before first use:** set it to x10, clip it to the scope's CAL output, and turn its trimmer until
  the square wave has flat corners.
- **Use x10 for anything on the HV side** (the HV rail, X, Y, the coils). The scope reads 1/10 of the real voltage, so
  multiply the volts/div setting by 10.
- **x1 is only for low-voltage points:** R_feed (0.1 V per mA), the LED resistors, the GPIOs, and the 12 V circuit in step R3.
- The scope's ground clip is tied to mains earth. **Clip it only to board GND**, never to X, Y or the HV rail.
- To see the coil voltage (X − Y): CH1 on X and CH2 on Y (both x10, both grounds on GND), then CH2 invert and ADD, if the
  BK 1535A has those modes (look for "INV" and "ADD" on the vertical controls).

### 4.8 Noise into the audio

The codec's ADC is sensitive to its supply: a noisy USB hub raised the idle microphone noise from -81 to -20 dBFS
(2026-09-27). The ringer can reach the audio three ways:

- **Conducted back through the battery pins: handled by R_s and C_s.** They form a very slow filter. The XL6019's
  switching ripple sees 9.75 Ω against the supercapacitors' fraction of an ohm (about 30 dB less at the pins; the 10 µF
  ceramic takes the fastest edges), and the ringing bursts reach the board only as a slow change of a few tens of mA
  following the cadence, far below the audio band.
- **The MAX1771 during calls: open.** The phone never rings during a call, but the module keeps switching in short
  bursts to top up C1 and C2, and those bursts can fall in the audio band and couple from its inductor into the
  microphone wiring. A GPIO on SHDN would turn it off for the whole call.
- **Ground and magnetic pickup: open.** Connect the ringer's GND at the battery connector's GND, away from the microphone
  and line-in grounds. Route the microphone cable away from the coils, the modules and the HV wiring; twist the HV pair.

R7 measures all three.

## 5. Bill of materials

| Part | Qty | Status |
|---|---|---|
| Broadcom ASSR-4128-002E, dual PhotoMOS, DIP-8 | 2 (+1 or 2 spare) | chosen, $3.60 each |
| MAX1771 nixie boost module, 5-12 V in, 150-220 V out | 1 | chosen, about $10 |
| XL6019 boost module | 1 | owned (from the DRV8825 build) |
| Capacitor 150 µF / 450 V (C1, C2) | 2 | owned |
| Supercapacitor 1.5 F / 5.5 V (C_s) | 2 | owned |
| Capacitor 1500 µF / 16 V (C_10) | 3 | owned |
| Resistor 39 Ω (R_s; 39 Ω or more each) | 4 (up to 8, section 4.2) | kit |
| P6KE180CA bidirectional TVS (Z1) | 1 | owned |
| Resistor 100 Ω (R_feed) | 1 | owned (kit) |
| Resistor 470 kΩ (bleeder, only if R1 step 6 needs it) | 2 | kit |
| Resistor 470 Ω (LEDs) | 4 | owned (kit) |
| Hantek PP-150 scope probes, x1/x10 (section 4.7) | 2 | to buy |
| Perfboard, DIP-8 sockets, high-voltage wire | | wire owned |
| Resistor about 4.7 kΩ (dummy load for step R3) | 1 | owned (kit) |
| Resistors 4 × 5.6 kΩ in series, 22.4 kΩ (module load test, step R1) | 4 | owned (kit) |

All resistors are through-hole, ½ W. Each is used inside that rating: the largest loads are R_s at plug-in (up to 0.45 W
each for a few seconds, then about 0.03 W), the step R1 load (0.25 W and about 37 V per resistor) and the optional
bleeder (about 0.03 W and 80 V each). R_feed dissipates about 0.1 W while ringing; in a fault it burns open, which is its
purpose.

**Removed from the design:** the DRV8825 module and its nENABLE pull-up; the second XL6019 stage; any large capacitor
wired straight to the battery pins or the XL6019 input (the 3300 µF and the 1.5 F used before); the 2 × 100 kΩ R_bleed
(replaced by the module's own drain, measured in R1).

## 6. Build and test plan

Do the steps in order and stop at the first failure. **Change one thing at a time.** Record results in
`doc/validation_log.md` as stage 11 (v2), with the step names below.

**Firmware needed** (a rewrite of the `hwtest` `ring` command, `hwtest/main/cmds_ring.c`; not yet written):

| Command (proposed) | Does |
|---|---|
| `ring idle` | Both pins high (off). The state at the end of every command, on any error, and after the time limit. |
| `ring fwd <ms>`, `ring rev <ms>` | One pulse in one direction, then idle. |
| `ring run <Hz> [fwd ms] [rev ms] [dead ms] [s]` | Repeat: forward for `fwd ms`, idle for `dead ms`, reverse for `rev ms`, idle for the rest of the period. Time limit. |
| `ring sweep <from Hz> <to Hz> [dwell s] [step Hz]` | `run` at each frequency in turn, with the same pulse settings. |
| `ring cadence [us\|uk] [n]` | The cadence from section 3, `n` cycles. |
| `ring status` | Current settings and state. |

Timing needs only about 0.1 ms resolution: use `esp_timer` or a hardware timer, never the 10 ms loop. The defaults to
start from: 20 Hz, forward 12 ms, reverse 12 ms, dead 1 ms (so each direction is on for about half of its half-period).

### R0. Preparation

1. Mark every adjustment screw on the ringer before anything else, and leave them alone.
2. Take the DRV8825, its pull-up and its boost wiring off IO22 and IO19. Power up and run `boot` and `pins`: IO22 and
   IO19 should read 1 at boot (as on 2026-09-19).
3. Check the parts against section 5, and read the ASSR-4128 datasheet pinout.

**Pass:** pins free and idle high; parts on hand.

### R1. Power chain alone (no bridge, no bell)

1. **XL6019:** power it from the bench supply at 4.2 V, **nothing on its output**. Turn its pot down to **10 V**,
   measured with the multimeter.
2. **MAX1771:** connect it to the XL6019 output. With SHDN unconnected, measure its output (DC, 200 V+ range).
   - About 150 V or more: the board is on by default. Tie SHDN to GND, or leave it as is.
   - Near 0 V: tie SHDN to GND and measure again. Still off: the board's logic is inverted; tie SHDN to its input.
3. Set the output to **150 V** (its lowest). Note the range the pot gives.
4. **Load test:** four 5.6 kΩ resistors in series (22.4 kΩ) across the output: about 6.7 mA and 1 W at 150 V, 0.25 W
   in each resistor. The output should hold within a few
   volts. Read the bench supply's current at 4.2 V: this is the chain's input current for about 1 W out. After a minute, check the MOSFET and inductor on both modules are warm at most, not hot.
5. **XL6019 drop-out:** keep the 22.4 kΩ load. Lower the bench supply from 4.2 V in 0.1 V steps and note the input
   voltage at which the 150 V output starts to fall (V_floor). Hold just above it for a minute and feel the MOSFETs on
   both modules: warm is fine. V_floor is the lowest useful supercap voltage; section 4.2 assumes about 3.3 V.
6. **HV capacitors and drain time:** remove the load, fit C1 and C2 (no bleeder) and C_10. Power up at 4.2 V and time
   how long the output takes to reach 150 V; read the bench supply's current during that charge (the chain's full-power
   input current). Then unplug and read the HV voltage at 0, 5, 10 and 20 minutes. The time constant is
   τ = t ÷ ln(V0 ÷ Vt), and the time to fall below 30 V is about 1.6 × τ. **Record that time: it is the wait before
   touching the circuit, from here on.** If it is more than about an hour, fit the 2 × 470 kΩ bleeder and measure again.
7. **R_s and C_s alone:** C_s (both supercaps, polarity checked) behind R_s, nothing after them. Bench supply 4.2 V,
   current limit 1 A. Just after power-up, the voltage across R_s should be close to 4.2 V (0.43 A), falling as C_s
   charges; C_s should reach 3.3 V in about 45 s. Feel R_s: warm at most.
8. **The whole chain through R_s,** from empty (C_s drained through a resistor first): bench supply at 4.2 V, then R_s,
   C_s, the XL6019, C_10, the MAX1771, C1 and C2. Power up and watch the supercap node and the HV output. The node should
   climb past V_floor and the HV should reach 150 V within a few minutes. If the node sits near V_floor for longer, the
   MAX1771 needs a start-up delay on SHDN (section 4.2).

**Pass:** 150 V holds under a 1 W load, modules warm at most, V_floor known, the HV drain time measured and recorded,
the chain starts from empty through R_s with the current from the supply never above about 0.45 A.

### R2. LEDs and switches, no high voltage

1. Fit U1, U2 and the four 470 Ω resistors to IO22, IO19. Leave the outputs unconnected.
2. With the ohmmeter across each switch's output pins in turn, drive the pins with the `hwtest` `set` command:
   - `set 22 1`, `set 19 0` (forward): SW1 and SW4 read tens of ohms; SW2 and SW3 read open.
   - `set 22 0`, `set 19 1` (reverse): the opposite.
   - Both 1, then both 0: all four open.
3. In forward, measure the voltage across one LED resistor: expect about 2 V (4.3 mA).
4. **Boot safety:** with the ohmmeter on SW1, press RESET and unplug and replug USB several times. It should stay open.
   Repeat on SW3.

**Pass:** the truth table in section 4.5 is right, LED current about 4 mA, switches stay open through reset.

### R3. Bridge on low voltage, dummy load

1. Wire the bridge completely (section 4.1) with **a 4.7 kΩ resistor in place of the coils**, Z1 fitted, R_feed fitted.
   Feed the top from the **bench supply at 12 V, current limit 25 mA** instead of the HV chain.
2. `ring fwd 1000`: X is about 12 V above Y (multimeter across the load). `ring rev 1000`: Y above X.
3. `ring run 20` on the scope: across R_feed, a square pulse of about 2.5 mA (0.25 V) in each half-period. With CH2
   inverted and ADD, the load voltage alternates + and −, with the dead time visible between.

**Pass:** correct polarity both ways, clean waveform, no current in idle.

### R4. Bell on the 55-60 V bench supply

1. Replace the dummy load with the coils: **yellows joined, A-black to X, B-black to Y.** Bench supply 55 V, 25 mA limit.
2. `ring fwd 50`: one strike on bell 2, then the hammer returns to bell 1. `ring rev 50`: the armature seats harder and
   nothing strikes (at rest it is already against coil B).
3. `ring fwd 50` followed at once by `ring rev 50` (or `ring run 2`): bell 2, then a stronger return onto bell 1.
4. `ring run 10`, then `ring sweep 8 25`. Compare with the DRV8825 at 38 V (strong 5-11 Hz, weak above).
5. Change one setting at a time and note the sound: forward width, reverse width (the spring makes the two sides
   unequal), dead time.
6. On the scope across the coils, look at the turn-off: a flat step at about the TVS voltage lasting a few milliseconds
   is the energy being dumped. If it lasts most of the dead time, the current is not decaying fast enough.

**Pass:** both bells strike; the best 55 V settings recorded. It will not be loud at 20 Hz yet.

### R5. High voltage

1. Power off, wait the drain time measured in R1 step 6 (or discharge C1 and C2 through a resistor and check with the
   meter), and connect the HV chain (R1, including R_s and C_s, fed from the bench supply at 4.2 V) in place of the 55 V
   supply. **From here on the circuit is at 150 V or more: insulate everything, one hand only, never touch it while
   powered or until the drain time has passed.**
2. At 150 V, repeat R4 steps 2 to 5, starting at 20 Hz with the best 55 V settings.
3. Find the settings that give a loud, clean ring at 20 Hz. Then sweep 15 to 30 Hz with them, and record the useful range.
4. Only if 150 V is not loud enough: raise the voltage to **160 V at most** (the limit set by Z1, section 4.4) and repeat
   step 3. If that is still not enough, see section 7.5.
5. After a 2-minute run of the US cadence, feel (with the power off and the capacitor drained) the TVS, the PhotoMOS chips,
   R_feed and both modules. Warm is fine; too hot to hold is not.
6. Watch the HV rail on the multimeter during a 2 s burst: note how far it sags and how long it takes to recover.
7. **Ring budget:** start from C_s full (the node at about 4.2 V) and run `ring cadence us 10`. Write down the supercap
   node voltage at the end of each burst, and the voltage across R_s (÷ 9.75 = current). Count the cycles until the node
   falls to V_floor + 0.2 V, then time how long it takes to climb back to 4 V. Fewer than about 5 cycles: shorten the
   pulses or lower the voltage and repeat, or go to 6 resistors in R_s (section 4.2 table) if the board will run from a
   Mac port or a 2 A adapter.

**Pass:** a loud, clean two-bell ring at 20 Hz (or at the best frequency found), at a recorded voltage and pulse setting,
with nothing hot, and the number of cadence cycles C_s supports recorded.

### R6. Cadence, ring trip and faults

1. `ring cadence us 10` and `ring cadence uk 10`. Listen for a steady ring through the whole burst, and the clean
   stop at the end.
2. **Ring trip:** the firmware must go to idle when the hook opens, even mid-pulse. Test with the hook contact connected
   (the dial work in stage 10 has the hook on IO13).
3. **Firmware fault:** press RESET mid-ring. The bell stops at once and stays silent through boot.
4. **Supply loss:** unplug USB mid-ring. The bell stops; C1 and C2 drain over the time measured in R1. (With the chain
   on the battery pins, R8 repeats this.)

**Pass:** cadences correct, lifting the handset stops the bell at once, silent through reset.

### R7. Interference

These are the waiting items from stage 10 step 5 and stage 7 test 7.

1. `dial trials 20` while `ring cadence us` runs: no wrong digits, no false hook changes.
2. `audio on 16000`, then `mic avg 30` in three conditions (section 4.8): the ringer chain unplugged (the baseline,
   about -81 dBFS on a clean supply), the chain powered but idle, and `ring cadence us` running. **The idle figure is
   the one that matters for calls**; it should be within a couple of dB of the baseline. Also `mic snr ringing`, and
   listen to the earpiece.
3. A Bluetooth call connected while ringing: no drops (a real incoming call rings while the link is up).

**Pass:** no false digits or hook changes; no audible noise added to the earpiece or microphone.

### R8. The board's power rail

Use a Mac port or a USB hub you can afford to lose for this step. (A 1.5 F capacitor straight on these pins destroyed a
hub, and on 2026-09-25 connecting the old boost converter to them dropped the USB device.)

1. **The pins alone:** USB plugged in, nothing on the battery connector. Measure the pin voltage (expect about 4.2 V,
   perhaps a slow sawtooth from the charger with no battery). R_s's current figures assume 4.2 V; if the pins sit higher,
   the plug-in current is higher in proportion.
2. **Connect the chain through R_s,** with C_s drained first, board already running. The voltage across R_s at the
   first moment should be about the pin voltage (0.43 A at 4.2 V). The board must not reset or drop USB.
3. **Unplug and plug in USB** with the chain connected: from empty (C_s drained), and again right after an unplug (C_s
   partly charged), several times each. The board boots and stays on USB every time.
4. **Ring:** `ring cadence us 10`. Watch the supercap node and the board: the board keeps running, and the ring budget
   matches R5 step 7.
5. **Supply loss:** unplug USB mid-ring. The bell stops. C_s then feeds back through R_s into the board (through Q1),
   so the board may keep running, or reset a few times, until C_s is drained. That is expected and harmless.

**Pass:** the board runs normally in every plug and unplug order, through ringing and recharge, and never draws more
than the R_s limit.

### R9. Final assembly

1. Build on perfboard with at least 3 mm between high-voltage and low-voltage copper, HV wire for the HV runs, and
   insulation over every HV joint.
2. Label the board "160 V DC: wait <drain time from R1> after unplugging" near C1 and C2.
3. Repeat R6 and R7 inside the phone.
4. Record the final settings (voltage, frequency, pulse widths, dead time) per region profile in `initial_design.md`.

**Pass:** the phone rings in its case, loud and clean, with everything in R6 and R7 still passing.

## 7. Options to try if the plan falls short

### 7.1 The original series capacitor

The phone's "225" (2.2 µF) capacitor was in series with the ringer on the line. With the coils at roughly 34 H, a series
2.2 µF resonates near **1 / (2π √(34 × 2.2 µF)) ≈ 18 Hz**, close to 20 Hz. So the original ringer circuit may have been
**tuned to the ring frequency**: at resonance the capacitor cancels most of the coils' reactance and more current flows
than the coils alone would allow. The inductance changes with the armature's position and was only measured at 100 Hz, so
this is a hint, not a result. To try it: a new 2.2 µF film capacitor rated 250 V or more in series with the coils (do
not reuse the 80-year-old part), driven with a plain square wave (forward and reverse, no dead time), and a frequency sweep.

### 7.2 Coils in parallel

For the same forward polarity: **A-black with B-yellow** to X, and **A-yellow with B-black** to Y. That is 1 kΩ, twice the
current at the same voltage, with a similar time constant. The DRV8825 parallel trial rang more weakly, but its lead
pairing was not recorded against the polarity table above.

### 7.3 Split coils (two switches, no H-bridge)

Both blacks to the HV rail; a low-side switch on A-yellow pulls forward, and one on B-yellow pulls reverse. It needs only
two switches, and a single coil builds force about twice as fast at the same voltage. **The catch:** while one coil is
driven, the other acts as a transformer secondary and its free end can rise to about twice the supply plus spikes (the
"centre-tap" effect of unipolar steppers). At 160 V that exceeds 400 V with spikes. It would need a lower supply, a TVS across each
coil, or higher-voltage switches. The same ASSR-4128 parts can be rewired for it.

### 7.4 Closed-loop timing

The flick test showed each coil generates 2 to 3 V when the armature moves. A future version could read that back-EMF on
an ADC pin to time each pulse to the armature's real motion. Set aside until open-loop drive is exhausted, and there are
no spare GPIOs today.

### 7.5 More than 160 V: four diodes instead of Z1

If 160 V is not loud enough, replace Z1 with the standard H-bridge clamp: a diode across each switch, cathode toward the
HV rail (X to HV, Y to HV, GND to X, GND to Y), rated at least 400 V: 1N4004 to 1N4007 (1N4007 is 1000 V; slow recovery is fine at 20 Hz). The 1N4001 to 1N4003
(50 to 200 V) and Schottky diodes such as the 1N5817 (20 V) are not suitable. At turn-off the
coil current flows through two diodes back into C1: the coil sees about minus the supply, so the current still falls
quickly, and **no switch sees more than the supply plus a diode drop**. The supply can then go up to the MAX1771's 220 V,
and the coil energy is returned to C1 instead of heating a TVS. The cost is four parts instead of one.

## 8. Open items

- **Ring frequencies for regions other than the US** are not verified (`tones.md` has only the US 20 Hz). Research them
  before building the region profiles.
- Why the battery pins pass current from USB with no limit when their voltage is low (a 1.5 F capacitor on them
  destroyed a hub). R_s makes the answer unnecessary, but it is not understood.
- The ring budget: how many cadence cycles C_s supports (step R5 step 7), and whether that is enough.
- Whether the MAX1771 needs a start-up delay on SHDN (step R1 step 8).
- The MAX1771's idle switching during calls (section 4.8, step R7).
- The LCR meter's equivalent-circuit mode for the 100 Hz readings, if the numbers are needed again.
- Whether a GPIO can be found for the MAX1771 SHDN pin: high voltage only while ringing, no switching noise during
  calls, and a start-up delay, all in one.
- Updating `initial_design.md` (Ring driver, Ring-driver power, BOM) and `validate_board_plan.md` stage 11 to point here.

## Sources

- Broadcom ASSR-4118/4119/4128 datasheet (AV02-0218EN): <https://docs.broadcom.com/docs/AV02-0218EN>
- Broadcom ASSR-4110/4111/4120 datasheet (AV02-0279EN), single-channel alternative: <https://docs.broadcom.com/docs/AV02-0279EN>
- MAX1771 5-12 V to 150-220 V module (Amazon): <https://www.amazon.com/5V-12V-150V-220V-Voltage-MAX1771-Function/dp/B09P47K5C6>
- Omnixie NCH8200HV (2.5-15 V in, 170 V out; considered, $72): <https://www.tindie.com/products/omnixie/nch8200hv-nixie-high-voltage-power-module/>
- `doc/tones.md` (ring frequency and cadences), `doc/validation_log.md` (DRV8825 results, 2026-09-25 and 26),
  `doc/audio_kit_v2.2.md` (GPIOs, pin current limit).
