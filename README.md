# Talko 2 Monk Synth 1.0

A singing formant synthesizer for the Talko 2 RP2040. The encoder selects
one of the 12 MONK‑1 factory singers. SOUND sweeps the vowel live as the
ALGORITHM control does on the FM‑1.

At startup, the front display shows `1`, the dot, then `0`. The rear
LED flashes red once for the major version and blue once for the dot.
The zero minor version has no green count flashes.

## Play

Start with bank **0** (Jerry), **PITCH** at noon, and **FX** fully
counterclockwise. Hold **SPEAK** and turn **SOUND** through its range.

| Control | Action |
|---|---|
| Encoder | Turn to choose a singer. Press to switch between the singer and FX pages. |
| SOUND knob or CV | Sweep continuously through the five vowels. |
| SPEAK button or GATE input | Start a note using the selected switch mode. |
| PITCH | Tune from A0 at the left end through A2 at noon to A3 at the right end. |
| FX | Turn fully left for dry sound, then turn clockwise to bring in the selected effect. |
| ERROR | Hold for a varied live vocal performance on the selected singer. Release to return to the panel settings. |
| SPEED | Turn clockwise for faster vowel changes. While holding ERROR, turn clockwise for faster gestures and shorter stutters. |

| Bank | Singer | Bank | Singer |
|---|---|---|---|
| `0` | Jerry | `6` | EeOo |
| `1` | Guppo | `7` | Snider |
| `2` | Philip | `8` | Fipsy |
| `3` | Suze | `9` | Peppo |
| `4` | Gerald | `A` | Marley |
| `5` | Bess | `b` | Razor |

The encoder stops at bank 0 and bank b. Turn back to select another bank.

| Switch position | SPEAK and GATE behavior |
|---|---|
| Left | One-shot. Each trigger starts a half-second note followed by the singer's release. |
| Middle | Gate. The note lasts as long as the button or input is held. |
| Right | Drone. One trigger starts the note and the next releases it. |

Philip uses three unison voices. Jerry and Marley share
the same factory audio parameters.

## FX page

Press the encoder. The dot stays on in FX mode and goes off on the singer page. Turn to choose an
effect, then use the FX knob to adjust it. Press again to return to the
singer banks. The effect stays active across banks. The module starts
in bank 0 with Delay selected.

| Glyph | Effect | FX knob |
|---|---|---|
| `-` | Off | Uses the singer's base voice. |
| `v` | Vibrato | Adds pitch movement at the singer's preset rate. |
| `r` | Reverb | Brings up a diffuse tail and increases decay. |
| `d` | Delay | Adds repeats at the singer's preset echo time. |
| `o` | Ring mod | Increases modulation depth and sweeps the carrier from 30 to 1,200 Hz. |
| `S` | Shimmer | Raises an octave-up layer over the voice and sends it through the reverb tail. |
| `P` | Phaser | Increases the depth of a four-stage sweep. |
| `G` | Growl | Blends the regular throat pulses into noise shaped by the selected vowel. |
| `t` | Tape | Fades in at half speed over the first quarter of travel. Playback reaches normal speed near noon and double speed at the right, with tape wobble throughout. |

Every selectable effect is dry in the small zone at the FX knob’s
counterclockwise end. Turn fully left to switch effects during a note,
then turn clockwise to bring in the new effect. A singer’s factory
vibrato is part of its base sound. Shimmer uses octave-up feedback in a
reverb loop. [Shimmer design background](https://valhalladsp.com/2010/05/11/enolanois-shimmer-sound-how-it-is-made/)

## ERROR performance

Play a note, then hold ERROR. The selected singer moves through vowel
chatter, rising and falling pitch phrases, octave jumps and brief stutters,
with Growl and Shimmer passages. The gestures vary while held and between
presses. They follow the current singer and knob positions.

SPEED sets the gesture pace. SOUND and PITCH remain live. FX controls the
effect level, including the Growl and Shimmer passages; fully left keeps
those effects dry. ERROR's vowel, pitch and stutter movements still work.

Once a note is playing, ERROR keeps it sounding until release. If pressed
before a note, ERROR waits for SPEAK or GATE. Release restores the current
bank, effect and panel settings, with normal gate and mode-switch behavior.

## Vowel labels and Visual Console

USB serial at 115200 baud prints one vowel code per line:

| SOUND position | Vowel | Console code |
|---|---|---|
| Far left | oo / u | `UW` |
| One quarter | oh / o | `OH` |
| Centre | ah / a | `AA` |
| Three quarters | eh / e | `EH` |
| Far right | ee / i | `IY` |
| Silence | Closed mouth | `-` |

The label follows the rendered vowel glide and ERROR movement, with a small
hysteresis around boundaries. Intermediate vowels use their nearest label.
Effect tails can contain a blend of earlier vowels. Changes are limited to
40 updates per second; a steady vowel is refreshed every 200 ms.

Open the existing Kraftor Visual Console, enable **3D Head** or **2D Mouth**,
select **Phoneme model → S.A.M.**, and choose **Serial → Connect Port**.
Select Talko's USB port. Close other serial monitors first. **Text Input →
Words** displays the vowel labels alongside the mouth.

Console requests its existing framed S.A.M. stream (`RS SAM US`); Monk sends
`RS SAM:<code> US` events alongside the plain labels. A sustained vowel holds
the mouth open; `-` closes it when the audio becomes quiet. No Console edit
is required.

## Sleeping Monk

After five minutes without activity, the display dims and shows a
mouth opening and closing in a slow breath. Turn a knob or the encoder,
press a button, move the mode switch, or send a gate to bring back the
current bank or FX page. A held note, drone, or held ERROR button keeps the
playing display visible.

## Install and build

For a BOOTSEL install, copy [Talko_Monk_Synth-1.0.uf2](Talko_Monk_Synth-1.0.uf2)
onto the `RPI-RP2` drive. The module restarts automatically.

Open this folder in PlatformIO and run `pio run` to rebuild. The generated
file is `.pio/build/pico/firmware.uf2`. For USB serial upload, close the
serial monitor and run `pio run -t upload --upload-port <Talko serial port>`.

At 115200 baud, the USB console prints the bank when it changes and audio
timing after a gate release. Send `?` for the current bank, FX page, mode, panel readings (0–4095), and encoder and audio counters.
`budget_us` is the time available to render 128 frames at 22.05 kHz.
`peak_us`, `late_blocks`, and `underrun_samples` are cumulative since boot.

The native audio check runs the actual firmware control and rendering code,
with Pico hardware calls stubbed. Run `sh tests/run_host.sh` for all twelve
banks, effects, held ERROR, live control changes, arming and release checks
under AddressSanitizer and UndefinedBehaviorSanitizer. To save an eleven-second
audio example, run `sh tests/run_host.sh /private/tmp/monk-error.wav`.

The 1.0 hardware stress test swept SOUND, PITCH, SPEED and effects on
Philip's three-voice bank, with held ERROR and the vowel stream active.
USB serial open and closed both reported zero late blocks and zero
underruns. Peak rendering took 3,180 µs of the available 5,805 µs.
The serial check covered all five vowel labels, silence and a request
split across USB reads. Listening approval of ERROR is still pending.

Build toolchain: PlatformIO Raspberry Pi platform 1.17.0, installed commit
`4d1ad09d78445f16688627de0819646fd655910e`.

See [the user manual](USER_MANUAL.md) for the complete panel guide and
[release notes](CHANGELOG.md) for the 1.0 publication status.

Source and license details are in [MonkDSP provenance](lib/MonkDSP/UPSTREAM.md).
