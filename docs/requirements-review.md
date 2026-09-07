# Requirements review

Reviewed `reference-conversation.md`, the historical “Design ant farm simulation” discussion. Historical assistant text records proposals, not verified biology, hardware compatibility, or independently approved requirements. The latest request adds an explicit priority for longevity tests, resource/weather calibration, calm observation, and measured ESP32-P4 efficiency.

## Explicit user decisions

- A physical, autonomous ant farm on an embedded display. Normal operation runs at real time, without user-facing speed controls.
- Simple innate ant behaviour from birth: local environmental and pheromone responses; no neural networks, reinforcement learning, or training phase.
- Ant corpses persist for a long period and disappear only through decomposition.
- Soil never disappears: excavation moves earth, and oversized/unsupported tunnels can collapse with earth falling into them. Keep this simple but physically credible.
- Plants emerge in varied locations, supply food, exhaust resources, die, and decompose. Their normal complete growth-to-death lifecycle should be around 8–10 weeks, with enough turnover to avoid a static surface.
- No seasons or winter/dormancy code. Replay a historical hourly weather period with a spring/summer character, smooth the loop boundary over several days to a week, and add restrained variation between cycles. “February–June 1995” was explicitly only an example.
- Approximately 30% above ground and 70% underground, portrait and roughly A4 size or larger. High enough resolution to leave room for detail.
- No touchscreen. Raw laptop panel, purchased display bridge, ESP32-P4, USB-C power, rear brightness up/down and power buttons, user-built enclosure.
- Battery-backed clock semantics: deliberate OFF pauses the world until ON; unplugging while running causes elapsed-time catch-up on restart.
- Host headless tests run as fast as possible before hardware arrives. Latest request explicitly asks to test resource spawning, weather choice, ant progress and long-term survival, then optimize rigorously without harming visuals or simulation.
- Latest visual direction: interesting to observe but otherwise visually non-stimulating, like a real ant farm.

## Coherent design proposals consistent with the decisions

These are useful implementation defaults, but numerical values remain hypotheses to verify and tune.

- Lasius-niger-inspired black garden ants: one queen, individual workers, egg/larva/pupa development, age-biased but flexible work preferences, feeding, foraging, excavation, brood relocation, corpse transport and resting.
- Local rules, pheromone gradients and local crowd/environment cues create trails and nest structure; avoid a global job dispatcher or predefined nursery/food/cemetery rooms. Soil is actual material in a grid, not a decorative picture behind a node graph.
- Carbohydrate and protein are separate nutritional needs. Food exists in sources, carried fragments and ant crops rather than teleporting from a global inventory to hungry ants. Species choice and diet must remain consistent; plants alone do not automatically supply a suitable insect-based protein economy.
- Nutrient/biomass bookkeeping links plants, ants, waste and decomposition. Seeds and a persistent seed bank explain new plants; limited incoming windborne seeds/prey are explicit boundary inputs, not invisible rescue provisions.
- Temperature and moisture propagate into soil and influence activity, brood location/development, plant growth, spoilage and stability. Use a simple support/cohesion model, not full granular physics or automatic collapse of every large room.
- Persistent individual identities and physical corpses; all living and decomposing state survives saves, including random-generator and weather position.
- One portable simulation core shared by desktop/headless and ESP32-P4, with renderer and platform I/O separate. Display interpolation and infrequent slow-process updates can save work without accelerating biology.
- Alternating validated save snapshots, versioned state, exact deliberate-pause restore, and a separately validated outage catch-up model.
- Target proposed by the historical assistant: roughly 16-inch 1920×1200 matte eDP panel (1200×1920 portrait), 32 MB PSRAM, microSD, MIPI-DSI-to-eDP bridge. Exact part choices, timings, power rails and throughput need fresh verification before presenting them as a working hardware integration.

## Superseded, unapproved or optional ideas

- Earlier seasons, annual day-length progression and real winter dormancy were explicitly rejected. Do not reintroduce them in the name of biological accuracy; label the result a stylized ant ecology inspired by the species.
- Earlier touchscreen controls, ant inspection by tapping, zooming, food-placement tools and touch hardware milestones were superseded by the no-touch physical object.
- Earlier 7–10-inch or 13.3-inch screen proposals and 5 V-only supply assumptions were superseded by the larger raw-panel architecture.
- The reference's citations and biological numbers are historical assistant claims. Source articles are not embedded. They require verification before becoming calibrated constants or evidence claims.
- Queen + 8 workers was an illustrative starting point, not a user-selected initial condition.
- Genetics, reproductive succession, predators, aphid tending, optional network connectivity and interactive diagnostic overlays were exploratory or later-stage ideas, not baseline commitments.
- A suggested 6–12-week range for some plant archetypes must not silently replace the user's central 8–10-week lifecycle target.

## Latest decisions and remaining hardware constraint

1. **Colony mortality.** Extinction is possible but should be rare. Only a deliberate OFF then ON after complete extinction resets the underground soil and brings new founders in from the screen edge. Elapsed day, weather position, plants and seed bank persist. No automatic replacement workers, emergency food or immortal queen.
2. **Initial experience.** Start with approximately 30 total ants (one queen and 29 workers), entering fresh soil from off screen. Test smaller founding populations and retain the smallest with evidence of rare extinction. Founding size is still a calibration hypothesis, not a proven survival guarantee.
3. **Exact installed hardware.** Board, panel and bridge are not confirmed purchases. This blocks verified wiring/driver integration and defensible device performance claims, but not the portable simulation, software renderer, headless testing or memory-budget work.

The current target is 1080×1920 portrait at approximately A4 size on ESP32-P4; no board or panel has been purchased. Weather place/year/window, modest noise magnitude, exact birth/decay rates, population range and renderer scheduling are implementation/calibration choices supported by tests. The three original reference videos were located and sampled: warm layered sand, sparse plants, small ants and branching dark tunnels.

## Consequences for testing and claims

- Test multiple random seeds and candidate weather windows; track extinction, adult/brood trajectory, resource availability, plant turnover, excavation/deposition, corpse persistence, decomposition and numerical/mass invariants over long simulated durations.
- Healthy longevity alone does not prove the intended simulation: a colony held alive by abstract food access or population rescue would not test local foraging, physical cargo and nutrient transport.
- Maximum-speed headless execution should use the same biological/behavioural core. If coarse outage catch-up or an accelerated ecological surrogate is tested, report it separately and compare against the full-detail simulation; do not present surrogate years as equivalent validation of ant movement and nest construction.
- No proof of universal survival or “absolute best possible” efficiency is achievable from a finite test suite. Provide measured before/after profiles, memory budgets, reproducible benchmarks, preserved-output comparisons where applicable, and explicitly distinguish host measurements from real ESP32-P4 measurements.
