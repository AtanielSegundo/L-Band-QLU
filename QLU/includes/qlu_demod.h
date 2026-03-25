#ifndef QLU_DEMOD_H

#define QLU_DEMOD_H

#include <math.h>

// --- Estrutura de Troca de Mensagens (Core 0 -> Core 1) ---
// Blocos de 256 amostras são enviados entre cores
typedef struct {
    uint16_t i_samples[PROCESS_BLOCK_SIZE];
    uint16_t q_samples[PROCESS_BLOCK_SIZE];
    uint32_t timestamp;
} IqBlock_t;


typedef enum {
    MOD_BPSK, 
    MOD_QPSK, 
    MOD_16QAM,
    
    MOD_NUM_MODULATIONS
} modulation_type_t;

typedef struct {
    // Link bandwidth in Hz
    double link_bw_hz;        
    // ADC sampling rate in Hz
    double sampling_rate_hz;  
    // Raised cosine roll-off factor (0.0 to 1.0)
    double roll_off;          
    
    // ADC resolution in bits (e.g., 16)
    uint8_t signal_resolution; 
    // Modulation scheme 
    modulation_type_t modulation;
    
    // Calculated: link_bw / (1 + roll_off)
    double  symbol_rate_hz;      
    // Calculated: sampling_rate / symbol_rate
    double  samples_per_symbol;  
    // Depends on modulation type
    uint8_t bits_per_symbol;     
    
} demod_config_t;


typedef struct {
    double acc_i;
    double acc_q;
    uint32_t count;
} symbol_acc_t;

typedef struct {
    demod_config_t config;
    double scale;
    
    uint32_t stream_idx;
    symbol_acc_t sym;
    
    double sum_symbol_signal_power;
    double sum_symbol_error_power;
    uint64_t symbol_count;
    
    double sum_sample_signal_power;
    double sum_sample_error_power;
    uint64_t sample_count;
} demod_t;

// FIX: Macro corrigida para usar o contador correto baseado no tipo
// Para symbol: usa symbol_count
// Para sample: usa sample_count
#define GET_AVG_POWER(demod,type,part) ((demod)->sum_ ## type ## _ ## part ## _power / (demod)->type ## _count)

const int get_bits_per_symbol[] ={
    [MOD_BPSK]  = 1,
    [MOD_QPSK]  = 2,
    [MOD_16QAM] = 4
};

const char* get_modulation_name[] = {
    [MOD_BPSK]  = "BPSK",
    [MOD_QPSK]  = "QPSK",
    [MOD_16QAM] = "16QAM"
};

bool get_modulation_from_name(modulation_type_t* mod, char* name){
    bool match = false;
    for(size_t i=0; i < MOD_NUM_MODULATIONS; i++){
        if(strcmp(get_modulation_name[i],name) == 0){
            match = true;
            *mod = (modulation_type_t)i; 
        }
    }
    return match;
}

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

inline double slicer_calculate_power(double i, double q){
    return (i * i + q * q);
}


typedef struct{
    double ideal_i;
    double ideal_q;
} SlicerResult;


typedef SlicerResult (*slicer_fn_t)(double,double);

SlicerResult bpsk_slicer(double rx_i, double rx_q){
    return (SlicerResult){
        .ideal_i = (rx_i >= 0.0) ? +1.0 : -1.0,
        .ideal_q = (0.0)
    };
};

// Garante que Symbol Power = 1.0
const float QPSK_NORM = 0.7071067812;
SlicerResult qpsk_slicer(double rx_i, double rx_q){
    // QPSK is essentially two BPSK signals in quadrature.
    // We snap the incoming signal to the nearest +/- 1.0 level.
    return (SlicerResult){
        .ideal_i = (rx_i >= 0.0) ? QPSK_NORM : -QPSK_NORM,
        .ideal_q = (rx_q >= 0.0) ? QPSK_NORM : -QPSK_NORM
    };
};

// 16-QAM: Levels +/- 1, +/- 3 must be scaled by 1/sqrt(10)
const float QAM16_NORM = 0.3162277660; 

static inline double slice_pam4(double x) {
    double threshold = 2.0 * QAM16_NORM;
    if (x >= threshold)   return  3.0 * QAM16_NORM;
    if (x >= 0.0)         return  1.0 * QAM16_NORM;
    if (x >= -threshold)  return -1.0 * QAM16_NORM;
    return -3.0 * QAM16_NORM;
}

SlicerResult qam16_slicer(double rx_i, double rx_q){
    return (SlicerResult){
        .ideal_i = slice_pam4(rx_i),
        .ideal_q = slice_pam4(rx_q)
    };
};

slicer_fn_t get_slicer_by_mod[] = {
    [MOD_BPSK]  = bpsk_slicer,
    [MOD_QPSK]  = qpsk_slicer,
    [MOD_16QAM] = qam16_slicer
};


static inline int32_t uint16_to_signed(uint16_t raw, uint8_t resolution) {
    // // Correção: half deve ser 2^(resolution-1), não (2^resolution - 1) / 2
    // uint32_t half = (1u << (resolution - 1));  // Para 16 bits: 32768 (não 32767!)
    // int32_t tmp = (int32_t)raw - (int32_t)half;
    
    uint32_t max_uint = ((1u << resolution) - 1u);
    uint32_t half = max_uint / 2u;
    int32_t tmp = (int32_t)raw - (int32_t)half;

    if (tmp < -32768) tmp = -32768;
    if (tmp >  32767) tmp =  32767;
    
    return tmp;
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

void demod_cfg_update(demod_t *demod,demod_config_t cfg){
    demod->config                  = cfg;
    demod->scale                   = config_get_scale_factor(&demod->config);
}

// ---------------------------------------------------------------------------
// SQI — Signal Quality Index (ported from modulations/metrics.py)
// ---------------------------------------------------------------------------

// MER normalization per modulation (Ku-band Gemini-4 thresholds)
//   BPSK:  floor= 5 dB, ceiling=20 dB
//   QPSK:  floor= 8 dB, ceiling=23 dB
//   16QAM: floor=15 dB, ceiling=32 dB
static inline double normalize_mer(double mer_db, modulation_type_t mod) {
    double floor_db, ceil_db;
    switch (mod) {
        case MOD_BPSK:  floor_db =  5.0; ceil_db = 20.0; break;
        case MOD_QPSK:  floor_db =  8.0; ceil_db = 23.0; break;
        case MOD_16QAM: floor_db = 15.0; ceil_db = 32.0; break;
        default:        floor_db =  8.0; ceil_db = 25.0; break;
    }
    double v = (mer_db - floor_db) / (ceil_db - floor_db) * 100.0;
    if (v < 0.0)   return 0.0;
    if (v > 100.0)  return 100.0;
    return v;
}

// C/N0 normalization (Ku-band VSAT: 50–80 dB-Hz)
static inline double normalize_cn0(double cn0_dbhz) {
    double v = (cn0_dbhz - 50.0) / (80.0 - 50.0) * 100.0;
    if (v < 0.0)   return 0.0;
    if (v > 100.0)  return 100.0;
    return v;
}

// Skew score from IQ imbalance (ported from skew.py:skew_score)
//   Phase:     0° → 100,  >=15° → 0
//   Amplitude: 0dB → 100, >=3dB → 0
static inline double calculate_skew_score(double amp_imb_db, double phase_imb_deg) {
    const double PHASE_MAX = 15.0;
    const double AMP_MAX   = 3.0;
    double ps = 100.0 - fabs(phase_imb_deg) * (100.0 / PHASE_MAX);
    double as = 100.0 - fabs(amp_imb_db) * (100.0 / AMP_MAX);
    if (ps < 0.0) ps = 0.0;
    if (as < 0.0) as = 0.0;
    return (ps + as) / 2.0;
}

// Compute SQI from sub-scores
//   SQI = 0.40*MER_n + 0.30*CN0_n + 0.20*Skew_n + 0.10*Stability_n
static inline double calculate_sqi(double mer_n, double cn0_n, double skew_n, double stability_n) {
    double v = 0.40 * mer_n + 0.30 * cn0_n + 0.20 * skew_n + 0.10 * stability_n;
    if (v < 0.0)   return 0.0;
    if (v > 100.0)  return 100.0;
    return v;
}

// Grade string from SQI value
static inline const char* sqi_to_grade(double sqi) {
    if (sqi >= 90.0) return "Excellent";
    if (sqi >= 75.0) return "Good";
    if (sqi >= 55.0) return "Fair";
    if (sqi >= 30.0) return "Poor";
    return "Critical";
}

#endif