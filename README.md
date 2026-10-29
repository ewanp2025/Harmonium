## License & Acknowledgments
This project is open-source software released under the **GNU General Public License v3.0 (GPLv3)**.

## Overview
Firstly — I did not create the expression format for the sound synthesis. This was developed by the programmers of the LMMS plugin Xpressive. As I originally synthesised the sounds in LMMS Xpressive, I decided to create a method to parse and evaluate the same mathematical expressions standalone, so that I can continually improve the synthesis formulas without requiring external sample libraries.

The concept behind this project is a dedicated, standalone electronic harmonium and rhythm box. It includes traditional percussion/bol patterns, a continuous drone, and the ability to either play the harmonium live or have it automatically play preset melodies while you practice. The goal is to make it open-source and low cost, hopefully less expensive than buying a physical harmonium. 

I currently have the code running on Windows connected to my MicroKorg as a MIDI controller, with plans to test it on embedded hardware.

### Planned Hardware Setup
* 7-inch touch screen (to be tested when it arrives)
* Raspberry Pi 3A+ running Raspberry Pi OS Lite (to be tested when it arrives)
* Inexpensive USB music keyboard (currently using my MicroKorg as a controller so I don't have to purchase extra hardware to test)

---

## Features

* **Mathematical Sound Synthesis (`XpressiveParser`):** Real-time procedural audio synthesis driven by mathematical equations rather than samples. Includes:
  * Dual-reed harmonium emulation with bellows/tremolo dynamics
  * 14+ synthesized percussion instruments and classical bols (*Dha, Dhin, Ge, Ka, Na, Ta, Ti, Re, Kartal, Manjira, Ghanta, etc.*)
  * Continuous multi-voice drone supporting both Shruti box and Tanpura models (Sa-Pa, Sa-Ma, Sa-Ni, and Sa-only tunings)
* **Performance Modes:**
  * **Live Mode:** Play melodies directly using a connected MIDI controller, the on screen touch keyboard, or standard PC typing keys.
  * **Preset Melody Mode:** Automated playback of harmonium compositions to practice alongside percussion.
* **Master Performance Controls:**
  * Master tempo slider (BPM) with tap adjustments
  * Microtonal tuning (set Sa to any key from C through B, plus adjustable A4 concert pitch from 415 Hz to 466 Hz)
  * Real-time detection display showing current note name and octave
  * Individual mute toggles for every rhythm track and instrument
  * Built-in internal transfer function based effects
  * Buit in saveable pattern randomisers
* **Built-in Visual Editors:**
  * **Harmonium Editor:** Create and edit songs using standard notation (`S`, `r`, `R`, `g`, `G`, etc.) and split compositions across multiple forms.
  * **Tala Editor:** Build and customise rhythm cycles.
  * **Files:** Save and load custom setups via project files (`.mhb`) or export them directly to C++ preset headers for development (`drum_presets.h`).

---

### Credits & Compatibility
* **LMMS & Xpressive Synthesis Specification:** This software includes an independent, lightweight C++ expression parser (`XpressiveParser`) and project loader designed for compatibility with mathematical instrument formulas created in [LMMS](https://lmms.io/) using the **Xpressive** synthesiser plugin. Full credit goes to the LMMS development team and the authors of the Xpressive plugin for pioneering this formula-based synthesis workflow.

* **Frameworks & Platform Audio:** Built with [Qt 6](https://www.qt.io/) (used under the LGPLv3/GPLv3 license) using ALSA for Raspberry Pi OS Lite and WinMM for Windows USB-MIDI and audio output.

### Known Issues
* CPU performance slows down when drone is enabled or when pressing keys very quickly (currently investigating).
* Only tested on Windows so far; Raspberry Pi testing pending hardware arrival.

### Future
* If this works well enough on Raspberry PI, to investigate if this can be optimised to run on even less expensive hardware.
