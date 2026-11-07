#ifndef I18N_H
#define I18N_H

#include <string>
#include <vector>
#include <map>

namespace I18n {

struct LangPack {
    const char* code;
    const char* menuLabel; 


    const char* windowTitle;
    const char* tabLiveBox;
    const char* tabHarmEditor;
    const char* tabTalaEditor;


    const char* btnStart;
    const char* btnStop;
    const char* btnPause;
    const char* btnResume;
    const char* chkCountdown;
    const char* lblTala;
    const char* lblSong;
    const char* btnLoad;
    const char* btnSave;


    const char* swaraPopupPrefix;  
    const char* countdownPrefix;    
    const char* grpTunerTitle;
    const char* saKeyPrefix;        
    const char* indianKeyLabels[12];
    const char* btnDroneOff;
    const char* btnDroneOn;
    const char* dronePresetBoth;
    const char* dronePresetShruti;
    const char* dronePresetTanpura;
    const char* droneSurSaPa;
    const char* droneSurSaMa;
    const char* droneSurSaNi;
    const char* droneSurSaOnly;


    const char* grpTracksTitle;
    const char* stateOn;
    const char* stateOff;


    const char* grpHarmTitle;
    const char* btnHarmPresetOn;
    const char* btnHarmPresetOff;
    const char* btnOctDown;
    const char* btnOctUp;
    const char* lblOctavePrefix;  
    const char* btnEqOn;
    const char* btnEqOff;
    const char* btnReverbOn;
    const char* btnReverbOff;


    const char* footerSongLbl;     
    const char* footerTalaLbl;      
    const char* footerMatraLbl;     
    const char* midiPcMode;
    const char* midiConnected;
    const char* swaraNames[12];     


    const char* btnEdNew;
    const char* btnEdRand;
    const char* btnEdPlay;
    const char* btnEdSave;
    const char* btnEdLoad;
    const char* lblEdDivisions;


    std::map<std::string, std::string> trackDisplayNames;

    std::string translateTrackName(const std::string& canonical) const {
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

            "START",
            "STOP",
            "PAUSE",
            "RESUME",
            "Count",
            "Tala:",
            "Song:",
            "Load",
            "Save",

            "Swara:",
            "COUNTDOWN:",
            "Raga Scale (Adhara Shadja) & Shruti / Tanpura Drone",
            "Sa = ",
            {
                "C (Safed 1)", "C# (Kali 1)", "D (Safed 2)", "D# (Kali 2)",
                "E (Safed 3)", "F (Safed 4)", "F# (Kali 3)", "G (Safed 5)",
                "G# (Kali 4)", "A (Safed 6)", "A# (Kali 5)", "B (Safed 7)"
            },
            "DRONE: OFF",
            "DRONE: ON",
            "Shruti + Tanpura",
            "Shruti Only",
            "Tanpura Only",
            "Sur: Sa-Pa (S-P-S)",
            "Sur: Sa-Ma (S-m-S)",
            "Sur: Sa-Ni (S-N-S)",
            "Sur: Sa Only (S-S)",

            "Tala Instruments & Bols (Tap to Turn ON / OFF)",
            "ON",
            "OFF",

            "Harmonium Mode & DSP",
            "HARMONIUM: ON (Playing Preset)",
            "HARMONIUM: OFF (Live Keyboard)",
            "Oct -",
            "Oct +",
            "Oct:",
            "8-Band EQ: ON",
            "8-Band EQ: OFF",
            "Reverb: ON",
            "Reverb: OFF",

            "Song",
            "Tala",
            "Matra",
            "PC/Touch Keys Active (Plug USB-MIDI any time)",
            "USB-MIDI Connected",
            {"S", "r", "R", "g", "G", "m", "M", "P", "d", "D", "n", "N"},

            "+ New", "Randomise", "▶ Play/Stop", "Save .mhb", "Load .mhb", "Divs: ",

            {
                {"Dha", "Dha"}, {"Dhin", "Dhin"}, {"Ge", "Ge"}, {"Ka", "Ka"},
                {"Na", "Na"}, {"Ta", "Ta"}, {"Ti", "Ti"}, {"Re", "Re"},
                {"Kartal Open", "Kartal Open"}, {"Kartal Closed", "Kartal Closed"},
                {"Ghanta", "Ghanta"}, {"Kansa", "Kansa"}, {"Manjira", "Manjira"},
                {"Damaru", "Damaru"}, {"Ra", "Ra"}, {"Ki", "Ki"},
                {"Tabla_Na", "Tabla Na"}, {"Dagga_Ge", "Dagga Ge"},
                {"Khol_High", "Khol High"}, {"Khol_Low", "Khol Low"},
                {"Shankha", "Shankha"}, {"Shankha 2", "Shankha 2"},
                {"Harmonium_Bellows", "Harmonium"}
            }
        },

        // 1: HINDI (हिन्दी)
        {
            "hi",
            "हिन्दी",
            "हारमोनियम और तालमाला बॉक्स (800x480)",
            "हारमोनियम अभ्यास लाइव बॉक्स",
            "हारमोनियम स्वरलिपि एडिटर",
            "ताल ठेका एडिटर",

            "प्रारंभ",
            "रोकें",
            "विराम",
            "जारी",
            "गिनती",
            "ताल:",
            "गीत:",
            "लोड",
            "सेव",

            "स्वर:",
            "गिनती:",
            "राग स्केल (आधार षड्ज) और श्रुति / तानपूरा ड्रोन",
            "सा = ",
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
            "सुर: सा-प (S-P-S)",
            "सुर: सा-म (S-m-S)",
            "सुर: सा-नि (S-N-S)",
            "सुर: केवल सा (S-S)",

            "ताल वाद्य और बोल (चालू / बंद करने के लिए दबाएँ)",
            "चालू",
            "बंद",

            "हारमोनियम मोड और DSP",
            "हारमोनियम बॉक्स: चालू (प्रीसेट धुन)",
            "हारमोनियम बॉक्स: बंद (लाइव कीबोर्ड)",
            "सप्तक -",
            "सप्तक +",
            "सप्तक:",
            "8-बैंड EQ: चालू",
            "8-बैंड EQ: बंद",
            "गूँज (Reverb): चालू",
            "गूँज (Reverb): बंद",

            "गीत",
            "ताल",
            "मात्रा",
            "टच / कीबोर्ड चालू (USB-MIDI कभी भी जोड़ें)",
            "MIDI जुड़ा है",
            {"सा", "रे॒", "रे", "ग॒", "ग", "म", "म॑", "प", "ध॒", "ध", "नि॒", "नि"},

            "+ नया", "नया रेंडम", "बजाएँ/रोकें", ".mhb सेव करें", ".mhb लोड करें", "विभाग: ",

            {
             {"Dha", "धा (Dha)"}, {"Dhin", "धिन (Dhin)"}, {"Ge", "गे (Ge)"}, {"Ka", "क (Ka)"},
             {"Na", "ना (Na)"}, {"Ta", "ता (Ta)"}, {"Ti", "ति (Ti)"}, {"Re", "रे (Re)"},
             {"Kartal Open", "करताल खुला"}, {"Kartal Closed", "करताल बंद"},
             {"Ghanta", "घंटा"}, {"Kansa", "कांसा"}, {"Manjira", "मंजीरा"},
             {"Damaru", "डमरू"}, {"Ra", "रा (Ra)"}, {"Ki", "कि (Ki)"},
             {"Tabla_Na", "तबला ना"}, {"Dagga_Ge", "डग्गा गे"},
             {"Khol_High", "खोल हाई"}, {"Khol_Low", "खोल लो"},
             {"Shankha", "शंख"}, {"Shankha 2", "शंख 2"},
             {"Harmonium_Bellows", "हारमोनियम"}
            }
        }

        /* =====================================================================
         * FUTURE LANGUAGE TEMPLATE 
         * =====================================================================
        ,{
            "",
            "",
            ...
        }
        */
    };
    return langs;
}

} // namespace I18n

#endif // I18N_H
