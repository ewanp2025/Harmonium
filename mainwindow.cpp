#include "mainwindow.h"
#include "projectio.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

static const char* WESTERN_NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Standard Swara Dropdown Choices
static const QStringList STANDARD_SWARA_LIST = {
    "-", ".",
    "'P", "'d", "'D", "'n", "'N",
    "S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N",
    "S'", "r'", "R'", "g'", "G'", "m'", "P'",
    "S R", "R g", "R G", "g R", "G m", "m P", "P d", "d P", "d n", "n S'", "S 'n", "'n S"
};

// Standard Tala Bol Dropdown Choices
static const QStringList STANDARD_BOL_LIST = {
    "-", "Dha", "Dhin", "Ge", "Ka", "Na", "Ta", "Ti", "Re",
    "Ti Re", "Re Ti", "Ta Dhin", "Ta Ti", "Ti Ka", "Ta Ge", "Dha Ge", "Dhin Na", "Ka Ta"
};

static const QStringList STANDARD_KARTAL_LIST = {
    "-", "Kartal Open", "Kartal Closed", "Closed+Closed", "Manjira", "Ghanta", "Kansa", "Damaru"
};

static bool isBlackKey(int midi) {
    int n = midi % 12;
    return (n == 1 || n == 3 || n == 6 || n == 8 || n == 10);
}

TouchPianoWidget::TouchPianoWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(85);
}

void TouchPianoWidget::setSaSemitone(int sa) {
    m_saSemitone = (sa % 12 + 12) % 12;
    update();
}

void TouchPianoWidget::setActiveNotes(const std::set<int>& notes) {
    m_activeNotes = notes;
    update();
}

void TouchPianoWidget::setLanguage(int langIdx) {
    const auto& langs = I18n::getLanguages();
    if (langIdx >= 0 && langIdx < static_cast<int>(langs.size())) {
        m_langIdx = langIdx;
        update();
    }
}

int TouchPianoWidget::hitTestMidi(const QPoint& pos) const {
    int whiteCount = 0;
    for (int i = 0; i < m_numKeys; ++i) {
        if (!isBlackKey(m_startMidi + i)) whiteCount++;
    }
    if (whiteCount == 0) return -1;
    double whiteW = static_cast<double>(width()) / whiteCount;
    int blackH = static_cast<int>(height() * 0.60);

    if (pos.y() <= blackH) {
        int wIdx = 0;
        for (int i = 0; i < m_numKeys; ++i) {
            int midi = m_startMidi + i;
            if (!isBlackKey(midi)) {
                wIdx++;
            } else {
                double x = (wIdx - 0.33) * whiteW;
                QRectF r(x, 0, whiteW * 0.66, blackH);
                if (r.contains(pos)) return midi;
            }
        }
    }

    int targetWhite = static_cast<int>(pos.x() / whiteW);
    int wIdx = 0;
    for (int i = 0; i < m_numKeys; ++i) {
        int midi = m_startMidi + i;
        if (!isBlackKey(midi)) {
            if (wIdx == targetWhite) return midi;
            wIdx++;
        }
    }
    return -1;
}

void TouchPianoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const auto& lang = I18n::getLanguages()[m_langIdx];

    int whiteCount = 0;
    for (int i = 0; i < m_numKeys; ++i) {
        if (!isBlackKey(m_startMidi + i)) whiteCount++;
    }
    double whiteW = static_cast<double>(width()) / std::max(1, whiteCount);
    int blackH = static_cast<int>(height() * 0.60);

    int wIdx = 0;
    for (int i = 0; i < m_numKeys; ++i) {
        int midi = m_startMidi + i;
        if (isBlackKey(midi)) continue;

        QRectF r(wIdx * whiteW, 0, whiteW - 1, height() - 1);
        bool active = m_activeNotes.count(midi) > 0;
        bool isSa = ((midi % 12) == m_saSemitone);

        p.setBrush(active ? QColor("#00e5ff") : (isSa ? QColor("#fff3cd") : QColor("#f5f5f5")));
        p.setPen(QColor("#333"));
        p.drawRect(r);

        int rel = (midi - m_saSemitone + 120) % 12;
        p.setPen(QColor("#111"));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(r.adjusted(0, 0, 0, -3), Qt::AlignBottom | Qt::AlignHCenter,
                   QString("%1\n%2").arg(lang.swaraNames[rel], WESTERN_NOTES[midi % 12]));
        wIdx++;
    }

    wIdx = 0;
    for (int i = 0; i < m_numKeys; ++i) {
        int midi = m_startMidi + i;
        if (!isBlackKey(midi)) {
            wIdx++;
            continue;
        }
        QRectF r((wIdx - 0.33) * whiteW, 0, whiteW * 0.66, blackH);
        bool active = m_activeNotes.count(midi) > 0;
        p.setBrush(active ? QColor("#ff9800") : QColor("#1b1b1b"));
        p.setPen(QColor("#000"));
        p.drawRect(r);

        int rel = (midi - m_saSemitone + 120) % 12;
        p.setPen(QColor("#ddd"));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(r.adjusted(0, 0, 0, -2), Qt::AlignBottom | Qt::AlignHCenter, lang.swaraNames[rel]);
    }
}

void TouchPianoWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    int midi = hitTestMidi(event->pos());
    if (midi >= 0) {
        m_mouseHeldMidi = midi;
        emit notePressed(midi, 105);
    }
}

void TouchPianoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int midi = hitTestMidi(event->pos());
    if (midi != m_mouseHeldMidi) {
        if (m_mouseHeldMidi >= 0) emit noteReleased(m_mouseHeldMidi);
        m_mouseHeldMidi = midi;
        if (m_mouseHeldMidi >= 0) emit notePressed(m_mouseHeldMidi, 105);
    }
}

void TouchPianoWidget::mouseReleaseEvent(QMouseEvent*) {
    if (m_mouseHeldMidi >= 0) {
        emit noteReleased(m_mouseHeldMidi);
        m_mouseHeldMidi = -1;
    }
}

const I18n::LangPack& MainWindow::L() const {
    const auto& langs = I18n::getLanguages();
    int idx = std::clamp(m_langIdx, 0, static_cast<int>(langs.size()) - 1);
    return langs[idx];
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(L().windowTitle);
    resize(800, 480);

    m_engine = new SynthEngine(this);
    setupUI();
    rebuildTrackAndTalaControls();
    retranslateUi();

    m_midiReader = new MidiReader([this](int note, int vel, bool on) {
        QMetaObject::invokeMethod(this, [this, note, vel, on]() {
            if (on) {
                m_activeNotes.insert(note);
                m_engine->noteOn(note, vel);
            } else {
                m_activeNotes.erase(note);
                m_engine->noteOff(note);
            }
            m_pianoWidget->setActiveNotes(m_activeNotes);
        }, Qt::QueuedConnection);
    }, this);

    connect(m_midiReader, &MidiReader::midiStatusChanged, this, [this](const QString& status) {
        m_lastRawMidiStatus = status;
        updateMidiStatusLabel();
    });
    m_midiReader->start();

    m_uiTimer = new QTimer(this);
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::onUiTick);
    m_uiTimer->start(50);
}

MainWindow::~MainWindow() {
    if (m_midiReader) {
        m_midiReader->stopReader();
        m_midiReader->wait();
    }
    m_engine->stopPlayback();
}

void MainWindow::setupUI() {
    m_tabs = new QTabWidget(this);
    setCentralWidget(m_tabs);
    m_tabs->setStyleSheet(
        "QWidget { background-color: #16191e; color: #f0f0f0; "
        "font-family: 'Segoe UI', 'Nirmala UI', 'Noto Sans Devanagari', 'Lohit Devanagari', sans-serif; font-size: 11px; }"
        "QTabWidget::pane { border: 1px solid #3a3f4b; }"
        "QTabBar::tab { background: #252830; color: #ccc; padding: 6px 14px; font-weight: bold; border: 1px solid #3a3f4b; }"
        "QTabBar::tab:selected { background: #00695c; color: #ffffff; border-bottom: 2px solid #00e5ff; }"
        "QGroupBox { border: 1px solid #3a3f4b; border-radius: 5px; margin-top: 6px; padding: 3px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #ffca28; }"
        "QPushButton { background-color: #2b303c; border: 1px solid #555; border-radius: 4px; padding: 3px 6px; font-weight: bold; min-height: 21px; }"
        "QPushButton:pressed { background-color: #00acc1; }"
        "QSpinBox, QDoubleSpinBox, QComboBox, QLineEdit { background-color: #252830; border: 1px solid #555; border-radius: 4px; padding: 2px; min-height: 21px; }"
        );

    // Language Selector in the Top-Right Corner of the Tab Bar
    m_cmbLangSelector = new QComboBox(m_tabs);
    m_cmbLangSelector->setStyleSheet(
        "background-color: #1e293b; color: #00e5ff; font-weight: bold; border: 1px solid #00acc1; padding: 2px 8px; margin: 2px;"
        );
    const auto& langs = I18n::getLanguages();
    for (size_t i = 0; i < langs.size(); ++i) {
        m_cmbLangSelector->addItem(langs[i].menuLabel, static_cast<int>(i));
    }
    connect(m_cmbLangSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLanguageChanged);
    m_tabs->setCornerWidget(m_cmbLangSelector, Qt::TopRightCorner);

    m_tabs->addTab(createMainPerformanceTab(), L().tabLiveBox);
    m_tabs->addTab(createHarmoniumEditorTab(), L().tabHarmEditor);
    m_tabs->addTab(createTalaEditorTab(), L().tabTalaEditor);
}

void MainWindow::onLanguageChanged(int index) {
    const auto& langs = I18n::getLanguages();
    if (index < 0 || index >= static_cast<int>(langs.size())) return;
    m_langIdx = index;
    m_pianoWidget->setLanguage(m_langIdx);
    retranslateUi();
}

void MainWindow::retranslateUi() {
    const auto& lang = L();
    setWindowTitle(lang.windowTitle);

    m_tabs->setTabText(0, lang.tabLiveBox);
    m_tabs->setTabText(1, lang.tabHarmEditor);
    m_tabs->setTabText(2, lang.tabTalaEditor);

    // Tab 1: Top Transport Bar
    m_btnStartStop->setText(m_engine->isPlaying() ? lang.btnStop : lang.btnStart);
    m_btnPause->setText(m_engine->isPaused() ? lang.btnResume : lang.btnPause);
    m_chkCountdown->setText(lang.chkCountdown);
    m_lblTopTala->setText(lang.lblTala);
    m_lblTopSong->setText(lang.lblSong);
    m_btnLoad->setText(lang.btnLoad);
    m_btnSave->setText(lang.btnSave);

    // Tab 1: Raga Scale & Drone Row
    m_grpTuner->setTitle(lang.grpTunerTitle);
    m_cmbSaNote->blockSignals(true);
    int curSaIdx = m_cmbSaNote->currentIndex();
    for (int i = 0; i < 12 && i < m_cmbSaNote->count(); ++i) {
        m_cmbSaNote->setItemText(i, lang.saKeyPrefix.arg(lang.indianKeyLabels[i]));
    }
    m_cmbSaNote->setCurrentIndex(std::max(0, curSaIdx));
    m_cmbSaNote->blockSignals(false);

    m_btnDroneToggle->setText(m_engine->isDroneEnabled() ? lang.btnDroneOn : lang.btnDroneOff);

    m_cmbDronePreset->blockSignals(true);
    m_cmbDronePreset->setItemText(0, lang.dronePresetBoth);
    m_cmbDronePreset->setItemText(1, lang.dronePresetShruti);
    m_cmbDronePreset->setItemText(2, lang.dronePresetTanpura);
    m_cmbDronePreset->blockSignals(false);

    m_cmbDroneSwara->blockSignals(true);
    m_cmbDroneSwara->setItemText(0, lang.droneSurSaPa);
    m_cmbDroneSwara->setItemText(1, lang.droneSurSaMa);
    m_cmbDroneSwara->setItemText(2, lang.droneSurSaNi);
    m_cmbDroneSwara->setItemText(3, lang.droneSurSaOnly);
    m_cmbDroneSwara->blockSignals(false);

    // Tab 1: Tracks & Raga Source Combo
    m_grpTracks->setTitle(lang.grpTracksTitle);
    MmpSong song = m_engine->getSongSnapshot();

    m_cmbRagaSource->blockSignals(true);
    if (m_cmbRagaSource->count() >= 2) {
        m_cmbRagaSource->setItemText(0, lang.showAny);
        m_cmbRagaSource->setItemText(1, lang.showKeys);
    }
    for (size_t i = 0; i < song.tracks.size(); ++i) {
        QString dispName = lang.translateTrackName(song.tracks[i].name);
        if (static_cast<int>(i + 2) < m_cmbRagaSource->count()) {
            m_cmbRagaSource->setItemText(static_cast<int>(i + 2), lang.showTrkPrefix + dispName);
        }
        if (i < m_trackButtons.size()) {
            bool muted = m_engine->isTrackMuted(static_cast<int>(i));
            m_trackButtons[i]->setText(QString("%1 [%2]").arg(dispName, muted ? lang.stateOff : lang.stateOn));
        }
    }
    m_cmbRagaSource->blockSignals(false);

    // Tab 1: Harmonium & DSP Row
    m_grpHarm->setTitle(lang.grpHarmTitle);
    m_btnHarmoniumToggle->setText(m_engine->isHarmoniumPresetMode() ? lang.btnHarmPresetOn : lang.btnHarmPresetOff);
    m_btnOctDown->setText(lang.btnOctDown);
    m_btnOctUp->setText(lang.btnOctUp);
    m_lblOctave->setText(lang.lblOctaveFmt.arg(m_engine->getOctaveShift()));
    m_btnEqToggle->setText(m_engine->isEqEnabled() ? lang.btnEqOn : lang.btnEqOff);
    m_btnReverbToggle->setText(m_engine->isReverbEnabled() ? lang.btnReverbOn : lang.btnReverbOff);

    updateSongTitleFooter();
    updateMidiStatusLabel();
    onUiTick();

    // Tab 2: Harmonium Editor
    m_lblHarmEdSong->setText(lang.lblEdSong);
    m_btnHarmEdNew->setText(lang.btnNewSong);
    m_lblHarmEdName->setText(lang.lblEdName);
    m_txtHarmEdTitle->setPlaceholderText(lang.txtSongNamePlaceholder);
    m_lblHarmEdVibhags->setText(lang.lblEdVibhags);
    m_spinHarmEdForms->setPrefix(lang.spinFormsPrefix);
    m_spinHarmEdRowsPerForm->setPrefix(lang.spinRowsPrefix);
    m_btnHarmEdPlay->setText(lang.btnPlayInEd);
    m_btnHarmEdApply->setText(lang.btnApplySong);
    m_btnHarmEdSaveFile->setText(lang.btnSaveMhb);
    m_btnHarmEdLoadFile->setText(lang.btnLoadMhb);
    m_btnHarmEdExport->setText(lang.btnExportHeader);

    // Tab 3: Tala Editor
    m_lblTalaEdSelect->setText(lang.lblSelectTala);
    m_btnTalaEdNew->setText(lang.btnNewTala);
    m_lblTalaEdName->setText(lang.lblTalaName);
    m_lblTalaEdVibhags->setText(lang.lblVibhagDivs);
    m_btnTalaEdPlay->setText(lang.btnPlayTala);
    m_btnTalaEdApply->setText(lang.btnApplyTala);
    m_btnTalaEdSaveFile->setText(lang.btnSaveMhb);
    m_btnTalaEdLoadFile->setText(lang.btnLoadMhb);
    m_btnTalaEdExport->setText(lang.btnExportHeader);

    syncTalaEditorCellsToModel();
    rebuildTalaGrid();
}

void MainWindow::updateSongTitleFooter() {
    MmpSong song = m_engine->getSongSnapshot();
    QString activeTalaName = song.talaThekas.empty() ? "" : song.talaThekas[song.currentTalaIdx].name;
    QString activeSongName = song.harmoniumSongs.empty() ? "" : song.harmoniumSongs[song.currentHarmSongIdx].title;
    m_lblSongTitle->setText(L().footerStatusFmt.arg(activeSongName, activeTalaName).arg(song.drumLoopTicks / 48));
}

void MainWindow::updateMidiStatusLabel() {
    const auto& lang = L();
    if (m_lastRawMidiStatus.isEmpty()) {
        m_lblMidiStatus->setText(lang.midiScanning);
    } else if (m_lastRawMidiStatus.startsWith("No USB Keyboard")) {
        m_lblMidiStatus->setText(lang.midiNoUsbTouch);
    } else if (m_lastRawMidiStatus.startsWith("PC Mode:")) {
        m_lblMidiStatus->setText(lang.midiPcMode);
    } else if (m_lastRawMidiStatus.startsWith("MIDI Connected: ")) {
        QString dev = m_lastRawMidiStatus.mid(16);
        m_lblMidiStatus->setText(lang.midiConnectedFmt.arg(dev));
    } else if (m_lastRawMidiStatus == "ALSA MIDI Unavailable") {
        m_lblMidiStatus->setText(lang.midiAlsaUnavail);
    } else if (m_lastRawMidiStatus == "On-Screen Keyboard Mode") {
        m_lblMidiStatus->setText(lang.midiOnScreenMode);
    } else {
        m_lblMidiStatus->setText(m_lastRawMidiStatus);
    }
}

QWidget* MainWindow::createMainPerformanceTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(5, 4, 5, 4);
    mainLayout->setSpacing(3);

    // Top Transport, Tala & Harmonium Song Selector Bar
    QHBoxLayout* topBar = new QHBoxLayout();
    m_btnStartStop = new QPushButton(L().btnStart);
    m_btnStartStop->setStyleSheet("background-color: #1b5e20; color: white; font-size: 13px; min-width: 75px;");
    connect(m_btnStartStop, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);

    m_btnPause = new QPushButton(L().btnPause);
    m_btnPause->setStyleSheet("background-color: #e65100; color: white; font-size: 12px; min-width: 60px;");
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onPauseClicked);

    m_chkCountdown = new QCheckBox(L().chkCountdown);
    m_chkCountdown->setChecked(true);

    m_lblTopTala = new QLabel(L().lblTala);
    m_cmbTalaSelector = new QComboBox();
    m_cmbTalaSelector->setStyleSheet("background-color: #004d40; color: #fff; font-weight: bold;");
    connect(m_cmbTalaSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTalaSelected);

    m_lblTopSong = new QLabel(L().lblSong);
    m_cmbHarmSongSelector = new QComboBox();
    m_cmbHarmSongSelector->setStyleSheet("background-color: #4a148c; color: #fff; font-weight: bold;");
    connect(m_cmbHarmSongSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onHarmSongSelected);

    QPushButton* btnBpmDown = new QPushButton("-5");
    btnBpmDown->setFixedWidth(26);
    m_spinBpm = new QSpinBox();
    m_spinBpm->setRange(40, 260);
    m_spinBpm->setValue(m_engine->getBpm());
    m_spinBpm->setSuffix(" BPM");
    QPushButton* btnBpmUp = new QPushButton("+5");
    btnBpmUp->setFixedWidth(26);

    connect(btnBpmDown, &QPushButton::clicked, this, [this]() { m_spinBpm->setValue(m_spinBpm->value() - 5); });
    connect(btnBpmUp, &QPushButton::clicked, this, [this]() { m_spinBpm->setValue(m_spinBpm->value() + 5); });
    connect(m_spinBpm, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onBpmChanged);

    m_btnLoad = new QPushButton(L().btnLoad);
    m_btnLoad->setStyleSheet("background-color: #0277bd; color: white;");
    connect(m_btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadClicked);

    m_btnSave = new QPushButton(L().btnSave);
    m_btnSave->setStyleSheet("background-color: #37474f; color: white;");
    connect(m_btnSave, &QPushButton::clicked, this, &MainWindow::onSaveMhbClicked);

    topBar->addWidget(m_btnStartStop);
    topBar->addWidget(m_btnPause);
    topBar->addWidget(m_chkCountdown);
    topBar->addWidget(m_lblTopTala);
    topBar->addWidget(m_cmbTalaSelector);
    topBar->addWidget(m_lblTopSong);
    topBar->addWidget(m_cmbHarmSongSelector);
    topBar->addWidget(btnBpmDown);
    topBar->addWidget(m_spinBpm);
    topBar->addWidget(btnBpmUp);
    topBar->addWidget(m_btnLoad);
    topBar->addWidget(m_btnSave);
    mainLayout->addLayout(topBar);

    // Raga Swara Popup + Scale Tuner & Shruti/Tanpura Drone Row
    QHBoxLayout* ragaRow = new QHBoxLayout();
    m_lblRagaPopup = new QLabel(L().swaraPopupFmt.arg(L().swaraNames[0], "C4"));
    m_lblRagaPopup->setAlignment(Qt::AlignCenter);
    m_lblRagaPopup->setMinimumWidth(175);
    m_lblRagaPopup->setStyleSheet(
        "background-color: #0d2538; color: #00e5ff; border: 2px solid #00e5ff; "
        "border-radius: 6px; font-size: 18px; font-weight: bold; padding: 2px;"
        );

    m_grpTuner = new QGroupBox(L().grpTunerTitle);
    QHBoxLayout* tunerLayout = new QHBoxLayout(m_grpTuner);
    tunerLayout->setContentsMargins(4, 2, 4, 2);

    m_cmbSaNote = new QComboBox();
    for (int i = 0; i < 12; ++i) {
        m_cmbSaNote->addItem(L().saKeyPrefix.arg(L().indianKeyLabels[i]), i);
    }

    m_btnDroneToggle = new QPushButton(L().btnDroneOff);
    m_btnDroneToggle->setStyleSheet("background-color: #424242; color: #ccc;");
    connect(m_btnDroneToggle, &QPushButton::clicked, this, [this]() {
        bool en = !m_engine->isDroneEnabled();
        m_engine->setDroneEnabled(en);
        m_btnDroneToggle->setText(en ? L().btnDroneOn : L().btnDroneOff);
        m_btnDroneToggle->setStyleSheet(en
                                            ? "background-color: #f57f17; color: #000; font-weight: bold; border: 1px solid #fff;"
                                            : "background-color: #424242; color: #ccc;");
    });

    m_cmbDronePreset = new QComboBox();
    m_cmbDronePreset->addItem(L().dronePresetBoth, 2);
    m_cmbDronePreset->addItem(L().dronePresetShruti, 0);
    m_cmbDronePreset->addItem(L().dronePresetTanpura, 1);
    connect(m_cmbDronePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_engine->setDronePreset(m_cmbDronePreset->currentData().toInt());
    });

    m_cmbDroneSwara = new QComboBox();
    m_cmbDroneSwara->addItem(L().droneSurSaPa, 0);
    m_cmbDroneSwara->addItem(L().droneSurSaMa, 1);
    m_cmbDroneSwara->addItem(L().droneSurSaNi, 2);
    m_cmbDroneSwara->addItem(L().droneSurSaOnly, 3);
    connect(m_cmbDroneSwara, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_engine->setDroneSwaraMode(m_cmbDroneSwara->currentData().toInt());
    });

    m_spinA4Hz = new QDoubleSpinBox();
    m_spinA4Hz->setRange(415.0, 466.0);
    m_spinA4Hz->setDecimals(1);
    m_spinA4Hz->setValue(440.0);
    m_spinA4Hz->setSuffix("Hz");

    m_cmbRagaSource = new QComboBox();
    m_cmbRagaSource->addItem(L().showAny, -1);
    m_cmbRagaSource->addItem(L().showKeys, -2);

    connect(m_cmbSaNote, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTuningChanged);
    connect(m_spinA4Hz, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onTuningChanged);
    connect(m_cmbRagaSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_engine->setRagaTrackSource(m_cmbRagaSource->currentData().toInt());
    });

    tunerLayout->addWidget(m_cmbSaNote);
    tunerLayout->addWidget(m_btnDroneToggle);
    tunerLayout->addWidget(m_cmbDronePreset);
    tunerLayout->addWidget(m_cmbDroneSwara);
    tunerLayout->addWidget(m_spinA4Hz);
    tunerLayout->addWidget(m_cmbRagaSource);

    ragaRow->addWidget(m_lblRagaPopup);
    ragaRow->addWidget(m_grpTuner, 1);
    mainLayout->addLayout(ragaRow);

    // Multi-Row Grid for All 17 Tala Instrument Switches
    m_grpTracks = new QGroupBox(L().grpTracksTitle);
    QVBoxLayout* trkOuter = new QVBoxLayout(m_grpTracks);
    trkOuter->setContentsMargins(3, 2, 3, 2);
    m_trackBtnContainer = new QWidget();
    m_trackBtnLayout = new QGridLayout(m_trackBtnContainer);
    m_trackBtnLayout->setContentsMargins(0, 0, 0, 0);
    m_trackBtnLayout->setSpacing(3);
    trkOuter->addWidget(m_trackBtnContainer);
    mainLayout->addWidget(m_grpTracks);

    // Harmonium Mode Switch + Master DSP
    m_grpHarm = new QGroupBox(L().grpHarmTitle);
    QHBoxLayout* harmTop = new QHBoxLayout(m_grpHarm);
    harmTop->setContentsMargins(4, 2, 4, 2);

    m_btnHarmoniumToggle = new QPushButton(L().btnHarmPresetOn);
    m_btnHarmoniumToggle->setStyleSheet("background-color: #ef6c00; color: white; font-size: 12px;");
    connect(m_btnHarmoniumToggle, &QPushButton::clicked, this, [this]() {
        bool presetAutoOn = !m_engine->isHarmoniumPresetMode();
        m_engine->setHarmoniumPresetMode(presetAutoOn);
        if (presetAutoOn) {
            m_btnHarmoniumToggle->setText(L().btnHarmPresetOn);
            m_btnHarmoniumToggle->setStyleSheet("background-color: #ef6c00; color: white; font-size: 12px;");
        } else {
            m_btnHarmoniumToggle->setText(L().btnHarmPresetOff);
            m_btnHarmoniumToggle->setStyleSheet("background-color: #00695c; color: white; font-size: 12px;");
        }
    });

    m_btnOctDown = new QPushButton(L().btnOctDown);
    m_btnOctUp = new QPushButton(L().btnOctUp);
    m_lblOctave = new QLabel(L().lblOctaveFmt.arg(0));
    m_lblOctave->setAlignment(Qt::AlignCenter);
    connect(m_btnOctDown, &QPushButton::clicked, this, [this]() {
        m_engine->setOctaveShift(m_engine->getOctaveShift() - 1);
        m_lblOctave->setText(L().lblOctaveFmt.arg(m_engine->getOctaveShift()));
    });
    connect(m_btnOctUp, &QPushButton::clicked, this, [this]() {
        m_engine->setOctaveShift(m_engine->getOctaveShift() + 1);
        m_lblOctave->setText(L().lblOctaveFmt.arg(m_engine->getOctaveShift()));
    });

    m_btnEqToggle = new QPushButton(L().btnEqOn);
    m_btnEqToggle->setStyleSheet("background-color: #283593; color: white;");
    connect(m_btnEqToggle, &QPushButton::clicked, this, [this]() {
        bool en = !m_engine->isEqEnabled();
        m_engine->setEqEnabled(en);
        m_btnEqToggle->setText(en ? L().btnEqOn : L().btnEqOff);
        m_btnEqToggle->setStyleSheet(en ? "background-color: #283593; color: white;" : "background-color: #333; color: #888;");
    });

    m_btnReverbToggle = new QPushButton(L().btnReverbOn);
    m_btnReverbToggle->setStyleSheet("background-color: #283593; color: white;");
    connect(m_btnReverbToggle, &QPushButton::clicked, this, [this]() {
        bool en = !m_engine->isReverbEnabled();
        m_engine->setReverbEnabled(en);
        m_btnReverbToggle->setText(en ? L().btnReverbOn : L().btnReverbOff);
        m_btnReverbToggle->setStyleSheet(en ? "background-color: #283593; color: white;" : "background-color: #333; color: #888;");
    });

    harmTop->addWidget(m_btnHarmoniumToggle);
    harmTop->addWidget(m_btnOctDown);
    harmTop->addWidget(m_lblOctave);
    harmTop->addWidget(m_btnOctUp);
    harmTop->addWidget(m_btnEqToggle);
    harmTop->addWidget(m_btnReverbToggle);
    mainLayout->addWidget(m_grpHarm);

    // 5. On-Screen Touch / PC Keyboard Piano
    m_pianoWidget = new TouchPianoWidget(this);
    connect(m_pianoWidget, &TouchPianoWidget::notePressed, this, [this](int midi, int vel) {
        m_activeNotes.insert(midi);
        m_engine->noteOn(midi, vel);
        m_pianoWidget->setActiveNotes(m_activeNotes);
    });
    connect(m_pianoWidget, &TouchPianoWidget::noteReleased, this, [this](int midi) {
        m_activeNotes.erase(midi);
        m_engine->noteOff(midi);
        m_pianoWidget->setActiveNotes(m_activeNotes);
    });
    mainLayout->addWidget(m_pianoWidget, 1);

    // 6. Status Footer
    QHBoxLayout* footer = new QHBoxLayout();
    m_lblSongTitle = new QLabel();
    m_lblMidiStatus = new QLabel(L().midiScanning);
    m_lblMidiStatus->setAlignment(Qt::AlignRight);
    footer->addWidget(m_lblSongTitle);
    footer->addWidget(m_lblMidiStatus);
    mainLayout->addLayout(footer);

    return page;
}


// TAB 2: Harmonium Swarlipi Editor

QWidget* MainWindow::createHarmoniumEditorTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);

    QHBoxLayout* topRow = new QHBoxLayout();
    m_lblHarmEdSong = new QLabel(L().lblEdSong);
    m_cmbHarmEdSelect = new QComboBox();
    m_cmbHarmEdSelect->setMinimumWidth(140);
    connect(m_cmbHarmEdSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onHarmEditorSongSelected);

    m_btnHarmEdNew = new QPushButton(L().btnNewSong);
    m_btnHarmEdNew->setStyleSheet("background-color: #00695c; color: white;");
    connect(m_btnHarmEdNew, &QPushButton::clicked, this, &MainWindow::onHarmEditorNewSong);

    m_lblHarmEdName = new QLabel(L().lblEdName);
    m_txtHarmEdTitle = new QLineEdit("");
    m_txtHarmEdTitle->setPlaceholderText(L().txtSongNamePlaceholder);

    m_lblHarmEdVibhags = new QLabel(L().lblEdVibhags);
    m_cmbHarmEdDivisions = new QComboBox();
    m_cmbHarmEdDivisions->setEditable(true);
    m_cmbHarmEdDivisions->addItems({"4+4+4+4", "4+4", "3+3", "4+4+4", "3+2+3", "2+3+2+3"});
    connect(m_cmbHarmEdDivisions, &QComboBox::currentTextChanged, this, &MainWindow::onHarmEditorConfigChanged);

    m_spinHarmEdForms = new QSpinBox();
    m_spinHarmEdForms->setRange(1, 6);
    m_spinHarmEdForms->setValue(2);
    m_spinHarmEdForms->setPrefix(L().spinFormsPrefix);
    connect(m_spinHarmEdForms, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onHarmEditorConfigChanged);

    m_spinHarmEdRowsPerForm = new QSpinBox();
    m_spinHarmEdRowsPerForm->setRange(1, 8);
    m_spinHarmEdRowsPerForm->setValue(2);
    m_spinHarmEdRowsPerForm->setPrefix(L().spinRowsPrefix);
    connect(m_spinHarmEdRowsPerForm, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onHarmEditorConfigChanged);

    topRow->addWidget(m_lblHarmEdSong);
    topRow->addWidget(m_cmbHarmEdSelect);
    topRow->addWidget(m_btnHarmEdNew);
    topRow->addWidget(m_lblHarmEdName);
    topRow->addWidget(m_txtHarmEdTitle, 1);
    topRow->addWidget(m_lblHarmEdVibhags);
    topRow->addWidget(m_cmbHarmEdDivisions);
    topRow->addWidget(m_spinHarmEdForms);
    topRow->addWidget(m_spinHarmEdRowsPerForm);
    layout->addLayout(topRow);

    QHBoxLayout* actionRow = new QHBoxLayout();
    m_btnHarmEdPlay = new QPushButton(L().btnPlayInEd);
    m_btnHarmEdPlay->setStyleSheet("background-color: #1b5e20; color: white;");
    connect(m_btnHarmEdPlay, &QPushButton::clicked, this, [this]() {
        onHarmEditorSaveSong();
        m_engine->setHarmoniumPresetMode(true);
        m_btnHarmoniumToggle->setText(L().btnHarmPresetOn);
        m_btnHarmoniumToggle->setStyleSheet("background-color: #ef6c00; color: white; font-size: 12px;");
        onStartStopClicked();
    });

    m_btnHarmEdApply = new QPushButton(L().btnApplySong);
    m_btnHarmEdApply->setStyleSheet("background-color: #0277bd; color: white;");
    connect(m_btnHarmEdApply, &QPushButton::clicked, this, &MainWindow::onHarmEditorSaveSong);

    m_btnHarmEdSaveFile = new QPushButton(L().btnSaveMhb);
    m_btnHarmEdSaveFile->setStyleSheet("background-color: #4a148c; color: white;");
    connect(m_btnHarmEdSaveFile, &QPushButton::clicked, this, [this]() {
        onHarmEditorSaveSong();
        onSaveMhbClicked();
    });

    m_btnHarmEdLoadFile = new QPushButton(L().btnLoadMhb);
    connect(m_btnHarmEdLoadFile, &QPushButton::clicked, this, &MainWindow::onLoadClicked);

    m_btnHarmEdExport = new QPushButton(L().btnExportHeader);
    m_btnHarmEdExport->setStyleSheet("background-color: #b71c1c; color: white;");
    connect(m_btnHarmEdExport, &QPushButton::clicked, this, [this]() {
        onHarmEditorSaveSong();
        onExportPresetsHeaderClicked();
    });

    actionRow->addWidget(m_btnHarmEdPlay);
    actionRow->addWidget(m_btnHarmEdApply);
    actionRow->addWidget(m_btnHarmEdSaveFile);
    actionRow->addWidget(m_btnHarmEdLoadFile);
    actionRow->addWidget(m_btnHarmEdExport);
    layout->addLayout(actionRow);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_harmEdGridWidget = new QWidget();
    m_harmEdGridLayout = new QGridLayout(m_harmEdGridWidget);
    m_harmEdGridLayout->setContentsMargins(4, 4, 4, 4);
    m_harmEdGridLayout->setSpacing(3);
    scroll->setWidget(m_harmEdGridWidget);
    layout->addWidget(scroll, 1);

    return page;
}

void MainWindow::rebuildHarmoniumGrid() {
    QLayoutItem* child;
    while ((child = m_harmEdGridLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    m_harmSwaraCombos.clear();

    int totalMatras = 0;
    QVector<int> vibhags = ProjectIO::parseVibhagDivisions(m_editingHarmSong.divisions, &totalMatras);
    QStringList taliLabels = ProjectIO::computeVibhagTaliKhaliLabels(vibhags);

    int gridRow = 0;
    for (size_t fIdx = 0; fIdx < m_editingHarmSong.forms.size(); ++fIdx) {
        const SongForm& form = m_editingHarmSong.forms[fIdx];

        QLabel* lblForm = new QLabel(QString("<b>%1</b>").arg(form.formName));
        lblForm->setStyleSheet("color: #ffca28; font-size: 13px; padding-top: 4px;");
        m_harmEdGridLayout->addWidget(lblForm, gridRow++, 0, 1, totalMatras + vibhags.size());

        int col = 0;
        int matraNum = 1;
        for (int v = 0; v < vibhags.size(); ++v) {
            for (int m = 0; m < vibhags[v]; ++m) {
                QString hdrText = (m == 0)
                ? QString("[%1]  M%2").arg(taliLabels[v]).arg(matraNum)
                : QString("M%1").arg(matraNum);
                QLabel* lblM = new QLabel(hdrText);
                lblM->setAlignment(Qt::AlignCenter);
                lblM->setStyleSheet(m == 0
                                        ? "background-color: #263238; color: #00e5ff; font-weight: bold; border: 1px solid #00acc1; padding: 2px;"
                                        : "background-color: #1e222a; color: #aaa; border: 1px solid #333; padding: 2px;");
                m_harmEdGridLayout->addWidget(lblM, gridRow, col++);
                matraNum++;
            }
            if (v + 1 < vibhags.size()) {
                QLabel* sep = new QLabel("|");
                sep->setAlignment(Qt::AlignCenter);
                sep->setStyleSheet("color: #ffca28; font-weight: bold;");
                m_harmEdGridLayout->addWidget(sep, gridRow, col++);
            }
        }
        gridRow++;

        for (int r = 0; r < form.numRows; ++r) {
            col = 0;
            for (int v = 0; v < vibhags.size(); ++v) {
                for (int m = 0; m < vibhags[v]; ++m) {
                    int cellIdxInForm = r * totalMatras + (col - v);
                    QString curSwara = (cellIdxInForm < form.swaraCells.size()) ? form.swaraCells[cellIdxInForm] : "-";

                    QComboBox* cmb = new QComboBox();
                    cmb->setEditable(true);
                    cmb->addItems(STANDARD_SWARA_LIST);
                    cmb->setCurrentText(curSwara);
                    cmb->setMinimumWidth(52);
                    cmb->setStyleSheet(m == 0
                                           ? "background-color: #1a2733; color: #ffffff; font-weight: bold; border: 1px solid #00acc1;"
                                           : "background-color: #252830; color: #e0e0e0; border: 1px solid #444;");

                    connect(cmb, &QComboBox::currentTextChanged, this, [this](const QString& txt) {
                        m_engine->auditionSwara(txt);
                    });

                    m_harmEdGridLayout->addWidget(cmb, gridRow, col++);
                    m_harmSwaraCombos.push_back(cmb);
                }
                if (v + 1 < vibhags.size()) {
                    QLabel* sep = new QLabel("|");
                    sep->setAlignment(Qt::AlignCenter);
                    sep->setStyleSheet("color: #ffca28; font-weight: bold;");
                    m_harmEdGridLayout->addWidget(sep, gridRow, col++);
                }
            }
            gridRow++;
        }
    }
}

void MainWindow::syncHarmEditorCellsToModel() {
    int totalMatras = 0;
    ProjectIO::parseVibhagDivisions(m_editingHarmSong.divisions, &totalMatras);
    size_t comboCursor = 0;
    for (auto& form : m_editingHarmSong.forms) {
        int needed = form.numRows * totalMatras;
        form.swaraCells.clear();
        for (int i = 0; i < needed; ++i) {
            if (comboCursor < m_harmSwaraCombos.size()) {
                form.swaraCells.push_back(m_harmSwaraCombos[comboCursor++]->currentText().trimmed());
            } else {
                form.swaraCells.push_back("-");
            }
        }
    }
    m_editingHarmSong.title = m_txtHarmEdTitle->text().trimmed();
}

void MainWindow::onHarmEditorSongSelected(int idx) {
    MmpSong snap = m_engine->getSongSnapshot();
    if (idx < 0 || idx >= static_cast<int>(snap.harmoniumSongs.size())) return;
    m_editingHarmSong = snap.harmoniumSongs[idx];

    m_txtHarmEdTitle->setText(m_editingHarmSong.title);
    m_cmbHarmEdDivisions->blockSignals(true);
    m_cmbHarmEdDivisions->setCurrentText(m_editingHarmSong.divisions);
    m_cmbHarmEdDivisions->blockSignals(false);

    m_spinHarmEdForms->blockSignals(true);
    m_spinHarmEdForms->setValue(std::max(1, static_cast<int>(m_editingHarmSong.forms.size())));
    m_spinHarmEdForms->blockSignals(false);

    m_spinHarmEdRowsPerForm->blockSignals(true);
    m_spinHarmEdRowsPerForm->setValue(m_editingHarmSong.forms.empty() ? 2 : m_editingHarmSong.forms[0].numRows);
    m_spinHarmEdRowsPerForm->blockSignals(false);

    rebuildHarmoniumGrid();
}

void MainWindow::onHarmEditorConfigChanged() {
    syncHarmEditorCellsToModel();
    m_editingHarmSong.divisions = m_cmbHarmEdDivisions->currentText().trimmed();
    int totalMatras = 0;
    ProjectIO::parseVibhagDivisions(m_editingHarmSong.divisions, &totalMatras);

    int targetForms = m_spinHarmEdForms->value();
    int targetRows = m_spinHarmEdRowsPerForm->value();

    m_editingHarmSong.forms.resize(targetForms);
    for (int f = 0; f < targetForms; ++f) {
        if (m_editingHarmSong.forms[f].formName.isEmpty()) {
            m_editingHarmSong.forms[f].formName = QString("Form %1").arg(QChar('A' + f));
        }
        m_editingHarmSong.forms[f].numRows = targetRows;
        int needed = targetRows * totalMatras;
        while (m_editingHarmSong.forms[f].swaraCells.size() < needed) {
            m_editingHarmSong.forms[f].swaraCells.push_back("-");
        }
    }
    rebuildHarmoniumGrid();
}

void MainWindow::onHarmEditorSaveSong() {
    syncHarmEditorCellsToModel();
    int idx = m_cmbHarmEdSelect->currentIndex();
    m_engine->updateHarmoniumSong(idx, m_editingHarmSong);
    m_engine->selectHarmoniumSong(idx);
    rebuildTrackAndTalaControls();
}

void MainWindow::onHarmEditorNewSong() {
    HarmoniumSong ns;
    ns.title = "New Song";
    ns.ragaName = "Yaman / Bilaval";
    ns.divisions = "4+4";
    ns.defaultBpm = 130;
    SongForm fa;
    fa.formName = "Form A (Sthayi)";
    fa.numRows = 2;
    fa.swaraCells = {"S", "R", "G", "m", "P", "d", "n", "S'", "S'", "n", "d", "P", "m", "G", "R", "S"};
    ns.forms = {fa};

    m_engine->addHarmoniumSong(ns);
    rebuildTrackAndTalaControls();
}


// TAB 3: Tala Theka Editor

QWidget* MainWindow::createTalaEditorTab() {
    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);

    QHBoxLayout* topRow = new QHBoxLayout();
    m_lblTalaEdSelect = new QLabel(L().lblSelectTala);
    m_cmbTalaEdSelect = new QComboBox();
    m_cmbTalaEdSelect->setMinimumWidth(160);
    connect(m_cmbTalaEdSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onTalaEditorSelected);

    m_btnTalaEdNew = new QPushButton(L().btnNewTala);
    m_btnTalaEdNew->setStyleSheet("background-color: #00695c; color: white;");
    connect(m_btnTalaEdNew, &QPushButton::clicked, this, &MainWindow::onTalaEditorNewTala);

    m_lblTalaEdName = new QLabel(L().lblTalaName);
    m_txtTalaEdName = new QLineEdit("");

    m_lblTalaEdVibhags = new QLabel(L().lblVibhagDivs);
    m_cmbTalaEdDivisions = new QComboBox();
    m_cmbTalaEdDivisions->setEditable(true);
    m_cmbTalaEdDivisions->addItems({"4+4+4+4", "4+4", "3+3", "3+2+2", "2+3+2+3", "4+4+4"});
    connect(m_cmbTalaEdDivisions, &QComboBox::currentTextChanged, this, &MainWindow::onTalaEditorDivisionsChanged);

    topRow->addWidget(m_lblTalaEdSelect);
    topRow->addWidget(m_cmbTalaEdSelect);
    topRow->addWidget(m_btnTalaEdNew);
    topRow->addWidget(m_lblTalaEdName);
    topRow->addWidget(m_txtTalaEdName, 1);
    topRow->addWidget(m_lblTalaEdVibhags);
    topRow->addWidget(m_cmbTalaEdDivisions);
    layout->addLayout(topRow);

    QHBoxLayout* actionRow = new QHBoxLayout();
    m_btnTalaEdPlay = new QPushButton(L().btnPlayTala);
    m_btnTalaEdPlay->setStyleSheet("background-color: #1b5e20; color: white;");
    connect(m_btnTalaEdPlay, &QPushButton::clicked, this, [this]() {
        onTalaEditorSaveTala();
        onStartStopClicked();
    });

    m_btnTalaEdApply = new QPushButton(L().btnApplyTala);
    m_btnTalaEdApply->setStyleSheet("background-color: #0277bd; color: white;");
    connect(m_btnTalaEdApply, &QPushButton::clicked, this, &MainWindow::onTalaEditorSaveTala);

    m_btnTalaEdSaveFile = new QPushButton(L().btnSaveMhb);
    m_btnTalaEdSaveFile->setStyleSheet("background-color: #4a148c; color: white;");
    connect(m_btnTalaEdSaveFile, &QPushButton::clicked, this, [this]() {
        onTalaEditorSaveTala();
        onSaveMhbClicked();
    });

    m_btnTalaEdLoadFile = new QPushButton(L().btnLoadMhb);
    connect(m_btnTalaEdLoadFile, &QPushButton::clicked, this, &MainWindow::onLoadClicked);

    m_btnTalaEdExport = new QPushButton(L().btnExportHeader);
    m_btnTalaEdExport->setStyleSheet("background-color: #b71c1c; color: white;");
    connect(m_btnTalaEdExport, &QPushButton::clicked, this, [this]() {
        onTalaEditorSaveTala();
        onExportPresetsHeaderClicked();
    });

    actionRow->addWidget(m_btnTalaEdPlay);
    actionRow->addWidget(m_btnTalaEdApply);
    actionRow->addWidget(m_btnTalaEdSaveFile);
    actionRow->addWidget(m_btnTalaEdLoadFile);
    actionRow->addWidget(m_btnTalaEdExport);
    layout->addLayout(actionRow);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_talaEdGridWidget = new QWidget();
    m_talaEdGridLayout = new QGridLayout(m_talaEdGridWidget);
    m_talaEdGridLayout->setContentsMargins(4, 4, 4, 4);
    m_talaEdGridLayout->setSpacing(4);
    scroll->setWidget(m_talaEdGridWidget);
    layout->addWidget(scroll, 1);

    return page;
}

void MainWindow::rebuildTalaGrid() {
    QLayoutItem* child;
    while ((child = m_talaEdGridLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    m_talaBolCombos.clear();
    m_talaKartalCombos.clear();
    m_talaMatraHeaders.clear();

    const auto& lang = L();
    int totalMatras = 0;
    QVector<int> vibhags = ProjectIO::parseVibhagDivisions(m_editingTala.divisions, &totalMatras);
    QStringList taliLabels = ProjectIO::computeVibhagTaliKhaliLabels(vibhags);

    int matraIdx = 0;
    int visualBlockRow = 0;

    for (int v = 0; v < vibhags.size(); ++v) {
        int vLen = vibhags[v];
        QGroupBox* grpVibhag = new QGroupBox(lang.vibhagHeaderFmt.arg(v + 1).arg(taliLabels[v]));
        QGridLayout* vGrid = new QGridLayout(grpVibhag);
        vGrid->setContentsMargins(4, 4, 4, 4);
        vGrid->setSpacing(4);

        vGrid->addWidget(new QLabel(lang.lblMatraCol), 0, 0);
        vGrid->addWidget(new QLabel(lang.lblThekaBolCol), 1, 0);
        vGrid->addWidget(new QLabel(lang.lblKartalCol), 2, 0);

        for (int m = 0; m < vLen; ++m) {
            QLabel* lblMatra = new QLabel(lang.matraItemFmt.arg(matraIdx + 1));
            lblMatra->setAlignment(Qt::AlignCenter);
            lblMatra->setStyleSheet("background-color: #263238; color: #00e5ff; font-weight: bold; padding: 3px; border-radius: 3px;");
            vGrid->addWidget(lblMatra, 0, m + 1);
            m_talaMatraHeaders.push_back(lblMatra);

            QComboBox* cmbBol = new QComboBox();
            cmbBol->setEditable(true);
            cmbBol->addItems(STANDARD_BOL_LIST);
            QString curBol = (matraIdx < m_editingTala.bolCells.size()) ? m_editingTala.bolCells[matraIdx] : "-";
            cmbBol->setCurrentText(curBol);
            connect(cmbBol, &QComboBox::currentTextChanged, this, [this](const QString& txt) {
                m_engine->auditionBolByName(txt);
            });
            vGrid->addWidget(cmbBol, 1, m + 1);
            m_talaBolCombos.push_back(cmbBol);

            QComboBox* cmbKartal = new QComboBox();
            cmbKartal->addItems(STANDARD_KARTAL_LIST);
            QString curKartal = (matraIdx < m_editingTala.kartalCells.size()) ? m_editingTala.kartalCells[matraIdx] : "-";
            cmbKartal->setCurrentText(curKartal);
            connect(cmbKartal, &QComboBox::currentTextChanged, this, [this](const QString& txt) {
                m_engine->auditionBolByName(txt);
            });
            vGrid->addWidget(cmbKartal, 2, m + 1);
            m_talaKartalCombos.push_back(cmbKartal);

            matraIdx++;
        }

        m_talaEdGridLayout->addWidget(grpVibhag, visualBlockRow / 2, visualBlockRow % 2);
        visualBlockRow++;
    }
}

void MainWindow::syncTalaEditorCellsToModel() {
    m_editingTala.name = m_txtTalaEdName->text().trimmed();
    m_editingTala.divisions = m_cmbTalaEdDivisions->currentText().trimmed();
    m_editingTala.bolCells.clear();
    m_editingTala.kartalCells.clear();
    for (size_t i = 0; i < m_talaBolCombos.size(); ++i) {
        m_editingTala.bolCells.push_back(m_talaBolCombos[i]->currentText().trimmed());
    }
    for (size_t i = 0; i < m_talaKartalCombos.size(); ++i) {
        m_editingTala.kartalCells.push_back(m_talaKartalCombos[i]->currentText().trimmed());
    }
}

void MainWindow::onTalaEditorSelected(int idx) {
    MmpSong snap = m_engine->getSongSnapshot();
    if (idx < 0 || idx >= static_cast<int>(snap.talaThekas.size())) return;
    m_editingTala = snap.talaThekas[idx];

    m_txtTalaEdName->setText(m_editingTala.name);
    m_cmbTalaEdDivisions->blockSignals(true);
    m_cmbTalaEdDivisions->setCurrentText(m_editingTala.divisions);
    m_cmbTalaEdDivisions->blockSignals(false);

    rebuildTalaGrid();
}

void MainWindow::onTalaEditorDivisionsChanged() {
    syncTalaEditorCellsToModel();
    int totalMatras = 0;
    ProjectIO::parseVibhagDivisions(m_editingTala.divisions, &totalMatras);
    while (m_editingTala.bolCells.size() < totalMatras) m_editingTala.bolCells.push_back("-");
    while (m_editingTala.kartalCells.size() < totalMatras) m_editingTala.kartalCells.push_back("-");
    rebuildTalaGrid();
}

void MainWindow::onTalaEditorSaveTala() {
    syncTalaEditorCellsToModel();
    int idx = m_cmbTalaEdSelect->currentIndex();
    m_engine->updateTalaTheka(idx, m_editingTala);
    m_engine->selectTala(idx);
    rebuildTrackAndTalaControls();
}

void MainWindow::onTalaEditorNewTala() {
    TalaTheka nt;
    nt.name = "Custom Tala (8 Matra)";
    nt.divisions = "4+4";
    nt.bolCells = {"Dha", "Dhin", "Dhin", "Dha", "Dha", "Ti", "Ti", "Ta"};
    nt.kartalCells = {"Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed", "Kartal Open", "Kartal Closed", "Kartal Closed", "Kartal Closed"};
    m_engine->addTalaTheka(nt);
    rebuildTrackAndTalaControls();
}

void MainWindow::rebuildTrackAndTalaControls() {
    for (auto* b : m_trackButtons) delete b;
    m_trackButtons.clear();

    MmpSong song = m_engine->getSongSnapshot();

    m_cmbTalaSelector->blockSignals(true);
    m_cmbTalaEdSelect->blockSignals(true);
    m_cmbTalaSelector->clear();
    m_cmbTalaEdSelect->clear();
    for (size_t i = 0; i < song.talaThekas.size(); ++i) {
        m_cmbTalaSelector->addItem(song.talaThekas[i].name, static_cast<int>(i));
        m_cmbTalaEdSelect->addItem(song.talaThekas[i].name, static_cast<int>(i));
    }
    m_cmbTalaSelector->setCurrentIndex(song.currentTalaIdx);
    m_cmbTalaEdSelect->setCurrentIndex(song.currentTalaIdx);
    m_cmbTalaSelector->blockSignals(false);
    m_cmbTalaEdSelect->blockSignals(false);

    m_cmbHarmSongSelector->blockSignals(true);
    m_cmbHarmEdSelect->blockSignals(true);
    m_cmbHarmSongSelector->clear();
    m_cmbHarmEdSelect->clear();
    for (size_t i = 0; i < song.harmoniumSongs.size(); ++i) {
        m_cmbHarmSongSelector->addItem(song.harmoniumSongs[i].title, static_cast<int>(i));
        m_cmbHarmEdSelect->addItem(song.harmoniumSongs[i].title, static_cast<int>(i));
    }
    m_cmbHarmSongSelector->setCurrentIndex(song.currentHarmSongIdx);
    m_cmbHarmEdSelect->setCurrentIndex(song.currentHarmSongIdx);
    m_cmbHarmSongSelector->blockSignals(false);
    m_cmbHarmEdSelect->blockSignals(false);

    updateSongTitleFooter();

    m_cmbRagaSource->blockSignals(true);
    m_cmbRagaSource->clear();
    m_cmbRagaSource->addItem(L().showAny, -1);
    m_cmbRagaSource->addItem(L().showKeys, -2);

    const int cols = 6;
    for (size_t i = 0; i < song.tracks.size(); ++i) {
        const auto& trk = song.tracks[i];
        QString dispName = L().translateTrackName(trk.name);
        m_cmbRagaSource->addItem(L().showTrkPrefix + dispName, static_cast<int>(i));

        QPushButton* btn = new QPushButton(this);
        auto updateBtnStyle = [this, btn](const QString& canonicalName, bool muted) {
            QString localName = L().translateTrackName(canonicalName);
            btn->setText(QString("%1 [%2]").arg(localName, muted ? L().stateOff : L().stateOn));
            btn->setStyleSheet(muted
                                   ? "background-color: #37474f; color: #90a4ae; border: 1px solid #555;"
                                   : "background-color: #2e7d32; color: #ffffff; border: 2px solid #81c784;");
        };
        updateBtnStyle(trk.name, trk.muted);

        int idx = static_cast<int>(i);
        connect(btn, &QPushButton::clicked, this, [this, idx, trkName = trk.name, updateBtnStyle]() {
            bool newMuted = !m_engine->isTrackMuted(idx);
            m_engine->setTrackMuted(idx, newMuted);
            updateBtnStyle(trkName, newMuted);
        });

        m_trackBtnLayout->addWidget(btn, static_cast<int>(i) / cols, static_cast<int>(i) % cols);
        m_trackButtons.push_back(btn);
    }
    m_cmbRagaSource->blockSignals(false);

    onHarmEditorSongSelected(song.currentHarmSongIdx);
    onTalaEditorSelected(song.currentTalaIdx);
}

void MainWindow::onTalaSelected(int index) {
    if (index < 0) return;
    m_engine->selectTala(index);
    m_cmbTalaEdSelect->blockSignals(true);
    m_cmbTalaEdSelect->setCurrentIndex(index);
    m_cmbTalaEdSelect->blockSignals(false);
    onTalaEditorSelected(index);
    updateSongTitleFooter();
}

void MainWindow::onHarmSongSelected(int index) {
    if (index < 0) return;
    m_engine->selectHarmoniumSong(index);
    m_cmbHarmEdSelect->blockSignals(true);
    m_cmbHarmEdSelect->setCurrentIndex(index);
    m_cmbHarmEdSelect->blockSignals(false);
    onHarmEditorSongSelected(index);
    updateSongTitleFooter();
}

void MainWindow::onStartStopClicked() {
    if (!m_engine->isPlaying()) {
        m_engine->startWithCountdown(m_chkCountdown->isChecked());
        m_btnStartStop->setText(L().btnStop);
        m_btnStartStop->setStyleSheet("background-color: #b71c1c; color: white; font-size: 13px; min-width: 75px;");
    } else {
        m_engine->stopPlayback();
        m_btnStartStop->setText(L().btnStart);
        m_btnStartStop->setStyleSheet("background-color: #1b5e20; color: white; font-size: 13px; min-width: 75px;");
        m_btnPause->setText(L().btnPause);
    }
}

void MainWindow::onPauseClicked() {
    if (!m_engine->isPlaying()) return;
    m_engine->togglePause();
    m_btnPause->setText(m_engine->isPaused() ? L().btnResume : L().btnPause);
}

void MainWindow::onLoadClicked() {
    QString path = QFileDialog::getOpenFileName(
        this, "Load Tala File", "",
        "Files (*.mhb *.mmp);; Harmonium Box (*.mhb);;LMMS Project (*.mmp)");
    if (path.isEmpty()) return;

    MmpSong loaded;
    if (ProjectIO::loadFile(path, loaded)) {
        m_engine->stopPlayback();
        m_btnStartStop->setText(L().btnStart);
        m_btnStartStop->setStyleSheet("background-color: #1b5e20; color: white; font-size: 13px; min-width: 75px;");
        m_engine->loadSong(loaded);
        m_spinBpm->setValue(loaded.bpm);
        m_cmbSaNote->setCurrentIndex(loaded.saSemitone);
        m_spinA4Hz->setValue(loaded.a4Hz);
        rebuildTrackAndTalaControls();
    } else {
        QMessageBox::warning(this, "Load Error", "Could not parse selected file.");
    }
}

void MainWindow::onSaveMhbClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save mhb Format (.mhb)", "", "Harmonium Box (*.mhb)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".mhb", Qt::CaseInsensitive)) path += ".mhb";

    if (ProjectIO::saveMhbFile(path, m_engine->getSongSnapshot())) {
        QMessageBox::information(this, "Saved", "Saved all Harmonium Melodies & Talas in .mhb format!");
    } else {
        QMessageBox::warning(this, "Save Error", "Could not write .mhb file.");
    }
}

void MainWindow::onExportPresetsHeaderClicked() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Pre-Compilation C++ Header (drum_presets.h)", "drum_presets.h", "C++ Header (drum_presets.h *.h)");
    if (path.isEmpty()) return;

    if (ProjectIO::exportDrumPresetsHeader(path, m_engine->getSongSnapshot())) {
        QMessageBox::information(this, "Exported drum_presets.h",
                                 "Saved all Talas & Harmonium Songs to:\n" + path +
                                     "\n\nYou can also edit this header file in any text editor before compiling!");
    } else {
        QMessageBox::warning(this, "Error", "Failed to write header file.");
    }
}

void MainWindow::onBpmChanged(int newBpm) {
    m_engine->setBpm(newBpm);
}

void MainWindow::onTuningChanged() {
    int sa = m_cmbSaNote->currentData().toInt();
    double a4 = m_spinA4Hz->value();
    m_engine->setTuning(sa, a4);
    m_pianoWidget->setSaSemitone(sa);
}

QString MainWindow::formatRagaSwara(int midiNote) const {
    const auto& lang = L();
    if (midiNote < 0) return lang.swaraPopupNone;
    int sa = m_engine->getSaSemitone();
    int rel = (midiNote - (60 + sa));
    int semitone = ((rel % 12) + 12) % 12;
    int octaveOffset = static_cast<int>(std::floor(rel / 12.0));

    QString swara = lang.swaraNames[semitone];
    if (octaveOffset < 0) swara = "'" + swara; // Mandra Saptak ('n, 'P)
    else if (octaveOffset > 0) swara += "'";   // Tara Saptak (S', R')

    int westOct = (midiNote / 12) - 1;
    QString west = QString("%1%2").arg(WESTERN_NOTES[midiNote % 12]).arg(westOct);
    return lang.swaraPopupFmt.arg(swara, west);
}

void MainWindow::onUiTick() {
    if (m_engine->isPlaying() && m_engine->countdownBeatsLeft() > 0) {
        m_lblRagaPopup->setText(L().countdownFmt.arg(m_engine->countdownBeatsLeft()));
        return;
    }
    int midi = m_engine->getCurrentDetectedMidiNote();
    m_lblRagaPopup->setText(formatRagaSwara(midi));

    if (m_tabs->currentIndex() == 2 && m_engine->isPlaying()) {
        int curMatra = m_engine->getCurrentDrumTick() / 48;
        for (int m = 0; m < static_cast<int>(m_talaMatraHeaders.size()); ++m) {
            m_talaMatraHeaders[m]->setStyleSheet(m == curMatra
                                                     ? "background-color: #ffca28; color: #000; font-weight: bold; padding: 3px; border-radius: 3px;"
                                                     : "background-color: #263238; color: #00e5ff; font-weight: bold; padding: 3px; border-radius: 3px;");
        }
    }
}

int MainWindow::pcKeyToMidi(int key) const {
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

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) return;
    int midi = pcKeyToMidi(event->key());
    if (midi >= 0) {
        m_activeNotes.insert(midi);
        m_engine->noteOn(midi, 105);
        m_pianoWidget->setActiveNotes(m_activeNotes);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) return;
    int midi = pcKeyToMidi(event->key());
    if (midi >= 0) {
        m_activeNotes.erase(midi);
        m_engine->noteOff(midi);
        m_pianoWidget->setActiveNotes(m_activeNotes);
    }
}
