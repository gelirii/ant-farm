# ESP32-P4 integration boundary

The C++ simulation and RGB565 renderer are supplied as an ESP-IDF component.
There is no selected or tested board, panel, bridge, RTC or storage driver yet.
This directory is consequently a component integration starting point, **not
flashable firmware or a verified wiring plan**. ESP-IDF is not installed on the
current development host; the component build must be checked in the board project.

Use a board with sufficient PSRAM; 32 MB is the working budget. Espressif documents
16 MB and 32 MB variants in the [P4 datasheet](https://documentation.espressif.com/esp32-p4_datasheet_en.html).
The exact panel timing and sustained scanout bandwidth must be tested. MIPI-DSI
support alone does not establish compatibility with a laptop eDP panel. The
[MIPI-DSI driver documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/lcd/dsi_lcd.html)
also specifies the dedicated 2.5 V DPHY supply and DMA alignment requirements.

## Board application contract

1. Mount storage, initialize battery-backed RTC, load the selected weather CSV,
   then load the newest valid world snapshot. Keep two validated snapshot slots;
   each needs an atomically associated RTC timestamp and sequence number.
2. A running snapshot resumes through `World::catch_up(elapsed_seconds)` using
   RTC time since its checkpoint. A deliberately paused snapshot resumes without
   advancing the interval spent OFF. Do not silently advance after an invalid or
   backwards RTC reading; retain the world and report the clock fault in diagnostics.
3. Use a monotonic clock for normal operation: advance exactly one simulated
   second for each elapsed real second. Chunk long catch-up into bounded pieces
   and yield to the watchdog. Chunking is covered by equivalence tests.
4. Keep one renderer and output buffer alive. Render fractional movement at an
   initial 10 Hz and profile before choosing a higher rate. Render from an
   immutable world snapshot or serialize render/advance access; these classes
   are not concurrently mutable. No gameplay interface belongs on the display.
5. Implement panel scanout with its required DMA buffers, alignment, cache
   synchronization and tear-free handoff. A cached compositor saves CPU drawing;
   it does not remove the LCD's continuous scanout bandwidth. Current vector
   output needs copying to a driver-owned DMA buffer unless the renderer API is
   extended to accept that buffer directly and validated byte-for-byte.
6. Debounce rear power and brightness buttons. Deliberate OFF must persist the
   paused state successfully before power is removed. After complete extinction,
   `power_off()` followed by `power_on()` replaces underground soil and brings in
   new founders while preserving world day, plants and seed bank.

One 1080×1920 RGB565 framebuffer is 4,147,200 bytes. Add the renderer cache, core,
weather, driver buffers, stacks, filesystem and temporary checkpoint storage to
that figure. See the measured host report; PSRAM capacity is not a bandwidth or
frame-rate guarantee. Do not change biological time steps to conceal a slow port.

Required device validation: repeated power cuts during both checkpoint slots;
RTC battery removal/backwards time; many-hour outage catch-up; sustained scanout
with SD activity; heap fragmentation; worst-case colony/render load; measured
board power, frame latency and temperature. Host checks cannot substitute for it.
