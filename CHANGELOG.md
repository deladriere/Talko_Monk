# Release notes

## 1.0 candidate

Publication is waiting for the user's listening approval of ERROR. Work stays on `main`.

- Twelve MONK-1 singers, continuous vowel control, low pitch range, and three gate modes.
- Held ERROR varies the live selected singer with vowel chatter, pitch phrases, short ratchets, Growl and Shimmer passages. It restores the panel controls on release.
- Nine selectable effects, all dry with FX fully counterclockwise.
- Vibrato's FX depth extends to about ±4 semitones; SPEED retains vowel glide and factory vibrato is preserved at zero FX.
- Rendered-vowel labels over USB serial and compatibility with Visual Console's existing S.A.M. mouth stream.
- Dedicated Talko Monk model in Console's 2D Mouth and 3D Head menus.
- Normal serial output carries only vowel labels and requested mouth events; diagnostic reports require an explicit `?`.
- Front display and rear LED announce 1.0; Sleeping Monk appears after five minutes idle.
- User manual prepared to follow the published Talko S.A.M. manual's organization.

Validation: native control and audio checks passed with ASan and UBSan.
The Talko hardware stress test reported 3,646 µs peak against a 5,805 µs
block deadline, with zero late blocks and underruns with USB open and closed.
All five vowel labels, silence and the Console stream request passed.

The recovered 0.9 source and earlier UF2s are preserved in local Git history at `v0.9`. Only the 1.0 UF2 is kept in the current release folder.
