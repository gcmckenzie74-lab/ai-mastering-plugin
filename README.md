# AI Master

An AI-assisted mastering plugin (VST3, Windows) inspired by LANDR, built with JUCE.
Designed for Cubase 11 LE: put it on the **Stereo Out** insert.

## Stage 1 (this version)

**Analyse → Auto gain → Lookahead limiter**

- **Loudness meter**: ITU-R BS.1770 / EBU R128 momentary, short-term and gated integrated LUFS
  (checked against the EBU reference: a −23 dBFS 997 Hz stereo sine reads −23.0 LUFS).
- **Analyse**: press *Analyse*, play the loudest 20–30 s of the song, press *Stop & Apply*.
  The plugin sets the gain so the master hits your **Target LUFS**. Running Analyse a
  second time refines it, because the second pass measures how much loudness the limiter removes.
- **Lookahead limiter**: 5 ms lookahead brickwall with an adjustable **Ceiling** (dBFS) and **Release**.
  It never overshoots the ceiling. Its latency is reported to Cubase, which compensates automatically.
- **Trim**: manual ±6 dB on top of the auto gain.

Typical targets: **−14 LUFS** (Spotify/YouTube), **−9 to −8 LUFS** (loud club/EDM), ceiling **−1.0 dBFS**.

## Getting the plugin (no compiler needed)

1. Open the repository's **Actions** tab → **Build Windows VST3** → latest successful run.
2. Download the **AI-Master-VST3-Windows** artifact and unzip it.
3. Copy the `AI Master.vst3` folder to `C:\Program Files\Common Files\VST3\`.
4. In Cubase: **Studio → VST Plug-in Manager → Update Plug-in Information** (or restart Cubase).
5. Add **AI Master** as an insert on the Stereo Out.

Builds that are tagged (e.g. `v0.1.0`) are also attached to a GitHub Release as a zip.

## Building locally (optional)

Requires Visual Studio 2022 (Desktop C++ workload), CMake ≥ 3.22 and Git.

```bat
cmake -B build -A x64
cmake --build build --config Release --target AIMaster_VST3
```

JUCE 9.0.6 is downloaded automatically. To use a local JUCE checkout: `-DJUCE_DIR=C:\path\to\JUCE`.

## Roadmap

1. ✅ Loudness analysis, auto gain, lookahead limiter
2. Spectral analysis + EQ matching to genre target curves / a reference track
3. Multiband compression set from per-band crest factor; stereo imaging (mono bass)
4. Genre classifier and ML parameter prediction (trained in Python, run via ONNX)
5. True-peak (oversampled) limiting

## Licence note

JUCE is dual-licensed (AGPLv3 or the commercial JUCE licence). Publishing this source in a public
repository under the AGPLv3 is fine. If you ever want to sell a closed-source version, you'll need a JUCE licence.
