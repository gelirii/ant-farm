# Saved worlds and power behaviour

The portable core serializes a complete world snapshot. Loading a snapshot does
not reconstruct an approximate colony: ant identities, tasks, crops, carried soil,
food, brood, corpses, plants, seed bank, terrain, pheromones, moisture, nutrients,
statistics, current weather sample, simulation clock, pending ecology tick times,
and random generator state are all restored. Configuration and deliberate pause
state are included. Scheduler buckets and occupancy caches are rebuilt.

The loaded weather tape must have exactly the same fingerprint as the tape used
when saving. The fingerprint covers the tape and its provenance/configuration.
Weather data is not silently substituted when restoring. This is essential for
reproducing a future run: identical state with different future rain is a different
experiment. Load the intended weather CSV before calling `World::load`.

## Format version 2

Version 2 includes each ant's physically established nest orientation. Development
snapshots from version 1 are rejected rather than silently changing future behaviour.

All integer fields use explicit little-endian encoding. Floating-point fields are
IEEE-754 binary32 with explicit byte order. No C++ object padding, pointers, or
native container layouts are written. The 44-byte header contains:

| Byte offset | Field |
| --- | --- |
| 0 | Eight bytes `ANTFARM` followed by NUL |
| 8 | Format version, uint32 |
| 12 | Grid width, uint32 |
| 16 | Grid height, uint32 |
| 20 | Surface row, uint32 |
| 24 | Weather fingerprint, uint64 |
| 32 | Payload byte length, uint64 |
| 40 | IEEE CRC-32 of payload, uint32 |

The payload encodes each member individually; variable collections have uint32
element counts. The decoder checks file size, checksum, enum values, collection
sizes, finite floating-point values, coordinates, scheduling bounds, and world
invariants. Invalid input, incompatible versions and mismatching weather produce
an explanatory error. A failed load leaves the existing world unchanged.

`state_hash()` computes FNV-1a over the weather fingerprint and the same canonical
payload. It detects reproducibility regressions without hashing compiler padding
or transient caches. It is a comparison aid, not a cryptographic authenticity
check. Reproducibility is tested within the same build; identical floating-point
results across different compilers/architectures are not assumed.

## Writes and recovery

The host writer stages a sibling temporary file, checks write and close results,
and renames it into place only after completion. On POSIX filesystems the rename
is atomic. Concurrent threads receive distinct temporary filenames; callers must
still synchronize access to a world while it is being advanced or saved.

An atomic rename is not a power-loss durability guarantee: the portable C++ writer
does not perform platform-specific file and directory `fsync`. Embedded storage
integration can use the [portable checkpoint controller](checkpoint.md), which
implements two alternating snapshots with checksummed RTC metadata and fallback
selection. The ESP32 filesystem, SD card and power-cut behavior must be tested on the
selected hardware before claiming physical-device outage reliability. A save is
not authenticated; do not load untrusted snapshots as a security feature.

## Clock contract

- Deliberate OFF persists the paused world. Time while deliberately paused must
  not be passed through ecological catch-up on restart.
- Unplugging a running world requires the platform to retain a wall-clock
  timestamp and use a battery-backed clock to calculate elapsed outage time.
  `catch_up` advances the same full-detail simulation rules used by headless tests.
- Extinction does not summon replacement ants. A deliberate OFF/ON transition
  starts a new founding colony with fresh soil, preserves the current day and
  plants, and has the founders enter from the display edge.

RTC access, button debouncing, brightness, filesystem flushing and selecting the
newest valid snapshot are platform responsibilities; the current portable core
does not imply those hardware integrations have already been validated.

The substantive core tests verify arbitrary-duration chunk equivalence, save/load
continuation, mutation-free corrupt-load rejection, paused restoration, matching
full-detail catch-up, mineral conservation, physical foraging/excavation progress,
and the deliberate extinction reset. Long-term ecological calibration is a
separate experiment; passing persistence tests is not evidence of rare extinction.
