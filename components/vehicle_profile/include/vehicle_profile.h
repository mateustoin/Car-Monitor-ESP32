#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Perfis de veículo: todo número específico de um carro mora aqui. */

#define VP_MAX_GEARS 8

typedef enum { VP_FUEL_FLEX, VP_FUEL_GASOLINE, VP_FUEL_ETHANOL, VP_FUEL_DIESEL } vp_fuel_t;

typedef struct {
    const char *id;               /* ≤ 12 chars: vira chave NVS "r_<id>" */
    const char *name;
    uint8_t gears; vp_fuel_t fuel;
    float seed_ratios[VP_MAX_GEARS];      /* km/h por 1000 RPM; 0 = sem semente */
    uint16_t idle_rpm, redline_rpm;
    uint16_t up_rpm_light, up_rpm_heavy;  /* limite de subida em função da carga */
    uint8_t  load_light_pct, load_heavy_pct;
    uint16_t min_rpm_after_upshift;
    uint16_t lug_rpm; uint8_t lug_load_pct;
    uint16_t target_band_low, target_band_high;
    float    ratio_tolerance;              /* fração, ex. 0.07 */
    uint16_t gear_hold_ms;
    uint16_t stale_ms;
} vehicle_profile_t;

size_t vp_count(void);
const vehicle_profile_t *vp_get(size_t i);          /* NULL se i >= vp_count() */
const vehicle_profile_t *vp_find(const char *id);   /* NULL se não existe */
const vehicle_profile_t *vp_default(void);          /* "sandero_k7m" */
bool vp_validate(const vehicle_profile_t *p);
