#pragma once

#include "audio_codec.h"
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>

struct XhS3eVoiceDiagSnapshot {
    std::string device_state = "starting";
    std::string mic_mode = "uninitialized";
    float mic_gain = 1.0f;
    float mic_best_snr = 0.0f;
    int mic_raw_peak = 0;
    int mic_raw_rms = 0;
    int wake_count = 0;
    std::string last_wake_phrase;
    float last_wake_prob = 0.0f;
    std::string last_timeout_phrase;
    float last_timeout_prob = 0.0f;
    std::string last_stt;
    std::string last_tts;
    std::string last_mcp_tool;
    int last_uart_k = 0;
    int last_uart_byte = 0;
};

class XhS3eVoiceDiag {
public:
    static XhS3eVoiceDiag& GetInstance();

    void SetDeviceState(const char* state);
    void SetMicInfo(const std::string& mode, float gain, float snr);
    void SetMicScanReportJson(const std::string& json);
    std::string GetMicScanReportJson() const;

    void RecordMicLevel(int peak, int rms);
    void RecordWakeDetected(const std::string& phrase, float prob);
    void RecordWakeTimeout(const std::string& phrase, float prob);
    void RecordStt(const std::string& text);
    void RecordTts(const std::string& text);
    void RecordMcpTool(const std::string& tool);
    void RecordUartTx(int k_num, uint8_t uart_byte);

    XhS3eVoiceDiagSnapshot GetSnapshot() const;

private:
    mutable std::mutex mtx_;
    XhS3eVoiceDiagSnapshot snap_;
    std::string scan_report_json_ = "[]";
};

class XhS3eSmartAudioCodec : public AudioCodec {
public:
    XhS3eSmartAudioCodec(int input_sample_rate, int output_sample_rate,
                         gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout);
    virtual ~XhS3eSmartAudioCodec();

    // Re-runs the 24-mode acoustic loopback scan (onboard speaker -> onboard mic)
    void RescanMicrophone(bool save_to_nvs = true);

    // Plays raw 16kHz signed 16-bit mono PCM directly out the speaker without changing Application state
    void PlayRawPcm16kDirect(const int16_t* pcm16k, size_t samples, int volume_override = 85);

protected:
    virtual int Write(const int16_t* data, int samples) override;
    virtual int Read(int16_t* dest, int samples) override;

private:
    gpio_num_t spk_bclk_;
    gpio_num_t spk_ws_;
    gpio_num_t spk_dout_;

    std::mutex tx_mutex_;
    std::mutex rx_mutex_;

    int selected_mode_idx_ = -1;
    bool is_pdm_ = false;
    float calibrated_gain_ = 1.0f;
    int32_t dc_offset_ = 0;
    bool dc_init_ = false;

    void InitSpeakerTx();
    bool OpenMicMode(int mode_idx, i2s_chan_handle_t* out_rx);
    void CloseMicChannel(i2s_chan_handle_t& rx);
    int ReadModeSamples16(int mode_idx, i2s_chan_handle_t rx, int16_t* dst, int samples, uint32_t timeout_ms);
    void WriteSpeakerDirectLocked(const int16_t* data24k, int samples, int vol_pct);
};
