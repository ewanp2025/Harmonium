#ifndef I18N_H
#define I18N_H

#include <QString>
#include <QStringList>
#include <vector>
#include <map>

namespace I18n {

struct LangPack {
    QString code;
    QString menuLabel; // Shown in the top-right language selector

    // Window & Tabs
    QString windowTitle;
    QString tabLiveBox;
    QString tabHarmEditor;
    QString tabTalaEditor;

    // Tab 1: Top Transport Bar
    QString btnStart;
    QString btnStop;
    QString btnPause;
    QString btnResume;
    QString chkCountdown;
    QString lblTala;
    QString lblSong;
    QString btnLoad;
    QString btnSave;

    // Tab 1: Raga Scale & Drone Row
    QString swaraPopupFmt;      // e.g. "Swara: %1 (%2)"
    QString swaraPopupNone;     // e.g. "Swara: --"
    QString countdownFmt;       // e.g. "COUNTDOWN: %1"
    QString grpTunerTitle;
    QString saKeyPrefix;        // e.g. "Sa = %1"
    QString indianKeyLabels[12];
    QString btnDroneOff;
    QString btnDroneOn;
    QString dronePresetBoth;
    QString dronePresetShruti;
    QString dronePresetTanpura;
    QString droneSurSaPa;
    QString droneSurSaMa;
    QString droneSurSaNi;
    QString droneSurSaOnly;
    QString showAny;
    QString showKeys;
    QString showTrkPrefix;

    // Tab 1: Tala Instrument Grid
    QString grpTracksTitle;
    QString stateOn;
    QString stateOff;

    // Tab 1: Harmonium Mode & DSP Row
    QString grpHarmTitle;
    QString btnHarmPresetOn;
    QString btnHarmPresetOff;
    QString btnOctDown;
    QString btnOctUp;
    QString lblOctaveFmt;
    QString btnEqOn;
    QString btnEqOff;
    QString btnReverbOn;
    QString btnReverbOff;

    // Tab 1: Footer & Piano Swara Labels
    QString footerStatusFmt;    // "Song: %1 | Tala: %2 (%3 Matras)"
    QString midiScanning;
    QString midiNoUsbTouch;
    QString midiPcMode;
    QString midiConnectedFmt;
    QString midiAlsaUnavail;
    QString midiOnScreenMode;
    QString swaraNames[12];     // 12 Swara labels drawn on Piano & Raga Popup

    // Tab 2 & 3: Editors
    QString lblEdSong;
    QString btnNewSong;
    QString lblEdName;
    QString txtSongNamePlaceholder;
    QString lblEdVibhags;
    QString spinFormsPrefix;
    QString spinRowsPrefix;
    QString btnPlayInEd;
    QString btnApplySong;
    QString btnSaveMhb;
    QString btnLoadMhb;
    QString btnExportHeader;

    QString lblSelectTala;
    QString btnNewTala;
    QString lblTalaName;
    QString lblVibhagDivs;
    QString btnPlayTala;
    QString btnApplyTala;
    QString vibhagHeaderFmt;    // "Vibhag %1  (Tali/Khali: %2)"
    QString lblMatraCol;
    QString lblThekaBolCol;
    QString lblKartalCol;
    QString matraItemFmt;       // "Matra %1"

    // Map canonical instrument track names to localized display names
    std::map<QString, QString> trackDisplayNames;

    QString translateTrackName(const QString& canonical) const {
        auto it = trackDisplayNames.find(canonical);
        return (it != trackDisplayNames.end()) ? it->second : canonical;
    }
};

inline const std::vector<LangPack>& getLanguages() {
    static const std::vector<LangPack> langs = {

        // 0: ENGLISH

        {
            "en",
            "🌐 English",
            "Harmonium & Talamala Box (800x480)",
            "Harmonium Practice Live Box",
            "Harmonium Editor",
            "Tala Theka Editor",

            "▶ START",
            "⏹ STOP",
            "⏸ PAUSE",
            "▶ RESUME",
            "Count",
            "Tala:",
            "Song:",
            "Load",
            "Save",

            "Swara: %1 (%2)",
            "Swara: --",
            "COUNTDOWN: %1",
            "Raga Scale (Adhara Shadja) & Shruti / Tanpura Drone",
            "Sa = %1",
            {
                "C (Safed 1)", "C# (Kali 1)", "D (Safed 2)", "D# (Kali 2)",
                "E (Safed 3)", "F (Safed 4)", "F# (Kali 3)", "G (Safed 5)",
                "G# (Kali 4)", "A (Safed 6)", "A# (Kali 5)", "B (Safed 7)"
            },
            "DRONE: OFF",
            "DRONE: ON",
            "Shruti + Tanpura",
            "Shruti Drone Only",
            "Tanpura Drone Only",
            "Sur: Sa-Pa (S-P̣-Ṣ)",
            "Sur: Sa-Ma (S-ṃ-Ṣ)",
            "Sur: Sa-Ni (S-Ṇ-Ṣ)",
            "Sur: Sa Only (S-Ṣ)",
            "Show: Any",
            "Show: Keys",
            "Trk: ",

            "Tala Instruments & Bols (Tap to Turn ON / OFF)",
            "ON",
            "OFF",

            "Harmonium Mode (ON = Play Preset Melody | OFF = Play Live on Keyboard) & DSP",
            "🎹 HARMONIUM BOX: ON (Playing Preset)",
            "🎹 HARMONIUM BOX: OFF (Play Live Keyboard)",
            "Oct -",
            "Oct +",
            "Oct: %1",
            "8-Band EQ: ON",
            "8-Band EQ: OFF",
            "Plate2x2 Reverb: ON",
            "Plate2x2 Reverb: OFF",

            "Song: %1 | Tala: %2 (%3 Matras)",
            "Scanning MIDI...",
            "No USB Keyboard (Touch Keys Active)",
            "PC Mode: Touch/PC Keys Active (Plug USB-MIDI any time)",
            "MIDI Connected: %1",
            "ALSA MIDI Unavailable",
            "On-Screen Keyboard Mode",
            {"S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N"},

            "Song:",
            "+ New",
            "Name:",
            "Song Name...",
            "Vibhags:",
            "Forms: ",
            "Rows/Form: ",
            "▶ Play / Stop in Editor",
            "✔ Apply Changes to Song",
            "💾 Save .mhb File",
            "📂 Load .mhb File",
            "📄 Write drum_presets.h",

            "Select Tala:",
            "+ New Tala",
            "Tala Name:",
            "Vibhag Divisions:",
            "▶ Play / Stop Tala",
            "✔ Apply Changes to Tala",
            "Vibhag %1  (Tali/Khali: %2)",
            "<b>Matra:</b>",
            "<b>Theka Bol:</b>",
            "<b>Kartal/Bell:</b>",
            "Matra %1",

            {
                {"Dha", "Dha"}, {"Dhin", "Dhin"}, {"Ge", "Ge"}, {"Ka", "Ka"},
                {"Na", "Na"}, {"Ta", "Ta"}, {"Ti", "Ti"}, {"Re", "Re"},
                {"Kartal Open", "Kartal Open"}, {"Kartal Closed", "Kartal Closed"},
                {"Ghanta", "Ghanta"}, {"Kansa", "Kansa"}, {"Manjira", "Manjira"},
                {"Damaru", "Damaru"}, {"Shankha", "Shankha"}, {"Shankha 2", "Shankha 2"},
                {"Harmonium_Bellows", "Harmonium_Bellows"}
            }
        },


        // 1: HINDI (हिन्दी)

        {
            "hi",
            "🌐 हिन्दी",
            " हारमोनियम और तालमाला बॉक्स (800x480)",
            "हारमोनियम अभ्यास लाइव बॉक्स",
            "हारमोनियम स्वरलिपि एडिटर",
            "ताल ठेका एडिटर",

            "▶ प्रारंभ",
            "⏹ रोकें",
            "⏸ विराम",
            "▶ जारी रखें",
            "गिनती",
            "ताल:",
            "गीत:",
            "लोड",
            "सेव",

            "स्वर: %1 (%2)",
            "स्वर: --",
            "गिनती: %1",
            "राग स्केल (आधार षड्ज) और श्रुति / तानपूरा ड्रोन",
            "सा = %1",
            {
                "C (सफेद 1)", "C# (काली 1)", "D (सफेद 2)", "D# (काली 2)",
                "E (सफेद 3)", "F (सफेद 4)", "F# (काली 3)", "G (सफेद 5)",
                "G# (काली 4)", "A (सफेद 6)", "A# (काली 5)", "B (सफेद 7)"
            },
            "ड्रोन: बंद",
            "ड्रोन: चालू",
            "श्रुति + तानपूरा",
            "केवल श्रुति ड्रोन",
            "केवल तानपूरा ड्रोन",
            "सुर: सा-प (S-P̣-Ṣ)",
            "सुर: सा-म (S-ṃ-Ṣ)",
            "सुर: सा-नि (S-Ṇ-Ṣ)",
            "सुर: केवल सा (S-Ṣ)",
            "दिखाएँ: कोई भी",
            "दिखाएँ: कीबोर्ड",
            "ट्रैक: ",

            "ताल वाद्य और बोल (चालू / बंद करने के लिए दबाएँ)",
            "चालू",
            "बंद",

            "हारमोनियम मोड (चालू = प्रीसेट धुन | बंद = लाइव कीबोर्ड) और DSP",
            "🎹 हारमोनियम बॉक्स: चालू (प्रीसेट धुन)",
            "🎹 हारमोनियम बॉक्स: बंद (लाइव कीबोर्ड बजाएँ)",
            "सप्तक -",
            "सप्तक +",
            "सप्तक: %1",
            "8-बैंड EQ: चालू",
            "8-बैंड EQ: बंद",
            "गूँज (Reverb): चालू",
            "गूँज (Reverb): बंद",

            "गीत: %1 | ताल: %2 (%3 मात्रा)",
            "MIDI खोजा जा रहा है...",
            "कोई USB कीबोर्ड नहीं (टच कीज़ चालू)",
            "PC मोड: टच/कीबोर्ड चालू (USB-MIDI कभी भी जोड़ें)",
            "MIDI जुड़ा है: %1",
            "ALSA MIDI अनुपलब्ध",
            "ऑन-स्क्रीन कीबोर्ड मोड",
            {"सा", "रे॒", "रे", "ग॒", "ग", "म", "म॑", "प", "ध॒", "ध", "नि॒", "नि"},

            "गीत:",
            "+ नया गीत",
            "नाम:",
            "गीत का नाम...",
            "विभाग:",
            "भाग (Forms): ",
            "पंक्तियाँ: ",
            "▶ एडिटर में बजाएँ / रोकें",
            "✔ गीत में बदलाव लागू करें",
            "💾 .mhb फ़ाइल सेव करें",
            "📂 .mhb फ़ाइल लोड करें",
            "📄 drum_presets.h लिखें",

            "ताल चुनें:",
            "+ नया ताल",
            "ताल का नाम:",
            "विभाग विभाजन:",
            "▶ ताल बजाएँ / रोकें",
            "✔ ताल में बदलाव लागू करें",
            "विभाग %1  (ताली/खाली: %2)",
            "<b>मात्रा:</b>",
            "<b>ठेका बोल:</b>",
            "<b>करताल/घंटा:</b>",
            "मात्रा %1",

            {
                {"Dha", "धा (Dha)"}, {"Dhin", "धिन (Dhin)"}, {"Ge", "गे (Ge)"}, {"Ka", "क (Ka)"},
                {"Na", "ना (Na)"}, {"Ta", "ता (Ta)"}, {"Ti", "ति (Ti)"}, {"Re", "रे (Re)"},
                {"Kartal Open", "करताल खुला"}, {"Kartal Closed", "करताल बंद"},
                {"Ghanta", "घंटा"}, {"Kansa", "कांसा"}, {"Manjira", "मंजीरा"},
                {"Damaru", "डमरू"}, {"Shankha", "शंख"}, {"Shankha 2", "शंख 2"},
                {"Harmonium_Bellows", "हारमोनियम"}
            }
        }

        /* =====================================================================
         * FUTURE LANGUAGE TEMPLATE 
         * =====================================================================
        ,{

        }
        */
    };
    return langs;
}

} // namespace I18n

#endif // I18N_H
