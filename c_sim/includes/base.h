#ifndef BASE_H

    #define BASE_H
    
    typedef enum {
        MOD_BPSK, 
        MOD_QPSK, 
        MOD_16QAM,
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

    // type := (symbol,sample) part := (signal,error)
    #define GET_AVG_POWER(demod,type,part) ((demod)->sum_ ## type ## _ ## part ## _power / (demod)->sample_count)

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
    


#endif