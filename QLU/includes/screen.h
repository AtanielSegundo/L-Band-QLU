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

#include "gfx.h"
#include "st7735.h"

#define BOOL_DEFINED
#include "qrcode.h"

#include "qlu_base.h"

#define SCREEN_VERTICAL (0x0)
#define ST7735_SCREEN_HEIGHT (160)
#define ST7735_SCREEN_WIDTH  (128)

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