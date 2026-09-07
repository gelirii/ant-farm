# Historical weather tapes

The bundled inputs are **real historical ERA5 reanalysis records**, retrieved
through Open-Meteo for Newcastle upon Tyne (requested 54.9783° N, 1.6178° W).
Reanalysis reconstructs past weather from observations and atmospheric modelling;
these are not measurements from a particular garden or individual weather station.
The underlying ERA5 atmospheric grid is 0.25°. Returned grid coordinates and
elevation are recorded in each provenance manifest.

Sources: [Open-Meteo archive documentation](https://open-meteo.com/en/docs/historical-weather-api),
[ECMWF/Copernicus ERA5 hourly dataset](https://doi.org/10.24381/cds.adbb2d47).

## Candidate windows

All three tapes cover May 1 through August 31: 123 days, 2,952 consecutive UTC
hours, with no missing values. These are fixed warm-season weather windows;
the simulation has no winter calendar or dormancy season.

| Newcastle window | Mean °C | Min–max °C | Rain mm | Wet days¹ | Longest dry run¹ | Hours <10°C |
|---|---:|---:|---:|---:|---:|---:|
| May–August 2019 | 14.523 | 0.9–28.1 | 288.7 | 92 | 4 days | 447 |
| May–August 2021 | 14.365 | 0.8–29.1 | 222.6 | 72 | 13 days | 471 |
| May–August 2023 | 14.747 | 5.6–25.4 | 271.1 | 81 | 13 days | 270 |

¹ A wet day has at least 0.1 mm liquid rain; a dry day is below that threshold.
Statistics describe source intervals before loop smoothing or cycle variation.
The checked-in `data/weather/summary.json` contains additional reproducible
metrics, including solar irradiance and wind.

**2023 is the initial candidate**, because its temperatures are less extreme,
while its 13-day dry spell still exercises stored moisture and plant resilience.
2019 provides more frequent rain and colder nights; 2021 provides lower total
rain and greater thermal extremes. These choices are based on weather coverage
and contrast, not a claim that a colony survives any of them. Survival and
resource calibration must come from the actual headless ant simulation.

## Time and loop behaviour

The runtime uses one 8-byte packed record per hour: temperature in tenths of
a degree Celsius, rain in hundredths of a millimetre, irradiance in W/m², and
wind in hundredths of m/s. A selected tape occupies **23,616 bytes of packed
observations**, plus vector/object and source metadata overhead. CSV/JSON text
is retained for auditing and need not occupy embedded RAM.

The source API timestamps liquid rain as the **preceding hour's accumulation**
and irradiance as the preceding hour's mean. The importer requests one extra
day and assigns those values to the corresponding interval start. Temperature
and wind stay at their instantaneous timestamps. A runtime record therefore
means temperature/wind at hour H, with rain/irradiance applying to H→H+1.

Temperature, wind and visual light interpolate between records. Rain is held
as the rate for its recorded hour, preserving dry hours and each hour's source
rain amount outside the seam. Light is irradiance divided by 800 W/m² and
clamped to 0–1; this is a simulation/display normalization, not illuminance in
lux. Interpolation and packing make the replay a documented transformation of
the source, not a claim of exact atmospheric reconstruction.

A **5-day overlap and smoothstep crossfade** joins the end of the tape to its
beginning. The replay period is therefore **118 days**. Simulation day zero
starts at source May 6; the final five replay days blend the final five source
days with May 1–5. Then May 6 follows continuously. Full-day overlap preserves
the time of day. No hourly weather lookup or internet access occurs at runtime.

Each loop receives a deterministic seed-derived temperature offset of at most
±0.6°C and rate multipliers of at most ±8% rain, ±5% wind and ±3% light.
During the seam, the current and next loop's adjusted samples blend together,
so the variation does not add a discontinuity at the cycle boundary. The
variation is restrained and does not invent extra rain events in dry hours.
The historical temperature cycle, including its changes in day length, repeats;
there is no annually advancing date or extra season model.

`WeatherTape::sample(seconds, seed)` is stateless and consumes no simulation
random numbers. Its FNV-1a fingerprint covers the packed observations, imported
metadata and seam duration. Saves can reject a different weather tape before
restoring world state. Import metadata includes the raw source hash, so a
newly fetched source response is a different provenance identity even if its
rounded observations happen to match.

## Reproduction and failure handling

From the repository root:

```sh
python3 tools/fetch_weather.py --offline
```

This checks each archived response against its recorded SHA-256, revalidates
and converts it, and regenerates the CSV and summary. To obtain fresh responses:

```sh
python3 tools/fetch_weather.py
```

The importer validates UTC time order, complete hourly coverage, units, finite
numeric values and physical ranges. A malformed or missing hour **fails the
import**; no invented values or silent interpolation fill data gaps. Each tape
retains the raw API response, exact request URL, retrieval time, returned grid,
raw/converted SHA-256 hashes, transformations and missing-data policy.

The C++ loader independently validates column order, dates, consecutive hours,
numeric ranges and 120–184 complete days. Rejected loads preserve the previous
tape. A newly constructed `WeatherTape` contains labelled **synthetic diagnostic
weather** solely for deterministic low-level tests. Historical runs must call
`load_csv` and check `historical()`; diagnostic results cannot establish that a
chosen historical window is suitable.

## Attribution

Weather data by [Open-Meteo](https://open-meteo.com/). Contains modified
Copernicus Climate Change Service information: ECMWF ERA5 hourly data on single
levels, [DOI 10.24381/cds.adbb2d47](https://doi.org/10.24381/cds.adbb2d47).
The data is provided under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
Changes consist of variable selection, interval alignment, CSV conversion,
runtime numeric packing, cyclic overlap and the explicitly described variation.
This software is not endorsed by those data providers. The free API has its
own [non-commercial access terms](https://open-meteo.com/en/terms); the bundled
simulation needs no ongoing API access.
