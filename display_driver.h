#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <algorithm>
#include <cstring>
#include <cstdint>

#ifndef SCREEN_PROFILE
#define SCREEN_PROFILE 0
#endif

#if SCREEN_PROFILE == 0
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <Wire.h>
#include "buffered_rgb_bus.h"
#include <driver/i2c.h>
#endif

// Default SPI pins for ESP32-S3-DevKitC-1 N16R8 + 2.8" SPI Touch LCD (ILI9341 / ST7789)
#ifndef TFT_SPI_MOSI
#define TFT_SPI_MOSI 11
#define TFT_SPI_MISO 13
#define TFT_SPI_SCLK 12
#define TFT_SPI_CS   10
#define TFT_SPI_DC   4
#define TFT_SPI_RST  5
#define TFT_SPI_BL   46
#define TOUCH_SPI_CS 14
#define TOUCH_SPI_IRQ 21
#endif

inline void initWaveshare7InchBacklightAndTouch() {
#if SCREEN_PROFILE == 0
    Wire.begin(8, 9, 400000);
    Wire.beginTransmission(0x24);
    Wire.write(0x01);
    Wire.endTransmission();

    Wire.beginTransmission(0x38);
    Wire.write(0x0E);
    Wire.endTransmission();
    delay(50);
#else
    if (TFT_SPI_BL >= 0) {
        pinMode(TFT_SPI_BL, OUTPUT);
        digitalWrite(TFT_SPI_BL, HIGH);
    }
#endif
}

namespace EmbeddedDevanagari {

struct DevGlyph {
    uint16_t code;
    uint8_t  width;
    bool     hasRightStem;
    uint8_t  rows[10];
};

static constexpr uint16_t LIG_TRA  = 0xE001;
static constexpr uint16_t LIG_SHRA = 0xE002;
static constexpr uint16_t LIG_DYA  = 0xE003;
static constexpr uint16_t LIG_KSHA = 0xE004;

static const DevGlyph kGlyphs[] = {
    {0x0905, 8, true,  {0x1F, 0x72, 0x8A, 0x32, 0x1E, 0x8A, 0x8A, 0x72, 0x02, 0x00}},
    {0x0907, 8, false, {0xFF, 0x10, 0x78, 0x80, 0x78, 0x04, 0x78, 0x30, 0x18, 0x04}},
    {0x0909, 8, false, {0xFF, 0x10, 0x78, 0x04, 0x38, 0x04, 0x84, 0x84, 0x78, 0x00}},
    {0x090B, 8, true,  {0xFF, 0x12, 0x5A, 0x36, 0x1E, 0x52, 0x32, 0x12, 0x0E, 0x00}},
    {0x090F, 8, false, {0xFF, 0x44, 0x44, 0x44, 0x68, 0x50, 0x20, 0x18, 0x04, 0x00}},
    {0x0915, 8, false, {0xFF, 0x10, 0x7C, 0x92, 0x92, 0x7C, 0x12, 0x12, 0x10, 0x00}},
    {0x0916, 8, true,  {0xFF, 0x82, 0x42, 0xBC, 0x4A, 0x4A, 0x8E, 0x72, 0x02, 0x00}},
    {0x0917, 8, true,  {0xFF, 0x44, 0x44, 0x44, 0xC4, 0xC4, 0x04, 0x04, 0x04, 0x00}},
    {0x0918, 8, true,  {0xFF, 0x74, 0x84, 0x74, 0x84, 0x84, 0x7C, 0x04, 0x04, 0x00}},
    {0x0919, 8, false, {0xFF, 0x10, 0x78, 0x80, 0x78, 0x04, 0x84, 0x78, 0x00, 0x00}},
    {0x091A, 8, true,  {0xFF, 0x04, 0xFC, 0x44, 0x44, 0x44, 0x3C, 0x04, 0x04, 0x00}},
    {0x091B, 8, false, {0xFF, 0x48, 0xB8, 0x48, 0x78, 0x84, 0x84, 0x7C, 0x04, 0x00}},
    {0x091C, 8, true,  {0xFF, 0x04, 0x84, 0xBC, 0xA4, 0xA4, 0x74, 0x04, 0x04, 0x00}},
    {0x091D, 8, true,  {0xFF, 0x24, 0x74, 0x84, 0x7C, 0x24, 0x54, 0x8C, 0x04, 0x00}},
    {0x091E, 8, true,  {0xFF, 0x04, 0x64, 0x9C, 0x84, 0x64, 0x04, 0x04, 0x04, 0x00}},
    {0x091F, 8, false, {0xFF, 0x10, 0x10, 0x78, 0x80, 0x80, 0x84, 0x78, 0x00, 0x00}},
    {0x0920, 8, false, {0xFF, 0x10, 0x38, 0x44, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00}},
    {0x0921, 8, false, {0xFF, 0x10, 0x78, 0x80, 0x78, 0x04, 0x84, 0x78, 0x00, 0x00}},
    {0x0922, 8, false, {0xFF, 0x10, 0x78, 0x80, 0x80, 0x98, 0x98, 0x70, 0x00, 0x00}},
    {0x0923, 8, true,  {0xFF, 0x94, 0x94, 0x94, 0x94, 0x64, 0x04, 0x04, 0x04, 0x00}},
    {0x0924, 8, true,  {0xFF, 0x04, 0x7C, 0x44, 0x44, 0x44, 0x84, 0x04, 0x04, 0x00}},
    {0x0925, 8, true,  {0xFF, 0x64, 0x94, 0x64, 0x44, 0x3C, 0x04, 0x04, 0x04, 0x00}},
    {0x0926, 8, false, {0xFF, 0x10, 0x78, 0x80, 0x80, 0x7C, 0x08, 0x04, 0x04, 0x00}},
    {0x0927, 8, true,  {0xFF, 0x64, 0x94, 0x64, 0x94, 0x7C, 0x04, 0x04, 0x04, 0x00}},
    {0x0928, 8, true,  {0xFF, 0x04, 0x04, 0xFC, 0x94, 0xEC, 0x04, 0x04, 0x04, 0x00}},
    {0x092A, 8, true,  {0xFF, 0x44, 0x44, 0x44, 0x44, 0x7C, 0x04, 0x04, 0x04, 0x00}},
    {0x092B, 8, false, {0xFF, 0x50, 0x50, 0x5C, 0x52, 0x72, 0x1C, 0x10, 0x10, 0x00}},
    {0x092C, 8, true,  {0xFF, 0x04, 0x7C, 0x94, 0xAC, 0x94, 0x7C, 0x04, 0x04, 0x00}},
    {0x092D, 8, true,  {0xFF, 0xC4, 0x44, 0xFC, 0x44, 0xC4, 0x04, 0x04, 0x04, 0x00}},
    {0x092E, 8, true,  {0xFF, 0x44, 0x44, 0xFC, 0xC4, 0x44, 0x04, 0x04, 0x04, 0x00}},
    {0x092F, 8, true,  {0xFF, 0x64, 0x24, 0x44, 0x84, 0x7C, 0x04, 0x04, 0x04, 0x00}},
    {0x0930, 8, false, {0xFF, 0x18, 0x18, 0x18, 0x78, 0x20, 0x10, 0x08, 0x04, 0x00}},
    {0x0932, 8, true,  {0xFF, 0x04, 0x6C, 0x94, 0x94, 0x84, 0x44, 0x04, 0x04, 0x00}},
    {0x0933, 8, false, {0xFF, 0x10, 0x6C, 0x92, 0x92, 0x6C, 0x44, 0x38, 0x00, 0x00}},
    {0x0935, 8, true,  {0xFF, 0x04, 0x7C, 0x84, 0x84, 0x7C, 0x04, 0x04, 0x04, 0x00}},
    {0x0936, 8, true,  {0xFF, 0x64, 0x94, 0x64, 0x24, 0x54, 0x8C, 0x04, 0x04, 0x00}},
    {0x0937, 8, true,  {0xFF, 0x44, 0x64, 0x54, 0x4C, 0x7C, 0x04, 0x04, 0x04, 0x00}},
    {0x0938, 8, true,  {0xFF, 0x44, 0x44, 0x7C, 0x24, 0x44, 0x84, 0x04, 0x04, 0x00}},
    {0x0939, 8, false, {0xFF, 0x10, 0x78, 0x40, 0x78, 0x84, 0x78, 0x40, 0x3C, 0x00}},
    {LIG_TRA,  8, true,  {0xFF, 0x04, 0x1C, 0x24, 0x7C, 0x24, 0x44, 0x84, 0x04, 0x00}},
    {LIG_SHRA, 8, true,  {0xFF, 0x64, 0x94, 0x7C, 0x14, 0x24, 0x4C, 0x84, 0x04, 0x00}},
    {LIG_DYA,  8, true,  {0xFF, 0x14, 0x74, 0x94, 0x7C, 0x84, 0x7C, 0x04, 0x04, 0x00}},
    {LIG_KSHA, 8, true,  {0xFF, 0x64, 0x94, 0x7C, 0x54, 0x74, 0x84, 0x04, 0x04, 0x00}},
    {0x0966, 6, false, {0x00, 0x38, 0x44, 0x82, 0x82, 0x82, 0x44, 0x38, 0x00, 0x00}},
    {0x0967, 6, false, {0x00, 0x30, 0x48, 0x30, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00}},
    {0x0968, 6, false, {0x00, 0x78, 0x04, 0x38, 0x40, 0x20, 0x10, 0x0C, 0x00, 0x00}},
    {0x0969, 6, false, {0x00, 0x78, 0x04, 0x38, 0x04, 0x44, 0x38, 0x08, 0x00, 0x00}},
    {0x096A, 6, false, {0x00, 0x84, 0x84, 0x48, 0x30, 0x48, 0x84, 0x78, 0x00, 0x00}},
    {0x096B, 6, false, {0x00, 0x84, 0x84, 0x84, 0x7C, 0x04, 0x04, 0x04, 0x00, 0x00}},
    {0x096C, 6, false, {0x00, 0x38, 0x40, 0x38, 0x40, 0x44, 0x38, 0x04, 0x00, 0x00}},
    {0x096D, 6, false, {0x00, 0x40, 0x40, 0x58, 0x64, 0x44, 0x38, 0x00, 0x00, 0x00}},
    {0x096E, 6, false, {0x00, 0x10, 0x20, 0x40, 0x80, 0x84, 0x78, 0x00, 0x00, 0x00}},
    {0x096F, 6, false, {0x00, 0x38, 0x44, 0x38, 0x10, 0x08, 0x04, 0x00, 0x00, 0x00}}
};

inline const DevGlyph* findGlyph(uint16_t cp) {
    for (const auto& g : kGlyphs) {
        if (g.code == cp) return &g;
    }
    return nullptr;
}

inline uint32_t nextUtf8(const char*& p) {
    if (!p || !*p) return 0;
    uint8_t c0 = static_cast<uint8_t>(*p++);
    if (c0 < 0x80) return c0;
    if ((c0 & 0xE0) == 0xC0) {
        uint8_t c1 = static_cast<uint8_t>(*p);
        if ((c1 & 0xC0) == 0x80) { p++; return ((c0 & 0x1F) << 6) | (c1 & 0x3F); }
        return c0;
    }
    if ((c0 & 0xF0) == 0xE0) {
        uint8_t c1 = static_cast<uint8_t>(p[0]);
        uint8_t c2 = static_cast<uint8_t>(p[1]);
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
            p += 2;
            return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        }
        return c0;
    }
    if ((c0 & 0xF8) == 0xF0) {
        uint8_t c1 = static_cast<uint8_t>(p[0]);
        uint8_t c2 = static_cast<uint8_t>(p[1]);
        uint8_t c3 = static_cast<uint8_t>(p[2]);
        if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
            p += 3;
            return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
        return c0;
    }
    return c0;
}

inline bool isDevBase(uint32_t cp) {
    return (cp >= 0x0905 && cp <= 0x0939) || (cp >= 0x0958 && cp <= 0x0961);
}

inline bool isDevModifier(uint32_t cp) {
    return (cp >= 0x0900 && cp <= 0x0903) ||
           (cp >= 0x093A && cp <= 0x0957) ||
           (cp >= 0x0962 && cp <= 0x0963);
}

inline bool isIconCode(uint32_t cp) {
    return cp == 0x25B6 || cp == 0x23F9 || cp == 0x23F8 ||
           cp == 0x1F310 || cp == 0x1F3B9 || cp == 0x1F3A4;
}

} // namespace EmbeddedDevanagari

class Waveshare7Display : public lgfx::LGFX_Device {
public:
#if SCREEN_PROFILE == 0
    BufferedRGBBus    _bus_instance;
    lgfx::Panel_RGB   _panel_instance;
    lgfx::Touch_GT911 _touch_instance;
    int32_t m_scrW = 800;
    int32_t m_scrH = 480;
#else
    lgfx::Bus_SPI       _bus_instance;
#if defined(USE_ST7789) && USE_ST7789
    lgfx::Panel_ST7789  _panel_instance;
#else
    lgfx::Panel_ILI9341 _panel_instance;
#endif
    lgfx::Touch_XPT2046 _touch_instance;
    int32_t m_scrW = 320;
    int32_t m_scrH = 240;
#endif

    Waveshare7Display() {
#if SCREEN_PROFILE == 0
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = 800;
            cfg.memory_height = 480;
            cfg.panel_width   = 800;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel_instance.config(cfg);

            auto cfg_detail = _panel_instance.config_detail();
            cfg_detail.use_psram = 1;
            _panel_instance.config_detail(cfg_detail);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            cfg.pin_d0  = GPIO_NUM_14;
            cfg.pin_d1  = GPIO_NUM_38;
            cfg.pin_d2  = GPIO_NUM_18;
            cfg.pin_d3  = GPIO_NUM_17;
            cfg.pin_d4  = GPIO_NUM_10;
            cfg.pin_d5  = GPIO_NUM_39;
            cfg.pin_d6  = GPIO_NUM_0;
            cfg.pin_d7  = GPIO_NUM_45;
            cfg.pin_d8  = GPIO_NUM_48;
            cfg.pin_d9  = GPIO_NUM_47;
            cfg.pin_d10 = GPIO_NUM_21;
            cfg.pin_d11 = GPIO_NUM_1;
            cfg.pin_d12 = GPIO_NUM_2;
            cfg.pin_d13 = GPIO_NUM_42;
            cfg.pin_d14 = GPIO_NUM_41;
            cfg.pin_d15 = GPIO_NUM_40;

            cfg.pin_henable = GPIO_NUM_5;
            cfg.pin_vsync   = GPIO_NUM_3;
            cfg.pin_hsync   = GPIO_NUM_46;
            cfg.pin_pclk    = GPIO_NUM_7;
            cfg.freq_write  = 16000000;

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 8;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 8;
            cfg.pclk_idle_high    = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 0;
            cfg.x_max      = 799;
            cfg.y_min      = 0;
            cfg.y_max      = 479;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.i2c_port   = I2C_NUM_0;
            cfg.pin_sda    = GPIO_NUM_8;
            cfg.pin_scl    = GPIO_NUM_9;
            cfg.pin_int    = GPIO_NUM_4;
            cfg.pin_rst    = -1;
            cfg.freq       = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
#else
        // 2.8" 320x240 SPI ILI9341 / ST7789 + XPT2046 Touch Configuration
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host   = SPI2_HOST;
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire  = false;
            cfg.use_lock   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk   = TFT_SPI_SCLK;
            cfg.pin_mosi   = TFT_SPI_MOSI;
            cfg.pin_miso   = TFT_SPI_MISO;
            cfg.pin_dc     = TFT_SPI_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs       = TFT_SPI_CS;
            cfg.pin_rst      = TFT_SPI_RST;
            cfg.pin_busy     = -1;
            cfg.panel_width  = 240;
            cfg.panel_height = 320;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 1; // Landscape 320x240
            cfg.readable     = true;
            cfg.invert       = false;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = true;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 300;
            cfg.x_max      = 3900;
            cfg.y_min      = 300;
            cfg.y_max      = 3900;
            cfg.pin_int    = TOUCH_SPI_IRQ;
            cfg.bus_shared = true;
            cfg.offset_rotation = 1;
            cfg.spi_host   = SPI2_HOST;
            cfg.freq       = 2500000;
            cfg.pin_sclk   = TFT_SPI_SCLK;
            cfg.pin_mosi   = TFT_SPI_MOSI;
            cfg.pin_miso   = TFT_SPI_MISO;
            cfg.pin_cs     = TOUCH_SPI_CS;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
#endif
        setPanel(&_panel_instance);
    }

    int screenWidth() const { return m_scrW; }
    int screenHeight() const { return m_scrH; }

    void setTextColor(uint16_t fg) {
        m_textFg = fg;
        lgfx::LGFX_Device::setTextColor(fg);
    }

    void setTextColor(uint16_t fg, uint16_t bg) {
        m_textFg = fg;
        lgfx::LGFX_Device::setTextColor(fg, bg);
    }

    void setTextDatum(uint8_t datum) {
        m_datum = datum;
        lgfx::LGFX_Device::setTextDatum(datum);
    }

    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
        if (w <= 0 || h <= 0) return;
#if SCREEN_PROFILE == 0
        const int32_t step = 8;
        for (int32_t curY = 0; curY < h; curY += step) {
            int32_t sliceH = std::min(step, h - curY);
            lgfx::LGFX_Device::fillRect(x, y + curY, w, sliceH, color);
            if (w * sliceH >= 1600) {
                delayMicroseconds(15);
            }
        }
#else
        lgfx::LGFX_Device::fillRect(x, y, w, h, color);
#endif
    }

    void fillScreen(uint16_t color) {
        fillRect(0, 0, m_scrW, m_scrH, color);
    }

    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
        if (w <= 0 || h <= 0) return;
#if SCREEN_PROFILE == 0
        if (h <= 16 || r <= 0) {
            lgfx::LGFX_Device::fillRoundRect(x, y, w, h, r, color);
            if (w * h >= 1600) delayMicroseconds(15);
            return;
        }
        lgfx::LGFX_Device::fillRoundRect(x, y, w, r * 2 + 2, r, color);
        delayMicroseconds(10);
        fillRect(x, y + r, w, h - 2 * r, color);
        lgfx::LGFX_Device::fillRoundRect(x, y + h - (r * 2 + 2), w, r * 2 + 2, r, color);
        delayMicroseconds(10);
#else
        lgfx::LGFX_Device::fillRoundRect(x, y, w, h, r, color);
#endif
    }

    size_t drawString(const char* text, int32_t x, int32_t y) {
        if (!text || !*text) return 0;

        bool hasUtf8 = false;
        for (const char* p = text; *p; ++p) {
            if (static_cast<uint8_t>(*p) >= 0x80) {
                hasUtf8 = true;
                break;
            }
        }

        if (!hasUtf8) {
            return lgfx::LGFX_Device::drawString(text, x, y);
        }

        int32_t totalW = renderOrMeasureUtf8(text, 0, 0, false);
        int32_t totalH = 16;

        int32_t drawX = x;
        int32_t drawY = y;

        if (m_datum == MC_DATUM || m_datum == TC_DATUM || m_datum == BC_DATUM) {
            drawX = x - totalW / 2;
        } else if (m_datum == MR_DATUM || m_datum == TR_DATUM || m_datum == BR_DATUM) {
            drawX = x - totalW;
        }

        if (m_datum == MC_DATUM || m_datum == ML_DATUM || m_datum == MR_DATUM) {
            drawY = y - totalH / 2;
        } else if (m_datum == BC_DATUM || m_datum == BL_DATUM || m_datum == BR_DATUM) {
            drawY = y - totalH;
        }

        renderOrMeasureUtf8(text, drawX, drawY, true);
        return static_cast<size_t>(totalW);
    }

    bool getTouch(uint16_t* tx, uint16_t* ty) {
        uint32_t now = millis();
        if (now - m_lastTouchMs >= 20) {
            m_lastTouchMs = now;
            m_cachedTouching = lgfx::LGFX_Device::getTouch(&m_cachedX, &m_cachedY);
        }
        if (m_cachedTouching) {
            *tx = m_cachedX;
            *ty = m_cachedY;
        }
        return m_cachedTouching;
    }

private:
    uint32_t m_lastTouchMs = 0;
    uint16_t m_cachedX = 0;
    uint16_t m_cachedY = 0;
    bool     m_cachedTouching = false;
    uint16_t m_textFg = 0xFFFF;
    uint8_t  m_datum = TL_DATUM;

    struct AksharaCluster {
        uint16_t halfCons[2] = {0, 0};
        int      numHalf = 0;
        uint16_t mainCode = 0;
        bool     hasReph = false;
        bool     hasRakar = false;
        bool     hasHalant = false;
        bool     hasNukta = false;
        bool     hasPreI = false;
        bool     hasPostII = false;
        bool     hasRightStemMatra = false;
        int      topECount = 0;
        bool     hasCandra = false;
        bool     hasAnusvara = false;
        bool     hasVisarga = false;
        int      bottomVowel = 0;
        bool     hasUdatta = false;
        bool     hasAnudatta = false;
    };

    void drawFgPixel(int32_t x, int32_t y) {
        if (x >= 0 && x < m_scrW && y >= 0 && y < m_scrH) {
            lgfx::LGFX_Device::drawPixel(x, y, m_textFg);
        }
    }

    void drawFgHLine(int32_t x, int32_t y, int32_t w) {
        if (y >= 0 && y < m_scrH && w > 0) {
            lgfx::LGFX_Device::drawFastHLine(x, y, w, m_textFg);
        }
    }

    void drawFgVLine(int32_t x, int32_t y, int32_t h) {
        if (x >= 0 && x < m_scrW && h > 0) {
            lgfx::LGFX_Device::drawFastVLine(x, y, h, m_textFg);
        }
    }

    void drawGlyphRows(const EmbeddedDevanagari::DevGlyph* g, int32_t x, int32_t y, int32_t clipW) {
        if (!g) return;
        for (int r = 0; r < 10; ++r) {
            uint8_t bits = g->rows[r];
            if (!bits) continue;
            for (int c = 0; c < clipW; ++c) {
                if (bits & (0x80 >> c)) {
                    drawFgPixel(x + c, y + 3 + r);
                }
            }
        }
    }

    int32_t drawOrMeasureIcon(uint32_t cp, int32_t x, int32_t y, bool draw) {
        if (!draw) return 12;
        if (cp == 0x25B6) {
            for (int i = 0; i < 5; ++i) drawFgVLine(x + 2 + i, y + 3 + i, 10 - i * 2);
        } else if (cp == 0x23F9) {
            lgfx::LGFX_Device::fillRect(x + 2, y + 4, 8, 8, m_textFg);
        } else if (cp == 0x23F8) {
            lgfx::LGFX_Device::fillRect(x + 2, y + 4, 3, 8, m_textFg);
            lgfx::LGFX_Device::fillRect(x + 7, y + 4, 3, 8, m_textFg);
        } else if (cp == 0x1F310) {
            lgfx::LGFX_Device::drawCircle(x + 5, y + 8, 5, m_textFg);
            drawFgHLine(x + 1, y + 8, 9);
            drawFgVLine(x + 5, y + 3, 11);
        } else if (cp == 0x1F3B9) {
            lgfx::LGFX_Device::drawRect(x + 1, y + 3, 10, 10, m_textFg);
            drawFgVLine(x + 4, y + 3, 10);
            drawFgVLine(x + 7, y + 3, 10);
            lgfx::LGFX_Device::fillRect(x + 3, y + 3, 2, 6, m_textFg);
            lgfx::LGFX_Device::fillRect(x + 6, y + 3, 2, 6, m_textFg);
        } else if (cp == 0x1F3A4) {
            lgfx::LGFX_Device::fillRoundRect(x + 4, y + 3, 4, 6, 2, m_textFg);
            drawFgHLine(x + 3, y + 10, 6);
            drawFgVLine(x + 5, y + 10, 3);
            drawFgHLine(x + 3, y + 13, 5);
        }
        return 12;
    }

    int32_t drawOrMeasureAkshara(const AksharaCluster& cl, int32_t x, int32_t y, bool draw) {
        using namespace EmbeddedDevanagari;
        const DevGlyph* mainG = findGlyph(cl.mainCode);
        int32_t mainW = mainG ? mainG->width : 7;
        int32_t curX = x;

        int32_t preStemX = curX + 1;
        if (cl.hasPreI) {
            if (draw) {
                drawFgHLine(curX, y + 3, 3);
                drawFgVLine(preStemX, y + 3, 9);
            }
            curX += 3;
        }

        for (int i = 0; i < cl.numHalf; ++i) {
            const DevGlyph* hg = findGlyph(cl.halfCons[i]);
            int32_t hw = (hg && hg->hasRightStem) ? 5 : 6;
            if (draw && hg) {
                drawGlyphRows(hg, curX, y, hw);
                if (!hg->hasRightStem) {
                    drawFgPixel(curX + 3, y + 12);
                    drawFgPixel(curX + 4, y + 13);
                }
            }
            curX += hw;
        }

        int32_t mainStartX = curX;
        int32_t mainStemX = mainStartX + mainW - 2;
        if (draw) {
            if (mainG) drawGlyphRows(mainG, mainStartX, y, mainW);
            else drawFgHLine(mainStartX, y + 3, mainW);

            if (cl.hasPreI) {
                int32_t archW = mainStemX - preStemX;
                if (archW > 1) {
                    drawFgPixel(preStemX, y + 2);
                    drawFgPixel(preStemX + 1, y + 1);
                    drawFgHLine(preStemX + 1, y, archW - 1);
                    drawFgPixel(mainStemX, y + 1);
                    drawFgPixel(mainStemX, y + 2);
                }
            }
            if (cl.hasRakar) {
                drawFgPixel(mainStemX - 1, y + 10);
                drawFgPixel(mainStemX - 2, y + 11);
                drawFgPixel(mainStemX - 3, y + 12);
            }
            if (cl.hasNukta) {
                drawFgPixel(mainStartX + 3, y + 13);
                drawFgPixel(mainStartX + 4, y + 13);
            }
        }
        curX += mainW;

        int32_t activeStemX = mainStemX;
        if (cl.hasRightStemMatra || cl.hasPostII) {
            int32_t postStemX = curX + 1;
            if (draw) {
                drawFgHLine(curX, y + 3, 3);
                drawFgVLine(postStemX, y + 3, 9);
                if (cl.hasPostII) {
                    int32_t archW = postStemX - mainStemX + 1;
                    drawFgPixel(mainStemX, y + 2);
                    drawFgPixel(mainStemX, y + 1);
                    drawFgHLine(mainStemX, y, archW);
                    drawFgPixel(postStemX, y + 1);
                    drawFgPixel(postStemX, y + 2);
                }
            }
            activeStemX = postStemX;
            curX += 3;
        }

        if (cl.hasVisarga) {
            if (draw) {
                drawFgPixel(curX + 1, y + 6);
                drawFgPixel(curX + 1, y + 10);
            }
            curX += 3;
        }

        if (draw) {
            if (cl.topECount >= 1) {
                drawFgPixel(activeStemX, y + 2);
                drawFgPixel(activeStemX - 1, y + 1);
                drawFgPixel(activeStemX - 2, y);
                drawFgPixel(activeStemX - 3, y);
            }
            if (cl.topECount >= 2) {
                drawFgPixel(activeStemX - 2, y + 2);
                drawFgPixel(activeStemX - 3, y + 1);
                drawFgPixel(activeStemX - 4, y);
            }
            if (cl.hasCandra) {
                drawFgPixel(activeStemX - 2, y + 1);
                drawFgHLine(activeStemX - 1, y + 2, 3);
                drawFgPixel(activeStemX + 2, y + 1);
            }
            if (cl.hasAnusvara) {
                int32_t dotX = (cl.topECount > 0) ? (activeStemX + 1) : activeStemX;
                drawFgPixel(dotX, y);
                drawFgPixel(dotX + 1, y);
                drawFgPixel(dotX, y + 1);
                drawFgPixel(dotX + 1, y + 1);
            }
            if (cl.hasReph) {
                drawFgPixel(activeStemX, y);
                drawFgPixel(activeStemX + 1, y);
                drawFgPixel(activeStemX - 1, y + 1);
                drawFgPixel(activeStemX, y + 2);
            }
            if (cl.hasUdatta) drawFgVLine(mainStartX + mainW / 2, y, 3);

            if (cl.bottomVowel == 1) {
                drawFgPixel(activeStemX, y + 12);
                drawFgPixel(activeStemX + 1, y + 13);
                drawFgHLine(activeStemX - 2, y + 14, 3);
                drawFgPixel(activeStemX - 3, y + 13);
            } else if (cl.bottomVowel == 2) {
                drawFgPixel(activeStemX, y + 12);
                drawFgPixel(activeStemX - 1, y + 13);
                drawFgHLine(activeStemX - 1, y + 14, 4);
                drawFgPixel(activeStemX + 3, y + 15);
            } else if (cl.bottomVowel == 3) {
                drawFgPixel(activeStemX, y + 12);
                drawFgPixel(activeStemX - 1, y + 13);
                drawFgHLine(activeStemX - 1, y + 14, 3);
            }

            if (cl.hasHalant) {
                drawFgPixel(activeStemX, y + 12);
                drawFgPixel(activeStemX + 1, y + 13);
                drawFgPixel(activeStemX + 2, y + 14);
            }
            if (cl.hasAnudatta) {
                drawFgHLine(mainStartX + 1, y + 14, mainW - 1);
                drawFgHLine(mainStartX + 1, y + 15, mainW - 1);
            }
        }

        return (curX - x) + 1;
    }

    int32_t renderOrMeasureUtf8(const char* text, int32_t startX, int32_t startY, bool draw) {
        using namespace EmbeddedDevanagari;
        int32_t curX = startX;
        const char* p = text;

        while (*p) {
            if (static_cast<uint8_t>(*p) < 0x80) {
                char asciiBuf[96];
                int len = 0;
                while (*p && static_cast<uint8_t>(*p) < 0x80 && len < 94) {
                    asciiBuf[len++] = *p++;
                }
                asciiBuf[len] = '\0';

                int32_t runW = lgfx::LGFX_Device::textWidth(asciiBuf);
                if (draw) {
                    uint8_t savedDatum = m_datum;
                    lgfx::LGFX_Device::setTextDatum(TL_DATUM);
                    lgfx::LGFX_Device::drawString(asciiBuf, curX, startY);
                    lgfx::LGFX_Device::setTextDatum(savedDatum);
                }
                curX += runW;
                continue;
            }

            uint32_t cp = nextUtf8(p);
            if (isIconCode(cp)) {
                curX += drawOrMeasureIcon(cp, curX, startY, draw);
                continue;
            }
            if (cp >= 0x0966 && cp <= 0x096F) {
                const DevGlyph* dg = findGlyph(static_cast<uint16_t>(cp));
                if (draw && dg) drawGlyphRows(dg, curX, startY, dg->width);
                curX += (dg ? dg->width : 6) + 1;
                continue;
            }
            if (cp == 0x0964 || cp == 0x0965) {
                if (draw) {
                    drawFgVLine(curX + 1, startY + 3, 10);
                    if (cp == 0x0965) drawFgVLine(curX + 3, startY + 3, 10);
                }
                curX += (cp == 0x0965) ? 5 : 3;
                continue;
            }

            if (isDevBase(cp)) {
                AksharaCluster cl;
                if (cp == 0x0906) { cl.mainCode = 0x0905; cl.hasRightStemMatra = true; }
                else if (cp == 0x0908) { cl.mainCode = 0x0907; cl.hasReph = true; }
                else if (cp == 0x090A) { cl.mainCode = 0x0909; cl.bottomVowel = 2; }
                else if (cp == 0x0910) { cl.mainCode = 0x090F; cl.topECount = 1; }
                else if (cp == 0x0911) { cl.mainCode = 0x0905; cl.hasRightStemMatra = true; cl.hasCandra = true; }
                else if (cp == 0x0913) { cl.mainCode = 0x0905; cl.hasRightStemMatra = true; cl.topECount = 1; }
                else if (cp == 0x0914) { cl.mainCode = 0x0905; cl.hasRightStemMatra = true; cl.topECount = 2; }
                else cl.mainCode = static_cast<uint16_t>(cp);

                while (*p) {
                    const char* peek1 = p;
                    uint32_t nextCp = nextUtf8(peek1);
                    if (nextCp == 0x093C) { cl.hasNukta = true; p = peek1; continue; }
                    if (nextCp == 0x094D) {
                        p = peek1;
                        const char* peek2 = p;
                        uint32_t c2 = nextUtf8(peek2);
                        if (c2 >= 0x0915 && c2 <= 0x0939) {
                            p = peek2;
                            if (cl.mainCode == 0x0930 && cl.numHalf == 0) {
                                cl.hasReph = true;
                                cl.mainCode = static_cast<uint16_t>(c2);
                            } else if (cl.mainCode == 0x0924 && c2 == 0x0930) cl.mainCode = LIG_TRA;
                            else if (cl.mainCode == 0x0936 && c2 == 0x0930) cl.mainCode = LIG_SHRA;
                            else if (cl.mainCode == 0x0926 && c2 == 0x092F) cl.mainCode = LIG_DYA;
                            else if (cl.mainCode == 0x0915 && c2 == 0x0937) cl.mainCode = LIG_KSHA;
                            else if (c2 == 0x0930) cl.hasRakar = true;
                            else {
                                if (cl.numHalf < 2) cl.halfCons[cl.numHalf++] = cl.mainCode;
                                cl.mainCode = static_cast<uint16_t>(c2);
                            }
                            continue;
                        } else {
                            cl.hasHalant = true;
                            continue;
                        }
                    }
                    if (isDevModifier(nextCp)) {
                        p = peek1;
                        switch (nextCp) {
                            case 0x0901: cl.hasCandra = true; cl.hasAnusvara = true; break;
                            case 0x0902: cl.hasAnusvara = true; break;
                            case 0x0903: cl.hasVisarga = true; break;
                            case 0x093E: cl.hasRightStemMatra = true; break;
                            case 0x093F: cl.hasPreI = true; break;
                            case 0x0940: cl.hasPostII = true; break;
                            case 0x0941: cl.bottomVowel = 1; break;
                            case 0x0942: cl.bottomVowel = 2; break;
                            case 0x0943: cl.bottomVowel = 3; break;
                            case 0x0945: cl.hasCandra = true; break;
                            case 0x0947: cl.topECount = std::max(cl.topECount, 1); break;
                            case 0x0948: cl.topECount = 2; break;
                            case 0x0949: cl.hasRightStemMatra = true; cl.hasCandra = true; break;
                            case 0x094B: cl.hasRightStemMatra = true; cl.topECount = std::max(cl.topECount, 1); break;
                            case 0x094C: cl.hasRightStemMatra = true; cl.topECount = 2; break;
                            case 0x0951: cl.hasUdatta = true; break;
                            case 0x0952: cl.hasAnudatta = true; break;
                            default: break;
                        }
                        continue;
                    }
                    break;
                }

                int32_t w = drawOrMeasureAkshara(cl, curX, startY, draw);
                const char* nextP = p;
                uint32_t upcoming = nextUtf8(nextP);
                if (draw && isDevBase(upcoming)) {
                    drawFgHLine(curX + w - 1, startY + 3, 2);
                }
                curX += w;
                continue;
            }
        }
        return curX - startX;
    }
};

#endif // DISPLAY_DRIVER_H
