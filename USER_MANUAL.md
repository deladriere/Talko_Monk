# Talko Monk User Manual

**Version 1.0 for Talko 2 (RP2040). Draft for the listening candidate; publication is pending approval.**

Talko Monk turns Talko 2 into a singing formant synthesizer. Choose one of twelve singers, move continuously through five vowel shapes, and play the voice with the panel or a gate. ERROR adds a changing vocal performance while you hold it.

## Play your first vowel

1. Connect Talko's audio output to your mixer or modular system.
2. Put the MODE switch in the middle and select bank **0**, Jerry.
3. Turn **FX** fully counterclockwise. Start with **PITCH** and **SPEED** near noon.
4. Hold **SPEAK**, or keep **GATE IN** high, and turn **SOUND** slowly through its range.

SOUND moves from **oo → oh → ah → eh → ee**, with continuous blends between them. Turn PITCH left for a lower voice or right for a higher voice. The range extends from A0 at the left end through A2 near noon to A3 at the right end.

## Use the encoder and display

| Decimal point | Page | Action |
|---|---|---|
| Off | Singer banks | Turn the encoder to choose a singer. SOUND changes its vowel. |
| Steady | Effects | Tap the encoder to enter FX. Turn it to choose an effect, then adjust the FX knob. Tap again to return to the singer banks. |

The selected effect stays active when you return to the bank page. Singer banks stop at **0** and **b**; turn back to move away from either end. Talko starts in bank 0 with Delay selected.

## MODE switch: gate response

| Position | Behavior |
|---|---|
| Left | Each SPEAK press or incoming gate trigger starts a half-second note, followed by the singer's release. |
| Middle | Hold SPEAK or keep GATE IN high to sustain the note. Release it to start the singer's release. |
| Right | One trigger starts a drone. The next trigger releases it. |

SPEED controls how quickly the vowel follows SOUND during normal playing. Turn it left for a slower glide or right for a quicker response.

## Use the ERROR button

Start a note and hold **ERROR**. Your selected singer moves through vowel chatter, rising and falling phrases, pitch jumps and short stutters. Growl and Shimmer passages bring in rougher and brighter colors. The movements vary as you hold the button and between presses.

| Control while holding ERROR | Action |
|---|---|
| SOUND | Move the vowel around which the performance plays. |
| PITCH | Raise or lower the performance's starting pitch. |
| SPEED | Turn clockwise for quicker changes and shorter stutters. |
| FX | Adjust the effect level, including Growl and Shimmer passages. Fully left keeps those effects dry. |
| Encoder | Choose another singer using the normal bank page. |

Once a note is playing, ERROR keeps it sounding even if you release SPEAK. Release ERROR to return to your current singer, selected effect and panel settings. Normal gate and MODE behavior resumes. If you hold ERROR before playing a note, it waits for SPEAK or GATE.

ERROR's vowel and pitch movements and short stutters work even with FX fully down. Raise FX to hear its Growl and Shimmer passages.

## Singer banks

| Display | Singer | Display | Singer |
|---|---|---|---|
| 0 | Jerry | 6 | EeOo |
| 1 | Guppo | 7 | Snider |
| 2 | Philip | 8 | Fipsy |
| 3 | Suze | 9 | Peppo |
| 4 | Gerald | A | Marley |
| 5 | Bess | b | Razor |

These are the MONK-1 factory characters. Philip uses three unison voices on Talko. Jerry and Marley share the same factory audio parameters.

## Effects

Turn FX fully left before choosing another effect, then turn clockwise to bring it in. All selectable effects have a small dry zone at the left end. Any vibrato built into a singer remains part of that singer's base sound.

| Display | Effect | FX knob |
|---|---|---|
| - | Off | Base singer without an added effect. |
| v | Vibrato | Adds pitch movement at the singer's preset rate. |
| r | Reverb | Raises the diffuse tail and its decay. |
| d | Delay | Adds repeats at the singer's preset echo time. |
| o | Ring modulation | Adds modulation and sweeps its carrier from about 30 to 1,200 Hz. |
| S | Shimmer | Adds an octave-up layer and reverb. |
| P | Phaser | Adds a moving four-stage phase sweep. |
| G | Growl | Blends the voiced throat pulses into noise shaped by the vowel. |
| t | Tape | Fades in near half speed over the first quarter of travel, reaches normal speed near noon, and double speed at the right. Wobble accompanies playback. |

## Read the vowel label over USB serial

Connect Talko using a USB data cable. Open its serial port in PlatformIO Monitor or another serial monitor at **115200 baud**.

| Vowel | Label |
|---|---|
| oo / u | UW |
| oh / o | OH |
| ah / a | AA |
| eh / e | EH |
| ee / i | IY |
| Silence | - |

Each label appears on its own line. It follows the rendered vowel glide and ERROR movement. Intermediate vowel blends use the nearest label, with a small margin around each boundary to avoid flickering. A sustained label is refreshed every 200 ms. Effect tails can blend earlier vowels.

Send **?** to read the firmware version, singer, effect, panel readings and audio timing counters.

## Watch the mouth in Visual Console

1. Open the existing S.A.M.-compatible [Visual Console](https://www.polaxis.be/console/) in desktop Chrome or Edge. Close other serial monitors using Talko's port.
2. Enable **3D Head**, or press **D** and enable **2D Mouth**.
3. In the head or mouth menu, choose **Phoneme model → S.A.M.**
4. Choose **Serial → Connect Port**, select Talko, and play a note.
5. Turn SOUND to move the mouth between vowels. Hold ERROR to follow its live vowel movements. Enable **Text Input → Words** to show the labels alongside the mouth.

Console's S.A.M. setting supplies the mouth-shape mapping. Talko Monk continues using its own singing engine. Sustained vowels hold the mouth open; silence closes it.

## Install or switch firmware

Use **Talko_Monk_Synth-1.0.uf2** for **Talko 2 (RP2040)**. The candidate is supplied locally while its sound is being reviewed. A public download link will be added after approval.

1. Connect Talko to your computer with a USB data cable.
2. Hold **BOOTSEL**, press and release **RESET**, then release BOOTSEL after a second or two.
3. Copy **Talko_Monk_Synth-1.0.uf2** to the **RPI-RP2** drive. Keep USB connected until the copy finishes and Talko restarts.
4. Check **1 · 0** on the front display at startup.

To restore another Talko firmware, repeat these steps with its UF2. For board access and the standard firmware, see the [Talko 2 user manual](https://www.polaxis.be/talko-2-user-manual/).

## Startup and idle display

At startup, the front display shows **1**, the decimal point, then **0**. The rear RGB LED flashes red once for the major version and blue once for the dot. The zero minor version has no green count flashes.

After five minutes without activity, the display dims and a little mouth opens and closes in a slow breath. Turn a knob or the encoder, press a button, move the MODE switch or send a gate to wake it. A held note, drone or held ERROR button keeps the display awake.

## Source and attribution

The engine comes from [FM-1 MONK-1](https://github.com/charlesvestal/fm1-monk), Charles Vestal's embedded port of [MonkSynth](https://github.com/JonET/monksynth) by Jonathan Taylor. The Talko adaptation adds panel controls, the twelve singer banks, effects, ERROR performance, USB vowel labels and PWM audio for the RP2040.

The source is supplied with this firmware under GPL-3.0-only, with the upstream MIT notice retained. See [LICENSE](LICENSE) and [MonkDSP provenance](lib/MonkDSP/UPSTREAM.md).
