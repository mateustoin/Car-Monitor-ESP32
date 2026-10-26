#include "vehicle_profile.h"

/* Renault Sandero 1.6 8V (K7M), câmbio JH3. Relações do JH3 ainda não
   confirmadas: sem sementes, o aparelho aprende dirigindo. */
const vehicle_profile_t profile_sandero_k7m = {
    .id = "sandero_k7m", .name = "Sandero K7M",
    .gears = 5, .fuel = VP_FUEL_FLEX,
    .seed_ratios = { 0 },
    .idle_rpm = 800, .redline_rpm = 6000,
    .up_rpm_light = 2100, .up_rpm_heavy = 2800,
    .load_light_pct = 20, .load_heavy_pct = 80,
    .min_rpm_after_upshift = 1500,
    .lug_rpm = 1300, .lug_load_pct = 60,
    .target_band_low = 2000, .target_band_high = 2500,
    .ratio_tolerance = 0.07f,
    .gear_hold_ms = 400, .stale_ms = 1000,
};
