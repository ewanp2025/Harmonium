#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <cstdint>

struct BtDeviceInfo {
    std::string name;
    std::string mac;
    int rssi = -60;
    bool hasRealName = false;
};

#ifdef ESP32
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

class BtAudioManager : public BLEAdvertisedDeviceCallbacks {
public:
    void begin() {
        loadPairedDevice();
        // Do NOT call BLEDevice::init() on boot so the 2.4 GHz radio stays 100% dedicated
        // to the Harmonium_Box Wi-Fi hotspot until the user explicitly scans for BLE.
        Serial1.begin(921600, SERIAL_8N1, 44, 43);

        if (!m_savedMac.empty()) {
            connectToSaved();
        }
    }

    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        std::string devMac = advertisedDevice.getAddress().toString().c_str();
        std::string rawName = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "";
        int rssi = advertisedDevice.getRSSI();

        addOrUpdateDevice(rawName, devMac, rssi);

        if (!m_connected && !m_savedMac.empty() && devMac == m_savedMac) {
            m_connected = true;
            m_status = "Connected: " + m_savedName;
        }
    }

    void startScan() {
        if (m_scanning) return;
        m_devices.clear();
        m_scanning = true;

        if (!m_bleInitialized) {
            BLEDevice::init("Harmonium_S3");
            m_pBLEScan = BLEDevice::getScan();
            m_pBLEScan->setAdvertisedDeviceCallbacks(this, true);
            m_pBLEScan->setActiveScan(true);
            m_pBLEScan->setInterval(80);
            m_pBLEScan->setWindow(79);
            m_bleInitialized = true;
        }

        Serial1.println("AT+SCAN");

        if (m_pBLEScan) {
            m_pBLEScan->clearResults();
            BLEScanResults* found = m_pBLEScan->start(3, false);
            if (found) {
                int count = found->getCount();
                for (int i = 0; i < count; ++i) {
                    BLEAdvertisedDevice d = found->getDevice(i);
                    std::string mac = d.getAddress().toString().c_str();
                    std::string name = d.haveName() ? d.getName().c_str() : "";
                    addOrUpdateDevice(name, mac, d.getRSSI());
                }
            }
            m_pBLEScan->clearResults();
        }

        pollUartBridge();

        std::sort(m_devices.begin(), m_devices.end(), [](const BtDeviceInfo& a, const BtDeviceInfo& b) {
            if (a.hasRealName != b.hasRealName) return a.hasRealName > b.hasRealName;
            return a.rssi > b.rssi;
        });

        m_scanning = false;
        if (m_devices.empty()) {
            m_status = "No BLE devices found (Classic-only speakers need UART bridge)";
        } else {
            m_status = "Found " + std::to_string(m_devices.size()) + " device(s). Tap one to pair:";
        }
    }

    void pollUartBridge() {
        while (Serial1.available()) {
            String line = Serial1.readStringUntil('\n');
            line.trim();
            if (line.startsWith("+DEV:")) {
                int c1 = line.indexOf(',', 5);
                int c2 = line.indexOf(',', c1 + 1);
                if (c1 > 5) {
                    std::string mac = line.substring(5, c1).c_str();
                    std::string name = (c2 > c1) ? line.substring(c1 + 1, c2).c_str()
                                                 : line.substring(c1 + 1).c_str();
                    int rssi = (c2 > c1) ? line.substring(c2 + 1).toInt() : -50;
                    addOrUpdateDevice(name, mac, rssi);
                }
            } else if (line.startsWith("+CONNECTED")) {
                m_connected = true;
                m_status = "Connected: " + m_savedName;
            }
        }
    }

    void selectAndPairDevice(int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_devices.size())) return;
        m_savedName = m_devices[idx].name;
        m_savedMac  = m_devices[idx].mac;
        savePairedDevice();
        connectToSaved();
    }

    void forgetPairedDevice() {
        m_savedName.clear();
        m_savedMac.clear();
        m_connected = false;
        LittleFS.remove("/bt_paired.json");
        Serial1.println("AT+DISCONN");
        m_status = "Unpaired. Tap SCAN PAIR MODE to search.";
    }

    void writeAudioFrames(const int16_t* monoSamples, int count) {
        if (!m_connected) return;
        if (Serial1.availableForWrite() >= static_cast<int>(count * sizeof(int16_t))) {
            Serial1.write(reinterpret_cast<const uint8_t*>(monoSamples), count * sizeof(int16_t));
        }
    }

    bool isScanning() const { return m_scanning; }
    bool isConnected() const { return m_connected; }
    const std::string& getSavedName() const { return m_savedName; }
    const std::string& getSavedMac() const { return m_savedMac; }
    const std::string& getStatusText() const { return m_status; }
    const std::vector<BtDeviceInfo>& getDevices() const { return m_devices; }

    static BtAudioManager& instance() {
        static BtAudioManager s_inst;
        return s_inst;
    }

private:
    BLEScan* m_pBLEScan = nullptr;
    bool m_bleInitialized = false;
    std::vector<BtDeviceInfo> m_devices;
    std::string m_savedName;
    std::string m_savedMac;
    std::string m_status = "Not Paired (Tap SCAN PAIR MODE)";
    volatile bool m_scanning = false;
    volatile bool m_connected = false;

    void addOrUpdateDevice(const std::string& rawName, const std::string& mac, int rssi) {
        bool hasName = !rawName.empty();
        std::string displayName = hasName ? rawName : ("BT [" + mac.substr(mac.length() > 8 ? mac.length() - 8 : 0) + "]");

        for (auto& d : m_devices) {
            if (d.mac == mac) {
                if (hasName || !d.hasRealName) {
                    d.name = displayName;
                    d.hasRealName = hasName;
                }
                d.rssi = std::max(d.rssi, rssi);
                return;
            }
        }
        if (m_devices.size() < 12) {
            m_devices.push_back({displayName, mac, rssi, hasName});
        }
    }

    void connectToSaved() {
        if (m_savedMac.empty()) return;
        Serial1.printf("AT+CONN=%s,%s\n", m_savedMac.c_str(), m_savedName.c_str());
        m_connected = true;
        m_status = "Paired & Stored: " + m_savedName;
    }

    void savePairedDevice() {
        File f = LittleFS.open("/bt_paired.json", "w");
        if (!f) return;
        JsonDocument doc;
        doc["name"] = m_savedName;
        doc["mac"]  = m_savedMac;
        serializeJson(doc, f);
        f.close();
    }

    void loadPairedDevice() {
        File f = LittleFS.open("/bt_paired.json", "r");
        if (!f) return;
        JsonDocument doc;
        if (!deserializeJson(doc, f)) {
            m_savedName = doc["name"] | "";
            m_savedMac  = doc["mac"]  | "";
            if (!m_savedName.empty()) {
                m_status = "Saved: " + m_savedName;
            }
        }
        f.close();
    }
};

#else
class BtAudioManager {
public:
    void begin() {
        m_devices = {
            {"JBL Flip 6", "A4:77:33:11:22:01", -48, true},
            {"Sony WH-1000XM5", "CC:98:8B:44:10:99", -55, true},
            {"Bose SoundLink", "08:DF:1F:77:88:32", -64, true}
        };
    }
    void startScan() {
        m_scanning = false;
        m_status = "Found 3 devices. Tap one to pair:";
    }
    void pollUartBridge() {}
    void selectAndPairDevice(int idx) {
        if (idx >= 0 && idx < static_cast<int>(m_devices.size())) {
            m_savedName = m_devices[idx].name;
            m_savedMac  = m_devices[idx].mac;
            m_connected = true;
            m_status = "Paired & Stored: " + m_savedName;
        }
    }
    void forgetPairedDevice() {
        m_savedName.clear();
        m_savedMac.clear();
        m_connected = false;
        m_status = "Device forgotten.";
    }
    void writeAudioFrames(const int16_t*, int) {}
    bool isScanning() const { return m_scanning; }
    bool isConnected() const { return m_connected; }
    const std::string& getSavedName() const { return m_savedName; }
    const std::string& getSavedMac() const { return m_savedMac; }
    const std::string& getStatusText() const { return m_status; }
    const std::vector<BtDeviceInfo>& getDevices() const { return m_devices; }

    static BtAudioManager& instance() {
        static BtAudioManager s_inst;
        return s_inst;
    }
private:
    std::vector<BtDeviceInfo> m_devices;
    std::string m_savedName;
    std::string m_savedMac;
    std::string m_status = "Tap SCAN PAIR MODE to search";
    bool m_scanning = false;
    bool m_connected = false;
};
#endif

#endif // BT_AUDIO_H
