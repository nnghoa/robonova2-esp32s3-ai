#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <wifi_station.h>

#include "application.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/display.h"
#include "led/single_led.h"
#include "robonova_controller.h"
#include "robonova_display.h"
#include "robonova_web_server.h"
#include "settings.h"
#include "wifi_board.h"

#define TAG "DbRobonova2Board"

class DbRobonova2S3Board : public WifiBoard {
private:
    Button boot_touch_button_;
    Display* display_ = nullptr;
    bool lcd_enabled_ = true;

    void InitializeDisplay() {
        Settings settings("robonova", false);
        lcd_enabled_ = settings.GetInt("lcd_enabled", 1) != 0;
        int spi_mode = settings.GetInt("lcd_spi_mode", DISPLAY_SPI_MODE);
        if (spi_mode != 0 && spi_mode != 3) {
            spi_mode = DISPLAY_SPI_MODE;
        }

        if (!lcd_enabled_) {
            ESP_LOGI(TAG, "ST7789 display disabled in NVS -> running headless (NoDisplay)");
            display_ = new NoDisplay();
            return;
        }

        // Giao tiếp SPI chỉ xuất tín hiệu một chiều (MISO = NC), nên dù không gắn màn hình
        // ST7789 1.54" thì hệ thống vẫn khởi động và hoạt động bình thường 100%.
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = spi_mode;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new RobonovaDisplay(
            panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
            true);

        if (auto* bl = GetBacklight()) {
            bl->RestoreBrightness();
        }
        ESP_LOGI(TAG, "Initialized 1.54\" ST7789 240x240 TFT (SPI mode %d)", spi_mode);
    }

    void InitializeButtons() {
        // 1 chạm (TTP223 hàn chân A = Active-LOW hoặc nút BOOT GPIO 0): Bật/tắt trò chuyện giọng nói AI
        boot_touch_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
                return;
            }
            app.ToggleChatState();
        });

        // Chạm đúp 2 lần: Dừng khẩn cấp chuỗi động tác robot & hiện địa chỉ Web :8080 lên màn hình
        boot_touch_button_.OnDoubleClick([this]() {
            RobonovaController::GetInstance().StopAll(false);
            std::string ip = WifiStation::GetInstance().GetIpAddress();
            if (!ip.empty() && display_ != nullptr) {
                display_->ShowNotification("Web: " + ip + ":" + std::to_string(ROBONOVA_WEB_PORT), 6000);
            }
        });

        // Chạm giữ lâu (>1.5s): Mở lại cổng cấu hình Wi-Fi AP
        boot_touch_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            app.SetDeviceState(kDeviceStateWifiConfiguring);
            ResetWifiConfiguration();
        });
    }

    void InitializeRobonovaTools() {
        auto& ctrl = RobonovaController::GetInstance();
        ctrl.Initialize();
        ctrl.RegisterMcpTools();
    }

public:
    DbRobonova2S3Board() : boot_touch_button_(BOOT_BUTTON_GPIO) {
        InitializeDisplay();
        InitializeButtons();
        InitializeRobonovaTools();
    }

    virtual std::string GetBoardType() override {
        return "db-robonova2-s3";
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (lcd_enabled_ && DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        auto tel = RobonovaController::GetInstance().GetTelemetry();
        if (tel.connected) {
            level = tel.battery_percent;
            charging = false;
            discharging = true;
            return true;
        }
        return false;
    }

    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();

        auto& wifi = WifiStation::GetInstance();
        if (wifi.IsConnected()) {
            RobonovaWebServer::GetInstance().Start(ROBONOVA_WEB_PORT);
            std::string ip = wifi.GetIpAddress();
            std::string url = ip + ":" + std::to_string(ROBONOVA_WEB_PORT);
            ESP_LOGI(TAG, "========================================================");
            ESP_LOGI(TAG, "DB-ROBONOVA2 WEB CONTROL READY: http://%s", url.c_str());
            ESP_LOGI(TAG, "========================================================");
            if (display_ != nullptr) {
                display_->ShowNotification("Web: " + url, 8000);
            }
        }
    }
};

DECLARE_BOARD(DbRobonova2S3Board);
