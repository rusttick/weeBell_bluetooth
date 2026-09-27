# Ringer Driver, Version 2 (PhotoMOS H-bridge, 150–220 V)

Replaces the DRV8825 ring driver in `doc/initial_design.md` (section "Ring driver") and the stage 11 plan in
`doc/validate_board_plan.md`. Written 2026-09-27 from bench measurements and a design discussion on the same day.

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
- Steady current at 150 V would be about 36 mA; at 200 V about 48 mA. The on-time is short, so real peaks stay lower.
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
  which comes out at about 120 to 200 V DC in pulses.

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
| Power | From the board's USB-fed charger rail only. **No battery**: the board has no low-voltage cutoff, and a plugged-in phone is more authentic | user, 2026-09-27 |
| Construction | **Off-the-shelf modules and through-hole parts on perfboard.** No custom PCB | user |
| GPIOs | **Two**: IO22 and IO19, the pins freed by removing the DRV8825 (STEP and nENABLE) | `audio_kit_v2.2.md` section 6 |
| Measurement | Infer current from voltages and behavior. **No current-probe or current-amplifier project** | user |

## 4. The circuit

### 4.1 Overview

```
 board charger rail (about 4.2 V)
        │
   XL6019 boost module (already owned), set to 10 V
        │
   MAX1771 nixie boost module, 5-12 V in, 150-220 V out (set 150 V to start)
        │
   HV rail ──┬── C1 150 µF / 450 V ──┬── R_bleed (2 × 100 kΩ, ½ W, in series) ── GND
             │                        │
             │                       GND
             │
     ┌───────┴───────────────┐
     │                       │
  U1 ch1 (SW1)            U1 ch2 (SW3)          U1, U2 = ASSR-4128 (dual PhotoMOS, 400 V, 100 mA)
     │                       │
     X ── A-black [coil A] A-yellow ═ B-yellow [coil B] B-black ── Y
     │         D1 (P6KE150CA) from X to Y         │
  U2 ch1 (SW2)            U2 ch2 (SW4)
     │                       │
     └───────┬───────────────┘
             │
          R_feed 100 Ω, 1 W   (low side: fault limit and current view)
             │
            GND  (board GND)
```

| Direction | Switches on | Coil polarity |
|---|---|---|
| **Forward** (bell 2) | SW1 + SW4 | X (A-black) +, Y (B-black) − |
| **Reverse** (bell 1) | SW3 + SW2 | Y (B-black) +, X (A-black) − |
| Off | none | coil current decays through D1 |

### 4.2 Power chain

- **Source:** the board's battery-side charger rail (about 4.2 V), the only place USB power is reachable today. No battery
  is fitted.
- **XL6019** (owned, 3-35 V in, 5-40 V out): set to **10 V**. It is still set to about 38 V from the DRV8825 tests;
  **turn it down before connecting the MAX1771**, which is rated 5-12 V in. The MAX1771 switches an external MOSFET whose
  gate drive equals its input voltage, so 10 V gives it proper gate drive; 4.2 V directly might only half turn it on.
- **MAX1771 nixie module** (Amazon, about $10, "DC 5V-12V to 170V 150V-220V ... MAX1771 with Off Function"): output
  adjustable 150 to 220 V. Its **SHDN** pin is active-high on the chip (high = off). Leave it at the "on" level with no
  GPIO (check which level that is on this board, section 6 step R1). A GPIO on SHDN, so that high voltage exists only while
  ringing, is a later upgrade if a pin frees up.
- **C1, 150 µF / 450 V** (owned) carries the burst. At 170 V it holds about 2.2 J and can give about 1 J while sagging to
  120 V. A 2 s burst at 1.5 to 2 W takes 3 to 4 J, so the module must also deliver during the burst. **A second 150 µF in
  parallel** doubles the reserve if the voltage sags too far; the module then refills during the 4 s off time.
- **R_bleed:** two 100 kΩ, ½ W resistors in series (200 kΩ; 0.14 W at 170 V; each resistor sees half the voltage). Time
  constant 30 s with one capacitor, 60 s with two. **Wait at least 3 minutes after unplugging, and measure before
  touching.**
- **Budget:** about 2 W from the rail while the capacitor recharges, plus the board itself. See section 6 step R8 for the
  rail check.

### 4.3 The H-bridge

- **Switches:** two **Broadcom ASSR-4128-002E**: 2 Form A (two normally-open channels), MOSFET output (AC/DC), 400 V,
  100 mA, 35 Ω maximum on-resistance, DIP-8, each channel with its own LED input and its own two output pins. About $3.60.
  Buy three or four; high-voltage experiments kill parts.
- **U1 holds both high-side switches** (SW1, SW3) and **U2 both low-side switches** (SW2, SW4), so each chip's two
  outputs sit at similar voltages. Take the pinout from the datasheet (AV02-0218EN).
- **Why PhotoMOS:** each switch is isolated from its LED, so the high-side switches need no gate driver, bootstrap or level
  shifter. That removes the part of an H-bridge that forced the big driver chips.
- **Worst-case switch voltage** is about the supply plus the TVS clamp (170 + about 160 V ≈ 330 V at 170 V), under the
  400 V rating. **Keep the HV at or below about 200 V.**
- **Do not substitute** Form B (normally closed) relays, triac-output "AC" or zero-cross SSRs (they never turn off on DC),
  or phototransistor optocouplers (voltage too low).

### 4.4 Turn-off: the TVS

When all switches open, the coils' stored energy (about 15 mJ) must go somewhere. The PhotoMOS outputs have no diode path
for it, so without a clamp the voltage would rise until the switches break down.

- **D1: P6KE150CA** (bidirectional TVS, about 150 V) directly across X and Y. It clamps both polarities and dumps the
  energy quickly, so the current falls in a few milliseconds and the armature is released for the next stroke. That is
  what the old DRV8825 wiring lacked if its diodes held the current up (a slow decay fights the spring's return).
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
- **Resistor 470 Ω (changed from the 330 Ω discussed earlier).** LED current is about (3.3 − 1.3) / 470 ≈ 4.3 mA. The
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

R_feed (100 Ω, 1 W) sits between the bottom of the bridge and GND, so every drive current passes through it:

- **Fault limit:** if a switch fails short with the other side's switch on, the current is limited to about 1.7 A until
  the resistor fails open. A cheap fuse.
- **Current view, with no current probe:** the voltage across it is 0.1 V per mA (20 mA reads 2 V), referenced to board
  GND. Put the scope ground on GND and the probe on the top of R_feed. This is not the current-amplifier problem from past
  projects: the currents here are tens of milliamps at 20 Hz, so one resistor gives volt-level signals. It is optional;
  the plan works without it.
- The current that circulates through D1 after turn-off does not pass through R_feed, so R_feed shows only the driven
  part of each pulse.

### 4.7 Scope safety on this circuit

- **Use 10x probes** on anything connected to the HV side, and check the probe's voltage rating.
- The scope's ground clip is tied to mains earth. **Clip it only to board GND**, never to X, Y or the HV rail.
- To see the coil voltage (X − Y): CH1 on X, CH2 on Y, both grounds on GND, then CH2 invert and ADD, if the BK 1535A has
  those modes (look for "INV" and "ADD" on the vertical controls).

## 5. Bill of materials

| Part | Qty | Status |
|---|---|---|
| Broadcom ASSR-4128-002E, dual PhotoMOS, DIP-8 | 2 (+1 or 2 spare) | chosen, $3.60 each |
| MAX1771 nixie boost module, 5-12 V in, 150-220 V out | 1 | chosen, about $10 |
| XL6019 boost module | 1 | owned (from the DRV8825 build) |
| Capacitor 150 µF / 450 V | 1 (2 if needed) | owned |
| P6KE150CA bidirectional TVS | 1 (+1 spare) | to buy |
| Resistor 100 Ω, 1 W (R_feed) | 1 | to buy |
| Resistor 100 kΩ, ½ W (R_bleed) | 2 | to buy |
| Resistor 470 Ω, ¼ W (LEDs) | 4 | to buy |
| Perfboard, DIP-8 sockets, high-voltage wire | | wire owned |
| Resistor about 4.7 kΩ, 1 W (dummy load for step R3) | 1 | to buy or use what is on hand |
| Resistor about 22 kΩ, 2 W or more (module load test, step R1) | 1 | to buy or use what is on hand |

**Removed from the design:** the DRV8825 module and its nENABLE pull-up; the second XL6019 stage.

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
4. **Load test:** 22 kΩ, 2 W or more, across the output (about 7 mA, 1 W at 150 V). The output should hold within a few
   volts. Read the bench supply's current at 4.2 V: this is the input current the board's rail will have to supply for
   about 1 W out. After a minute, check the MOSFET and inductor on both modules are warm at most, not hot.
5. **Capacitor and bleeder:** remove the load, fit C1 and R_bleed. Power up; time how long the output takes to reach
   150 V (the charge time). Unplug and time the fall to under 30 V (expect about 50 s for one capacitor).

**Pass:** 150 V holds under a 1 W load, modules warm at most, capacitor charges and bleeds down as expected.

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

1. Wire the bridge completely (section 4.1) with **a 4.7 kΩ resistor in place of the coils**, D1 fitted, R_feed fitted.
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

1. Power off, wait for the bleeder, and connect the HV chain (R1) in place of the bench supply. **From here on the
   circuit is at 150 V or more: insulate everything, one hand only, never touch it while powered or for 3 minutes
   after.**
2. At 150 V, repeat R4 steps 2 to 5, starting at 20 Hz with the best 55 V settings.
3. Find the settings that give a loud, clean ring at 20 Hz. Then sweep 15 to 30 Hz with them, and record the useful range.
4. Only if 150 V is not loud enough: raise the voltage in steps of about 20 V up to **200 V at most**, repeating step 3.
5. After a 2-minute run of the US cadence, feel (with the power off and the capacitor drained) the TVS, the PhotoMOS chips,
   R_feed and both modules. Warm is fine; too hot to hold is not.
6. Watch the HV rail on the multimeter during a 2 s burst: note how far it sags and how long it takes to recover. If it
   sags below about 120 V, fit the second capacitor.

**Pass:** a loud, clean two-bell ring at 20 Hz (or at the best frequency found), at a recorded voltage and pulse setting,
with nothing hot.

### R6. Cadence, ring trip and faults

1. `ring cadence us 10` and `ring cadence uk 10`. Listen for a steady ring through the whole burst, and the clean
   stop at the end.
2. **Ring trip:** the firmware must go to idle when the hook opens, even mid-pulse. Test with the hook contact connected
   (the dial work in stage 10 has the hook on IO13).
3. **Firmware fault:** press RESET mid-ring. The bell stops at once and stays silent through boot.
4. **Supply loss:** unplug USB mid-ring. The bell stops; the capacitor bleeds down.

**Pass:** cadences correct, lifting the handset stops the bell at once, silent through reset.

### R7. Interference

These are the waiting items from stage 10 step 5 and stage 7 test 7.

1. `dial trials 20` while `ring cadence us` runs: no wrong digits, no false hook changes.
2. `mic avg 30` and `mic snr ringing` while ringing: compare with the silent figures. Listen to the earpiece too.
3. A Bluetooth call connected while ringing: no drops (a real incoming call rings while the link is up).

**Pass:** no false digits or hook changes; no audible noise added to the earpiece or microphone.

### R8. The board's power rail

1. With the whole ringer powered from the board's charger rail (not the bench supply), measure the rail with the
   multimeter during a 2 s burst and during the capacitor recharge.
2. Check the board does not reset or drop USB. (On 2026-09-25 connecting the old boost converter to the battery pins once
   dropped the USB device; connect the ringer power after boot until this is understood.)
3. **If the rail sags badly or the board resets,** the charger chip's current limit is the bottleneck. Options, in order:
   take the XL6019's input from the board's 5 V rail (VCC5V, fed from USB through D1) instead of the charger rail; add the
   second capacitor to lower the peak demand; or lower the ring voltage.

**Pass:** the board runs normally through ringing and recharge.

### R9. Final assembly

1. Build on perfboard with at least 3 mm between high-voltage and low-voltage copper, HV wire for the HV runs, and
   insulation over every HV joint.
2. Label the board "170 V DC" near the capacitor.
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
"centre-tap" effect of unipolar steppers). At 170 V that exceeds 400 V. It would need a lower supply, a TVS across each
coil, or higher-voltage switches. The same ASSR-4128 parts can be rewired for it.

### 7.4 Closed-loop timing

The flick test showed each coil generates 2 to 3 V when the armature moves. A future version could read that back-EMF on
an ADC pin to time each pulse to the armature's real motion. Set aside until open-loop drive is exhausted, and there are
no spare GPIOs today.

## 8. Open items

- **Ring frequencies for regions other than the US** are not verified (`tones.md` has only the US 20 Hz). Research them
  before building the region profiles.
- The charger rail's current limit (step R8).
- The LCR meter's equivalent-circuit mode for the 100 Hz readings, if the numbers are needed again.
- Whether a GPIO can be found for the MAX1771 SHDN pin (high voltage only while ringing).
- Updating `initial_design.md` (Ring driver, Ring-driver power, BOM) and `validate_board_plan.md` stage 11 to point here.

## Sources

- Broadcom ASSR-4118/4119/4128 datasheet (AV02-0218EN): <https://docs.broadcom.com/docs/AV02-0218EN>
- Broadcom ASSR-4110/4111/4120 datasheet (AV02-0279EN), single-channel alternative: <https://docs.broadcom.com/docs/AV02-0279EN>
- MAX1771 5-12 V to 150-220 V module (Amazon): <https://www.amazon.com/5V-12V-150V-220V-Voltage-MAX1771-Function/dp/B09P47K5C6>
- Omnixie NCH8200HV (2.5-15 V in, 170 V out; considered, $72): <https://www.tindie.com/products/omnixie/nch8200hv-nixie-high-voltage-power-module/>
- `doc/tones.md` (ring frequency and cadences), `doc/validation_log.md` (DRV8825 results, 2026-09-25 and 26),
  `doc/audio_kit_v2.2.md` (GPIOs, pin current limit).
