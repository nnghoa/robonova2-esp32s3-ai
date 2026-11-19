#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"

#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <cJSON.h>
#include <sstream>
#include <cstdlib>

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

    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
    }

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
                        ESP_LOGI(TAG, "Command: '%s' -> '%s' (%s)",
                                 command_name->valuestring, text->valuestring, action->valuestring);
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
    wakenet_words_.clear();

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
        if (commands_.empty()) {
            commands_.push_back({CONFIG_CUSTOM_WAKE_WORD, CONFIG_CUSTOM_WAKE_WORD_DISPLAY, "wake"});
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
        ESP_LOGI(TAG, "Loaded SR Model [%d]: %s", i, models_->model_name[i]);
        if (strstr(models_->model_name[i], ESP_WN_PREFIX) != nullptr) {
            auto words = esp_srmodel_get_wake_words(models_, models_->model_name[i]);
            if (words != nullptr) {
                std::stringstream ss(words);
                std::string word;
                while (std::getline(ss, word, ';')) {
                    if (!word.empty()) {
                        wakenet_words_.push_back(word);
                        ESP_LOGI(TAG, "WakeNet fallback active: '%s'", word.c_str());
                    }
                }
            }
        }
    }

    // 1. Initialize MultiNet7 for custom wake word ("Hi Mina" / "Hey Mina" / "Mina")
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGW(TAG, "Language '%s' multinet not found, falling back to any multinet model", language_.c_str());
        mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
    }
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize multinet, mn_name is nullptr");
        return false;
    }

    multinet_ = esp_mn_handle_from_name(mn_name_);
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    multinet_->set_det_threshold(multinet_model_data_, threshold_);
    esp_mn_commands_clear();
    for (size_t i = 0; i < commands_.size(); i++) {
        esp_err_t add_err = esp_mn_commands_add(static_cast<int>(i + 1), commands_[i].command.c_str());
        if (add_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add phrase [%d] '%s': err=0x%x",
                     static_cast<int>(i + 1), commands_[i].command.c_str(), add_err);
        }
    }
    esp_mn_error_t* mn_err = esp_mn_commands_update();
    if (mn_err != nullptr) {
        ESP_LOGW(TAG, "esp_mn_commands_update reported %d unparsed phrase(s)", mn_err->num);
    }
    multinet_->print_active_speech_commands(multinet_model_data_);
    ESP_LOGI(TAG, "MultiNet '%s' ready (threshold=%.2f, duration=%d ms)",
             mn_name_, threshold_, duration_);

    // 2. Initialize Espressif AFE (Noise Suppression + WebRTC AGC + optional WakeNet9)
    int ref_num = codec_->input_reference() ? 1 : 0;
    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models_, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config != nullptr) {
        afe_config->aec_init = codec_->input_reference();
        afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
        afe_config->agc_init = true;
        afe_config->agc_mode = AFE_AGC_MODE_WEBRTC;
        afe_config->agc_compression_gain_db = 12;
        afe_config->agc_target_level_dbfs = 3;
        afe_config->afe_linear_gain = 1.6f;
        afe_config->afe_perferred_core = 1;
        afe_config->afe_perferred_priority = 1;
        afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

        afe_iface_ = esp_afe_handle_from_config(afe_config);
        if (afe_iface_ != nullptr) {
            afe_data_ = afe_iface_->create_from_config(afe_config);
        }
    }

    if (afe_data_ != nullptr) {
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
                3,
                detection_task_stack_,
                detection_task_buffer_);
            ESP_LOGI(TAG, "Hybrid AFE + MultiNet wake word task started (feed=%d, fetch=%d)",
                     afe_iface_->get_feed_chunksize(afe_data_),
                     afe_iface_->get_fetch_chunksize(afe_data_));
        } else {
            ESP_LOGE(TAG, "Failed to allocate custom_ww_det task buffers, using direct MultiNet fallback");
            afe_iface_->destroy(afe_data_);
            afe_data_ = nullptr;
        }
    }

    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    need_mn_clean_.store(true);
    running_ = true;
    if (afe_data_ != nullptr && event_group_ != nullptr) {
        xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
    }
}

void CustomWakeWord::Stop() {
    running_ = false;
    if (event_group_ != nullptr) {
        xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    }
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    need_mn_clean_.store(true);
}

void CustomWakeWord::AudioDetectionTask() {
    const size_t mn_chunksize = static_cast<size_t>(multinet_->get_samp_chunksize(multinet_model_data_));
    mn_accum_.reserve(mn_chunksize * 2);

    int chunks_since_log = 0;
    int16_t window_peak = 0;

    while (true) {
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        if (need_mn_clean_.exchange(false)) {
            multinet_->clean(multinet_model_data_);
            mn_accum_.clear();
            chunks_since_log = 0;
            window_peak = 0;
        }

        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if (!running_.load()) {
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL || res->data == nullptr || res->data_size <= 0) {
            continue;
        }

        const size_t samples = static_cast<size_t>(res->data_size) / sizeof(int16_t);
        for (size_t i = 0; i < samples; ++i) {
            int16_t v = res->data[i];
            int16_t av = (v == INT16_MIN) ? INT16_MAX : static_cast<int16_t>(std::abs(v));
            if (av > window_peak) {
                window_peak = av;
            }
        }
        if (++chunks_since_log >= 156) {  // ~5 seconds at 32ms/chunk
            ESP_LOGI(TAG, "Wake listener active (afe_peak=%d) — say 'Hi Mina' or 'Hi ESP'", window_peak);
            chunks_since_log = 0;
            window_peak = 0;
        }

        StoreWakeWordData(res->data, samples);

        // 1. Check WakeNet9 ("Hi, ESP") if packaged alongside MultiNet
        if (res->wakeup_state == WAKENET_DETECTED && running_.load()) {
            std::string detected = "Hi Mina";
            if (res->wakenet_model_index >= 1 &&
                res->wakenet_model_index <= static_cast<int>(wakenet_words_.size())) {
                ESP_LOGI(TAG, "WakeNet9 detected: '%s' -> waking Mina!",
                         wakenet_words_[res->wakenet_model_index - 1].c_str());
            } else {
                ESP_LOGI(TAG, "WakeNet9 detected -> waking Mina!");
            }
            Stop();
            last_detected_wake_word_ = detected;
            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            }
            continue;
        }

        // 2. Check MultiNet7 ("Hi Mina" / "Hey Mina" / "Hello Mina" / "Mina")
        mn_accum_.insert(mn_accum_.end(), res->data, res->data + samples);
        while (mn_accum_.size() >= mn_chunksize && running_.load()) {
            esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, mn_accum_.data());
            mn_accum_.erase(mn_accum_.begin(), mn_accum_.begin() + mn_chunksize);

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
                for (int i = 0; i < mn_result->num && running_.load(); i++) {
                    int cmd_idx = mn_result->command_id[i] - 1;
                    ESP_LOGI(TAG, "Custom wake word detected: command_id=%d, string='%s', prob=%.3f",
                             mn_result->command_id[i], mn_result->string, mn_result->prob[i]);
                    if (cmd_idx >= 0 && cmd_idx < static_cast<int>(commands_.size())) {
                        auto& command = commands_[cmd_idx];
                        if (command.action == "wake") {
                            last_detected_wake_word_ = command.text;
                            Stop();
                            multinet_->clean(multinet_model_data_);
                            mn_accum_.clear();
                            if (wake_word_detected_callback_) {
                                wake_word_detected_callback_(last_detected_wake_word_);
                            }
                            break;
                        }
                    }
                }
                multinet_->clean(multinet_model_data_);
            } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
                multinet_->clean(multinet_model_data_);
            }
        }
    }
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr || !running_.load()) {
        return;
    }

    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->feed(afe_data_, data.data());
        return;
    }

    // Fallback path if AFE was not created
    esp_mn_state_t mn_state;
    if (codec_->input_channels() == 2) {
        auto mono_data = std::vector<int16_t>(data.size() / 2);
        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
            mono_data[i] = data[j];
        }
        StoreWakeWordData(mono_data.data(), mono_data.size());
        mn_state = multinet_->detect(multinet_model_data_, mono_data.data());
    } else {
        StoreWakeWordData(data.data(), data.size());
        mn_state = multinet_->detect(multinet_model_data_, const_cast<int16_t*>(data.data()));
    }

    if (mn_state == ESP_MN_STATE_DETECTED) {
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        for (int i = 0; i < mn_result->num && running_.load(); i++) {
            int cmd_idx = mn_result->command_id[i] - 1;
            ESP_LOGI(TAG, "Custom wake word detected: command_id=%d, string='%s', prob=%.3f",
                     mn_result->command_id[i], mn_result->string, mn_result->prob[i]);
            if (cmd_idx >= 0 && cmd_idx < static_cast<int>(commands_.size())) {
                auto& command = commands_[cmd_idx];
                if (command.action == "wake") {
                    last_detected_wake_word_ = command.text;
                    running_ = false;
                    if (wake_word_detected_callback_) {
                        wake_word_detected_callback_(last_detected_wake_word_);
                    }
                }
            }
        }
        multinet_->clean(multinet_model_data_);
    } else if (mn_state == ESP_MN_STATE_TIMEOUT) {
        multinet_->clean(multinet_model_data_);
    }
}

size_t CustomWakeWord::GetFeedSize() {
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        return afe_iface_->get_feed_chunksize(afe_data_);
    }
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
