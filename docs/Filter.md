# Filter

[![Filter](screen_filter.png)](screen_filter.png)

An FFT-based spectral filter. Audio in is transformed into the frequency domain, a per-bin gain curve is applied, and the result is transformed back. The gain curve is built from one of seven filter types plus smoothing (Q). A display shows the input and output spectra (EMA-averaged per bin) plus the active gain curve.

## Inputs / Outputs

| Jack | Name | Description                |
|------|------|----------------------------|
| IN   | In   | Audio input (mono).        |
| X    | X CV | CV offset for X (0..1).    |
| Y    | Y CV | CV offset for Y (0..1).    |
| W    | W CV | CV offset for W (0..1).    |
| Q    | Q CV | CV offset for Q (0..1).    |
| V    | V CV | CV offset for V (0..1).    |
| GAIN | Gain CV | CV offset for GAIN (0..1). |
| OUT  | Out  | Filtered audio output.     |

A connected CV input adds to the knob value: +/-10 V sweeps the full 0..1 range
(the sum is clamped to 0..1).

## Params

| Knob  | Range    | Default | Description                                                                        |
|-------|----------|---------|------------------------------------------------------------------------------------|
| FILTER| 0..6     | 0       | Filter type (snapped): 0 Bypass, 1 Comb, 2 Distortion, 3 High Pass, 4 Low Pass, 5 Band Pass, 6 Rev Band Pass. |
| X     | 0..1     | 0.5     | Filter-specific main parameter (spacing / cutoff / band center).                      |
| Y     | 0..1     | 0.464   | Output level (cubic): 0 = true mute (exact zero, not a dB floor), 1 = 10x boost; 0.464 ≈ unity. |
| W     | 0..1     | 0.5     | Filter-specific secondary parameter (tooth size / band width).                         |
| Q     | 0..1     | 0       | Edge smoothing (skirt) width; window is ±Q x 16 bins. Not applied to Distortion.    |
| SEED  | 0..65535 | 1       | Random seed for the Distortion type (snapped, not randomized).                      |
| GAIN  | 0..1     | 0.5     | Input gain multiplier.                                                             |
| V     | 0..1     | 1       | Clip: maximum absolute gain per bin (cubic like Y). 1 = 10x (no clip), 0 = mute all. |
| MIX   | 0..1     | 1       | Wet/dry crossfade: 1 = wet (filtered) only, 0.5 = equal mix, 0 = raw input.         |
| SCALE | 0..1.5   | 1       | Vertical scale of the spectrum in the display.                                      |

## DSP path

- Audio is accumulated into a 256-sample bulk buffer; every 256 samples a block is processed.
- The input is windowed with a periodic Hann window (FFT size 1024, 4x overlap),
  transformed with a real FFT (pffft).
- The packed real spectrum keeps bins 0..N/2 (DC and Nyquist stored as reals, the rest as
  re/im pairs); the mirrored upper half is dropped.
- A per-bin `gain_buffer[]` is built by the selected filter type, smoothed by Q,
  clamped per-bin to the V clip level (`[-clip, +clip]`), then multiplied into the
  spectrum, followed by an inverse FFT (scaled by 0.5/N) and weighted overlap-add
  to reconstruct the output (4x overlap sums to a constant 2).
- The FFT path always runs (filter type 0 = flat gain 1 is just a bypass inside
  the FFT domain). Use MIX = 0 for a raw, low-CPU dry path.
- MIX crossfades the dry (gain-scaled input) and wet (filtered) signals at the output:
  `out = dry + (wet - dry) * MIX`. MIX 1 = wet only, 0.5 = equal mix, 0 = raw input.

## Filter types

| # | Name        | Behavior                                                       |
|---|-------------|----------------------------------------------------------------|
| 0 | Bypass      | Flat gain of 1, no filtering.                                  |
| 1 | Comb        | X sets tooth spacing (up to 64 bins); Y the tooth amplitude; W adds a tooth-size non-linearity. |
| 2 | Distortion  | SEED randomizes which harmonics survive; Q is the density (fraction kept); Y the gain. |
| 3 | High Pass   | X sets the cutoff bin (up to N/2); passes bins above it with gain Y. |
| 4 | Low Pass    | X sets the cutoff bin; passes bins below it with gain Y.        |
| 5 | Band Pass   | X sets the band center, W the band width (half on each side); passes the band with gain Y. |
| 6 | Rev Band Pass| Inverse of band pass: X sets the notch center, W the notch width; attenuates the notch, passes the rest. |

- Q > 0 applies a moving-average smoothing window to the gain curve (except Distortion),
  softening the edges into skirts.
- Y is a 0..1 level knob mapped non-linearly to a linear spectral gain
  (`knobToGain(v) = v^3 * 10`): 0 mutes the spectrum to exactly zero, 1 boosts 10x.

## Display

The display always shows input and output simultaneously:
- Red curve: input spectrum (magnitude, real+imag combined), EMA-averaged per bin
  across frames (α = 0.2) so the noise floor looks like a real analyzer instead of
  a random comb; SCALE scales it vertically.
- Green curve: output (filtered, wet) spectrum, same averaging/scaling.
- Yellow curve: where input and output coincide (within ~2 px), drawn on top of both.
- Cyan curve: the active filter's gain curve (form), drawn through the non-linear
  knob law (`knobToY`), so muted bins (gain 0) sit on the exact bottom.
- Magenta line: the V clip ceiling (a background guide line at the clip level in dB).
- The dB axis spans -60 dB (bottom) to +20 dB (top); 0 dB sits at 75% height.

## Known issues / TODO

See `TODO.md` (module name `Filter`):
- The display reads module state (FFT buffers) directly on the UI thread (module/UI race).
- Distortion only applies gain to bins 1..N/2-1 (DC and Nyquist are left untouched).
