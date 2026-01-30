// HAL RELATED LIBRARIES

    #include <stdio.h>
    #include "pico/stdlib.h"
    #include "hardware/spi.h"

// END

// THIRD PARTY LIBRARIES

    #include "fonts/freeMono9.h"
    #include "fonts/freeMono6.h"

//  END

// PROJECT INNER LIBRARIES

    #include "qlu_base.h"
    #include "screen.h"

// END

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

int main()
{
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

/*
[Ataniel - Testing Screen]

*/

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
// #define SPI_PORT spi0
// #define PIN_MISO 16
// #define PIN_CS   17
// #define PIN_SCK  18
// #define PIN_MOSI 19

// SPI initialisation. This example will use SPI at 1MHz.
// spi_init(SPI_PORT, 1000*1000);
// gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
// gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
// gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
// gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

// Chip select is active-low, so we'll initialise it to a driven-high state
// gpio_set_dir(PIN_CS, GPIO_OUT);
// gpio_put(PIN_CS, 1);
// For more examples of SPI use see https://github.com/raspberrypi/pico-examples/tree/master/spi