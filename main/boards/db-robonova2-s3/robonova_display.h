#pragma once

#include "display/lcd_display.h"

/**
 * @brief Màn hình 1.54" ST7789 240x240 cho DB-Robonova2 ESP32-S3 AI
 * Hỗ trợ mắt Robot GIF động (Otto Emoji GIF), phụ đề tiếng Việt có dấu,
 * sóng nhạc FFT Spectrum khi phát nhạc/Radio VOV và hiển thị QR/IP :8080.
 */
class RobonovaDisplay : public SpiLcdDisplay {
public:
    RobonovaDisplay(esp_lcd_panel_io_handle_t panel_io,
                    esp_lcd_panel_handle_t panel,
                    int width,
                    int height,
                    int offset_x,
                    int offset_y,
                    bool mirror_x,
                    bool mirror_y,
                    bool swap_xy,
                    bool use_robot_eyes = true);

    virtual ~RobonovaDisplay() = default;

    void SetRobotEyesStyle(bool enable);
    bool IsRobotEyesStyle() const { return use_robot_eyes_; }

private:
    bool use_robot_eyes_ = true;
    void InitializeRobotEyeGifs();
};
