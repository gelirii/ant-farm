# Validation — development snapshot, 8 September 2026

The current run is still collecting founding-size, weather and decade-long
results. This report will be replaced with the completed batch. No claim of rare
extinction or verified ESP32-P4 performance is made by this snapshot.

Verified so far:

- Ten core regression tests pass, including mineral conservation, deterministic
  continuation, corruption rejection, pause/outage semantics and extinction reset.
- All three historical weather tapes pass range, deterministic playback and seam
  checks; the raw-to-CSV offline reproduction succeeds.
- Twenty-seven cached-render comparisons match fresh full rendering pixel for pixel.
- An earlier four-run two-year screen survived in all four cases. That model
  preceded the final brood-feeding adjustment, so its survival statistics are
  developmental evidence only.
- The current model's first three two-year trials, each starting with ten ants,
  survived and ended with 25–30 workers after 82–97 births. Other trials are pending.
- A measured terrain-access cache reduced host CPU time by 38.5% in three paired
  thirty-day runs with identical world hashes. A current-model repeat is pending.

See the executable scripts in `tools/` for reproducible experiments. Hardware
integration, closed nutrient mass balance and statistical evidence that a founding
size has rare extinction remain outstanding.
