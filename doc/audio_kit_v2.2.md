# ESP32 Audio Kit V2.2 (AI-Thinker ESP32-A1S) — Board Reference

Reference for the development board this project targets. Companion to `doc/initial_design.md`.
Researched online in September 2026, then revised after reading AI-Thinker's own documents
(`doc/audio-kit-22.pdf` and `doc/ESP32-A1S-datasheet.pdf`).

Your board is marked **"esp32 audio kit v2.2 a618"** on the PCB, with **"k547"** on the back of the
antenna. **Neither marking appears in any source I could find**, including both official documents.
Section 9 lists what to confirm on your specific board.

**Source tags** used throughout:

- **[K]** Official kit spec: *ESP32-Audio-Kit_V2.2 Specification V1.1* (June 2021, Chinese),
  `doc/audio-kit-22.pdf`, including the board schematic (dated **2019-04-09**).
- **[M]** Official module datasheet: *ESP32-A1S Specification V2.3* (June 2021, English),
  `doc/ESP32-A1S-datasheet.pdf`, including the module's internal schematic (dated June 2021).
- **[P]** Primary: vendor or reference code that defines the hardware (AI-Thinker's GitHub repo, the
  `arduino-audiokit` board headers), read directly.
- **[S]** Secondary: a web page, blog or product listing I read directly.
- **[R]** Reported: appeared only in a search-engine summary, or the page could not be read. Unverified.
- **[I]** Inferred by us.

## 1. Bottom line

- **"V2.2" boards ship with different modules and pin maps under the same version string.** **[S]** The
  two official documents settle most of the wiring, but not which module version is on *your* board.
- **The newest ES8388 module (datasheet V2.3, 2021) puts the codec on GPIO 18, 23 and 5** (I2C SDA, I2C
  SCL, I2S BCLK), which are also three of the header pins and three of the button GPIOs. **[M]** Older
  ES8388 modules used 33/32/27 instead. **[P]** If your board has the newer module, those three header pins
  are not free. An I2C scan settles it (section 9).
- **Microphone and line-in inputs share one codec input, and the reason is now confirmed** from the
  official module pin table and board schematic (section 5). The line-in jack is **LIN2/RIN2**, so the
  firmware must select `LINE2` (it selects `LINE1` today).
- **The headphone jack is the codec's LOUT2/ROUT2**, so the firmware's output selection must change too
  (section 5). It defaults to LOUT1/ROUT1 today, which is the speaker-amplifier path.
- **There is no boost converter on the board** (this corrects the first design draft): on battery the
  "5 V" rail is just the battery voltage, and 3.3 V comes from a step-down regulator (section 7).
- **A pin plan that works on either module version** is in section 6: inputs on the JTAG header
  (IO13, IO14, IO15) and outputs on IO22 and IO19.

## 2. What is on the board

| Part | Detail | Source |
|---|---|---|
| **ESP32-A1S module** | ESP32 + ES8388 codec + 4 MB flash + 64 Mbit (8 MB) PSRAM in one shielded 31.5 × 19 mm module. The codec is inside it, so the audio traces go straight in. GPIO16/17 serve the PSRAM. | **[M]/[K]**; GPIO16/17 **[I]** |
| **Codec** | ES8388: two ADCs, two DACs, mic amplifier, headphone amplifier. Older boards used an AC101. | **[M]/[S]** |
| **CP2102** (U3) | USB-to-UART bridge (UART0 / GPIO1, GPIO3), with a DTR/RTS auto-reset circuit to IO0 and RST, plus BOOT (K1) and RESET (K2) buttons. **Not an amplifier.** | **[K]** |
| **Speaker amps** | Two class-D amplifier ICs (pins VoP, Bypass, GND, InP, InN, VCC, VoN, CTRL). Reported as NS4150; the name is not printed on the schematic. | **[K]** pins; **[S]** name |
| **Charger** | Single-cell linear charger IC (U1: VCC, CE, BAT, TEMP, PROG, CHRG, STDBY), with two status LEDs. | **[K]** |
| **3.3 V regulator** | **Step-down (buck)** converter U2, with the schematic note "Vo=(510k/110k+1)*0.6V=3.38V". | **[K]** |
| USB | Two micro-USB connectors: "UART/POWER" (CP2102 and 5 V) and "POWER" (5 V only). Recommended: power both together for over 1 A peak. | **[K]** |
| Audio | 3.5 mm headphone jack (J2), 3.5 mm line-in jack (J1), two speaker connectors (J3, J4), electret mics (MIC3/MIC4 symbols) plus footprints for MIC1/MIC2. | **[K]** |
| Buttons, LEDs | Six keys; power LED (LED3); LED4 on IO22; LED5 on IO19. | **[K]** |
| DIP switch | Five switches (S1), section 8. | **[K]** |
| microSD | Slot with card-detect on IO34. | **[K]** |
| Headers | P2 GPIO header, P3 UART/reset/boot, P4 3.3 V/GND, P1 JTAG (section 6). | **[K]** |
| Antenna | PCB antenna (yours) or external IPEX socket, depending on version. | **[K]** |
| Size | 82 × 73 mm (the parameter table's "82×37" looks like a typo). | **[K]** |

**Electrical (official):**

| Item | Value | Source |
|---|---|---|
| Kit supply | 4.7–5.3 V, over 1 A | **[K]** |
| **Module** supply | 3.0–3.6 V (3.3 V recommended), peak current over 500 mA | **[M]** |
| I/O logic | 3.3 V; VIL max 0.25·VDD, VIH min 0.75·VDD | **[M]** |
| Max per I/O pin | 12 mA | **[K]** |
| Serial | 110–4,608,000 baud, default 115200 | **[K]/[M]** |
| Temperature | −40 to 85 °C operating | **[K]/[M]** |
| Series resistor on GPIOs | 10–100 Ω recommended (overshoot, EMI, ESD) | **[K]/[M]** |

## 3. The module (ES8388 version, datasheet V2.3)

**Module pin table** **[M]** (the module has 38 pins):

| Pin | Name | Function |
|---|---|---|
| 1 | GND | |
| 2 | 3V3 | supply |
| 3 | SENSOR_VN | GPIO39, ADC1_CH3 |
| 4 | SENSOR_VP | GPIO36, ADC1_CH0 |
| 5 | IO34 | GPIO34, ADC1_CH6 |
| 6 | IO0 | GPIO0, ADC2_CH1, CLK_OUT1 |
| 7 | IO14 | GPIO14, MTMS, SD_CLK |
| 8 | IO12 | GPIO12, MTDI, SD_DATA2 |
| 9 | IO13 | GPIO13, MTCK, SD_DATA3 |
| 10 | IO15 | GPIO15, MTDO, SD_CMD |
| 11 | IO2 | GPIO2, SD_DATA0 |
| 12 | IO4 | GPIO4, SD_DATA1 |
| 13 | HBIAS | pulled up to AVCC by an internal 1 kΩ |
| 14 | MIC2N | codec **RIN2** |
| 15 | MIC1N | codec **RIN1** |
| 16 | MBIAS | pulled up to AVCC by an internal 1 kΩ |
| 17 | MIC1P | codec **LIN1** |
| 18 | MIC2P | codec **LIN2** |
| 19, 20 | GND | |
| 21 | LINEINR | codec **RIN2** |
| 22 | LINEINL | codec **LIN2** |
| 23 | NC | not connected |
| 24 | SPORN | codec **ROUT1** |
| 25 | NC | not connected |
| 26 | SPOLN | codec **LOUT1** |
| 27 | HPOUTL | codec **LOUT2** |
| 28 | HPOUTR | codec **ROUT2** |
| 29–34 | IO5, IO18, IO23, IO19, IO22, IO21 | GPIOs |
| 35 | EN | chip enable (high = on); 10 kΩ pull-up on the module |
| 36, 37 | TXD0, RXD0 | GPIO1, GPIO3 (UART0) |
| 38 | GND | |

**Internal codec wiring** (from the module's internal schematic) **[M]**:

| Codec signal | GPIO |
|---|---|
| I2C SCL | **23** |
| I2C SDA | **18** |
| I2S MCLK | **0** |
| I2S SCLK (BCLK) | **5** |
| I2S LRCK (WS) | **25** |
| I2S DSDIN (to codec) | **26** |
| I2S ADSDOUT (from codec) | **35** |

- The I2C lines have **10 kΩ pull-ups to AVCC and 100 Ω series resistors** inside the module. **[M]**
- The codec's **CE pin is tied low** (through 10 kΩ), which gives I2C address **0x10** (7-bit), matching
  this repo's driver (`ES8388_ADDR 0x20`, the 8-bit form). **[M]/[I]**
- **Startup:** GPIO0 has a 10 kΩ pull-up (SPI boot = high, download = low) and GPIO2 defaults to
  pull-down; "some pins have been internally pulled up." **[M]**
- **The GPIO pins 5, 18, 23, 25, 26, 35 and 0 are used by the codec inside the module**, yet pins
  5, 18, 23 (and 0) are also exposed. Driving them from outside would interfere with the codec. **[M]/[I]**
- **Output coupling capacitors** inside the module: **1 µF** on LOUT1/ROUT1 and **22 µF** on LOUT2/ROUT2.
  **[M]**
- **AVCC** is tied to 3.3 V through a 0 Ω resistor. **[M]**

**The module symbols disagree with the board schematic.** The board schematic (2019) labels module pins
14–18 as MIC2N, MIC2P, MBIAS, MIC1P, MIC1N and pins 23–28 as SPORP, SPORN, SPOLP, SPOLN, LINEINL/R, which is
the older AC101 naming. The ES8388 module (above) names pins 15 and 18 differently and has NC on 23 and 25.
So the ES8388 module drops into an AC101-era PCB, and the mismatch is why the mic and line-in inputs end up
mixed (section 5). **[K]/[M]/[I]**

## 4. Codec pin maps by variant

| Function | **Newest ES8388 module** (datasheet V2.3) | Older ES8388 ("variant 5") | AC101 |
|---|---|---|---|
| I2C SDA / SCL | **18 / 23** | 33 / 32 | 33 / 32 |
| I2S MCLK | 0 | 0 | 0 |
| I2S BCLK | **5** | 27 | 27 |
| I2S WS (LRCK) | 25 | 25 | **26** |
| I2S data out (to codec) | 26 | 26 | **25** |
| I2S data in (from codec) | 35 | 35 | 35 |
| Codec I2C address | 0x10 | 0x10 | 0x1A |
| Source | **[M]** (matches the library's "variant 7") | **[P]** library "variant 5" | **[P]** |

Other board wiring (all versions): headphone-jack detect on **39**, speaker-amp enable on **21**, aux-in
detect on **12**, SD card CS/MISO/MOSI/CLK on 13/2/15/14, card-detect on 34. **[P]/[K]**

- The library ties versions to strings: variant 5 = "ES8388 2957 / 3478 / A149", variant 6 = "AC101
  2473 / 2762 / 2957", variant 7 = "ES8388 2957". **[P]** The same "2957" appears three times, so the
  version string is **not** a reliable identifier.
- **A guess about your board:** version numbers of the form "A###" appear to increase with time (A149,
  A237, A247, A404 are known), so A618 may be a recent board with the newest module. **[I]** Do not rely on
  this; scan the I2C bus.
- **MCLK on GPIO0** is needed by the codec; GPIO0 is also the BOOT strapping pin, so it "must be hanging
  when using the internal codec." **[S]**

## 5. Audio input and output paths

### Inputs

| Board / module pin | Codec input | Goes to |
|---|---|---|
| LINEINL (22) | **LIN2** | line-in jack J1 (through C11) **[K]/[M]** |
| LINEINR (21) | **RIN2** | line-in jack J1 (through C13) **[K]/[M]** |
| MIC1P (17) | **LIN1** | left mic, through **C17** (not fitted) **[K]/[M]** |
| MIC1N (15) | **RIN1** | right mic, through **C19** (not fitted) **[K]/[M]** |
| MIC2P (18) | **LIN2** | left mic, through **C18** **[K]/[M]** |
| MIC2N (14) | **RIN2** | right mic, through **C20** **[K]/[M]** |

- **This explains the bug.** The proper microphone inputs (LIN1/RIN1, via C17 and C19) have their
  capacitors removed, while C18 and C20 still connect the mics to **LIN2/RIN2, the same inputs as the
  line-in jack**. So the mics and the jack are mixed on one input, and LIN1/RIN1 is unused. This was
  previously a reported observation **[S]**; the official pin table and schematic now confirm it. The
  published fix moves C18 to C17's position and C20 to C19's position. **[S]**
- **Consequences for this project:**
  1. Our microphone goes into the **line-in jack** (LIN2/RIN2). The firmware selects
     `AUDIO_HAL_ADC_INPUT_LINE1` today; it must become **`AUDIO_HAL_ADC_INPUT_LINE2`**. This repo's
     `es8388.c` supports that choice (`ADC_INPUT_LINPUT2_RINPUT2`). **[P]**
  2. The **onboard mics share that input**, so they would mix into the handset mic. Remove **C18 and C20**
     (or the mics themselves) to isolate the line-in path. **[I]**
  3. **Mic bias:** MBIAS and HBIAS are 3.3 V through 1 kΩ, for the onboard electret mics. Our MAX9814 needs
     its own supply (the 3.3 V header pin works). **[M]/[I]**
  4. Line-in volume quirk: the library notes the aux volume is fixed at line level unless a "volume hack"
     is enabled. **[P]**

### Outputs

| Module pin | Codec output | Goes to |
|---|---|---|
| SPOLN (26) | **LOUT1** (1 µF coupling) | left speaker amplifier (input N) **[M]/[K]** |
| SPORN (24) | **ROUT1** (1 µF coupling) | right speaker amplifier (input N) **[M]/[K]** |
| HPOUTL (27) | **LOUT2** (22 µF coupling) | headphone jack J2 (through R21) **[M]/[K]** |
| HPOUTR (28) | **ROUT2** (22 µF coupling) | headphone jack J2 (through R20) **[M]/[K]** |

- **The headphone jack is LOUT2/ROUT2.** This repo's `es8388.c` maps `AUDIO_HAL_DAC_OUTPUT_LINE2` to
  `LOUT2|ROUT2`, so the earpiece needs **`AUDIO_HAL_DAC_OUTPUT_LINE2`**; the default is `LINE1`
  (LOUT1/ROUT1, the speaker-amplifier path). **[P]/[I]**
- The module has only the N side of each speaker output (the P pins are NC), so the amplifiers are driven
  single-ended. **[M]**
- **The speaker amplifiers are off by default.** GPIO21 drives their CTRL pins through R46, and R51 pulls
  that line to ground. **[K]** Leave GPIO21 alone.
- **Headphone-jack detect** is on GPIO39, pulled up by R36. **[K]** We can ignore it.

## 6. GPIOs you can actually use

**Headers** **[K]:**

| Header | Pins |
|---|---|
| **P2** (GPIO, bottom row) | IO21, IO22, IO19, IO23, IO18, IO5 |
| **P3** (UART/reset, top row) | GND, IO0, RST, TX0, RX0, 3V3 |
| **P4** | 3V3, GND |
| **P1** (JTAG, 4 pins) | pin 1 JT_MTDO (IO15 via DIP SW5), pin 2 JT_MTCK (IO13 via DIP SW4), pin 3 IO12, pin 4 IO14 |

The SD pins (IO2, 4, 12, 13, 14, 15) are otherwise only on the SD slot.

**Wiring of each candidate** **[K]/[M]:**

| GPIO | Board wiring | Usable? |
|---|---|---|
| **IO22** | LED4 only (through R14 to 3.3 V; the LED lights when the pin is low) | **Yes**, output (the LED follows it) |
| **IO19** | LED5 (through R76) **and** the key ladder (through R67, KEY3) | **Yes** if R67 is removed |
| **IO13** | via DIP: KEY2, SD DATA3, JTAG MTCK | **Yes** via P1 pin 2, SW4 ON, SW1/SW2 OFF |
| **IO15** | via DIP: SD CMD, JTAG MTDO | **Yes** via P1 pin 1, SW5 ON, SW3 OFF |
| **IO14** | SD CLK (through R26), JTAG MTMS | **Yes** via P1 pin 4 |
| **IO12** | SD DATA2 (pulled up), JTAG MTDI; strapping pin | **Avoid** |
| **IO21** | speaker-amp CTRL | **No** |
| **IO23, IO18, IO5** | keys KEY4/5/6 **and, on the newest module, the codec's I2C SCL, SDA and BCLK** | **No** (on the newest module); on the older module only after removing R68–R70 |
| IO36, 39, 34 | key ladder, headphone detect, SD detect | input-only; not on a header |
| IO25, 26, 27, 32, 33, 35 | codec I2S/I2C (internal) | No |
| IO0, IO1, IO3 | boot/MCLK, UART | No |

**The key ladder couples the key GPIOs.** The schematic shows one resistor ladder (R52 from 3.3 V, R54,
R55–R59) with the six keys on its taps, read on IO36, and the five other keys each also wired to a GPIO
through a series resistor (R66 to IO13, R67 to IO19, R68 to IO23, R69 to IO18, R70 to IO5). **[K]**
Resistor values are not printed. The spec says the key GPIOs have "no external pull-up resistor," but with no
key pressed the ladder is fed from 3.3 V through R52 and R54, so each of those GPIOs sees a weak pull-up
through several series resistors, and grounding one drags its neighbours. **[I]** On the newest module this
would couple into the I2C and I2S lines.

**Caution: the microSD card (a project requirement, see `initial_design.md`) conflicts with the pin
assignment below.** The SD slot needs IO14 (CLK), IO15 (CMD) and IO2 (DATA0) in 1-bit mode, plus IO4, IO12
and IO13 in 4-bit mode. With the SD card, only IO13 (via P1 pin 2), IO22, IO19 (after removing R67) and IO21
(after removing R46, which isolates it from the amp enable) remain clean; the plan in
`validate_board_plan.md`, stage 4, decides how to get the fifth signal. The table below is the
pre-SD proposal and is kept for reference.

**Proposed pin assignment before the SD card requirement (works on either module version):** **[I]**

| Signal | GPIO | Where |
|---|---|---|
| Hook switch (input) | **IO13** | P1 pin 2, DIP **SW4 ON** |
| Dial pulse (input) | **IO15** | P1 pin 1, DIP **SW5 ON** |
| Dial-in-progress (input) | **IO14** | P1 pin 4 |
| DRV8825 STEP (output) | **IO22** | P2 |
| DRV8825 nENABLE (output) | **IO19** | P2, **remove R67** |

DIP switches: **SW4 and SW5 ON, SW1, SW2 and SW3 OFF.** IO21 stays unused.

Cautions:

- **R15** (a resistor to ground on the JTAG lines, value not shown) may pull IO13 or IO15 low; measure it,
  and use external pull-ups of about 4.7–10 kΩ to a 3.3 V header pin on all three inputs.
- **IO14 goes to the SD slot's CLK pin**, and IO13/IO15 pull-ups on the SD side are disconnected by the DIP
  switches; leave the SD slot empty. Measure IO14's idle level before relying on it.
- **IO19's LED5** lights when the pin is low; harmless.
- Add the **10–100 Ω series resistor** the spec recommends on each line, and keep each pin under **12 mA**.
- **Output pins must be safe at boot:** the DRV8825 nENABLE also gets its own pull-up (already in the
  design), so the bell stays off while pins float.
- Avoid the strapping pins (0, 2, 5, 12, 15) as outputs; IO15 is used only as an input here.

## 7. Power

- **Supply:** 5 V on either micro-USB (over 1 A recommended, or both ports together). **[K]**
- **3.3 V comes from a buck converter (U2)** whose input rail is "VCC5V." **[K]**
- **On battery there is no boost.** A P-channel MOSFET (Q1) sits between the battery (VBAT) and VCC5V, with
  its gate divider (R8/R9) fed from VBUS. With USB present the gate is high, so Q1 is off and VCC5V comes
  from VBUS through diode D1; with no USB, Q1 turns on and **VCC5V is simply the battery voltage (about
  3.0–4.2 V)**. **[K]/[I]** The speaker amplifiers run from this rail too. The spec's feature list says the
  board supports "3.7 V lithium battery input; 5 V 2 A input; simultaneous battery charging." **[K]**
- **Consequence:** a 3.38 V buck output from a nearly empty LiPo has little headroom, so expect a brown-out
  around 3.5 V or so. **[I]** This contradicts the first design draft ("onboard boost converter to the ~5 V
  rail"), which is corrected in `initial_design.md`.
- **Battery charge state is not visible to firmware**; the charger status pins go only to LEDs. **[K]**
- **3.3 V on the headers** (P3, P4): current capability not stated; use for small loads (pull-ups, the
  MAX9814). **[K]**
- **Programming:** UART0 through the CP2102, with DTR/RTS auto-reset; an external FTDI can use P3. **[K]**
- **Do not leave a microSD card in the slot** on at least one AC101 board (it would not boot). **[S]**

## 8. DIP switch block (official)

Five switches on S1, for choosing what IO13 and IO15 do. **[K]**

| Switch | Function when ON |
|---|---|
| **SW1** | IO13 connected to **KEY2** |
| **SW2** | IO13 connected to the SD slot's **DATA3** |
| **SW3** | IO15 connected to the SD slot's **CMD** |
| **SW4** | IO13 connected to JTAG **MTCK** (P1 pin 2) |
| **SW5** | IO15 connected to JTAG **MTDO** (P1 pin 1) |

The schematic also shows two greyed-out jumper symbols (JP1/JP2) for the same signals, apparently not
fitted. **[K]** For our pin plan, set **SW4 and SW5 ON and the other three OFF**.

## 9. What to confirm on your board (V2.2 A618 / k547)

1. **I2C scan** (do this first). Scan **SDA 18 / SCL 23**, then **SDA 33 / SCL 32**. Expect **0x10**
   (ES8388). The pair that answers tells you the module version (newest = 18/23). No answer on either
   means AC101 (try 0x1A on 33/32) or a wiring problem.
2. **Photograph the top of the shield can** and note any printed code. **k547** may be a date or lot code;
   nothing documents it.
3. **Confirm the mic couplers:** C17 and C19 absent, C18 and C20 present; and whether the onboard mics are
   fitted.
4. **Measure R15** and the idle voltage on IO13/14/15 with SW4/SW5 ON, and the resistance from IO19 to the
   key ladder before and after removing R67.
5. **Battery connector:** identify the type and measure polarity before connecting a LiPo.
6. **Confirm the speaker amp part** by reading its marking.
7. **Confirm the headphone jack is LOUT2/ROUT2** with a test tone.

## Sources

- **[K]** *ESP32-Audio-Kit_V2.2 Specification V1.1* (Shenzhen Ai-Thinker, June 2021), in this repo as `doc/audio-kit-22.pdf` (text, pin tables, DIP-switch table and the sheet 2/3 and 3/3 schematic).
- **[M]** *ESP32-A1S Specification V2.3* (Shenzhen Ai-Thinker, June 2021), in this repo as `doc/ESP32-A1S-datasheet.pdf` (pin table, startup table, and the module's audio and main-chip schematics).
- **[P]** AI-Thinker ESP32-A1S-AudioKit repository: <https://github.com/Ai-Thinker-Open/ESP32-A1S-AudioKit> (README, `ac101.h`, `board_def.h`, module photos)
- **[P]** `arduino-audiokit` board definitions and settings: <https://github.com/pschatzmann/arduino-audiokit> (`src/audio_board/ai_thinker_es8388_5.h`, `ai_thinker_es8388_7.h`, `ai_thinker_ac101.h`, `src/AudioKitSettings.h`) and its GPIO overview wiki: <https://github.com/pschatzmann/arduino-audiokit/wiki/GPIO-Overview-by-Selected-Board>
- **[S]** Phil Schatzmann, "The AI Thinker Audio Kit experience": <https://www.pschatzmann.ch/home/2021/12/06/the-ai-thinker-audio-kit-experience-or-nothing-is-right/>
- **[S]** Phil Schatzmann, "The AI Thinker AudioKit Audio Input Bug": <https://www.pschatzmann.ch/home/2021/12/15/the-ai-thinker-audiokit-audio-input-bug/>
- **[S]** Phil Schatzmann, "Using the AI Thinker Audio Kit Buttons": <https://www.pschatzmann.ch/home/2021/12/08/using-the-ai-thinker-audio-kit-buttons/>
- **[S]** ESP32-A1S notes (chipmuenk): <https://github.com/chipmuenk/modular_synths/blob/main/esp32_a1s.md>
- **[S]** ESP-ADF board component for the A1S (trombik): <https://github.com/trombik/esp-adf-component-ai-thinker-esp32-a1s>
- **[S]** ESP32-A1S Audio Kit notes (diyelectromusic): <https://diyelectromusic.com/2025/04/07/esp32-a1s-audio-kit/>
- **[S]** NuttX ESP32-AUDIO-KIT page: <https://nuttx.apache.org/docs/latest/platforms/xtensa/esp32/boards/esp32-audio-kit/index.html>
- **[S]** ESP32-Audio-Kit notes (Coytar): <https://coytar.wordpress.com/2024/06/15/esp32-audio-kit/>
- **[R]** Home Assistant community thread on board versions: <https://community.home-assistant.io/t/esp32-a1s-audio-kit-for-voice-assistant/568301/41>
