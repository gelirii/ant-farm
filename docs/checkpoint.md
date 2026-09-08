# Two-slot checkpoints and RTC recovery

`CheckpointStore` associates a complete `World` snapshot, wall-clock timestamp,
and monotonically increasing sequence number in one file. It alternates between
`<base>.0.checkpoint` and `<base>.1.checkpoint`, keeping the previous valid slot
while preparing the replacement. The supplied parent directory must exist.

Each envelope starts with `ANTCHKP` plus NUL, version 1, a uint64 sequence, uint64
wall-clock seconds, uint64 snapshot length, and uint32 IEEE CRC-32. All fields use
little-endian byte order. The checksum covers the first 36 header bytes and the
entire nested snapshot, so timestamp and sequence corruption cannot silently be
combined with a valid world. `World::load` additionally verifies the nested save,
its invariants and the loaded weather source.

Restore checks both slots and selects the newest restorable snapshot. Truncated
or corrupt slots and uncommitted staging files do not displace the previous
snapshot. A clock earlier than the selected checkpoint is an error; recovery
does not hide it by falling back to an older timestamp. Restore and catch-up run
on a separate world and replace the caller's world only after success. Recovery
metadata also remains unchanged on failure.

For a running snapshot, elapsed wall-clock seconds are advanced using the same
full-detail simulation. For a paused snapshot, elapsed OFF time is excluded.
`resume_paused=true` explicitly calls `power_on`; after extinction this performs
the requested fresh-soil founding cycle while retaining day and plants. Merely
restoring a paused snapshot does not resume or recolonize it.

```cpp
antfarm::CheckpointStore checkpoints("/storage/farm");
std::string error;
// Hold the platform's world lock while capturing world state and RTC time.
checkpoints.write(world, rtc_seconds, &error);

antfarm::CheckpointRecovery recovered;
checkpoints.restore(world, rtc_seconds, false, &recovered, &error);
// For a deliberate ON event, pass true instead of false.
```

Callers must check every returned boolean and handle its error message. Deliberate
OFF should call `world.power_off()`, write that paused state successfully, and
only then power down. On ON, resume and commit a running checkpoint before normal
operation, so a later outage is not mistaken for a deliberate pause. Capture the
RTC timestamp corresponding to the snapshot while simulation advancement is
locked; all store and world operations require external serialization.

The implementation uses bounded temporary files to reuse the portable World save
format, with automatic cleanup on ordinary failure. Completed envelopes replace
one slot through a sibling-file rename, atomic on POSIX filesystems. This does
not certify SD-card power-loss durability: the hardware adapter still needs
filesystem-specific file/directory flushes, a validated battery-backed RTC, and
physical interrupted-write testing. A process killed during staging may leave
an ignored scratch file, which the platform can remove on startup. Checkpoint
CRC detects damage; it is not authentication against deliberate modification.

Compile and run the independent controller tests from the repository root:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude src/core.cpp src/weather.cpp src/persistence.cpp src/checkpoint.cpp tests/test_checkpoint.cpp -o build/test_checkpoint
./build/test_checkpoint
```

The tests cover running recovery, slot rotation, truncation and metadata
corruption, fallback preservation, failed atomic replacement, backwards-clock
rejection, temporary-file cleanup, deliberate pause and explicit extinction
restart. These are host tests, not physical-device validation.
