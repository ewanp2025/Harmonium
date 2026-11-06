#ifndef WIFI_STREAM_H
#define WIFI_STREAM_H

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include "synthengine.h"

class WifiAudioStreamer {
public:
    void begin(SynthEngine* engine) {
        m_engine = engine;
        m_ring = static_cast<int16_t*>(heap_caps_malloc(RING_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!m_ring) m_ring = static_cast<int16_t*>(std::malloc(RING_SAMPLES * sizeof(int16_t)));
        if (m_ring) std::memset(m_ring, 0, RING_SAMPLES * sizeof(int16_t));

        WiFi.persistent(false);
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(false); 
        WiFi.softAP("Harmonium_Box", "12345678", 1, 0, 4);

        m_server.on("/", HTTP_GET, [this]() {
            String html = "<HTML><HEAD><META name='viewport' content='width=device-width,initial-scale=1'><TITLE>Harmonium Audio Stream</TITLE></HEAD>";
            html += "<BODY style='background:#111;color:#0ff;text-align:center;font-family:sans-serif;padding-top:40px;'>";
            html += "<h2>Harmonium ESP32 Live Audio</h2>";
            html += "<p>Tap Play to hear live 22.05 kHz audio:</p><br/>";
            html += "<audio controls autoplay src='/stream.wav' style='width:90%;max-width:450px;height:54px;'></audio>";
            html += "</BODY></HTML>";
            m_server.send(200, "text/html", html);
        });

        m_server.on("/stream.wav", HTTP_GET, [this]() {
            WiFiClient client = m_server.client();
            if (!client) return;
            client.setNoDelay(true);

            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: audio/wav");
            client.println("Cache-Control: no-cache, no-store");
            client.println("Connection: close");
            client.println();

            uint8_t header[44] = {
                'R','I','F','F', 0xFF,0xFF,0xFF,0x7F, 'W','A','V','E',
                'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
                0x22, 0x56, 0x00, 0x00, // 22050 Hz sample rate
                0x44, 0xAC, 0x00, 0x00, // Byte rate (44100 B/s)
                2, 0, 16, 0,
                'd','a','t','a', 0xFF,0xFF,0xFF,0x7F
            };
            client.write(header, 44);

            m_ringRead = m_ringWrite;
            m_clientActive = true;

            int16_t txChunk[512];
            while (client.connected()) {
                uint32_t avail = (m_ringWrite - m_ringRead) & RING_MASK;
                if (avail >= 512 && m_ring) {
                    for (int i = 0; i < 512; ++i) {
                        txChunk[i] = m_ring[m_ringRead];
                        m_ringRead = (m_ringRead + 1) & RING_MASK;
                    }
                    int written = client.write(reinterpret_cast<const uint8_t*>(txChunk), sizeof(txChunk));
                    if (written <= 0) break;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
            }
            m_clientActive = false;
            client.stop();
        });

        m_server.begin();

        
        xTaskCreatePinnedToCore(wifiServerTask, "wifi_srv", 6144, this, 2, nullptr, 0);
    }


    void pushMonoSamples(const float* monoBuf, int count) {
        if (!m_clientActive || !m_ring) return;
        for (int i = 0; i < count; ++i) {
            uint32_t nextW = (m_ringWrite + 1) & RING_MASK;
            if (nextW != m_ringRead) {
                m_ring[m_ringWrite] = static_cast<int16_t>(monoBuf[i] * 32767.0f);
                m_ringWrite = nextW;
            }
        }
    }

    void handle() {
        // Handled asynchronously by wifiServerTask on Core 0
    }

    static WifiAudioStreamer& instance() {
        static WifiAudioStreamer s;
        return s;
    }

    IPAddress getIP() const { return WiFi.softAPIP(); }

private:
    static constexpr uint32_t RING_SAMPLES = 16384;
    static constexpr uint32_t RING_MASK = RING_SAMPLES - 1;

    static void wifiServerTask(void* arg) {
        auto* self = static_cast<WifiAudioStreamer*>(arg);
        while (true) {
            self->m_server.handleClient();
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    WebServer m_server{80};
    SynthEngine* m_engine = nullptr;
    int16_t* m_ring = nullptr;
    volatile uint32_t m_ringWrite = 0;
    volatile uint32_t m_ringRead = 0;
    volatile bool m_clientActive = false;
};

#else
class WifiAudioStreamer {
public:
    void begin(void*) {}
    void pushMonoSamples(const float*, int) {}
    void handle() {}
    static WifiAudioStreamer& instance() { static WifiAudioStreamer s; return s; }
};
#endif

#endif // WIFI_STREAM_H
