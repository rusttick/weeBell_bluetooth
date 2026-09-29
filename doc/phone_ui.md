# Phone UI

How the dial-and-handset interface behaves, and what the caller hears at each point. The spoken clips and their text
are in `doc/audio_clips.md`, and their words in `voice/tts_script.txt`; tones and per-era behavior are in
`doc/tones.md`; the hardware and firmware design is in `doc/initial_design.md`.

## General

- Hanging up cancels whatever is happening and returns to idle.
- Off-hook, the caller hears the era's dial tone. The first digit dialed selects a mode, and dial tone stops there, as
  it stopped at the first digit on a real exchange (`tones.md`, section 4). No dial tone plays inside any mode, and in
  mode 1 the caller hears silence between the digits of the number.
- Inside a mode every digit, including `0`, is ordinary input.
- When a mode's interaction is complete, the phone returns to dial tone as though the handset had just been picked up.
- Hanging up or dialing a digit interrupts a clip that is playing.
- Off-hook with nothing dialed, and in mode 1, a timeout behaves exactly as the era did for a phone left off-hook or a
  number left partly dialed (`tones.md`, section 7). In eras 1 and 3 that means waiting until the caller hangs up.
- In the other modes, waiting is handled by the operator (see "Waiting in a mode").
- The unassigned mode digits `7`, `8` and `9` each get a witty remark from the operator, matching the era and
  personality, then dial tone.
- A digit that is not valid inside a mode gets a sassy remark specific to that mode, era and personality, and the mode
  then waits for the same input again. If the caller keeps dialing invalid digits, the remarks escalate.
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
| `7`, `8`, `9` | Unassigned |

The operator only names each mode. A mode that needs more input gives its own instructions once entered.

### 0: Operator

The aim is what a real operator did when you dialed `0`. For now it reads one line per mode, in dial order:

- "Dial one and then the ten-digit number to place a call."
- "Dial two to pair a phone."
- "Dial three to set the earpiece volume."
- "Dial four to set the microphone volume."
- "Dial five to choose the tone era."
- "Dial six to forget the paired phone."

The list ends with a closing line (thanks, goodbye, or "dial zero again any time"), and then the phone returns to dial
tone. It does not read the current settings or the connection status.

### 1: Place a call

After the `1`, exactly 10 digits are collected, and the call is placed on the 10th digit as `1` plus those 10 digits.
Fewer than 10 digits followed by a stop gives silence (eras 1 and 3) or reorder after about 16 s (era 2). Ringback,
busy and reorder are generated tones. No clips are spoken.

### 2: Bluetooth pairing

The phone opens a discoverable window of about 3 minutes, and the user chooses the phone in their cellphone's
Bluetooth settings (Just Works. No passkeys). The phone announces the start, success, failure or timeout.

### 3 and 4: Earpiece and microphone volume

1. On entering, the phone says the label and the instruction: "Earpiece volume. Dial one for the quietest, up to nine
   for the loudest."
2. The caller dials `1` (quietest) to `9` (loudest).
3. The new level is applied at once. The phone then says the label, the digit and, for the earpiece, a short sample
   sentence, all at the new level, so the confirmation is also a sample of the loudness.

### 5: Tone era

One digit selects the era: `1` US before 1965, `2` US Precise Tone Plan, `3` UK GPO. The phone says the label, the
digit and the era's name.

### 6: Forget pairing

The phone asks the caller to dial `6` again to confirm ("Dial six again to unpair. Hang up to cancel."). On `6` it
removes the bond and says the phone is unpaired. Hanging up cancels.

## Dial clicks

While the dial turns, the earpiece plays the dial pulses in one of three settings:

- **Full:** the pulses heard loudly, as on an extension phone on the same line.
- **Faint:** soft clicks, as on sets that only attenuated them.
- **Silent:** the earpiece muted, as the dial's off-normal contact did on most period sets.

Other audio in the earpiece is muted while the dial turns, in every setting, except an interruption remark (below).

## Interruptions

Dialing while the operator is speaking cuts the clip off, and the operator answers with a short remark that fits the
era and the operator's personality. This deliberately breaks period accuracy:

- The remark starts as soon as the dial leaves rest (the dial-in-progress contact closes), not after the digit.
- The dial pulses, at the dial-click setting, are mixed with the remark as they arrive, not played instead of it.
- The dialed digit works as normal, as if the operator had not been speaking.

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
   hang-up in eras 1 and 3 ("I'll leave the dial tone on for you, dear"), and the off-hook howler in era 2, after a
   recorded "please hang up".

Dialing changes her mood and resets the monologue:

- A digit dialed after the lonely steps starts with a glad remark ("Oh! Someone's there!") before the digit acts.
- `1` cannot place a call without a phone: she says so, points to `2`, and the monologue restarts.
- `0`, `2` to `6` work as usual (settings are local; forgetting works while a paired phone is away, and gives the
  "nothing to forget" remark when none is paired). `7` to `9` get the usual unassigned remarks.
- When a mode finishes and there is still no phone, she returns to the subject ("Now, dear, about that telephone") and
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
  10 s in the background). She says she is trying ("Let me try your pocket telephone, dear"). On success she says so and
  dial tone starts; on failure (after about 5 s) the "can't reach your telephone" monologue begins.
- **Connected while she is speaking.** The clip is cut off and she breaks in ("Oh! There it is. You're connected."),
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
- Signal strength, roaming, the cellphone's battery level and its carrier name are also reported, but not used yet.

The waits (the 15 s after pairing, and the 10 s retry interval) are firmware constants.

## Settings storage and a missing SD card

The settings are stored on the SD card. Without a card there are no clips and no stored settings, so the phone runs in
a tone-only mode with fixed defaults, in which essentially only mode 1 (place a call) works.

## Calls

- An incoming call rings the bell (US: 2 s on, 4 s off). Lifting the handset answers and stops the ring at once.
- Digits dialed during a call go to the cellphone as DTMF.
- A call that ends unanswered gets the era's busy tone (the firmware cannot tell busy from failure).

## Open issues

Issue numbers are kept when an issue is resolved, so references to them stay valid.

Flow:

40. Mode 1, era 1: play a second dial tone after the area code? Some US offices gave one when an area code was
    dialed, to show the distant exchange was ready (`tones.md`, section 4). It may be the dial tone between digits
    you remember.

Missing SD card:

9. The fixed defaults: era, earpiece and microphone volume, dial clicks.
10. What digits other than `1` do in tone-only mode: ignored, or a tone? Does pairing (mode 2) still work silently?
11. Should the caller be told the card is missing (for example a distinctive tone at pick-up)?

Calls:

12. Read the dialed number back before calling? Only this needs the 30-clip digit set (`audio_clips.md`, "Reading digit
    strings naturally").
13. The cellphone refuses the call: busy tone or a clip?
14. A blocklist of area codes (needs a "That number cannot be dialed." clip)?
15. What plays when the far end hangs up after a call: busy tone, silence, or dial tone?

Pairing and forget:

16. The Bluetooth device name, which the pairing prompt has to say (`hwtest` uses `weeBell-test`).
17. Pairing when a phone is already paired (the new one replaces it): warn first?
18. What the caller hears during the pairing window: silence or a periodic tone?
19. Forget with nothing paired: a clip such as "No phone is paired."?

Settings:

21. Microphone volume sample: a confirmation only, record and play back ("Say something after the tone."), or sidetone
    while in the mode?
22. Era mode: an instruction on entering ("Dial one, two or three."), and read the era names there too?
23. Era name wording.
24. Keep era 3 (UK)? `ringer_driver_2.md` suggests tying the eras to whatever ring frequency the bell handles well.

Clips and voice:

26. Operator list: one clip per line (easier to change) or one clip?
27. Keep the 8 kHz clip bank? It was for notifications during a call, and none is left.
28. Voice assistant (from the original firmware): keep it, and on which digit?
29. Voice and style of the prompts (a calm period "telephone operator" was suggested).

Dial clicks:

30. How the caller chooses full, faint or silent: its own mode (on `7`, `8` or `9`), part of another mode, or fixed
    by the era?
31. The default setting (silent is the period-correct one for the caller's own phone).
32. The click sound: generated in the firmware (a short pulse on each dial-pulse edge), or a sample taken from a
    recording of real dialing?
33. Clip text for the setting, if it gets its own mode: a label ("Dial clicks") and the three option names.

Interruptions:

34. What personality means: one operator per era, or a separate choice (a setting of its own) that works with any era?
35. The remark is still playing when the digit completes and the mode starts to speak (for example the volume
    instructions): cut the remark off, let it finish first, or mix the two?
36. Which clips count as the operator speaking: only the operator list, or every spoken prompt?
37. How many remarks per era and personality, and are they picked at random, in turn, or escalating with repeated
    interruptions?
38. The remark text for each era and personality. Candidates so far:
    - US before 1965, polite switchboard operator: "One moment, please." "I'm still speaking, dear." "Patience,
      caller."
    - US before 1965, harried operator at a busy board: "Hold your horses!" "Say, who's in a hurry?"
    - US Precise Tone Plan, crisp and businesslike: "Please wait for the instructions." "Your call is important."
    - UK GPO: "I beg your pardon." "Do mind your manners, caller." "Hold the line, please."
    - Escalating, one step per repeated interruption: "One moment, please." then "Caller, I *am* trying to help you."
      then a sigh and silence.

Operator help by era and personality:

39. The operator list (dial `0`) in each era's and personality's voice. Each version keeps the six mode lines and adds
    its own opening and closing. Clip count is not a constraint (a 64 GB card and hours of text-to-speech budget).
    Every version should say "ten-digit" in the mode 1 line, so callers do not try 7-digit numbers. Candidates:

    US before 1965, polite switchboard operator (the warm voice of the 1940s–50s Bell booklets):
    - "Operator. Number, please? Or perhaps I can help you."
    - "Dial one and then the ten-digit number, and I'll put your call through."
    - "Dial two to connect your pocket telephone."
    - "Dial three if you'd like to hear me better."
    - "Dial four if your party can't hear you."
    - "Dial five to change the times."
    - "Dial six, and I'll forget your telephone was ever here."
    - "Thank you, caller."

    US before 1965, harried operator at a busy board (fast; other lines are lighting up):
    - "Operator! Make it quick."
    - "One and the ten-digit number to call."
    - "Two, pairing."
    - "Three, louder in your ear."
    - "Four, louder for them."
    - "Five, the era."
    - "Six, forget the phone."
    - "Got all that? Good."

    US Precise Tone Plan, 1965 on, the recorded announcement (electronic switching sent errors to recordings, not a live
    operator; "receiver" and "transmitter" are the period names for earpiece and microphone):
    - "This is a recording."
    - "To place a call, dial one, followed by the ten-digit number."
    - "For pairing, dial two."
    - "To adjust your receiver volume, dial three."
    - "To adjust your transmitter volume, dial four."
    - "To select a different era, dial five."
    - "To remove a paired telephone, dial six."
    - "Thank you for using the Bell System."

    UK GPO, General Post Office operator (formal, a little stiff; "wireless" is the period word for radio):
    - "Operator speaking. Which service do you require?"
    - "To make a call, dial one and then the ten-digit number, and I shall connect you."
    - "Dial two to pair your wireless handset."
    - "Dial three should the line be too quiet."
    - "Dial four should the other party not hear you."
    - "Dial five to change the exchange."
    - "Dial six to have me forget your telephone."
    - "Thank you, caller."

Connection status:

42. Use the cellphone's battery level, signal strength or carrier name in any clip (for example the operator list
    mentioning a low battery)?
