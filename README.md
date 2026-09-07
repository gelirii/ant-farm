# Ant farm

A quiet, autonomous ant colony in a portrait cross-section of soil. The portable
C++ core runs the same individual ant behaviours in real time and in accelerated
headless experiments. The target is a roughly A4, 1080×1920 ESP32-P4 display.

**Development prototype.** The desktop simulation, renderer, historical weather,
save/restore and test tools work. Ecological calibration is still being evaluated;
finite survival trials cannot establish an extinction guarantee. Panel-specific
firmware, physical RTC/power handling and P4 performance have not been validated.
See [validation results](reports/validation.md) and the
[hardware integration boundary](platform/esp32p4/README.md).

## Watch it

On a machine with a C++17 compiler, Make, Python 3 and Pillow:

```sh
make
python3 tools/viewer.py
```

Open the local address printed by the viewer. The first 30 ants enter from the
left and excavate fresh soil. The preview saves its world under `out/preview` and
resumes it on the next launch. Close the preview with Ctrl+C. The normal display
runs at **one real second per simulated second**, with no gameplay controls.

Pillow is only needed for the desktop preview. To install it in an isolated Python
environment if necessary:

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow
.venv/bin/python tools/viewer.py
```

For a separate preview that begins at a later age, use a new state directory:

```sh
python3 tools/viewer.py --days 60 --state-dir out/older-preview
```

Initial ageing executes the full simulation and can take time. It does not
change the real-time rate after the preview starts.

## What is simulated

- One queen and individual workers with local movement, nest orientation,
  pheromones, foraging, crop sharing, soil transport, nursing, rest and corpse removal.
- Eggs, larvae and pupae. Food must reach a worker or brood item physically;
  there is no colony-wide food inventory that feeds ants at a distance.
- Excavated mineral soil stays in the world, including material being carried.
  Loose spoil needs support; wide unsupported underground spans can collapse.
- Plants grow, seed, die and decompose. Sugar production depends on light and
  root moisture. Small prey carcasses and windborne seeds are explicit inputs
  from the surrounding meadow; their rates do not increase to rescue a colony.
- Actual Newcastle May–August weather from ERA5 reanalysis for 2019, 2021 and
  2023. A five-day crossfade produces a 118-day repeating window with restrained
  variation, without winter dormancy. Source data and attribution are included.
- Complete saved worlds, deterministic continuation and exact full-detail
  catch-up. Deliberate OFF pauses time. Only OFF then ON after complete extinction
  starts new founders in fresh underground soil, preserving day and plants.

This is a stylized ecology inspired by garden ants, not a validated biological
forecast. Nutrient bookkeeping is simplified; it is not a closed biochemical mass
balance. The grid and local instincts constrain nest shapes. Long-term population
stability and the smallest reliable founding size remain empirical questions.

## Run experiments

Run commands from the repository root. The default weather is the 2023 tape;
missing weather is an error. Synthetic diagnostic weather requires the explicit
`--weather synthetic` option.

```sh
./build/antfarm run --days 730 --seed 42 --founders 30 \
  --csv out/two-years.csv --save out/two-years.save
./build/antfarm render --load out/two-years.save --output out/two-years.ppm
python3 tools/sweep.py --days 730 --seeds 8 --founders 20 30 --jobs 3
```

`run` executes every scheduled action as fast as the host can manage, without
rendering. `sweep.py` freezes its executable, records its SHA-256, preserves daily
trajectories and reports survival with uncertainty. Trials stop on queen loss.
A result from an older experimental binary is not evidence for a later model.

The initial calibrated prey setting is `--insects 0.25`, corresponding to an
average event every 72 simulated hours; event size is 300–899 protein units.
These are model units, not measured milligrams. `--nectar 1` and `--rain 1` are the
baseline plant production and weather multipliers. See the report for the tested
range and current limits of the recommendation.

Restore with the same weather tape used by the saved world. Render and power-cycle
commands advance zero days unless `--days` is explicitly supplied. `power-cycle`
models the deliberate OFF/ON lifecycle; it is not a replacement for device RTC
and storage integration.

## Check and measure

```sh
make -j2 test test-render
make sanitize
python3 tools/fetch_weather.py --offline
```

Tests cover conservation, reproducibility, arbitrary time chunks, saved cargo and
brood, corrupted-save rejection, pause/outage semantics, extinction reset, spoil
support, weather continuity and cached-versus-fresh pixels. Full ecology trials
are reported separately. LeakSanitizer cannot run under some traced/sandboxed
hosts; `ASAN_OPTIONS=detect_leaks=0 ./build/test_sanitize` still checks address and
undefined behaviour but does not check leaks.

The renderer emits actual RGB565 pixels at the target resolution. It reuses a
terrain cache and redraws affected tiles; interpolation changes appearance only.
Host CPU and memory measurements are in the report. They are not P4 frame-rate
measurements, and no claim of an absolute optimum is made without device profiling.

The original private planning conversation and local saves are excluded from Git.
