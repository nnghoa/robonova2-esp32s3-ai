#pragma once

#include <esp_err.h>
#include <esp_http_server.h>

#include <mutex>
#include <set>
#include <string>

#include "config.h"

class RobonovaWebServer {
public:
    static RobonovaWebServer& GetInstance();

    RobonovaWebServer(const RobonovaWebServer&) = delete;
    RobonovaWebServer& operator=(const RobonovaWebServer&) = delete;

    bool Start(int port = ROBONOVA_WEB_PORT);
    void Stop();
    bool IsRunning() const { return server_ != nullptr; }
    void BroadcastStatus(const std::string& status_json);

private:
    RobonovaWebServer() = default;
    ~RobonovaWebServer();

    httpd_handle_t server_ = nullptr;
    std::mutex clients_mutex_;
    std::set<int> ws_clients_;

    static esp_err_t HandleIndexGet(httpd_req_t* req);
    static esp_err_t HandleApiActionsGet(httpd_req_t* req);
    static esp_err_t HandleApiStatusGet(httpd_req_t* req);
    static esp_err_t HandleApiCommandPost(httpd_req_t* req);
    static esp_err_t HandleWs(httpd_req_t* req);

    std::string ProcessJsonCommand(const char* json_str, size_t len);
};
