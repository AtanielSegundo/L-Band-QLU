/* mcu2_demod_modular.c
   Modular MCU2 BPSK demodulator using configuration header
   Independent of transmitter metadata - only needs link parameters
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "complex_bpsk.h"
#include "mod_configs.h" 

#define PRINT_EVERY_N_SYMBOLS 100

typedef struct {
    double acc_i;
    double acc_q;
    uint32_t count;
} symbol_acc_t;

typedef struct {
    mcu2_config_t config;
    double scale;
    
    uint32_t stream_idx;
    symbol_acc_t sym;
    
    double sum_symbol_signal_power;
    double sum_symbol_error_power;
    uint32_t symbol_count;
    
    double sum_sample_signal_power;
    double sum_sample_error_power;
    uint32_t sample_count;
} mcu2_demod_t;


void mcu2_demod_init(mcu2_demod_t *demod) {
    demod->config = config_preset_bpsk_10mhz();
    demod->scale = config_get_scale_factor(&demod->config);
    demod->stream_idx = 0;
    demod->sym = (symbol_acc_t){0, 0, 0};
    demod->sum_symbol_signal_power = 0.0;
    demod->sum_symbol_error_power = 0.0;
    demod->symbol_count = 0;
    demod->sum_sample_signal_power = 0.0;
    demod->sum_sample_error_power = 0.0;
    demod->sample_count = 0;
}

static inline int32_t uint16_to_signed(uint16_t raw, uint8_t resolution) {
    uint32_t max_uint = ((1u << resolution) - 1u);
    uint32_t half = max_uint / 2u;
    int32_t tmp = (int32_t)raw - (int32_t)half;
    if (tmp < -32768) tmp = -32768;
    if (tmp >  32767) tmp =  32767;
    return tmp;
}

void mcu2_get_sample(mcu2_demod_t *demod, int16_t *i, int16_t *q) {
    uint32_t total_entries = complex_bpsk_meta.n_samples;

    if (demod->stream_idx >= total_entries) 
        demod->stream_idx = 0;

    uint16_t raw_i = complex_bpsk[demod->stream_idx++];
    if (demod->stream_idx >= total_entries)
        demod->stream_idx = 0;
    uint16_t raw_q = complex_bpsk[demod->stream_idx++];

    *i = (int16_t)uint16_to_signed(raw_i, demod->config.signal_resolution);
    *q = (int16_t)uint16_to_signed(raw_q, demod->config.signal_resolution);
    
}

void mcu2_process_sample(mcu2_demod_t *demod, int16_t i, int16_t q) {
    // Convert to normalized floating point
    double fi = (double)i / demod->scale;
    double fq = (double)q / demod->scale;

    /* === PRE-FILTER SNR (Sample Level) === */
    double sample_ideal_i = (fi >= 0.0) ? +1.0 : -1.0;
    double sample_ideal_q = 0.0;
    double sample_error_i = fi - sample_ideal_i;
    double sample_error_q = fq - sample_ideal_q;
    
    demod->sum_sample_signal_power += 
        sample_ideal_i * sample_ideal_i + sample_ideal_q * sample_ideal_q;
    demod->sum_sample_error_power += 
        sample_error_i * sample_error_i + sample_error_q * sample_error_q;
    demod->sample_count++;

    /* === POST-FILTER SNR (Symbol Level - Matched Filtering) === */
    demod->sym.acc_i += fi;
    demod->sym.acc_q += fq;
    demod->sym.count++;

    // Check if we have accumulated one symbol
    uint32_t sps_ceil = (uint32_t)ceil(demod->config.samples_per_symbol);
    if (demod->sym.count >= sps_ceil) {
        // Average over symbol period (matched filter)
        double rx_i = demod->sym.acc_i / (double)demod->sym.count;
        double rx_q = demod->sym.acc_q / (double)demod->sym.count;

        // BPSK slicer decision
        double ideal_i = (rx_i >= 0.0) ? +1.0 : -1.0;
        double ideal_q = 0.0;

        // Error vector
        double error_i = rx_i - ideal_i;
        double error_q = rx_q - ideal_q;

        // Power calculations
        double ideal_power = ideal_i * ideal_i + ideal_q * ideal_q;
        double error_power = error_i * error_i + error_q * error_q;

        demod->sum_symbol_signal_power += ideal_power;
        demod->sum_symbol_error_power += error_power;
        demod->symbol_count++;

        // Reset symbol accumulator
        demod->sym.acc_i = 0.0;
        demod->sym.acc_q = 0.0;
        demod->sym.count = 0;

        // Periodic reporting
        if ((demod->symbol_count % PRINT_EVERY_N_SYMBOLS) == 0) {
            double avg_sym_sig = demod->sum_symbol_signal_power / demod->symbol_count;
            double avg_sym_err = demod->sum_symbol_error_power / demod->symbol_count;
            double post_snr_db = 10.0 * log10(avg_sym_sig / avg_sym_err);
            double post_evm = sqrt(avg_sym_err / avg_sym_sig) * 100.0;

            double avg_smp_sig = demod->sum_sample_signal_power / demod->sample_count;
            double avg_smp_err = demod->sum_sample_error_power / demod->sample_count;
            double pre_snr_db = 10.0 * log10(avg_smp_sig / avg_smp_err);
            double pre_evm = sqrt(avg_smp_err / avg_smp_sig) * 100.0;

            double gain_db = post_snr_db - pre_snr_db;
            double cn0_dbhz = post_snr_db + 10.0 * log10(demod->config.symbol_rate_hz);

            printf("[MCU2] Sym=%5u | PRE: SNR=%5.2f dB EVM=%5.2f%% | "
                   "POST: SNR=%5.2f dB EVM=%5.2f%% | Gain=%4.2f dB | C/N0=%5.2f dB-Hz\n",
                   demod->symbol_count, pre_snr_db, pre_evm, 
                   post_snr_db, post_evm, gain_db, cn0_dbhz);
        }
    }
}

void mcu2_print_final_stats(const mcu2_demod_t *demod) {
    printf("\n========================================================================\n");
    printf("  FINAL STATISTICS\n");
    printf("========================================================================\n");
    
    if (demod->sum_sample_error_power > 0.0 && demod->sample_count > 0) {
        double avg_sig = demod->sum_sample_signal_power / demod->sample_count;
        double avg_err = demod->sum_sample_error_power / demod->sample_count;
        double pre_snr_db = 10.0 * log10(avg_sig / avg_err);
        double pre_evm = sqrt(avg_err / avg_sig) * 100.0;

        printf("\nPRE-FILTER (Input SNR - matches Python):\n");
        printf("  Total samples:          %u\n", demod->sample_count);
        printf("  SNR:                    %.2f dB\n", pre_snr_db);
        printf("  EVM:                    %.2f%%\n", pre_evm);
    }

    if (demod->sum_symbol_error_power > 0.0 && demod->symbol_count > 0) {
        double avg_sig = demod->sum_symbol_signal_power / demod->symbol_count;
        double avg_err = demod->sum_symbol_error_power / demod->symbol_count;
        
        // Calculate SNRs first
        double pre_snr_db = 10.0 * log10(demod->sum_sample_signal_power / demod->sum_sample_error_power);
        double post_snr_db = 10.0 * log10(demod->sum_symbol_signal_power / demod->sum_symbol_error_power);

        double post_evm = sqrt(avg_err / avg_sig) * 100.0;

        printf("\nPOST-FILTER (Output SNR - demod performance):\n");
        printf("  Total symbols:          %u\n", demod->symbol_count);
        printf("  SNR:                    %.2f dB\n", post_snr_db);
        printf("  EVM:                    %.2f%%\n", post_evm);

        // Gain is simply the improvement in SNR
        double measured_gain_db = post_snr_db - pre_snr_db;
        double gain_expected = 10.0 * log10(demod->config.samples_per_symbol);

        printf("\nPROCESSING GAIN:\n");
        printf("  Measured:               %.2f dB\n", measured_gain_db);
        printf("  Expected:               %.2f dB (from SPS=%.2f)\n", 
               gain_expected, demod->config.samples_per_symbol);
        printf("  Error:                  %.2f dB\n", measured_gain_db - gain_expected);

        double cn0_dbhz = post_snr_db + 10.0 * log10(demod->config.symbol_rate_hz);
        double bit_rate = demod->config.symbol_rate_hz * demod->config.bits_per_symbol;
        
        printf("\nLINK PERFORMANCE:\n");
        printf("  C/N0:                   %.2f dB-Hz\n", cn0_dbhz);
        printf("  Symbol rate:            %.3f MHz\n", demod->config.symbol_rate_hz / 1e6);
        printf("  Bit rate:               %.3f Mbps\n", bit_rate / 1e6);
    }
    
    printf("========================================================================\n");
}

static inline void config_print(const mcu2_config_t *cfg) {
    printf("\n[MCU2 Configuration]\n");
    printf("  Modulation:             %s (%u bits/symbol)\n", 
           get_modulation_name[cfg->modulation], cfg->bits_per_symbol);
    printf("  Link bandwidth:         %.3f MHz\n", cfg->link_bw_hz / 1e6);
    printf("  Sampling rate:          %.3f MHz (%.1fx)\n", 
           cfg->sampling_rate_hz / 1e6, 
           cfg->sampling_rate_hz / cfg->link_bw_hz);
    printf("  Roll-off factor:        %.2f\n", cfg->roll_off);
    printf("  Signal resolution:      %u bits\n", cfg->signal_resolution);
    printf("\n[Calculated Parameters]\n");
    printf("  Symbol rate:            %.3f MHz\n", cfg->symbol_rate_hz / 1e6);
    printf("  Bit rate:               %.3f Mbps\n", 
           cfg->symbol_rate_hz * cfg->bits_per_symbol / 1e6);
    printf("  Samples per symbol:     %.2f\n", cfg->samples_per_symbol);
    printf("  Expected proc. gain:    %.2f dB\n", 
           10.0 * log10(cfg->samples_per_symbol));
    printf("  Estimated scale:        %.2f\n", 
           config_get_scale_factor(cfg));
}

int main(void) {
    mcu2_demod_t demod;
    
    // Initialize demodulator
    mcu2_demod_init(&demod);
    
    // Print configuration
    printf("========================================================================\n");
    printf("  MCU2 - Independent Demodulator\n");
    printf("  Configuration-Based (No Transmitter Metadata Required)\n");
    printf("========================================================================\n");
    config_print(&demod.config);
    printf("------------------------------------------------------------------------\n");
    printf("  PRE  = SNR before matched filter (input SNR from Python)\n");
    printf("  POST = SNR after matched filter (actual demod performance)\n");
    printf("========================================================================\n\n");
    
    // Compare with header metadata if available
    printf("[INFO] Comparing calculated vs header metadata:\n");
    printf("  Scale:          Calculated=%.2f  Header=%.2f  Diff=%.2f%%\n",
           demod.scale, complex_bpsk_meta.scale,
           100.0 * fabs(demod.scale - complex_bpsk_meta.scale) / complex_bpsk_meta.scale);
    printf("  Sample rate:    Configured=%.1f MHz  Header=%.1f MHz\n",
           demod.config.sampling_rate_hz / 1e6, 
           complex_bpsk_meta.sampling_rate / 1e6);
    printf("  SPS:            Calculated=%.2f  Header=%.2f\n\n",
           demod.config.samples_per_symbol, 
           complex_bpsk_meta.samples_per_symbol);
    
    scanf("%c");

    int16_t i, q;
    uint32_t max_iterations = 1000;

    for (size_t iter=0; iter < max_iterations; iter++){
        mcu2_get_sample(&demod, &i, &q);
        mcu2_process_sample(&demod, i, q);
    }

    mcu2_print_final_stats(&demod);
    
    printf("\n[SUCCESS] MCU2 operated independently with only these inputs:\n");
    printf("  Link bandwidth:  %.1f MHz\n", demod.config.link_bw_hz / 1e6);
    printf("  Sampling rate:   %.1f MHz\n", demod.config.sampling_rate_hz / 1e6);
    printf("  Modulation type: %s\n", get_modulation_name[demod.config.modulation]);
    printf("  Roll-off factor: %.2f\n", demod.config.roll_off);
    printf("  All other parameters were calculated automatically.\n\n");

    return 0;
}