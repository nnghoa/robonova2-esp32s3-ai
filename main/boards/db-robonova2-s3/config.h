#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/uart.h>

// ============================================================================
// SƠ ĐỒ DB-ROBONOVA2 ESP32-S3 XIAOZHI AI (ESP32-S3-WROOM-1 N16R8)
// Giữ nguyên cấu trúc robot gốc MR-C3024FX + Tích hợp AI, Web :8080, Đa phương tiện
// ============================================================================

// --- Audio I2S Simplex (INMP441 Mic + MAX98357A Speaker Amp) ---
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

// INMP441 Microphone (VDD->3.3V, GND->GND, L/R->GND)
#define AUDIO_I2S_MIC_GPIO_WS    GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK   GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN   GPIO_NUM_6

// MAX98357A Amplifier (VIN->5V, GND->GND)
#define AUDIO_I2S_SPK_GPIO_DOUT  GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK  GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK  GPIO_NUM_16

// --- Nút nhấn BOOT & Cảm biến chạm TTP223 (Hàn nối chân A = Active LOW) ---
#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define BUILTIN_LED_GPIO         GPIO_NUM_48

// --- Màn hình 1.54" IPS TFT ST7789 240x240px (Tùy chọn: không gắn vẫn chạy bình thường) ---
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_CLK_PIN          GPIO_NUM_9   // SCL (SPI CLOCK)
#define DISPLAY_MOSI_PIN         GPIO_NUM_10  // SDA (SPI DATA)
#define DISPLAY_RST_PIN          GPIO_NUM_18  // RES (RESET)
#define DISPLAY_DC_PIN           GPIO_NUM_8   // DC  (DATA/COMMAND)
#define DISPLAY_CS_PIN           GPIO_NUM_14  // CS  (ENABLE / CHIP SELECT)
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_13  // BLK (BACKLIGHT CONTROL)

#define DISPLAY_WIDTH            240
#define DISPLAY_HEIGHT           240
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_INVERT_COLOR     true
#define DISPLAY_RGB_ORDER        LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE         0            // 8-pin ST7789 có chân CS dùng Mode 0 (có thể đổi sang Mode 3 trên Web :8080 nếu dùng màn 7-pin)

// --- Giao tiếp UART với mạch gốc MINIROBOT MR-C3024FX (Robonova2 / MF-17ST) ---
// GPIO 42 (TX của ESP32-S3) -> Chân 'a' (ERX trên MR-C3024FX, nhận lệnh động tác K1..K68)
// GPIO 41 (RX của ESP32-S3) <- Chân 'A' (ETX trên MR-C3024FX, gửi phản hồi cảm biến mắt siêu âm + chân 10-NK + pin)
#define ROBONOVA_UART_NUM        UART_NUM_1
#define ROBONOVA_UART_TX_PIN     GPIO_NUM_42
#define ROBONOVA_UART_RX_PIN     GPIO_NUM_41
#define ROBONOVA_UART_BAUD_RATE  9600

// --- Cổng Web Control Dashboard + WebSocket ---
#define ROBONOVA_WEB_PORT        8080
#define ROBONOVA_WEB_CTRL_PORT   32769        // Tách riêng với OtaServer (ctrl_port 32768 ở cổng 80)

#define DB_ROBONOVA2_VERSION     "2.1.0-AI"

#endif // _BOARD_CONFIG_H_
