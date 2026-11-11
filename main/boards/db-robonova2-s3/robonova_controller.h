#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct RobonovaActionInfo {
    int k_num;               // Chỉ số K gốc của hãng: K1..K35, K40..K48, K50..K68 (68 động tác)
    uint8_t uart_byte;       // Mã ASCII/DEC gửi qua ERX 9600: 40..74, 80..88, 90..108
    const char* slug;        // Định danh ngắn gọn cho AI MCP & API
    const char* name_vi;     // Tên tiếng Việt chuẩn hiển thị Web :8080 & màn hình
    const char* category;    // Nhóm: basic | move | martial | dance | special
    int duration_ms;         // Thời gian ước lượng hoàn thành động tác (ms)
    const char* emotion;     // Biểu cảm mắt trên màn hình ST7789
};

struct RobonovaTelemetry {
    bool connected = false;          // Đã nhận gói phản hồi từ MR-C3024FX (ETX -> GPIO 41)
    float battery_voltage = 0.0f;    // Điện áp pin (V) từ AD(6): 10 * val / 256
    int battery_percent = 100;       // Phần trăm pin ước tính (%)
    int sonar_cm = -1;               // Khoảng cách cảm biến siêu âm trên mắt (cm), -1 = chưa có
    bool foot_10nk_detected = false; // Cảm biến hồng ngoại 10-NK dưới mũi chân trái phát hiện vật cản/bậc
    bool tilt_fallen_back = false;   // Cảm biến nghiêng AD(5) báo ngã ngửa
    bool tilt_fallen_fwd = false;    // Cảm biến nghiêng AD(5) báo ngã sấp
    bool low_battery_alarm = false;  // Cảnh báo pin yếu (< 5.8V)
    int64_t last_update_ms = 0;      // Thời điểm cập nhật gần nhất (esp_timer_get_time / 1000)
};

class RobonovaController {
public:
    static RobonovaController& GetInstance();

    RobonovaController(const RobonovaController&) = delete;
    RobonovaController& operator=(const RobonovaController&) = delete;

    void Initialize();
    void RegisterMcpTools();

    // Tra cứu danh sách 68 động tác gốc
    static const std::vector<RobonovaActionInfo>& GetAllActions();
    static const RobonovaActionInfo* FindActionByK(int k_num);
    static const RobonovaActionInfo* FindActionBySlugOrName(const std::string& query);

    // Điều khiển thực thi động tác
    bool QueueActionByK(int k_num, int repeat = 1);
    bool QueueActionByName(const std::string& name_or_k, int repeat = 1);
    int QueueCombo(const std::string& combo_csv);
    bool SendRawUartByte(uint8_t byte_val);
    void StopAll(bool relax_servos = false);

    // Chế độ nhảy theo nhạc & Tự hành cảm biến
    void SetDanceWithMusic(bool enable);
    bool IsDanceWithMusic() const { return dance_with_music_.load(); }

    void SetAutoSensorMode(bool enable);
    bool IsAutoSensorMode() const { return auto_sensor_mode_.load(); }

    void SetAutoClimbStairs(bool enable);
    bool IsAutoClimbStairs() const { return auto_climb_stairs_.load(); }

    // Trạng thái & Telemetry
    bool IsBusy() const { return is_executing_.load(); }
    int GetCurrentActionK() const { return current_action_k_.load(); }
    std::string GetCurrentActionName() const;
    size_t GetQueueCount() const;
    RobonovaTelemetry GetTelemetry() const;
    std::string BuildStatusJson() const;

    using StatusCallback = std::function<void(const std::string& status_json)>;
    void SetStatusCallback(StatusCallback cb);

private:
    RobonovaController() = default;
    ~RobonovaController();

    struct QueuedItem {
        int k_num;
        uint8_t uart_byte;
        int duration_ms;
        const char* name_vi;
        const char* emotion;
    };

    bool initialized_ = false;
    QueueHandle_t action_queue_ = nullptr;
    SemaphoreHandle_t ack_sem_ = nullptr;
    TaskHandle_t action_task_handle_ = nullptr;
    TaskHandle_t rx_task_handle_ = nullptr;
    TaskHandle_t auto_task_handle_ = nullptr;

    std::atomic<bool> is_executing_{false};
    std::atomic<bool> abort_current_wait_{false};
    std::atomic<int> current_action_k_{0};
    std::atomic<bool> dance_with_music_{false};
    std::atomic<bool> auto_sensor_mode_{false};
    std::atomic<bool> auto_climb_stairs_{false};
    std::atomic<bool> ack_supported_{false};  // true khi MR-C3024FX đã gửi ít nhất 1 byte ACK '!'

    mutable std::mutex state_mutex_;
    std::string current_action_name_ = "Đứng nghỉ";
    RobonovaTelemetry telemetry_;
    StatusCallback status_callback_;

    static void ActionTaskEntry(void* arg);
    static void UartRxTaskEntry(void* arg);
    static void AutoBehaviorTaskEntry(void* arg);

    void RunActionTask();
    void RunUartRxTask();
    void RunAutoBehaviorTask();
    void NotifyStatusChanged();
    static std::string NormalizeKey(const std::string& input);
};
