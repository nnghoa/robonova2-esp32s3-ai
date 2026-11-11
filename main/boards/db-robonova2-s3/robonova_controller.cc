#include "robonova_controller.h"

#include <cJSON.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <wifi_station.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "application.h"
#include "audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "config.h"
#include "display.h"
#include "esp32_music.h"
#include "esp32_radio.h"
#include "mcp_server.h"
#include "settings.h"

#define TAG "RobonovaCtrl"

namespace {

// Bảng đầy đủ 68 động tác nguyên gốc của hãng MINIROBOT Robonova2 / Metal Fighter MF-17ST
// Khớp 100% với code MR-C3024FX:
//   BT = 40..74  -> A = BT - 39 -> K1..K35  (ASCII '(' .. 'J')
//   BT = 80..88  -> A = BT - 79 -> K40..K48 (ASCII 'P' .. 'X')
//   BT = 90..108 -> A = BT - 89 -> K50..K68 (ASCII 'Z' .. 'l')
const std::vector<RobonovaActionInfo> kRobonovaActions = {
    // --- Nhóm 1: K1..K35 (DEC 40..74) ---
    {1,  40,  "bow",              "Cúi chào",                 "basic",   2600, "happy"},
    {2,  41,  "hands_up",         "Giơ hai tay",              "basic",   2400, "happy"},
    {3,  42,  "sit_down",         "Ngồi xuống đứng dậy",      "basic",   2800, "relaxed"},
    {4,  43,  "sit_hands_up",     "Ngồi giơ tay",             "basic",   3000, "happy"},
    {5,  44,  "foot_up",          "Nhấc một chân",            "basic",   2800, "cool"},
    {6,  45,  "simple_dance",     "Lắc thân (Nhảy nhẹ)",      "dance",   4200, "happy"},
    {7,  46,  "wing_move",        "Vẫy cánh tay (Flying)",    "dance",   3600, "happy"},
    {8,  47,  "kick_both",        "Đá phải & Đá trái",        "martial", 5200, "anger"},
    {9,  48,  "handstand",        "Trồng chuối chống tay",    "special", 7500, "cool"},
    {10, 49,  "fast_walk",        "Đi nhanh",                 "move",    2200, "staticstate"},
    {11, 50,  "walk_forward",     "Đi tới",                   "move",    2400, "staticstate"},
    {12, 51,  "walk_backward",    "Đi lùi",                   "move",    2400, "staticstate"},
    {13, 52,  "shift_right",      "Bước ngang phải",          "move",    2000, "staticstate"},
    {14, 53,  "shift_left",       "Bước ngang trái",          "move",    2000, "staticstate"},
    {15, 54,  "left_attack",      "Đấm móc trái",             "martial", 2600, "anger"},
    {16, 55,  "toggle_sit_relax", "Ngồi nghỉ / Bật lại Servo","basic",   2800, "sleepy"},
    {17, 56,  "left_forward",     "Bước chéo trái",           "move",    2400, "staticstate"},
    {18, 57,  "beep_c",           "Phát còi nốt C",           "basic",   600,  "happy"},
    {19, 58,  "standup_back",     "Đứng dậy từ nằm ngửa",     "special", 4800, "confident"},
    {20, 59,  "right_attack",     "Đấm móc phải",             "martial", 2600, "anger"},
    {21, 60,  "tumble_forward",   "Lộn nhào tới",             "special", 5500, "cool"},
    {22, 61,  "turn_left",        "Quay trái",                "move",    2000, "staticstate"},
    {23, 62,  "beep_d",           "Phát còi nốt D",           "basic",   600,  "happy"},
    {24, 63,  "turn_right",       "Quay phải",                "move",    2000, "staticstate"},
    {25, 64,  "standup_front",    "Đứng dậy từ nằm sấp",      "special", 4800, "confident"},
    {26, 65,  "toggle_sit_pose2", "Tư thế ngồi 2 (Toggle)",   "basic",   2500, "relaxed"},
    {27, 66,  "right_forward",    "Bước chéo phải",           "move",    2400, "staticstate"},
    {28, 67,  "tumble_left",      "Nhào lộn sang trái",       "special", 5000, "cool"},
    {29, 68,  "forward_punch",    "Đấm thẳng",                "martial", 2400, "anger"},
    {30, 69,  "tumble_right",     "Nhào lộn sang phải",       "special", 5000, "cool"},
    {31, 70,  "tumble_back",      "Lộn ngược ra sau",         "special", 5500, "cool"},
    {32, 71,  "beep_e",           "Phát còi nốt E",           "basic",   600,  "happy"},
    {33, 72,  "new_fast_walk",    "Đi rất nhanh",             "move",    2000, "confident"},
    {34, 73,  "fast_left_turn",   "Quay trái nhanh",          "move",    1800, "staticstate"},
    {35, 74,  "fast_right_turn",  "Quay phải nhanh",          "move",    1800, "staticstate"},

    // --- Nhóm 2: K40..K48 (DEC 80..88) - Võ thuật & Sức mạnh ---
    {40, 80,  "pushup_1",         "Chống đẩy 1 (Có nhạc)",    "martial", 7500, "confident"},
    {41, 81,  "honor_bow",        "Chào danh dự & Cúi chào",  "basic",   4800, "confident"},
    {42, 82,  "big_toss",         "Quật ngã (Big Toss)",      "martial", 5000, "anger"},
    {43, 83,  "big_reach",        "Vươn cao (Big Reach)",     "basic",   3500, "happy"},
    {44, 84,  "kungfu_2",         "Võ thuật 2 (Kungfu 2)",    "martial", 5500, "anger"},
    {45, 85,  "kungfu_3",         "Võ thuật 3 (Kungfu 3)",    "martial", 5500, "anger"},
    {46, 86,  "kungfu_4",         "Võ thuật 4 (Kungfu 4)",    "martial", 5500, "anger"},
    {47, 87,  "kungfu_5",         "Võ thuật 5 (Kungfu 5)",    "martial", 5500, "anger"},
    {48, 88,  "kungfu_1",         "Võ thuật 1 (Kungfu 1)",    "martial", 5800, "anger"},

    // --- Nhóm 3: K50..K68 (DEC 90..108) - Nâng cao, Nhảy múa, Cầu thang ---
    {50, 90,  "fast_run",         "Chạy nhanh",               "move",    2200, "confident"},
    {51, 91,  "fast_turn_right2", "Quay phải nhanh 2",        "move",    2200, "staticstate"},
    {52, 92,  "fast_turn_left2",  "Quay trái nhanh 2",        "move",    2200, "staticstate"},
    {53, 93,  "avoid_matrix",     "Né đòn Matrix",            "martial", 4800, "cool"},
    {54, 94,  "goalkeeper",       "Thủ môn bắt bóng",         "special", 5200, "cool"},
    {55, 95,  "cheer_up",         "Cổ vũ reo hò",             "dance",   4500, "happy"},
    {56, 96,  "single_foot",      "Đứng thăng bằng 1 chân",   "special", 5000, "cool"},
    {57, 97,  "clap_hands",       "Vỗ tay",                   "dance",   3500, "happy"},
    {58, 98,  "shake_hand",       "Bắt tay chào hỏi",         "basic",   3500, "happy"},
    {59, 99,  "break_dance",      "Nhảy Breakdance",          "dance",   8500, "cool"},
    {60, 100, "sit_walk_fwd",     "Đi tới kiểu ngồi thấp",    "move",    3000, "funny"},
    {61, 101, "sit_walk_back",    "Đi lùi kiểu ngồi thấp",    "move",    3000, "funny"},
    {62, 102, "sit_walk_left",    "Đi ngang trái ngồi thấp",  "move",    3000, "funny"},
    {63, 103, "sit_walk_right",   "Đi ngang phải ngồi thấp",  "move",    3000, "funny"},
    {64, 104, "dance_1",          "Nhảy Dance 1",             "dance",   8000, "happy"},
    {65, 105, "dance_2",          "Nhảy Dance 2",             "dance",   8000, "happy"},
    {66, 106, "climb_stairs",     "Leo bậc cầu thang",        "special", 8500, "confident"},
    {67, 107, "pushup_2",         "Chống đẩy 2 (10 nhịp)",    "martial", 9500, "confident"},
    {68, 108, "dance_3",          "Nhảy Dance 3",             "dance",   8500, "happy"},
};

struct AliasPair {
    const char* alias;
    int k_num;
};

// Từ điển đồng nghĩa tiếng Việt / tiếng Anh giúp AI gọi chính xác ngay cả khi dùng từ tự nhiên
const AliasPair kActionAliases[] = {
    {"tien", 11}, {"tienlen", 11}, {"ditoi", 11}, {"forward", 11}, {"walk", 11},
    {"lui", 12}, {"luilai", 12}, {"dilui", 12}, {"backward", 12}, {"back", 12},
    {"dinhanh", 10}, {"chay", 50}, {"run", 50}, {"diratnhanh", 33},
    {"releft", 22}, {"retrai", 22}, {"quaytrai", 22}, {"left", 22},
    {"reright", 24}, {"rephai", 24}, {"quayphai", 24}, {"right", 24},
    {"ngangphai", 13}, {"sangphai", 13}, {"ngangtrai", 14}, {"sangtrai", 14},
    {"chao", 1}, {"cuichao", 1}, {"chaodanhdu", 41}, {"salute", 41},
    {"giotay", 2}, {"giohaitay", 2}, {"vuonvai", 43}, {"vuoncao", 43},
    {"ngoi", 3}, {"ngoixuong", 3}, {"sit", 3}, {"nghingoi", 16}, {"tatservo", 16},
    {"battay", 58}, {"votay", 57}, {"clap", 57}, {"covu", 55}, {"cheer", 55},
    {"muavo", 48}, {"kungfu", 48}, {"vothuat", 48}, {"vothuat1", 48},
    {"vothuat2", 44}, {"vothuat3", 45}, {"vothuat4", 46}, {"vothuat5", 47},
    {"dam", 29}, {"damthang", 29}, {"punch", 29}, {"damtrai", 15}, {"damphai", 20},
    {"da", 8}, {"kick", 8}, {"nedon", 53}, {"matrix", 53}, {"quatnga", 42},
    {"chongday", 40}, {"pushup", 40}, {"hitdat", 67}, {"chongday2", 67},
    {"trongchuoi", 9}, {"lonnhao", 21}, {"lontoi", 21}, {"lonnguoc", 31},
    {"lontrai", 28}, {"lonphai", 30},
    {"dungday", 25}, {"dungdayngasap", 25}, {"dungdayngangua", 19},
    {"thumon", 54}, {"goalkeeper", 54}, {"dung1chan", 56}, {"dungmotchan", 56},
    {"leocauthang", 66}, {"cauthang", 66}, {"stairs", 66}, {"upstair", 66},
    {"nhay", 64}, {"nhaymua", 64}, {"dance", 64}, {"dance1", 64},
    {"dance2", 65}, {"dance3", 68}, {"breakdance", 59}, {"hiphop", 59},
};

bool IsMusicOrRadioActive() {
    auto& app = Application::GetInstance();
    auto* radio = app.GetRadio();
    if (radio != nullptr && radio->IsPlaying()) {
        return true;
    }
    auto* music = app.GetMusic();
    if (music != nullptr && (music->IsDownloading() || music->GetBufferSize() > 0)) {
        return true;
    }
    return false;
}

}  // namespace

RobonovaController& RobonovaController::GetInstance() {
    static RobonovaController instance;
    return instance;
}

RobonovaController::~RobonovaController() {
    if (action_task_handle_) vTaskDelete(action_task_handle_);
    if (rx_task_handle_) vTaskDelete(rx_task_handle_);
    if (auto_task_handle_) vTaskDelete(auto_task_handle_);
    if (action_queue_) vQueueDelete(action_queue_);
    if (ack_sem_) vSemaphoreDelete(ack_sem_);
}

void RobonovaController::Initialize() {
    if (initialized_) {
        return;
    }

    // Khởi tạo UART1: TX = GPIO 42 (-> MR-C3024FX ERX), RX = GPIO 41 (<- MR-C3024FX ETX), 9600 8N1
    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate = ROBONOVA_UART_BAUD_RATE;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(ROBONOVA_UART_NUM, 512, 256, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(ROBONOVA_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(ROBONOVA_UART_NUM,
                                 ROBONOVA_UART_TX_PIN,
                                 ROBONOVA_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    // Khôi phục cấu hình chế độ cảm biến từ NVS
    {
        Settings settings("robonova", false);
        auto_sensor_mode_.store(settings.GetInt("auto_sensor", 0) != 0);
        auto_climb_stairs_.store(settings.GetInt("auto_stairs", 0) != 0);
    }

    action_queue_ = xQueueCreate(32, sizeof(QueuedItem));
    ack_sem_ = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(ActionTaskEntry, "robonova_act", 4096, this, 5, &action_task_handle_, 1);
    xTaskCreatePinnedToCore(UartRxTaskEntry, "robonova_rx", 4096, this, 5, &rx_task_handle_, 1);
    xTaskCreatePinnedToCore(AutoBehaviorTaskEntry, "robonova_auto", 4096, this, 4, &auto_task_handle_, 1);

    initialized_ = true;
    ESP_LOGI(TAG, "RobonovaController initialized on UART%d (TX=%d, RX=%d, %d bps, 63 actions K1..K68)",
             static_cast<int>(ROBONOVA_UART_NUM),
             static_cast<int>(ROBONOVA_UART_TX_PIN),
             static_cast<int>(ROBONOVA_UART_RX_PIN),
             ROBONOVA_UART_BAUD_RATE);
}

const std::vector<RobonovaActionInfo>& RobonovaController::GetAllActions() {
    return kRobonovaActions;
}

const RobonovaActionInfo* RobonovaController::FindActionByK(int k_num) {
    for (const auto& item : kRobonovaActions) {
        if (item.k_num == k_num) {
            return &item;
        }
    }
    return nullptr;
}

std::string RobonovaController::NormalizeKey(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

const RobonovaActionInfo* RobonovaController::FindActionBySlugOrName(const std::string& query) {
    if (query.empty()) {
        return nullptr;
    }

    std::string norm = NormalizeKey(query);
    if (norm.empty()) {
        return nullptr;
    }

    // Hỗ trợ định dạng "K48", "k11" hoặc số nguyên "48"
    if (norm[0] == 'k' && norm.size() > 1 && std::all_of(norm.begin() + 1, norm.end(), ::isdigit)) {
        return FindActionByK(std::atoi(norm.c_str() + 1));
    }
    if (std::all_of(norm.begin(), norm.end(), ::isdigit)) {
        int val = std::atoi(norm.c_str());
        if (const auto* by_k = FindActionByK(val)) {
            return by_k;
        }
        // Nếu người dùng truyền trực tiếp mã DEC (40..108)
        for (const auto& item : kRobonovaActions) {
            if (item.uart_byte == val) {
                return &item;
            }
        }
    }

    // Khớp chính xác slug
    for (const auto& item : kRobonovaActions) {
        if (NormalizeKey(item.slug) == norm) {
            return &item;
        }
    }

    // Khớp từ điển đồng nghĩa tiếng Việt / Anh
    for (const auto& alias : kActionAliases) {
        if (norm == alias.alias) {
            return FindActionByK(alias.k_num);
        }
    }

    // Khớp gần đúng (chứa từ khóa trong slug hoặc tên tiếng Việt)
    for (const auto& item : kRobonovaActions) {
        std::string slug_norm = NormalizeKey(item.slug);
        if (slug_norm.find(norm) != std::string::npos || norm.find(slug_norm) != std::string::npos) {
            return &item;
        }
    }
    return nullptr;
}

bool RobonovaController::SendRawUartByte(uint8_t byte_val) {
    if (!initialized_) {
        Initialize();
    }
    int written = uart_write_bytes(ROBONOVA_UART_NUM, reinterpret_cast<const char*>(&byte_val), 1);
    ESP_LOGI(TAG, "UART TX -> MR-C3024FX ERX: DEC=%u CHAR='%c' (written=%d)",
             static_cast<unsigned>(byte_val),
             (byte_val >= 32 && byte_val <= 126) ? static_cast<char>(byte_val) : '?',
             written);
    return written == 1;
}

bool RobonovaController::QueueActionByK(int k_num, int repeat) {
    const auto* info = FindActionByK(k_num);
    if (info == nullptr) {
        ESP_LOGW(TAG, "Invalid K action: K%d", k_num);
        return false;
    }
    if (repeat < 1) repeat = 1;
    if (repeat > 10) repeat = 10;

    QueuedItem item{
        .k_num = info->k_num,
        .uart_byte = info->uart_byte,
        .duration_ms = info->duration_ms,
        .name_vi = info->name_vi,
        .emotion = info->emotion,
    };

    bool ok = false;
    for (int i = 0; i < repeat; ++i) {
        if (xQueueSend(action_queue_, &item, pdMS_TO_TICKS(50)) == pdTRUE) {
            ok = true;
        }
    }
    NotifyStatusChanged();
    return ok;
}

bool RobonovaController::QueueActionByName(const std::string& name_or_k, int repeat) {
    const auto* info = FindActionBySlugOrName(name_or_k);
    if (info == nullptr) {
        return false;
    }
    return QueueActionByK(info->k_num, repeat);
}

int RobonovaController::QueueCombo(const std::string& combo_csv) {
    int count = 0;
    std::stringstream ss(combo_csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (QueueActionByName(token, 1)) {
            ++count;
        }
    }
    return count;
}

void RobonovaController::StopAll(bool relax_servos) {
    dance_with_music_.store(false);
    abort_current_wait_.store(true);
    if (action_queue_ != nullptr) {
        xQueueReset(action_queue_);
    }
    if (ack_sem_ != nullptr) {
        xSemaphoreGive(ack_sem_);
    }
    if (relax_servos) {
        QueueActionByK(16, 1);  // K16: Ngồi nghỉ / Tắt mở servo
    } else {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_action_k_.store(0);
        current_action_name_ = "Đã dừng (Đứng chuẩn)";
    }
    NotifyStatusChanged();
}

void RobonovaController::SetDanceWithMusic(bool enable) {
    dance_with_music_.store(enable);
    if (!enable && action_queue_ != nullptr) {
        xQueueReset(action_queue_);
        abort_current_wait_.store(true);
    }
    NotifyStatusChanged();
}

void RobonovaController::SetAutoSensorMode(bool enable) {
    auto_sensor_mode_.store(enable);
    Settings settings("robonova", true);
    settings.SetInt("auto_sensor", enable ? 1 : 0);
    NotifyStatusChanged();
}

void RobonovaController::SetAutoClimbStairs(bool enable) {
    auto_climb_stairs_.store(enable);
    Settings settings("robonova", true);
    settings.SetInt("auto_stairs", enable ? 1 : 0);
    NotifyStatusChanged();
}

std::string RobonovaController::GetCurrentActionName() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_action_name_;
}

size_t RobonovaController::GetQueueCount() const {
    if (action_queue_ == nullptr) return 0;
    return static_cast<size_t>(uxQueueMessagesWaiting(action_queue_));
}

RobonovaTelemetry RobonovaController::GetTelemetry() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return telemetry_;
}

void RobonovaController::SetStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_callback_ = std::move(cb);
}

void RobonovaController::NotifyStatusChanged() {
    StatusCallback cb;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        cb = status_callback_;
    }
    if (cb) {
        cb(BuildStatusJson());
    }
}

void RobonovaController::ActionTaskEntry(void* arg) {
    static_cast<RobonovaController*>(arg)->RunActionTask();
}

void RobonovaController::RunActionTask() {
    QueuedItem item{};
    while (true) {
        if (xQueueReceive(action_queue_, &item, pdMS_TO_TICKS(250)) == pdTRUE) {
            is_executing_.store(true);
            abort_current_wait_.store(false);
            current_action_k_.store(item.k_num);
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                current_action_name_ = std::string("K") + std::to_string(item.k_num) + " - " + item.name_vi;
            }

            // Cập nhật biểu cảm mắt trên màn hình 1.54" ST7789 (nếu không đang hiển thị FFT nhạc)
            auto* display = Board::GetInstance().GetDisplay();
            if (display != nullptr && !IsMusicOrRadioActive()) {
                display->SetEmotion(item.emotion);
            }

            // Xóa tín hiệu ACK cũ trước khi gửi lệnh mới xuống MR-C3024FX
            xSemaphoreTake(ack_sem_, 0);
            SendRawUartByte(item.uart_byte);
            NotifyStatusChanged();

            // Chờ động tác hoàn tất: ưu tiên nhận byte ACK ('!' = 33) từ MR-C3024FX,
            // nếu mạch robot chạy code hãng gốc (chưa nạp bản cảm biến) thì chờ theo duration_ms.
            int waited_ms = 0;
            const int slice_ms = 100;
            while (waited_ms < item.duration_ms && !abort_current_wait_.load()) {
                if (xSemaphoreTake(ack_sem_, pdMS_TO_TICKS(slice_ms)) == pdTRUE) {
                    ESP_LOGI(TAG, "Received action-complete ACK from MR-C3024FX for K%d", item.k_num);
                    break;
                }
                waited_ms += slice_ms;
            }

            if (uxQueueMessagesWaiting(action_queue_) == 0) {
                is_executing_.store(false);
                current_action_k_.store(0);
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    current_action_name_ = "Đứng nghỉ";
                }
                if (display != nullptr && !IsMusicOrRadioActive()) {
                    display->SetEmotion("staticstate");
                }
                NotifyStatusChanged();
            }
        }
    }
}

void RobonovaController::UartRxTaskEntry(void* arg) {
    static_cast<RobonovaController*>(arg)->RunUartRxTask();
}

void RobonovaController::RunUartRxTask() {
    uint8_t rx_buf[64];
    uint8_t frame[5];
    int frame_idx = 0;
    std::string ascii_line;

    while (true) {
        int len = uart_read_bytes(ROBONOVA_UART_NUM, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(100));
        if (len <= 0) {
            // Đánh dấu mất kết nối telemetry nếu quá 8 giây không nhận gói mới
            bool notify = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                int64_t now_ms = esp_timer_get_time() / 1000;
                if (telemetry_.connected && (now_ms - telemetry_.last_update_ms > 8000)) {
                    telemetry_.connected = false;
                    notify = true;
                }
            }
            if (notify) NotifyStatusChanged();
            continue;
        }

        bool telemetry_updated = false;
        for (int i = 0; i < len; ++i) {
            uint8_t b = rx_buf[i];

            // Byte '!' (DEC 33): MR-C3024FX báo vừa hoàn thành động tác tại main_exit
            if (frame_idx == 0 && b == '!') {
                xSemaphoreGive(ack_sem_);
                continue;
            }

            // Giao thức gói 5 byte nhị phân: [0xAA, batt_ad6, sonar_cm, flags, checksum]
            if (frame_idx == 0 && b == 0xAA) {
                frame[0] = b;
                frame_idx = 1;
                continue;
            }
            if (frame_idx > 0) {
                frame[frame_idx++] = b;
                if (frame_idx == 5) {
                    uint8_t expected_cs = (frame[1] ^ frame[2] ^ frame[3]) & 0x7F;
                    if (frame[4] == expected_cs || frame[4] == 0x55) {
                        uint8_t batt_raw = frame[1];
                        uint8_t sonar_raw = frame[2];
                        uint8_t flags = frame[3];

                        float voltage = (static_cast<float>(batt_raw) * 10.0f) / 256.0f;
                        int pct = static_cast<int>(((voltage - 5.8f) / (7.4f - 5.8f)) * 100.0f);
                        if (pct < 0) pct = 0;
                        if (pct > 100) pct = 100;

                        {
                            std::lock_guard<std::mutex> lock(state_mutex_);
                            telemetry_.connected = true;
                            telemetry_.battery_voltage = voltage;
                            telemetry_.battery_percent = pct;
                            telemetry_.sonar_cm = (sonar_raw == 255) ? -1 : static_cast<int>(sonar_raw);
                            telemetry_.foot_10nk_detected = (flags & 0x01) != 0;
                            telemetry_.tilt_fallen_back = (flags & 0x02) != 0;
                            telemetry_.tilt_fallen_fwd = (flags & 0x04) != 0;
                            telemetry_.low_battery_alarm = (flags & 0x08) != 0;
                            telemetry_.last_update_ms = esp_timer_get_time() / 1000;
                        }
                        telemetry_updated = true;
                    }
                    frame_idx = 0;
                }
                continue;
            }

            // Hỗ trợ thêm định dạng ASCII dự phòng: "S:<sonar>,<foot>,<batt>\n"
            if (b == '\n' || b == '\r') {
                if (ascii_line.rfind("S:", 0) == 0) {
                    int s_cm = -1, foot = 0, batt_raw = 0;
                    if (std::sscanf(ascii_line.c_str() + 2, "%d,%d,%d", &s_cm, &foot, &batt_raw) >= 2) {
                        float voltage = (batt_raw > 0) ? ((batt_raw * 10.0f) / 256.0f) : 6.5f;
                        int pct = static_cast<int>(((voltage - 5.8f) / 1.6f) * 100.0f);
                        pct = std::clamp(pct, 0, 100);
                        std::lock_guard<std::mutex> lock(state_mutex_);
                        telemetry_.connected = true;
                        telemetry_.sonar_cm = s_cm;
                        telemetry_.foot_10nk_detected = (foot != 0);
                        telemetry_.battery_voltage = voltage;
                        telemetry_.battery_percent = pct;
                        telemetry_.last_update_ms = esp_timer_get_time() / 1000;
                        telemetry_updated = true;
                    }
                }
                ascii_line.clear();
            } else if (ascii_line.size() < 48 && b >= 32 && b <= 126) {
                ascii_line.push_back(static_cast<char>(b));
            }
        }

        if (telemetry_updated) {
            NotifyStatusChanged();
        }
    }
}

void RobonovaController::AutoBehaviorTaskEntry(void* arg) {
    static_cast<RobonovaController*>(arg)->RunAutoBehaviorTask();
}

void RobonovaController::RunAutoBehaviorTask() {
    const int kDanceCycle[] = {6, 64, 57, 65, 55, 68, 59};
    size_t dance_idx = 0;
    bool turn_toggle = false;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(400));

        // 1. Chế độ Nhảy theo nhạc / Radio VOV
        if (dance_with_music_.load()) {
            if (!IsMusicOrRadioActive()) {
                // Nếu nhạc chưa kịp tải xong trong 3s đầu thì vẫn chờ, chỉ xếp động tác khi rảnh
            }
            if (!is_executing_.load() && GetQueueCount() == 0) {
                QueueActionByK(kDanceCycle[dance_idx % (sizeof(kDanceCycle) / sizeof(kDanceCycle[0]))], 1);
                ++dance_idx;
            }
            continue;
        }

        // 2. Chế độ Tự hành thông minh (Mắt siêu âm + Cảm biến chân 10-NK + Cảm biến nghiêng AD5)
        if (auto_sensor_mode_.load() && !is_executing_.load() && GetQueueCount() == 0) {
            RobonovaTelemetry tel = GetTelemetry();
            if (!tel.connected) {
                continue;
            }

            // Ưu tiên 1: Tự động đứng dậy nếu bị ngã
            if (tel.tilt_fallen_back) {
                ESP_LOGW(TAG, "AutoSensor: Fallen backward detected -> K19 standup_back");
                QueueActionByK(19, 1);
                continue;
            }
            if (tel.tilt_fallen_fwd) {
                ESP_LOGW(TAG, "AutoSensor: Fallen forward detected -> K25 standup_front");
                QueueActionByK(25, 1);
                continue;
            }

            // Ưu tiên 2: Cảm biến hồng ngoại 10-NK ở mũi chân trái (< 10cm) phát hiện bậc thềm / vật cản thấp
            if (tel.foot_10nk_detected) {
                if (auto_climb_stairs_.load() && (tel.sonar_cm < 0 || tel.sonar_cm > 30)) {
                    // Phía trên thoáng nhưng dưới chân có bậc -> Leo cầu thang K66!
                    ESP_LOGI(TAG, "AutoSensor: Foot 10-NK step detected + head clear -> K66 climb_stairs");
                    QueueActionByK(66, 1);
                } else {
                    ESP_LOGI(TAG, "AutoSensor: Foot 10-NK low obstacle -> step back K12 & turn");
                    QueueActionByK(12, 1);
                    QueueActionByK(turn_toggle ? 22 : 24, 2);
                    turn_toggle = !turn_toggle;
                }
                continue;
            }

            // Ưu tiên 3: Cảm biến siêu âm trên mắt phát hiện vật cản phía trước (< 25cm)
            if (tel.sonar_cm > 0 && tel.sonar_cm < 25) {
                if (tel.sonar_cm < 12) {
                    QueueActionByK(53, 1);  // Quá gần -> Né đòn Matrix K53
                }
                QueueActionByK(12, 1);      // Lùi 1 nhịp
                QueueActionByK(turn_toggle ? 34 : 35, 1);  // Quay nhanh tránh vật cản
                turn_toggle = !turn_toggle;
                continue;
            }

            // Đường thông thoáng -> Tiến về phía trước K11
            QueueActionByK(11, 1);
        }
    }
}

std::string RobonovaController::BuildStatusJson() const {
    RobonovaTelemetry tel = GetTelemetry();
    auto& wifi = WifiStation::GetInstance();
    std::string ip = wifi.GetIpAddress();
    std::string web_url = ip.empty() ? "" : ("http://" + ip + ":" + std::to_string(ROBONOVA_WEB_PORT));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "robot", "DB-Robonova2 ESP32-S3 AI");
    cJSON_AddStringToObject(root, "version", DB_ROBONOVA2_VERSION);
    cJSON_AddStringToObject(root, "ip", ip.c_str());
    cJSON_AddNumberToObject(root, "web_port", ROBONOVA_WEB_PORT);
    cJSON_AddStringToObject(root, "web_url", web_url.c_str());
    cJSON_AddBoolToObject(root, "busy", is_executing_.load());
    cJSON_AddNumberToObject(root, "current_k", current_action_k_.load());
    cJSON_AddStringToObject(root, "current_action", GetCurrentActionName().c_str());
    cJSON_AddNumberToObject(root, "queue_size", static_cast<double>(GetQueueCount()));
    cJSON_AddBoolToObject(root, "dance_with_music", dance_with_music_.load());
    cJSON_AddBoolToObject(root, "auto_sensor_mode", auto_sensor_mode_.load());
    cJSON_AddBoolToObject(root, "auto_climb_stairs", auto_climb_stairs_.load());

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        cJSON_AddNumberToObject(root, "volume", codec->output_volume());
    }
    auto* backlight = Board::GetInstance().GetBacklight();
    if (backlight != nullptr) {
        cJSON_AddNumberToObject(root, "brightness", backlight->brightness());
    }

    cJSON* sensor = cJSON_CreateObject();
    cJSON_AddBoolToObject(sensor, "telemetry_online", tel.connected);
    cJSON_AddNumberToObject(sensor, "battery_voltage", tel.battery_voltage);
    cJSON_AddNumberToObject(sensor, "battery_percent", tel.battery_percent);
    cJSON_AddNumberToObject(sensor, "ultrasonic_cm", tel.sonar_cm);
    cJSON_AddBoolToObject(sensor, "foot_10nk_obstacle", tel.foot_10nk_detected);
    cJSON_AddBoolToObject(sensor, "fallen_backward", tel.tilt_fallen_back);
    cJSON_AddBoolToObject(sensor, "fallen_forward", tel.tilt_fallen_fwd);
    cJSON_AddBoolToObject(sensor, "low_battery", tel.low_battery_alarm);
    cJSON_AddItemToObject(root, "sensors", sensor);

    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed ? printed : "{}";
    if (printed) cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}

void RobonovaController::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.robonova.action",
        "Điều khiển robot hình người Robonova2 thực hiện đầy đủ 68 động tác nguyên gốc của hãng.\n"
        "Dùng công cụ này khi người dùng ra lệnh bằng giọng nói yêu cầu robot cử động.\n"
        "Các giá trị `action` phổ biến (hoặc truyền mã K1..K68):\n"
        "- Di chuyển: walk_forward (K11 đi tới), walk_backward (K12 đi lùi), fast_walk (K10 đi nhanh), "
        "new_fast_walk (K33 đi rất nhanh), fast_run (K50 chạy nhanh), turn_left (K22 quay trái), "
        "turn_right (K24 quay phải), fast_left_turn (K34), fast_right_turn (K35), shift_left (K14 bước ngang trái), "
        "shift_right (K13 bước ngang phải), left_forward (K17), right_forward (K27), sit_walk_fwd (K60), climb_stairs (K66 leo cầu thang).\n"
        "- Chào hỏi & Cơ bản: bow (K1 cúi chào), honor_bow (K41 chào danh dự), shake_hand (K58 bắt tay), "
        "clap_hands (K57 vỗ tay), cheer_up (K55 cổ vũ), hands_up (K2 giơ 2 tay), sit_down (K3 ngồi xuống), "
        "sit_hands_up (K4), big_reach (K43 vươn vai), toggle_sit_relax (K16 ngồi nghỉ tắt servo).\n"
        "- Võ thuật & Kungfu: kungfu_1 (K48 múa võ 1), kungfu_2 (K44), kungfu_3 (K45), kungfu_4 (K46), kungfu_5 (K47), "
        "forward_punch (K29 đấm thẳng), left_attack (K15 đấm trái), right_attack (K20 đấm phải), "
        "kick_both (K8 đá 2 chân), big_toss (K42 quật ngã), avoid_matrix (K53 né đòn Matrix).\n"
        "- Thể lực & Nhào lộn: pushup_1 (K40 chống đẩy có nhạc), pushup_2 (K67 chống đẩy 10 cái), "
        "handstand (K9 trồng chuối), single_foot (K56 đứng 1 chân), goalkeeper (K54 thủ môn), "
        "tumble_forward (K21 lộn nhào tới), tumble_back (K31 lộn ngược), tumble_left (K28), tumble_right (K30), "
        "standup_back (K19 đứng dậy khi nằm ngửa), standup_front (K25 đứng dậy khi nằm sấp).\n"
        "- Nhảy múa: simple_dance (K6 lắc thân), dance_1 (K64), dance_2 (K65), dance_3 (K68), break_dance (K59 nhảy breakdance), wing_move (K7).",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("repeat", kPropertyTypeInteger, 1, 1, 10),
        }),
        [this](const PropertyList& props) -> ReturnValue {
            std::string action = props["action"].value<std::string>();
            int repeat = props["repeat"].value<int>();
            const auto* info = FindActionBySlugOrName(action);
            if (info == nullptr) {
                return std::string("{\"success\":false,\"error\":\"Không tìm thấy động tác: ") + action + "\"}";
            }
            bool ok = QueueActionByK(info->k_num, repeat);
            return std::string("{\"success\":") + (ok ? "true" : "false") +
                   ",\"k\":" + std::to_string(info->k_num) +
                   ",\"action\":\"" + info->name_vi + "\",\"repeat\":" + std::to_string(repeat) + "}";
        });

    mcp.AddTool(
        "self.robonova.combo",
        "Thực hiện chuỗi liên hoàn nhiều động tác võ thuật / nhảy múa / biểu diễn theo thứ tự.\n"
        "Ví dụ `sequence`: \"K41,K48,K29,K8,K53,K1\" hoặc \"bow,kungfu_1,pushup_1,clap_hands\".",
        PropertyList({
            Property("sequence", kPropertyTypeString),
        }),
        [this](const PropertyList& props) -> ReturnValue {
            std::string seq = props["sequence"].value<std::string>();
            int queued = QueueCombo(seq);
            return std::string("{\"success\":") + (queued > 0 ? "true" : "false") +
                   ",\"queued_actions\":" + std::to_string(queued) + "}";
        });

    mcp.AddTool(
        "self.robonova.dance_music",
        "Bật hoặc tắt chế độ robot vừa phát nhạc trực tuyến vừa nhảy múa đồng bộ theo nhạc.\n"
        "Nếu truyền `song_name`, robot sẽ tải phát bài hát đó và bắt đầu nhảy.",
        PropertyList({
            Property("enable", kPropertyTypeBoolean, true),
            Property("song_name", kPropertyTypeString, std::string("")),
        }),
        [this](const PropertyList& props) -> ReturnValue {
            bool enable = props["enable"].value<bool>();
            std::string song = props["song_name"].value<std::string>();
            if (enable && !song.empty()) {
                auto* music = Application::GetInstance().GetMusic();
                if (music != nullptr) {
                    music->Download(song, "");
                }
            }
            SetDanceWithMusic(enable);
            return enable ? std::string("{\"success\":true,\"message\":\"Đã bật chế độ vừa hát vừa nhảy\"}")
                          : std::string("{\"success\":true,\"message\":\"Đã tắt chế độ nhảy theo nhạc\"}");
        });

    mcp.AddTool(
        "self.robonova.auto_sensor_mode",
        "Bật/tắt chế độ tự hành thông minh sử dụng cảm biến siêu âm trên mắt và cảm biến khoảng cách 10-NK dưới bàn chân trái "
        "(tự tránh vật cản, tự đứng dậy khi ngã, tùy chọn tự leo bậc cầu thang).",
        PropertyList({
            Property("enable", kPropertyTypeBoolean),
            Property("auto_climb_stairs", kPropertyTypeBoolean, false),
        }),
        [this](const PropertyList& props) -> ReturnValue {
            bool enable = props["enable"].value<bool>();
            bool stairs = props["auto_climb_stairs"].value<bool>();
            SetAutoClimbStairs(stairs);
            SetAutoSensorMode(enable);
            return BuildStatusJson();
        });

    mcp.AddTool(
        "self.robonova.stop",
        "Dừng ngay lập tức mọi chuỗi động tác đang xếp hàng, tắt chế độ nhảy/tự hành. "
        "Đặt `relax_servos=true` nếu người dùng yêu cầu 'ngồi nghỉ tắt động cơ'.",
        PropertyList({
            Property("relax_servos", kPropertyTypeBoolean, false),
        }),
        [this](const PropertyList& props) -> ReturnValue {
            bool relax = props["relax_servos"].value<bool>();
            StopAll(relax);
            return std::string("{\"success\":true,\"message\":\"Đã dừng robot\"}");
        });

    mcp.AddTool(
        "self.robonova.get_status",
        "Lấy trạng thái chi tiết của robot Robonova2: địa chỉ Web điều khiển cổng 8080 (VD http://192.168.1.68:8080), "
        "động tác hiện tại, mức pin, khoảng cách mắt siêu âm (cm), và trạng thái cảm biến chân 10-NK.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return BuildStatusJson();
        });

    ESP_LOGI(TAG, "Registered 6 Robonova2 MCP tools");
}
