#include <driver/spi_master.h>
#include "headers/simulation_base.h"

#define SPI_HOST_ID   SPI2_HOST // FSPI
#define SPI_PORT_FREQUENCY (1 * 1000 * 1000)
#define PIN_NUM_MISO  13
#define PIN_NUM_MOSI  11
#define PIN_NUM_CLK   12
#define PIN_NUM_CS    10

const uint8_t SYNC_HEADER[4] = {0xFE, 0xCA, 0xFE, 0xCA};

struct SimulationConfig {
    Modulations modulation;
    int snr;
    bool update_req;
};

QueueHandle_t configQueue;
spi_device_handle_t spi_handle;

Modulations global_mod = QAM16;
int global_snr = 1;

void commandTask(void *pvParameters);
void transmissionTask(void *pvParameters);
void idleTask(void *pvParameters);
bool parseCommand(String cmd);
void initSPI();

void setup() {
    pinMode(LED_BUILTIN,OUTPUT);

    Serial.begin(115200);
    global_snr = available_snr[0];

    int timeout = 0;
    while(!Serial && timeout < 2000) { 
      delay(10); 
      timeout += 10; 
    }

    Serial.println("--- SDR Simulator Booting ---");

    initSPI();

    configQueue = xQueueCreate(1, sizeof(SimulationConfig));

    xTaskCreatePinnedToCore(
        idleTask,    // Função
        "IdleTask",  // Nome
        4096,        
        NULL,        // Params
        1,           // Prioridade
        NULL,        // Handle
        0            // Core 0
    );

    xTaskCreatePinnedToCore(
        commandTask,    // Função
        "CmdTask",      // Nome
        4096,           // Stack (4KB)
        NULL,           // Params
        2,              // Prioridade
        NULL,           // Handle
        0               // Core 0
    );

    xTaskCreatePinnedToCore(
        transmissionTask, // Função
        "TxTask",         // Nome
        4096,             // Stack (4KB)
        NULL,             // Params
        10,                // Prioridade (Maior que a de Cmd)
        NULL,             // Handle
        1                 // Core 1
    );
}

void loop() {
    vTaskDelete(NULL);
}

// ============================================================
// CORE 0: Tarefa Idle
// ============================================================
void idleTask(void *pvParameters) {
    while(true){
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        vTaskDelay(pdMS_TO_TICKS(1000));    
    }
}

// ============================================================
// CORE 0: Tarefa de Comandos (Interface Serial)
// ============================================================
void commandTask(void *pvParameters) {
    char rx_buffer[64];
    int rx_index = 0;

    Serial.println("[Core 0] Command Task Started. Waiting for: AT, MODULACAO, SNR");

    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            
            if (c == '\n' || c == '\r') {
                if (rx_index > 0) {
                    rx_buffer[rx_index] = '\0';
                    parseCommand(String(rx_buffer));
                    rx_index = 0;
                }
            } else {
                if (rx_index < 63) {
                    rx_buffer[rx_index++] = c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool parseCommand(String cmd) {
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "AT") {
        Serial.println("--- STATUS ATUAL ---");
        Serial.printf("Modulacao: %s\n", GET_MODULATION_NAME(global_mod));
        Serial.printf("SNR: %d dB\n", global_snr);
        return true;
    } 
    
    else if (cmd.startsWith("MODULACAO:")) {
        String arg = cmd.substring(10);
        arg.trim();
        
        Modulations new_mod = MODULATIONS_COUNT;
        if (arg == "BPSK") new_mod = BPSK;
        else if (arg == "QPSK") new_mod = QPSK;
        else if (arg == "QAM16") new_mod = QAM16;

        if (new_mod != MODULATIONS_COUNT) {
            global_mod = new_mod;
            SimulationConfig cfg = {global_mod, global_snr, true};
            xQueueOverwrite(configQueue, &cfg); 
            Serial.printf("OK: Modulacao alterada para %s\n", arg.c_str());
        } else {
            Serial.println("ERRO: Modulacao invalida. Use: BPSK, QPSK, QAM16");
        }
        return true;
    }
    else if (cmd.startsWith("SNR:")) {
        int new_snr = cmd.substring(4).toInt();
        
        bool valid = false;
        for(size_t i=0; i < available_snr_count; i++) {
            if(available_snr[i] == new_snr) {
                valid = true;
                break;
            }
        }

        if (valid) {
            global_snr = new_snr;
            SimulationConfig cfg = {global_mod, global_snr, true};
            xQueueOverwrite(configQueue, &cfg);
            Serial.printf("OK: SNR alterado para %d dB\n", new_snr);
        } else {
            Serial.println("ERRO: SNR indisponivel na tabela.");
        }
        return true;
    }

    Serial.println("ERRO: Comando desconhecido.");
    return false;
}

// ============================================================
// CORE 1: Tarefa de Transmissão (SPI DMA)
// ============================================================
void transmissionTask(void *pvParameters) {
    const stream_data_t* tx_ptr = nullptr;
    const stream_meta_t* meta_ptr = nullptr;
    
    tx_ptr = GET_DATA(QAM16, available_snr[0]);  // Start with QAM16
    meta_ptr = GET_META(QAM16, available_snr[0]);
    
    // Packet contains 256 I/Q pairs
    const size_t IQ_PAIRS_PER_PACKET = 256;
    const size_t ARRAY_ELEMENTS_PER_PACKET = IQ_PAIRS_PER_PACKET * 2; // 512 (interleaved I,Q)
    const size_t PAYLOAD_BYTES = IQ_PAIRS_PER_PACKET * 4; // 1024 bytes
    const size_t TOTAL_PACKET_SIZE = sizeof(SYNC_HEADER) + PAYLOAD_BYTES; // 1028 bytes

    SimulationConfig rxConfig;
    spi_transaction_t t;

    uint8_t *tx_buffer = (uint8_t*) heap_caps_malloc(TOTAL_PACKET_SIZE, MALLOC_CAP_DMA);
    if (tx_buffer == NULL) {
        Serial.println("[ERROR] Failed to allocate DMA buffer!");
        vTaskDelete(NULL);
        return;
    }
    
    // Copy sync header once (never changes)
    memcpy(tx_buffer, SYNC_HEADER, sizeof(SYNC_HEADER));

    // Track position in source array
    size_t source_offset = 0;

    Serial.println("[Core 1] Waiting 3s for Pico...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    Serial.println("[Core 1] Starting transmission...");
    
    // DEBUG: Print first packet contents
    Serial.println("[TX DEBUG] First packet will contain:");
    for (int i = 0; i < 6; i++) {
        Serial.printf("  Array[%d] = %u (0x%04X)\n", i, tx_ptr[i], tx_ptr[i]);
    }

    while (true) {
        // Check for config changes
        if (xQueueReceive(configQueue, &rxConfig, 0) == pdTRUE) {
            tx_ptr = GET_DATA(rxConfig.modulation, rxConfig.snr);
            meta_ptr = GET_META(rxConfig.modulation, rxConfig.snr);
            source_offset = 0; // Reset position
            Serial.printf("[Core 1] Config changed: %s, SNR %d dB\n", 
                         GET_MODULATION_NAME(rxConfig.modulation), rxConfig.snr);
        }

        if (tx_ptr != nullptr && meta_ptr != nullptr) {
            memset(&t, 0, sizeof(t));
            
            // Calculate timing for this packet
            uint64_t target_duration_us = ((uint64_t)IQ_PAIRS_PER_PACKET * 1000000ULL) / 
                                          (meta_ptr->sampling_rate / 2); // sampling_rate is for I+Q combined

            // Fill packet payload with BIG ENDIAN data
            size_t dest_offset = sizeof(SYNC_HEADER);
            size_t total_samples = meta_ptr->n_samples;
            
            for (size_t i = 0; i < ARRAY_ELEMENTS_PER_PACKET; i++) {
                // Get next sample from source array (with wraparound)
                size_t src_idx = (source_offset + i) % total_samples;
                uint16_t sample = tx_ptr[src_idx];
                
                // Write in BIG ENDIAN format (MSB first, LSB second)
                tx_buffer[dest_offset++] = (uint8_t)((sample >> 8) & 0xFF); // MSB
                tx_buffer[dest_offset++] = (uint8_t)(sample & 0xFF);        // LSB
            }
            
            // Advance source offset for next packet
            source_offset = (source_offset + ARRAY_ELEMENTS_PER_PACKET) % total_samples;

            // Transmit via SPI
            t.length = TOTAL_PACKET_SIZE * 8; // Length in bits
            t.tx_buffer = tx_buffer;
            t.rx_buffer = NULL;

            int64_t start_time = esp_timer_get_time();
            esp_err_t ret = spi_device_transmit(spi_handle, &t);
            int64_t end_time = esp_timer_get_time();
            
            if (ret != ESP_OK) {
                Serial.printf("[ERROR] SPI transmit failed: %d\n", ret);
            }
            
            // Maintain timing
            int64_t elapsed_us = end_time - start_time;
            if (elapsed_us < target_duration_us) {
                ets_delay_us(target_duration_us - elapsed_us);
            }

        } else {
            Serial.println("[WARNING] No data source configured!");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// ============================================================
// Inicialização do Driver SPI (IDF Nativo)
// ============================================================
void initSPI() {
    esp_err_t ret;

    // 1. Configuração do Barramento
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096 // Tamanho máximo do buffer DMA (ajuste conforme o maior array)
    };

    ret = spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        Serial.printf("Falha ao inicializar bus SPI: %d\n", ret);
        return;
    }

    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 3,                  // SPI Modo 0
        .duty_cycle_pos = 128,      // 50% duty cycle
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = SPI_PORT_FREQUENCY, // 40 MHz (Clock Base)
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,            // Tamanho da fila de transações
        // .pre_cb = NULL,          // Callback antes da transação
    };

    ret = spi_bus_add_device(SPI_HOST_ID, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        Serial.printf("Falha ao adicionar device SPI: %d\n", ret);
    } else {
        Serial.println("SPI Inicializado a 40 MHz com sucesso.");
    }
}