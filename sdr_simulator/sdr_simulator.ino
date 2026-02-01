#include <driver/spi_master.h>
#include "headers/simulation_base.h"

#define SPI_HOST_ID   SPI2_HOST // FSPI
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

Modulations global_mod = BPSK;
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
    size_t data_len_bytes = 0;
    
    tx_ptr = GET_DATA(BPSK, available_snr[0]);
    meta_ptr = GET_META(BPSK, available_snr[0]);
    
    size_t chunk_size_samples = 256;
    size_t payload_size = chunk_size_samples * sizeof(stream_data_t); // 256 * 4 bytes
    size_t total_size = sizeof(SYNC_HEADER) + payload_size;

    SimulationConfig rxConfig;
    spi_transaction_t t;

    uint8_t *tx_buffer = (uint8_t*) heap_caps_malloc(total_size, MALLOC_CAP_DMA);
    memcpy(tx_buffer, SYNC_HEADER, sizeof(SYNC_HEADER));

    Serial.println("[Core 1] Transmission Task Started with Precise Timing");

    while (true) {
        if (xQueueReceive(configQueue, &rxConfig, 0) == pdTRUE) {
            tx_ptr = GET_DATA(rxConfig.modulation, rxConfig.snr);
            meta_ptr = GET_META(rxConfig.modulation, rxConfig.snr);  
        }

        if (tx_ptr != nullptr && meta_ptr != nullptr) {
            memset(&t, 0, sizeof(t));
            uint32_t num_pairs = meta_ptr->n_samples / 2;
            uint64_t target_duration_us = ((uint64_t)num_pairs * 1000000ULL) / meta_ptr->sampling_rate;
            
            // Se o sample rate for muito alto (ex: 20MHz), a duração alvo pode ser 0 ou muito baixa.
            // Nesse caso, roda-se no máximo que o SPI aguentar.
            memcpy(tx_buffer + 4, tx_ptr, payload_size);

            data_len_bytes = meta_ptr->n_samples * sizeof(stream_data_t);
            t.length = total_size * 8;
            t.tx_buffer = tx_buffer;
            t.rx_buffer = NULL; // Garante que não queremos receber nada
            t.rxlength = 0;     // Garante que o tamanho esperado de RX é 0

            int64_t start_time = esp_timer_get_time(); // Tempo em microsegundos desde o boot
            
            spi_device_transmit(spi_handle, &t);
            
            int64_t end_time = esp_timer_get_time();
            int64_t elapsed_us = end_time - start_time;

            // === COMPENSAÇÃO DE TEMPO (DELAY) ===
            // Se transmitimos mais rápido do que o sample rate exige, esperamos a diferença.
            // Se o SPI (40MHz) for mais lento que o sample rate exigido, não esperamos nada (máximo esforço).
            
            if (elapsed_us < target_duration_us) {
                // ets_delay_us é um "busy wait" preciso, ideal para microsegundos
                ets_delay_us(target_duration_us - elapsed_us);
            }

        } else {
            vTaskDelay(1);
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
        .mode = 0,                  // SPI Modo 0
        .duty_cycle_pos = 128,      // 50% duty cycle
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 4 * 1000 * 1000, // 40 MHz (Clock Base)
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