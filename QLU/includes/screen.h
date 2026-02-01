/*

[Ataniel - 29/01/2026]
    
Biblioteca Encapsuladora das Funções de Interação com
o Screen TFT ST7735.

*/

#ifndef SCREEN_H

#define SCREEN_H

#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include "qlu_base.h"

#ifdef SCREEN_IS_ST7735
    #define SCREEN_VERTICAL (0x0)
    #define ST7735_SCREEN_HEIGHT (160)
    #define ST7735_SCREEN_WIDTH  (128)
    #include "gfx.h"
    #include "st7735.h"

    void screen_init_setup(uint8_t init_opt, uint8_t rotation, GFXfont* font){
        LCD_initDisplay(init_opt);
        LCD_setRotation(rotation);
        GFX_createFramebuf();
        GFX_clearScreen();
        GFX_setFont(font);
    }

    void write_boxed_metrics(uint8_t pad,
                         uint8_t font_size,
                         uint16_t box_color,
                         QLUMetrics* qm
    ){
        uint8_t cursor_walk = pad + font_size; 
        GFX_setTextColor(ST77XX_WHITE);
        GFX_fillRect(0,0,ST7735_SCREEN_WIDTH,3*(font_size+pad)+1,ST77XX_BLACK);

        GFX_setCursor(0,cursor_walk);
        GFX_printf("MER: %.2f dB",qm->mer);

        GFX_setCursor(0,2*(cursor_walk));
        GFX_printf("CN0: %.2f dB-Hz",qm->cn0);

        GFX_setCursor(0,3*(cursor_walk));
        GFX_printf("evm: %.2f%%",qm->evm*100.0);

        GFX_flush();       
    }

    #define BOOL_DEFINED

    #include "qrcode.h"

    #define qr_code_grid_lenght(version) (4 * version + 17)

    typedef struct {
        QRCode*  qrcode;
        uint8_t* qrcodeBytes;    
        uint8_t  version;
        uint8_t  error_level;
        size_t   grid_len; 
        float    scale_factor;
    } QRCodeGenerated;

    typedef struct {
        uint8_t version;
        uint8_t error_level;
        uint8_t target_width;
    } QRCodeConfig;

    const QRCodeConfig SMALL_QR_CONFIG = {
        .error_level  = ECC_LOW,
        .version      = 5,
        .target_width = 100
    };

    void generate_qr_code(QRCodeGenerated *qr_gen, QRCodeConfig* cfg ,char* data){
        qr_gen->version     = cfg->version;
        qr_gen->error_level = cfg->error_level;
        qr_gen->qrcode      = calloc(1, sizeof(QRCode));
        qr_gen->grid_len    = qr_code_grid_lenght(cfg->version);
        qr_gen->qrcodeBytes = calloc(qrcode_getBufferSize(cfg->version), sizeof(uint8_t));
        qrcode_initText(qr_gen->qrcode, qr_gen->qrcodeBytes, cfg->version, cfg->error_level, data);
        qr_gen->scale_factor = (float)cfg->target_width / (float)qr_gen->grid_len;
    }

    void fill_area_with_qr_code(QRCodeGenerated* qr_gen, int16_t offset_x, int16_t offset_y){
        for (uint8_t y = 0; y < qr_gen->qrcode->size; y++) {
            for (uint8_t x = 0; x < qr_gen->qrcode->size; x++) {
                GFX_fillRect(
                    offset_x + (uint8_t)ceil(qr_gen->scale_factor * (float)x),
                    offset_y + (uint8_t)ceil(qr_gen->scale_factor * (float)y),
                    (uint8_t)ceil(qr_gen->scale_factor),
                    (uint8_t)ceil(qr_gen->scale_factor),
                    qrcode_getModule(qr_gen->qrcode, x, y)
                        ? ST77XX_WHITE
                        : ST77XX_BLACK
                );
            }
        }
    }

    void fill_with_qr_code_centered(QRCodeGenerated* qr_gen){
        size_t total_px = (size_t)ceil((float)qr_gen->grid_len * qr_gen->scale_factor);
        // now true leftover padding on each side
        int16_t offset_x = (ST7735_SCREEN_WIDTH  - total_px) / 2;
        int16_t offset_y = (ST7735_SCREEN_HEIGHT - total_px) / 2;
        fill_area_with_qr_code(qr_gen, offset_x, offset_y);
    }

    void fill_with_qr_code_bottom(QRCodeGenerated* qr_gen){
        size_t total_px = (size_t)ceil((float)qr_gen->grid_len * qr_gen->scale_factor);
        // now true leftover padding on each side
        int16_t offset_x = (ST7735_SCREEN_WIDTH  - total_px) / 2;
        int16_t offset_y = (ST7735_SCREEN_HEIGHT - total_px) - 10;
        fill_area_with_qr_code(qr_gen, offset_x, offset_y);
    }

#endif


#ifdef SCREEN_IS_SSD1306

    #define SSD1306_IMPLEMENTATION
    #include "stb_ssd1306.h"
    
    
    #define FONT_PIXEL_WIDTH  8
    #define FONT_PIXEL_HEIGHT 8

    const uint SSD1306_I2C_SDA = 14;
    const uint SSD1306_I2C_SCL = 15;

    volatile struct render_area frame_area = {
        start_column : 0,
        end_column : ssd1306_width - 1,
        start_page : 0,
        end_page : ssd1306_n_pages - 1
    };

    volatile uint8_t ssd[ssd1306_buffer_length];

    void ssd1306_i2c_setup(){
        i2c_init(i2c1, ssd1306_i2c_clock * 1000);
        gpio_set_function(SSD1306_I2C_SDA, GPIO_FUNC_I2C);
        gpio_set_function(SSD1306_I2C_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(SSD1306_I2C_SDA);
        gpio_pull_up(SSD1306_I2C_SCL);
        ssd1306_init();
        calculate_render_area_buffer_length(&frame_area);
    }

    void fill_screen(int color){
        memset(ssd, color, ssd1306_buffer_length);
        render_on_display(ssd, &frame_area);
    }    

    void clear_screen(void){
        fill_screen(0U);
    }    

    #define STRINGS(...) (char*[]){__VA_ARGS__}
    void render_strings(char* strings[], uint count){
        int y = 0;
        for (uint i = 0; i < count; i++)
        {
            ssd1306_draw_string(ssd, 0, y, strings[i]);
            y += FONT_PIXEL_HEIGHT;
        }
        render_on_display(ssd, &frame_area);
    }


    void ssd1306_clear_rect_area(int x_start, int y_start, int width, int height) {
        for (int x = x_start; x < x_start + width; x++) {
            for (int y = y_start; y < y_start + height; y++) {
                if (x >= 0 && x < ssd1306_width && y >= 0 && y < ssd1306_height) {
                    ssd1306_set_pixel((uint8_t*)ssd, x, y, false);
                }
            }
        }
    }

    void write_boxed_metrics(uint8_t pad,
                             uint8_t font_size, 
                             uint16_t box_color,
                             QLUMetrics* qm
    ){
        char buffer[32];
        const uint8_t FIXED_FONT_HEIGHT = 8;
        
        uint8_t line_height = FIXED_FONT_HEIGHT + pad;
        uint8_t total_height = 3 * line_height + 2;
        
        ssd1306_clear_rect_area(0, 0, ssd1306_width, total_height);

        uint8_t cursor_y = pad;

        snprintf(buffer, sizeof(buffer), "SNR: %.2f dB", qm->snr);
        ssd1306_draw_string((uint8_t*)ssd, 0, cursor_y, buffer);
        
        cursor_y += line_height;
        snprintf(buffer, sizeof(buffer), "MER: %.2f dB", qm->mer);
        ssd1306_draw_string((uint8_t*)ssd, 0, cursor_y, buffer);

        cursor_y += line_height;
        snprintf(buffer, sizeof(buffer), "CN0: %.2f dB-Hz", qm->cn0);
        ssd1306_draw_string((uint8_t*)ssd, 0, cursor_y, buffer);

        cursor_y += line_height;
        snprintf(buffer, sizeof(buffer), "evm: %.2f%%", qm->evm);
        ssd1306_draw_string((uint8_t*)ssd, 0, cursor_y, buffer);

        render_on_display((uint8_t*)ssd, (struct render_area*)&frame_area);
    }

#endif

#endif