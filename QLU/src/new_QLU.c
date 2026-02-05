// HAL RELATED LIBRARIES

    #include <stdio.h>
    #include "pico/stdlib.h"
    #include "hardware/spi.h"
    #include "hardware/dma.h"

// END

// THIRD PARTY LIBRARIES

    #include "fonts/freeMono9.h"
    #include "fonts/freeMono6.h"

    #include "FreeRTOS.h"
    #include "task.h"
    #include "queue.h"
    #include "semphr.h"

//  END

// PROJECT INNER LIBRARIES

    #include "qlu_base.h"
    
    #define PROCESS_BLOCK_SIZE 256
    #include "qlu_demod.h"
    
    // #define SCREEN_IS_ST7735
    #define SCREEN_IS_SSD1306

    #include "screen.h"

// END

// QLU GLOBAL DEFINITIONS

    #define SPI_PORT spi0
    #define PIN_RX   16
    #define PIN_CSN  17
    #define PIN_SCK  18
    #define PIN_TX   19

    // --- Configurações de Buffer ---
    #define DMA_BUFFER_SIZE 4096 
    uint8_t __attribute__((aligned(DMA_BUFFER_SIZE))) rx_ring_buffer[DMA_BUFFER_SIZE];
    int dma_chan;

    #define DSP_QUEUE_LENGHT 10
    
    QueueHandle_t xDspQueue;
    QueueHandle_t xToScreenMetrics;
    QueueHandle_t xToWebMetrics;

// END


// QLU TEST DEFINITIONS

#define DEMOD_TEST
#undef DEMOD_TEST

#ifdef DEMOD_TEST
    #include "complex_bpsk_13.h"
    #include "complex_qam16_13.h"
    #include "complex_qpsk_13.h"
    #define COMPLEX_IQ  complex_qam16_13
    #define _CONCAT(a, b) a ## b
    #define CONCAT(a, b) _CONCAT(a, b)
    #define COMPLEX_IQ_META CONCAT(COMPLEX_IQ, _meta)

#endif

// END


// SPI STREAM HANDLING DEFINITIONS

    #define SYNC_BYTE_0 0xFE
    #define SYNC_BYTE_1 0xCA
    #define SYNC_BYTE_2 0xFE
    #define SYNC_BYTE_3 0xCA
    #define PAYLOAD_SIZE (256 * 4)

    typedef enum {
        STATE_SYNC_0, // Procurando 0xFE
        STATE_SYNC_1, // Procurando 0xCA
        STATE_SYNC_2, // Procurando 0xFE
        STATE_SYNC_3, // Procurando 0xCA
        STATE_PAYLOAD // Lendo dados
    } SyncState;

// END


void setup_spi_dma() {

    spi_deinit(SPI_PORT);

    spi_init(SPI_PORT, 4 * 1000 * 1000);
    
    spi_set_format(
            SPI_PORT,
            8,           // 8 bits por transferência
            SPI_CPOL_1,  // Clock polarity 0
            SPI_CPHA_1,  // Clock phase 0
            SPI_MSB_FIRST // MSB primeiro
        );
        
    spi_set_slave(SPI_PORT, true);
    
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CSN, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);

    // 2. Configura DMA
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);

    // Transfere bytes
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8); 
    // Lê sempre do mesmo reg SPI
    channel_config_set_read_increment(&c, false); 
    // Escreve incrementando na RAM para cada amostra
    channel_config_set_write_increment(&c, true); 
    // Sincroniza com RX do SPI
    channel_config_set_dreq(&c, spi_get_dreq(SPI_PORT, false)); 
    
    // Configura o Ring Buffer no DMA
    // 12 bits -> 4096 bytes (2^12)
    channel_config_set_ring(&c, true, 12); 

    dma_channel_configure(
        dma_chan,
        &c,
        rx_ring_buffer,        
        &spi_get_hw(SPI_PORT)->dr, 
        0xFFFFFFFF,            // Número de transferências (infinito/max)
        true                   // Iniciar imediatamente
    );

    printf("[INFO] DMA canal %d configurado e iniciado\n", dma_chan);
    printf("[INFO] Ring buffer: 4096 bytes em 0x%08X\n", (uint32_t)rx_ring_buffer);
}

void peripherals_setup(void){
    stdio_init_all();
    
    #ifdef SCREEN_IS_ST7735
        screen_init_setup(INITR_BLACKTAB,SCREEN_VERTICAL,&FreeMono6pt8b);
    #endif

    #ifdef SCREEN_IS_SSD1306
        ssd1306_i2c_setup();
        clear_screen();
    #endif
    
    setup_spi_dma();
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

// PROJECT TASKS 

// Defina o fator de suavização (0.0 a 1.0)
// 0.05 = Resposta lenta, muito estável (bom para números que pulam muito)
// 0.20 = Resposta rápida, menos estável
#define EMA_ALPHA 0.1

void StreamProcessToMetricsTask(void* params){
    IqBlock_t rxBlock;
    demod_t demod;
    QLUMetrics local_qlu_metrics = {0};

    demod_config_t cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_16QAM
    };
    config_calculate_derived(&cfg);
    demod_init(&demod,cfg);

    double smooth_snr = 0.0;
    double smooth_mer = 0.0;
    double smooth_evm = 0.0;
    double smooth_cn0 = 0.0;
    
    bool first_run = true;
    uint32_t sps_ceil = (uint32_t)ceil(demod.config.samples_per_symbol);
    uint32_t blocks_since_reset = 0;
    const uint32_t RESET_EVERY_N_BLOCKS = 5;

    double inst_mer = 0.0;
    double inst_snr = 0.0;
    double inst_evm = 0.0;
    double inst_cn0 = 0.0;

    float        signal_power      = 0.0f;
    uint16_t     raw_i             = 0u;
    uint16_t     raw_q             = 0u;
    int32_t      halfed_i          = 0;
    int32_t      halfed_q          = 0;
    double       fi                = 0.0;
    double       fq                = 0.0;
    SlicerResult result            = {0};
    double       rx_i              = 0.0;
    double       rx_q              = 0.0;
    double       error_i           = 0.0;
    double       error_q           = 0.0;
    double       ideal_power       = 0.0;
    double       error_power       = 0.0;

    double avg_sym_sig_power = 0.0;
    double avg_sym_err_power = 0.0;
    double avg_smp_sig       = 0.0;
    double avg_smp_err       = 0.0;

    while (true)
    {
        // FIXED: Single while with data processing AND queue sending
        while (xQueueReceive(xDspQueue, &rxBlock, 0) == pdPASS) {
            
            // 1. Process the block
            for(int k=0; k<PROCESS_BLOCK_SIZE; k++) {    
                raw_i = rxBlock.i_samples[k];
                raw_q = rxBlock.q_samples[k];
                halfed_i = uint16_to_signed(raw_i,demod.config.signal_resolution);
                halfed_q = uint16_to_signed(raw_q,demod.config.signal_resolution);
                fi = (double)halfed_i / demod.scale;
                fq = (double)halfed_q / demod.scale;                

                result = get_slicer_by_mod[demod.config.modulation](fi,fq);
                demod.sum_sample_signal_power += slicer_calculate_power(result.ideal_i,result.ideal_q);
                demod.sum_sample_error_power  += slicer_calculate_power(fi - result.ideal_i, fq - result.ideal_q);
                demod.sample_count++;

                demod.sym.acc_i += fi;
                demod.sym.acc_q += fq;
                demod.sym.count++;
                
                if (demod.sym.count >= sps_ceil){
                    rx_i = demod.sym.acc_i / (double)demod.sym.count;
                    rx_q = demod.sym.acc_q / (double)demod.sym.count;
                    result = get_slicer_by_mod[demod.config.modulation](rx_i,rx_q);
                    
                    demod.sum_symbol_signal_power += slicer_calculate_power(result.ideal_i,result.ideal_q);
                    demod.sum_symbol_error_power += slicer_calculate_power(rx_i - result.ideal_i, rx_q - result.ideal_q);
                    demod.symbol_count++;

                    demod.sym.acc_i = 0.0;
                    demod.sym.acc_q = 0.0;
                    demod.sym.count = 0;            
                }   
            }

            // 2. Calculate instantaneous metrics
            avg_sym_sig_power = (demod.symbol_count > 0) ? (demod.sum_symbol_signal_power / demod.symbol_count) : 0.0;
            avg_sym_err_power = (demod.symbol_count > 0) ? (demod.sum_symbol_error_power / demod.symbol_count) : 0.0;
            
            if (avg_sym_err_power > 0.000001 && avg_sym_sig_power > 0.000001) {
                inst_mer = 10.0 * log10(avg_sym_sig_power / avg_sym_err_power);
                inst_evm = sqrt(avg_sym_err_power / avg_sym_sig_power) * 100.0;
            } else {
                inst_mer = 0.0;
                inst_evm = 0.0;
            }

            avg_smp_sig = (demod.sample_count > 0) ? (demod.sum_sample_signal_power / demod.sample_count) : 0.0;
            avg_smp_err = (demod.sample_count > 0) ? (demod.sum_sample_error_power / demod.sample_count) : 0.0;
            
            if (avg_smp_err > 0.000001) {
                inst_snr = 10.0 * log10(avg_smp_sig / avg_smp_err);
            } else {
                inst_snr = 0.0;
            }

            inst_cn0 = inst_mer + 10.0 * log10(demod.config.symbol_rate_hz);
            
            if (first_run) {
                smooth_mer = inst_mer;
                smooth_snr = inst_snr;
                smooth_evm = inst_evm;
                smooth_cn0 = inst_cn0;
                first_run = false;
            } else {
                smooth_mer = (EMA_ALPHA * inst_mer) + ((1.0 - EMA_ALPHA) * smooth_mer);
                smooth_snr = (EMA_ALPHA * inst_snr) + ((1.0 - EMA_ALPHA) * smooth_snr);
                smooth_evm = (EMA_ALPHA * inst_evm) + ((1.0 - EMA_ALPHA) * smooth_evm);
                smooth_cn0 = (EMA_ALPHA * inst_cn0) + ((1.0 - EMA_ALPHA) * smooth_cn0);
            }

            // 3. Update metrics structure
            local_qlu_metrics.snr = smooth_snr;
            local_qlu_metrics.mer = smooth_mer;
            local_qlu_metrics.evm = smooth_evm;
            local_qlu_metrics.cn0 = smooth_cn0;

            blocks_since_reset++;
            if (blocks_since_reset >= RESET_EVERY_N_BLOCKS) {
                demod.sum_symbol_signal_power = 0.0;
                demod.sum_symbol_error_power  = 0.0;
                demod.symbol_count            = 0;
                demod.sum_sample_signal_power = 0.0;
                demod.sum_sample_error_power  = 0.0;
                demod.sample_count            = 0;
                blocks_since_reset = 0;
            }

            // 4. SEND TO QUEUES (NOW INSIDE THE LOOP!)
            
            xQueueOverwrite(xToScreenMetrics, &local_qlu_metrics);
            xQueueOverwrite(xToWebMetrics, &local_qlu_metrics);
            
            // #if (DSP_QUEUE_LENGHT == 1)
            //     xQueueOverwrite(xToScreenMetrics, &local_qlu_metrics);
            //     xQueueOverwrite(xToWebMetrics, &local_qlu_metrics);
            // #else
            //     xQueueSend(xToScreenMetrics, &local_qlu_metrics, 0);
            //     xQueueSend(xToWebMetrics, &local_qlu_metrics, 0);
            // #endif
        }
        
        // Small delay to prevent starving other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void UpdateScreenTask(void* params){
    QLUMetrics local_metrics = {0};
    
    while (true)
    {
        xQueueReceive(xToScreenMetrics,&local_metrics,portMAX_DELAY);

        #ifdef SCREEN_IS_ST7735
            write_boxed_metrics(5,6,ST77XX_BLUE,&m_qm);
        #endif

        #ifdef SCREEN_IS_SSD1306
            write_boxed_metrics(5,6,8,&local_metrics);
        #endif
    
        vTaskDelay(pdMS_TO_TICKS(250));    
    }
    
}

void Iq_from_payload_block(uint8_t* payload_buf, IqBlock_t* iq_buf, size_t block_size){
    for (size_t i = 0; i < block_size; i++) {
        // ATENÇÃO: Endianness corrigido aqui (Little Endian)
        // Buffer bruto: [LSB_I, MSB_I, LSB_Q, MSB_Q]
        uint8_t b0 = payload_buf[i*4 + 0];
        uint8_t b1 = payload_buf[i*4 + 1];
        uint8_t b2 = payload_buf[i*4 + 2];
        uint8_t b3 = payload_buf[i*4 + 3];

        // Reconstrói int16
        iq_buf->i_samples[i] = (uint16_t)((b0 << 8) | b1);
        iq_buf->q_samples[i] = (uint16_t)((b2 << 8) | b3);
    }
}

void SPISyncedStreamTask(void* params){
    static uint32_t tail_index     = 0;
    static SyncState current_state = STATE_SYNC_0;
    static uint16_t payload_idx    = 0;
    static uint8_t temp_payload_buffer[PAYLOAD_SIZE];
    
    IqBlock_t txBlock;

    printf("[Core 1] Iniciando Sincronizacao de Frame...\n");

    while (true)
    {
        uint32_t current_write_addr = (uint32_t)dma_hw->ch[dma_chan].write_addr;
        uint32_t head_index = current_write_addr - (uint32_t)rx_ring_buffer;
        
        if (head_index == tail_index) {
            vTaskDelay(1); 
            continue;
        }

        while(tail_index != head_index) {
            uint8_t byte = rx_ring_buffer[tail_index];
            tail_index = (tail_index + 1) % DMA_BUFFER_SIZE;

            // PROPER STATE MACHINE (like DEBUG version)
            switch (current_state) {
                case STATE_SYNC_0:
                    if (byte == SYNC_BYTE_0) current_state = STATE_SYNC_1;
                    break;
                    
                case STATE_SYNC_1:
                    if (byte == SYNC_BYTE_1) current_state = STATE_SYNC_2;
                    else current_state = STATE_SYNC_0;
                    break;
                    
                case STATE_SYNC_2:
                    if (byte == SYNC_BYTE_2) current_state = STATE_SYNC_3;
                    else current_state = STATE_SYNC_0;
                    break;
                    
                case STATE_SYNC_3:
                    if (byte == SYNC_BYTE_3) {
                        current_state = STATE_PAYLOAD;
                        payload_idx = 0; // CRITICAL: Reset here!
                    } else {
                        current_state = STATE_SYNC_0;
                    }
                    break;

                case STATE_PAYLOAD:
                    temp_payload_buffer[payload_idx++] = byte;

                    if (payload_idx >= PAYLOAD_SIZE) {
                        Iq_from_payload_block(temp_payload_buffer, &txBlock, PROCESS_BLOCK_SIZE);

                        #if (DSP_QUEUE_LENGHT == 1)
                            xQueueOverwrite(xDspQueue, &txBlock);
                        #else
                            xQueueSend(xDspQueue, &txBlock, 0);
                        #endif

                        current_state = STATE_SYNC_0;
                    }
                    break;
            }
        }
    }
}

void SPISyncedStreamTaskOld(void* params){
    static uint32_t tail_index = 0;
    static SyncState current_state = STATE_SYNC_0;
    static uint16_t payload_idx = 0;
    static uint8_t temp_payload_buffer[PAYLOAD_SIZE]; // Buffer temporário para montar o pacote
    
    IqBlock_t txBlock;

    printf("[Core 1] Iniciando Sincronizacao de Frame...\n");

    while(true) {
        uint32_t current_write_addr = (uint32_t)dma_hw->ch[dma_chan].write_addr;
        uint32_t head_index = current_write_addr - (uint32_t)rx_ring_buffer;
        
        if (head_index == tail_index) {
            vTaskDelay(1); 
            continue;
        }

        // Processa byte a byte do Ring Buffer
        while(tail_index != head_index) {
            uint8_t byte = rx_ring_buffer[tail_index];
            tail_index = (tail_index + 1) % DMA_BUFFER_SIZE;

            switch (current_state) {
                // --- MÁQUINA DE ESTADOS DE SINCRONIA ---
                case STATE_SYNC_0:
                    if (byte == SYNC_BYTE_0) current_state = STATE_SYNC_1;
                    break;
                case STATE_SYNC_1:
                    if (byte == SYNC_BYTE_1) current_state = STATE_SYNC_2;
                    else current_state = STATE_SYNC_0; // Reset se falhar
                    break;
                case STATE_SYNC_2:
                    if (byte == SYNC_BYTE_2) current_state = STATE_SYNC_3;
                    else current_state = STATE_SYNC_0;
                    break;
                case STATE_SYNC_3:
                    if (byte == SYNC_BYTE_3) {
                        current_state = STATE_PAYLOAD;
                        payload_idx = 0; // Preparar para ler payload
                    } else {
                        current_state = STATE_SYNC_0;
                    }
                    break;

                // --- ESTADO DE LEITURA DE DADOS ---
                case STATE_PAYLOAD:
                    temp_payload_buffer[payload_idx++] = byte;

                    if (payload_idx >= PAYLOAD_SIZE) {
                        // Pacote completo recebido! Agora processamos.
                        
                        for (int i = 0; i < 256; i++) {
                            // ATENÇÃO: Endianness corrigido aqui (Little Endian)
                            // Buffer bruto: [LSB_I, MSB_I, LSB_Q, MSB_Q]
                            uint8_t b0 = temp_payload_buffer[i*4 + 0];
                            uint8_t b1 = temp_payload_buffer[i*4 + 1];
                            uint8_t b2 = temp_payload_buffer[i*4 + 2];
                            uint8_t b3 = temp_payload_buffer[i*4 + 3];

                            // Reconstrói int16
                            txBlock.i_samples[i] = (uint16_t)((b0 << 8) | b1);
                            txBlock.q_samples[i] = (uint16_t)((b2 << 8) | b3);
                        }

                        // Envia para a fila de DSP
                        #if (DSP_QUEUE_LENGHT == 1)
                            xQueueOverwrite(xDspQueue, &txBlock);
                        #else
                            xQueueSend(xDspQueue, &txBlock, 0);
                        #endif

                        // Volta a procurar o próximo header
                        current_state = STATE_SYNC_0;
                    }
                    break;
            }
        }

        vTaskDelay(1);
    }
}

void SPISyncedStreamTask_DEBUG(void* params){
    static uint32_t tail_index = 0;
    static SyncState current_state = STATE_SYNC_0;
    static uint16_t payload_idx = 0;
    static uint8_t temp_payload_buffer[PAYLOAD_SIZE];
    
    IqBlock_t txBlock;

    printf("[Core 1] DEBUG: Iniciando Sincronizacao de Frame...\n");
    
    // ═══════════════════════════════════════════════════════════
    // VARIÁVEIS DE DEBUG
    // ═══════════════════════════════════════════════════════════
    uint32_t bytes_received_total = 0;
    uint32_t last_print_time = 0;
    uint32_t sync_attempts = 0;
    uint8_t last_10_bytes[10] = {0};
    uint8_t last_byte_idx = 0;
    bool first_byte_received = false;
    // ═══════════════════════════════════════════════════════════

    while(true) {
        uint32_t current_write_addr = (uint32_t)dma_hw->ch[dma_chan].write_addr;
        uint32_t head_index = current_write_addr - (uint32_t)rx_ring_buffer;
        
        // ═══════════════════════════════════════════════════════════
        // DEBUG: Print do estado do DMA a cada segundo
        // ═══════════════════════════════════════════════════════════
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_print_time >= 1000) {
            printf("[DEBUG] DMA Status:\n");
            printf("  Write addr: 0x%08X\n", current_write_addr);
            printf("  Head index: %u\n", head_index);
            printf("  Tail index: %u\n", tail_index);
            printf("  Bytes received (total): %u\n", bytes_received_total);
            printf("  Current state: %d\n", current_state);
            printf("  Sync attempts: %u\n", sync_attempts);
            
            if (bytes_received_total > 0) {
                printf("  Last 10 bytes: ");
                for(int i=0; i<10; i++) {
                    printf("%02X ", last_10_bytes[i]);
                }
                printf("\n");
            } else {
                printf("  ⚠️  NO BYTES RECEIVED YET!\n");
                printf("  Check:\n");
                printf("    - SPI connections (MOSI, CLK, CS, GND)\n");
                printf("    - ESP32 is transmitting\n");
                printf("    - DMA is configured correctly\n");
            }
            printf("\n");
            last_print_time = now;
        }
        // ═══════════════════════════════════════════════════════════
        
        if (head_index == tail_index) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Aumentado para 10ms no debug
            continue;
        }

        // Processa byte a byte do Ring Buffer
        while(tail_index != head_index) {
            uint8_t byte = rx_ring_buffer[tail_index];
            tail_index = (tail_index + 1) % DMA_BUFFER_SIZE;
            
            // ═══════════════════════════════════════════════════════════
            // DEBUG: Primeiro byte recebido
            // ═══════════════════════════════════════════════════════════
            if (!first_byte_received) {
                printf("[DEBUG] ✓ FIRST BYTE RECEIVED: 0x%02X\n", byte);
                printf("  SPI is working! Data is arriving.\n\n");
                first_byte_received = true;
            }
            // ═══════════════════════════════════════════════════════════
            
            bytes_received_total++;
            
            // Guarda últimos 10 bytes para debug
            last_10_bytes[last_byte_idx] = byte;
            last_byte_idx = (last_byte_idx + 1) % 10;

            switch (current_state) {
                case STATE_SYNC_0:
                    if (byte == SYNC_BYTE_0) {
                        current_state = STATE_SYNC_1;
                        sync_attempts++;
                        // DEBUG: Possível início de sync
                        printf("[DEBUG] Sync attempt #%u: Found 0x%02X\n", 
                               sync_attempts, byte);
                    }
                    break;
                    
                case STATE_SYNC_1:
                    if (byte == SYNC_BYTE_1) {
                        current_state = STATE_SYNC_2;
                        printf("[DEBUG]   → Found 0x%02X (2/4)\n", byte);
                    } else {
                        printf("[DEBUG]   ✗ Expected 0x%02X, got 0x%02X\n", 
                               SYNC_BYTE_1, byte);
                        current_state = STATE_SYNC_0;
                    }
                    break;
                    
                case STATE_SYNC_2:
                    if (byte == SYNC_BYTE_2) {
                        current_state = STATE_SYNC_3;
                        printf("[DEBUG]   → Found 0x%02X (3/4)\n", byte);
                    } else {
                        printf("[DEBUG]   ✗ Expected 0x%02X, got 0x%02X\n", 
                               SYNC_BYTE_2, byte);
                        current_state = STATE_SYNC_0;
                    }
                    break;
                    
                case STATE_SYNC_3:
                    if (byte == SYNC_BYTE_3) {
                        current_state = STATE_PAYLOAD;
                        payload_idx = 0;
                        printf("[DEBUG]   → Found 0x%02X (4/4) ✓ SYNC LOCKED!\n", byte);
                        printf("[DEBUG]   → Starting payload reception...\n");
                    } else {
                        printf("[DEBUG]   ✗ Expected 0x%02X, got 0x%02X\n", 
                               SYNC_BYTE_3, byte);
                        current_state = STATE_SYNC_0;
                    }
                    break;

                case STATE_PAYLOAD:
                    temp_payload_buffer[payload_idx++] = byte;

                    if (payload_idx >= PAYLOAD_SIZE) {
                        printf("[DEBUG] ✓ FULL PACKET RECEIVED! Processing...\n");
                        
                        // Processa payload
                        for (int i = 0; i < 256; i++) {
                            uint8_t b0 = temp_payload_buffer[i*4 + 0];
                            uint8_t b1 = temp_payload_buffer[i*4 + 1];
                            uint8_t b2 = temp_payload_buffer[i*4 + 2];
                            uint8_t b3 = temp_payload_buffer[i*4 + 3];

                            txBlock.i_samples[i] = (uint16_t)((b0 << 8) | b1);
                            txBlock.q_samples[i] = (uint16_t)((b2 << 8) | b3);
                        }
                        
                        // Debug: Print primeiras amostras
                        printf("[DEBUG] First 3 I/Q pairs:\n");
                        for(int i=0; i<3; i++) {
                            printf("  [%d] I=%u Q=%u\n", 
                                   i, txBlock.i_samples[i], txBlock.q_samples[i]);
                        }

                        // Envia para fila
                        if (xQueueSend(xDspQueue, &txBlock, 0) == pdTRUE) {
                            printf("[DEBUG] ✓ Packet sent to DSP queue!\n\n");
                        } else {
                            printf("[DEBUG] ✗ DSP queue full, packet dropped!\n\n");
                        }

                        current_state = STATE_SYNC_0;
                    } else {
                        // Print progresso a cada 256 bytes
                        if ((payload_idx % 256) == 0) {
                            printf("[DEBUG]   Payload progress: %u/%u bytes\n", 
                                   payload_idx, PAYLOAD_SIZE);
                        }
                    }
                    break;
            }
        }
    }
}

#ifdef DEMOD_TEST

void SPITestStreamTask(void* params){
    IqBlock_t txBlock;
    
    printf("[Core 0] Test Acquisition Task Iniciada\n");
    const size_t total_values = COMPLEX_IQ_META.n_samples;  // Total de valores no array
    size_t read_index = 0U;  // Índice de leitura no array intercalado
    
    while(true) {
        // Lê PROCESS_BLOCK_SIZE pares I/Q do array intercalado
        for(size_t i = 0; i < PROCESS_BLOCK_SIZE; i++){
            // Cada par I/Q ocupa 2 posições consecutivas
            size_t base_idx = read_index + (2 * i);
            txBlock.i_samples[i] = (uint16_t)(COMPLEX_IQ[(base_idx + 0) % total_values]);  // I
            txBlock.q_samples[i] = (uint16_t)(COMPLEX_IQ[(base_idx + 1) % total_values]);  // Q
        }
        
        // Avança o índice pelo número de VALORES lidos (não pares!)
        // Lemos 256 pares = 512 valores
        read_index = (read_index + PROCESS_BLOCK_SIZE * 2) % total_values;
        
        if (xQueueSend(xDspQueue, &txBlock, 0) != pdTRUE) {
            // printf("Drop!\n"); 
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

#endif

// END

int main()
{
    peripherals_setup();
    sleep_ms(2000);
        printf("[INFO] SYSTEM STARTING...\n");    
    sleep_ms(2000);
        printf("[INFO] SYSTEM STARTED...\n");

    xDspQueue        = xQueueCreate(DSP_QUEUE_LENGHT, sizeof(IqBlock_t));
    
    xToScreenMetrics = xQueueCreate(1, sizeof(QLUMetrics));
    xToWebMetrics    = xQueueCreate(1, sizeof(QLUMetrics));

    // Core 1: Gerencia o buffer DMA e monta os pacotes para o processamento
    #ifdef DEMOD_TEST
        xTaskCreateAffinitySet(
            SPITestStreamTask, 
            "SPI Test Stream Handling Task", 
            4096, 
            NULL, 
            10,      
            RP2040_CORE_1,
            NULL
        );
    #else
        xTaskCreateAffinitySet(
            SPISyncedStreamTask, 
            "SPI Stream Handling Task", 
            4096, 
            NULL, 
            20,      
            RP2040_CORE_1,
            NULL
        );
    #endif

    // Core 0: Gerencia o Screen e processa os dados recebidos na fila
    xTaskCreateAffinitySet(
        StreamProcessToMetricsTask,
        "Stream Process To Metrics",
        4096,
        NULL,
        5,
        RP2040_CORE_0,
        NULL
    );

    xTaskCreateAffinitySet(
        UpdateScreenTask,
        "Screen Update Task",
        1024,
        NULL,
        10,
        RP2040_CORE_0,
        NULL
    );

    vTaskStartScheduler();

    while (true) {
        tight_loop_contents();
    }
}


// EXAMPLES

#ifdef SCREEN_IS_ST7735
    int ST7735_EXAMPLE_1(){
        stdio_init_all();

        // ST7735 SETUP
        screen_init_setup(INITR_BLACKTAB,SCREEN_VERTICAL,&FreeMono6pt8b);
        QLUMetrics m_qm = {.mer=18.0,.cn0=89.0,.evm=0.28};
        QRCodeGenerated qr_gen = {0};

        generate_qr_code(&qr_gen,&SMALL_QR_CONFIG,"http://QLU/dashboard");
        fill_with_qr_code_bottom(&qr_gen);    

        while (true) {
            m_qm.mer += 0.1; 
            m_qm.cn0 += 0.05; 
            m_qm.evm += 0.01; 
            write_boxed_metrics(5,6,ST77XX_BLUE,&m_qm);
            sleep_ms(500);
        }
    }
#endif

// END