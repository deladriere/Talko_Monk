# MonkDSP provenance

The audio engine comes from [FM‑1 MONK‑1](https://github.com/charlesvestal/fm1-monk), Charles Vestal's embedded port of [MonkSynth](https://github.com/JonET/monksynth) by Jonathan Taylor. The FM‑1 port marks the DSP MIT AND GPL‑3.0‑only and its math header GPL‑3.0‑only. The GPL text is at the root of this project. The original MIT notice is included here.

| Upstream file | Git blob SHA |
|---|---|
| [`monk.c`](https://github.com/charlesvestal/fm1-monk/blob/main/firmware/src/dsp/monk.c) | `4f9a1ff09f44f6e6b522fda01750ecf9609f1733` |
| [`monk.h`](https://github.com/charlesvestal/fm1-monk/blob/main/firmware/src/dsp/monk.h) | `571bdfaf382b571f5127cf54d1eb3ab4f85ceeac` |
| [`fastmath.h`](https://github.com/charlesvestal/fm1-monk/blob/main/firmware/src/dsp/fastmath.h) | `7d2e41bf70590bafa8af0a7376bb912bf763b098` |
| [Factory character parameters](https://github.com/charlesvestal/fm1-monk/blob/main/assets/characters/characters.json) | `0059f04131b8224ff846fe728892dad08aef4547` |

This Talko prototype adapts the engine to 22.05 kHz and 128-frame blocks. It scales the grain, echo timing and buffer sizes to suit the lower rate. It also reduces the maximum unison count to three and uses ordinary zero-initialized RP2040 storage. The Talko panel and PWM wrapper run independently of the FM‑1 platform.

Version 0.2 runs the grain, overlap, vibrato, envelope, delay and output
loops in fixed point. Initialization and block-level control math still
use floats. The pitch-period and soft-clip lookup tables keep divisions
and the clipper polynomial out of the sample loop.
