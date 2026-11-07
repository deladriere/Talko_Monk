# Talko 2 Monk Synth 0.9

A singing formant synthesizer for the Talko 2 RP2040. The encoder selects
one of the 12 MONK‑1 factory singers. SOUND sweeps the vowel live as the
ALGORITHM control does on the FM‑1.

At startup, the front display shows `0`, the dot, then `9`. The rear
LED flashes blue once for the dot and green nine times for the minor
version. Red flashes count the major version when it is above zero.

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
| ERROR | Hold to loop and bend a slice of the current audio. Release to return to the live voice. |
| SPEED | Turn clockwise for faster vowel changes. While holding ERROR, turn clockwise to go from a slow warble to a fast stutter. |

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

Philip uses three unison voices in this prototype. Jerry and Marley share
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

## ERROR tape freeze

Hold ERROR to capture the voice or effect tail. The slice keeps looping
until release, with a changing tape speed and occasional reversed passes.
If you hold ERROR before playing a note, the capture waits for sound.

While holding ERROR, turn SPEED from a slow warble to a fast stutter,
PITCH to bend the loop, and SOUND to scrub through the captured audio.
A little live voice stays audible underneath. Release for a short
crossfade back to the live voice.

## Sleeping Monk

After five minutes without activity, the display dims and shows a
mouth opening and closing in a slow breath. Turn a knob or the encoder,
press a button, move the mode switch, or send a gate to bring back the
current bank or FX page. A held note, drone, or held ERROR button keeps the
playing display visible.

## Install and build

For a BOOTSEL install, copy [Talko_Monk_Synth-0.9.uf2](Talko_Monk_Synth-0.9.uf2)
onto the `RPI-RP2` drive. The module restarts automatically.

Open this folder in PlatformIO and run `pio run` to rebuild. The generated
file is `.pio/build/pico/firmware.uf2`. For USB serial upload, close the
serial monitor and run `pio run -t upload --upload-port <Talko serial port>`.

At 115200 baud, the USB console prints the bank when it changes and audio
timing after a gate release. Send `?` for the current bank, FX page, mode, panel readings (0–4095), and encoder and audio counters.
`budget_us` is the time available to render 128 frames at 22.05 kHz.
`peak_us`, `late_blocks`, and `underrun_samples` are cumulative since boot.

The RP2040 build and desktop DSP checks cover all 12 presets, vowel
sweeps, pitch changes, echo and release. The output uses the Talko mono
PWM circuit on GP1. The 0.2 timing test held notes and swept SOUND
and PITCH across all 12 banks at full FX. Both the open and closed USB serial
runs reported zero underruns. The longest block took 2,933 µs of the available
5,805 µs. Core 1 renders the audio. Core 0 services the panel and USB, with
PWM updates at higher interrupt priority than USB.

The 0.3 hardware stress test swept SOUND, PITCH and SPEED on Philip's
three-voice bank with each effect at full FX and with ERROR freeze
engaged and released. The longest block took 3,629 µs with no late
blocks or underruns.

The 0.3.4 hardware stress test swept Shimmer and Growl at full FX on
Philip's three-voice bank. The longest block took 3,605 µs of the
available 5,805 µs. Both the open and closed USB serial runs had zero
late blocks and zero underruns.

Source and license details are in [MonkDSP provenance](lib/MonkDSP/UPSTREAM.md).
