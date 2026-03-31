# ESP32 Polytuner v4.0.1 - Industrial Precision Tuning Pedal

ESP32 Polytuner er en avanceret, industriel polyfonisk stemmepedal til guitar og bas, designet til at overgå markedsstandarder som TC Electronic 
Polytune 3. Ved at udnytte ESP32'erens dual-core arkitektur og hardware-accelereret DSP, leverer dette projekt lynhurtig polyfonisk detektion og 
ekstrem monofonisk præcision (±0.1 cents).
Det anvender færdigt DEVIT V1 ESP32-32 Development Board, 36Pins modul for at slippe for dyr og omstændig RF godkendelse:
https://www.aliexpress.com/item/1005010739940143.html?spm=a2g0o.productlist.main.3.19adPf2dPf2dB3&algo_pvid=3759358a-e156-4956-a9b9-c7689d256605&algo_exp_id=3759358a-e156-4956-a9b9-c7689d256605-2&pdp_ext_f=%7B%22order%22%3A%22-1%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21DKK%2187.17%2169.74%21%21%2113.10%2110.48%21%40211b876717749177995738376e0b0b%2112000053361056812%21sea%21DK%210%21ABX%211%210%21n_tag%3A-29910%3Bd%3Af14da666%3Bm03_new_user%3A-29895&curPageLogUid=RF09V4xjevpP&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005010739940143%7C_p_origin_prod%3A#nav-specification

## 🚀 Key Features

* **Polyfonisk Multi-Peak Detektion:** Stem alle strenge (op til 6) samtidigt med et enkelt anslag. Systemet identificerer peaks via Radix-4 FFT.
* **Ultra-Præcis Monofonisk Mode:** Benytter en optimeret YIN-algoritme med tau-begrænsning og parabolsk interpolation for 0.1 cents nøjagtighed.
* **Simuleret Strobe Mode:** Til professionel intonering og finjustering med visuel feedback i realtid.
* **Dual-Core Processing:** Core 0 er dedikeret 100% til DSP (lydanalyse), mens Core 1 håndterer det responsive OLED-UI og brugerinput.
* **Pristine Analog Thru:** En dedikeret, lav-ohmsk analog buffer-kreds (Maxim CMOS) sikrer, at din tone bevares uden tab (zero tone-suck).
* **Udvidede Profiler:** Understøtter Guitar, 4-str. Bas og 5-str. Bas med profiler som Standard, Drop D, DADGAD, Open G og brugerdefinerede tunings.
* **NVS Lagring:** Alle indstillinger og brugerprofiler gemmes i ESP32'erens interne flash-hukommelse.

## 🛠 Hardware Arkitektur

Systemet er bygget op omkring en **DEVIT V1 ESP32-32 Development Board, 36Pins** og en avanceret analog front-end (AFE):

1.  **Analog Front-End (AFE):** Benytter en Maxim Integrated Rail-to-Rail CMOS operationsforstærker optimeret til 3.3V drift. Dette sikrer maksimalt 
    headroom for både passive og high-output aktive pickupper.
2.  **Buffered Thru-Output:** Signalet splittes efter indgangsbufferen og føres gennem en dedikeret udgangsbuffer med DC-blokering (47uF), hvilket 
    gør pedalen ideel som det første led i en professionel signalkæde. Man får en buffer der sikrer at evt. signaltab ikke er tilstede i 
    effektkæden.
3.  **Brugerflade:** Et høj-kontrast SSD1306 OLED-display sikrer læsbarhed på mørke scener, assisteret af en industri-standard rotary encoder til 
    navigation.
4.  **Sikkerhed:** Inkluderer I2C Watchdog med auto-recovery, adaptiv clipping-detektion og batteriovervågning.

## 💻 Software & Algoritmer

Firmwaren er skrevet i C++/Arduino og kører på FreeRTOS:

* **DSP (Core 0):** Implementerer `esp-dsp` biblioteket for hardware-accelereret FFT. YIN-algoritmen kører med adaptiv threshold og noise-gate.
* **UI (Core 1):** Håndterer en event-drevet tilstandsmaskine (FSM), der sikrer, at brugerfladen aldrig lagger, selv under tunge beregninger.
* **DMA Sampling:** Lyddata streames direkte fra ADC til RAM via DMA (Direct Memory Access) for at minimere CPU-belastning.
* Softwaren er langt mere avanceret end markeds tilgængelige polytuner versioner.

## 📦 Installation & Afhængigheder

For at kompilere og uploade firmwaren skal du bruge Arduino IDE (eller PlatformIO) med ESP32-board support installeret.

### Nødvendige biblioteker:
* **Adafruit GFX & SSD1306:** Til styring af det grafiske OLED-display [REQ-UI-103].
* **esp-dsp:** Espressif's officielle DSP-bibliotek til hardware-accelereret FFT [REQ-ALG-201].
* **Preferences:** Til persistent lagring af indstillinger i NVS [REQ-DAT-401].
* **Wire:** Til I2C-kommunikation med display og sensorer.

### Opsætning:
1.  Klon dette repository til din lokale maskine.
2.  Åbn `ESP32_polytuner_firmware2.ino` i Arduino IDE.
3.  Vælg "ESP32 Dev Module" under værktøjer.
4.  Sørg for, at PSRAM er deaktiveret (medmindre dit specifikke modul kræver det), da vi optimerer til intern RAM.
5.  Kompiler og upload til din enhed.

## 📈 Tekniske Specifikationer & Validering

Projektet er udviklet med en "Industrial Standard" tilgang, hvilket betyder, at hver funktion er valideret mod specifikke krav (SRS):

* **Måleområde:** 30 Hz (Low B på bas) til 1000 Hz.
* **Præcision:** ±0.1 cents i Monofonisk/Strobe mode [REQ-ALG-202].
* **Latency:** < 50ms fra anslag til visuel respons [REQ-ALG-203].
* **Input Impedans:** 1 MOhm (Professional Guitar Standard).
* **Output:** Unity-gain buffered thru-path med 47uF DC-blokering [REQ-HW-507].
* **Stabilitet:** I2C Watchdog sikrer mod display-frys pga. udefrakommende støj [REQ-HW-504].

## 📝 Dokumentation

Dette projekt er omfattende dokumenteret i følgende filer (findes i `/docs` mappen):
* **SRS-ESP32-PT-400-2026-REV3:** Kravspecifikation med alle funktionelle og elektriske krav.
* **STD-ESP32-PT-400-2026-REV4:** Teknisk systemdokumentation inklusiv hardwareanalyse, algoritme-gennemgang og brugervejledning.
* **Schematics og PCB layout** er under udarbejdelse i KiCAD.

## 👨‍💻 Forfatter

**Jan Engelbrecht Pedersen**
*Status: Production Ready / Industrial Standard*

---
*Dette projekt er open-source under MIT-licensen.*
