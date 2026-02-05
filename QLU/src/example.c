#include <stdio.h>
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// --- Configurações de Hardware ---
#define SPI_PORT spi0
#define PIN_RX   16
#define PIN_CSN  17
#define PIN_SCK  18
#define PIN_TX   19 // Não usado, mas necessário definir

// --- Configurações de Buffer ---
// O buffer circular deve ser alinhado para o DMA funcionar em modo circular
// Tamanho 4096 bytes = 1024 amostras (4 bytes cada: I+Q)
#define DMA_BUFFER_SIZE 4096 
uint8_t __attribute__((aligned(DMA_BUFFER_SIZE))) rx_ring_buffer[DMA_BUFFER_SIZE];

// --- Estrutura de Troca de Mensagens (Core 0 -> Core 1) ---
// Não enviamos os dados brutos na fila (lento).
// Enviamos uma cópia de um BLOCO de dados para processamento.
#define PROCESS_BLOCK_SIZE 256 // Processamos blocos de 256 amostras
typedef struct {
    int16_t i_samples[PROCESS_BLOCK_SIZE];
    int16_t q_samples[PROCESS_BLOCK_SIZE];
    uint32_t timestamp;
} IqBlock_t;

QueueHandle_t xDspQueue;
int dma_chan;

// =============================================================
// SETUP DO DMA (A "Mágica" que substitui a Tarefa 1 de leitura)
// =============================================================
void setup_spi_dma() {
    // 1. Configura SPI Slave
    spi_init(SPI_PORT, 40 * 1000 * 1000);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_set_slave(SPI_PORT, true);
    
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CSN, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);

    // 2. Configura DMA
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8); // Transfere bytes
    channel_config_set_read_increment(&c, false); // Lê sempre do mesmo reg SPI
    channel_config_set_write_increment(&c, true); // Escreve incrementando RAM
    channel_config_set_dreq(&c, spi_get_dreq(SPI_PORT, false)); // Sincroniza com RX do SPI
    
    // Configura o Ring Buffer no DMA
    // O hardware vai "dar a volta" no ponteiro automaticamente quando chegar no fim do buffer
    channel_config_set_ring(&c, true, 12); // 12 bits -> 4096 bytes (2^12)

    dma_channel_configure(
        dma_chan,
        &c,
        rx_ring_buffer,        // Destino
        &spi_get_hw(SPI_PORT)->dr, // Origem (Registrador de dados do SPI)
        0xFFFFFFFF,            // Número de transferências (infinito/max)
        true                   // Iniciar imediatamente
    );
}

// =============================================================
// CORE 1: Processamento DSP (Consumidor)
// =============================================================
void vDspTask(void *pvParameters) {
    IqBlock_t rxBlock;
    float evm_acumulado = 0;
    int calculos = 0;

    printf("[Core 1] DSP Task Iniciada\n");

    while(true) {
        // Bloqueia esperando um novo bloco de dados vindo do Core 0
        if (xQueueReceive(xDspQueue, &rxBlock, portMAX_DELAY) == pdPASS) {
            
            // --- Simulação de Cálculo de Métricas (Mer/EVM/Power) ---
            float signal_power = 0;
            float max_mag = 0;

            for(int k=0; k<PROCESS_BLOCK_SIZE; k++) {
                // Converte para float
                float i = (float)rxBlock.i_samples[k];
                float q = (float)rxBlock.q_samples[k];
                
                float mag_sq = (i*i + q*q);
                signal_power += mag_sq;
                
                if (mag_sq > max_mag) max_mag = mag_sq;
            }
            
            signal_power /= PROCESS_BLOCK_SIZE;
            
            // Apenas para não spammar o log
            calculos++;
            if (calculos % 10 == 0) {
                // Aqui você atualizaria o LCD ou prepararia o JSON pro WebSocket
                printf("[DSP] Power: %.2f | Peak: %.2f | Fila livre: %d\n", 
                       signal_power, sqrtf(max_mag), uxQueueSpacesAvailable(xDspQueue));
            }
        }
    }
}

// =============================================================
// CORE 0: Gerenciador de Buffer e Dispatch (Produtor)
// =============================================================
void vAcquisitionTask(void *pvParameters) {
    static uint32_t tail_index = 0; // Onde nós (software) paramos de ler
    IqBlock_t txBlock;

    printf("[Core 0] Acquisition Task Iniciada\n");

    while(true) {
        // 1. Descobre onde o DMA está escrevendo agora (Head)
        // O registrador TRANS_COUNT decrementa, mas WRITE_ADDR é mais fácil de rastrear no ring
        uint32_t current_write_addr = (uint32_t)dma_hw->ch[dma_chan].write_addr;
        uint32_t head_index = current_write_addr - (uint32_t)rx_ring_buffer;

        // 2. Calcula quantos bytes novos temos
        uint32_t available_bytes;
        if (head_index >= tail_index) {
            available_bytes = head_index - tail_index;
        } else {
            // Deu a volta no buffer
            available_bytes = (DMA_BUFFER_SIZE - tail_index) + head_index;
        }

        // 3. Se temos dados suficientes para um bloco de processamento (ex: 256 amostras * 4 bytes)
        size_t bytes_needed = PROCESS_BLOCK_SIZE * 4;
        
        if (available_bytes >= bytes_needed) {
            
            // Copia do Ring Buffer Raw para a estrutura organizada (I separado de Q)
            // Isso facilita a vida do DSP (matemática vetorial fica mais rápida se I e Q estiverem separados)
            
            for (int i = 0; i < PROCESS_BLOCK_SIZE; i++) {
                // Recupera 4 bytes (Low I, High I, Low Q, High Q)
                // Cuidado com o wrap-around manual aqui
                
                uint8_t b0 = rx_ring_buffer[(tail_index + 0) % DMA_BUFFER_SIZE];
                uint8_t b1 = rx_ring_buffer[(tail_index + 1) % DMA_BUFFER_SIZE];
                uint8_t b2 = rx_ring_buffer[(tail_index + 2) % DMA_BUFFER_SIZE];
                uint8_t b3 = rx_ring_buffer[(tail_index + 3) % DMA_BUFFER_SIZE];

                txBlock.i_samples[i] = (int16_t)((b1 << 8) | b0);
                txBlock.q_samples[i] = (int16_t)((b3 << 8) | b2);
                
                tail_index = (tail_index + 4) % DMA_BUFFER_SIZE;
            }

            // Envia para o Core 1
            // Se a fila estiver cheia (DSP lento), nós descartamos esse bloco (como você permitiu)
            // Usamos xQueueSendToBack com tempo de espera 0 para não travar a aquisição
            if (xQueueSend(xDspQueue, &txBlock, 0) != pdTRUE) {
                // printf("Drop!\n"); 
                // Se cair aqui, o Core 1 não está dando conta
            }

        } else {
            // Se não tem dados suficientes, dorme um pouco para deixar a CPU livre
            // 1ms é tempo suficiente para encher bastante coisa a 20Mbps
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// =============================================================
// MAIN
// =============================================================
int main() {
    stdio_init_all();
    sleep_ms(2000); 

    printf("--- FreeRTOS SMP SDR Receiver ---\n");

    // 1. Inicia Hardware
    setup_spi_dma();

    // 2. Cria Fila de Comunicação Inter-Core
    // Guarda até 10 blocos pendentes
    xDspQueue = xQueueCreate(10, sizeof(IqBlock_t));

    // 3. Cria Tarefas e Pina nos Cores (Opcional, mas recomendado para determinismo)
    
    // Core 0: Gerencia o buffer DMA e monta os pacotes
    xTaskCreateAffinitySet(
        vAcquisitionTask, 
        "Acquisition", 
        2048, 
        NULL, 
        2,      // Prioridade Alta
        1,      // Máscara de afinidade: 1 = Core 0 (Bit 0)
        NULL
    );

    // Core 1: Faz a matemática
    xTaskCreateAffinitySet(
        vDspTask, 
        "DSP", 
        4096,   // Stack maior para float/math
        NULL, 
        1,      // Prioridade Normal
        2,      // Máscara de afinidade: 2 = Core 1 (Bit 1)
        NULL
    );

    // 4. Inicia Scheduler
    vTaskStartScheduler();

    while(1);
    return 0;
}