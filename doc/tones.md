# Tones, Dial Protocol and Service Behavior — Verified Reference

Reference for making the phone behave like dial-telephone service from the era before tone
standardization. Companion to `doc/initial_design.md` and `doc/audio_clips.md`.
Researched online in September 2026.

**Policy: only verified tones go into firmware era profiles.** Anything not verified is listed in
section 9 and stays out of the firmware until a source is found.

**Source tags** used throughout:

- **[P]** Primary: a contemporary document from the period (1924 Automatic Electric handbook,
  1959 patent describing then-current practice).
- **[S]** Secondary: an encyclopedic or reference site that I read directly.
- **[R]** Reported: appeared only in a search-engine summary; the page was not read or was
  unreachable. Treated as unverified.
- **[I]** Inferred by us from sourced facts or how the hardware works.

## 0. Summary of era profiles

There was no single tone standard before 1965. Tones "varied from telephone exchange to exchange"
until standardization began with the first electronic switch in 1965. **[S]** So an "era" here means
a documented family of tones, not one exchange.

| # | Era profile | Verification | What is verified |
|---|---|---|---|
| A | **US, pre-1965 "low-tone" family** | Good | dial, busy, all-trunks-busy, high tone, ringback (sec. 1) |
| B | **US Precise Tone Plan, 1965 onward** | Good | dial, busy, reorder, ringback, off-hook (sec. 2) |
| C | **UK GPO Strowger, before the mid-1960s** | Partial | dial, busy, ring cadence; ringback frequency is **[R]** only (sec. 3) |

**Decided:** these three are the only eras for now. The era digits are `1` = A, `2` = B, `3` = C.

Not included because nothing verifiable was found: Germany, France, Japan, Australia, New Zealand,
India and the rest of Europe before standardization. The firmware already has modern-standard
entries for Australia, Europe, Germany pre-1979, India, New Zealand and the UK from its original
author; this research did not verify those (sec. 9).

## 1. Profile A — US, pre-1965 "low-tone" family

The best single source is a patent that describes the practice it was replacing:

> "The frequencies commonly utilized include cycles per second for ringing as modified by the
> particular ringing group pattern established; **low tone, usually 600 cycles per second
> modulated at 120 cycles per second for busy tone, all trunks busy tone and dial tone signals,
> busy tone being supplied at 60 impulses per minute and all trunks busy tone being supplied at
> 120 impulses per minute; high tone, usually 500 cycles per second for indicating operator
> trunking** and, as explained above, **audible ring back** for indicating to the calling
> subscriber that the called subscribers phone is ringing, **usually 420 cycles per second
> modulated at 40 cycles per second supplied in accordance with the established ringing
> pattern**."
> — US Patent 3,059,061, Lorain Products Corp., filed 26 June 1959, granted 16 October 1962. **[P]**

| Signal | Tone | Cadence |
|---|---|---|
| Dial tone | 600 Hz carrier amplitude-modulated at 120 Hz | continuous **[P]** |
| Busy | same low tone | 60 impulses/min (0.5 s on / 0.5 s off) **[P]** |
| All trunks busy | same low tone | 120 impulses/min (0.25 s on / 0.25 s off) **[P]** |
| High tone | 500 Hz | used for operator trunking **[P]** (not a subscriber signal) |
| Audible ringback | 420 Hz modulated at 40 Hz | follows the ringing pattern **[P]**, which is 2 s on / 4 s off for a normal line **[S]** |

**Corroboration and variants:**

- Older dial tone was 600 Hz amplitude-modulated by 120 Hz. **[S]** Variants had the 600 Hz carrier
  modulated at 120, 133, 140 or 160 Hz at levels of 61–71 dBrnC. **[S]**
- The ringback variants were 420/40 Hz for larger metropolitan offices and 500/40 Hz on small
  No. 5 crossbar offices. **[S]**
- A 1970s recording of a New York City panel exchange (GEdney-9) has the "old city dial tone" as a
  "120 Hz and 600 Hz blend," with distortion and noise from the motor-driven generators. **[S]**
- Customer instructions of the 1940s–50s described the sounds as: dial tone "a steady humming
  sound"; busy "a buzz-buzz-buzz sound"; ringing "a brrr brrr sound." **[S]** Those match the
  120 Hz-modulated dial and busy tones and a 40 Hz-modulated ringback.

**Level and modulation depth are not documented** in any source found. Start with 100% depth and
tune by ear (sec. 8).

**Caveat on dating:** the primary source is from 1959, and describes tones "commonly utilized." It
is consistent with the older secondary sources, but I have no 1940s-dated document giving these
exact numbers. Treat "1940s" as "consistent with, not proven by."

## 2. Profile B — US Precise Tone Plan (1965 onward)

Standardized as the first electronic switch (No. 1 ESS, Succasunna NJ, 1965) went into service.
**[S]** Tolerances: frequency ±0.5%, amplitude ±1.5 dB, harmonic distortion at least 30 dB down. **[S]**

| Signal | Frequencies | Level | Cadence |
|---|---|---|---|
| Dial tone | 350 + 440 Hz | −13 dBm | continuous |
| Busy | 480 + 620 Hz | −24 dBm | 0.5 s on / 0.5 s off |
| Reorder | 480 + 620 Hz | −24 dBm | 0.25 s on / 0.25 s off |
| Ringback | 440 + 480 Hz | −19 dBm | 2 s on / 4 s off |
| High tone | 480 Hz | −17 dB | |
| Receiver off-hook | 1400 + 2060 + 2450 + 2600 Hz | | 0.1 s on / 0.1 s off |

All **[S]**. A second reference table gives ringback at −13 dBm and lists reorder as 0.3 s on /
0.2 s off (local) or 0.2 s on / 0.3 s off (toll); those disagree with the primary values above, so
0.25/0.25 and −19 dBm are used.

## 3. Profile C — UK GPO Strowger (partial)

| Signal | Tone | Verification |
|---|---|---|
| Dial tone | **33 Hz** nominal, from a motor-driven ringing machine (small exchanges: a vibrating-reed generator). Some later machines produced 50 Hz. Not a sine: rich in harmonics, most energy at 200–400 Hz, since a pure 33 Hz would be nearly inaudible through a receiver. | **[S]** Wikipedia (33 Hz, machines); **[R]** harmonic content |
| Busy | **400 Hz**, 0.75 s on / 0.75 s off. In the mid-to-late 1960s this changed to 0.375 s / 0.375 s. | **[S]** |
| Ringing cadence | double ring: 0.4 s on, 0.2 s off, 0.4 s on, 2.0 s off. Pre-digital exchanges used this same cadence "but used several different tones depending on the type of equipment." Digital switching (1980–81) introduced 400 + 450 Hz. | **[S]** |
| Ringback tone | 400 Hz modulated by the ringing current, interrupted in the ringing cadence | **[R]** only |
| Number unobtainable | 400 Hz, interrupted | **[R]** only, and cadence sources conflict |

Strowger dial standard: 10 impulses/s (allowable 7–12), break ratio 66% (allowable 63–72%). **[S]**

Profile C is only partly usable: dial tone and busy are verified in frequency, the ringing cadence
is verified, and the **ringback frequency is not**. It ships flagged "partial," using the reported
400 Hz-modulated ringback as a placeholder until confirmed (sec. 10).

## 4. How step-by-step exchanges actually behaved (1924 primary source)

*Principles of Automatic Telephony*, Harry P. Mahoney, Automatic Electric Company, Chicago,
copyright 1924. I read the text of the scanned PDF directly. **[P]** It describes Strowger
step-by-step apparatus. It gives **no tone frequencies**, but it documents behavior:

- **Dial tone is induced into the line.** When a subscriber lifts the receiver and a first selector
  is seized, a relay "closes the dial tone circuit through the primary of the induction coil. The
  dial tone is induced into the secondary winding ... and is heard by the calling party, indicating
  that connection has been made to a first selector and that the subscriber can now dial." **[P]**
- **Dial tone was optional.** The text refers to "exchange systems where a dial tone is not used."
  **[P]** (Its own review questions ask "Why is the dial tone used?", so it was a feature you could
  choose to have.)
- **Busy tone** is connected to the calling line by the connector switch when the called line is
  busy, and induced into the line the same way. **[P]**
- **All trunks busy:** a selector that finds no idle trunk on a level steps to the 11th position
  and gives the caller **busy tone**. **[P]** So in a step-by-step exchange there was **no separate
  fast "reorder" tone**; the caller heard the same busy signal.
- **Dialing during busy does nothing.** The rotary impulse circuit is opened while the connector
  gives busy, "so that in case the calling subscriber operates the dial, the connector cannot step
  from the busy line." **[P]** The only way to reach the number: "hang up the receiver, allow the
  connector to restore to normal, and dial the number again." **[P]**
- **Ringing:** the connector applies ringing current in bursts. The generator control relay is
  operated intermittently by a cam on the generator shaft or another interrupter. The gap is the
  "silent period," when the bell does not ring. **[P]**
- **Ringback ("ringing induction tone"):** the caller hears "ringing induction tone ... in the
  receiver each time the control relay sends ringing current out over the line," because some of
  the ring current is coupled back to the calling telephone. **[P]** A later source describes the
  step-by-step ringback as coupled harmonics of a 20 Hz vibrator, and the busy signal as a
  self-interrupted relay ("dying mosquito"). **[S]**
- **Ring trip:** if the called party answers during the ring or the silent period, the ring-cut-off
  relay operates and stops the generator and interrupter. **[P]**
- **Off-normal dial contacts:** turning the dial closes shunt springs that place a shunt around the
  transmitter and receiver. This "reduces the resistance of the telephone circuit to a minimum
  during the sending of impulses, but also prevents the impulses from sounding in the subscriber's
  ear." **[P]**
- **Permanent signals** (a line left off-hook with nothing dialed) were a maintenance matter: the
  supervisory switch "is closed during the period that the selectors are routined for permanents."
  **[P]** No automatic timed treatment is described in this book.

## 5. Rotary dial protocol

| Parameter | Value | Source |
|---|---|---|
| Nominal dial speed | 10 impulses per second | **[P]** 1924 |
| Speed the exchange accepts | dials "operate the switches satisfactorily" from 8 to 12 pulses/s | **[P]** 1924 |
| Break / make | 60 ms / 40 ms (60% break). GPO: 66% (63–72%). | **[S]** |
| Bell adjustment tolerance | 9.5–10.5 pulses/s at the phone; exchange tolerance about 8–11 | **[S]** |
| Digit encoding | 1 pulse = 1 ... 10 pulses = 0 | **[S]** |
| Interdigital pause | minimum about 0.6 s | **[R]** (search summary of a reference page; the 1924 book gives no figure) |
| Dial layout | digits counterclockwise from upper right; 2=ABC 3=DEF 4=GHI 5=JKL 6=MNO 7=PRS 8=TUV 9=WXY, 0=Operator | **[S]** |

The 1924 book also states that the exchange's own impulse machine ran at about 13 pulses/s, which
is a design figure for the exchange side, not the phone. **[P]** (The scan's text says "SO per cent
greater"; 13 is 30% above 10, so I read it as 30%.)

## 6. Ringing at the bell

- Normal ringing: **20 Hz, about 90 V** (60–105 V RMS across administrations), from a motor-driven
  ringing generator. **[S]**
- Cadence for a single-party line: **2 s on / 4 s off**. **[S]** Party-line code ringing used other
  cadences (for example 1 on, 1 off, 1 on, 3 off). **[S]**
- **Ring trip** (see sec. 4): answering stops the ring. **[P]**
- Our bell is driven by the DRV8825 at a bench-tuned frequency, not a real 20 Hz / 90 V supply
  (see `initial_design.md`).

## 7. Off-hook, partial-dial and failed-call behavior, by era

**Decided:** these behaviors are set by the era selection (mode 5), and match the era as closely
as the sources allow.

What the sources say:

- **1924 step-by-step:** the book describes **no timed treatment** for a phone left off-hook or a
  partly dialed number. A "permanent" (line left off-hook) was a maintenance matter: the
  supervisory switch "is closed during the period that the selectors are routined for permanents."
  **[P]** (This is from the absence of any timer in the text, not a statement that none existed.)
- **Later switches** sent an off-hook phone to a **permanent signal holding trunk (PSHT)**, which
  played either the howler or a **480/500 Hz "high tone."** **[S]** The alert came after a
  preselected time "such as 30 to 45 seconds." **[R]** The GEdney-9 panel recording (1970s) shows
  dial tone timing out after about **50 seconds**, then the call moving to the PSHT. **[S]** When
  these treatments began is not documented in anything found.
- **Reorder** can mean "a timed-out sender or unassigned code dialed." **[R]** No figure for the
  incomplete-dial timeout in the Precise-plan era was found. A patent summary mentioned a
  16-second timer within an 8–20 second range chosen by the telephone company. **[R]**
- **Failed or busy calls:** in step-by-step, a busy line and all-trunks-busy both gave busy tone
  (sec. 4). **[P]** In Precise-plan crossbar and electronic switches, busy and reorder were
  distinct tones. **[S]**

Behavior table (what the firmware does):

| Situation | A: US pre-1965 | B: US Precise 1965+ | C: UK GPO (partial) |
|---|---|---|---|
| Off-hook, nothing dialed | dial tone until you hang up or dial; **no timeout** **[P]** | dial tone for **about 50 s**, then the off-hook howler (1400/2060/2450/2600 Hz, 0.1 s on/off) **[S]** | as A **[I]** |
| Part of a number dialed (fewer than 10 digits), then you stop | **silence** until you hang up or dial more; **no timeout** **[P]** | **reorder** after **about 16 s** of no dialing **[R]** | as A **[I]** |
| Call ends without being answered (cellphone reports busy or failure) | busy tone, 60 impulses/min **[P]** | busy tone, 0.5 s / 0.5 s **[S]** | busy tone, 400 Hz 0.75 s / 0.75 s **[S]** |
| Dial pulses while a busy tone is playing | ignored **[P]** | ignored **[I]** | ignored **[I]** |
| Dial tone at all | yes **[P]** | yes **[S]** | yes, 33 Hz **[S]** |

Notes:

- **Era A and C will feel like a dead phone if you stop mid-number.** That is the documented
  behavior; hanging up resets. I would add one compile-time constant per era (0 = no timeout) so
  the no-timeout behavior can be changed to a timed one without code changes.
- **Era C's rows are inferred** from Era A: the GPO also used Strowger step-by-step equipment, but I
  have no UK source for its off-hook or partial-dial treatment.
- **The firmware cannot tell busy from failure.** The cellphone's Bluetooth handsfree profile
  reports the call ended, not why, so every unanswered end gets the era's busy tone. **[I]**
- **Era B's 16 s and 50 s are starting values** to tune. 50 s comes from the 1970s panel recording;
  16 s from an unnamed patent summary.

## 8. Firmware synthesis notes

- **Dial tone (Profile A)** is a 600 Hz carrier amplitude-modulated at 120 Hz. **[I]** AM at 100%
  depth gives spectral lines at 480, 600 and 720 Hz. At 8 kHz, 25 ms is exactly 3 cycles of 120 Hz
  and 15 cycles of 600 Hz, so a **200-sample wavetable loops with no seam**. The sample-based tone
  path (`sample_tone_tx_bufP`) already loops a buffer.
- **Ringback (Profile A):** 420 Hz modulated at 40 Hz. The waveform repeats every 50 ms (2 cycles of
  40 Hz, 21 cycles of 420 Hz), so a **400-sample wavetable** is exact. The 500/40 Hz variant also
  fits in 400 samples (25 cycles). **[I]**
- **Busy and all-trunks-busy** reuse the dial-tone wavetable, gated 0.5 s / 0.5 s and 0.25 s / 0.25 s
  respectively. **[I]** The existing sample path has no cadence, so gating must be added.
- **UK dial tone:** 33 Hz harmonic-rich. One 30 ms cycle is 240 samples. The exact waveform is not
  documented; suggested start is a pulse-like waveform emphasizing 200–400 Hz, tuned by ear. **[I]**
- **UK busy:** 400 Hz is exactly 20 samples per cycle at 8 kHz. **[I]**
- `tone_info_t` in `international.h` holds up to four frequencies with one shared level, so the
  unequal AM sidebands need the wavetable route, not `super_tone_tx_make_step_4`.
- **Levels and modulation depth are not documented for the period tones.** Set by ear and against
  recordings. The current firmware's reorder level (−13 dB) is well above the −24 dBm spec.
- **Earpiece mute while dialing** (off-normal contact, sec. 4) is authentic and removes the need to
  filter pulse clicks. **[I]**
- **Ignore dial pulses while busy tone plays** (sec. 4). **[I]**

## 9. Excluded: not verified

These came up in searches but are **not** in any era profile:

- "Old dial tone is 600 Hz modulated by 120 Hz from a tone alternator, or **133 Hz** from an
  interrupter," and "old high tone 500 Hz (alternator) or 400 Hz (interrupter)." Reported by a
  search summary; I could not find the page that says it (the Telephone Tribute page does not
  contain it). **[R]**
- UK ringing tone at **133 Hz** with the 0.4/0.2 cadence, from a Strowger textbook snippet whose
  site was unreachable. **[R]**
- "Call-in-progress tones are usually 800 Hz with 50% duty ratio" from another unnamed textbook
  snippet. **[R]**
- **No-such-number tone**: a 1941 Nature note (paywalled) and a Bell Laboratories Record summary
  describe a siren-like tone sweeping 200–400 Hz at half-second intervals. I could not read either.
  **[R]**
- UK number-unobtainable cadence: sources conflict (continuous vs. 2.5 s on / 0.5 s off). **[R]**
- Any pre-standard tones for Germany, France, Japan, Australia, New Zealand, India or the rest of
  Europe. Only modern values were found (for example, 425 Hz single tone for most of Europe, France
  440 Hz, Japan 400 Hz), and those are not period tones. **[S]**
- Original 1940s Bell Labs / BSTJ documents specifying these tones. Not found; the BSTJ and Bell
  Laboratories Record archives are on worldradiohistory.com and could still be searched.

## 10. Open questions

1. **Era digits — decided.** `1` = US pre-1965 (A), `2` = US Precise (B), `3` = UK GPO (C). No
   other eras for now.
2. **UK ringback frequency.** Era C is partial: the ringback tone is unverified (sec. 3). Until it is
   confirmed the firmware would use the reported 400 Hz modulated by the ringing cadence as a
   placeholder, flagged in the code. You have said to keep Era C, so that is the plan.
3. **Verification by measurement.** Recordings (telephoneworld.org, PhoneTrips.com) could be
   analyzed with an FFT to confirm modulation depth, levels and the ringback waveform. I have not
   done this.
4. **Anachronism.** 1 + 10-digit dialing was not subscriber-dialed in the 1940s. The first
   cross-country direct-dial call from a subscriber was in 1951. **[S]** Local numbers were two
   letters + five digits (for example CHelsea 2-5034 dialed as 2425034), and long distance was
   placed through the operator (dial 0). **[S]** Area codes date from 1947 and were used by
   operators. **[S]** Keeping 1 + 10 is a decision already made for the novelty use.

## Sources

- **[P]** US Patent 3,059,061 (Lorain Products Corp., filed 1959): <https://patents.google.com/patent/US3059061A/en>
- **[P]** Mahoney, *Principles of Automatic Telephony* (Automatic Electric Co., 1924), scan: <https://www.telephonecollectors.info/index.php/browse/bc-switching-library/automatic-electric/ae-switching-docs/9969-ae-automatic-telephony-1924-bc-ocr-r/file>
- **[S]** Precise tone plan: <https://en.wikipedia.org/wiki/Precise_tone_plan>
- **[S]** Dial tone: <https://en.wikipedia.org/wiki/Dial_tone>
- **[S]** Busy signal: <https://en.wikipedia.org/wiki/Busy_signal>
- **[S]** Ringing tone: <https://en.wikipedia.org/wiki/Ringing_tone>
- **[S]** Reorder tone: <https://en.wikipedia.org/wiki/Reorder_tone>
- **[S]** Off-hook tone: <https://en.wikipedia.org/wiki/Off-hook_tone>
- **[S]** Permanent signal: <https://en.wikipedia.org/wiki/Permanent_signal>
- **[S]** Pulse dialing: <https://en.wikipedia.org/wiki/Pulse_dialing>
- **[S]** Rotary dial: <https://en.wikipedia.org/wiki/Rotary_dial>
- **[S]** Party line: <https://en.wikipedia.org/wiki/Party_line_(telephony)>
- **[S]** Telephone Tribute, Signals and Circuit Conditions: <https://www.telephonetribute.com/signal_and_circuit_conditions.htm>
- **[S]** ElmerCat, Dial Tone (GEdney-9 recording notes): <https://elmercat.org/phone/dialtone/>
- **[S]** Tech-FAQ, Telephone Tone Frequencies: <https://www.tech-faq.com/frequencies-of-the-telephone-tones.html>
- **[S]** Bell 1940s–50s booklets, Click Americana: <https://clickamericana.com/topics/science-technology/how-do-you-use-a-rotary-phone>
- **[S]** Ringing machines, Tribute to Relays: <https://www.calling315.com/ringing-machines>
- **[S]** Telephone exchange names / 2L-5N numbering: <https://en.wikipedia.org/wiki/Telephone_exchange_names>
