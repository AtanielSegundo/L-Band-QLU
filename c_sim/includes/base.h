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
        
    } mcu2_config_t;

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