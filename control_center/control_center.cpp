// SPDX-License-Identifier: GPL-3.0-only
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <unistd.h>

#include "json.hpp"
#include "ipc_udp.h"
#include "uuid.h"
#include "cfg.h"

using json = nlohmann::json;
static int g_audio_upload_enable = 1;
static std::string g_session_id;

typedef enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeAlwaysOn
} ListeningMode;

typedef enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
} DeviceState;

static p_ipc_endpoint_t g_ipc_ep_audio;
static p_ipc_endpoint_t g_ipc_ep_ui;
static p_ipc_endpoint_t g_ipc_ep_net;
static DeviceState g_device_state = kDeviceStateUnknown;

// Forward declarations
static void send_to_net(const std::string& msg);
static void send_to_net_binary(const char* data, int len);
static void process_hello_json(const char *buffer, size_t size);
static void process_other_json(const char *buffer, size_t size);
static void process_opus_data_downloaded(const char *buffer, size_t size);

static void set_device_state(DeviceState state)
{
    g_device_state = state;
}

static void send_device_state(void)
{
    if (!g_ipc_ep_ui) return;
    std::string stateString = "{\"state\":" + std::to_string(g_device_state) + "}";
    g_ipc_ep_ui->send(g_ipc_ep_ui, stateString.data(), stateString.size());
}

static void send_stt(const std::string& text)
{
    if (!g_ipc_ep_ui) return;
    try {
        json j;
        j["text"] = text;
        std::string textString = j.dump();
        g_ipc_ep_ui->send(g_ipc_ep_ui, textString.data(), textString.size());
    } catch (...) {}
}

static void send_start_listening_req(ListeningMode mode)
{
    std::string startString = "{\"session_id\":\"" + g_session_id + "\"";
    startString += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeAutoStop) {
        startString += ",\"mode\":\"auto\"}";
    } else if (mode == kListeningModeManualStop) {
        startString += ",\"mode\":\"manual\"}";
    } else if (mode == kListeningModeAlwaysOn) {
        startString += ",\"mode\":\"realtime\"}";
    }
    send_to_net(startString);
    std::cout << "Send: " << startString << std::endl;
}

static void process_hello_json(const char *buffer, size_t size)
{
    try {
        json j = json::parse(buffer, buffer + size);
        int sample_rate = j["audio_params"]["sample_rate"];
        int channels = j["audio_params"]["channels"];
        std::cout << "Received valid 'hello' message with sample_rate: " << sample_rate << " and channels: " << channels << std::endl;     
        g_session_id = j["session_id"];

        std::string desc = R"({"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Speaker","description":"扬声器","properties":{"volume":{"description":"当前音量值","type":"number"}},"methods":{"SetVolume":{"description":"设置音量","parameters":{"volume":{"description":"0到100之间的整数","type":"number"}}}}}]})";
        send_to_net(desc);

        std::string desc2 = R"({"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Backlight","description":"屏幕背光","properties":{"brightness":{"description":"当前亮度百分比","type":"number"}},"methods":{"SetBrightness":{"description":"设置亮度","parameters":{"brightness":{"description":"0到100之间的整数","type":"number"}}}}}]})";
        send_to_net(desc2);

        std::string desc3 = R"({"session_id":"","type":"iot","update":true,"descriptors":[{"name":"Battery","description":"电池管理","properties":{"level":{"description":"当前电量百分比","type":"number"},"charging":{"description":"是否充电中","type":"boolean"}},"methods":{}}]})";
        send_to_net(desc3);

        std::string startString = R"({"session_id":"","type":"listen","state":"start","mode":"auto"})";
        send_to_net(startString);

        std::string state = R"({"session_id":"","type":"iot","update":true,"states":[{"name":"Speaker","state":{"volume":80}},{"name":"Backlight","state":{"brightness":75}},{"name":"Battery","state":{"level":0,"charging":false}}]})";
        g_audio_upload_enable = 1;
        send_to_net(state);
    } catch (...) {}
}

static void process_other_json(const char *buffer, size_t size)
{
    try {
        json j = json::parse(buffer, buffer + size);
        if (!j.contains("type")) return;
        
        if (j["type"] == "tts") {
            auto state = j["state"];
            if (state == "start") {
                g_audio_upload_enable = 0;
                set_device_state(kDeviceStateListening);
                send_device_state();
            } else if (state == "stop") {
                sleep(2);
                send_start_listening_req(kListeningModeAutoStop);
                set_device_state(kDeviceStateListening);
                send_device_state();
                g_audio_upload_enable = 1;
            } else if (state == "sentence_start") {
                auto text = j["text"];
                send_stt(text.get<std::string>());
                send_start_listening_req(kListeningModeAutoStop);
                set_device_state(kDeviceStateSpeaking);
                send_device_state();
            }
        } else if (j["type"] == "stt") {
            auto text = j["text"];
            send_stt(text.get<std::string>());
        }
    } catch (...) {}
}

static void process_opus_data_downloaded(const char *buffer, size_t size)
{
    if (g_ipc_ep_audio) {
        g_ipc_ep_audio->send(g_ipc_ep_audio, buffer, size);
    }
}

// Global variable to store activation response
static std::string g_activation_response;
static bool g_activation_received = false;

int process_net_data(char *buffer, size_t size, void *user_data)
{
    if (size > 0 && buffer[0] == '{') {
        try {
            json j = json::parse(buffer, buffer + size);
            if (j.contains("type")) {
                std::string type = j["type"];
                if (type == "http_response") {
                    if (j.contains("body")) {
                        g_activation_response = j["body"];
                        g_activation_received = true;
                    }
                } else if (type == "hello") {
                     process_hello_json(buffer, size);
                } else {
                     process_other_json(buffer, size);
                }
            }
        } catch (...) {
            // Ignore parse errors
        }
    } else {
        process_opus_data_downloaded(buffer, size);
    }
    return 0;
}

static void send_to_net(const std::string& msg) {
    if (g_ipc_ep_net) {
        g_ipc_ep_net->send(g_ipc_ep_net, msg.data(), msg.size());
    }
}

static void send_to_net_binary(const char* data, int len) {
    if (g_ipc_ep_net) {
        g_ipc_ep_net->send(g_ipc_ep_net, data, len);
    }
}

int process_opus_data_uploaded(char *buffer, size_t size, void *user_data)
{
    if (g_audio_upload_enable) {
        static int cnt = 0;
        if ((cnt++ % 100) == 0)
            std::cout << "Send opus data to server: " << size <<" count: "<< cnt << std::endl;
        send_to_net_binary(buffer, size);
    }
    return 0;
}

int process_ui_data(char *buffer, size_t size, void *user_data)
{
    return 0;
}

std::string read_uuid_from_config() {
    std::ifstream config_file(CFG_FILE);
    if (!config_file.is_open()) return "";
    try {
        json config_json;
        config_file >> config_json;
        if (config_json.contains("uuid")) return config_json["uuid"].get<std::string>();
    } catch (...) {}
    return "";
}

bool write_uuid_to_config(const std::string& uuid) {
    std::ofstream config_file(CFG_FILE);
    if (!config_file.is_open()) return false;
    try {
        json config_json;
        config_json["uuid"] = uuid;
        config_file << config_json.dump(4);
        return true;
    } catch (...) {}
    return false;
}

int main(int argc, char **argv)
{
    std::string mac = get_wireless_mac_address();
    if (mac.empty()) mac = "00:00:00:00:00:00";

    std::string uuid = read_uuid_from_config();
    if (uuid.empty()) {
        uuid = generate_uuid();
        write_uuid_to_config(uuid);
    }
    std::cout << "UUID: " << uuid << std::endl;

    g_ipc_ep_audio = ipc_endpoint_create_udp(AUDIO_PORT_UP, AUDIO_PORT_DOWN, process_opus_data_uploaded, NULL);
    g_ipc_ep_ui = ipc_endpoint_create_udp(UI_PORT_UP, UI_PORT_DOWN, process_ui_data, NULL);
    g_ipc_ep_net = ipc_endpoint_create_udp(NET_BRIDGE_PORT_IN, NET_BRIDGE_PORT_OUT, process_net_data, NULL);

    // Activation Loop
    while (true) {
        json post_body;
        post_body["uuid"] = uuid;
        post_body["application"] = {{"name", "xiaozhi_linux_100ask"}, {"version", "1.0.0"}};
        post_body["ota"] = json::object();
        post_body["board"] = {{"type", "100ask_linux_board"}, {"name", "100ask_imx6ull_board"}};

        json headers;
        headers["Content-Type"] = "application/json";
        headers["Device-Id"] = mac;
        headers["User-Agent"] = "weidongshan1";
        headers["Accept-Language"] = "zh-CN";

        json cmd;
        cmd["cmd"] = "http_post";
        cmd["url"] = "https://api.tenclass.net/xiaozhi/ota/";
        cmd["headers"] = headers;
        cmd["body"] = post_body.dump();

        g_activation_received = false;
        send_to_net(cmd.dump());

        // Wait for response (simple polling)
        int timeout = 50; // 5 seconds
        while (!g_activation_received && timeout > 0) {
            usleep(100000);
            timeout--;
        }

        if (g_activation_received) {
            try {
                json resp = json::parse(g_activation_response);
                if (resp["code"] == 0) {
                    std::cout << "Device Activated!" << std::endl;
                    break;
                } else {
                    // Check for active code
                    if (resp.contains("data") && resp["data"].contains("code")) {
                         std::string code = resp["data"]["code"];
                         std::string auth_code = "Active-Code: " + code;
                         set_device_state(kDeviceStateActivating);
                         send_device_state();
                         send_stt(auth_code);
                    }
                }
            } catch (...) {}
        }
        sleep(5);
    }

    set_device_state(kDeviceStateIdle);
    send_device_state();
    send_stt("设备已经激活");

    // Connect WebSocket
    json ws_headers;
    ws_headers["Authorization"] = "Bearer test-token";
    ws_headers["Protocol-Version"] = "1";
    ws_headers["Device-Id"] = mac;
    ws_headers["Client-Id"] = uuid;

    json ws_cmd;
    ws_cmd["cmd"] = "ws_connect";
    ws_cmd["url"] = "wss://api.tenclass.net/xiaozhi/v1/";
    ws_cmd["headers"] = ws_headers;
    
    send_to_net(ws_cmd.dump());

    // Send Hello
    json hello;
    hello["type"] = "hello";
    hello["version"] = 1;
    hello["transport"] = "websocket";
    hello["audio_params"] = {{"format", "opus"}, {"sample_rate", 16000}, {"channels", 1}, {"frame_duration", 60}};
    
    // Wait a bit for connection
    sleep(1);
    send_to_net(hello.dump());

    while (1)
    {
        sleep(1);
    }
}
