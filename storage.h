#ifndef STORAGE_H
#define STORAGE_H

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "drum_presets.h"
#include <vector>
#include <string>

inline void initFileSystem() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed - Formatting...");
    }
}


inline const char* mapStaticString(const char* input, const char* const* pool, int poolSize) {
    if (!input) return "-";
    for (int i = 0; i < poolSize; ++i) {
        if (strcasecmp(input, pool[i]) == 0) return pool[i];
    }
    return "-"; 
}


inline std::vector<std::string> listBankFiles() {
    std::vector<std::string> files;
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        std::string fname = file.name();
        if (fname.find(".mhb") != std::string::npos) files.push_back(fname);
        file = root.openNextFile();
    }
    return files;
}

inline bool saveBankToFile(const char* filename, int curTala, int curSong) {
    File file = LittleFS.open(filename, "w");
    if (!file) return false;

    JsonDocument doc;
    doc["format"] = "HarmoniumBoxFormat_v3";
    doc["currentTalaIdx"] = curTala;
    doc["currentHarmSongIdx"] = curSong;

    JsonArray tArr = doc["talaThekas"].to<JsonArray>();
    for (const auto& tala : BuiltInPresets::getTalaThekas()) {
        JsonObject obj = tArr.add<JsonObject>();
        obj["name"] = tala.name;
        obj["divisions"] = tala.divisions;
        JsonArray bols = obj["bolCells"].to<JsonArray>();
        JsonArray kars = obj["kartalCells"].to<JsonArray>();
        for (const char* b : tala.bolCells) bols.add(b);
        for (const char* k : tala.kartalCells) kars.add(k);
    }

    JsonArray sArr = doc["harmoniumSongs"].to<JsonArray>();
    for (const auto& song : BuiltInPresets::getHarmoniumSongs()) {
        JsonObject obj = sArr.add<JsonObject>();
        obj["title"] = song.title;
        obj["ragaName"] = song.ragaName;
        obj["divisions"] = song.divisions;
        obj["defaultBpm"] = song.defaultBpm;
        JsonArray formsArr = obj["forms"].to<JsonArray>();
        for (const auto& f : song.forms) {
            JsonObject fo = formsArr.add<JsonObject>();
            fo["formName"] = f.formName;
            fo["numRows"] = f.numRows;
            JsonArray swaras = fo["swaraCells"].to<JsonArray>();
            for (const char* s : f.swaraCells) swaras.add(s);
        }
    }
    serializeJson(doc, file);
    file.close();
    return true;
}

inline bool loadBankFromFile(const char* filename, int& outTalaIdx, int& outSongIdx) {
    File file = LittleFS.open(filename, "r");
    if (!file) return false;

    JsonDocument doc;
    if (deserializeJson(doc, file)) { file.close(); return false; }

    outTalaIdx = doc["currentTalaIdx"] | 0;
    outSongIdx = doc["currentHarmSongIdx"] | 0;

    static const char* bolPool[] = {
        "-", "Dha", "Dhin", "Ge", "Ka", "Na", "Ta", "Ti", "Re", "Ti Re",
        "Ka Ta", "Ta Dhin", "Dha Ti Ra Ki", "Ta Ta Ta Ti", "Ra Ki Ta Ta",
        "Ka Ti Ra Ki", "Ta Dha Dhin -", "Dha - Dhin -", "Dha - - -",
        "- - - -", "Ra", "Ki"
    };
    static const char* karPool[] = {"-", "Kartal Open", "Kartal Closed", "Closed+Closed", "Manjira", "Ghanta", "Kansa", "Damaru", "Shankha"};
    static const char* swaraPool[] = {"-", ".", "'P", "'d", "'D", "'n", "'N", "S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N", "S'", "r'", "R'", "g'"};

    auto& activeTalas = const_cast<std::vector<TalaTheka>&>(BuiltInPresets::getTalaThekas());
    if (doc["talaThekas"].is<JsonArray>()) {
        activeTalas.clear();
        for (JsonObject obj : doc["talaThekas"].as<JsonArray>()) {
            TalaTheka t;
            t.name = strdup(obj["name"] | "Custom Tala");
            t.divisions = strdup(obj["divisions"] | "4+4");
            for (JsonVariant v : obj["bolCells"].as<JsonArray>()) t.bolCells.push_back(mapStaticString(v.as<const char*>(), bolPool, 22));
            for (JsonVariant v : obj["kartalCells"].as<JsonArray>()) t.kartalCells.push_back(mapStaticString(v.as<const char*>(), karPool, 9));
            activeTalas.push_back(t);
        }
    }

    auto& activeSongs = const_cast<std::vector<HarmoniumSong>&>(BuiltInPresets::getHarmoniumSongs());
    if (doc.containsKey("harmoniumSongs")) {
        activeSongs.clear();
        for (JsonObject obj : doc["harmoniumSongs"].as<JsonArray>()) {
            HarmoniumSong s;
            s.title = strdup(obj["title"] | "Custom Song");
            s.ragaName = strdup(obj["ragaName"] | "Raga");
            s.divisions = strdup(obj["divisions"] | "4+4");
            s.defaultBpm = obj["defaultBpm"] | 130;
            for (JsonObject fo : obj["forms"].as<JsonArray>()) {
                SongForm f;
                f.formName = strdup(fo["formName"] | "Form");
                f.numRows = fo["numRows"] | 2;
                for (JsonVariant v : fo["swaraCells"].as<JsonArray>()) f.swaraCells.push_back(mapStaticString(v.as<const char*>(), swaraPool, 23));
                s.forms.push_back(f);
            }
            activeSongs.push_back(s);
        }
    }
    file.close();
    return true;
}
#endif // STORAGE_H
