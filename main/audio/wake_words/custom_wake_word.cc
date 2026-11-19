#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <cJSON.h>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <algorithm>

#define DETECTION_RUNNING_EVENT 1
#define TAG "CustomWakeWord"

CustomWakeWord::CustomWakeWord()
    : wake_word_pcm_(), wake_word_opus_() {
    event_group_ = xEventGroupCreate();
}

CustomWakeWord::~CustomWakeWord() {
    running_ = false;
    if (event_group_ != nullptr) {
        xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    }
    input_cv_.notify_all();

    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
    }

    if (detection_task_stack_ != nullptr) {
        heap_caps_free(detection_task_stack_);
    }
    if (detection_task_buffer_ != nullptr) {
        heap_caps_free(detection_task_buffer_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }
    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    if (owns_models_ && models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }

    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

void CustomWakeWord::ParseWakenetModelConfig() {
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "Failed to read index.json");
        return;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse index.json");
        return;
    }
    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
    if (cJSON_IsObject(multinet_model)) {
        cJSON* language = cJSON_GetObjectItem(multinet_model, "language");
        cJSON* duration = cJSON_GetObjectItem(multinet_model, "duration");
        cJSON* threshold = cJSON_GetObjectItem(multinet_model, "threshold");
        cJSON* commands = cJSON_GetObjectItem(multinet_model, "commands");
        if (cJSON_IsString(language)) {
            language_ = language->valuestring;
        }
        if (cJSON_IsNumber(duration)) {
            duration_ = duration->valueint;
        }
        if (cJSON_IsNumber(threshold)) {
            threshold_ = static_cast<float>(threshold->valuedouble);
        }
        if (cJSON_IsArray(commands)) {
            for (int i = 0; i < cJSON_GetArraySize(commands); i++) {
                cJSON* command = cJSON_GetArrayItem(commands, i);
                if (cJSON_IsObject(command)) {
                    cJSON* command_name = cJSON_GetObjectItem(command, "command");
                    cJSON* text = cJSON_GetObjectItem(command, "text");
                    cJSON* action = cJSON_GetObjectItem(command, "action");
                    if (cJSON_IsString(command_name) && cJSON_IsString(text) && cJSON_IsString(action)) {
                        commands_.push_back({command_name->valuestring, text->valuestring, action->valuestring});
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}

bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();

    if (models_list == nullptr) {
#if defined(CONFIG_SR_MN_EN_MULTINET7_QUANT) || defined(CONFIG_SR_MN_EN_MULTINET6_QUANT)
        language_ = "en";
#else
        language_ = "cn";
#endif
        models_ = esp_srmodel_init("model");
        owns_models_ = true;
#ifdef CONFIG_CUSTOM_WAKE_WORD
        threshold_ = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
        std::stringstream ss(CONFIG_CUSTOM_WAKE_WORD);
        std::string phrase;
        while (std::getline(ss, phrase, ';')) {
            if (!phrase.empty()) {
                commands_.push_back({phrase, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake"});
            }
        }
#endif
    } else {
        models_ = models_list;
        owns_models_ = false;
        ParseWakenetModelConfig();
    }

    if (models_ == nullptr || models_->num <= 0) {
        ESP_LOGE(TAG, "Failed to initialize SR models");
        return false;
    }

    for (int i = 0; i < models_->num; i++) {
        ESP_LOGI(TAG, "Available SR Model [%d]: %s", i, models_->model_name[i]);
    }

    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
    }
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to find multinet model");
        return false;
    }

    // Add extra Vietnamese-accent Pinyin variants if using Chinese MultiNet ("Hi Mina" / "Mina ơi" / "Ê Mina")
    if (strstr(mn_name_, "cn") != nullptr) {
        const char* extra_vn_phrases[] = {
            "hai mi na",
            "hei mi na",
            "he lou mi na",
            "ha luo mi na",
            "ni hao mi na",
            "mi na ei",
            "mi na a",
            "ei mi na",
            "a mi na",
            "mi na",
        };
        for (const char* p : extra_vn_phrases) {
            bool exists = std::any_of(commands_.begin(), commands_.end(),
                                      [p](const Command& c) { return c.command == p; });
            if (!exists) {
                commands_.push_back({p, "Hi Mina", "wake"});
            }
        }
    }

    // Sensitive threshold (0.12) without AFE/AGC clipping distortion
    if (threshold_ > 0.12f) {
        threshold_ = 0.12f;
    }

    multinet_ = esp_mn_handle_from_name(mn_name_);
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    multinet_->set_det_threshold(multinet_model_data_, threshold_);

    esp_mn_commands_clear();
    for (size_t i = 0; i < commands_.size(); i++) {
        esp_err_t add_err = esp_mn_commands_add(static_cast<int>(i + 1), commands_[i].command.c_str());
        if (add_err != ESP_OK) {
            ESP_LOGW(TAG, "Skipped unsupported syllable phrase [%d] '%s' (err=0x%x)",
                     static_cast<int>(i + 1), commands_[i].command.c_str(), add_err);
        }
    }
    esp_mn_error_t* mn_err = esp_mn_commands_update();
    if (mn_err != nullptr) {
        ESP_LOGW(TAG, "esp_mn_commands_update reported %d unparsed phrase(s)", mn_err->num);
    }
    multinet_->print_active_speech_commands(multinet_model_data_);

    const size_t chunksize = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_model_data_));
    input_buffer_.reserve(chunksize * 4);

    // Run MultiNet7 on its own 16KB PSRAM-stacked worker task (saves ~45KB internal SRAM vs dual-AFE)
    const size_t det_stack_size = 16384;
    if (detection_task_stack_ == nullptr) {
        detection_task_stack_ = static_cast<StackType_t*>(heap_caps_malloc(det_stack_size, MALLOC_CAP_SPIRAM));
    }
    if (detection_task_buffer_ == nullptr) {
        detection_task_buffer_ = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL));
    }
    if (detection_task_stack_ != nullptr && detection_task_buffer_ != nullptr) {
        detection_task_ = xTaskCreateStatic(
            [](void* arg) {
                static_cast<CustomWakeWord*>(arg)->AudioDetectionTask();
                vTaskDelete(nullptr);
            },
            "custom_ww_det",
            det_stack_size,
            this,
            5,
            detection_task_stack_,
            detection_task_buffer_);
        ESP_LOGI(TAG, "MultiNet '%s' worker started (threshold=%.2f, chunk=%u, free_sram=%u, min_sram=%u)",
                 mn_name_, threshold_, static_cast<unsigned>(chunksize),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    } else {
        ESP_LOGE(TAG, "Failed to allocate custom_ww_det task");
        return false;
    }

    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_buffer_.clear();
    }
    need_mn_clean_.store(true);
    running_.store(true);
    if (event_group_ != nullptr) {
        xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
    }
    input_cv_.notify_all();
}

void CustomWakeWord::Stop() {
    running_.store(false);
    if (event_group_ != nullptr) {
        xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    }
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_buffer_.clear();
    }
    need_mn_clean_.store(true);
    input_cv_.notify_all();
}

void CustomWakeWord::AudioDetectionTask() {
    const size_t chunksize = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_model_data_));
    std::vector<int16_t> chunk(chunksize);

    int chunks_since_log = 0;
    int16_t window_peak = 0;
    int64_t sum_sq = 0;
    size_t total_samples = 0;

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        if (need_mn_clean_.exchange(false)) {
            multinet_->clean(multinet_model_data_);
            chunks_since_log = 0;
            window_peak = 0;
            sum_sq = 0;
            total_samples = 0;
        }

        {
            std::unique_lock<std::mutex> lock(input_mutex_);
            input_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, chunksize]() {
                return !running_.load() || input_buffer_.size() >= chunksize;
            });
            if (!running_.load() || input_buffer_.size() < chunksize) {
                continue;
            }
            std::copy_n(input_buffer_.begin(), chunksize, chunk.begin());
            input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + chunksize);
        }

        for (size_t i = 0; i < chunksize; ++i) {
            int32_t v = chunk[i];
            int16_t av = (v <= -INT16_MAX) ? INT16_MAX : static_cast<int16_t>(std::abs(v));
            if (av > window_peak) {
                window_peak = av;
            }
            sum_sq += v * v;
        }
        total_samples += chunksize;

        if (++chunks_since_log >= 156) {  // ~5 seconds
            int rms = total_samples > 0 ? static_cast<int>(std::sqrt(static_cast<double>(sum_sq) / total_samples)) : 0;
            ESP_LOGI(TAG, "Mic ready: raw_peak=%d, raw_rms=%d, free_sram=%u, min_sram=%u",
                     window_peak, rms,
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
            chunks_since_log = 0;
            window_peak = 0;
            sum_sq = 0;
            total_samples = 0;
        }

        StoreWakeWordData(chunk.data(), chunksize);

        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, chunk.data());
        if (mn_state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
            for (int i = 0; i < mn_result->num && running_.load(); i++) {
                int cmd_idx = mn_result->command_id[i] - 1;
                ESP_LOGI(TAG, "WAKE WORD DETECTED: id=%d, phrase='%s', prob=%.3f",
                         mn_result->command_id[i], mn_result->string, mn_result->prob[i]);
                if (cmd_idx >= 0 && cmd_idx < static_cast<int>(commands_.size())) {
                    auto& command = commands_[cmd_idx];
                    if (command.action == "wake") {
                        last_detected_wake_word_ = command.text;
                        Stop();
                        multinet_->clean(multinet_model_data_);
                        if (wake_word_detected_callback_) {
                            wake_word_detected_callback_(last_detected_wake_word_);
                        }
                        break;
                    }
                }
            }
            multinet_->clean(multinet_model_data_);
        } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
            if (mn_result != nullptr && mn_result->num > 0) {
                ESP_LOGI(TAG, "Speech timeout (below %.2f): top_id=%d, phrase='%s', prob=%.3f",
                         threshold_, mn_result->command_id[0], mn_result->string, mn_result->prob[0]);
            }
            multinet_->clean(multinet_model_data_);
        }
    }
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr || !running_.load() || data.empty()) {
        return;
    }

    const size_t max_buffered = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_model_data_)) * 8;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        if (!running_.load()) {
            return;
        }
        if (codec_->input_channels() > 1) {
            const int ch = codec_->input_channels();
            for (size_t i = 0; i < data.size(); i += ch) {
                input_buffer_.push_back(data[i]);
            }
        } else {
            input_buffer_.insert(input_buffer_.end(), data.begin(), data.end());
        }
        if (input_buffer_.size() > max_buffered) {
            input_buffer_.erase(input_buffer_.begin(),
                                input_buffer_.begin() + (input_buffer_.size() - max_buffered));
        }
    }
    input_cv_.notify_one();
}

size_t CustomWakeWord::GetFeedSize() {
    if (multinet_model_data_ == nullptr) {
        return 0;
    }
    return multinet_->get_samp_chunksize(multinet_model_data_);
}

void CustomWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    wake_word_pcm_.emplace_back(data, data + samples);
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWord::EncodeWakeWordData() {
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0);

            int packets = 0;
            for (auto& pcm: this_->wake_word_pcm_) {
                encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(std::move(opus));
                    this_->wake_word_cv_.notify_all();
                });
                packets++;
            }
            this_->wake_word_pcm_.clear();

            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
