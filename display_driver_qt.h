#ifndef DISPLAY_DRIVER_QT_H
#define DISPLAY_DRIVER_QT_H

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QStringList>
#include <cstdint>
#include <functional>

enum TextDatum {
    MC_DATUM = 0,
    BC_DATUM = 1,
    ML_DATUM = 2,
    MR_DATUM = 3
};

class Waveshare7QtDisplay : public QWidget {
public:
    std::function<void(int, bool)> onPcKeyMidi;

    explicit Waveshare7QtDisplay(QWidget* parent = nullptr)
        : QWidget(parent), m_fb(800, 480, QImage::Format_RGB16) {
        setFixedSize(800, 480);
        setFocusPolicy(Qt::StrongFocus);
        m_fb.fill(Qt::black);

        m_uiFont = QFont("Segoe UI", 9, QFont::Bold);
        m_uiFont.setFamilies({"Segoe UI", "Nirmala UI", "Noto Sans Devanagari", "Lohit Devanagari", "sans-serif"});
    }

    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    static QColor from565(uint16_t c) {
        int r = ((c >> 11) & 0x1F) * 255 / 31;
        int g = ((c >> 5)  & 0x3F) * 255 / 63;
        int b = (c         & 0x1F) * 255 / 31;
        return QColor(r, g, b);
    }

    void fillScreen(uint16_t col) {
        QPainter p(&m_fb);
        p.fillRect(0, 0, 800, 480, from565(col));
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
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(from565(col));
        p.drawRoundedRect(x, y, w, h, r, r);
        update();
    }

    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t col) {
        QPainter p(&m_fb);
        p.setRenderHint(QPainter::Antialiasing);
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
        p.setRenderHint(QPainter::TextAntialiasing);
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
        *tx = static_cast<uint16_t>(std::clamp(m_touchX, 0, 799));
        *ty = static_cast<uint16_t>(std::clamp(m_touchY, 0, 479));
        return true;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.drawImage(0, 0, m_fb);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            m_touching = true;
            m_touchX = ev->pos().x();
            m_touchY = ev->pos().y();
        }
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (m_touching) {
            m_touchX = ev->pos().x();
            m_touchY = ev->pos().y();
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

    QImage m_fb;
    QFont m_uiFont;
    QColor m_textCol = Qt::white;
    TextDatum m_datum = MC_DATUM;
    bool m_touching = false;
    int m_touchX = 0;
    int m_touchY = 0;
};

#endif // DISPLAY_DRIVER_QT_H
