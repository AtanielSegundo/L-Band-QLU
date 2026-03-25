// HAL RELATED LIBRARIES

    #include <stdio.h>
    #include "pico/stdlib.h"
    #include "pico/cyw43_arch.h"
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

    #include "websocket.h"

// END

// QLU GLOBAL DEFINITIONS

    #define SPI_PORT_FREQUENCY (1 * 1000 * 1000)
    #define SPI_PORT spi0
    #define PIN_RX   16
    #define PIN_CSN  17
    #define PIN_SCK  18
    #define PIN_TX   19

    // --- Configurações de Buffer ---
    #define DMA_BUFFER_SIZE 4096 
    uint8_t __attribute__((aligned(DMA_BUFFER_SIZE))) rx_ring_buffer[DMA_BUFFER_SIZE];
    int dma_chan;

    #define DSP_QUEUE_LENGHT 1
    
    QueueHandle_t xDspQueue;
    QueueHandle_t xToScreenMetrics;
    QueueHandle_t xToWebMetrics;
    QueueHandle_t xDemodConfig;

    #define WEB_REF_SAMPLES_CNT (15U)

    typedef struct {
        QLUMetrics m;
        double f_I[WEB_REF_SAMPLES_CNT];
        double f_Q[WEB_REF_SAMPLES_CNT];
    } WebMetrics;

    typedef enum {
        REQUEST_INFO,
        REQUEST_CURRENT,
        REQUEST_UPDATE_YOURS,
        REQUEST_COUNT
    } WS_CONFIG_REQUEST_TYPE;

    typedef struct {
        WS_CONFIG_REQUEST_TYPE T;
        modulation_type_t mod;
        ws_client_tpcb ws_client;
        double roll_off;
    } ConfigRequest;
    

    QueueHandle_t xConfigRequest;

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


// SERVER RELATED DEFINITIONS

    #include "http.h"

    #include "routes/index.h"
    #include "routes/ws_only.h"

    SemaphoreHandle_t lwip_mutex = NULL;
    
    static ip4_addr_t cyw43_ip = {0};
    
    typedef struct {char* ssid; char* password} WifiNetwork;
    
    #define ACCESS_POINT_MODE

    #ifdef ACCESS_POINT_MODE

        #include "ap.h"
        #include "pico/unique_id.h"

        #define AP_SSID "QLU"
        #define AP_PWD "vsatqlu123"
        #define SITE_NAME "qlu.local"
        
        void setup_cyw43_network(void){
            pico_unique_board_id_t board_id;
            pico_get_unique_board_id(&board_id);

            char temp[64];

            sprintf(temp, "%s_%02X%02X%02X%02X%02X%02X%02X%02X",
                    AP_SSID,
                    board_id.id[0], board_id.id[1], board_id.id[2],
                    board_id.id[3], board_id.id[4], board_id.id[5],
                    board_id.id[6], board_id.id[7]
            );

            setup_access_point(temp, AP_PWD, SITE_NAME);
        }
    
    #else 
        #define CONNECTION_MAX_TRIES (3U)
        
        const WifiNetwork NETWORKS_TO_CONNECT[] ={
            (WifiNetwork){"F_Santos_2_4","Asss060325"},
            (WifiNetwork){"mi_local","senha123"},
        };
        const size_t NETWORK_TO_CONNECT_COUNT = count_of(NETWORKS_TO_CONNECT);

        void setup_cyw43_network(void){
            cyw43_arch_init();
            cyw43_arch_enable_sta_mode();
            bool connected = false;
            for(size_t i=0; i < NETWORKS_TO_CONNECT; i++){
                for(int tries = 0; tries < CONNECTION_MAX_TRIES; tries++){
                     if (cyw43_arch_wifi_connect_timeout_ms(
                        NETWORKS_TO_CONNECT[i].ssid,
                        NETWORKS_TO_CONNECT[i].password, 
                        CYW43_AUTH_WPA2_AES_PSK,
                        30000) != 0){
                            printf("[INFO] Tentando Reconectar a %s: tentativa %d\n",NETWORKS_TO_CONNECT[i].ssid,(tries+1));
                            sleep_ms(500);
                        }
                    else {
                        connected = true;
                        break;
                    }
                }
                if (connected){
                    printf("[INFO] Conectado a %s\n",NETWORKS_TO_CONNECT[i].ssid);
                    break;
                }
            };

            if (connected){
                struct netif *sta_if = &cyw43_state.netif[CYW43_ITF_STA];
                while (ip4_addr_isany_val(*netif_ip4_addr(sta_if))) {
                    sleep_ms(100);
                }
                cyw43_ip = *netif_ip4_addr(sta_if);
            }
        }

    #endif

// END


void setup_spi_dma() {

    spi_deinit(SPI_PORT);

    spi_init(SPI_PORT, SPI_PORT_FREQUENCY);
    
    spi_set_format(
            SPI_PORT,
            8,            // 8 bits por transferência
            SPI_CPOL_1,   // Clock polarity 0
            SPI_CPHA_1,   // Clock phase 0
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

    setup_cyw43_network();

    printf("[INFO] Device IP: %s\n", ip4addr_ntoa(&cyw43_ip));
}

// PROJECT TASKS 

// Defina o fator de suavização (0.0 a 1.0)
// 0.05 = Resposta lenta, muito estável (bom para números que pulam muito)
// 0.20 = Resposta rápida, menos estável
#define EMA_ALPHA 0.1

void StreamProcessToMetricsTask(void* params){
    static IqBlock_t  rxBlock;
    static QLUMetrics local_qlu_metrics = {0};
    static WebMetrics local_web_metrics = {0};
    
    demod_t demod;

    demod_config_t cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_16QAM
    };

    config_calculate_derived(&cfg);
    
    xQueueOverwrite(xDemodConfig,&cfg);
    
    demod_init(&demod,cfg);

    double smooth_snr = 0.0;
    double smooth_mer = 0.0;
    double smooth_evm = 0.0;
    double smooth_cn0 = 0.0;
    double smooth_stability = 100.0;
    double smooth_skew = 100.0;
    double smooth_sqi  = 0.0;

    bool first_run = true;
    uint32_t blocks_since_reset = 0;
    const uint32_t RESET_EVERY_N_BLOCKS = 5;
    const uint32_t SKEW_EVERY_N_BLOCKS  = 20;  // skew needs more samples for stability
    uint32_t skew_blocks = 0;

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

    // IQ imbalance accumulators (for skew measurement)
    double sum_I_sq = 0.0;
    double sum_Q_sq = 0.0;
    double sum_IQ   = 0.0;
    uint32_t iq_imb_count = 0;

    // Stability: ring buffer of block powers to compute CV
    #define STABILITY_WINDOW_CNT 16
    double power_history[STABILITY_WINDOW_CNT];
    uint32_t power_hist_idx = 0;
    uint32_t power_hist_filled = 0;
    double block_rx_power_sum = 0.0;
    
    uint32_t sps_ceil = (uint32_t)ceil(demod.config.samples_per_symbol);
    
    while (true)
    {
        if (xQueueReceive(xDemodConfig,&cfg,0) == pdPASS){
            config_calculate_derived(&cfg);
            demod_cfg_update(&demod, cfg);

            sps_ceil = (uint32_t)ceil(demod.config.samples_per_symbol);

            // Full reset — purge all stale data from previous modulation
            demod.sym.acc_i = 0;
            demod.sym.acc_q = 0;
            demod.sym.count = 0;
            demod.sum_symbol_signal_power = 0.0;
            demod.sum_symbol_error_power  = 0.0;
            demod.symbol_count            = 0;
            demod.sum_sample_signal_power = 0.0;
            demod.sum_sample_error_power  = 0.0;
            demod.sample_count            = 0;
            sum_I_sq       = 0.0;
            sum_Q_sq       = 0.0;
            sum_IQ         = 0.0;
            iq_imb_count   = 0;
            block_rx_power_sum = 0.0;
            power_hist_filled  = 0;
            power_hist_idx     = 0;
            blocks_since_reset = 0;
            skew_blocks        = 0;
            smooth_skew        = 100.0;  // reset sentinel for EMA seed
            first_run = true;
        }
        
        // FIXED: Single while with data processing AND queue sending
        if (xQueueReceive(xDspQueue, &rxBlock, 0) == pdPASS) {
            
            // 1. Process the block
            for(int k=0; k<PROCESS_BLOCK_SIZE; k++) {    
                raw_i = rxBlock.i_samples[k];
                raw_q = rxBlock.q_samples[k];
                halfed_i = uint16_to_signed(raw_i,demod.config.signal_resolution);
                halfed_q = uint16_to_signed(raw_q,demod.config.signal_resolution);
                fi = (double)halfed_i / demod.scale;
                fq = (double)halfed_q / demod.scale;                
                
                if( k % WEB_REF_SAMPLES_CNT == 0 ){
                    local_web_metrics.f_I[(k / WEB_REF_SAMPLES_CNT) % WEB_REF_SAMPLES_CNT] = fi;
                    local_web_metrics.f_Q[(k / WEB_REF_SAMPLES_CNT) % WEB_REF_SAMPLES_CNT] = fq;
                }

                // Stability: accumulate received power (before slicer, cheap)
                block_rx_power_sum += fi * fi + fq * fq;

                result = get_slicer_by_mod[demod.config.modulation](fi,fq);
                demod.sum_sample_signal_power += slicer_calculate_power(result.ideal_i,result.ideal_q);
                demod.sum_sample_error_power  += slicer_calculate_power(fi - result.ideal_i, fq - result.ideal_q);
                demod.sample_count++;

                // Skew: accumulate ERROR vector I²/Q²/IQ (after slicer)
                // Using error vectors instead of raw signal fixes BPSK:
                // raw I²>>Q² for BPSK even with no skew, but error_I²≈error_Q² (AWGN is isotropic)
                {
                    double ei = fi - result.ideal_i;
                    double eq = fq - result.ideal_q;
                    sum_I_sq += ei * ei;
                    sum_Q_sq += eq * eq;
                    sum_IQ   += ei * eq;
                    iq_imb_count++;
                }

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
            
            avg_smp_sig = (demod.sample_count > 0) ? (demod.sum_sample_signal_power / demod.sample_count) : 0.0;
            avg_smp_err = (demod.sample_count > 0) ? (demod.sum_sample_error_power / demod.sample_count) : 0.0;

            if (avg_sym_err_power > 0.000001 && avg_sym_sig_power > 0.000001) {
                inst_mer = 10.0 * log10(avg_sym_sig_power / avg_sym_err_power);
                // inst_evm = sqrt(avg_sym_err_power / avg_sym_sig_power) * 100.0;
                inst_evm = sqrt(avg_smp_err / avg_smp_sig) * 100.0;
            } else {
                inst_mer = 0.0;
                inst_evm = 0.0;
            }
            
            if (avg_smp_err > 0.000001) {
                inst_snr = 10.0 * log10(avg_smp_sig / avg_smp_err);
            } else {
                inst_snr = 0.0;
            }

            // inst_cn0 = inst_mer + 10.0 * log10(demod.config.symbol_rate_hz);
            inst_cn0 = inst_snr + 10.0 * log10(demod.config.symbol_rate_hz);

            // 2b. Stability: store block power (cheap — just an array write)
            {
                double block_avg_power = block_rx_power_sum / PROCESS_BLOCK_SIZE;
                power_history[power_hist_idx] = block_avg_power;
                power_hist_idx = (power_hist_idx + 1) % STABILITY_WINDOW_CNT;
                if (power_hist_filled < STABILITY_WINDOW_CNT) power_hist_filled++;
                block_rx_power_sum = 0.0;
            }

            // EMA for MER/EVM/SNR/CN0 every block (cheap, no transcendentals beyond the existing log10/sqrt above)
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

            // 2c. MER/EVM accumulators reset every N blocks
            blocks_since_reset++;
            if (blocks_since_reset >= RESET_EVERY_N_BLOCKS) {
                // Stability from power history CV (sqrt)
                if (power_hist_filled >= 2) {
                    double sum_p = 0.0, sum_p2 = 0.0;
                    for (uint32_t w = 0; w < power_hist_filled; w++) {
                        sum_p  += power_history[w];
                        sum_p2 += power_history[w] * power_history[w];
                    }
                    double mean_p = sum_p / power_hist_filled;
                    double var_p  = (sum_p2 / power_hist_filled) - (mean_p * mean_p);
                    if (var_p < 0.0) var_p = 0.0;
                    double cv = sqrt(var_p) / (mean_p + 1e-12);
                    const double CV_CEILING = 0.30;
                    smooth_stability = (1.0 - cv / CV_CEILING) * 100.0;
                    if (smooth_stability < 0.0)   smooth_stability = 0.0;
                    if (smooth_stability > 100.0)  smooth_stability = 100.0;
                }

                // Reset MER/EVM accumulators (short window)
                demod.sum_symbol_signal_power = 0.0;
                demod.sum_symbol_error_power  = 0.0;
                demod.symbol_count            = 0;
                demod.sum_sample_signal_power = 0.0;
                demod.sum_sample_error_power  = 0.0;
                demod.sample_count            = 0;
                blocks_since_reset            = 0;
            }

            // 2d. Skew — longer accumulation window (20 blocks = ~5120 samples)
            //     More samples → stable pwr_I/pwr_Q ratio, especially at high SNR
            skew_blocks++;
            if (skew_blocks >= SKEW_EVERY_N_BLOCKS && iq_imb_count > 0) {
                double pwr_I = sum_I_sq / iq_imb_count;
                double pwr_Q = sum_Q_sq / iq_imb_count;
                double cross = sum_IQ   / iq_imb_count;

                double amp_imb_db = 10.0 * log10((pwr_I + 1e-12) / (pwr_Q + 1e-12));
                double denom_skew = sqrt(pwr_I * pwr_Q) + 1e-12;
                double arg   = 2.0 * cross / denom_skew;
                if (arg >  1.0) arg =  1.0;
                if (arg < -1.0) arg = -1.0;
                double phase_imb_deg = asin(arg) * (180.0 / M_PI);
                double inst_skew = calculate_skew_score(amp_imb_db, phase_imb_deg);

                // EMA smooth skew like other metrics
                if (smooth_skew >= 99.9) {
                    smooth_skew = inst_skew;  // first measurement
                } else {
                    smooth_skew = (EMA_ALPHA * inst_skew) + ((1.0 - EMA_ALPHA) * smooth_skew);
                }

                // Decay accumulators instead of hard reset (keeps history, reduces variance)
                const double DECAY = 0.3;  // keep 30% of old accumulation
                sum_I_sq     *= DECAY;
                sum_Q_sq     *= DECAY;
                sum_IQ       *= DECAY;
                iq_imb_count  = (uint32_t)(iq_imb_count * DECAY);
                skew_blocks   = 0;
            }

            // 2e. SQI from latest smoothed values (cheap — just multiplies and adds)
            {
                double mer_n = normalize_mer(smooth_mer, demod.config.modulation);
                double cn0_n = normalize_cn0(smooth_cn0);
                smooth_sqi = calculate_sqi(mer_n, cn0_n, smooth_skew, smooth_stability);
            }

            // 3. Update metrics structure
            local_qlu_metrics.snr = smooth_snr;
            local_qlu_metrics.mer = smooth_snr;
            local_qlu_metrics.evm = smooth_evm;
            local_qlu_metrics.cn0 = smooth_cn0;
            local_qlu_metrics.stability  = smooth_stability;
            local_qlu_metrics.skew_score = smooth_skew;
            local_qlu_metrics.sqi        = smooth_sqi;

            local_web_metrics.m.snr = smooth_snr;
            local_web_metrics.m.mer = smooth_snr;
            local_web_metrics.m.evm = smooth_evm;
            local_web_metrics.m.cn0 = smooth_cn0;
            local_web_metrics.m.stability  = smooth_stability;
            local_web_metrics.m.skew_score = smooth_skew;
            local_web_metrics.m.sqi        = smooth_sqi;

            // 4. SEND TO QUEUES (NOW INSIDE THE LOOP!)
            
            xQueueOverwrite(xToScreenMetrics, &local_qlu_metrics);
            xQueueOverwrite(xToWebMetrics, &local_web_metrics);
            
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


#define WS_JSON_BUF_SIZE 768

void WebMetricsTojson(char* json_buffer, WebMetrics* metrics, size_t* json_lenght) {
    const char* grade = sqi_to_grade(metrics->m.sqi);

    int offset = snprintf(json_buffer, WS_JSON_BUF_SIZE,
        "{\"snr\":%.2f,\"mer\":%.2f,\"evm\":%.2f,\"cn0\":%.2f,"
        "\"stability\":%.1f,\"skew\":%.1f,\"sqi\":%.1f,\"grade\":\"%s\","
        "\"points\":[",
        metrics->m.snr, metrics->m.mer, metrics->m.evm, metrics->m.cn0,
        metrics->m.stability, metrics->m.skew_score, metrics->m.sqi, grade);

    for (uint32_t i = 0; i < WEB_REF_SAMPLES_CNT; i++) {
        int written = snprintf(json_buffer + offset, WS_JSON_BUF_SIZE - offset,
            "{\"i\":%.3f,\"q\":%.3f}%s",
            metrics->f_I[i],
            metrics->f_Q[i],
            (i < WEB_REF_SAMPLES_CNT - 1) ? "," : "");

        offset += written;

        if (offset >= (WS_JSON_BUF_SIZE - 2)) break;
    }

    int final_bits = snprintf(json_buffer + offset, WS_JSON_BUF_SIZE - offset, "]}");
    offset += final_bits;

    if (json_lenght != NULL) {
        *json_lenght = (size_t)offset;
    }
}

void WebStreamMetricsTask(void* parameters){
    WebMetrics local_metrics = {0};
    static char ws_stream_json[WS_JSON_BUF_SIZE];
    size_t ws_stream_lenght = 0;

    for(;;){
        xQueueReceive(xToWebMetrics,&local_metrics,0);
        WebMetricsTojson(ws_stream_json,&local_metrics,&ws_stream_lenght);
        if (xSemaphoreTake(lwip_mutex, portMAX_DELAY)){
            ws_send_to_all_clients("/ws/stream",WS_OP_TEXT,ws_stream_json,ws_stream_lenght);
            xSemaphoreGive(lwip_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(350));
    }   
};

static char handle_msg_buffer[512];
void handle_text_requests(ws_client_tpcb wc, uint8_t* ws_msg, size_t ws_msg_len){
    const char* route = ws_get_client_route(wc);
    static char msg_buffer[512];
    static ConfigRequest req = {0};

    // Verifica se a rota é correta
    if(strcmp(route, "/ws/config") == 0){
        
        size_t safe_len = (ws_msg_len < 511) ? ws_msg_len : 511;
        memcpy(msg_buffer, ws_msg, safe_len);
        msg_buffer[safe_len] = '\0';

        req.ws_client = wc;
        bool valid_request = false;

        if (strstr(msg_buffer, "INFO") != NULL) {
            req.T = REQUEST_INFO;
            valid_request = true;
        }
        
        else if (strstr(msg_buffer, "CURRENT") != NULL) {
            req.T = REQUEST_CURRENT;
            valid_request = true;
        }

        else if (strstr(msg_buffer, "UPDATE_YOURS") != NULL) {
            req.T = REQUEST_UPDATE_YOURS;
            valid_request = true;

            char* mod_key = strstr(msg_buffer, "\"modulation\"");
            if (mod_key) {
                char* val_start = strchr(mod_key, ':');
                if (val_start) {
                    // CORREÇÃO: Adicionado +1 para pular o ':' 
                    // Antes: atoi(val_start) -> lia ":" -> retornava 0
                    // Agora: atoi(val_start + 1) -> lê " 2" -> retorna 2
                    // printf("[DEBUG] Mode Key STR: %s\n",val_start+1);
                    req.mod = (modulation_type_t)atoi(val_start + 1);
                    // printf("[DEBUG] MODE KEY PARSED %s\n",get_modulation_name[req.mod]);
                }
            }

            char* roll_key = strstr(msg_buffer, "\"roll_off\"");
            if (roll_key) {
                char* val_start = strchr(roll_key, ':');
                if (val_start) {
                    // Já estava correto, mas mantenha o +1 aqui também
                    // printf("[DEBUG] ROLL OFF STR: %s\n",val_start);
                    req.roll_off = atof(val_start + 1);
                    // printf("[DEBUG] ROLL OFF PARSED: %lf\n",req.roll_off);
                }
            }

        }
        if (valid_request) {
            xQueueSend(xConfigRequest, &req, pdMS_TO_TICKS(10));
        }
    };
}

void renderInfoRequestJson(char* json_buf, size_t* json_len, size_t buf_len){
    int offset = 0;
    offset += snprintf(json_buf, buf_len, "{\"options\": [");
    
    for (size_t i = 0; i < MOD_NUM_MODULATIONS; i++) {
        if (i > 0) {
            offset += snprintf(json_buf + offset, buf_len - offset, ",");
        }
        
        offset += snprintf(json_buf + offset, buf_len - offset, 
                           "{\"name\": \"%s\", \"val\": %d}", 
                           get_modulation_name[i], i);
    }
    
    offset += snprintf(json_buf + offset, buf_len - offset, "]}");
    *json_len = offset;
};

void WebConfigProcessTask(void* parameters){
    // CORREÇÃO 3: Inicializa com os mesmos defaults do DSP para não mostrar 0 no início
    demod_config_t local_cfg = {
        .link_bw_hz = 10e6,
        .sampling_rate_hz = 20e6,
        .roll_off = 0.25,
        .signal_resolution = 16,
        .modulation = MOD_16QAM
    };
    config_calculate_derived(&local_cfg);

    ConfigRequest local_cfg_request = {0};
    static char ws_config_json[512];
    size_t ws_config_lenght = 0;

    ws_add_on_text_handler(handle_text_requests);

    for(;;){
        // CORREÇÃO 4: Removemos a leitura de xDemodConfig aqui. 
        // Esta task mantém o estado em 'local_cfg' e apenas ENVIA para o DSP.
        // Se ela ler a fila, rouba a configuração da task de DSP.
        
        if (xQueueReceive(xConfigRequest, &local_cfg_request, 0) == pdPASS){
            if (xSemaphoreTake(lwip_mutex, portMAX_DELAY)){
                
                switch (local_cfg_request.T)
                {
                    case REQUEST_INFO:
                        renderInfoRequestJson(ws_config_json, &ws_config_lenght, 512);
                        ws_send_message(local_cfg_request.ws_client, WS_OP_TEXT, (uint8_t*)ws_config_json, ws_config_lenght);
                        break;
                    
                    case REQUEST_CURRENT:
                        // CORREÇÃO 2: Envia 'modulation' como inteiro (%d) para casar com o value do <select>
                        ws_config_lenght = snprintf(ws_config_json, 512, 
                                           "{\"modulation\": %d, \"roll_off\": %.2f}",
                                           local_cfg.modulation, local_cfg.roll_off);
                        
                        ws_send_message(local_cfg_request.ws_client, WS_OP_TEXT, (uint8_t*)ws_config_json, ws_config_lenght);
                        break;
                    
                    case REQUEST_UPDATE_YOURS:
                        // Atualiza estado local
                        local_cfg.modulation = local_cfg_request.mod;
                        local_cfg.roll_off   = local_cfg_request.roll_off;
                        
                        config_calculate_derived(&local_cfg);
                        
                        xQueueOverwrite(xDemodConfig, &local_cfg);
                        break;
                        
                    default:
                        break;
                }

                xSemaphoreGive(lwip_mutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay reduzido para resposta mais ágil da UI
    }
};

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

void create_index_response(char* query_params ,char* buffer, size_t len) {
    snprintf(buffer, len, HTTP_HEADER INDEX_BODY);   
}

void create_ws_only_response(char* query_params ,char* buffer, size_t len){
    snprintf(buffer, len, HTTP_HEADER WS_ONLY_BODY);
}

void serverTask(void *p){
  
    add_http_route("/", create_index_response);
    add_http_route("/ws/stream", create_ws_only_response);
    add_http_route("/ws/config", create_ws_only_response);
    
    add_new_schema_route("websocket", websocket_schema_upgrade);

    start_http_server();

    xTaskCreateAffinitySet(
        WebStreamMetricsTask,
        "Web Stream Metrics Task",
        1024,
        NULL,
        5,
        RP2040_CORE_0,
        NULL
    );

    xTaskCreateAffinitySet(
        WebConfigProcessTask,
        "Web Config Process Task",
        1024,
        NULL,
        5,
        RP2040_CORE_0,
        NULL
    );

    for(;;){
        if (xSemaphoreTake(lwip_mutex, portMAX_DELAY)){
            cyw43_arch_poll();
            xSemaphoreGive(lwip_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


// END

int main()
{
    printf("[INFO] SYSTEM STARTING...\n");    
    sleep_ms(3000);
        peripherals_setup();
    sleep_ms(2000);
        printf("[INFO] SYSTEM STARTED...\n");

    xDspQueue        = xQueueCreate(DSP_QUEUE_LENGHT, sizeof(IqBlock_t));
    
    xToScreenMetrics = xQueueCreate(1, sizeof(QLUMetrics));
    xToWebMetrics    = xQueueCreate(1, sizeof(WebMetrics));
    xDemodConfig     = xQueueCreate(1, sizeof(demod_config_t));
    xConfigRequest   = xQueueCreate(1, sizeof(ConfigRequest)); 
    lwip_mutex = xSemaphoreCreateMutex();

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
        10,
        RP2040_CORE_0,
        NULL
    );

    xTaskCreateAffinitySet(
        UpdateScreenTask,
        "Screen Update Task",
        1024,
        NULL,
        5,
        RP2040_CORE_0,
        NULL
    );

    xTaskCreateAffinitySet(
        serverTask,
        "Server Task",
        4096,
        NULL,
        10,
        RP2040_CORE_0,
        NULL);
    
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