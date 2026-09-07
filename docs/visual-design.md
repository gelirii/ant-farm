# A quiet formicarium

The three reference videos establish the composition: a portrait view, approximately 30% above ground, a granular soil cross-section, individually visible ants, botanical growth, and passages excavated by the colony. The renderer follows that visual language with a narrower, muted palette: warm soil, umber cavities, olive foliage, pale flowers, and charcoal ants. It is intentionally legible without demanding attention.

The 180 × 320 simulation grid is rendered directly into a 1080 × 1920 RGB565 framebuffer. One world cell occupies six display pixels. Workers have a small segmented body, six bent legs, two antennae, a direction, and a visible soil parcel when carrying one. Queens are larger. Brood, dead ants, fallen food, and plant growth all correspond to actual world objects. There are no painted-in passages or decorative populations. New terrain starts closed and its history remains visible as the colony digs and piles soil.

Surface plants use their actual biomass, type, water level, and alive/dead state. Rootlets stop at open cells. Grain stays in place as time passes: it is a deterministic spatial texture, never animated noise. Only changes to the soil or moisture change the ground image. Underground lighting remains stable; the sky slowly follows the world clock through a limited range of pale daylight and soft grey dusk. It stays readable at night. Weather effects are currently expressed through the ecology and moisture, without animated rain or cloud overlays.

The framebuffer has no controls, numbers, labels, alerts, bright highlights, camera moves, or particle bursts. A frame around the image belongs to the physical enclosure. The desktop viewer adds a removable explanatory margin outside the framebuffer; it is not part of the display artwork.

## Implementation and memory

`Renderer` retains a RGB565 terrain cache and restores only 32 × 32 tiles touched by the previous frame's organisms or changed terrain. A changed soil cell also refreshes neighbouring edge shading. Terrain keys include visible moisture and nutrient bins; spatial texture hashing runs only for invalidated cells. Flags, the terrain cache, and the caller's output vector retain their capacity. Drawing is clipped, integer-based, and does not allocate per organism. Shapes are rasterised directly into RGB565, with no RGBA intermediate, font engine, graphics library, image assets, or framebuffer conversion on the device.

At 1080 × 1920, each RGB565 framebuffer is 4,147,200 bytes. One terrain cache plus one caller-owned output is 8,294,400 bytes before world state, dirty flags, and the board's display buffers. Continuous display scanout may require another framebuffer depending on the eventual driver. External PSRAM and actual bandwidth measurements are therefore necessary before selecting a board and panel. A software render-time benchmark is not a measurement of physical LCD transfer or scanout performance.

Keep a `Renderer` and output vector alive across frames. The global `render` convenience function creates a temporary renderer for a single exported frame; independent displays should own separate instances. The core can run without this renderer in headless experiments. `write_ppm` is a host-only export convenience; the P4 driver should use RGB565 pixels directly.

The `alpha` argument is the fractional second past the world's integer clock. Each ant interpolates from its previous cell to its current cell over two seconds from its recorded `last_move`. A resting ant stays in place after that interval; an old previous position is never replayed on later timer cycles. An idle creature's leg pose does not animate independently of simulation movement.

## Preview

Build the native executable, then run `python3 tools/viewer.py`. The viewer launches the same compiled simulation in real time and serves the completed framebuffer at `http://127.0.0.1:8765`. Pillow is needed only for browser PNG encoding. `--days 60` creates a new preview at day 60, while an existing save always resumes. Use a different `--state-dir` to create a separate colony. `--no-launch` displays an externally generated `frame.ppm` from the selected state directory.

The Python viewer has no duplicate simulation and cannot make cosmetic decisions about where tunnels, organisms, or resources appear. Native colour precision, spatial texture, and object locations survive the PPM-to-PNG conversion exactly.
