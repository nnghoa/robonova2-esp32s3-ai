#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/uart.h>

// ============================================================================
// SƠ ĐỒ SPARKLE-IOT XH-S3E-AI V1.0 (ESP32-S3 N16R8) + OLED I2C + ROBONOVA2
// - Đã tích hợp sẵn trên bo tím XH-S3E-AI: Mic số, Amp loa (EN=IO17) + Loa hộp,
//   LED RGB (IO48), nút BOOT (IO0), nút VOL+ (IO40), VOL- (IO39).
// - Hàng rào 8 chân trên đỉnh mạch (từ trái qua phải khi nhìn mặt lưng):
//     [1:GND | 2:3V3 | 3:IO42/SCL | 4:IO41/SDA] -> Cắm thẳng màn OLED 4 chân
//     [5:VIN | 6:GND | 7:RXD(IO44)| 8:TXD(IO43)] -> Nối 3 dây xuống SUPER-M V6.1
// ============================================================================

// --- Audio I2S Simplex tích hợp sẵn trên XH-S3E-AI_V1.0 ---
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

// MEMS Microphone tích hợp sẵn trên mạch
#define AUDIO_I2S_MIC_GPIO_WS    GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK   GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN   GPIO_NUM_6

// Khuếch đại loa I2S tích hợp sẵn + Chân kích hoạt Amp (BẮT BUỘC kéo lên mức HIGH)
#define AUDIO_I2S_SPK_GPIO_DOUT  GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK  GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK  GPIO_NUM_16
#define AUDIO_AMP_ENABLE_GPIO    GPIO_NUM_17

// --- Nút nhấn & Đèn trạng thái có sẵn trên mạch XH-S3E-AI_V1.0 ---
#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO    GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO  GPIO_NUM_39
#define BUILTIN_LED_GPIO         GPIO_NUM_48

// --- Màn hình OLED 4 chân I2C 128x64 (SH1106 1.3" / SSD1306 0.96") ---
// Cắm vào 4 chân bên trái: GND | 3V3 | IO42/SCL | IO41/SDA
#define DISPLAY_SDA_PIN          GPIO_NUM_41
#define DISPLAY_SCL_PIN          GPIO_NUM_42
#define DISPLAY_WIDTH            128
#define DISPLAY_HEIGHT           64
#define DISPLAY_MIRROR_X         true
#define DISPLAY_MIRROR_Y         true

// --- Giao tiếp UART với mạch lưng BJROBOT SUPER-M V6.1 (MR-C3024FX) ---
// Chân TXD (GPIO 43) -> Chân trên cùng (ERX - dây Nâu) ở Khu A - Cột 6 (9600 bps)
// Chân RXD (GPIO 44) <- Tùy chọn dây thứ 4 (bỏ trống khi chạy chế độ 3 dây)
#define ROBONOVA_UART_NUM        UART_NUM_1
#define ROBONOVA_UART_TX_PIN     GPIO_NUM_43
#define ROBONOVA_UART_RX_PIN     GPIO_NUM_44
#define ROBONOVA_UART_BAUD_RATE  9600

// --- Cổng Web Control Dashboard + WebSocket ---
#define ROBONOVA_WEB_PORT        8080
#define ROBONOVA_WEB_CTRL_PORT   32769

#define DB_ROBONOVA2_VERSION     "2.3.1-XH-S3E"

#endif // _BOARD_CONFIG_H_
