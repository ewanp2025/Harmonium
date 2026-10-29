#include "projectio.h"
#include "drum_presets.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QtXml/QDomDocument>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <set>

QVector<int> ProjectIO::parseVibhagDivisions(const QString& divStr, int* outTotalMatras) {
    QVector<int> vibhags;
    int total = 0;
    QStringList parts = divStr.split('+', Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        int v = p.trimmed().toInt();
        if (v > 0 && v <= 16) {
            vibhags.push_back(v);
            total += v;
        }
    }
    if (vibhags.isEmpty()) {
        vibhags = {4, 4};
        total = 8;
    }
    if (outTotalMatras) *outTotalMatras = total;
    return vibhags;
}

QStringList ProjectIO::computeVibhagTaliKhaliLabels(const QVector<int>& vibhags) {
    QStringList labels;
    int n = vibhags.size();
    int taliNum = 2;
    for (int i = 0; i < n; ++i) {
        if (i == 0) labels.push_back("X");
        else if (n == 4 && i == 2) labels.push_back("0");
        else if (n == 2 && i == 1) labels.push_back("0");
        else labels.push_back(QString::number(taliNum++));
    }
    return labels;
}

// Parses Swara notation: 'n (lower), S (middle), S' (upper) -> MIDI key (where S = 60)
bool ProjectIO::parseSwaraToken(const QString& token, int& outMidiKey) {
    QString s = token.trimmed();
    if (s.isEmpty() || s == "-" || s == ".") return false;

    int octaveShift = 0;
    while (s.startsWith('\'')) {
        octaveShift -= 12;
        s.remove(0, 1);
    }
    while (s.endsWith('\'')) {
        octaveShift += 12;
        s.chop(1);
    }
    if (s.isEmpty()) return false;

    int semi = -1;
    QChar c = s[0];
    if (c == 'S' || c == 's') semi = 0;
    else if (c == 'r') semi = 1;
    else if (c == 'R') semi = 2;
    else if (c == 'g') semi = 3;
    else if (c == 'G') semi = 4;
    else if (c == 'm') semi = 5;
    else if (c == 'M') semi = 6;
    else if (c == 'P' || c == 'p') semi = 7;
    else if (c == 'd') semi = 8;
    else if (c == 'D') semi = 9;
    else if (c == 'n') semi = 10;
    else if (c == 'N') semi = 11;

    if (semi < 0) return false;
    outMidiKey = 60 + octaveShift + semi;
    return true;
}

static QString midiKeyToSwaraToken(int midiKey) {
    static const char* SWARA_TABLE[12] = {"S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N"};
    int rel = midiKey - 60;
    int semi = ((rel % 12) + 12) % 12;
    int oct = static_cast<int>(std::floor(rel / 12.0));
    QString tok = SWARA_TABLE[semi];
    if (oct < 0) tok = "'" + tok;
    else if (oct > 0) tok = tok + "'";
    return tok;
}

HarmoniumSong ProjectIO::generateRandomRagaSong(const HarmoniumSong& baseSong) {
    HarmoniumSong out = baseSong;
    auto* rng = QRandomGenerator::global();

    // Extract active pitch classes (0..11) from the base Raga composition
    std::set<int> pitchClasses = {0}; // Always include Sa (0)
    for (const auto& f : baseSong.forms) {
        for (const QString& cell : f.swaraCells) {
            QStringList subs = cell.split(' ', Qt::SkipEmptyParts);
            for (const QString& sub : subs) {
                int k = 60;
                if (parseSwaraToken(sub, k)) {
                    pitchClasses.insert(((k - 60) % 12 + 12) % 12);
                }
            }
        }
    }
    // Fallback to Bilawal scale if fewer than 5 notes were found
    if (pitchClasses.size() < 5) {
        pitchClasses = {0, 2, 4, 5, 7, 9, 11};
    }

    // Build a multi-octave scale ladder from Mandra 'P (55) to Tara G' (76)
    std::vector<int> ladder;
    for (int midi = 55; midi <= 76; ++midi) {
        int pc = ((midi - 60) % 12 + 12) % 12;
        if (pitchClasses.count(pc)) {
            ladder.push_back(midi);
        }
    }

    auto findClosestIdx = [&](int targetMidi) -> int {
        int bestIdx = 0;
        int bestDist = 999;
        for (int i = 0; i < static_cast<int>(ladder.size()); ++i) {
            int d = std::abs(ladder[i] - targetMidi);
            if (d < bestDist) {
                bestDist = d;
                bestIdx = i;
            }
        }
        return bestIdx;
    };

    int saIdx = findClosestIdx(60);
    int paIdx = findClosestIdx(67);
    int highSaIdx = findClosestIdx(72);

    int totalMatras = 0;
    QVector<int> vibhags = parseVibhagDivisions(out.divisions, &totalMatras);
    if (out.forms.empty()) {
        out.forms = {{"Form A (Sthayi)", 4, {}}, {"Form B (Antara)", 4, {}}};
    }

    for (size_t fIdx = 0; fIdx < out.forms.size(); ++fIdx) {
        SongForm& form = out.forms[fIdx];
        bool isAntara = (fIdx > 0);
        int numRows = std::max(2, form.numRows);
        form.swaraCells.clear();

        // Register bounds for Sthayi vs Antara
        int minIdx = isAntara ? std::max(0, saIdx + 1) : 0;
        int maxIdx = isAntara ? static_cast<int>(ladder.size()) - 1 : std::min(static_cast<int>(ladder.size()) - 1, highSaIdx);
        int curIdx = isAntara ? paIdx : saIdx;

        for (int r = 0; r < numRows; ++r) {
            bool isLastRow = (r == numRows - 1);
            int matraInRow = 0;

            for (int v = 0; v < vibhags.size(); ++v) {
                int vLen = vibhags[v];
                for (int m = 0; m < vLen; ++m) {
                    int remainingInRow = totalMatras - 1 - matraInRow;

                    // Final cadence of the Form: resolve smoothly to Madhya Sa (or Tara Sa in Antara row 0)
                    if (isLastRow && remainingInRow == 0) {
                        form.swaraCells.push_back("-");
                    } else if (isLastRow && remainingInRow == 1) {
                        curIdx = saIdx;
                        form.swaraCells.push_back("S");
                    } else if (isLastRow && remainingInRow == 2) {
                        curIdx = std::clamp(saIdx + (rng->bounded(2) == 0 ? 1 : -1), 0, static_cast<int>(ladder.size()) - 1);
                        form.swaraCells.push_back(midiKeyToSwaraToken(ladder[curIdx]));
                    } else if (isAntara && r == 0 && v == 1 && m == 0) {
                        // Antara signature: reach and sustain Tara Sa (S') in the first row
                        curIdx = highSaIdx;
                        form.swaraCells.push_back("S'");
                    } else if (isAntara && r == 0 && v == 1 && m == 1) {
                        form.swaraCells.push_back("-");
                    } else {
                        int roll = rng->bounded(100);
                        // Sustain '-' on non-initial beat of a vibhag (22% chance)
                        if (m > 0 && roll < 22) {
                            form.swaraCells.push_back("-");
                        } else if (m > 0 && roll >= 22 && roll < 34) {
                            // Graceful 2-note ornamentation (Alankar/Murki) on weak beat
                            int step1 = (rng->bounded(2) == 0) ? 1 : -1;
                            int idx1 = std::clamp(curIdx + step1, minIdx, maxIdx);
                            int idx2 = std::clamp(idx1 + ((rng->bounded(2) == 0) ? 1 : -1), minIdx, maxIdx);
                            curIdx = idx2;
                            form.swaraCells.push_back(midiKeyToSwaraToken(ladder[idx1]) + " " + midiKeyToSwaraToken(ladder[idx2]));
                        } else {
                            // Stepwise melodic motion (80% step of +/-1, 20% leap of +/-2)
                            int delta = 0;
                            int dirRoll = rng->bounded(100);
                            if (dirRoll < 42) delta = 1;
                            else if (dirRoll < 84) delta = -1;
                            else if (dirRoll < 92) delta = 2;
                            else delta = -2;

                            // Gently bias towards high Sa in early Antara, or towards Madhya Sa in final row
                            if (isAntara && r == 0 && curIdx < highSaIdx) delta = 1;
                            if (isLastRow && curIdx > saIdx + 1) delta = -1;

                            curIdx = std::clamp(curIdx + delta, minIdx, maxIdx);
                            form.swaraCells.push_back(midiKeyToSwaraToken(ladder[curIdx]));
                        }
                    }
                    matraInRow++;
                }
            }
        }
    }

    QString baseName = out.ragaName.isEmpty() ? out.title : out.ragaName;
    out.title = QString("%1 - Gen #%2").arg(baseName).arg(rng->bounded(100, 999));
    return out;
}

TalaTheka ProjectIO::generateRandomTalaTheka(const TalaTheka& baseTala) {
    TalaTheka out = baseTala;
    auto* rng = QRandomGenerator::global();

    int totalMatras = 0;
    QVector<int> vibhags = parseVibhagDivisions(out.divisions, &totalMatras);
    QStringList taliLabels = computeVibhagTaliKhaliLabels(vibhags);

    static const QStringList SAM_BOLS = {"Dha", "Dhin", "Dha", "Dhin"};
    static const QStringList TALI_START_BOLS = {"Dha", "Dhin", "Ge", "Dha"};
    static const QStringList TALI_BODY_BOLS = {"Dhin", "Dha", "Ge", "Na", "Ti", "Ta", "Dha Ge", "Ta Dhin", "Ge Ti"};
    static const QStringList KHALI_START_BOLS = {"Ti", "Ta", "Na", "Ka"};
    static const QStringList KHALI_BODY_BOLS = {"Ti", "Ta", "Na", "Ka", "Ti Re", "Ka Ti", "Ta Ti", "-"};
    static const QStringList CADENCE_PENULTIMATE = {"Ti Re", "Dha Ge", "Ti Re Ka Ta", "Ka Dha", "Ta Dhin"};
    static const QStringList CADENCE_FINAL = {"Na", "Ta", "Dha", "Dhin", "Ti Re Ka Ta"};

    out.bolCells.clear();
    out.kartalCells.clear();

    int globalMatra = 0;
    for (int v = 0; v < vibhags.size(); ++v) {
        int vLen = vibhags[v];
        bool isSamVibhag = (v == 0);
        bool isKhaliVibhag = (taliLabels[v] == "0");

        for (int m = 0; m < vLen; ++m) {
            int remaining = totalMatras - 1 - globalMatra;
            QString bol = "Dha";
            QString kartal = "Kartal Closed";

            if (isSamVibhag && m == 0) {
                // Sam (Beat 1): Heavy resonant stroke + Kartal Open
                bol = SAM_BOLS[rng->bounded(SAM_BOLS.size())];
                kartal = "Kartal Open";
            } else if (remaining == 0) {
                // Final Matra of cycle: Cadential pickup into Sam
                bol = CADENCE_FINAL[rng->bounded(CADENCE_FINAL.size())];
                kartal = bol.contains(' ') ? "Closed+Closed" : "Kartal Closed";
            } else if (remaining == 1 && totalMatras >= 6 && rng->bounded(100) < 65) {
                // Penultimate Matra: Tirakita / Dha Ge cadence setup
                bol = CADENCE_PENULTIMATE[rng->bounded(CADENCE_PENULTIMATE.size())];
                kartal = bol.contains(' ') ? "Closed+Closed" : "Kartal Closed";
            } else if (m == 0) {
                // Start of a Vibhag (Tali vs Khali)
                if (isKhaliVibhag) {
                    bol = KHALI_START_BOLS[rng->bounded(KHALI_START_BOLS.size())];
                    kartal = "Kartal Closed";
                } else {
                    bol = TALI_START_BOLS[rng->bounded(TALI_START_BOLS.size())];
                    kartal = "Kartal Open";
                }
            } else {
                // Inner beats of a Vibhag
                if (isKhaliVibhag) {
                    // Second half of Khali vibhag can re-introduce bass (Baya) to build towards next Tali
                    if (m >= vLen / 2 && rng->bounded(100) < 55) {
                        bol = TALI_BODY_BOLS[rng->bounded(TALI_BODY_BOLS.size())];
                    } else {
                        bol = KHALI_BODY_BOLS[rng->bounded(KHALI_BODY_BOLS.size())];
                    }
                    kartal = (rng->bounded(100) < 45) ? "-" : "Kartal Closed";
                } else {
                    bol = TALI_BODY_BOLS[rng->bounded(TALI_BODY_BOLS.size())];
                    if (bol.contains(' ')) kartal = "Closed+Closed";
                    else kartal = (rng->bounded(100) < 25) ? "-" : "Kartal Closed";
                }
            }

            out.bolCells.push_back(bol);
            out.kartalCells.push_back(kartal);
            globalMatra++;
        }
    }

    out.name = QString("Prakar %1M #%2").arg(totalMatras).arg(rng->bounded(100, 999));
    return out;
}

void ProjectIO::compileTrackAsts(MmpTrack& trk) {
    int cO1 = 0, cO2 = 0, cW1 = 0, cW2 = 0, cW3 = 0;
    try { trk.w1Ast = XpressiveParser::parse(trk.w1Str.toStdString(), &cW1); } catch (...) { trk.w1Ast.reset(); }
    try { trk.w2Ast = XpressiveParser::parse(trk.w2Str.toStdString(), &cW2); } catch (...) { trk.w2Ast.reset(); }
    try { trk.w3Ast = XpressiveParser::parse(trk.w3Str.toStdString(), &cW3); } catch (...) { trk.w3Ast.reset(); }
    try { trk.o1Ast = XpressiveParser::parse(trk.o1Str.toStdString(), &cO1); } catch (...) { trk.o1Ast.reset(); }
    try { trk.o2Ast = XpressiveParser::parse(trk.o2Str.toStdString(), &cO2); } catch (...) { trk.o2Ast.reset(); }
    trk.numIntegrators = std::max(8, cO1 + cO2 + cW1 + cW2 + cW3 + 4);
}

void ProjectIO::rebuildCompiledPatterns(MmpSong& song) {
    if (song.talaThekas.empty() || song.tracks.empty()) return;

    std::map<QString, int> trkMap;
    int harmTrackIdx = -1;
    int shankhaTrackIdx = -1;
    for (size_t i = 0; i < song.tracks.size(); ++i) {
        trkMap[song.tracks[i].name.toLower()] = static_cast<int>(i);
        if (!song.tracks[i].isBeatTrack && song.tracks[i].name.contains("Harmonium", Qt::CaseInsensitive)) {
            harmTrackIdx = static_cast<int>(i);
        }
        if (!song.tracks[i].isBeatTrack && shankhaTrackIdx < 0 && song.tracks[i].name.contains("Shankha", Qt::CaseInsensitive)) {
            shankhaTrackIdx = static_cast<int>(i);
        }
    }

    // Compile the selected Harmonium Song into SongNote events
    std::vector<SongNote> compiledHarmNotes;
    int totalSongTicks = 768;
    if (!song.harmoniumSongs.empty()) {
        int sIdx = std::clamp(song.currentHarmSongIdx, 0, static_cast<int>(song.harmoniumSongs.size()) - 1);
        const HarmoniumSong& hs = song.harmoniumSongs[sIdx];
        int matrasPerRow = 0;
        parseVibhagDivisions(hs.divisions, &matrasPerRow);

        int currentMatraGlobal = 0;
        for (const SongForm& form : hs.forms) {
            int formCells = form.numRows * matrasPerRow;
            for (int c = 0; c < formCells; ++c) {
                QString cellText = (c < form.swaraCells.size()) ? form.swaraCells[c].trimmed() : "-";
                int cellStartTick = currentMatraGlobal * 48;

                if (cellText == "-" || cellText.isEmpty()) {
                    if (!compiledHarmNotes.empty()) {
                        compiledHarmNotes.back().lenTicks += 48;
                    }
                } else if (cellText != ".") {
                    QStringList subTokens = cellText.split(' ', Qt::SkipEmptyParts);
                    if (subTokens.size() == 1) {
                        int midiKey = 60;
                        if (parseSwaraToken(subTokens[0], midiKey)) {
                            compiledHarmNotes.push_back({cellStartTick, 46, midiKey, 1.0f});
                        }
                    } else if (subTokens.size() >= 2) {
                        int subLen = 48 / static_cast<int>(subTokens.size());
                        for (int st = 0; st < subTokens.size(); ++st) {
                            if (subTokens[st] == "-" && !compiledHarmNotes.empty()) {
                                compiledHarmNotes.back().lenTicks += subLen;
                            } else {
                                int midiKey = 60;
                                if (parseSwaraToken(subTokens[st], midiKey)) {
                                    compiledHarmNotes.push_back({cellStartTick + st * subLen, subLen - 2, midiKey, 1.0f});
                                }
                            }
                        }
                    }
                }
                currentMatraGlobal++;
            }
        }
        totalSongTicks = std::max(192, currentMatraGlobal * 48);
    }

    // Compile each Tala into TalaPattern
    song.talas.clear();
    for (const TalaTheka& kt : song.talaThekas) {
        int talaMatras = 0;
        parseVibhagDivisions(kt.divisions, &talaMatras);

        TalaPattern tp;
        tp.name = kt.name;
        tp.drumLoopTicks = std::max(48, talaMatras * 48);
        tp.loopTicks = std::max(tp.drumLoopTicks, totalSongTicks);
        tp.trackNotes.resize(song.tracks.size());

        auto addHitByName = [&](const QString& bolName, int tick, int len) {
            QString key = bolName.trimmed().toLower();
            if (key.isEmpty() || key == "-") return;
            auto it = trkMap.find(key);
            if (it != trkMap.end()) {
                tp.trackNotes[it->second].push_back({tick, len, 69, 1.0f});
            }
        };

        for (int m = 0; m < talaMatras; ++m) {
            int matraTick = m * 48;
            if (m < kt.bolCells.size()) {
                QString bolCell = kt.bolCells[m].trimmed();
                if (!bolCell.isEmpty() && bolCell != "-") {
                    QStringList subBols = bolCell.split(' ', Qt::SkipEmptyParts);
                    int subLen = 48 / std::max(1, static_cast<int>(subBols.size()));
                    for (int sb = 0; sb < subBols.size(); ++sb) {
                        addHitByName(subBols[sb], matraTick + sb * subLen, subLen);
                    }
                }
            }
            if (m < kt.kartalCells.size()) {
                QString kCell = kt.kartalCells[m].trimmed();
                if (kCell == "Closed+Closed") {
                    addHitByName("Kartal Closed", matraTick, 24);
                    addHitByName("Kartal Closed", matraTick + 24, 24);
                } else if (!kCell.isEmpty() && kCell != "-") {
                    addHitByName(kCell, matraTick, 48);
                }
            }
        }

        if (harmTrackIdx >= 0) {
            tp.trackNotes[harmTrackIdx] = compiledHarmNotes;
        }
        if (shankhaTrackIdx >= 0) {
            tp.trackNotes[shankhaTrackIdx] = {{0, 240, 60, 1.0f}};
        }
        song.talas.push_back(std::move(tp));
    }

    int activeIdx = std::clamp(song.currentTalaIdx, 0, static_cast<int>(song.talas.size()) - 1);
    song.currentTalaIdx = activeIdx;
    song.totalLoopTicks = song.talas[activeIdx].loopTicks;
    song.drumLoopTicks = song.talas[activeIdx].drumLoopTicks;
    for (size_t t = 0; t < song.tracks.size() && t < song.talas[activeIdx].trackNotes.size(); ++t) {
        song.tracks[t].notes = song.talas[activeIdx].trackNotes[t];
    }
}

bool ProjectIO::loadFile(const QString& filePath, MmpSong& outSong) {
    if (filePath.endsWith(".mhb", Qt::CaseInsensitive)) {
        return loadMhbFile(filePath, outSong);
    }
    return loadMmpFile(filePath, outSong);
}

bool ProjectIO::saveMhbFile(const QString& filePath, const MmpSong& song) {
    QJsonObject root;
    root["format"] = "HarmoniumBoxFormat_v3";
    root["title"] = song.title;
    root["bpm"] = song.bpm;
    root["saSemitone"] = song.saSemitone;
    root["a4Hz"] = song.a4Hz;
    root["currentTalaIdx"] = song.currentTalaIdx;
    root["currentHarmSongIdx"] = song.currentHarmSongIdx;

    QJsonArray talaThekasArr;
    for (const auto& kt : song.talaThekas) {
        QJsonObject ko;
        ko["name"] = kt.name;
        ko["divisions"] = kt.divisions;
        ko["bolCells"] = QJsonArray::fromStringList(kt.bolCells);
        ko["kartalCells"] = QJsonArray::fromStringList(kt.kartalCells);
        talaThekasArr.append(ko);
    }
    root["talaThekas"] = talaThekasArr;

    QJsonArray harmoniumSongsArr;
    for (const auto& ks : song.harmoniumSongs) {
        QJsonObject so;
        so["title"] = ks.title;
        so["ragaName"] = ks.ragaName;
        so["divisions"] = ks.divisions;
        so["defaultBpm"] = ks.defaultBpm;
        QJsonArray formsArr;
        for (const auto& f : ks.forms) {
            QJsonObject fo;
            fo["formName"] = f.formName;
            fo["numRows"] = f.numRows;
            fo["swaraCells"] = QJsonArray::fromStringList(f.swaraCells);
            formsArr.append(fo);
        }
        so["forms"] = formsArr;
        harmoniumSongsArr.append(so);
    }
    root["harmoniumSongs"] = harmoniumSongsArr;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ProjectIO::loadMhbFile(const QString& filePath, MmpSong& outSong) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    outSong = BuiltInPresets::createDefaultTalaBank();
    outSong.title = root["title"].toString(QFileInfo(filePath).baseName());
    outSong.bpm = root["bpm"].toInt(140);
    outSong.saSemitone = root["saSemitone"].toInt(0);
    outSong.a4Hz = root["a4Hz"].toDouble(440.0);
    outSong.currentTalaIdx = root["currentTalaIdx"].toInt(0);
    outSong.currentHarmSongIdx = root["currentHarmSongIdx"].toInt(0);

    if (root.contains("talaThekas")) {
        outSong.talaThekas.clear();
        QJsonArray tArr = root["talaThekas"].toArray();
        for (int i = 0; i < tArr.size(); ++i) {
            QJsonObject ko = tArr[i].toObject();
            TalaTheka kt;
            kt.name = ko["name"].toString();
            kt.divisions = ko["divisions"].toString("4+4");
            for (const auto& v : ko["bolCells"].toArray()) kt.bolCells.push_back(v.toString());
            for (const auto& v : ko["kartalCells"].toArray()) kt.kartalCells.push_back(v.toString());
            outSong.talaThekas.push_back(std::move(kt));
        }
    }

    if (root.contains("harmoniumSongs")) {
        outSong.harmoniumSongs.clear();
        QJsonArray sArr = root["harmoniumSongs"].toArray();
        for (int i = 0; i < sArr.size(); ++i) {
            QJsonObject so = sArr[i].toObject();
            HarmoniumSong ks;
            ks.title = so["title"].toString();
            ks.ragaName = so["ragaName"].toString();
            ks.divisions = so["divisions"].toString("4+4+4+4");
            ks.defaultBpm = so["defaultBpm"].toInt(130);
            QJsonArray fArr = so["forms"].toArray();
            for (int f = 0; f < fArr.size(); ++f) {
                QJsonObject fo = fArr[f].toObject();
                SongForm kf;
                kf.formName = fo["formName"].toString();
                kf.numRows = fo["numRows"].toInt(1);
                for (const auto& sv : fo["swaraCells"].toArray()) kf.swaraCells.push_back(sv.toString());
                ks.forms.push_back(std::move(kf));
            }
            outSong.harmoniumSongs.push_back(std::move(ks));
        }
    }

    rebuildCompiledPatterns(outSong);
    return !outSong.tracks.empty();
}

bool ProjectIO::loadMmpFile(const QString& filePath, MmpSong& outSong) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QDomDocument doc;
    if (!doc.setContent(&file)) return false;

    outSong = BuiltInPresets::createDefaultTalaBank();
    outSong.title = QFileInfo(filePath).baseName();
    QDomElement head = doc.documentElement().firstChildElement("head");
    outSong.bpm = head.attribute("bpm", "140").toInt();
    return true;
}

static QString escapeCppString(QString s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", " ");
    s.replace("\r", "");
    return s;
}

bool ProjectIO::exportDrumPresetsHeader(const QString& filePath, const MmpSong& song) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);

    out << "#ifndef DRUM_PRESETS_H\n#define DRUM_PRESETS_H\n\n#include \"projectio.h\"\n\n";
    out << "namespace BuiltInPresets {\n\n";
    out << "inline QString defaultHarmoniumO1() { return \"" << escapeCppString(BuiltInPresets::defaultHarmoniumO1()) << "\"; }\n";
    out << "inline QString shrutiDroneO1() { return \"" << escapeCppString(BuiltInPresets::shrutiDroneO1()) << "\"; }\n";
    out << "inline QString shrutiDroneO2() { return \"" << escapeCppString(BuiltInPresets::shrutiDroneO2()) << "\"; }\n";
    out << "inline QString tanpuraDroneO1() { return \"" << escapeCppString(BuiltInPresets::tanpuraDroneO1()) << "\"; }\n";
    out << "inline QString tanpuraDroneO2() { return \"" << escapeCppString(BuiltInPresets::tanpuraDroneO2()) << "\"; }\n\n";

    out << "inline MmpSong createDefaultTalaBank() {\n";
    out << "    MmpSong s;\n";
    out << "    s.title = \"" << escapeCppString(song.title) << "\";\n";
    out << "    s.bpm = " << song.bpm << ";\n";
    out << "    s.saSemitone = " << song.saSemitone << ";\n";
    out << "    s.a4Hz = " << song.a4Hz << ";\n\n";
    out << "    auto addInst = [&](const QString& name, bool isBeat, const QString& o1, const QString& o2,\n";
    out << "                       bool useAdsr = false, double dec = 0.5, double sus = 0.5, double rel = 0.1, const QString& tfStr = \"\") {\n";
    out << "        MmpTrack t; t.name = name; t.isBeatTrack = isBeat; t.o1Str = o1; t.o2Str = o2; t.tfStr = tfStr;\n";
    out << "        t.useAdsr = useAdsr; t.dec = dec; t.sus = sus; t.rel = rel;\n";
    out << "        ProjectIO::compileTrackAsts(t); s.tracks.push_back(std::move(t));\n    };\n\n";

    for (const auto& t : song.tracks) {
        out << "    addInst(\"" << escapeCppString(t.name) << "\", "
            << (t.isBeatTrack ? "true" : "false") << ",\n"
            << "        \"" << escapeCppString(t.o1Str) << "\",\n"
            << "        \"" << escapeCppString(t.o2Str) << "\", "
            << (t.useAdsr ? "true" : "false") << ", " << t.dec << ", " << t.sus << ", " << t.rel << ", \"" << escapeCppString(t.tfStr) << "\");\n\n";
    }

    out << "    // Tala Definitions\n    s.talaThekas = {\n";
    for (size_t i = 0; i < song.talaThekas.size(); ++i) {
        const auto& kt = song.talaThekas[i];
        out << "        {\n            \"" << escapeCppString(kt.name) << "\", \"" << escapeCppString(kt.divisions) << "\",\n            {";
        for (int b = 0; b < kt.bolCells.size(); ++b) {
            out << "\"" << escapeCppString(kt.bolCells[b]) << "\"" << (b + 1 < kt.bolCells.size() ? ", " : "");
        }
        out << "},\n            {";
        for (int k = 0; k < kt.kartalCells.size(); ++k) {
            out << "\"" << escapeCppString(kt.kartalCells[k]) << "\"" << (k + 1 < kt.kartalCells.size() ? ", " : "");
        }
        out << "}\n        }" << (i + 1 < song.talaThekas.size() ? "," : "") << "\n";
    }
    out << "    };\n\n";

    out << "    // Harmonium Songs\n    s.harmoniumSongs = {\n";
    for (size_t i = 0; i < song.harmoniumSongs.size(); ++i) {
        const auto& ks = song.harmoniumSongs[i];
        out << "        {\n            \"" << escapeCppString(ks.title) << "\", \"" << escapeCppString(ks.ragaName)
            << "\", \"" << escapeCppString(ks.divisions) << "\", " << ks.defaultBpm << ",\n            {\n";
        for (size_t f = 0; f < ks.forms.size(); ++f) {
            const auto& kf = ks.forms[f];
            out << "                {\n                    \"" << escapeCppString(kf.formName) << "\", " << kf.numRows << ",\n                    {";
            for (int c = 0; c < kf.swaraCells.size(); ++c) {
                out << "\"" << escapeCppString(kf.swaraCells[c]) << "\"" << (c + 1 < kf.swaraCells.size() ? ", " : "");
            }
            out << "}\n                }" << (f + 1 < ks.forms.size() ? "," : "") << "\n";
        }
        out << "            }\n        }" << (i + 1 < song.harmoniumSongs.size() ? "," : "") << "\n";
    }
    out << "    };\n\n";
    out << "    ProjectIO::rebuildCompiledPatterns(s);\n    return s;\n}\n\n} // namespace BuiltInPresets\n\n#endif // DRUM_PRESETS_H\n";
    return true;
}

MmpSong ProjectIO::createBuiltInDefaultSong() {
    return BuiltInPresets::createDefaultTalaBank();
}