#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#ifdef ESP32
#include <Arduino.h>
#include <usb/usb_host.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "usb_midi_host.h"

class UsbAudioOut : public UsbAudioPartner {
public:
    void begin() {
        m_ringL = static_cast<int16_t*>(heap_caps_malloc(RING_SIZE * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        m_ringR = static_cast<int16_t*>(heap_caps_malloc(RING_SIZE * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!m_ringL) m_ringL = static_cast<int16_t*>(heap_caps_malloc(RING_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!m_ringR) m_ringR = static_cast<int16_t*>(heap_caps_malloc(RING_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (m_ringL) std::memset(m_ringL, 0, RING_SIZE * sizeof(int16_t));
        if (m_ringR) std::memset(m_ringR, 0, RING_SIZE * sizeof(int16_t));

        UsbAudioPartner::s_instance = this;
    }

    bool isConnected() const { return m_connected; }

    const char* getStatusLabel() const {
        if (m_connected) {
            return (m_urbOkCount > 0) ? m_activeLabel : "USB: CLAIMED";
        }
        return m_statusLabel;
    }

    void writePcm(const void* data, size_t bytes) {
        const int16_t* inStereo = static_cast<const int16_t*>(data);
        int inFrames = static_cast<int>(bytes / (2 * sizeof(int16_t)));

        if (!m_connected || !m_devHdl || !m_ringL || !m_ringR) {
            vTaskDelay(pdMS_TO_TICKS(11));
            return;
        }

        for (int i = 0; i < inFrames; ++i) {
            int16_t sL = inStereo[i * 2];
            int16_t sR = inStereo[i * 2 + 1];

            m_phaseAccum += m_usbSampleRate;
            while (m_phaseAccum >= 22050) {
                m_phaseAccum -= 22050;
                uint32_t nextW = (m_ringWrite + 1) & RING_MASK;
                int spin = 0;
                while (nextW == m_ringRead && m_connected) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    if (++spin > 30) break;
                }
                if (nextW != m_ringRead) {
                    m_ringL[m_ringWrite] = sL;
                    m_ringR[m_ringWrite] = sR;
                    m_ringWrite = nextW;
                }
            }
        }
    }

    bool tryClaimAudioInterface(usb_host_client_handle_t clientHdl,
                                usb_device_handle_t devHdl,
                                const usb_config_desc_t* configDesc) override {
        AltCandidate cands[12];
        int numCands = 0;
        int curCand = -1;
        bool isAudioControl = false;

        m_ctrlIface = 0;
        m_featureUnitId = -1;

        int offset = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(configDesc);
        while (offset < configDesc->wTotalLength) {
            uint8_t len = p[offset];
            uint8_t type = p[offset + 1];
            if (len == 0) break;

            if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
                auto* ifc = reinterpret_cast<const usb_intf_desc_t*>(&p[offset]);
                isAudioControl = (ifc->bInterfaceClass == 0x01 && ifc->bInterfaceSubClass == 0x01);
                if (isAudioControl) m_ctrlIface = ifc->bInterfaceNumber;

                if (ifc->bInterfaceClass == 0x01 && ifc->bInterfaceSubClass == 0x02 && ifc->bAlternateSetting > 0 && numCands < 12) {
                    curCand = numCands++;
                    cands[curCand] = AltCandidate{};
                    cands[curCand].iface = ifc->bInterfaceNumber;
                    cands[curCand].alt = ifc->bAlternateSetting;
                } else {
                    curCand = -1;
                }
            } else if (isAudioControl && type == 0x24 && len >= 6 && p[offset + 2] == 0x06) {
                if (m_featureUnitId < 0) m_featureUnitId = p[offset + 3];
            } else if (curCand >= 0 && type == 0x24 && len >= 8 && p[offset + 2] == 0x02) {
                uint8_t fmtType = p[offset + 3];
                uint8_t ch = p[offset + 4];
                uint8_t subFrameSize = p[offset + 5];
                uint8_t samFreqType = p[offset + 7];
                if (fmtType == 0x01) {
                    cands[curCand].hasFormat = true;
                    cands[curCand].channels = ch;
                    cands[curCand].subFrameSize = subFrameSize;
                    if (samFreqType == 0 && len >= 14) {
                        uint32_t lo = p[offset + 8]  | (p[offset + 9] << 8)  | (p[offset + 10] << 16);
                        uint32_t hi = p[offset + 11] | (p[offset + 12] << 8) | (p[offset + 13] << 16);
                        cands[curCand].sampleRate = std::clamp<uint32_t>(48000u, lo, hi);
                        cands[curCand].hasFreqCtrl = true;
                    } else if (samFreqType > 0) {
                        cands[curCand].sampleRate = p[offset + 8] | (p[offset + 9] << 8) | (p[offset + 10] << 16);
                        if (samFreqType > 1) cands[curCand].hasFreqCtrl = true;
                        for (uint8_t k = 0; k < samFreqType; ++k) {
                            if (offset + 10 + k * 3 < configDesc->wTotalLength) {
                                uint32_t r = p[offset + 8 + k * 3] | (p[offset + 9 + k * 3] << 8) | (p[offset + 10 + k * 3] << 16);
                                if (r == 48000 || r == 44100 || r == 32000 || r == 22050) {
                                    cands[curCand].sampleRate = r;
                                    if (r == 48000) break;
                                }
                            }
                        }
                    }
                }
            } else if (curCand >= 0 && type == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                auto* ep = reinterpret_cast<const usb_ep_desc_t*>(&p[offset]);
                bool isOut = (ep->bEndpointAddress & 0x80) == 0;
                uint8_t xferType = ep->bmAttributes & 0x03;
                if (isOut && xferType == 0x01) {
                    cands[curCand].hasIsoOutEp = true;
                    cands[curCand].epAddr = ep->bEndpointAddress;
                    cands[curCand].maxPacketSize = (ep->wMaxPacketSize & 0x7FF);
                }
            } else if (curCand >= 0 && type == 0x25 && len >= 4 && p[offset + 2] == 0x01) {
                if (p[offset + 3] & 0x01) cands[curCand].hasFreqCtrl = true;
            }
            offset += len;
        }

        int bestIdx = -1;
        for (int i = 0; i < numCands; ++i) {
            if (!cands[i].hasIsoOutEp || !cands[i].hasFormat) continue;
            if (cands[i].subFrameSize != 2 || cands[i].maxPacketSize > 256) continue;
            if (bestIdx < 0 || (cands[i].channels == 2 && cands[bestIdx].channels != 2)) {
                bestIdx = i;
            }
        }

        if (bestIdx < 0) {
            return false;
        }

        const auto& chosen = cands[bestIdx];
        if (usb_host_interface_claim(clientHdl, devHdl, chosen.iface, chosen.alt) != ESP_OK) {
            std::snprintf(m_statusLabel, sizeof(m_statusLabel), "USB: FIFO ERR");
            return false;
        }

        m_clientHdl = clientHdl;
        m_devHdl = devHdl;
        m_streamIface = chosen.iface;
        m_streamAlt = chosen.alt;
        m_epAddr = chosen.epAddr;
        m_channels = chosen.channels;
        m_maxPacketSize = std::max<uint16_t>(chosen.maxPacketSize, 192);
        m_usbSampleRate = (chosen.sampleRate >= 8000 && chosen.sampleRate <= 48000) ? chosen.sampleRate : 48000;

        usb_host_transfer_alloc(64, 0, &m_ctrlXfer);


        submitControlSync(0x01, 0x0B, static_cast<uint16_t>(m_streamAlt), static_cast<uint16_t>(m_streamIface), nullptr, 0);

     
        if (chosen.hasFreqCtrl) {
            uint8_t rateBytes[3] = {
                static_cast<uint8_t>(m_usbSampleRate & 0xFF),
                static_cast<uint8_t>((m_usbSampleRate >> 8) & 0xFF),
                static_cast<uint8_t>((m_usbSampleRate >> 16) & 0xFF)
            };
            submitControlSync(0x22, 0x01, 0x0100, static_cast<uint16_t>(m_epAddr), rateBytes, 3);
        }

     
        if (m_featureUnitId >= 0) {
            uint8_t unmute = 0x00;
            uint8_t vol0dB[2] = {0x00, 0x00};
            uint16_t fuIndex = static_cast<uint16_t>((m_featureUnitId << 8) | (m_ctrlIface & 0xFF));
            for (uint16_t ch = 0; ch <= 2; ++ch) {
                submitControlSync(0x21, 0x01, (0x01 << 8) | ch, fuIndex, &unmute, 1);
                submitControlSync(0x21, 0x01, (0x02 << 8) | ch, fuIndex, vol0dB, 2);
            }
        }

   
        m_ringRead = 0;
        m_ringWrite = 256;
        if (m_ringL) std::memset(m_ringL, 0, RING_SIZE * sizeof(int16_t));
        if (m_ringR) std::memset(m_ringR, 0, RING_SIZE * sizeof(int16_t));
        m_pktFrameAccum = 0;
        m_urbOkCount = 0;
        m_connected = true;

        std::snprintf(m_activeLabel, sizeof(m_activeLabel), "USB: %luK OK", static_cast<unsigned long>(m_usbSampleRate / 1000));

        for (uint32_t u = 0; u < NUM_URBS; ++u) {
            usb_host_transfer_alloc(m_maxPacketSize * NUM_ISOC_PACKETS, NUM_ISOC_PACKETS, &m_isocXfer[u]);
            if (m_isocXfer[u]) {
                m_isocXfer[u]->device_handle = m_devHdl;
                m_isocXfer[u]->bEndpointAddress = m_epAddr;
                m_isocXfer[u]->callback = onIsocTransfer;
                m_isocXfer[u]->context = this;
                m_isocXfer[u]->timeout_ms = 200;
                fillAndSubmitIsoc(m_isocXfer[u]);
            }
        }
        return true;
    }

    void onDeviceDisconnected(usb_host_client_handle_t clientHdl,
                              usb_device_handle_t devHdl) override {
        m_connected = false;
        m_urbOkCount = 0;
        std::snprintf(m_statusLabel, sizeof(m_statusLabel), "USB: NO DEV");
        for (uint32_t u = 0; u < NUM_URBS; ++u) {
            if (m_isocXfer[u]) {
                usb_host_transfer_free(m_isocXfer[u]);
                m_isocXfer[u] = nullptr;
            }
        }
        if (m_ctrlXfer) {
            usb_host_transfer_free(m_ctrlXfer);
            m_ctrlXfer = nullptr;
        }
        if (devHdl && m_streamIface >= 0) {
            usb_host_interface_release(clientHdl, devHdl, m_streamIface);
            m_streamIface = -1;
        }
        m_devHdl = nullptr;
    }

private:
    static constexpr uint32_t NUM_ISOC_PACKETS = 4;
    static constexpr uint32_t NUM_URBS = 2;
    static constexpr uint32_t RING_SIZE = 1024;
    static constexpr uint32_t RING_MASK = RING_SIZE - 1;

    struct AltCandidate {
        int iface = -1;
        int alt = 0;
        bool hasFormat = false;
        bool hasIsoOutEp = false;
        bool hasFreqCtrl = false;
        uint8_t channels = 2;
        uint8_t subFrameSize = 2;
        uint8_t epAddr = 0;
        uint16_t maxPacketSize = 192;
        uint32_t sampleRate = 48000;
    };

    usb_host_client_handle_t m_clientHdl = nullptr;
    usb_device_handle_t m_devHdl = nullptr;
    usb_transfer_t* m_ctrlXfer = nullptr;
    usb_transfer_t* m_isocXfer[NUM_URBS] = {nullptr, nullptr};

    volatile bool m_ctrlDone = false;
    volatile bool m_connected = false;
    volatile uint32_t m_urbOkCount = 0;

    char m_statusLabel[24] = "USB: NO DEV";
    char m_activeLabel[24] = "USB: 48K OK";

    int m_ctrlIface = 0;
    int m_featureUnitId = -1;
    int m_streamIface = -1;
    int m_streamAlt = 1;
    uint8_t m_epAddr = 0;
    uint8_t m_channels = 2;
    uint16_t m_maxPacketSize = 192;
    uint32_t m_usbSampleRate = 48000;
    uint32_t m_phaseAccum = 0;
    uint32_t m_pktFrameAccum = 0;

    int16_t* m_ringL = nullptr;
    int16_t* m_ringR = nullptr;
    volatile uint32_t m_ringWrite = 0;
    volatile uint32_t m_ringRead = 0;

    static void onCtrlTransfer(usb_transfer_t* xfer) {
        auto* self = static_cast<UsbAudioOut*>(xfer->context);
        self->m_ctrlDone = true;
    }

    void fillAndSubmitIsoc(usb_transfer_t* xfer) {
        if (!m_connected || !m_devHdl || !xfer) return;

        uint8_t* bufPtr = xfer->data_buffer;
        uint32_t totalBytes = 0;

        for (uint32_t p = 0; p < NUM_ISOC_PACKETS; ++p) {
            m_pktFrameAccum += m_usbSampleRate;
            uint32_t framesThisPkt = m_pktFrameAccum / 1000;
            m_pktFrameAccum %= 1000;

            uint32_t pktBytes = framesThisPkt * m_channels * sizeof(int16_t);
            if (pktBytes > m_maxPacketSize) {
                pktBytes = m_maxPacketSize;
                framesThisPkt = pktBytes / (m_channels * sizeof(int16_t));
            }

            int16_t* outPcm = reinterpret_cast<int16_t*>(bufPtr + totalBytes);
            for (uint32_t f = 0; f < framesThisPkt; ++f) {
                int16_t l = 0, r = 0;
                if (m_ringL && m_ringR && m_ringRead != m_ringWrite) {
                    l = m_ringL[m_ringRead];
                    r = m_ringR[m_ringRead];
                    m_ringRead = (m_ringRead + 1) & RING_MASK;
                }
                if (m_channels == 2) {
                    *outPcm++ = l;
                    *outPcm++ = r;
                } else {
                    *outPcm++ = l;
                }
            }

            xfer->isoc_packet_desc[p].num_bytes = pktBytes;
            totalBytes += pktBytes;
        }

        xfer->num_bytes = totalBytes;
        usb_host_transfer_submit(xfer);
    }

    static void onIsocTransfer(usb_transfer_t* xfer) {
        auto* self = static_cast<UsbAudioOut*>(xfer->context);
        if (xfer->status == USB_TRANSFER_STATUS_COMPLETED) {
            self->m_urbOkCount = self->m_urbOkCount + 1;
        }
        if (self->m_connected && self->m_devHdl) {
            self->fillAndSubmitIsoc(xfer);
        }
    }

    bool submitControlSync(uint8_t bmReqType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, const uint8_t* payload, uint16_t wLength) {
        if (!m_ctrlXfer || !m_devHdl || !m_clientHdl) return false;
        uint8_t* b = m_ctrlXfer->data_buffer;
        b[0] = bmReqType;
        b[1] = bRequest;
        b[2] = static_cast<uint8_t>(wValue & 0xFF);
        b[3] = static_cast<uint8_t>((wValue >> 8) & 0xFF);
        b[4] = static_cast<uint8_t>(wIndex & 0xFF);
        b[5] = static_cast<uint8_t>((wIndex >> 8) & 0xFF);
        b[6] = static_cast<uint8_t>(wLength & 0xFF);
        b[7] = static_cast<uint8_t>((wLength >> 8) & 0xFF);
        if (wLength > 0 && payload) {
            std::memcpy(&b[8], payload, wLength);
        }

        m_ctrlXfer->device_handle = m_devHdl;
        m_ctrlXfer->bEndpointAddress = 0;
        m_ctrlXfer->callback = onCtrlTransfer;
        m_ctrlXfer->context = this;
        m_ctrlXfer->timeout_ms = 250;
        m_ctrlXfer->num_bytes = 8 + wLength;

        m_ctrlDone = false;
        if (usb_host_transfer_submit_control(m_clientHdl, m_ctrlXfer) != ESP_OK) {
            return false;
        }

        uint32_t startMs = millis();
        while (!m_ctrlDone && (millis() - startMs < 300)) {
            usb_host_client_handle_events(m_clientHdl, pdMS_TO_TICKS(5));
        }
        return m_ctrlDone && (m_ctrlXfer->status == USB_TRANSFER_STATUS_COMPLETED);
    }
};
#else
class UsbAudioOut {
public:
    void begin() {}
    bool isConnected() const { return false; }
    const char* getStatusLabel() const { return "OUT: QT PC"; }
    void writePcm(const void*, size_t) {}
};
#endif

#endif // USB_AUDIO_H
