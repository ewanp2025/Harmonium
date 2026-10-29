#ifndef PROJECTIO_H
#define PROJECTIO_H

#include <QString>
#include <QStringList>
#include <vector>
#include <memory>
#include "xpressive_parser.h"
#include "effect.h"

struct SongNote {
    int startTick = 0;   // 48 ticks = 1 Matra (beat), 24 ticks = 1/2 Matra
    int lenTicks = 48;
    int key = 69;
    float vol = 1.0f;
};

struct TrackFilterSpec {
    bool isHighpass = false;
    double cutoffHz = 1000.0;
};

struct MmpTrack {
    QString name;
    bool isBeatTrack = false;
    bool muted = false;
    double vol = 1.0;
    double pan = 0.0;
    int baseNote = 69;
    double A1 = 1.0, A2 = 1.0, A3 = 1.0;
    double pan1 = 1.0, pan2 = -1.0;

    QString w1Str, w2Str, w3Str, o1Str, o2Str, tfStr;
    std::vector<float> w1Sample, w2Sample, w3Sample;

    bool useAdsr = false;
    double att = 0.0, dec = 0.5, sus = 0.5, rel = 0.1;

    std::vector<TrackFilterSpec> fxFilters;
    std::vector<SongNote> notes;

    std::shared_ptr<XpressiveParser::Node> o1Ast, o2Ast, w1Ast, w2Ast, w3Ast;
    int numIntegrators = 0;
};

// Tala Theka structure (Vibhags + Matra Bol cells)
struct TalaTheka {
    QString name;
    QString divisions = "4+4";          // Vibhag lengths in Matras
    QStringList bolCells;               // 1 entry per Matra: e.g. "Dha", "Dhin", "Ti Re", "-"
    QStringList kartalCells;            // 1 entry per Matra: e.g. "Kartal Open", "Kartal Closed", "-"
};

// Harmonium Form (e.g., Form A / Sthayi, Form B / Antara)
struct SongForm {
    QString formName = "Form A";
    int numRows = 2;                    // Number of Avartans (rows) in this Form
    QStringList swaraCells;             // numRows * totalMatras entries: e.g. "S", "'n", "R", "g", "-"
};

struct HarmoniumSong {
    QString title;
    QString ragaName;
    QString divisions = "4+4";          // Vibhag lengths in Matras
    int defaultBpm = 130;
    std::vector<SongForm> forms;
};

struct TalaPattern {
    QString name = "Tala";
    int loopTicks = 3072;
    int drumLoopTicks = 384;
    std::vector<std::vector<SongNote>> trackNotes;
};

struct MmpSong {
    QString title = "Harmonium Preset Bank";
    int bpm = 140;
    int saSemitone = 0;
    double a4Hz = 440.0;
    int currentTalaIdx = 0;
    int currentHarmSongIdx = 0;
    int totalLoopTicks = 3072;
    int drumLoopTicks = 384;

    std::vector<MmpTrack> tracks;
    std::vector<TalaPattern> talas;

    // Song & Tala storage
    std::vector<TalaTheka> talaThekas;
    std::vector<HarmoniumSong> harmoniumSongs;
};

class ProjectIO {
public:
    static QVector<int> parseVibhagDivisions(const QString& divStr, int* outTotalMatras = nullptr);
    static QStringList computeVibhagTaliKhaliLabels(const QVector<int>& vibhags);
    static bool parseSwaraToken(const QString& token, int& outMidiKey);

    // Intelligent Raga & Tala Randomisers
    static HarmoniumSong generateRandomRagaSong(const HarmoniumSong& baseSong);
    static TalaTheka generateRandomTalaTheka(const TalaTheka& baseTala);

    static void rebuildCompiledPatterns(MmpSong& song);
    static bool loadFile(const QString& filePath, MmpSong& outSong);
    static bool loadMmpFile(const QString& filePath, MmpSong& outSong);
    static bool loadMhbFile(const QString& filePath, MmpSong& outSong);
    static bool saveMhbFile(const QString& filePath, const MmpSong& song);
    static bool exportDrumPresetsHeader(const QString& filePath, const MmpSong& song);
    static void compileTrackAsts(MmpTrack& trk);
    static MmpSong createBuiltInDefaultSong();
};

#endif // PROJECTIO_H