# Validation — 8 September 2026

**All 12 completed ecology trials survived their requested horizon: eleven runs
of two years and one run of ten years, totalling 32 simulated years.** Every
scheduled ant action used the production C++ core. Rendering was disabled for
these runs; biological time steps, movement, food transport and digging were
unchanged. No trial invoked recolonization, manual feeding or emergency resources.

[Machine-readable results](validation.json) · [Daily trajectories and manifests](trajectories/)

## Founding population and weather

| Founders, including queen | Weather window | Trials × duration | Survived | Workers at end |
|---|---|---|---|---|
| 10 | Newcastle 2023 | 3 × 730 days | 3/3 | 25–30 |
| 20 | Newcastle 2023 | 3 × 730 days | 3/3 | 16–28 |
| 30 | Newcastle 2023 | 3 × 730 days | 3/3 | 18–32 |
| 30 | Newcastle 2019 | 1 × 730 days | 1/1 | 45 |
| 30 | Newcastle 2021 | 1 × 730 days | 1/1 | 41 |
| 30 | Newcastle 2023 | 1 × 3,650 days | 1/1 | 10 |

Founding comparisons use seeds 2000–2002; alternative-weather checks use seed
3000; the decade trial uses seed 42. All groups use baseline nectar and rain,
with prey scale 0.25: a mean arrival interval of 72 hours and 300–899 protein units
per event. Resource units are model units, not calibrated insect mass.

Ten is the smallest founding population tested, and is a promising candidate.
**Rare extinction and the smallest reliable founding size have not been
established.** Three successes per founding size still give a two-sided Wilson
95% upper bound of about 56% on its failure rate. The different weather and
founding groups should not be pooled into a universal risk claim. As an example,
299 independent failure-free trials at one fixed setting would be needed for a
one-sided 95% upper bound below 1%; that would still describe this model and the
specified time horizon. The default remains the requested 30 founders pending
that broader calibration.

The 2023 window remains the default because it has the mildest temperature range
of the three and a useful dry spell. The wetter 2019 and drier 2021 comparison
runs also survived. These checks support the chosen spring/summer loop; they do
not prove that this location/window is uniquely best. Each tape contains 123 days
of ERA5 historical reanalysis, with a five-day overlap producing a 118-day loop.
See [weather provenance and reproduction](../docs/weather.md).

## Ten-year trajectory

The queen remained alive; the colony produced **460 new workers**. The worker
population ranged from **7 to 58**, ending at 10 with 27 brood items. There were
15 living plants at the end, and the daily plant count never fell below six.
Mineral soil remained exactly **322,288 units** in every sampled state. The ants
excavated and transported 8,608 units of it.

The run executed **871,655,906 ant actions** and took 907.024 seconds of host wall
time while other work was running. This is not a device benchmark. All eleven
shorter trials also preserved mineral soil exactly. Adult founders have much
shorter programmed lives than ten years, so this result includes repeated worker
replacement rather than merely keeping the initial ants alive.

## Correctness and power recovery

- Ten core regression groups pass: founding/conservation, deterministic seeds,
  arbitrary time chunks, complete saves and continuation, checkpoint boundaries,
  persisted cargo/brood/decomposition, transactional corruption rejection,
  deliberate pause/full-detail catch-up, extinction OFF/ON reset and spoil support.
- Three weather tapes pass deterministic playback, finite/range checks and smooth
  seam checks. Offline raw-data-to-CSV reproduction succeeds.
- Twenty-seven cached-render comparisons match a fresh full renderer pixel for
  pixel, including changed soil, plants, removed objects and buffer resizing.
- Four controller test groups pass: two-slot rotation, corrupt/torn-slot fallback,
  failed replacement, backwards RTC, ignored OFF time and explicit extinction
  resume. Snapshot, RTC timestamp and sequence share one checksummed envelope.
- Core and controller address/undefined-behaviour sanitizer runs pass. LeakSanitizer
  could not run under the host's tracing environment, so leak checking is not claimed.

The supplied controller implements portable recovery logic. Physical RTC readings,
filesystem durability barriers and real power-cut testing still belong to the
selected board integration. See [checkpoint API](../docs/checkpoint.md).

## Measured optimization

Repeated terrain-access checks accounted for about 39% of sampled CPU time in the
initial profile. A locally updated walkability cache removes the repeated geometry
work while retaining the original query as a reference implementation.

Across three paired thirty-day trials, the median host CPU reduction was
**38.3%**. Each pair produced the same complete persistent-state hash.
An additional seven-day continuation from the ten-year saved world also matched
between the reference and cached versions. The cache costs approximately 57.6 KB.

[Paired measurements](optimization.json) · [Aged-world equivalence](aged-equivalence.json)

Reproduce with:

```sh
make -j2 build/antfarm-reference build/antfarm
python3 tools/benchmark.py build/antfarm-reference build/antfarm
```

The reference build retains cache maintenance for a controlled comparison and
recomputes access when queried. Both builds use the same biological rules. This
benchmark does not establish identical floating-point output across all compilers
or architectures.

The dense rendering fixture contains 512 workers, 1,024 brood items, 128 corpses
and 128 food sites. Across 300 warm 1080×1920 frames it measured **1.99 ms
mean and 2.97 ms at the 95th percentile** on the host. It is an
artificial rendering load, not an ecological run or proven worst-case device load.
[Full fixture measurements](render-stress.json) and its
[source](../tools/render_benchmark.cpp) are included.

Approximate persistent host allocation: 1.25 MB core at the decade snapshot,
4.26 MB renderer cache, 4.15 MB output framebuffer and 0.024 MB packed weather.
This excludes extra LCD DMA buffers, OS stacks, allocator overhead, filesystem
buffers and temporary recovery copies. The working device budget is 32 MB PSRAM.
No P4 frame rate, power consumption or absolute performance optimum is claimed.

## Limits and next checks

Earlier developmental runs exposed inaccessible spoil towers, queen drift,
stranded soil carriers, ineffective larval feeding, population overshoot and
failed plant regeneration. Those failures informed local-rule fixes and the
resource calibration; their old success/failure counts are not pooled with the
current model's results.

The ecology remains stylized. Nutrient bookkeeping is simplified and is not a
closed biochemical mass balance; the tested nest tends towards a shallow horizontal gallery and one deeper queen
chamber. It does not yet reproduce the branching nest structure in the references;
that remains a concrete behaviour/visual issue for the next iteration. The ESP-IDF
component has not been cross-compiled here, and panel-specific firmware, RTC,
storage, brightness buttons, sustained DMA scanout and hardware performance still
require the chosen board and display. Those are explicit unfinished device work.
