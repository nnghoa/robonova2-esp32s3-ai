#include "robonova_display.h"

#include <esp_log.h>
#include <memory>

#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "otto_emoji_gif.h"

#define TAG "RobonovaDisplay"

RobonovaDisplay::RobonovaDisplay(esp_lcd_panel_io_handle_t panel_io,
                                 esp_lcd_panel_handle_t panel,
                                 int width,
                                 int height,
                                 int offset_x,
                                 int offset_y,
                                 bool mirror_x,
                                 bool mirror_y,
                                 bool swap_xy,
                                 bool use_robot_eyes)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy),
      use_robot_eyes_(use_robot_eyes) {
    SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
    if (use_robot_eyes_) {
        InitializeRobotEyeGifs();
    }
}

void RobonovaDisplay::InitializeRobotEyeGifs() {
    ESP_LOGI(TAG, "Initializing animated Robot GIF eyes for ST7789 240x240");

    auto emoji_collection = std::make_shared<EmojiCollection>();

    // Trạng thái bình thường / chờ -> staticstate
    emoji_collection->AddEmoji("staticstate", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emoji_collection->AddEmoji("neutral", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emoji_collection->AddEmoji("relaxed", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emoji_collection->AddEmoji("sleepy", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    emoji_collection->AddEmoji("idle", new LvglRawImage((void*)staticstate.data, staticstate.data_size));

    // Vui vẻ / hào hứng -> happy
    emoji_collection->AddEmoji("happy", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("laughing", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("funny", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("loving", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("confident", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("winking", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("cool", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("delicious", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("kissy", new LvglRawImage((void*)happy.data, happy.data_size));
    emoji_collection->AddEmoji("silly", new LvglRawImage((void*)happy.data, happy.data_size));

    // Buồn -> sad
    emoji_collection->AddEmoji("sad", new LvglRawImage((void*)sad.data, sad.data_size));
    emoji_collection->AddEmoji("crying", new LvglRawImage((void*)sad.data, sad.data_size));

    // Võ thuật / Mạnh mẽ -> anger
    emoji_collection->AddEmoji("anger", new LvglRawImage((void*)anger.data, anger.data_size));
    emoji_collection->AddEmoji("angry", new LvglRawImage((void*)anger.data, anger.data_size));

    // Ngạc nhiên / Cảnh báo vật cản -> scare
    emoji_collection->AddEmoji("scare", new LvglRawImage((void*)scare.data, scare.data_size));
    emoji_collection->AddEmoji("surprised", new LvglRawImage((void*)scare.data, scare.data_size));
    emoji_collection->AddEmoji("shocked", new LvglRawImage((void*)scare.data, scare.data_size));

    // Suy nghĩ -> buxue
    emoji_collection->AddEmoji("buxue", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emoji_collection->AddEmoji("thinking", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emoji_collection->AddEmoji("confused", new LvglRawImage((void*)buxue.data, buxue.data_size));
    emoji_collection->AddEmoji("embarrassed", new LvglRawImage((void*)buxue.data, buxue.data_size));

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");
    if (light_theme != nullptr) {
        light_theme->set_emoji_collection(emoji_collection);
    }
    if (dark_theme != nullptr) {
        dark_theme->set_emoji_collection(emoji_collection);
    }

    SetEmotion("staticstate");
}

void RobonovaDisplay::SetRobotEyesStyle(bool enable) {
    use_robot_eyes_ = enable;
    if (enable) {
        InitializeRobotEyeGifs();
    }
}
