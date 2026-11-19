#include "xh_s3e_smart_audio.h"
#include "config.h"
#include "settings.h"

#include <esp_log.h>
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#define TAG "XhS3eSmartAudio"

namespace {

struct MicCandidate {
    bool is_pdm;
    gpio_num_t pin_a;   // PDM: clk  | STD: ws
    gpio_num_t pin_b;   // PDM: din  | STD: sck
    gpio_num_t pin_c;   // PDM: NC   | STD: din
    bool right_slot;
    const char* label;
};

static const MicCandidate kCandidates[] = {
    // 12 PDM modes on {GPIO 4, 5, 6} (ESP32-S3 PDM RX hardware is on I2S_NUM_0)
    {true,  GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_NC, false, "PDM(CLK=4,DIN=5,LEFT)"},
    {true,  GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_NC, true,  "PDM(CLK=4,DIN=5,RIGHT)"},
    {true,  GPIO_NUM_5, GPIO_NUM_4, GPIO_NUM_NC, false, "PDM(CLK=5,DIN=4,LEFT)"},
    {true,  GPIO_NUM_5, GPIO_NUM_4, GPIO_NUM_NC, true,  "PDM(CLK=5,DIN=4,RIGHT)"},
    {true,  GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_NC, false, "PDM(CLK=5,DIN=6,LEFT)"},
    {true,  GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_NC, true,  "PDM(CLK=5,DIN=6,RIGHT)"},
    {true,  GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_NC, false, "PDM(CLK=4,DIN=6,LEFT)"},
    {true,  GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_NC, true,  "PDM(CLK=4,DIN=6,RIGHT)"},
    {true,  GPIO_NUM_6, GPIO_NUM_4, GPIO_NUM_NC, false, "PDM(CLK=6,DIN=4,LEFT)"},
    {true,  GPIO_NUM_6, GPIO_NUM_4, GPIO_NUM_NC, true,  "PDM(CLK=6,DIN=4,RIGHT)"},
    {true,  GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_NC, false, "PDM(CLK=6,DIN=5,LEFT)"},
    {true,  GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_NC, true,  "PDM(CLK=6,DIN=5,RIGHT)"},

    // 12 Standard I2S modes on {GPIO 4, 5, 6}
    {false, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6,  false, "I2S(WS=4,SCK=5,DIN=6,LEFT)"},
    {false, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6,  true,  "I2S(WS=4,SCK=5,DIN=6,RIGHT)"},
    {false, GPIO_NUM_5, GPIO_NUM_4, GPIO_NUM_6,  false, "I2S(WS=5,SCK=4,DIN=6,LEFT)"},
    {false, GPIO_NUM_5, GPIO_NUM_4, GPIO_NUM_6,  true,  "I2S(WS=5,SCK=4,DIN=6,RIGHT)"},
    {false, GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_5,  false, "I2S(WS=4,SCK=6,DIN=5,LEFT)"},
    {false, GPIO_NUM_4, GPIO_NUM_6, GPIO_NUM_5,  true,  "I2S(WS=4,SCK=6,DIN=5,RIGHT)"},
    {false, GPIO_NUM_6, GPIO_NUM_4, GPIO_NUM_5,  false, "I2S(WS=6,SCK=4,DIN=5,LEFT)"},
    {false, GPIO_NUM_6, GPIO_NUM_4, GPIO_NUM_5,  true,  "I2S(WS=6,SCK=4,DIN=5,RIGHT)"},
    {false, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_4,  false, "I2S(WS=5,SCK=6,DIN=4,LEFT)"},
    {false, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_4,  true,  "I2S(WS=5,SCK=6,DIN=4,RIGHT)"},
    {false, GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_4,  false, "I2S(WS=6,SCK=5,DIN=4,LEFT)"},
    {false, GPIO_NUM_6, GPIO_NUM_5, GPIO_NUM_4,  true,  "I2S(WS=6,SCK=5,DIN=4,RIGHT)"},
};

constexpr int kNumCandidates = sizeof(kCandidates) / sizeof(kCandidates[0]);

void AnalyzeWindow16k(const int16_t* buf, int n, float& out_rms, int& out_peak, float& out_g1k) {
    if (n <= 0) {
        out_rms = 0.0f;
        out_peak = 0;
        out_g1k = 0.0f;
        return;
    }
    int64_t sum = 0;
    for (int i = 0; i < n; ++i) sum += buf[i];
    const float mean = static_cast<float>(sum) / static_cast<float>(n);

    // Goertzel at 1000 Hz for fs = 16000 Hz (omega = 2*pi*1000/16000 = pi/8)
    const float coeff = 2.0f * std::cos(3.14159265358979323846f / 8.0f);
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    double sq_sum = 0.0;
    int peak = 0;

    for (int i = 0; i < n; ++i) {
        float x = static_cast<float>(buf[i]) - mean;
        int ax = static_cast<int>(std::fabs(x));
        if (ax > peak) peak = ax;
        sq_sum += static_cast<double>(x) * static_cast<double>(x);

        // Apply Hann window to Goertzel to reject DC step / broadband edge leakage
        float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * i / (n - 1)));
        s0 = (x * w) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    out_rms = static_cast<float>(std::sqrt(sq_sum / n));
    out_peak = peak;
    float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    out_g1k = (power > 0.0f) ? (std::sqrt(power) / (n * 0.25f)) : 0.0f;
}

}  // namespace

// ======================== XhS3eVoiceDiag ========================

XhS3eVoiceDiag& XhS3eVoiceDiag::GetInstance() {
    static XhS3eVoiceDiag inst;
    return inst;
}

void XhS3eVoiceDiag::SetDeviceState(const char* state) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.device_state = state ? state : "";
}

void XhS3eVoiceDiag::SetMicInfo(const std::string& mode, float gain, float snr) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.mic_mode = mode;
    snap_.mic_gain = gain;
    snap_.mic_best_snr = snr;
}

void XhS3eVoiceDiag::SetMicScanReportJson(const std::string& json) {
    std::lock_guard<std::mutex> lock(mtx_);
    scan_report_json_ = json;
}

std::string XhS3eVoiceDiag::GetMicScanReportJson() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return scan_report_json_;
}

void XhS3eVoiceDiag::RecordMicLevel(int peak, int rms) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.mic_raw_peak = peak;
    snap_.mic_raw_rms = rms;
}

void XhS3eVoiceDiag::RecordWakeDetected(const std::string& phrase, float prob) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.wake_count++;
    snap_.last_wake_phrase = phrase;
    snap_.last_wake_prob = prob;
}

void XhS3eVoiceDiag::RecordWakeTimeout(const std::string& phrase, float prob) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.last_timeout_phrase = phrase;
    snap_.last_timeout_prob = prob;
}

void XhS3eVoiceDiag::RecordStt(const std::string& text) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.last_stt = text;
}

void XhS3eVoiceDiag::RecordTts(const std::string& text) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.last_tts = text;
}

void XhS3eVoiceDiag::RecordMcpTool(const std::string& tool) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.last_mcp_tool = tool;
}

void XhS3eVoiceDiag::RecordUartTx(int k_num, uint8_t uart_byte) {
    std::lock_guard<std::mutex> lock(mtx_);
    snap_.last_uart_k = k_num;
    snap_.last_uart_byte = static_cast<int>(uart_byte);
}

XhS3eVoiceDiagSnapshot XhS3eVoiceDiag::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return snap_;
}

// ======================== XhS3eSmartAudioCodec ========================

XhS3eSmartAudioCodec::XhS3eSmartAudioCodec(int input_sample_rate, int output_sample_rate,
                                           gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout)
    : spk_bclk_(spk_bclk), spk_ws_(spk_ws), spk_dout_(spk_dout) {
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    InitSpeakerTx();

    // Hardware verified: mode [12] = I2S(WS=4, SCK=5, DIN=6, LEFT) with 1.0x gain (>>12 = +24dB)
    Settings settings("robonova", false);
    int saved_mode = settings.GetInt("mic_mode", 12);
    int saved_gain_x100 = settings.GetInt("mic_gain_x100", 100);
    int saved_snr_x10 = settings.GetInt("mic_snr_x10", 100);

    if (saved_mode >= 0 && saved_mode < kNumCandidates) {
        selected_mode_idx_ = saved_mode;
        calibrated_gain_ = std::clamp(saved_gain_x100 / 100.0f, 0.5f, 20.0f);
        if (OpenMicMode(selected_mode_idx_, &rx_handle_)) {
            is_pdm_ = kCandidates[selected_mode_idx_].is_pdm;
            ESP_LOGI(TAG, "Initialized verified mic mode [%d] %s (gain=%.2fx)",
                     selected_mode_idx_, kCandidates[selected_mode_idx_].label,
                     calibrated_gain_);
            XhS3eVoiceDiag::GetInstance().SetMicInfo(
                kCandidates[selected_mode_idx_].label, calibrated_gain_, saved_snr_x10 / 10.0f);
            return;
        }
    }

    RescanMicrophone(true);
}

XhS3eSmartAudioCodec::~XhS3eSmartAudioCodec() {
    CloseMicChannel(rx_handle_);
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
    }
}

void XhS3eSmartAudioCodec::InitSpeakerTx() {
    // Speaker TX on I2S_NUM_1 so I2S_NUM_0 is 100% dedicated to Mic RX (both PDM & STD supported on I2S0!)
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(static_cast<i2s_port_t>(1), I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    tx_chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    tx_chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = static_cast<uint32_t>(output_sample_rate_),
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk_,
            .ws = spk_ws_,
            .dout = spk_dout_,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &tx_std_cfg));
}

bool XhS3eSmartAudioCodec::OpenMicMode(int mode_idx, i2s_chan_handle_t* out_rx) {
    if (mode_idx < 0 || mode_idx >= kNumCandidates || out_rx == nullptr) {
        return false;
    }
    *out_rx = nullptr;
    const auto& c = kCandidates[mode_idx];

    gpio_reset_pin(GPIO_NUM_4);
    gpio_reset_pin(GPIO_NUM_5);
    gpio_reset_pin(GPIO_NUM_6);

    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(static_cast<i2s_port_t>(0), I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM;
    rx_chan_cfg.dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM;
    if (i2s_new_channel(&rx_chan_cfg, nullptr, out_rx) != ESP_OK) {
        return false;
    }

    esp_err_t err = ESP_FAIL;
    if (c.is_pdm) {
#if SOC_I2S_SUPPORTS_PDM_RX
        i2s_pdm_rx_config_t pdm_cfg = {
            .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(input_sample_rate_)),
            .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .clk = c.pin_a,
                .din = c.pin_b,
                .invert_flags = { .clk_inv = false },
            },
        };
        pdm_cfg.slot_cfg.slot_mask = c.right_slot ? I2S_PDM_SLOT_RIGHT : I2S_PDM_SLOT_LEFT;
        err = i2s_channel_init_pdm_rx_mode(*out_rx, &pdm_cfg);
#endif
    } else {
        i2s_std_config_t std_cfg = {
            .clk_cfg = {
                .sample_rate_hz = static_cast<uint32_t>(input_sample_rate_),
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
                .ext_clk_freq_hz = 0,
#endif
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                .slot_mode = I2S_SLOT_MODE_MONO,
                .slot_mask = c.right_slot ? I2S_STD_SLOT_RIGHT : I2S_STD_SLOT_LEFT,
                .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
                .ws_pol = false,
                .bit_shift = true,
#ifdef I2S_HW_VERSION_2
                .left_align = true,
                .big_endian = false,
                .bit_order_lsb = false,
#endif
            },
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = c.pin_b,
                .ws = c.pin_a,
                .dout = I2S_GPIO_UNUSED,
                .din = c.pin_c,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
        err = i2s_channel_init_std_mode(*out_rx, &std_cfg);
    }

    if (err != ESP_OK) {
        i2s_del_channel(*out_rx);
        *out_rx = nullptr;
        return false;
    }
    return true;
}

void XhS3eSmartAudioCodec::CloseMicChannel(i2s_chan_handle_t& rx) {
    if (rx != nullptr) {
        i2s_channel_disable(rx);
        i2s_del_channel(rx);
        rx = nullptr;
    }
    gpio_reset_pin(GPIO_NUM_4);
    gpio_reset_pin(GPIO_NUM_5);
    gpio_reset_pin(GPIO_NUM_6);
}

int XhS3eSmartAudioCodec::ReadModeSamples16(int mode_idx, i2s_chan_handle_t rx, int16_t* dst, int samples, uint32_t timeout_ms) {
    if (rx == nullptr || samples <= 0) return 0;
    size_t bytes_read = 0;
    if (kCandidates[mode_idx].is_pdm) {
        if (i2s_channel_read(rx, dst, samples * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
            return 0;
        }
        return static_cast<int>(bytes_read / sizeof(int16_t));
    } else {
        std::vector<int32_t> buf32(samples);
        if (i2s_channel_read(rx, buf32.data(), samples * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
            return 0;
        }
        int got = static_cast<int>(bytes_read / sizeof(int32_t));
        for (int i = 0; i < got; ++i) {
            int32_t v = buf32[i] >> 12;
            dst[i] = (v > INT16_MAX) ? INT16_MAX : (v < -INT16_MAX) ? -INT16_MAX : static_cast<int16_t>(v);
        }
        return got;
    }
}

void XhS3eSmartAudioCodec::WriteSpeakerDirectLocked(const int16_t* data24k, int samples, int vol_pct) {
    if (tx_handle_ == nullptr || samples <= 0) return;
    std::vector<int32_t> buf(samples);
    int32_t vol_factor = static_cast<int32_t>(std::pow(static_cast<double>(vol_pct) / 100.0, 2.0) * 65536.0);
    for (int i = 0; i < samples; ++i) {
        int64_t t = static_cast<int64_t>(data24k[i]) * vol_factor;
        buf[i] = (t > INT32_MAX) ? INT32_MAX : (t < INT32_MIN) ? INT32_MIN : static_cast<int32_t>(t);
    }
    size_t written = 0;
    i2s_channel_write(tx_handle_, buf.data(), samples * sizeof(int32_t), &written, pdMS_TO_TICKS(200));
}

void XhS3eSmartAudioCodec::RescanMicrophone(bool save_to_nvs) {
    std::lock_guard<std::mutex> rx_lk(rx_mutex_);
    std::lock_guard<std::mutex> tx_lk(tx_mutex_);

    const bool was_rx_enabled = input_enabled_;
    const bool was_tx_enabled = output_enabled_;
    if (rx_handle_ != nullptr) {
        CloseMicChannel(rx_handle_);
    }
    if (!was_tx_enabled && tx_handle_ != nullptr) {
        i2s_channel_enable(tx_handle_);
    }

    ESP_LOGI(TAG, "=== STARTING 24-MODE ACOUSTIC LOOPBACK MIC AUTO-SCAN ===");

    // Pre-build 240-sample (10ms @ 24kHz) 1000Hz sine frame & silence frame
    constexpr int kTxChunk = 240;  // 10ms @ 24000 Hz
    constexpr int kRxChunk = 160;  // 10ms @ 16000 Hz
    int16_t sine1k[kTxChunk];
    int16_t zeros[kTxChunk] = {0};
    for (int i = 0; i < kTxChunk; ++i) {
        // 1000 Hz completes exactly 10 cycles in 240 samples @ 24000 Hz -> phase-continuous across frames!
        sine1k[i] = static_cast<int16_t>(16000.0f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * i / 24000.0f));
    }

    int best_idx = 12;  // fallback I2S(WS=4,SCK=5,DIN=6,LEFT)
    float best_snr = 0.0f;
    int best_tone_peak = 2000;

    cJSON* report_arr = cJSON_CreateArray();

    for (int idx = 0; idx < kNumCandidates; ++idx) {
        i2s_chan_handle_t probe_rx = nullptr;
        if (!OpenMicMode(idx, &probe_rx)) {
            continue;
        }
        if (i2s_channel_enable(probe_rx) != ESP_OK) {
            CloseMicChannel(probe_rx);
            continue;
        }

        // 1. 40ms warmup (discard 4 x 160 samples while speaker is silent)
        int16_t tmp[kRxChunk];
        for (int f = 0; f < 4; ++f) {
            WriteSpeakerDirectLocked(zeros, kTxChunk, 0);
            ReadModeSamples16(idx, probe_rx, tmp, kRxChunk, 40);
        }

        // 2. 40ms silence window (640 samples)
        int16_t sil_buf[kRxChunk * 4] = {0};
        int sil_got = 0;
        for (int f = 0; f < 4; ++f) {
            WriteSpeakerDirectLocked(zeros, kTxChunk, 0);
            sil_got += ReadModeSamples16(idx, probe_rx, sil_buf + sil_got, kRxChunk, 40);
        }

        // 3. 20ms tone pre-roll + 40ms 1000Hz tone measurement (640 samples)
        for (int f = 0; f < 2; ++f) {
            WriteSpeakerDirectLocked(sine1k, kTxChunk, 85);
            ReadModeSamples16(idx, probe_rx, tmp, kRxChunk, 40);
        }
        int16_t tone_buf[kRxChunk * 4] = {0};
        int tone_got = 0;
        for (int f = 0; f < 4; ++f) {
            WriteSpeakerDirectLocked(sine1k, kTxChunk, 85);
            tone_got += ReadModeSamples16(idx, probe_rx, tone_buf + tone_got, kRxChunk, 40);
        }
        // Flush silence
        WriteSpeakerDirectLocked(zeros, kTxChunk, 0);
        WriteSpeakerDirectLocked(zeros, kTxChunk, 0);

        CloseMicChannel(probe_rx);

        float sil_rms = 0, sil_g1k = 0;
        int sil_peak = 0;
        AnalyzeWindow16k(sil_buf, sil_got, sil_rms, sil_peak, sil_g1k);

        float tone_rms = 0, tone_g1k = 0;
        int tone_peak = 0;
        AnalyzeWindow16k(tone_buf, tone_got, tone_rms, tone_peak, tone_g1k);

        float snr = (tone_g1k + 1.0f) / (sil_g1k + 12.0f);
        ESP_LOGI(TAG, "[%02d] %-27s | sil_rms=%6.1f g1k=%6.1f | tone_rms=%6.1f g1k=%7.1f peak=%5d | SNR=%.2fx",
                 idx, kCandidates[idx].label, sil_rms, sil_g1k, tone_rms, tone_g1k, tone_peak, snr);

        cJSON* row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "idx", idx);
        cJSON_AddStringToObject(row, "mode", kCandidates[idx].label);
        cJSON_AddNumberToObject(row, "sil_rms", std::round(sil_rms * 10) / 10.0);
        cJSON_AddNumberToObject(row, "tone_rms", std::round(tone_rms * 10) / 10.0);
        cJSON_AddNumberToObject(row, "sil_1k", std::round(sil_g1k * 10) / 10.0);
        cJSON_AddNumberToObject(row, "tone_1k", std::round(tone_g1k * 10) / 10.0);
        cJSON_AddNumberToObject(row, "tone_peak", tone_peak);
        cJSON_AddNumberToObject(row, "snr", std::round(snr * 100) / 100.0);
        cJSON_AddItemToArray(report_arr, row);

        if (snr > best_snr && tone_peak > 40) {
            best_snr = snr;
            best_idx = idx;
            best_tone_peak = tone_peak;
        }
    }

    char* rep_str = cJSON_PrintUnformatted(report_arr);
    if (rep_str) {
        XhS3eVoiceDiag::GetInstance().SetMicScanReportJson(rep_str);
        cJSON_free(rep_str);
    }
    cJSON_Delete(report_arr);

    selected_mode_idx_ = best_idx;
    is_pdm_ = kCandidates[selected_mode_idx_].is_pdm;
    dc_init_ = false;
    dc_offset_ = 0;

    // Auto-calibrate digital gain so near-field tone peaks around ~8500 (leaves +11dB headroom, zero clipping!)
    if (best_snr >= 3.0f && best_tone_peak > 0) {
        calibrated_gain_ = std::clamp(8500.0f / static_cast<float>(best_tone_peak), 0.5f, 16.0f);
    } else {
        calibrated_gain_ = is_pdm_ ? 3.0f : 1.0f;
    }

    ESP_LOGI(TAG, ">>> SELECTED MIC [%d]: %s | SNR=%.2fx | auto_gain=%.2fx <<<",
             selected_mode_idx_, kCandidates[selected_mode_idx_].label, best_snr, calibrated_gain_);
    XhS3eVoiceDiag::GetInstance().SetMicInfo(
        kCandidates[selected_mode_idx_].label, calibrated_gain_, best_snr);

    if (save_to_nvs && best_snr >= 3.5f) {
        Settings settings("robonova", true);
        settings.SetInt("mic_mode", selected_mode_idx_);
        settings.SetInt("mic_gain_x100", static_cast<int>(calibrated_gain_ * 100.0f));
        settings.SetInt("mic_snr_x10", static_cast<int>(best_snr * 10.0f));
    }

    OpenMicMode(selected_mode_idx_, &rx_handle_);
    if (was_rx_enabled && rx_handle_ != nullptr) {
        i2s_channel_enable(rx_handle_);
    }
    if (!was_tx_enabled && tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
    }
}

void XhS3eSmartAudioCodec::PlayRawPcm16kDirect(const int16_t* pcm16k, size_t samples, int volume_override) {
    if (pcm16k == nullptr || samples < 2 || tx_handle_ == nullptr) {
        return;
    }
    if (!output_enabled_) {
        EnableOutput(true);
    }
    std::lock_guard<std::mutex> tx_lk(tx_mutex_);

    // Exact 3:2 polyphase upsampling from 16000 Hz -> 24000 Hz in 10ms chunks (160 -> 240 samples)
    int16_t out24k[240];
    size_t pos = 0;
    while (pos + 2 <= samples) {
        size_t chunk_in = std::min<size_t>(160, (samples - pos) & ~static_cast<size_t>(1));
        if (chunk_in < 2) break;
        size_t out_idx = 0;
        for (size_t i = 0; i < chunk_in; i += 2) {
            int32_t x0 = pcm16k[pos + i];
            int32_t x1 = pcm16k[pos + i + 1];
            int32_t x2 = (pos + i + 2 < samples) ? pcm16k[pos + i + 2] : x1;
            out24k[out_idx++] = static_cast<int16_t>(x0);
            out24k[out_idx++] = static_cast<int16_t>((x0 + 2 * x1) / 3);
            out24k[out_idx++] = static_cast<int16_t>((2 * x1 + x2) / 3);
        }
        WriteSpeakerDirectLocked(out24k, static_cast<int>(out_idx), volume_override);
        pos += chunk_in;
    }

    int16_t silence[240] = {0};
    WriteSpeakerDirectLocked(silence, 240, 0);
}

int XhS3eSmartAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> tx_lk(tx_mutex_);
    WriteSpeakerDirectLocked(data, samples, output_volume_);
    return samples;
}

int XhS3eSmartAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> rx_lk(rx_mutex_);
    if (rx_handle_ == nullptr || samples <= 0) {
        return 0;
    }

    int got = ReadModeSamples16(selected_mode_idx_, rx_handle_, dest, samples, 1000);
    const int32_t gain_q8 = static_cast<int32_t>(calibrated_gain_ * 256.0f);
    for (int i = 0; i < got; ++i) {
        int32_t raw = dest[i];
        if (!dc_init_) {
            dc_offset_ = raw;
            dc_init_ = true;
        } else {
            dc_offset_ += (raw - dc_offset_) >> 8;
        }
        int32_t v = ((raw - dc_offset_) * gain_q8) >> 8;
        dest[i] = (v > INT16_MAX) ? INT16_MAX : (v < -INT16_MAX) ? -INT16_MAX : static_cast<int16_t>(v);
    }
    return got;
}
