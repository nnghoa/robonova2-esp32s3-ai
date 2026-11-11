#include "robonova_web_server.h"

#include <cJSON.h>
#include <esp_log.h>
#include <sys/socket.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "application.h"
#include "audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "display.h"
#include "esp32_music.h"
#include "esp32_radio.h"
#include "mcp_server.h"
#include "robonova_controller.h"
#include "settings.h"

#define TAG "RobonovaWeb"

namespace {

const char kDashboardHtml[] = R"ROBOHTML(<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>DB-Robonova2 ESP32-S3 AI Control (:8080)</title>
<style>
:root{--bg:#0b1020;--card:#151d36;--card2:#1c2747;--pri:#00d4ff;--acc:#ff4d6d;--ok:#00e676;--warn:#ffb300;--txt:#eef2ff;--mut:#94a3b8}
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--txt);padding:10px;max-width:980px;margin:0 auto}
.hdr{background:linear-gradient(135deg,#132046,#1d2f64);border:1px solid #2b3f78;border-radius:14px;padding:12px 14px;margin-bottom:10px;display:flex;flex-wrap:wrap;justify-content:space-between;align-items:center;gap:8px}
.title{font-size:1.1rem;font-weight:800;color:var(--pri);letter-spacing:.3px}
.sub{font-size:.78rem;color:var(--mut)}
.pills{display:flex;flex-wrap:wrap;gap:6px}
.pill{background:#0f172a;border:1px solid #293860;border-radius:999px;padding:4px 10px;font-size:.76rem;font-weight:600}
.pill.ok{border-color:var(--ok);color:var(--ok)}
.pill.warn{border-color:var(--warn);color:var(--warn)}
.pill.danger{border-color:var(--acc);color:var(--acc)}
.qbar{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-bottom:10px}
button{cursor:pointer;border:1px solid #2c3d6e;background:var(--card2);color:var(--txt);border-radius:10px;padding:10px 8px;font-size:.86rem;font-weight:600;transition:transform .08s,background .15s}
button:active{transform:scale(.96);background:#283b70}
.btn-stop{background:#c9184a;border-color:#ff4d6d;color:#fff;font-weight:800}
.btn-pri{background:#0284c7;border-color:#38bdf8;color:#fff}
.btn-ok{background:#059669;border-color:#34d399;color:#fff}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px}
@media(max-width:700px){.grid2{grid-template-columns:1fr}}
.card{background:var(--card);border:1px solid #233159;border-radius:14px;padding:12px}
.card h3{font-size:.92rem;color:var(--pri);margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}
.dpad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;max-width:290px;margin:0 auto}
.dpad button{padding:14px 6px;font-size:.9rem}
.tabs{display:flex;gap:6px;overflow-x:auto;padding-bottom:8px;margin-bottom:8px}
.tab{white-space:nowrap;padding:6px 12px;border-radius:999px;font-size:.8rem;background:#0f172a}
.tab.active{background:var(--pri);color:#061224;border-color:var(--pri);font-weight:800}
.act-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(145px,1fr));gap:7px;max-height:370px;overflow-y:auto;padding-right:2px}
.act-btn{text-align:left;padding:8px 10px;display:flex;flex-direction:column;gap:2px}
.act-btn .k{font-size:.7rem;color:var(--pri);font-weight:800}
.act-btn .n{font-size:.82rem;line-height:1.2}
.row{display:flex;gap:8px;align-items:center;margin-bottom:8px;flex-wrap:wrap}
input[type=text]{flex:1;min-width:150px;background:#0f172a;border:1px solid #2c3d6e;color:var(--txt);border-radius:8px;padding:8px 10px;font-size:.86rem}
input[type=range]{flex:1}
label.sw{display:flex;align-items:center;gap:7px;font-size:.83rem;cursor:pointer;background:#0f172a;padding:6px 10px;border-radius:8px;border:1px solid #28365e}
.foot{text-align:center;font-size:.75rem;color:var(--mut);margin-top:12px;padding:8px}
a{color:var(--pri);text-decoration:none}
</style>
</head>
<body>
<div class="hdr">
  <div>
    <div class="title">🤖 DB-ROBONOVA2 ESP32-S3 AI</div>
    <div class="sub" id="actTxt">Đang kết nối WebSocket...</div>
  </div>
  <div class="pills">
    <span class="pill" id="wsPill">WS: ...</span>
    <span class="pill" id="eyePill">Mắt Siêu Âm: --</span>
    <span class="pill" id="footPill">Chân 10-NK: --</span>
    <span class="pill" id="batPill">Pin: --</span>
  </div>
</div>

<div class="qbar">
  <button class="btn-stop" onclick="sendCmd({cmd:'stop'})">⏹ DỪNG KHẨN CẤP</button>
  <button class="btn-pri" onclick="act(1)">🙇 K1 Cúi chào</button>
  <button class="btn-pri" onclick="act(48)">🥋 K48 Kungfu 1</button>
  <button class="btn-ok" onclick="act(64)">💃 K64 Nhảy Dance</button>
  <button onclick="act(40)">💪 K40 Chống đẩy</button>
  <button onclick="act(16)">🧘 K16 Ngồi/Tắt Servo</button>
</div>

<div class="grid2">
  <div class="card">
    <h3>🎮 Điều hướng & Chiến đấu nhanh</h3>
    <div class="dpad">
      <button onclick="act(17)">↖ Chéo T (K17)</button>
      <button class="btn-pri" onclick="act(11)">⬆ Tiến (K11)</button>
      <button onclick="act(27)">↗ Chéo P (K27)</button>
      <button onclick="act(22)">↺ Quay T (K22)</button>
      <button class="btn-ok" onclick="act(50)">⚡ Chạy (K50)</button>
      <button onclick="act(24)">↻ Quay P (K24)</button>
      <button onclick="act(14)">⬅ Ngang T (K14)</button>
      <button onclick="act(12)">⬇ Lùi (K12)</button>
      <button onclick="act(13)">➡ Ngang P (K13)</button>
    </div>
    <div class="row" style="margin-top:10px;justify-content:center">
      <button onclick="act(29)">🥊 Đấm thẳng (K29)</button>
      <button onclick="act(8)">🦵 Đá đôi (K8)</button>
      <button onclick="act(53)">🕶 Né Matrix (K53)</button>
      <button onclick="act(66)">🪜 Leo bậc (K66)</button>
      <button onclick="act(25)">🔄 Đứng dậy sấp (K25)</button>
      <button onclick="act(19)">🔄 Đứng dậy ngửa (K19)</button>
    </div>
  </div>

  <div class="card">
    <h3>🎵 Giải trí Đa phương tiện & Cảm biến AI</h3>
    <div class="row">
      <input type="text" id="songInp" placeholder="Nhập tên bài hát (VD: Lạc Trôi, Bắc Bling...)">
      <button class="btn-pri" onclick="playSong(false)">▶ Phát nhạc</button>
      <button class="btn-ok" onclick="playSong(true)">💃 Hát & Nhảy</button>
    </div>
    <div class="row">
      <button onclick="playRadio('VOV1')">📻 VOV1</button>
      <button onclick="playRadio('VOV2')">📻 VOV2</button>
      <button onclick="playRadio('VOV3')">📻 VOV3</button>
      <button onclick="playRadio('VOVGT_HN')">🚗 VOV GT</button>
      <button onclick="sendCmd({cmd:'stop_media'})">⏹ Tắt nhạc/đài</button>
    </div>
    <div class="row">
      <span style="font-size:.82rem;width:78px">🔊 Âm lượng</span>
      <input type="range" id="volRng" min="0" max="100" value="80" onchange="sendCmd({cmd:'volume',val:+this.value})">
      <span id="volVal" style="font-size:.8rem;width:34px">80%</span>
    </div>
    <div class="row">
      <span style="font-size:.82rem;width:78px">🔆 Đèn nền</span>
      <input type="range" id="briRng" min="0" max="100" value="90" onchange="sendCmd({cmd:'brightness',val:+this.value})">
      <span id="briVal" style="font-size:.8rem;width:34px">90%</span>
    </div>
    <div class="row">
      <label class="sw"><input type="checkbox" id="chkDance" onchange="sendCmd({cmd:'dance_sync',enable:this.checked})"> Nhảy theo nhạc</label>
      <label class="sw"><input type="checkbox" id="chkAuto" onchange="sendCmd({cmd:'auto_sensor',enable:this.checked})"> Tự tránh vật cản (Mắt + 10-NK)</label>
      <label class="sw"><input type="checkbox" id="chkStair" onchange="sendCmd({cmd:'auto_stairs',enable:this.checked})"> Tự leo bậc (10-NK)</label>
    </div>
  </div>
</div>

<div class="card">
  <h3><span>📋 Toàn bộ 63 Động tác gốc Nhà sản xuất (K1 – K68)</span><span id="cntLbl" style="font-size:.78rem;color:var(--mut)"></span></h3>
  <div class="tabs" id="tabs">
    <button class="tab active" onclick="setCat('all',this)">Tất cả (K1–K68)</button>
    <button class="tab" onclick="setCat('move',this)">🚶 Di chuyển</button>
    <button class="tab" onclick="setCat('martial',this)">🥋 Võ thuật - Kungfu</button>
    <button class="tab" onclick="setCat('dance',this)">💃 Nhảy múa</button>
    <button class="tab" onclick="setCat('special',this)">🤸 Nhào lộn & Thế khó</button>
    <button class="tab" onclick="setCat('basic',this)">👋 Cơ bản</button>
  </div>
  <div class="act-grid" id="actGrid"></div>
  <div class="row" style="margin-top:10px">
    <input type="text" id="comboInp" value="K41,K48,K29,K8,K53,K1" placeholder="Chuỗi liên hoàn VD: K41,K48,K53,K1">
    <button class="btn-pri" onclick="runCombo()">⚡ Chạy chuỗi Combo</button>
  </div>
</div>

<div class="foot">
  DB-Robonova2 ESP32-S3 Xiaozhi AI • UART 9600 (TX=GPIO42, RX=GPIO41) • ST7789 240x240 •
  <a href="#" onclick="sendCmd({cmd:'toggle_spi_mode'});return false;">Đổi SPI Mode ST7789 (0/3)</a> •
  <a href="#" onclick="location.href='http://'+location.hostname+'/ota';return false;">Cập nhật Firmware OTA (Cổng 80)</a>
</div>

<script>
let actions=[], curCat='all', ws=null;
function connectWs(){
  const proto=location.protocol==='https:'?'wss:':'ws:';
  ws=new WebSocket(proto+'//'+location.host+'/ws');
  ws.onopen=()=>{const p=document.getElementById('wsPill');p.textContent='WS: Đã kết nối';p.className='pill ok';sendCmd({cmd:'status'});};
  ws.onclose=()=>{const p=document.getElementById('wsPill');p.textContent='WS: Mất kết nối';p.className='pill danger';setTimeout(connectWs,2000);};
  ws.onmessage=(ev)=>{try{const d=JSON.parse(ev.data);if(d.robot)updateUi(d);}catch(e){}};
}
function sendCmd(obj){
  if(ws&&ws.readyState===1){ws.send(JSON.stringify(obj));}
  else{fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(obj)}).then(r=>r.json()).then(d=>{if(d.robot)updateUi(d);}).catch(()=>{});}
}
function act(k){sendCmd({cmd:'action',k:k});}
function runCombo(){const s=document.getElementById('comboInp').value.trim();if(s)sendCmd({cmd:'combo',sequence:s});}
function playSong(dance){const s=document.getElementById('songInp').value.trim();sendCmd({cmd:'music',song:s||'Nhạc trẻ sôi động',dance:dance});}
function playRadio(st){sendCmd({cmd:'radio',station:st});}
function updateUi(d){
  document.getElementById('actTxt').textContent='Trạng thái: '+(d.current_action||'Đứng nghỉ')+(d.queue_size?' (Hàng đợi: '+d.queue_size+')':'');
  document.getElementById('chkDance').checked=!!d.dance_with_music;
  document.getElementById('chkAuto').checked=!!d.auto_sensor_mode;
  document.getElementById('chkStair').checked=!!d.auto_climb_stairs;
  if(typeof d.volume==='number'){document.getElementById('volRng').value=d.volume;document.getElementById('volVal').textContent=d.volume+'%';}
  if(typeof d.brightness==='number'){document.getElementById('briRng').value=d.brightness;document.getElementById('briVal').textContent=d.brightness+'%';}
  const s=d.sensors||{};
  const eye=document.getElementById('eyePill'), foot=document.getElementById('footPill'), bat=document.getElementById('batPill');
  if(s.telemetry_online){
    eye.textContent='Mắt Siêu Âm: '+(s.ultrasonic_cm>=0?(s.ultrasonic_cm+' cm'):'Thoáng');
    eye.className='pill '+((s.ultrasonic_cm>0&&s.ultrasonic_cm<25)?'warn':'ok');
    foot.textContent='Chân 10-NK: '+(s.foot_10nk_obstacle?'CÓ BẬC/VẬT CẢN!':'Thoáng');
    foot.className='pill '+(s.foot_10nk_obstacle?'danger':'ok');
    bat.textContent='Pin: '+Number(s.battery_voltage||0).toFixed(2)+'V ('+(s.battery_percent||0)+'%)';
    bat.className='pill '+(s.low_battery?'danger':'ok');
  }else{
    eye.textContent='Mắt Siêu Âm: Chế độ Zin';eye.className='pill';
    foot.textContent='Chân 10-NK: Chế độ Zin';foot.className='pill';
    bat.textContent='UART TX->ERX Sẵn sàng';bat.className='pill ok';
  }
}
function setCat(cat,btn){
  curCat=cat;
  document.querySelectorAll('.tab').forEach(b=>b.classList.remove('active'));
  btn.classList.add('active');
  renderActs();
}
function renderActs(){
  const g=document.getElementById('actGrid');
  const list=curCat==='all'?actions:actions.filter(x=>x.cat===curCat);
  document.getElementById('cntLbl').textContent=list.length+' động tác';
  g.innerHTML=list.map(a=>`<button class="act-btn" onclick="act(${a.k})"><span class="k">K${a.k} • ASCII ${a.dec}</span><span class="n">${a.name}</span></button>`).join('');
}
fetch('/api/actions').then(r=>r.json()).then(arr=>{actions=arr;renderActs();});
connectWs();
</script>
</body>
</html>
)ROBOHTML";

}  // namespace

RobonovaWebServer& RobonovaWebServer::GetInstance() {
    static RobonovaWebServer instance;
    return instance;
}

RobonovaWebServer::~RobonovaWebServer() {
    Stop();
}

bool RobonovaWebServer::Start(int port) {
    if (server_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = static_cast<uint16_t>(port);
    config.ctrl_port = ROBONOVA_WEB_CTRL_PORT;  // Tách biệt khỏi OtaServer (32768)
    config.max_open_sockets = 7;
    config.max_uri_handlers = 10;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Web Control Server on port %d: %s", port, esp_err_to_name(err));
        server_ = nullptr;
        return false;
    }

    httpd_uri_t index_uri = {};
    index_uri.uri = "/";
    index_uri.method = HTTP_GET;
    index_uri.handler = HandleIndexGet;
    httpd_register_uri_handler(server_, &index_uri);

    httpd_uri_t actions_uri = {};
    actions_uri.uri = "/api/actions";
    actions_uri.method = HTTP_GET;
    actions_uri.handler = HandleApiActionsGet;
    httpd_register_uri_handler(server_, &actions_uri);

    httpd_uri_t status_uri = {};
    status_uri.uri = "/api/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = HandleApiStatusGet;
    httpd_register_uri_handler(server_, &status_uri);

    httpd_uri_t cmd_uri = {};
    cmd_uri.uri = "/api/cmd";
    cmd_uri.method = HTTP_POST;
    cmd_uri.handler = HandleApiCommandPost;
    httpd_register_uri_handler(server_, &cmd_uri);

    httpd_uri_t ws_uri = {};
    ws_uri.uri = "/ws";
    ws_uri.method = HTTP_GET;
    ws_uri.handler = HandleWs;
    ws_uri.is_websocket = true;
    httpd_register_uri_handler(server_, &ws_uri);

    RobonovaController::GetInstance().SetStatusCallback([this](const std::string& status_json) {
        BroadcastStatus(status_json);
    });

    ESP_LOGI(TAG, "Robonova2 Web Control & WebSocket Server started on port %d", port);
    return true;
}

void RobonovaWebServer::Stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(clients_mutex_);
    ws_clients_.clear();
}

esp_err_t RobonovaWebServer::HandleIndexGet(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, kDashboardHtml, sizeof(kDashboardHtml) - 1);
}

esp_err_t RobonovaWebServer::HandleApiActionsGet(httpd_req_t* req) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& a : RobonovaController::GetAllActions()) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "k", a.k_num);
        cJSON_AddNumberToObject(item, "dec", a.uart_byte);
        cJSON_AddStringToObject(item, "slug", a.slug);
        cJSON_AddStringToObject(item, "name", a.name_vi);
        cJSON_AddStringToObject(item, "cat", a.category);
        cJSON_AddItemToArray(arr, item);
    }
    char* json_str = cJSON_PrintUnformatted(arr);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_sendstr(req, json_str ? json_str : "[]");
    if (json_str) cJSON_free(json_str);
    cJSON_Delete(arr);
    return ret;
}

esp_err_t RobonovaWebServer::HandleApiStatusGet(httpd_req_t* req) {
    std::string status = RobonovaController::GetInstance().BuildStatusJson();
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, status.c_str());
}

esp_err_t RobonovaWebServer::HandleApiCommandPost(httpd_req_t* req) {
    if (req->content_len <= 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body length");
        return ESP_FAIL;
    }
    std::vector<char> buf(static_cast<size_t>(req->content_len) + 1, '\0');
    int received = httpd_req_recv(req, buf.data(), req->content_len);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        return ESP_FAIL;
    }
    std::string reply = GetInstance().ProcessJsonCommand(buf.data(), static_cast<size_t>(received));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, reply.c_str());
}

esp_err_t RobonovaWebServer::HandleWs(httpd_req_t* req) {
    auto& self = GetInstance();
    int sock_fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        {
            std::lock_guard<std::mutex> lock(self.clients_mutex_);
            self.ws_clients_.insert(sock_fd);
        }
        ESP_LOGI(TAG, "WebSocket client connected: fd=%d", sock_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    std::vector<uint8_t> payload(ws_pkt.len + 1, 0);
    if (ws_pkt.len > 0) {
        ws_pkt.payload = payload.data();
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        std::lock_guard<std::mutex> lock(self.clients_mutex_);
        self.ws_clients_.erase(sock_fd);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0) {
        std::string reply = self.ProcessJsonCommand(reinterpret_cast<const char*>(payload.data()), ws_pkt.len);
        if (!reply.empty()) {
            httpd_ws_frame_t resp = {};
            resp.type = HTTPD_WS_TYPE_TEXT;
            resp.payload = reinterpret_cast<uint8_t*>(reply.data());
            resp.len = reply.size();
            httpd_ws_send_frame(req, &resp);
        }
    }
    return ESP_OK;
}

std::string RobonovaWebServer::ProcessJsonCommand(const char* json_str, size_t len) {
    if (json_str == nullptr || len == 0 || len > 4096) {
        return "{\"success\":false,\"error\":\"invalid_payload\"}";
    }

    cJSON* root = cJSON_ParseWithLength(json_str, len);
    if (root == nullptr) {
        return "{\"success\":false,\"error\":\"invalid_json\"}";
    }

    auto& ctrl = RobonovaController::GetInstance();
    cJSON* cmd_item = cJSON_GetObjectItem(root, "cmd");

    // 1. Nếu là gói MCP JSON-RPC (tương thích ứng dụng điều khiển Otto / Xiaozhi qua ws://<IP>:8080/ws)
    if (cmd_item == nullptr) {
        cJSON* type_item = cJSON_GetObjectItem(root, "type");
        if (type_item && cJSON_IsString(type_item) && std::strcmp(type_item->valuestring, "mcp") == 0) {
            cJSON* payload = cJSON_GetObjectItem(root, "payload");
            if (payload) {
                cJSON* dup = cJSON_Duplicate(payload, 1);
                McpServer::GetInstance().ParseMessage(dup);
                cJSON_Delete(dup);
            }
        } else if (cJSON_GetObjectItem(root, "jsonrpc") != nullptr) {
            cJSON* dup = cJSON_Duplicate(root, 1);
            McpServer::GetInstance().ParseMessage(dup);
            cJSON_Delete(dup);
        }
        cJSON_Delete(root);
        return ctrl.BuildStatusJson();
    }

    // 2. Xử lý lệnh trực tiếp từ giao diện Web Control :8080
    std::string cmd = cJSON_IsString(cmd_item) ? cmd_item->valuestring : "";
    if (cmd == "action") {
        cJSON* k_item = cJSON_GetObjectItem(root, "k");
        cJSON* rep_item = cJSON_GetObjectItem(root, "repeat");
        int rep = cJSON_IsNumber(rep_item) ? rep_item->valueint : 1;
        if (cJSON_IsNumber(k_item)) {
            ctrl.QueueActionByK(k_item->valueint, rep);
        } else if (cJSON* name_item = cJSON_GetObjectItem(root, "name"); cJSON_IsString(name_item)) {
            ctrl.QueueActionByName(name_item->valuestring, rep);
        }
    } else if (cmd == "combo") {
        if (cJSON* seq = cJSON_GetObjectItem(root, "sequence"); cJSON_IsString(seq)) {
            ctrl.QueueCombo(seq->valuestring);
        }
    } else if (cmd == "stop") {
        cJSON* relax = cJSON_GetObjectItem(root, "relax");
        ctrl.StopAll(cJSON_IsTrue(relax));
    } else if (cmd == "volume") {
        if (cJSON* val = cJSON_GetObjectItem(root, "val"); cJSON_IsNumber(val)) {
            int v = std::clamp(val->valueint, 0, 100);
            if (auto* codec = Board::GetInstance().GetAudioCodec()) {
                codec->SetOutputVolume(v);
            }
        }
    } else if (cmd == "brightness") {
        if (cJSON* val = cJSON_GetObjectItem(root, "val"); cJSON_IsNumber(val)) {
            int b = std::clamp(val->valueint, 0, 100);
            if (auto* bl = Board::GetInstance().GetBacklight()) {
                bl->SetBrightness(static_cast<uint8_t>(b), true);
            }
        }
    } else if (cmd == "dance_sync") {
        cJSON* en = cJSON_GetObjectItem(root, "enable");
        ctrl.SetDanceWithMusic(cJSON_IsTrue(en));
    } else if (cmd == "auto_sensor") {
        cJSON* en = cJSON_GetObjectItem(root, "enable");
        ctrl.SetAutoSensorMode(cJSON_IsTrue(en));
    } else if (cmd == "auto_stairs") {
        cJSON* en = cJSON_GetObjectItem(root, "enable");
        ctrl.SetAutoClimbStairs(cJSON_IsTrue(en));
    } else if (cmd == "music") {
        cJSON* song = cJSON_GetObjectItem(root, "song");
        cJSON* dance = cJSON_GetObjectItem(root, "dance");
        std::string song_name = cJSON_IsString(song) ? song->valuestring : "";
        bool with_dance = cJSON_IsTrue(dance);
        if (!song_name.empty()) {
            Application::GetInstance().Schedule([song_name, with_dance]() {
                if (auto* radio = Application::GetInstance().GetRadio()) {
                    if (radio->IsPlaying()) radio->Stop();
                }
                if (auto* music = Application::GetInstance().GetMusic()) {
                    music->Download(song_name, "");
                }
                RobonovaController::GetInstance().SetDanceWithMusic(with_dance);
            });
        }
    } else if (cmd == "radio") {
        cJSON* st = cJSON_GetObjectItem(root, "station");
        std::string station = cJSON_IsString(st) ? st->valuestring : "VOV1";
        Application::GetInstance().Schedule([station]() {
            if (auto* music = Application::GetInstance().GetMusic()) {
                music->StopStreaming();
            }
            if (auto* radio = Application::GetInstance().GetRadio()) {
                radio->PlayStation(station);
            }
        });
    } else if (cmd == "stop_media") {
        ctrl.SetDanceWithMusic(false);
        Application::GetInstance().Schedule([]() {
            if (auto* music = Application::GetInstance().GetMusic()) {
                music->StopStreaming();
            }
            if (auto* radio = Application::GetInstance().GetRadio()) {
                radio->Stop();
            }
        });
    } else if (cmd == "toggle_spi_mode") {
        Settings settings("robonova", true);
        int cur = settings.GetInt("lcd_spi_mode", DISPLAY_SPI_MODE);
        int next = (cur == 0) ? 3 : 0;
        settings.SetInt("lcd_spi_mode", next);
        if (auto* disp = Board::GetInstance().GetDisplay()) {
            disp->ShowNotification("SPI Mode -> " + std::to_string(next) + " (Khởi động lại)", 3000);
        }
        Application::GetInstance().Schedule([]() {
            vTaskDelay(pdMS_TO_TICKS(1000));
            Application::GetInstance().Reboot();
        });
    }

    cJSON_Delete(root);
    return ctrl.BuildStatusJson();
}

void RobonovaWebServer::BroadcastStatus(const std::string& status_json) {
    if (server_ == nullptr || status_json.empty()) {
        return;
    }

    std::vector<int> fds;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        fds.assign(ws_clients_.begin(), ws_clients_.end());
    }
    if (fds.empty()) {
        return;
    }

    for (int fd : fds) {
        httpd_ws_frame_t pkt = {};
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(status_json.data()));
        pkt.len = status_json.size();

        esp_err_t err = httpd_ws_send_frame_async(server_, fd, &pkt);
        if (err != ESP_OK) {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            ws_clients_.erase(fd);
        }
    }
}
