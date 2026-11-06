#ifndef DISPLAY_DRIVER_QT_H
#define DISPLAY_DRIVER_QT_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QComboBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <algorithm>

enum TextDatum {
    MC_DATUM = 0,
    BC_DATUM = 1,
    ML_DATUM = 2,
    MR_DATUM = 3
};

class Waveshare7QtDisplay : public QWidget {
public:
    std::function<void(int, bool)> onPcKeyMidi;
    std::function<void(int)> onProfileChanged;

    static constexpr int TOOLBAR_H = 32;

    explicit Waveshare7QtDisplay(QWidget* parent = nullptr)
        : QWidget(parent), m_fb(800, 480, QImage::Format_RGB16) {
        setFocusPolicy(Qt::StrongFocus);
        m_fb.fill(Qt::black);

        m_toolbar = new QWidget(this);
        m_toolbar->setGeometry(0, 0, 800, TOOLBAR_H);
        m_toolbar->setStyleSheet("background-color: #111827; color: #00E5FF; font-weight: bold;");

        auto* hLayout = new QHBoxLayout(m_toolbar);
        hLayout->setContentsMargins(8, 2, 8, 2);
        auto* lbl = new QLabel("Screen Target Simulator:", m_toolbar);
        m_profileCombo = new QComboBox(m_toolbar);
        m_profileCombo->addItem("0: 800x480 (7\" Waveshare Touch + Piano)");
        m_profileCombo->addItem("1: 320x240 (2.8\" SPI ILI9341/ST7789 - No Visual Piano)");
        m_profileCombo->addItem("2: 128x64 (Monochrome LCD Keyboard Box Preview)");
        m_profileCombo->setFocusPolicy(Qt::NoFocus);
        m_profileCombo->setStyleSheet("background-color: #1F2937; color: #FFFFFF; padding: 2px 6px;");

        hLayout->addWidget(lbl);
        hLayout->addWidget(m_profileCombo, 1);

        QObject::connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx) {
            setScreenProfile(idx);
            if (onProfileChanged) onProfileChanged(idx);
        });

        setScreenProfile(0);
    }

    void setScreenProfile(int profileIdx) {
        m_profileIdx = profileIdx;
        if (profileIdx == 0) {
            m_scrW = 800; m_scrH = 480; m_monoMode = false;
            m_uiFont = QFont("Segoe UI", 9, QFont::Bold);
        } else if (profileIdx == 1) {
            m_scrW = 320; m_scrH = 240; m_monoMode = false;
            m_uiFont = QFont("Segoe UI", 6, QFont::Bold);
        } else {
            m_scrW = 128; m_scrH = 64; m_monoMode = true;
            m_uiFont = QFont("Consolas", 6, QFont::Normal);
        }
        m_uiFont.setFamilies({"Segoe UI", "Nirmala UI", "Noto Sans Devanagari", "Lohit Devanagari", "sans-serif"});

        m_fb = QImage(m_scrW, m_scrH, QImage::Format_RGB16);
        m_fb.fill(Qt::black);

        int winW = std::max(m_scrW, 480);
        int winH = m_scrH + TOOLBAR_H;
        m_toolbar->setGeometry(0, 0, winW, TOOLBAR_H);
        setFixedSize(winW, winH);
        update();
    }

    int screenWidth() const { return m_scrW; }
    int screenHeight() const { return m_scrH; }
    int currentProfile() const { return m_profileIdx; }

    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    QColor from565(uint16_t c) const {
        int r = ((c >> 11) & 0x1F) * 255 / 31;
        int g = ((c >> 5)  & 0x3F) * 255 / 63;
        int b = (c         & 0x1F) * 255 / 31;
        if (m_monoMode) {
            int luma = (r * 77 + g * 150 + b * 29) >> 8;
            return (luma > 65) ? QColor(180, 255, 255) : QColor(10, 18, 24);
        }
        return QColor(r, g, b);
    }

    void fillScreen(uint16_t col) {
        QPainter p(&m_fb);
        p.fillRect(0, 0, m_scrW, m_scrH, from565(col));
        update();
    }

    void fillRect(int x, int y, int w, int h, uint16_t col) {
        QPainter p(&m_fb);
        p.fillRect(x, y, w, h, from565(col));
        update();
    }

    void drawRect(int x, int y, int w, int h, uint16_t col) {
        QPainter p(&m_fb);
        p.setPen(from565(col));
        p.setBrush(Qt::NoBrush);
        p.drawRect(x, y, w - 1, h - 1);
        update();
    }

    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t col) {
        QPainter p(&m_fb);
        p.setRenderHint(QPainter::Antialiasing, !m_monoMode);
        p.setPen(Qt::NoPen);
        p.setBrush(from565(col));
        p.drawRoundedRect(x, y, w, h, r, r);
        update();
    }

    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t col) {
        QPainter p(&m_fb);
        p.setRenderHint(QPainter::Antialiasing, !m_monoMode);
        p.setPen(from565(col));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(x, y, w, h, r, r);
        update();
    }

    void setTextColor(uint16_t fg, uint16_t bg = 0) {
        Q_UNUSED(bg);
        m_textCol = from565(fg);
    }

    void setTextDatum(TextDatum datum) {
        m_datum = datum;
    }

    void drawString(const char* text, int x, int y) {
        if (!text) return;
        QPainter p(&m_fb);
        p.setRenderHint(QPainter::TextAntialiasing, !m_monoMode);
        p.setPen(m_textCol);
        p.setFont(m_uiFont);

        QString s = QString::fromUtf8(text);
        QFontMetrics fm(p.font());
        int tw = fm.horizontalAdvance(s);
        int th = fm.height();

        int drawX = x;
        int drawY = y;
        if (m_datum == MC_DATUM) {
            drawX = x - tw / 2;
            drawY = y + fm.ascent() - th / 2;
        } else if (m_datum == BC_DATUM) {
            drawX = x - tw / 2;
            drawY = y - 2;
        } else if (m_datum == ML_DATUM) {
            drawX = x;
            drawY = y + fm.ascent() - th / 2;
        } else if (m_datum == MR_DATUM) {
            drawX = x - tw;
            drawY = y + fm.ascent() - th / 2;
        }
        p.drawText(drawX, drawY, s);
        update();
    }

    bool getTouch(uint16_t* tx, uint16_t* ty) const {
        if (!m_touching) return false;
        *tx = static_cast<uint16_t>(std::clamp(m_touchX, 0, m_scrW - 1));
        *ty = static_cast<uint16_t>(std::clamp(m_touchY, 0, m_scrH - 1));
        return true;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(17, 24, 39));
        int offsetX = (width() - m_scrW) / 2;
        p.drawImage(offsetX, TOOLBAR_H, m_fb);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        int offsetX = (width() - m_scrW) / 2;
        int py = ev->pos().y() - TOOLBAR_H;
        int px = ev->pos().x() - offsetX;
        if (ev->button() == Qt::LeftButton && py >= 0 && py < m_scrH && px >= 0 && px < m_scrW) {
            m_touching = true;
            m_touchX = px;
            m_touchY = py;
        }
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (m_touching) {
            int offsetX = (width() - m_scrW) / 2;
            m_touchX = ev->pos().x() - offsetX;
            m_touchY = ev->pos().y() - TOOLBAR_H;
        }
    }

    void mouseReleaseEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            m_touching = false;
        }
    }

    void keyPressEvent(QKeyEvent* ev) override {
        if (ev->isAutoRepeat()) return;
        int midi = pcKeyToMidi(ev->key());
        if (midi >= 0 && onPcKeyMidi) onPcKeyMidi(midi, true);
    }

    void keyReleaseEvent(QKeyEvent* ev) override {
        if (ev->isAutoRepeat()) return;
        int midi = pcKeyToMidi(ev->key());
        if (midi >= 0 && onPcKeyMidi) onPcKeyMidi(midi, false);
    }

private:
    static int pcKeyToMidi(int key) {
        switch (key) {
        case Qt::Key_Z: return 60;
        case Qt::Key_S: return 61;
        case Qt::Key_X: return 62;
        case Qt::Key_D: return 63;
        case Qt::Key_C: return 64;
        case Qt::Key_V: return 65;
        case Qt::Key_G: return 66;
        case Qt::Key_B: return 67;
        case Qt::Key_H: return 68;
        case Qt::Key_N: return 69;
        case Qt::Key_J: return 70;
        case Qt::Key_M: return 71;
        case Qt::Key_Comma: return 72;
        default: return -1;
        }
    }

    QWidget* m_toolbar = nullptr;
    QComboBox* m_profileCombo = nullptr;
    int m_profileIdx = 0;
    int m_scrW = 800;
    int m_scrH = 480;
    bool m_monoMode = false;
    QImage m_fb;
    QFont m_uiFont;
    QColor m_textCol = Qt::white;
    TextDatum m_datum = MC_DATUM;
    bool m_touching = false;
    int m_touchX = 0;
    int m_touchY = 0;
};

#endif // DISPLAY_DRIVER_QT_H
