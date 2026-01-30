/* mcu2_demod_modular.c
   Modular MCU2 BPSK demodulator using configuration header
   Independent of transmitter metadata - only needs link parameters
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// #include "complex_bpsk.h"
#include "complex_qpsk.h"
#include "complex_qam16.h"

#include "mod_configs.h" 
#include "slicers.h"

#define PRINT_EVERY_N_SYMBOLS 100

#define COMPLEX_IQ  complex_qam16

#define _CONCAT(a, b) a ## b
#define CONCAT(a, b) _CONCAT(a, b)

#define COMPLEX_IQ_META CONCAT(COMPLEX_IQ, _meta)

void demod_init(demod_t *demod,demod_config_t cfg);
void print_final_stats(const demod_t *demod);
void process_sample(demod_t *demod, int16_t i, int16_t q);
void get_sample(demod_t *demod, int16_t *i, int16_t *q);
static inline void config_print(const demod_config_t *cfg);

int main(void) {
    demod_t demod;
    
    demod_config_t cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_16QAM
    };
    config_calculate_derived(&cfg);

    demod_init(&demod,cfg);
    
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
           demod.scale, COMPLEX_IQ_META.scale,
           100.0 * fabs(demod.scale - COMPLEX_IQ_META.scale) / COMPLEX_IQ_META.scale);
    printf("  Sample rate:    Configured=%.1f MHz  Header=%.1f MHz\n",
           demod.config.sampling_rate_hz / 1e6, 
           COMPLEX_IQ_META.sampling_rate / 1e6);
    printf("  SPS:            Calculated=%.2f  Header=%.2f\n\n",
           demod.config.samples_per_symbol, 
           COMPLEX_IQ_META.samples_per_symbol);
    
    scanf("%c");



    int16_t i, q;
    uint32_t max_iterations = 3600;

    for (size_t iter=0; iter < max_iterations; iter++){
        get_sample(&demod, &i, &q);
        process_sample(&demod, i, q);
    }

    print_final_stats(&demod);
    
    printf("\n[SUCCESS] MCU2 operated independently with only these inputs:\n");
    printf("  Link bandwidth:  %.1f MHz\n", demod.config.link_bw_hz / 1e6);
    printf("  Sampling rate:   %.1f MHz\n", demod.config.sampling_rate_hz / 1e6);
    printf("  Modulation type: %s\n", get_modulation_name[demod.config.modulation]);
    printf("  Roll-off factor: %.2f\n", demod.config.roll_off);
    printf("  All other parameters were calculated automatically.\n\n");

    return 0;
}

void demod_init(demod_t *demod,demod_config_t cfg) {
    demod->config                  = cfg;
    demod->scale                   = config_get_scale_factor(&demod->config);
    demod->stream_idx              = 0;
    demod->sym                     = (symbol_acc_t){0, 0, 0};
    demod->sum_symbol_signal_power = 0.0;
    demod->sum_symbol_error_power  = 0.0;
    demod->symbol_count            = 0;
    demod->sum_sample_signal_power = 0.0;
    demod->sum_sample_error_power  = 0.0;
    demod->sample_count            = 0;
}

static inline int32_t uint16_to_signed(uint16_t raw, uint8_t resolution) {
    uint32_t max_uint = ((1u << resolution) - 1u);
    uint32_t half = max_uint / 2u;
    int32_t tmp = (int32_t)raw - (int32_t)half;
    if (tmp < -32768) tmp = -32768;
    if (tmp >  32767) tmp =  32767;
    return tmp;
}

void get_sample(demod_t *demod, int16_t *i, int16_t *q) {
    uint32_t total_entries = COMPLEX_IQ_META.n_samples;

    if (demod->stream_idx >= total_entries) 
        demod->stream_idx = 0;

    uint16_t raw_i = COMPLEX_IQ[demod->stream_idx++];
    if (demod->stream_idx >= total_entries)
        demod->stream_idx = 0;
    uint16_t raw_q = COMPLEX_IQ[demod->stream_idx++];

    *i = (int16_t)uint16_to_signed(raw_i, demod->config.signal_resolution);
    *q = (int16_t)uint16_to_signed(raw_q, demod->config.signal_resolution);
    
}


void process_sample(demod_t *demod, int16_t i, int16_t q) {
    // Convert to normalized floating point
    double fi = (double)i / demod->scale;
    double fq = (double)q / demod->scale;

    /* === PRE-FILTER SNR (Sample Level) === */
    SlicerResult result = get_slicer_by_mod[demod->config.modulation](fi,fq);
    double sample_error_i = fi - result.ideal_i;
    double sample_error_q = fq - result.ideal_q;
    
    demod->sum_sample_signal_power += slicer_calculate_power(result.ideal_i,result.ideal_q);
    demod->sum_sample_error_power  += slicer_calculate_power(sample_error_i,sample_error_q);
    demod->sample_count++;

    /* === POST-FILTER SNR (Symbol Level) === */
    demod->sym.acc_i += fi;
    demod->sym.acc_q += fq;
    demod->sym.count++;

    // Check if we have accumulated one symbol
    uint32_t sps_ceil = (uint32_t)ceil(demod->config.samples_per_symbol);
    if (demod->sym.count >= sps_ceil) {
        double rx_i = demod->sym.acc_i / (double)demod->sym.count;
        double rx_q = demod->sym.acc_q / (double)demod->sym.count;

        result = get_slicer_by_mod[demod->config.modulation](rx_i,rx_q);

        double error_i = rx_i - result.ideal_i;
        double error_q = rx_q - result.ideal_q;

        double ideal_power = slicer_calculate_power(result.ideal_i,result.ideal_q);
        double error_power = slicer_calculate_power(error_i,error_q);

        demod->sum_symbol_signal_power += ideal_power;
        demod->sum_symbol_error_power += error_power;
        demod->symbol_count++;

        demod->sym.acc_i = 0.0;
        demod->sym.acc_q = 0.0;
        demod->sym.count = 0;

        if ((demod->symbol_count % PRINT_EVERY_N_SYMBOLS) == 0) {
            
            // double avg_sym_sig_power = demod->sum_symbol_signal_power / demod->symbol_count;
            double avg_sym_sig_power = GET_AVG_POWER(demod,symbol,signal);
            // double avg_sym_err_power = demod->sum_symbol_error_power / demod->symbol_count;
            double avg_sym_err_power = GET_AVG_POWER(demod,symbol,error);
            
            double post_snr_db = 10.0 * log10(avg_sym_sig_power / avg_sym_err_power);
            double post_evm = sqrt(avg_sym_err_power / avg_sym_sig_power) * 100.0;

            // double avg_smp_sig = demod->sum_sample_signal_power / demod->sample_count;
            double avg_smp_sig = GET_AVG_POWER(demod,sample,signal);
            // double avg_smp_err = demod->sum_sample_error_power / demod->sample_count;
            double avg_smp_err = GET_AVG_POWER(demod,sample,error);
            
            double pre_snr_db = 10.0 * log10(avg_smp_sig / avg_smp_err);
            double pre_evm = sqrt(avg_smp_err / avg_smp_sig) * 100.0;

            double gain_db = post_snr_db - pre_snr_db;
            double cn0_dbhz = post_snr_db + 10.0 * log10(demod->config.symbol_rate_hz);

            printf("[MCU2] Sym=%5u | SNR=%5.2f dB | "
                   "MER=%5.2f dB | EVM=%5.2f%% | Gain=%4.2f dB | C/N0=%5.2f dB-Hz\n",
                   demod->symbol_count, pre_snr_db, 
                   post_snr_db, post_evm, gain_db, cn0_dbhz);
        }
    }
}

void print_final_stats(const demod_t *demod) {
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

static inline void config_print(const demod_config_t *cfg) {
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