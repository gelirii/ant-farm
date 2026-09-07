#!/usr/bin/env python3
"""Fetch and validate real historical reanalysis. No silent gap filling.

Stdlib only. Default candidates are the same Newcastle May-August window in
2019, 2021 and 2023. The archived API response and a SHA-256 provenance manifest
are retained beside each converted tape. Use --offline to reprocess those
responses without network access or to reproduce summary.json.
"""
import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "weather"
VARIABLES = ("temperature_2m", "rain", "shortwave_radiation", "wind_speed_10m")
SOURCE = "https://open-meteo.com/en/docs/historical-weather-api"
MODEL_SOURCE = "https://doi.org/10.24381/cds.adbb2d47"
YEARS = (2019, 2021, 2023)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate_and_convert(raw, year):
    payload = json.loads(raw)
    if payload.get("error"):
        raise ValueError(payload.get("reason", "Weather API returned an error"))
    if payload.get("utc_offset_seconds") != 0:
        raise ValueError("Only UTC records are accepted")
    hourly = payload["hourly"]
    times = [dt.datetime.fromisoformat(t) for t in hourly["time"]]
    start, finish = dt.datetime(year, 5, 1), dt.datetime(year, 9, 1)
    count = int((finish - start).total_seconds() // 3600)
    expected = [start + dt.timedelta(hours=i) for i in range(count + 1)]
    if times[:count + 1] != expected:
        raise ValueError("Missing, repeated or out-of-order hours; refusing gap filling")
    units = payload["hourly_units"]
    for var, unit in zip(VARIABLES, ("°C", "mm", "W/m²", "m/s")):
        if units[var] != unit:
            raise ValueError(f"Wrong unit for {var}: {units[var]}")
        values = hourly[var]
        if len(values) != len(times) or any(v is None or not math.isfinite(v) for v in values):
            raise ValueError(f"Incomplete {var}; refusing synthetic replacements")
    rows = []
    for i in range(count):
        # Rain/irradiance are the preceding hour's sum/mean. Record i's interval
        # with the API value timestamped i+1; temperature/wind remain instantaneous.
        values = (hourly["temperature_2m"][i], hourly["rain"][i + 1],
                  hourly["shortwave_radiation"][i + 1], hourly["wind_speed_10m"][i])
        for v, low, high in zip(values, (-60, 0, 0, 0), (60, 500, 1500, 150)):
            if not low <= v <= high:
                raise ValueError(f"Physical range failure at {times[i]}: {values}")
        rows.append((times[i].isoformat(timespec="minutes"), *values))
    return payload, rows


def metrics(rows):
    temperatures = [r[1] for r in rows]
    rain = [r[2] for r in rows]
    daily_rain = [sum(rain[i:i + 24]) for i in range(0, len(rain), 24)]
    run = longest = 0
    for amount in daily_rain:
        run = run + 1 if amount < 0.1 else 0
        longest = max(longest, run)
    return {
        "days": len(rows) // 24, "hours": len(rows), "missing_values": 0,
        "mean_temperature_c": round(statistics.mean(temperatures), 3),
        "min_temperature_c": min(temperatures), "max_temperature_c": max(temperatures),
        "hours_below_10c": sum(t < 10 for t in temperatures),
        "hours_above_28c": sum(t > 28 for t in temperatures),
        "rain_total_mm": round(sum(rain), 2),
        "rainy_days_at_least_0_1mm": sum(r >= 0.1 for r in daily_rain),
        "longest_dry_run_days_under_0_1mm": longest,
        "max_hour_rain_mm": max(rain),
        "mean_solar_wm2": round(statistics.mean(r[3] for r in rows), 3),
        "mean_wind_ms": round(statistics.mean(r[4] for r in rows), 3),
    }


def fetch(year, offline):
    params = {
        "latitude": 54.9783, "longitude": -1.6178,
        "start_date": f"{year}-05-01", "end_date": f"{year}-09-01",
        "hourly": ",".join(VARIABLES), "timezone": "GMT",
        "wind_speed_unit": "ms", "models": "era5",
    }
    url = "https://archive-api.open-meteo.com/v1/archive?" + urllib.parse.urlencode(params)
    stem = f"newcastle_{year}_may_aug"
    raw_path, csv_path = OUT / f"{stem}.raw.json", OUT / f"{stem}.csv"
    manifest_path = OUT / f"{stem}.provenance.json"
    if offline:
        raw = raw_path.read_bytes()
        manifest = json.loads(manifest_path.read_text())
        if sha(raw) != manifest["raw_sha256"]:
            raise ValueError("Archived source SHA-256 mismatch")
        retrieved = manifest["retrieved_utc"]
    else:
        request = urllib.request.Request(url, headers={"User-Agent": "antfarm-weather-research/1.0"})
        with urllib.request.urlopen(request, timeout=55) as response:
            raw = response.read()
        retrieved = dt.datetime.now(dt.timezone.utc).isoformat()
    payload, rows = validate_and_convert(raw, year)
    text = io.StringIO(newline="")
    text.write("#kind=historical_reanalysis\n")
    text.write(f"#name=Newcastle upon Tyne, May-August {year}\n")
    text.write("#source=Open-Meteo Historical Weather API; ECMWF ERA5\n")
    text.write(f"#source_url={url}\n")
    text.write(f"#raw_sha256={sha(raw)}\n")
    text.write("#license=CC BY 4.0; credit Open-Meteo and Copernicus Climate Change Service\n")
    text.write("#time_basis=UTC; rain and solar shifted to interval-start; temperature and wind instantaneous\n")
    writer = csv.writer(text, lineterminator="\n")
    writer.writerow(("time_utc", "temperature_c", "rain_mm_hour", "solar_wm2", "wind_ms"))
    writer.writerows(rows)
    csv_data = text.getvalue().encode()
    result = metrics(rows)
    provenance = {
        "name": stem, "kind": "historical_reanalysis", "dataset": "ECMWF ERA5 via Open-Meteo",
        "source_documentation": SOURCE, "underlying_dataset": MODEL_SOURCE,
        "source_url": url, "retrieved_utc": retrieved,
        "requested_coordinates": {"latitude": 54.9783, "longitude": -1.6178},
        "returned_grid": {k: payload[k] for k in ("latitude", "longitude", "elevation")},
        "raw_sha256": sha(raw), "csv_sha256": sha(csv_data),
        "license": "CC BY 4.0", "license_url": "https://creativecommons.org/licenses/by/4.0/",
        "attribution": "Weather data by Open-Meteo. Contains modified Copernicus Climate Change Service information.",
        "method": "ERA5 historical reanalysis, not a direct station observation or invented weather trace.",
        "interval_conversion": "Rain and shortwave radiation at API hour i+1 assigned to tape interval i. Temperature and wind taken at hour i.",
        "missing_data_policy": "Reject incomplete/non-finite/nonconsecutive hours; no imputation.",
        "metrics_before_loop_smoothing_or_variation": result,
    }
    # No output is replaced until the complete response has passed validation.
    raw_path.write_bytes(raw)
    csv_path.write_bytes(csv_data)
    manifest_path.write_text(json.dumps(provenance, indent=2) + "\n")
    return {"name": stem, "csv": csv_path.name, **result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    results = []
    for year in YEARS:
        result = fetch(year, args.offline)
        results.append(result)
        print(json.dumps(result), flush=True)
    (OUT / "summary.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
