#pragma once
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>

class BufferedRGBBus : public lgfx::Bus_RGB {
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint8_t *frame_ = nullptr;
public:
    bool init() override {
        const auto &c = config();
        esp_lcd_rgb_panel_config_t p = {};
        p.clk_src = LCD_CLK_SRC_DEFAULT;
        p.timings.pclk_hz = c.freq_write;
        p.timings.h_res = 800; p.timings.v_res = 480;
        p.timings.hsync_pulse_width = c.hsync_pulse_width;
        p.timings.hsync_back_porch = c.hsync_back_porch;
        p.timings.hsync_front_porch = c.hsync_front_porch;
        p.timings.vsync_pulse_width = c.vsync_pulse_width;
        p.timings.vsync_back_porch = c.vsync_back_porch;
        p.timings.vsync_front_porch = c.vsync_front_porch;
        p.timings.flags.hsync_idle_low = c.hsync_polarity;
        p.timings.flags.vsync_idle_low = c.vsync_polarity;
        p.timings.flags.pclk_active_neg = c.pclk_active_neg;
        p.timings.flags.pclk_idle_high = c.pclk_idle_high;
        p.data_width = 16;
        p.bits_per_pixel = 16;
        p.bounce_buffer_size_px = 800 * 10;
        p.hsync_gpio_num = c.pin_hsync; p.vsync_gpio_num = c.pin_vsync;
        p.de_gpio_num = c.pin_henable; p.pclk_gpio_num = c.pin_pclk;
        p.disp_gpio_num = -1;
        // LovyanGFX's RGB565 framebuffer uses the opposite byte order to the
        // ESP LCD driver; swap the two 8-bit pin groups to preserve colors.
        for (int i = 0; i < 16; ++i) p.data_gpio_nums[i ^ 8] = c.pin_data[i];
        p.flags.fb_in_psram = 1;
        if (esp_lcd_new_rgb_panel(&p, &panel_) != ESP_OK) return false;
        void *fb = nullptr;
        if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 1, &fb) != ESP_OK || !fb ||
            esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK) {
            esp_lcd_panel_del(panel_); panel_ = nullptr; return false;
        }
        frame_ = static_cast<uint8_t *>(fb);
        return true;
    }
    uint8_t *getDMABuffer(uint32_t) override { return frame_; }
    void release() override {
        if (panel_) esp_lcd_panel_del(panel_);
        panel_ = nullptr; frame_ = nullptr;
    }
};
