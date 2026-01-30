#ifndef MCU2_CONFIG_H
#define MCU2_CONFIG_H

#include <stdint.h>
#include <math.h>
#include "base.h"

static inline void config_calculate_derived(demod_config_t *cfg) {
    cfg->bits_per_symbol = get_bits_per_symbol[cfg->modulation];
    cfg->symbol_rate_hz = cfg->link_bw_hz / (1.0 + cfg->roll_off);
    cfg->samples_per_symbol = ceil(cfg->sampling_rate_hz / cfg->symbol_rate_hz);
}

static inline double config_get_scale_factor(const demod_config_t *cfg) {
    /*
       Estimate the scale factor used in encoding.
       Python uses: scale = (half * 0.95) / max_abs
       
       For normalized constellations (max amplitude ~1.0):
       scale ≈ half * 0.95
    */
    const float scale_estimative = 1.3;
    uint32_t max_uint = ((1u << cfg->signal_resolution) - 1u);
    uint32_t half = max_uint / 2u;
    return (double)(half * 0.95) / 1.5f;
}

static inline demod_config_t config_preset_bpsk_10mhz(void) {
    demod_config_t cfg = {
        .link_bw_hz = 10e6,          // 10 MHz
        .sampling_rate_hz = 20e6,    // 20 MHz (2x oversampling)
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_BPSK
    };
    config_calculate_derived(&cfg);
    return cfg;
}

static inline demod_config_t config_preset_qpsk_10mhz(void) {
    demod_config_t cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_QPSK
    };
    config_calculate_derived(&cfg);
    return cfg; 
}

static inline demod_config_t config_preset_16qam_10mhz(void) {
    demod_config_t cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_16QAM
    };
    config_calculate_derived(&cfg);
    return cfg;
}

#endif /* MCU2_CONFIG_H */