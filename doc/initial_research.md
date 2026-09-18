# Rotary Phone → VoIP Conversion: Initial Research

## Goal (as stated)
Convert an old rotary dial telephone into a working phone using a Raspberry Pi Pico W and
a small battery mounted inside the phone body. Requirements:
- Pick up handset → hear a dial tone
- Dial a number using the actual rotary dial
- Carry on a full duplex conversation through the phone's original earpiece and mouthpiece
- Battery powered, self-contained inside the case

## The five subsystems

Every rotary-to-VoIP conversion (Pico, ESP32, or Pi) breaks down into the same five pieces.
This shapes everything below.

1. **Hook switch** – detect handset up/down. Trivial GPIO read (the phone already has a
   switch for this).
2. **Rotary dial decoding** – the dial is just a pulse-per-digit mechanical switch (and
   usually a second switch that shorts the line while dialing, to mute clicks). Trivial GPIO
   interrupt + pulse counting with debounce.
3. **Dial tone / call-progress tones** – synthesize a 350+440 Hz dial tone (and busy/ringback
   tones) and play them into the earpiece before/while dialing. Easy on any of these
   platforms — it's just a sine generator into a DAC.
4. **Line interface (audio in/out to the actual handset)** – old phone handsets use a
   carbon or electret transmitter and a low-impedance receiver, usually on a 2-wire circuit
   shared with the ringer/dial. This needs a small analog front end (mic bias/amp + speaker
   amp, or a proper SLIC) between the handset wiring and whatever does the digital audio.
   This is the fiddly analog-electronics part of the project, independent of which
   microcontroller you choose.
5. **The actual call** – this is where the Pico W choice gets genuinely hard, and is the
   crux of this research. Options below.

## Option A: Pico W as a full SIP/VoIP endpoint (what was originally proposed)

The Pico W (RP2040, no hardware floating point, ~264KB RAM, Infineon CYW43439 radio) would
need to run, simultaneously:
- A TCP/IP + WiFi stack (lwIP over cyw43 — available and reasonably mature via the Pico SDK)
- A SIP signaling stack (REGISTER/INVITE/BYE, auth) — **no mature, maintained SIP library
  exists for RP2040/Pico SDK today**. You'd be porting/writing one (e.g. porting a minimal
  subset of PJSIP or reSIProcate, both of which target real OSes and are heavy) or
  hand-rolling INVITE/ACK/BYE against a specific SIP server, which is fragile against
  anything but a server you control.
- RTP audio streaming with a codec (G.711 is cheapest computationally and is the natural
  choice — G.729/Opus need meaningfully more CPU than an RP2040 comfortably has alongside
  networking)
- I2S audio in + out (this part is well trodden — there are working bidirectional I2S
  examples for RP2040, e.g. `malacalypse/rp2040_i2s_example`), needing an external I2S codec
  or separate ADC/DAC since the Pico has no built-in audio hardware
- All of this on a single core (or split across RP2040's two cores) with no OS, hard
  real-time audio timing, while also polling the rotary dial and hook switch

**Verdict:** technically possible in principle, but there is no existing project doing this,
and you'd be building a SIP/RTP stack from scratch on bare-metal RP2040 as the hard,
unrewarding part of the project. This is a multi-month embedded-networking project in its
own right, separate from the fun physical/analog part.

Also relevant: **Pico W's Bluetooth does not support audio (SCO/HFP)** — Bluetooth Classic
ACL/LE are supported but hands-free audio profiles are not, and there's no indication that's
changing. So "pair with a phone over Bluetooth and use it as a hands-free speaker" — a
well-proven approach on ESP32 — is **not available on Pico W**.

## Option B: ESP32 instead of Pico W (proven, closest match to the goal)

This is a straight swap of the microcontroller, keeping everything else about the vision
(battery-powered, self-contained, mounted in the phone). ESP32 has:
- Bluetooth Classic **with SCO/HFP audio support**, and WiFi
- More CPU headroom and a genuine RTOS (FreeRTOS) underneath, so networking + audio + I/O
  as separate tasks is normal, not exotic
- Existing, working open-source projects that do almost exactly this:
  - **weeBell** ([hardware](https://github.com/danjulio/weeBell_hardware),
    [Bluetooth firmware](https://github.com/danjulio/weeBell_bluetooth)) — ESP32 (gCore) +
    Silvertel AG1171 ringing SLIC + ES8388 audio codec. Handles off-hook detection, rotary
    *and* DTMF dial decoding, generates real ringing voltage to ring the phone's mechanical
    bell, and has firmware that bridges the POTS phone to a **cellphone via Bluetooth
    Hands-Free Profile** — i.e. pick up the rotary phone, it dials out through your paired
    cellphone. There's a separate variant for SIP as well.
  - **esp32-sip-phone** and **ESP32-SIP-Voice** — ESP32 SIP/VoIP phone firmware (not
    rotary-specific but shows SIP-on-ESP32 is a solved problem, unlike on RP2040)
  - **SLIC** (SigmazGFX) — Arduino + QCX601 SLIC + SIM800/SIM900 GSM module, for routing
    calls over a cellular SIM instead of VoIP or Bluetooth
- The hard analog part (SLIC) is the same regardless of microcontroller — weeBell's schematics
  are directly reusable/referenceable even if you don't use their exact board.

**Verdict:** if the Pico W is a hard requirement, treat that as a real constraint and expect
to build the SIP/RTP stack yourself (Option A). If the requirement is really "small,
self-contained, battery-powered microcontroller inside the phone," ESP32 gets you to a
*working, previously-demonstrated* result far faster, with Bluetooth-to-cellphone (simplest,
uses your existing phone number/carrier, no PBX to run) or SIP/VoIP (needs a SIP
account/server, e.g. a VoIP provider or self-hosted Asterisk) both being proven paths.

## Option C: Raspberry Pi Zero 2 W (easiest software, least "small embedded" purity)

Full Linux SBC, still small, runs a real SIP client (`baresip`, `linphone`) as ordinary
userspace software — no protocol stack to write. Rotary dial/hook switch read via GPIO with
a normal Python/C GPIO library; audio via a USB sound card or I2S HAT. Multiple complete,
working reference projects:
- **retrophone** (danjulio... er, `chkronenberg/retrophone`) — Pi Zero 2 W, Debian, baresip, Python
- **rasipfon** (weigu.lu) — detailed build log, schematics included
- **PiTelephone** (Logic Ethos) — Pi + rotary dial + SIP
- **mnutt/rotary** — rotary phone talking to Asterisk
- Original 2015 Hackaday piece "Convert A Rotary Phone To VOIP Using Raspberry Pi" — the
  pattern is a decade old and well proven

**Verdict:** most reliable path to "it just works," at the cost of higher power draw (Linux
boot, WiFi, always-on daemon) than a microcontroller — harder to run for long stretches on a
small battery, and less in the spirit of a lean embedded build, but by far the least new
engineering.

## Recommendation

Given the explicit goals (battery powered, fits inside the phone, real dial tone, real
call), in order of how well they match "small + battery + inside the case" *and* how much
new/unproven engineering they require:

1. **ESP32 + weeBell-style SLIC/codec board, Bluetooth HFP to your cellphone** — smallest
   scope, proven hardware and firmware to build from, no server to run, uses your existing
   phone number. This is the pragmatic sweet spot.
2. **ESP32 + SLIC/codec board, SIP/VoIP** (own SIP account or self-hosted Asterisk) — same
   hardware, if you specifically want VoIP (e.g. a dedicated number, or no cellphone
   pairing) rather than routing through a phone.
3. **Pico W, self-built SIP/RTP stack** — only if using a Pico W specifically is
   non-negotiable. Should be scoped as its own R&D milestone (get INVITE/ACK/BYE + one-way
   G.711 audio working against a test SIP server) before any phone-specific wiring is built,
   since it's the part most likely to stall the project.
4. **Pi Zero 2 W + baresip** — if reliability/time-to-working-demo matters more than doing it
   on a "bare" microcontroller, and battery life inside the case is not a hard constraint
   (or you're fine with a larger battery / external power).

## Open questions to resolve before design

- **Which telephone, specifically?** (Western Electric 500/2500, GPO 300/700series, etc.)
  Wiring (2-wire vs 4-wire handset, carbon vs electret mic, ringer coil impedance) differs by
  model and determines the analog front end.
- **Do you want the mechanical bell to actually ring on incoming calls**, or is this
  outbound-calling only? Real ringing needs ~90VAC/20Hz generation (a small transformer-based
  or 555/H-bridge boost circuit) or a bell replaced with a buzzer; skipping incoming calls
  entirely removes a whole subsystem.
- **Call routing preference**: VoIP account (own number, needs a SIP provider or
  self-hosted PBX) vs. bridging to your existing cellphone (Bluetooth HFP, uses your number,
  needs cellphone nearby) vs. cellular SIM in the phone itself (SIM800/A7670-style module,
  own number, no other phone needed, needs cellular airtime).
- **Battery budget**: WiFi/BT-radio duty cycle and audio codec power draw will dominate;
  worth deciding idle vs. talk-time targets before picking cell chemistry/capacity, since
  that also constrains how much fits inside the case.
- **Is Pico W a hard requirement** or a starting assumption? This is the single biggest
  fork in the road above.

## Sources consulted
- [Interfacing a Telephone No 746 Rotary Phone – Raspberry Pi Forums](https://forums.raspberrypi.com/viewtopic.php?t=386538)
- [retrophone – chkronenberg (Pi Zero 2 W, baresip)](https://github.com/chkronenberg/retrophone)
- [Rasipfon – weigu.lu](https://www.weigu.lu/sb-computer/rasipfon/index.html)
- [Convert A Rotary Phone To VOIP Using Raspberry Pi – Hackaday (2015)](https://hackaday.com/2015/03/09/convert-a-rotary-phone-to-voip-using-raspberry-pi/)
- [rotary-phone-audio-guestbook – nickpourazima](https://github.com/nickpourazima/rotary-phone-audio-guestbook)
- [PiTelephone – Logic Ethos](https://logicethos.com/blog/pitelephone-raspberry-pi-retro-dial-phone/index.html)
- [rp2040_i2s_example – malacalypse (bidirectional I2S on RP2040)](https://github.com/malacalypse/rp2040_i2s_example)
- [esp32-sip-phone – aneez11](https://github.com/aneez11/esp32-sip-phone)
- [Free Your Rotary Telephone From Its Wire – Hackaday (2026)](https://hackaday.com/2026/09/05/free-your-rotary-telephone-from-its-wire/)
- [rotary – mnutt (Asterisk)](https://github.com/mnutt/rotary)
- [A Dial Phone SIPs Asterisk – Hackaday (2024)](https://hackaday.com/2024/03/24/a-dial-phone-sips-asterisk/)
- [weeBell hardware – danjulio](https://github.com/danjulio/weeBell_hardware)
- [weeBell Bluetooth firmware – danjulio](https://github.com/danjulio/weeBell_bluetooth)
- [weeBell – personal central office for POTS phones – Hackaday.io](https://hackaday.io/project/191002-weebell-personal-central-office-for-pots-phones)
- [ESP32 brings old phones back to life – ESP32 Forum](https://www.esp32.com/viewtopic.php?t=34561)
- [Blue POT – FXS/SLIC hobbyist build – Hackaday.io](https://hackaday.io/project/166359-blue-pot)
- [SLIC – SigmazGFX (Arduino + QCX601 SLIC + SIM800/900 GSM)](https://github.com/SigmazGFX/SLIC)
- [Building A PBX Part 4 — Hooking Up A Rotary Phone – famicoman](https://famicoman.com/2018/07/29/building-a-pbx-part-4-hooking-up-a-rotary-phone/)
- [Raspberry Pi Pico W Now Supports Bluetooth – Hackaday](https://hackaday.com/2023/06/15/raspberry-pi-pico-w-now-supports-bluetooth/)
- [New functionality: Bluetooth for Pico W – Raspberry Pi (ACL/LE only, no SCO/HFP audio)](https://www.raspberrypi.com/news/new-functionality-bluetooth-for-pico-w/)
- [Generating rings – Hackaday.io (20Hz/90V ring generation)](https://hackaday.io/project/180332/log/196273-generating-rings)
- [phone-ringer – jonscheiding (Arduino ring voltage generator)](https://github.com/jonscheiding/phone-ringer)
