# Phone UI

How the dial-and-handset interface behaves, and what the caller hears at each point. The spoken clips and their text
are in `doc/audio_clips.md`, and their words in `voice/tts_script.txt`; tones and per-era behavior are in
`doc/tones.md`; the hardware and firmware design is in `doc/initial_design.md`.

## General

- Hanging up cancels whatever is happening and returns to idle.
- Off-hook, the caller hears the era's dial tone. The first digit dialed selects a mode, and dial tone stops there, as
  it stopped at the first digit on a real exchange (`tones.md`, section 4). No dial tone plays inside any mode, and in
  mode 1 the caller hears silence between the digits of the number, with no second dial tone after the area code.
- Inside a mode every digit, including `0`, is ordinary input.
- When a mode's interaction is complete, the phone returns to dial tone as though the handset had just been picked up.
- Hanging up or dialing a digit interrupts a clip that is playing.
- Off-hook with nothing dialed, and in mode 1, a timeout behaves exactly as the era did for a phone left off-hook or a
  number left partly dialed (`tones.md`, section 7). In eras 1 and 3 that means waiting until the caller hangs up.
- In the other modes, waiting is handled by the operator (see "Waiting in a mode").
- The unassigned mode digits `7` and `8` each get a witty remark from the era's operator, then dial tone.
- A digit that is not valid inside a mode gets a sassy remark specific to that mode and era, and the mode
  then waits for the same input again. If the caller keeps dialing invalid digits, the remarks escalate.
- Where the era would have played a tone, the phone plays it rather than leaving silence.
- The phone runs from USB and has no battery, so there are no battery notifications.

## Modes

| Digit | Mode |
|---|---|
| `0` | Operator |
| `1` | Place a call |
| `2` | Bluetooth pairing |
| `3` | Earpiece volume |
| `4` | Microphone volume |
| `5` | Tone era |
| `6` | Forget pairing |
| `7`, `8` | Unassigned |
| `9` | Voice assistant |

The operator only names each mode. A mode that needs more input gives its own instructions once entered.

### 0: Operator

The aim is what a real operator did when you dialed `0`. For now it reads one line per mode, in dial order:

- "Dial one and then the ten-digit number to place a call."
- "Dial two to pair a phone."
- "Dial three to set the earpiece volume."
- "Dial four to set the microphone volume."
- "Dial five to choose the tone era."
- "Dial six to forget the paired phone."
- "Dial nine to speak with your telephone's assistant."

The whole list is one clip per era. It ends with a closing line (thanks, goodbye, or "dial zero again any time"),
and then the phone returns to dial tone. It does not read the current settings or the connection status.

### 1: Place a call

After the `1`, exactly 10 digits are collected, and the call is placed on the 10th digit as `1` plus those 10 digits.
The number is not read back before dialing.
Fewer than 10 digits followed by a stop gives silence (eras 1 and 3) or reorder after about 16 s (era 2). Ringback,
busy and reorder are generated tones. No clips are spoken.

### 2: Bluetooth pairing

The phone opens a discoverable window of about 3 minutes, and the user chooses the phone in their cellphone's
Bluetooth settings (Just Works. No passkeys). The phone announces the start, success, failure or timeout.

- Only one phone can be paired. If a phone is already paired, mode 2 does not open the window: the operator says a
  phone is already paired and it must be forgotten first (mode 6), then dial tone.
- While the window is open, a hold scene of about 3 minutes plays, until pairing succeeds (which cuts it off) or the
  window closes. Eras 1 and 3: the operator hunts for the caller's phone at her switchboard, one side of her
  conversations with colleagues heard over the line. Era 2: period hold music with the recording checking in now and
  then. Each scene repeats the pairing instruction partway through.
- The phone is discoverable only during the pairing window, which can open only when no phone is paired, so a pairing
  attempt can never interrupt a call.

### 3 and 4: Earpiece and microphone volume

1. On entering, the phone says the label and the instruction: "Earpiece volume. Dial one for the quietest, up to nine
   for the loudest."
2. The caller dials `1` (quietest) to `9` (loudest).
3. The new level is applied at once. The phone then says the label, the digit and, for the earpiece, a short sample
   sentence, all at the new level, so the confirmation is also a sample of the loudness. The microphone level is only
   confirmed; there is no record-and-play-back test.

### 5: Tone era

One digit selects the era: `1` US before 1965, `2` US Precise Tone Plan, `3` UK GPO.

- On entering, the operator of the era set before `5` was dialed names the eras by short names: "Dial one for the
  Switchboard Days, two for Ma Bell, or three for the Post Office."
- After a choice, the new era's operator takes over with a longer description of her era (its time, its sounds, what
  to expect), then dial tone.
- Era 3 (UK GPO) stays, despite its partly verified tones; the aim is to make it work.

### 6: Forget pairing

The phone asks the caller to dial `6` again to confirm ("Dial six again to unpair. Hang up to cancel."). On `6` it
removes the bond and says the phone is unpaired. Hanging up cancels. If no phone is paired, the operator says there
is nothing to forget, then dial tone.

### 9: Voice assistant

Dialing `9` puts the caller through to the cellphone's voice assistant (Siri on an iPhone), as the original firmware
did with `0`. The operator announces it in the era's style, then the phone sends the handsfree voice-recognition
command, and the assistant is heard in the earpiece and hears the microphone. When the assistant finishes (the
cellphone turns voice recognition off), dial tone returns; hanging up ends it. If the assistant places a call, the
call is handled as any other call. If the cellphone does not start the assistant, the operator says so, then dial
tone. Without a connected phone, `9` gets a remark that there is no telephone to ask.

## Dial clicks

While the dial turns, the earpiece plays the dial pulses at a level fixed by the era (a best guess at each era's
sets; the levels are firmware constants):

- **Era 1 (US before 1965): faint** soft clicks, as on the many sets that only attenuated them.
- **Era 2 (US Precise): silent,** the earpiece muted by the dial's off-normal contact.
- **Era 3 (UK GPO): full,** the pulses clearly heard.
- **No SD card: silent.**

The clicks are generated in the firmware, a short pulse on each dial-pulse edge. Other audio in the earpiece is muted
while the dial turns, except an interruption remark (see "Interruptions").

## Interruptions

Dialing while the operator is speaking cuts the clip off, and the operator answers with a short remark that fits the
era. This deliberately breaks period accuracy:

- The remark starts as soon as the dial leaves rest (the dial-in-progress contact closes), not after the digit.
- The dial pulses, at the dial-click setting, are mixed with the remark as they arrive, not played instead of it.
- The dialed digit works as normal, as if the operator had not been speaking. If the remark is still playing when the
  digit completes, it finishes first, then the mode starts.
- Only clips where the operator is talking at the caller trigger a remark: the operator list, the no-phone monologue
  and the era description. (Dialing during the pairing hold scene gets the pairing mode's own invalid-digit remark,
  since pairing takes no digits.) A prompt that asks the caller to dial (a mode's instructions, a
  waiting remark) just stops quietly and takes the digit.
- The first interruption after the handset is lifted gets one of the era's remarks at random; each later one steps up
  the era's escalation (three steps, the last repeating). It resets when the handset is hung up.

## Waiting in a mode

Inside modes other than 1, the phone behaves as if an operator were on the line. If she asks the caller to dial and
nothing is dialed:

1. She makes a first remark specific to the mode, for example "Do you want to forget this phone or what?" in mode 6.
2. She then follows a progression shared by all modes, repeated at intervals: sassy and impatient at first, then sad.
3. Finally the caller hears her hang up (a recorded sound, `sfx_hangup` in `audio_clips.md`), and the phone returns to
   dial tone.

The timing is set by constants in the firmware. Defaults: the mode's first remark after 3 s, then the shared remarks
after gaps of 5, 7 and 9 s, then the hang-up and dial tone.

## No phone connected

When the handset is lifted and no cellphone is connected, the operator speaks instead of dial tone. She assumes the
caller is listening.

1. **Opening.** If no phone has been paired, she explains how to pair (dial `2`). If a phone is paired, she first
   tries to reach it (see "Connection status"); only if that fails does she say she cannot reach it and will keep
   trying.
2. **Monologue.** While nothing is dialed, she works through six steps at growing intervals: first asking about pairing
   (or about the missing phone), then confused, then drifting into light-hearted loneliness, now and then asking whether
   anyone is still there.
3. **Ending.** After the last step she says a closing line, and the era's off-hook treatment follows: dial tone until
   hang-up in eras 1 and 3 ("Putting you back on dial tone, pal"), and the off-hook howler in era 2, after a
   recorded "please hang up".

Dialing changes her mood and resets the monologue:

- A digit dialed after the lonely steps starts with a glad remark ("Oh! Somebody's there.") before the digit acts.
- `1` cannot place a call without a phone: she says so, points to `2`, and the monologue restarts.
- `0`, `2` to `6` work as usual (settings are local; forgetting works while a paired phone is away, and gives the
  "nothing to forget" remark when none is paired). `7` and `8` get the usual unassigned
  remarks, and `9` gets a remark that there is no telephone to ask.
- When a mode finishes and there is still no phone, she returns to the subject ("So. About that phone") and
  the monologue restarts from its first step.
- If the phone connects during the monologue, she says so and dial tone starts.

The timing is set by constants in the firmware. Defaults: the opening at pick-up, then the six steps after gaps of 6, 8,
10, 12, 14 and 16 s, then the closing line and the era's treatment, about a minute in all.

## Connection status

The operator follows what the Bluetooth stack reports, and a status change interrupts whatever she is saying, the way
dialing does.

What the firmware can know (ESP-IDF 4.4):

- Whether a phone is paired (the bond list), and whether the pairing window is open (our own timer).
- Pairing succeeded or failed, and a bond was removed.
- The handsfree connection: connecting, connected and ready for calls, or disconnected. A failed attempt and a dropped
  connection both end as disconnected; the reason (out of range or Bluetooth off) is not reported.
- Once connected: whether the cellphone has network service.

How the clips follow it:

- **Pick-up with a paired phone not connected.** The phone starts a reconnect attempt at once (it also retries every
  10 s in the background). She says she is trying ("Hang on, ringing your phone"). On success she says so and
  dial tone starts; on failure (after about 5 s) the "can't reach your telephone" monologue begins.
- **Connected while she is speaking.** The clip is cut off and she breaks in ("Wait, got it! You're connected."),
  then dial tone. If it connects during a pause in the monologue, she says so without the break-in.
- **Connection lost** while off-hook and not in a call, or during a call (the call ends): she says she has lost the
  telephone, and the "can't reach" monologue begins.
- **Pairing (mode 2).** Window open: the pairing instructions. Pairing succeeded: she breaks in if still speaking ("Oh,
  it's paired! Just a moment while it connects."). Connected and ready: the success line, then dial tone. Paired but
  not connected within 15 s: she says it is paired but won't connect, and the "can't reach" monologue begins. Pairing
  failed, or the window closed: the failure or timeout line, then dial tone.
- **No network service** reported by the connected cellphone: at pick-up she says so before dial tone; dialing `1`
  gets a "still no service" remark, then dial tone. Settings still work.
- **Forget** (mode 6) says "forgotten" only once the bond removal is confirmed.
- Signal strength, roaming, the cellphone's battery level and its carrier name are also reported, but not used.

The waits (the 15 s after pairing, and the 10 s retry interval) are firmware constants.

## Settings storage and a missing SD card

The settings are stored on the SD card. Without a card there are no clips and no stored settings, so the phone runs a
minimal tone-only mode (not expected in practice, so kept simple):

- **Dial tone** is distinct: the era 2 dial tone (350 + 440 Hz), interrupted 0.1 s on, 0.1 s off, so a missing card is
  obvious at pick-up.
- **Only mode 1 works.** Any other first digit is ignored. Without a connected phone, a completed number gets reorder.
- **Fixed defaults** in the firmware: era 2 (US Precise Tone Plan) tones and behavior, earpiece volume 5, microphone
  volume 5 (−15 dB), dial clicks silent, US ring cadence.

## Calls

- An incoming call rings the bell (US: 2 s on, 4 s off). Lifting the handset answers and stops the ring at once.
- Digits dialed during a call go to the cellphone as DTMF.
- While an outgoing call is being set up, the cellphone's call audio is muted, so its own ringback and any carrier
  announcement are not heard, and the era's ringback plays instead. It starts when the cellphone reports the far end
  is ringing ("outgoing alerting"), or 2 s after dialing if that report never comes (a firmware constant), so there is
  no silence where the era would have played a tone. It stops when the call is answered or ends; the call audio is
  heard once the call is answered.
- A call that ends unanswered gets the era's busy tone (the firmware cannot tell busy from failure).
- When the far end hangs up (or the network drops the call) after it was answered: silence until the caller hangs up
  in eras 1 and 3, as the caller's side held the connection in step-by-step exchanges; in era 2, silence and then the
  era's off-hook treatment.

## Call errors

Errors while dialing or placing a call get era-specific handling. Each era keeps its period behavior but says what went
wrong where that helps.

What is detected:

- **Impossible number,** checked by the firmware as the digits arrive: an area code or exchange (the next three digits)
  starting with `0` or `1`, or an `N11` area code such as `411` or `911`. Caught on the digit that makes it impossible.
- **Blocked area code:** `900` and `976` (premium rate). Caught on the third digit.
- **The cellphone refuses to dial** (an error reply to the dial command, sometimes with a reason code). A "no service"
  reason gets the no-service remark (see "Connection status").

Once an error is caught, the remaining digits are ignored, as dialing was during busy tone. After the error clip the
phone returns to dial tone.

What the caller hears, by era:

- **Era 1 (US before 1965): the intercept operator.** Calls to impossible or disconnected numbers went to a live
  intercept operator, who asked what number you were calling and told you why it failed (Wikipedia, "Intercept
  message"). The era 1 operator plays her.
- **Era 2 (US Precise): a recorded intercept announcement,** preceded by the three-note Special Information Tone
  (`tones.md`, section 2). The tone dates from about 1980, later than the start of the era, but it is the sound people
  recognize.
- **Era 3 (UK GPO): the number-unobtainable tone** for a few seconds, then a short line from the operator. The line
  bends period accuracy to say what went wrong. The tone is reported, not verified (`tones.md`, section 3).

## Open issues

Issue numbers are kept when an issue is resolved, so references to them stay valid.

None at the moment.
