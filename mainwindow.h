#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QGroupBox>
#include <QGridLayout>
#include <QTimer>
#include <vector>
#include <set>
#include "synthengine.h"
#include "midireader.h"
#include "i18n.h"

class TouchPianoWidget : public QWidget {
    Q_OBJECT
public:
    explicit TouchPianoWidget(QWidget* parent = nullptr);
    void setSaSemitone(int sa);
    void setActiveNotes(const std::set<int>& notes);
    void setLanguage(int langIdx);

signals:
    void notePressed(int midiNote, int velocity);
    void noteReleased(int midiNote);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int hitTestMidi(const QPoint& pos) const;
    int m_startMidi = 48;
    int m_numKeys = 37;
    int m_saSemitone = 0;
    int m_langIdx = 0;
    int m_mouseHeldMidi = -1;
    std::set<int> m_activeNotes;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private slots:
    void onLanguageChanged(int index);
    void onStartStopClicked();
    void onPauseClicked();
    void onLoadClicked();
    void onSaveMhbClicked();
    void onExportPresetsHeaderClicked();
    void onTalaSelected(int index);
    void onHarmSongSelected(int index);
    void onRandomiseRagaClicked();
    void onRandomiseTalaClicked();
    void onBpmChanged(int newBpm);
    void onTuningChanged();
    void onUiTick();

    // Harmonium Swarlipi Editor Slots
    void onHarmEditorSongSelected(int idx);
    void onHarmEditorConfigChanged();
    void onHarmEditorSaveSong();
    void onHarmEditorNewSong();

    // Tala Theka Editor Slots
    void onTalaEditorSelected(int idx);
    void onTalaEditorDivisionsChanged();
    void onTalaEditorSaveTala();
    void onTalaEditorNewTala();

private:
    void setupUI();
    void retranslateUi();
    void updateSongTitleFooter();
    void updateMidiStatusLabel();
    const I18n::LangPack& L() const;

    QWidget* createMainPerformanceTab();
    QWidget* createHarmoniumEditorTab();
    QWidget* createTalaEditorTab();

    void rebuildTrackAndTalaControls();
    void rebuildHarmoniumGrid();
    void rebuildTalaGrid();
    void syncHarmEditorCellsToModel();
    void syncTalaEditorCellsToModel();

    QString formatRagaSwara(int midiNote) const;
    int pcKeyToMidi(int key) const;

    SynthEngine* m_engine = nullptr;
    MidiReader* m_midiReader = nullptr;
    QTimer* m_uiTimer = nullptr;

    int m_langIdx = 0;
    QString m_lastRawMidiStatus;

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_cmbLangSelector = nullptr;

    // Tab 1: Live Performance Widgets
    QPushButton* m_btnStartStop = nullptr;
    QPushButton* m_btnPause = nullptr;
    QCheckBox* m_chkCountdown = nullptr;
    QLabel* m_lblTopTala = nullptr;
    QComboBox* m_cmbTalaSelector = nullptr;
    QPushButton* m_btnRandTalaLive = nullptr;
    QLabel* m_lblTopSong = nullptr;
    QComboBox* m_cmbHarmSongSelector = nullptr;
    QPushButton* m_btnRandRagaLive = nullptr;
    QSpinBox* m_spinBpm = nullptr;
    QPushButton* m_btnLoad = nullptr;
    QPushButton* m_btnSave = nullptr;

    QLabel* m_lblSongTitle = nullptr;
    QLabel* m_lblMidiStatus = nullptr;
    QLabel* m_lblCpuMeter = nullptr;
    QLabel* m_lblRagaPopup = nullptr;

    QGroupBox* m_grpTuner = nullptr;
    QComboBox* m_cmbSaNote = nullptr;
    QDoubleSpinBox* m_spinA4Hz = nullptr;
    QComboBox* m_cmbRagaSource = nullptr;

    QPushButton* m_btnDroneToggle = nullptr;
    QComboBox* m_cmbDronePreset = nullptr;
    QComboBox* m_cmbDroneSwara = nullptr;

    QGroupBox* m_grpTracks = nullptr;
    QWidget* m_trackBtnContainer = nullptr;
    QGridLayout* m_trackBtnLayout = nullptr;
    std::vector<QPushButton*> m_trackButtons;

    QGroupBox* m_grpHarm = nullptr;
    QPushButton* m_btnHarmoniumToggle = nullptr;
    QPushButton* m_btnOctDown = nullptr;
    QPushButton* m_btnOctUp = nullptr;
    QLabel* m_lblOctave = nullptr;

    // MIDI In / Out Controls
    QPushButton* m_btnMidiInToggle = nullptr;
    QComboBox* m_cmbMidiInMode = nullptr;
    QLabel* m_lblHarmCh = nullptr;
    QComboBox* m_cmbMidiHarmCh = nullptr;
    QLabel* m_lblDrumCh = nullptr;
    QComboBox* m_cmbMidiDrumCh = nullptr;
    QPushButton* m_btnMidiOutToggle = nullptr;
    QComboBox* m_cmbMidiOutMode = nullptr;

    TouchPianoWidget* m_pianoWidget = nullptr;
    std::set<int> m_activeNotes;

    // Tab 2: Harmonium Swarlipi Editor Widgets
    QLabel* m_lblHarmEdSong = nullptr;
    QComboBox* m_cmbHarmEdSelect = nullptr;
    QPushButton* m_btnHarmEdNew = nullptr;
    QPushButton* m_btnHarmEdRandom = nullptr;
    QLabel* m_lblHarmEdName = nullptr;
    QLineEdit* m_txtHarmEdTitle = nullptr;
    QLabel* m_lblHarmEdVibhags = nullptr;
    QComboBox* m_cmbHarmEdDivisions = nullptr;
    QSpinBox* m_spinHarmEdForms = nullptr;
    QSpinBox* m_spinHarmEdRowsPerForm = nullptr;
    QPushButton* m_btnHarmEdPlay = nullptr;
    QPushButton* m_btnHarmEdApply = nullptr;
    QPushButton* m_btnHarmEdSaveFile = nullptr;
    QPushButton* m_btnHarmEdLoadFile = nullptr;
    QPushButton* m_btnHarmEdExport = nullptr;
    QWidget* m_harmEdGridWidget = nullptr;
    QGridLayout* m_harmEdGridLayout = nullptr;
    HarmoniumSong m_editingHarmSong;
    std::vector<QComboBox*> m_harmSwaraCombos;

    // Tab 3: Tala Theka Editor Widgets
    QLabel* m_lblTalaEdSelect = nullptr;
    QComboBox* m_cmbTalaEdSelect = nullptr;
    QPushButton* m_btnTalaEdNew = nullptr;
    QPushButton* m_btnTalaEdRandom = nullptr;
    QLabel* m_lblTalaEdName = nullptr;
    QLineEdit* m_txtTalaEdName = nullptr;
    QLabel* m_lblTalaEdVibhags = nullptr;
    QComboBox* m_cmbTalaEdDivisions = nullptr;
    QPushButton* m_btnTalaEdPlay = nullptr;
    QPushButton* m_btnTalaEdApply = nullptr;
    QPushButton* m_btnTalaEdSaveFile = nullptr;
    QPushButton* m_btnTalaEdLoadFile = nullptr;
    QPushButton* m_btnTalaEdExport = nullptr;
    QWidget* m_talaEdGridWidget = nullptr;
    QGridLayout* m_talaEdGridLayout = nullptr;
    TalaTheka m_editingTala;
    std::vector<QComboBox*> m_talaBolCombos;
    std::vector<QComboBox*> m_talaKartalCombos;
    std::vector<QLabel*> m_talaMatraHeaders;
};

#endif // MAINWINDOW_H