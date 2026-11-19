#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_sh1106.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <wifi_station.h>

#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "led/single_led.h"
#include "robonova_controller.h"
#include "robonova_web_server.h"
#include "settings.h"
#include "wifi_board.h"

#define TAG "XhS3eRobonovaBoard"

class DbRobonova2S3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    bool lcd_enabled_ = true;

    void InitializeAudioAmp() {
        // Kích hoạt IC khuếch đại loa tích hợp trên mạch XH-S3E-AI_V1.0 (GPIO 17 = HIGH)
        gpio_config_t amp_cfg = {};
        amp_cfg.pin_bit_mask = (1ULL << AUDIO_AMP_ENABLE_GPIO);
        amp_cfg.mode = GPIO_MODE_OUTPUT;
        amp_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        amp_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        amp_cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&amp_cfg));
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_AMP_ENABLE_GPIO, 1));
        ESP_LOGI(TAG, "Speaker amplifier enabled on GPIO %d", static_cast<int>(AUDIO_AMP_ENABLE_GPIO));
    }

    void InitializeDisplay() {
        Settings settings("robonova", false);
        lcd_enabled_ = settings.GetInt("lcd_enabled", 1) != 0;
        // Mặc định oled_type = 1 (SH1106 1.3" OLED xanh phổ biến kèm XH-S3E-AI); 0 = SSD1306 0.96"
        int oled_type = settings.GetInt("oled_type", 1);

        if (!lcd_enabled_) {
            ESP_LOGI(TAG, "OLED display disabled in NVS -> running headless (NoDisplay)");
            display_ = new NoDisplay();
            return;
        }

        i2c_master_bus_config_t bus_config = {
            .i2c_port = static_cast<i2c_port_t>(0),
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        if (i2c_new_master_bus(&bus_config, &display_i2c_bus_) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to init I2C bus -> running headless (NoDisplay)");
            display_ = new NoDisplay();
            return;
        }

        // Dò trước địa chỉ 0x3C để nếu chưa cắm màn OLED thì mạch vẫn chạy bình thường 100% không treo
        if (i2c_master_probe(display_i2c_bus_, 0x3C, 80) != ESP_OK) {
            ESP_LOGW(TAG, "No I2C OLED found at 0x3C (SDA=IO41, SCL=IO42) -> running headless");
            display_ = new NoDisplay();
            return;
        }

        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };
        if (esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_) != ESP_OK) {
            display_ = new NoDisplay();
            return;
        }

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        esp_err_t drv_err = (oled_type == 1)
            ? esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_)
            : esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_);
        if (drv_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create OLED panel driver");
            display_ = new NoDisplay();
            return;
        }

        esp_lcd_panel_reset(panel_);
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init OLED panel -> headless fallback");
            display_ = new NoDisplay();
            return;
        }
        esp_lcd_panel_invert_color(panel_, false);
        esp_lcd_panel_disp_on_off(panel_, true);

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        ESP_LOGI(TAG, "Initialized %s 128x64 I2C OLED (SDA=IO41, SCL=IO42)",
                 (oled_type == 1) ? "SH1106 1.3\"" : "SSD1306 0.96\"");
    }

    void InitializeButtons() {
        // Nút BOOT (IO0) trên XH-S3E-AI:
        // - Bấm 1 lần: Bật/tắt trò chuyện giọng nói AI
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
                return;
            }
            app.ToggleChatState();
        });

        // - Bấm đúp 2 lần: Dừng khẩn cấp robot (gửi K70) & hiện địa chỉ Web :8080 lên OLED
        boot_button_.OnDoubleClick([this]() {
            RobonovaController::GetInstance().StopAll(false);
            std::string ip = WifiStation::GetInstance().GetIpAddress();
            if (!ip.empty() && display_ != nullptr) {
                display_->ShowNotification(ip + ":" + std::to_string(ROBONOVA_WEB_PORT), 6000);
            }
        });

        // - Nhấn giữ lâu (>1.5s): Mở lại điểm phát Wi-Fi cấu hình mạng
        boot_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            app.SetDeviceState(kDeviceStateWifiConfiguring);
            ResetWifiConfiguration();
        });

        // Nút cạnh phải VOL+ (IO40) & VOL- (IO39) trên XH-S3E-AI
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) volume = 100;
            codec->SetOutputVolume(volume);
            if (display_ != nullptr) {
                display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            if (display_ != nullptr) {
                display_->ShowNotification(Lang::Strings::MAX_VOLUME);
            }
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) volume = 0;
            codec->SetOutputVolume(volume);
            if (display_ != nullptr) {
                display_->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
            }
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            if (display_ != nullptr) {
                display_->ShowNotification(Lang::Strings::MUTED);
            }
        });
    }

    void InitializeRobonovaTools() {
        auto& ctrl = RobonovaController::GetInstance();
        ctrl.Initialize();
        ctrl.RegisterMcpTools();
    }

public:
    DbRobonova2S3Board() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeAudioAmp();
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
        static bool gain_configured = false;
        if (!gain_configured) {
            audio_codec.SetInputGain(2.5f);
            gain_configured = true;
        }
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
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
            ESP_LOGI(TAG, "XH-S3E-AI ROBONOVA2 WEB READY: http://%s", url.c_str());
            ESP_LOGI(TAG, "========================================================");
            if (display_ != nullptr) {
                display_->ShowNotification(url, 8000);
            }
        }
    }
};

DECLARE_BOARD(DbRobonova2S3Board);
