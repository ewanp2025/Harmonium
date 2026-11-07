# Harmonium & Rhythm Box Prototype (ESP32)

An open-source digital harmonium and rhythm box currently running on an ESP32-S3 microcontroller with a 7-inch capacitive touchscreen which can also be controlled by attached MIDI keyboard. 

The goal of this project is to build a dedicated, standalone electronic instrument and practice accompaniment machine that can be played with either on-screen touch keys or an external USB-MIDI keyboard. This software is free and can be used or modified for any device anyone builds or sells (so someone could if they so wanted, manufacture a MIDI keyboard and controller in one and use this software or a modification to control it).

## What It Does
* **Real-Time Procedural Synthesis:** Generates authentic harmonium reed tones (multi-reed voicing with simulated bellows pressure and mechanical vibrato) directly from mathematical equations in real time so no sample packs or soundfonts required.
* **Full Rhythm Section:** Synthesised traditional percussion sounds and patterns.
* **Drone:** Built-in continuous electronic drone with selectable root keys and tuning modes.
* **Accompaniment & Live Modes:** Play the harmonium live from a USB keyboard or the on-screen 37-key piano, or let the box play preset melodies and song forms while you practice singing or playing another instrument.
* **On-Screen Editors:** Built-in touch editors for both Drums and Harmonium scores.
* **Bank Storage:** Save and load custom songs, directly to internal flash memory (.mhb files on LittleFS).
* **Desktop Simulator:** Includes a cross-platform Qt6 C++ simulator (HarmoniumEsp32Sim) that compiles on Windows and Linux so you can develop, test sound equations, and edit banks without flashing the microcontoller every time.
* **Experimental Singing Voice:** An early-stage phonetic voice synthesiser designed to articulate lyrics and syllables alongside the melody (currently a work-in-progress).

## Sound Synthesis Architecture
The sound generation in this project uses mathematical expression parsing rather than static audio samples (although the parsed drum expressions are computed and saved as samples in memory on each boot). 

Credit goes to the creators of the LMMS Xpressive synthesizer plugin for establishing this expression language. Because the instruments were originally modeled and synthesized in LMMS Xpressive, this firmware includes a lightweight, standalone C++ parser and abstract syntax tree (AST) evaluator that runs the formulas directly on the ESP32-S3. This allows reed harmonics, bellows modulation, envelope decay, and percussion transients to be updated and refined purely in code without bloating flash storage with audio files.

## Hardware & Current Setup
The primary reference hardware tested is the Waveshare ESP32-S3 Touch LCD 7" (800×480 parallel RGB display with Goodix GT911 capacitive touch):
* **MCU:** ESP32-S3-WROOM-1 (16 MB Flash, 8 MB Octal PSRAM).
* **Display Interface:** 16-bit parallel RGB565 driven via BufferedRGBBus (using internal SRAM bounce buffers) to completely eliminate screen wobble and horizontal image jitter caused by PSRAM bus contention during touch events.
* **USB Host Port:** Dedicated native Full-Speed USB OTG port (GPIO 19/20) for USB-MIDI keyboard controllers and USB Audio Class output.

## Audio Output Options
Because the Waveshare 7-inch development board used for testing and development does not have an onboard audio DAC or 3.5 mm headphone jack, several output options are implemented:

* **I2S Hardware Audio (Default):** Digital PCM audio streams continuously out of the ESP32-S3 I2S0 peripheral at 22,050 Hz (16-bit stereo). 
  *Pin mapping:*
  * BCLK (Bit Clock): GPIO 6
  * LRCK / WS (Word Select): GPIO 16
  * DOUT (Serial Data Out): GPIO 15
  * Can be edited within menu and stored.
     
  Connect an external I2S DAC breakout (such as a PCM5102A for a 3.5 mm jack, or a MAX98357A for a direct speaker) to these pins for clean, zero-latency physical audio output. 
  > **\*\* Please note** that the above (I2s) has not been tested, as I have not wished to break connections on my test board which lacks an array of GPIO pins.

* **Wi-Fi Hotspot Audio Streaming (OUT: WIFI):** The board creates its own standalone Wi-Fi Access Point named `Harmonium_Box` (Password: `12345678`). 
  Connect your phone, tablet, or PC to the network and navigate to `http://192.168.4.1` in your browser. 
  Tap Play on the web player to stream live 22.05 kHz uncompressed PCM audio directly to your device and listen through your phone's speakers, headphones, or a paired Bluetooth speaker.

* **USB Audio Output (OUT: USB):** Supports standard USB Audio Class 1.0 (UAC 1.0) headphone dongles and DACs. 
  > **Important Hardware Note:** This only works with some USB headphones and dongles without a USB OGT Power Splitter. First select the OUT: button to USB: NO DEV, then plug in the device afterwards and the button will change to USB: 48K OK and then play through the device

## Current Limitations
* **Cabinet Resonance Transfer Function:** The cabinet filter (TransferFunctionFX) simulates the acoustic wooden body of the harmonium using frequency-domain processing. This has been overridden for the harmonium but the functionality is still incorporated incase the effect has to be modified. However the transfer function slows processing down.
* **Simultaneous USB Audio Out + USB MIDI In:** The ESP32-S3 Waveshare 7 Inch Board has a single USB OTG controller. While hot-plugging between a MIDI keyboard and a USB audio dongle is supported, running both simultaneously through a passive hub is limited by the current driver architecture.
* **WIFI streaming is too slow and delayed:** As I now have USB Audio working, this could probably be hidden going forward.
* * **Bellows Manual Control Not Tested:** I haven't tried connecting a switch.
* **Board Selection:** Once the firmware is worked on further, the goal is to target a less expensive / cost-effective ESP32-S3 development board such as the following:
  * ESP32-S3-DevKitC-1 N16R8
  * PCM5102A I2S DAC Breakout
  * 2.8" 320×240 ILI9341 or ST7789 SPI Touch Screen
  * I have created a mock up, but I have not been able to test this version.
* **Singing Voice Synthesis:** Needs to be worked on and is hidden from the main menu for now.

## To build

```bash
python -m esptool --chip esp32s3 --port COM3 --baud 115200 erase_flash 
python -m esptool --chip esp32s3 --port COM3 --baud 115200 write_flash -z 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```
