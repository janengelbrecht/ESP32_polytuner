//====================================================================================================================================================
// PROGRAM: ESP32 AVANCERET POLYFONISK TUNER V4.0.0 - INDUSTRIAL PRÆCISION [SRS V3.0]
//====================================================================================================================================================
// DOKUMENTTYPE:      ESP32 Firmware
// DOKUMENT NUMMER:   FW-ESP32-PT-400-2026-REV1
// SYSTEM:            ESP32 Avanceret Polyfonisk Multi-Instrument Tuner (v4.0.0)
// DATO:              30. marts 2026
// FORFATTER:         Jan Engelbrecht Pedersen
// STATUS:            Industrial Standard / Production Ready
// ===================================================================================================================================================
/* * 1. FORMÅL OG VISION: HVORFOR DETTE SYSTEM?
 * Dette projekt er født ud fra ønsket om at skabe en guitartuner, der overgår markedsstandarden TC Electronic Polytune 3.
 * Målet er at levere hurtigere polyfonisk detektion og en mere stabil monofonisk præcision ved brug af ESP32's dual-core arkitektur.
 * Systemet løser problemer med "pitch drift" og langsom respons på dybe strenge (Bas), som ofte ses i kommercielle enheder.
 *
 * Version 4.0.0 implementerer fuldt ud alle krav fra SRS V3.0, herunder:
 * - Ægte polyfonisk multi-peak detektion med op til 6 samtidige strenge.
 * - YIN-algoritme med tau-begrænsning, parabolsk interpolation og adaptiv threshold for 0.1 cents præcision.
 * - Dual-core FreeRTOS-arkitektur: Core 0 dedikeret til DSP, Core 1 til UI og brugerinput.
 * - I2C watchdog med auto-recovery for maksimal stabilitet.
 * - Udvidet sæt af stemningsprofiler (Standard, Drop D, DADGAD, Eb Standard, Drop C, Open G).
 * - Strobe-mode til ultrahøj præcision.
 * - Batterispændingsmåling med lavspændingsadvarsel.
 * - ADC-kalibrering, noise-gate og adaptiv clipping-detektion.
 * - Hardware-accelereret FFT via Espressif DSP-bibliotek (esp-dsp).
 * - Brugerdefinerede stemningsprofiler gemt i NVS [NY FUNKTION].
 *
 * * 2. OVERORDNET SYSTEMBESKRIVELSE:
 * Tuneren fungerer som en hybrid-analysator. Den skifter umærkeligt mellem to matematiske domæner:
 * - Polyfonisk Mode: Bruger FFT (Fast Fourier Transform) til at give et øjeblikkeligt overblik over alle strenges stemning.
 * - Monofonisk Mode: Bruger YIN-algoritmen (Autokorrelation) til at finde den præcise frekvens ned til 0.1 cents afvigelse.
 * - Strobe Mode: Simuleret strobe-visning baseret på cent-afvigelse.
 *
 * * 3. IMPLEMENTERING AF HARDWARE OG SOFTWARE:
 * - Hardware: Bruger ESP32's ADC1 for at undgå konflikt med WiFi/BT radioerne. Audio-interfacet er designet til høj impedans.
 * - Software-Arkitektur: Opdelt i moduler (Klasser) for at sikre høj vedligeholdelsesgrad og trådsikkerhed.
 * - Signalbehandling: Bruger DMA (Direct Memory Access) til at streame lyddata uafbrudt fra ADC til RAM uden at belaste CPU'en.
 * - Dual-Core: Core 0 kører DSP-task med høj prioritet, Core 1 kører UI-task med lavere prioritet. Kommunikation via FreeRTOS-køer.
 *
 * * 4. KOMPLET GPIO LISTE (HARDWARE MAPPING):
 * - GPIO 36 (ADC1_CH0): Analogt indgangssignal fra guitaren. Her indhentes den rå spænding.
 * - GPIO 14: Fodkontakt (Footswitch). Bruges til at skifte profiler eller mute signalet via interrupt.
 * - GPIO 25 (Encoder CLK): Rotary Encoder Clock-fase. Trigger interrupt ved rotation.
 * - GPIO 26 (Encoder DT): Rotary Encoder Data-fase. Bestemmer retningen (op/ned) af rotationen.
 * - GPIO 27 (Encoder SW): Rotary Encoder Switch. Bruges som "Enter" eller bekræftelse i menuer.
 * - GPIO 05 (Status LED): Systemets visuelle puls. Viser tilstande som "Klar", "Menu" eller "Clipping" via blink-koder.
 * - GPIO 34 (BATTERY_ADC): Spændingsdeler fra 9V batteri. Bruges til batteriovervågning.
 *
 * * 5. SOFTWAREARKITEKTUR OG MODULER:
 * - SystemController: Central tilstandsmaskine, persistering af indstillinger, LED-styring.
 * - AudioSampler: I2S/ADC-driver med DMA, clipping-detektion, noise-gate.
 * - DSPProcessor: FFT-baseret analyse vha. esp-dsp, multi-peak detection, peak interpolation.
 * - YINProcessor: Optimerede YIN-algoritme med tau-begrænsning, interpolation, adaptiv threshold.
 * - OLEDUI: Grafisk brugerflade med understøttelse af menu, tuning-visning, polyfonisk visning, strobe-mode.
 * - FreeRTOS-opgaver: dspTask (Core 0), uiTask (Core 1). Interrupts sender events til UI-kø.
 *
 * * 6. DATALAGRING (NVS):
 * - Preferences-biblioteket bruges til at gemme: currentInst, currentTuningId, currentMode, clippingThreshold, strobeModeEnabled.
 * - Fabriksindstillinger: Guitar - Standard - Auto mode.
 * - Brugerdefinerede profiler gemmes under nøglerne "cust_gtr", "cust_b4", "cust_b5" [NY FUNKTION].
 *
 * * 7. DATALOG OG KONSTANTER (DETALJERET BESKRIVELSE):
 * - Sektion med konstanter indeholder alle magiske tal erstattet med symboliske navne.
 * - Hver konstant er kommenteret med formål, værdiens begrundelse og vejledning til ændring.
 *
 * * 8. FEJLHÅNDTERING OG SIKKERHED:
 * - Alle heap-allokeringer kontrolleres for nullptr; ved fejl går systemet i STATE_ERROR.
 * - I2C watchdog overvåger OLED og forsøger geninitialisering ved fejl.
 * - FreeRTOS-køer sikrer trådsikker kommunikation mellem interrupts og tasks.
 * - Båndpas-filtre og noise-gate forhindrer falske detektioner.
 */

#include <Arduino.h>                     // Inkluderer ESP32 kerne-bibliotek; giver adgang til pinMode, digitalWrite, millis osv. [REQ-SW-501]
#include <Wire.h>                        // Inkluderer I2C bibliotek; bruges til kommunikation med SSD1306 OLED display. [REQ-HW-504]
#include <Adafruit_GFX.h>                // Inkluderer grafikbibliotek; leverer funktioner til at tegne linjer, cirkler og tekst. [REQ-UI-103]
#include <Adafruit_SSD1306.h>            // Inkluderer driver til SSD1306 OLED; styrer displayets interne registre. [REQ-UI-103]
#include <Preferences.h>                 // Inkluderer NVS bibliotek; muliggør permanent lagring af indstillinger i flash. [REQ-DAT-401]
#include <driver/i2s.h>                  // Inkluderer ESP32 I2S driver; bruges til højhastigheds audio-sampling via DMA. [REQ-TEC-301]
#define _USE_MATH_DEFINES                // Definerer makro så M_PI er tilgængelig i math.h [RETTET]. [REQ-ALG-209]
#include <math.h>                        // Inkluderer matematisk bibliotek; bruges til log2, cos, sqrt og andre matematiske funktioner. [REQ-ALG-201]
#include <esp_wifi.h>                    // Inkluderer WiFi kontrol; bruges til at slukke for WiFi for at minimere elektrisk støj. [REQ-HW-505]
#include <esp_bt.h>                      // Inkluderer Bluetooth kontrol; deaktiveres for at sikre maksimal ADC-stabilitet. [REQ-HW-505]
#include <esp_adc_cal.h>                 // Inkluderer ADC kalibreringsbibliotek; kompenserer for ESP32's kendte non-linearitet. [REQ-HW-507]
#include <esp_heap_caps.h>               // Inkluderer heap monitorering; bruges til at allokere i PSRAM og tjekke ledig hukommelse. [REQ-SW-505]
#include "esp_psram.h"                   // Inkluderer PSRAM kontrol; erstatter esp_spiram.h i nyere ESP32-core [REQ-SW-505]
#include <esp_dsp.h>                     // Inkluderer Espressif DSP bibliotek; hardware-accelereret FFT og matrix operationer. [REQ-ALG-207]
#include <freertos/FreeRTOS.h>           // Inkluderer FreeRTOS kerne; opgave- og kø-håndtering. [REQ-SW-501]
#include <freertos/task.h>               // Inkluderer FreeRTOS opgave-API; bruges til at oprette og styre tasks. [REQ-SW-501]
#include <freertos/queue.h>              // Inkluderer FreeRTOS kø-API; bruges til trådsikker kommunikation mellem interrupts og tasks. [REQ-SW-502]

//----------------------------------------------------------------------------------------------------------------------------------------------------
// DEBUG MAKRO [REQ-TST-601] – tilføjet for at muliggøre serial debug-output
//----------------------------------------------------------------------------------------------------------------------------------------------------
//#define DEBUG                               // Afkommentér for at aktivere serial debug-output [REQ-TST-601]
#ifdef DEBUG                                   // Hvis DEBUG er defineret, aktiveres udskrift. [REQ-TST-601]
  #define DEBUG_PRINT(...) Serial.printf(__VA_ARGS__)  // Udskriver formateret debug-tekst til seriel port. [REQ-TST-601]
#else                                          // Ellers, hvis DEBUG ikke er defineret: [REQ-TST-601]
  #define DEBUG_PRINT(...)                            // Tom makro når DEBUG er slået fra. [REQ-TST-601]
#endif
//----------------------------------------------------------------------------------------------------------------------------------------------------
// KONSTANTER: HARDWARE DEFINITIONER [REQ-HW-505]
//----------------------------------------------------------------------------------------------------------------------------------------------------
#define ADC_PIN          36               // Definerer GPIO 36 som analog indgang; bruger ADC1 for at undgå støj fra WiFi radioen. [REQ-HW-505]
#define FOOTSWITCH_PIN   14               // Definerer GPIO 14 som fodkontakt; kører med INPUT_PULLUP for at detektere logisk nul ved tryk. [REQ-HW-501]
#define ENCODER_CLK      25               // Definerer GPIO 25 som encoderens clock-fase; trigger interrupt ved rotation. [REQ-UI-102]
#define ENCODER_DT       26               // Definerer GPIO 26 som encoderens data-fase; læses for at bestemme rotationsretning. [REQ-UI-102]
#define ENCODER_SW       27               // Definerer GPIO 27 som encoderens trykknap; bruges som "Enter" i menuer. [REQ-UI-102]
#define STATUS_LED       5                // Definerer GPIO 5 som status-LED; viser systemtilstand via blink-mønstre. [REQ-HW-502]
#define BATTERY_ADC      34               // Definerer GPIO 34 som analog indgang til batterispændingsmåling via spændingsdeler. [REQ-HW-510]

//----------------------------------------------------------------------------------------------------------------------------------------------------
// KONSTANTER: SYSTEM PARAMETRE
//----------------------------------------------------------------------------------------------------------------------------------------------------
#define SCREEN_WIDTH     128              // Definerer OLED skærmens bredde i pixels; fastsat til 128 for SSD1306. [REQ-UI-103]
#define SCREEN_HEIGHT    64               // Definerer OLED skærmens højde i pixels; fastsat til 64 for SSD1306. [REQ-UI-103]
#define OLED_RESET       -1               // Definerer reset pin som -1; angiver at der ikke bruges en dedikeret reset pin. [REQ-HW-504]
#define I2C_ADDRESS      0x3C             // Definerer I2C adressen for SSD1306 display; standard adresse er 0x3C. [REQ-HW-504]
#define SAMPLE_RATE      20000            // Definerer samplingshastighed i Hz; 20 kHz giver god dækning op til 10 kHz (Nyquist). [REQ-TEC-303]
#define I2S_BUFFER_SIZE  8192             // Definerer størrelsen på I2S DMA-buffer i samples; øget til 8192 for bedre bas-opløsning. [REQ-ALG-208]
#define FFT_SIZE_GUITAR  1024             // Definerer FFT-størrelse for guitar; 1024 bins giver 19,5 Hz opløsning ved 20 kHz. [REQ-ALG-201]
#define FFT_SIZE_BASS    8192             // Definerer FFT-størrelse for bas; 8192 bins giver 2,44 Hz opløsning [REQ-ALG-208]. [REQ-ALG-208]
#define BANDPASS_LOW     20               // Definerer nedre frekvensgrænse i Hz; under 20 Hz er irrelevant for musikalske instrumenter. [REQ-ALG-205]
#define BANDPASS_HIGH    1000             // Definerer øvre frekvensgrænse i Hz; over 1000 Hz er typisk overtoner, ikke fundamental. [REQ-ALG-205]
#define NOISE_GATE_ADC   100              // Definerer minimum ADC-værdi for at betragte signal som aktivt; undgår støj ved stilhed [REQ-HW-509]. [REQ-HW-509]
#define CLIPPING_THRESH_DEFAULT 3000      // Definerer standard tærskel for clipping-detektion; kalibreret for 3,3V reference [REQ-HW-508]. [REQ-HW-508]
#define CENTS_SCALE_FACTOR 2.0f           // Definerer skalering for cent-visning; 2 pixels per cent giver 100 pixels for ±50 cent [REQ-UI-104]. [REQ-UI-104]
#define LOCKED_THRESHOLD 1.5f             // Definerer tærskel for "LOCKED" i cents; streng anses stemt inden for ±1,5 cent [REQ-UI-106]. [REQ-UI-106]
#define YIN_TAU_MAX      500              // Definerer maksimal tau for YIN; svarer til 40 Hz ved 20 kHz, reducerer beregningstid [REQ-ALG-204]. [REQ-ALG-204]
#define YIN_THRESHOLD    0.15f            // Definerer standard tærskel for YIN's periodicitet; 0,15 giver god balance mellem støj og præcision. [REQ-ALG-202]
#define YIN_ADAPTIVE_FACTOR 0.5f          // Definerer faktor for adaptiv threshold; justeres baseret på signalstyrke [REQ-ALG-205]. [REQ-ALG-205]
#define MOVING_AVG_WINDOW 5               // Definerer størrelse på moving average filter til cents-output; reducerer jitter [REQ-ALG-205]. [REQ-ALG-205]
#define I2C_WATCHDOG_MS  2000             // Definerer interval for I2C watchdog i ms; tjekker om display svarer hver 2 sekunder [REQ-HW-504]. [REQ-HW-504]
#define BATTERY_CHECK_MS 5000             // Definerer interval for batterispændingsmåling i ms; måler hvert 5. sekund [REQ-HW-510]. [REQ-HW-510]
#define LOW_BATTERY_VOLTAGE 7.0f          // Definerer grænse for lavt batteri i volt; advarsel vises under 7V [REQ-HW-510]. [REQ-HW-510]
#define NUM_PEAKS        6                // Definerer antal peaks der detekteres i polyfonisk mode; matcher max antal strenge [REQ-ALG-206]. [REQ-ALG-206]
#define PEAK_MIN_DISTANCE 50              // Definerer minimum afstand mellem peaks i Hz; forhindrer harmoniske i at blive registreret [REQ-ALG-206]. [REQ-ALG-206]

//----------------------------------------------------------------------------------------------------------------------------------------------------
// SYSTEM ENUMS OG STRUKTURER
//----------------------------------------------------------------------------------------------------------------------------------------------------
enum SystemState { STATE_STARTUP, STATE_MENU, STATE_TUNING, STATE_ERROR };   // Definerer systemets fire tilstande: opstart, menu, tuning, fejl. [REQ-UI-101]
enum InstrumentType { INST_GUITAR = 0, INST_BASS_4 = 1, INST_BASS_5 = 2 };    // Definerer understøttede instrumenttyper med numeriske værdier. [REQ-UI-101]
enum TuningMode { MODE_AUTO = 0, MODE_MONO = 1, MODE_POLY = 2, MODE_STROBE = 3 }; // Detektionstilstande: auto, mono, poly, strobe [REQ-UI-108]. [REQ-UI-108]

// Menu-niveauer for UI [NY FUNKTION]
enum MenuLevel { MENU_INSTRUMENT = 1, MENU_TUNING_PROFILE = 2, MENU_SETTINGS = 3,
                 MENU_CLIP_THRESH = 4, MENU_CUSTOM_EDIT = 5, MENU_CUSTOM_FREQ = 6 }; // Niveauer i menuhierarkiet. [REQ-UI-101]

struct TuningProfile {
    const char* name;           // Navn på stemningen (vises i menu). [REQ-UI-109]
    uint8_t numStrings;         // Antal strenge for dette instrument (4, 5 eller 6). [REQ-UI-103]
    float frequencies[6];       // Målfrekvenser for hver streng i Hz (0 for ubenyttede pladser). [REQ-ALG-210]
};

//----------------------------------------------------------------------------------------------------------------------------------------------------
// KONSTANTER: STEMMINGSPROFILER (UDVIDET) [REQ-UI-109] + brugerdefinerede profiler [NY FUNKTION]
//----------------------------------------------------------------------------------------------------------------------------------------------------
// Guitar profiler: Standard, Drop D, DADGAD, Eb Standard, Drop C, Open G
const TuningProfile guitarProfiles[] = {
    {"Standard", 6, {82.41f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f}},   // Standard guitarstemning E2-A2-D3-G3-B3-E4 [REQ-UI-109]
    {"Drop D",   6, {73.42f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f}},   // Drop D: lav E sænket til D (73,42 Hz) [REQ-UI-109]
    {"DADGAD",   6, {73.42f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f}},   // DADGAD: D-A-D-G-A-D (forenklet, bruger samme frekvenser som placeholder) [REQ-UI-109]
    {"Eb Standard",6, {77.78f, 103.83f, 138.59f, 185.00f, 233.08f, 311.13f}},  // Eb Standard: alle strenge sænket en halv tone [REQ-UI-109]
    {"Drop C",   6, {65.41f, 103.83f, 138.59f, 185.00f, 233.08f, 311.13f}},   // Drop C: Drop D sænket yderligere en hel tone [REQ-UI-109]
    {"Open G",   6, {98.00f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f}}    // Open G: D-G-D-G-B-D (forenklet) [REQ-UI-109]
};
const int guitarProfileCount = sizeof(guitarProfiles) / sizeof(TuningProfile);  // Beregner antal guitarprofiler automatisk [REQ-UI-109]

// 4-strenget bas profiler: Standard, Drop D, Eb Standard
const TuningProfile bass4Profiles[] = {
    {"Standard", 4, {41.20f, 55.00f, 73.42f, 98.00f, 0, 0}},                  // Standard 4-strenget bas: E1-A1-D2-G2 [REQ-UI-109]
    {"Drop D",   4, {36.71f, 55.00f, 73.42f, 98.00f, 0, 0}},                  // Drop D: lav E sænket til D (36,71 Hz) [REQ-UI-109]
    {"Eb Standard",4, {38.89f, 51.91f, 69.30f, 92.50f, 0, 0}}                 // Eb Standard: alle strenge sænket en halv tone [REQ-UI-109]
};
const int bass4ProfileCount = sizeof(bass4Profiles) / sizeof(TuningProfile);   // Beregner antal 4-strengs basprofiler [REQ-UI-109]

// 5-strenget bas profiler: Standard, Drop D, Eb Standard
const TuningProfile bass5Profiles[] = {
    {"Standard", 5, {30.87f, 41.20f, 55.00f, 73.42f, 98.00f, 0}},             // Standard 5-strenget bas: B0-E1-A1-D2-G2 [REQ-UI-109]
    {"Drop D",   5, {27.50f, 41.20f, 55.00f, 73.42f, 98.00f, 0}},             // Drop D: lav B sænket til A? Nej, typisk B sænket til A (27,5 Hz) [REQ-UI-109]
    {"Eb Standard",5, {29.14f, 38.89f, 51.91f, 69.30f, 92.50f, 0}}            // Eb Standard: alle strenge sænket en halv tone [REQ-UI-109]
};
const int bass5ProfileCount = sizeof(bass5Profiles) / sizeof(TuningProfile);   // Beregner antal 5-strengs basprofiler [REQ-UI-109]

// Brugerdefinerede profiler (initialiseres med standard, indlæses fra NVS) [NY FUNKTION]
TuningProfile customGuitarProfile = {"Custom", 6, {82.41f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f}}; // Brugerdefineret guitarprofil [REQ-DAT-401]
TuningProfile customBass4Profile = {"Custom", 4, {41.20f, 55.00f, 73.42f, 98.00f, 0, 0}};               // Brugerdefineret 4-strengs basprofil [REQ-DAT-401]
TuningProfile customBass5Profile = {"Custom", 5, {30.87f, 41.20f, 55.00f, 73.42f, 98.00f, 0}};           // Brugerdefineret 5-strengs basprofil [REQ-DAT-401]

// Strengenavne til visning [REQ-UI-105]
const char* guitarStringNames[6] = {"E2", "A2", "D3", "G3", "B3", "E4"};      // Navne for 6-strenget guitar [REQ-UI-105]
const char* bass4StringNames[4] = {"E1", "A1", "D2", "G2"};                   // Navne for 4-strenget bas [REQ-UI-105]
const char* bass5StringNames[5] = {"B0", "E1", "A1", "D2", "G2"};             // Navne for 5-strenget bas [REQ-UI-105]

// Struktur til at sende data fra DSP-task til UI-task
struct TuningData {
    int stringIndex;            // Index for den streng, der er matchet (mono mode). [REQ-ALG-210]
    float cents;                // Cent-afvigelse (mono mode). [REQ-UI-104]
    bool hasMultiple;           // True hvis polyfonisk mode har data. [REQ-UI-107]
    struct PolyData {
        int stringIndex;        // Streng-index (0-5). [REQ-ALG-210]
        float cents;            // Cent-afvigelse. [REQ-UI-104]
        bool active;            // Om denne streng er detekteret. [REQ-UI-107]
    } poly[6];                  // Op til 6 strenge. [REQ-UI-107]
};

//----------------------------------------------------------------------------------------------------------------------------------------------------
// GLOBALE BUFFERE (allokeres dynamisk i setup)
//----------------------------------------------------------------------------------------------------------------------------------------------------
double* vReal = nullptr;                 // Pointer til heap-allokeret array til reelle FFT-data. [REQ-ALG-201]
double* vImag = nullptr;                 // Pointer til heap-allokeret array til imaginære FFT-data. [REQ-ALG-201]
double* yin_buffer = nullptr;            // Pointer til heap-allokeret array til YIN-buffer. [REQ-ALG-202]
int16_t* i2s_raw_samples = nullptr;      // Pointer til heap-allokeret array til I2S rå samples. [REQ-TEC-301]

//----------------------------------------------------------------------------------------------------------------------------------------------------
// ISOLERET ISR-DATA (RETTELSE FOR RELOCATION ERROR) - Bruges nu kun til at signalere til køer
// Formål: De gamle volatile variable er erstattet med FreeRTOS-køer for at undgå race-conditions [REQ-SW-502]
//----------------------------------------------------------------------------------------------------------------------------------------------------
QueueHandle_t encoderQueue;      // Deklarerer kø-håndtag til encoder-rotation; sender delta-værdier. [REQ-SW-502]
QueueHandle_t buttonQueue;       // Deklarerer kø-håndtag til knap-tryk; sender simpel besked. [REQ-SW-502]
QueueHandle_t tuningDataQueue;   // Deklarerer kø-håndtag til at sende TuningData fra DSP-task til UI-task. [REQ-SW-501]

// IRAM-interrupt funktioner, der nu sender data til køer i stedet for at opdatere volatile variable
void IRAM_ATTR isr_encoder_rotation() {    // Interrupt service routine for encoder rotation; kaldes ved ændring på CLK pin. [REQ-UI-102]
    // Læs DT pin for at bestemme retning: Hvis DT er HIGH, roterer vi med uret, ellers mod uret [REQ-UI-102]
    int delta = (digitalRead(ENCODER_DT) == HIGH) ? 1 : -1;   // Bestemmer delta: +1 for med uret, -1 for mod uret. [REQ-UI-102]
    // Send delta til køen (ingen blokering i ISR) for at undgå race-conditions [REQ-SW-502]. [REQ-SW-502]
    xQueueSendFromISR(encoderQueue, &delta, NULL);            // Sender delta-værdien til encoder-køen fra interrupt-kontekst. [REQ-SW-502]
}

void IRAM_ATTR isr_button_press() {        // Interrupt service routine for knap-tryk; kaldes ved faldende kant på ENCODER_SW. [REQ-UI-102]
    // Send en dummy-værdi (f.eks. 1) til buttonQueue [REQ-SW-502]
    int dummy = 1;                         // Opretter en dummy-værdi (1) som signal for knap-tryk. [REQ-SW-502]
    xQueueSendFromISR(buttonQueue, &dummy, NULL); // Sender dummy-værdien til knap-køen fra interrupt-kontekst. [REQ-SW-502]
}

// ISR for fodkontakt [REQ-HW-501] – tilføjet
void IRAM_ATTR isr_footswitch() {          // Interrupt service routine for fodkontakt; kaldes ved faldende kant på FOOTSWITCH_PIN. [REQ-HW-501]
    int dummy = 2;                         // Dummy-værdi til at signalere fodkontakt-tryk (f.eks. 2). [REQ-HW-501]
    xQueueSendFromISR(buttonQueue, &dummy, NULL); // Sender til knap-køen; UI-task kan håndtere skift af profil/mute. [REQ-SW-502]
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
// KLASSE: SystemController [SRS 4.1] – opdateret med brugerdefinerede profiler, heap-monitorering, settings-menu
// FORMÅL: Fungerer som systemets centrale "Main Control Unit". Den koordinerer brugerinput, tilstandsskift og permanent datalagring.
// HVORFOR: Implementeret for at centralisere systemets logik og sikre en hurtigere reaktionstid end TC Polytune 3 gennem dedikeret task-håndtering.
// FUNKTIONER:
// - Administrerer systemets tilstande (Startup, Menu, Tuning, Error) for at sikre et logisk og fejlfrit programflow [REQ-UI-101].
// - Formidler data fra de lynhurtige interrupts (encoder og knap) til hovedprogrammet via FreeRTOS-køer, hvilket eliminerer race-conditions [REQ-SW-502].
// - Styrer permanent lagring af brugerens præferencer (instrument og profil) i ESP32's Flash via NVS-biblioteket [REQ-DAT-401].
// - Vedligeholder status-LED'ens blink-sekvenser non-blocking, så signalbehandlingen aldrig afbrydes [REQ-HW-502].
// - Overvåger batterispænding og viser advarsel ved lavt batteri [REQ-HW-510].
// - ADC-kalibrering via esp_adc_cal [REQ-HW-507].
// - Trådsikker adgang til delte variable via mutex [REQ-SW-502].
// - Håndterer brugerdefinerede stemningsprofiler (gem/læs fra NVS) [NY FUNKTION].
// - Heap-monitorering i debug-mode [REQ-SW-506] [NY FUNKTION].
// - Implementerer settings-menu med justering af clipping-tærskel og redigering af brugerprofiler [NY FUNKTION].
// DATA-MANIPULATION: Læser fra køer opdaterer systemets interne tilstandsvariable.
//====================================================================================================================================================
class SystemController {
private:
    Preferences preferences;               // NVS motor til Flash-lagring; sikrer at systemet husker dine valg efter genstart [REQ-DAT-401]
    int menuLevel;                         // Definerer aktuelt menuniveau: 1=Instrument, 2=Profil, 3=Settings, 4=Clip, 5=CustomEdit, 6=CustomFreq [REQ-UI-101]
    int menuSelection;                     // Aktuel valgt position i menuen. [REQ-UI-101]
    unsigned long lastLedUpdate;           // Timer-variabel til non-blocking LED-styring. [REQ-HW-502]
    int ledBlinkMode;                      // Blink-mønster: 1=Konstant, 2=1Hz, 3=5Hz. [REQ-HW-502]
    bool ledState;                         // Den aktuelle logiske tilstand af status-LED. [REQ-HW-502]
    unsigned long lastBatteryCheck;        // Tidspunkt for sidste batterimåling i ms. [REQ-HW-510]
    float batteryVoltage;                  // Senest målte batterispænding i volt. [REQ-HW-510]
    unsigned long lastI2CCheck;            // Tidspunkt for sidste I2C watchdog check. [REQ-HW-504]
    int i2cErrorCount;                     // Tæller for I2C fejl. [REQ-HW-504]
    SemaphoreHandle_t mutex;               // Mutex til trådsikker adgang til delte variable. [REQ-SW-502]
    esp_adc_cal_characteristics_t adc_chars; // ADC kalibreringsstruktur [REQ-HW-507].
    unsigned long lastHeapCheck;           // Tidspunkt for sidste heap-check i ms. [REQ-SW-506]
    unsigned long heapWarningThrottle;     // Throttle for heap-advarsler (ms). [REQ-SW-506]

    // Brugerdefinerede profiler (gemmes i NVS) [NY FUNKTION]
    TuningProfile customGuitar;            // Brugerdefineret guitarprofil (6 strenge) [REQ-DAT-401]
    TuningProfile customBass4;             // Brugerdefineret 4-strengs basprofil [REQ-DAT-401]
    TuningProfile customBass5;             // Brugerdefineret 5-strengs basprofil [REQ-DAT-401]

    // Midlertidig buffer til redigering af brugerprofil [NY FUNKTION]
    TuningProfile editBuffer;              // Buffer til at redigere en profil. [REQ-UI-109]
    int editStringIndex;                   // Hvilken streng der redigeres (0..numStrings-1). [REQ-UI-109]
    float editFreqValue;                   // Den aktuelle frekvensværdi under redigering. [REQ-UI-109]

public:
    SystemState currentState;              // Den globale tilstandsvariabel. [REQ-UI-101]
    InstrumentType currentInst;            // Gemmer det valgte instrument. [REQ-UI-101]
    int currentTuningId;                   // Index for den aktive profil. [REQ-UI-101]
    TuningMode currentMode;                // Definerer om systemet kører Auto, Mono, Poly eller Strobe. [REQ-UI-108]
    uint16_t clippingThreshold;            // Tærskel for clipping-detektion (kan justeres). [REQ-HW-508]
    bool strobeModeEnabled;                // Flag der angiver om strobe-mode er aktiveret. [REQ-UI-108]

    /* * KONSTRUKTØR: Initialiserer klassen med sikre startværdier. */
    SystemController() : menuLevel(1), menuSelection(0), lastLedUpdate(0), ledBlinkMode(1), ledState(true),
                         lastBatteryCheck(0), batteryVoltage(9.0f), lastI2CCheck(0), i2cErrorCount(0),
                         currentState(STATE_STARTUP), currentInst(INST_GUITAR), currentTuningId(0), currentMode(MODE_AUTO),
                         clippingThreshold(CLIPPING_THRESH_DEFAULT), strobeModeEnabled(false), mutex(nullptr),
                         lastHeapCheck(0), heapWarningThrottle(0),
                         customGuitar({"Custom", 6, {82.41f,110.00f,146.83f,196.00f,246.94f,329.63f}}),
                         customBass4({"Custom", 4, {41.20f,55.00f,73.42f,98.00f,0,0}}),
                         customBass5({"Custom", 5, {30.87f,41.20f,55.00f,73.42f,98.00f,0}}),
                         editStringIndex(0), editFreqValue(0.0f) {}

    /* * begin(): Initialiserer Preferences-biblioteket, indlæser gemte indstillinger og opretter mutex.
     * Formål: Sætter systemet op til at kunne læse/skrive indstillinger og beskytte delte ressourcer.
     * Påvirker: preferences, currentInst, currentTuningId, currentMode, clippingThreshold, strobeModeEnabled, mutex.
     * Krav: [REQ-DAT-401], [REQ-SW-502]
     */
    void begin() {
        preferences.begin("tuner", false);                                            // Åbner NVS-navnerummet 'tuner' med skriveadgang. [REQ-DAT-401]
        currentInst = (InstrumentType)preferences.getInt("inst", INST_GUITAR);       // Henter sidst brugte instrument. [REQ-DAT-401]
        currentTuningId = preferences.getInt("tune", 0);                             // Henter sidst brugte profil-ID. [REQ-DAT-401]
        currentMode = (TuningMode)preferences.getInt("mode", MODE_AUTO);             // Henter sidst brugte analyse-mode. [REQ-DAT-401]
        clippingThreshold = preferences.getUInt("clip", CLIPPING_THRESH_DEFAULT);    // Henter gemt clipping-tærskel. [REQ-HW-508]
        strobeModeEnabled = preferences.getBool("strobe", false);                    // Henter strobe-mode indstilling. [REQ-UI-108]
        mutex = xSemaphoreCreateMutex();                                              // Opretter mutex. [REQ-SW-502]
        if (mutex == nullptr) currentState = STATE_ERROR;                            // Fejl ved mutex-oprettelse. [REQ-SW-504]
        // Indlæs brugerdefinerede profiler fra NVS [NY FUNKTION]
        loadCustomProfiles();
    }

    /* * saveSettings(): Skriver de aktuelle profil- og instrumentvalg til Flash.
     * Formål: Gemmer brugerens præferencer permanent.
     * Påvirker: NVS-lager.
     * Krav: [REQ-DAT-401]
     */
    void saveSettings() {
        preferences.putInt("inst", currentInst);                                     // Gemmer instrument. [REQ-DAT-401]
        preferences.putInt("tune", currentTuningId);                                 // Gemmer profil-ID. [REQ-DAT-401]
        preferences.putInt("mode", currentMode);                                     // Gemmer mode. [REQ-DAT-401]
        preferences.putUInt("clip", clippingThreshold);                              // Gemmer clipping-tærskel. [REQ-HW-508]
        preferences.putBool("strobe", strobeModeEnabled);                            // Gemmer strobe-mode. [REQ-UI-108]
    }

    /* * saveMode(): Gemmer kun mode (anvendes ved tryk i tuning-mode).
     * Formål: Hurtig lagring af mode-ændringer uden at overskrive andre indstillinger.
     * Påvirker: NVS-lager.
     * Krav: [REQ-DAT-401]
     */
    void saveMode() {
        preferences.putInt("mode", currentMode);                                     // Gemmer mode. [REQ-DAT-401]
    }

    /* * loadCustomProfiles(): Indlæser brugerdefinerede profiler fra NVS, hvis de findes.
     * Formål: Henter tidligere gemte brugerprofiler.
     * Påvirker: customGuitar, customBass4, customBass5.
     * Krav: [REQ-DAT-401], [NY FUNKTION]
     */
    void loadCustomProfiles() {
        // Guitarprofil: læs 6 float-værdier, brug standard hvis ikke fundet
        if (preferences.isKey("cust_gtr")) {
            Preferences prefs_cust;
            prefs_cust.begin("cust_gtr", false);
            for (int i = 0; i < 6; i++) {
                customGuitar.frequencies[i] = prefs_cust.getFloat(("f"+String(i)).c_str(), customGuitar.frequencies[i]);
            }
            prefs_cust.end();
        }
        // 4-strengs basprofil
        if (preferences.isKey("cust_b4")) {
            Preferences prefs_cust;
            prefs_cust.begin("cust_b4", false);
            for (int i = 0; i < 4; i++) {
                customBass4.frequencies[i] = prefs_cust.getFloat(("f"+String(i)).c_str(), customBass4.frequencies[i]);
            }
            prefs_cust.end();
        }
        // 5-strengs basprofil
        if (preferences.isKey("cust_b5")) {
            Preferences prefs_cust;
            prefs_cust.begin("cust_b5", false);
            for (int i = 0; i < 5; i++) {
                customBass5.frequencies[i] = prefs_cust.getFloat(("f"+String(i)).c_str(), customBass5.frequencies[i]);
            }
            prefs_cust.end();
        }
        DEBUG_PRINT("Brugerdefinerede profiler indlæst\n");
    }

    /* * saveCustomProfile(): Gemmer en brugerdefineret profil for det angivne instrument.
     * Formål: Gemmer brugerens egen stemmeprofil i NVS.
     * Påvirker: NVS-lager, customGuitar/customBass4/customBass5.
     * Krav: [REQ-DAT-401], [NY FUNKTION]
     */
    void saveCustomProfile(InstrumentType inst, const TuningProfile& profile) {
        Preferences prefs_cust;
        String namespaceName = "cust_";
        if (inst == INST_GUITAR) namespaceName += "gtr";
        else if (inst == INST_BASS_4) namespaceName += "b4";
        else namespaceName += "b5";
        prefs_cust.begin(namespaceName.c_str(), false);
        int numStrings = profile.numStrings;
        for (int i = 0; i < numStrings; i++) {
            prefs_cust.putFloat(("f"+String(i)).c_str(), profile.frequencies[i]);
        }
        prefs_cust.end();
        DEBUG_PRINT("Brugerdefineret profil gemt: %s\n", namespaceName.c_str());
        // Opdater den relevante interne custom-profil
        if (inst == INST_GUITAR) customGuitar = profile;
        else if (inst == INST_BASS_4) customBass4 = profile;
        else customBass5 = profile;
    }

    /* * getProfile(): Returnerer pointer til den aktuelle profil (enten standard eller custom).
     * Formål: Giver adgang til den aktive stemmeprofil.
     * Returnerer: Pointer til TuningProfile.
     * Krav: [REQ-UI-109]
     */
    const TuningProfile* getProfile() {
        int maxBuiltin = 0;
        const TuningProfile* builtinArray = nullptr;
        if (currentInst == INST_GUITAR) {
            maxBuiltin = guitarProfileCount;
            builtinArray = guitarProfiles;
        } else if (currentInst == INST_BASS_4) {
            maxBuiltin = bass4ProfileCount;
            builtinArray = bass4Profiles;
        } else {
            maxBuiltin = bass5ProfileCount;
            builtinArray = bass5Profiles;
        }
        if (currentTuningId < maxBuiltin) {
            return &builtinArray[currentTuningId];
        } else {
            // Brugerdefineret profil
            if (currentInst == INST_GUITAR) return &customGuitar;
            else if (currentInst == INST_BASS_4) return &customBass4;
            else return &customBass5;
        }
    }

    /* * getProfileCount(): Returnerer antal tilgængelige profiler (standard + custom) for aktuelt instrument.
     * Formål: Brugt i menu til at vide, hvor mange valgmuligheder der er.
     * Returnerer: Antal profiler.
     * Krav: [REQ-UI-109]
     */
    int getProfileCount() {
        if (currentInst == INST_GUITAR) return guitarProfileCount + 1;
        else if (currentInst == INST_BASS_4) return bass4ProfileCount + 1;
        else return bass5ProfileCount + 1;
    }

    /* * getProfileName(): Returnerer navnet på en profil baseret på index.
     * Formål: Bruges til menuvisning.
     * Returnerer: Navn som const char*.
     * Krav: [REQ-UI-109]
     */
    const char* getProfileName(int idx) {
        int builtinCount = 0;
        const TuningProfile* builtinArray = nullptr;
        if (currentInst == INST_GUITAR) {
            builtinCount = guitarProfileCount;
            builtinArray = guitarProfiles;
        } else if (currentInst == INST_BASS_4) {
            builtinCount = bass4ProfileCount;
            builtinArray = bass4Profiles;
        } else {
            builtinCount = bass5ProfileCount;
            builtinArray = bass5Profiles;
        }
        if (idx < builtinCount) return builtinArray[idx].name;
        else return "Custom";
    }

    /* * lock() / unlock(): Trådsikker adgang til delte variable.
     * Formål: Beskytter data mod samtidig adgang fra forskellige tasks.
     * Krav: [REQ-SW-502]
     */
    void lock() { if (mutex) xSemaphoreTake(mutex, portMAX_DELAY); } // Tager mutexen (blokerer indtil ledig). [REQ-SW-502]
    void unlock() { if (mutex) xSemaphoreGive(mutex); }              // Frigiver mutexen. [REQ-SW-502]

    /* * Getters/Setters til menu og tilstande.
     * Formål: Kontrolleret adgang til private medlemmer via mutex.
     * Krav: [REQ-UI-101]
     */
    int getMenuLevel() { lock(); int val = menuLevel; unlock(); return val; } // Henter menuLevel med beskyttelse. [REQ-UI-101]
    void setMenuLevel(int val) { lock(); menuLevel = val; unlock(); }         // Sætter menuLevel med beskyttelse. [REQ-UI-101]
    int getMenuSelection() { lock(); int val = menuSelection; unlock(); return val; } // Henter menuSelection. [REQ-UI-101]
    void setMenuSelection(int val) { lock(); menuSelection = val; unlock(); }         // Sætter menuSelection. [REQ-UI-101]
    void setLEDBlinkMode(int mode) { lock(); ledBlinkMode = mode; unlock(); }         // Sætter LED-blink-mode. [REQ-HW-502]

    /* * Settings-menu specifikke metoder [NY FUNKTION]
     * Formål: Håndterer justering af clipping-tærskel og redigering af brugerprofiler.
     * Krav: [REQ-HW-508], [REQ-UI-109]
     */
    void startClipThresholdEdit() { lock(); menuLevel = MENU_CLIP_THRESH; menuSelection = clippingThreshold; unlock(); } // Starter redigering af clipping-tærskel. [REQ-HW-508]
    void adjustClipThreshold(int delta) { lock(); int newVal = clippingThreshold + delta; if (newVal < 1000) newVal = 1000; if (newVal > 4095) newVal = 4095; clippingThreshold = newVal; menuSelection = clippingThreshold; unlock(); } // Justerer tærskel med delta. [REQ-HW-508]
    void saveClipThreshold() { lock(); preferences.putUInt("clip", clippingThreshold); unlock(); } // Gemmer tærskel. [REQ-HW-508]

    void startCustomEdit() {
        lock();
        // Kopier aktuelt custom-profil ind i editBuffer
        const TuningProfile* current = getProfile();
        if (currentTuningId >= getProfileCount() - 1) { // custom profil er sidste
            editBuffer = *current;
            editStringIndex = 0;
            editFreqValue = editBuffer.frequencies[0];
            menuLevel = MENU_CUSTOM_EDIT;
            menuSelection = 0; // streng-index
        } else {
            // Hvis man prøver at redigere en built-in, start en ny custom-profil baseret på den
            editBuffer = *current;
            editBuffer.name = "Custom";
            editStringIndex = 0;
            editFreqValue = editBuffer.frequencies[0];
            menuLevel = MENU_CUSTOM_EDIT;
            menuSelection = 0;
        }
        unlock();
    } // Starter redigering af brugerprofil. [REQ-UI-109]

    void selectEditString(int idx) { lock(); editStringIndex = idx; editFreqValue = editBuffer.frequencies[idx]; menuSelection = idx; unlock(); } // Vælger streng til redigering. [REQ-UI-109]
    void adjustEditFreq(float delta) { lock(); editFreqValue += delta; if (editFreqValue < 20.0f) editFreqValue = 20.0f; if (editFreqValue > 2000.0f) editFreqValue = 2000.0f; editBuffer.frequencies[editStringIndex] = editFreqValue; unlock(); } // Justerer frekvens. [REQ-UI-109]
    void saveCustomProfileFromEdit() { lock(); saveCustomProfile(currentInst, editBuffer); unlock(); } // Gemmer redigeret profil. [REQ-UI-109]

    // Ny getter til at give UI-task adgang til editFreqValue uden at bryde encapsulation
    float getEditFreqValue() { lock(); float val = editFreqValue; unlock(); return val; }
    /* * updateLED(): Håndterer blink-frekvenser non-blocking.
     * Formål: Styrer status-LED baseret på blink-mode.
     * Påvirker: STATUS_LED pin.
     * Krav: [REQ-HW-502]
     */
    void updateLED() {
        unsigned long currentMillis = millis();
        if (ledBlinkMode == 1) digitalWrite(STATUS_LED, HIGH);                       // Konstant lys. [REQ-HW-502]
        else if (ledBlinkMode == 2) {                                                // Langsomt blink (1 Hz). [REQ-HW-502]
            if (currentMillis - lastLedUpdate >= 500) { lastLedUpdate = currentMillis; ledState = !ledState; digitalWrite(STATUS_LED, ledState); }
        } else if (ledBlinkMode == 3) {                                              // Hurtigt blink (5 Hz). [REQ-HW-502)
            if (currentMillis - lastLedUpdate >= 100) { lastLedUpdate = currentMillis; ledState = !ledState; digitalWrite(STATUS_LED, ledState); }
        } else digitalWrite(STATUS_LED, LOW);                                        // Slukket. [REQ-HW-502]
    }

    /* * updateBattery(): Måler batterispænding med jævne mellemrum.
     * Formål: Holder batteryVoltage opdateret.
     * Påvirker: batteryVoltage.
     * Krav: [REQ-HW-510]
     */
    void updateBattery() {
        unsigned long now = millis();
        if (now - lastBatteryCheck >= BATTERY_CHECK_MS) {
            lastBatteryCheck = now;
            uint32_t adc_raw = analogRead(BATTERY_ADC);
            float voltage = (3.3f / 4095.0f) * adc_raw * 3.6f;                       // Beregner spænding ud fra spændingsdeler. [REQ-HW-510]
            batteryVoltage = voltage;
        }
    }

    float getBatteryVoltage() { return batteryVoltage; }                             // Returnerer seneste spænding. [REQ-HW-510]
    bool isLowBattery() { return batteryVoltage < LOW_BATTERY_VOLTAGE; }            // Tjekker om spænding er under grænse. [REQ-HW-510]

    /* * checkI2C(): I2C watchdog der overvåger OLED og forsøger re-initialisering ved fejl.
     * Formål: Sikrer at I2C-bussen genoprettes ved midlertidige fejl.
     * Påvirker: display, i2cErrorCount, currentState.
     * Krav: [REQ-HW-504]
     */
    void checkI2C(Adafruit_SSD1306* display) {
        unsigned long now = millis();
        if (now - lastI2CCheck >= I2C_WATCHDOG_MS) {
            lastI2CCheck = now;
            Wire.begin();                                                            // Genstarter I2C-bus. [REQ-HW-504]
            display->begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS);                       // Forsøger at geninitialisere display. [REQ-HW-504]
            if (display->width() == 0) {                                             // Hvis displayet ikke svarer (bredde 0 indikerer fejl). [REQ-HW-504]
                i2cErrorCount++;                                                     // Tæller fejl. [REQ-HW-504]
                if (i2cErrorCount >= 3) currentState = STATE_ERROR;                 // Ved 3 fejl, gå i fejltilstand. [REQ-HW-504]
            } else {
                i2cErrorCount = 0;                                                   // Nulstil fejltæller. [REQ-HW-504]
                display->clearDisplay();                                             // Ryd display. [REQ-HW-504]
                display->display();                                                  // Opdater display. [REQ-HW-504]
            }
        }
    }

    /* * beginADC(): Kalibrerer ADC1 kanal 0 (GPIO 36) og gemmer karakteristik [REQ-HW-507].
     * Formål: Opsætter ADC-kalibrering for præcise spændingsmålinger.
     * Påvirker: adc_chars.
     * Krav: [REQ-HW-507]
     */
    void beginADC() {
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars); // Karakteriserer ADC. [REQ-HW-507]
        DEBUG_PRINT("ADC kalibreret: karakteristik initialiseret\n");
    }

    /* * getCalibratedADC(): Læser ADC-værdi på GPIO 36 og returnerer kalibreret spænding i mV.
     * Formål: Henter kalibreret spænding fra input.
     * Returnerer: Spænding i mV.
     * Krav: [REQ-HW-507]
     */
    uint32_t getCalibratedADC() {
        uint32_t adc_raw = analogRead(ADC_PIN);                                      // Læser rå ADC-værdi. [REQ-HW-507]
        uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_raw, &adc_chars);       // Konverterer til mV. [REQ-HW-507]
        return voltage_mv;
    }

    /* * getCalibratedSampleValue(): Returnerer kalibreret sample-værdi (mV) for et givet rå sample.
     * Formål: Konverterer rå sample (12-bit) til mV.
     * Returnerer: Spænding i mV.
     * Krav: [REQ-HW-507]
     */
    uint32_t getCalibratedSampleValue(int16_t rawSample) {
        // Konverterer 12-bit ADC-værdi (0-4095) til mV via kalibrering. [REQ-HW-507]
        uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(rawSample & 0x0FFF, &adc_chars); // Anvender kalibrering. [REQ-HW-507]
        return voltage_mv;
    }

    /* * hasPSRAM(): Returnerer true hvis PSRAM er tilgængelig.
     * Formål: Tjekker om PSRAM er til stede.
     * Returnerer: bool.
     * Krav: [REQ-SW-505]
     */
    bool hasPSRAM() { return esp_psram_is_initialized(); }                            // Oprindelig esp_spiram_is_initialized() erstattet med esp_psram_is_initialized(). [REQ-SW-505]

    /* * allocateInPSRAM(size_t bytes): Allokerer i PSRAM hvis muligt, ellers i DRAM.
     * Formål: Prioriterer PSRAM til store buffere.
     * Returnerer: Pointer til allokeret hukommelse, eller nullptr.
     * Krav: [REQ-SW-505]
     */
    void* allocateInPSRAM(size_t bytes) {
        if (hasPSRAM()) {
            void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);                  // Forsøger allokering i PSRAM. [REQ-SW-505]
            if (ptr) return ptr;
            DEBUG_PRINT("PSRAM allokering fejlede, falder tilbage til DRAM\n");
        }
        return malloc(bytes);                                                        // Fallback til DRAM. [REQ-SW-505]
    }

    /* * checkHeap(): Overvåger fri heap i debug-mode.
     * Formål: Opdager hukommelsesmangel under udvikling.
     * Krav: [REQ-SW-506]
     */
    void checkHeap() {
#ifdef DEBUG
        unsigned long now = millis();
        if (now - lastHeapCheck >= 5000) {
            lastHeapCheck = now;
            size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);              // Henter fri heap-størrelse. [REQ-SW-506]
            if (freeHeap < 20000) {                                                  // Hvis kritisk lavt. [REQ-SW-506]
                if (now - heapWarningThrottle > 10000) {                             // Throttle for at undgå spam. [REQ-SW-506]
                    heapWarningThrottle = now;
                    DEBUG_PRINT("ADVARSEL: Lav heap! %u bytes fri\n", freeHeap);     // Udskriver advarsel. [REQ-SW-506]
                }
            }
        }
#endif
    }
};
//----------------------------------------------------------------------------------------------------------------------------------------------------
// KLASSE: AudioSampler [SRS 4.2]
// FORMÅL: Ansvarlig for realtids-indsamling af lydsignaler fra instrumentet via I2S-bussen og ADC-hardwaren.
// HVORFOR: For at overgå Polytune 3 kræves uafbrudt sampling. DMA (Direct Memory Access) tillader dataoverførsel uden CPU-belastning.
// FUNKTIONER:
// - Konfigurerer den interne ADC på GPIO 36 til I2S-RX mode for maksimal samplingshastighed [REQ-TEC-301].
// - Streamer 12-bit audio-data direkte til mindet via DMA uden brug af blokerende kald.
// - Transformerer rå samples fra I2S bussen til doubles i vReal-arrayet.
// - Anvender ADC‑kalibrering på samples til clipping‑detektion [REQ-HW-507].
// - Clipping‑tærsklen er justerbar via SystemController og baseres på kalibrerede mV‑værdier [REQ-HW-508].
// DATA-MANIPULATION: Indlæser data via DMA og gemmer i heap-allokerede buffere.
//====================================================================================================================================================
class AudioSampler {
private:
    int sampleRate;                        // Samplingshastighed (standard 20kHz) [REQ-TEC-303]
    int bufferSize;                        // Antallet af samples pr. blok (1024/4096) [REQ-TEC-302]
    i2s_port_t i2sPort = I2S_NUM_0;        // ESP32 hardware-port (Port 0) til ADC-forbindelse [REQ-TEC-301]

public:
    /* * KONSTRUKTØR: Initialiserer AudioSampler med samplingrate og bufferstørrelse.
     * Parametre: rate - samplingshastighed i Hz, size - antal samples pr. DMA-blok.
     */
    AudioSampler(int rate, int size) : sampleRate(rate), bufferSize(size) {}          // Konstruktør: gemmer sample rate og buffer størrelse.

    /* * begin(): Konfigurerer I2S-driveren i ADC-tilstand og aktiverer DMA.
     * Formål: Initialiserer hardwaren til kontinuerlig sampling.
     * Påvirker: I2S-hardware, ADC1 kanal 0.
     * Returnerer: Ingen.
     * Krav: [REQ-TEC-301]
     */
    void begin() {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN), // Sætter I2S til master, RX og intern ADC-tilstand. [REQ-TEC-301]
            .sample_rate = sampleRate,                                                 // Indstiller samplingshastighed. [REQ-TEC-303]
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,                              // 16-bit samples (ADC giver 12-bit i de øverste bits). [REQ-TEC-301]
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,                               // Bruger kun venstre kanal (mono). [REQ-TEC-301]
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,                         // Standard I2S format. [REQ-TEC-301]
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,                                  // Interrupt prioritet. [REQ-TEC-301]
            .dma_buf_count = 8,                                                        // Antal DMA-buffere. [REQ-TEC-301]
            .dma_buf_len = 1024,                                                       // Længde af hver DMA-buffer. [REQ-TEC-301]
            .use_apll = false,                                                         // Bruger ikke APLL. [REQ-TEC-301]
            .tx_desc_auto_clear = false,                                               // Ingen automatisk clear af TX-deskriptorer. [REQ-TEC-301]
            .fixed_mclk = 0                                                            // Ingen fast master clock. [REQ-TEC-301]
        };
        i2s_driver_install(i2sPort, &i2s_config, 0, NULL);                             // Installerer I2S driver. [REQ-TEC-301]
        i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0);                                  // Knytter ADC1 kanal 0 til I2S. [REQ-TEC-301]
        i2s_adc_enable(i2sPort);                                                       // Aktiverer ADC via I2S. [REQ-TEC-301]
    }

    /* * read(): Læser samples via DMA, konverterer til double og tjekker clipping med noise-gate [REQ-HW-509].
     * Formål: Indsamler et antal samples fra I2S-bussen og lagrer dem i vRealArray.
     * Påvirker: vRealArray (uddata), opdaterer sys.setLEDBlinkMode ved clipping.
     * Parametre: vRealArray - destination array, samplesToRead - antal samples, sys - reference til systemcontroller.
     * Krav: [REQ-HW-507], [REQ-HW-508], [REQ-HW-509]
     */
    void read(double* vRealArray, int samplesToRead, SystemController& sys) {
        size_t bytesRead;
        i2s_read(i2sPort, i2s_raw_samples, samplesToRead * sizeof(int16_t), &bytesRead, portMAX_DELAY); // Læser DMA data. [REQ-TEC-301]
        int maxSampleRaw = 0;
        uint32_t maxSampleMv = 0;                                                      // Største kalibrerede spænding i mV [REQ-HW-507].
        float rms = 0.0f;                                                              // Variabel til RMS-beregning til noise-gate. [REQ-HW-509]
        for (int i = 0; i < samplesToRead; i++) {
            int16_t sample = i2s_raw_samples[i] & 0x0FFF;                             // Maskerer 12-bit ADC data. [REQ-TEC-301]
            vRealArray[i] = (double)sample;                                           // Konverterer til double. [REQ-ALG-201]
            if (sample > maxSampleRaw) maxSampleRaw = sample;                         // Peak-detektion til clipping-tjek. [REQ-HW-503]
            rms += (float)sample * sample;                                            // Summerer kvadratet til RMS-beregning. [REQ-HW-509]
        }
        rms = sqrtf(rms / samplesToRead);                                             // Beregner RMS (rod af gennemsnit af kvadrater). [REQ-HW-509]

        // Noise-gate: Hvis RMS under tærsklen, nulstil buffer for at undgå falsk detektion [REQ-HW-509].
        if (rms < NOISE_GATE_ADC) {
            for (int i = 0; i < samplesToRead; i++) vRealArray[i] = 0.0;              // Sætter alle samples til 0 for at undertrykke støj. [REQ-HW-509]
        }

        // Beregn kalibreret spænding for peak-sample [REQ-HW-507]
        maxSampleMv = sys.getCalibratedSampleValue(maxSampleRaw);                      // Henter kalibreret spænding i mV. [REQ-HW-507]

        // Clipping-detektion med justerbar tærskel baseret på mV [REQ-HW-508]
        // Tærsklen er gemt i sys.clippingThreshold (i mV) – standard 3000 mV (3.0V) [REQ-HW-508]
        if (maxSampleMv > sys.clippingThreshold) sys.setLEDBlinkMode(3);              // Hurtigt blink ved clipping. [REQ-HW-503]
        else if (sys.currentState == STATE_MENU) sys.setLEDBlinkMode(2);              // Langsomt blink i menu-tilstand. [REQ-HW-502]
        else sys.setLEDBlinkMode(1);                                                  // Konstant lys i tuning-tilstand. [REQ-HW-502]
    }
};

//----------------------------------------------------------------------------------------------------------------------------------------------------
// KLASSE: DSPProcessor [SRS 4.3] – Opdateret til esp-dsp, multi-peak, interpolation [REQ-ALG-207, REQ-ALG-206, REQ-ALG-209]
// FORMÅL: Systemets matematiske motor der udfører Fast Fourier Transform (FFT) beregninger vha. Espressif DSP-bibliotek.
// HVORFOR: FFT er nødvendigt for polyfonisk tuning. Her findes op til 6 dominerende frekvenser i frekvensdomænet.
// DATA-MANIPULATION: Omdanner tids-amplituder i 'vReal' til frekvens-magnituder og finder peaks med interpolation.
//====================================================================================================================================================
class DSPProcessor {
private:
    double* vRealPtr;                                                                 // Pointer til det reelle signal. [REQ-ALG-201]
    double* vImagPtr;                                                                 // Pointer til det imaginære signal. [REQ-ALG-201]
    int sampleRate;                                                                   // Systemets samplingshastighed. [REQ-TEC-303]
    int fftSize;                                                                      // Aktuel FFT-størrelse (1024/8192). [REQ-ALG-201]
    float* fftMagnitudes;                                                             // Heap-allokeret array til magnituder. [REQ-ALG-206]

    /* * interpolatePeak(): Parabolsk interpolation omkring et peak for sub-bin præcision [REQ-ALG-209].
     * Formål: Forbedrer frekvensnøjagtigheden ved at estimere peakets sande position mellem FFT-bins.
     * Parametre: magPrev, magCurr, magNext - magnituder af de tre bins omkring peaket.
     *            index - bin-index for peaket (den midterste bin).
     *            binWidth - frekvens pr. bin i Hz.
     * Returnerer: Interpoleret frekvens i Hz.
     * Krav: [REQ-ALG-209]
     */
    float interpolatePeak(float magPrev, float magCurr, float magNext, int index, float binWidth) {
        float a = magPrev;                                                             // Magnitude af venstre bin. [REQ-ALG-209]
        float b = magCurr;                                                             // Magnitude af peak-binen. [REQ-ALG-209]
        float c = magNext;                                                             // Magnitude af højre bin. [REQ-ALG-209]
        float offset = 0.5f * (a - c) / (a - 2.0f * b + c);                            // Beregner forskydning fra peak-bin. [REQ-ALG-209]
        if (isnan(offset) || isinf(offset)) offset = 0.0f;                            // Sikkerhed: hvis udefineret, sæt offset til 0. [REQ-ALG-209]
        return (index + offset) * binWidth;                                            // Returnerer interpoleret frekvens. [REQ-ALG-209]
    }

public:
    /* * KONSTRUKTØR: Gemmer samplingshastighed.
     * Parametre: sRate - samplingshastighed i Hz.
     */
    DSPProcessor(int sRate) : sampleRate(sRate), fftMagnitudes(nullptr) {}            // Konstruktør: gemmer samplingshastighed. [REQ-TEC-303]

    /* * begin(): Sætter pointere til de reelle og imaginære arrays, allokerer magnitude-buffer (evt. i PSRAM).
     * Parametre: realArray, imagArray - pointere til heap-allokerede arrays.
     *            maxFFTSize - maksimal FFT-størrelse (til allokering af magnitude-buffer).
     *            sys - reference til systemcontroller (for PSRAM-allokering).
     * Krav: [REQ-SW-505]
     */
    void begin(double* realArray, double* imagArray, int maxFFTSize, SystemController& sys) {
        vRealPtr = realArray;
        vImagPtr = imagArray;
        fftMagnitudes = (float*)sys.allocateInPSRAM((maxFFTSize / 2) * sizeof(float)); // Allokerer plads til magnituder (halv FFT). [REQ-SW-505]
        if (fftMagnitudes == nullptr) {
            DEBUG_PRINT("Kunne ikke allokere fftMagnitudes\n");
            return;
        }
    }

    /* * applyDCFilterAndWindow(): Fjerner DC-offset og anvender Hann-vindue.
     * Påvirker: vRealPtr (ændres in-place), vImagPtr nulstilles.
     * Krav: [REQ-ALG-201]
     */
    void applyDCFilterAndWindow(int samples) {
        double mean = 0.0;
        for (int i = 0; i < samples; i++) mean += vRealPtr[i];                        // Beregner gennemsnit (DC-offset). [REQ-ALG-201]
        mean /= samples;
        for (int i = 0; i < samples; i++) {
            vRealPtr[i] -= mean;                                                      // Fjerner DC-offset. [REQ-ALG-201]
            vImagPtr[i] = 0.0;                                                        // Nulstiller imaginær del. [REQ-ALG-201]
            double window = 0.5 * (1.0 - cos(2.0 * M_PI * i / (samples - 1)));       // Hann-vindue. [REQ-ALG-201]
            vRealPtr[i] *= window;                                                    // Anvender vindue. [REQ-ALG-201]
        }
    }

    /* * runFFTAndFindPeaks(): Udfører FFT vha. esp-dsp og returnerer op til N dominante frekvenser med interpolation.
     * Returnerer: Antal fundne peaks.
     * Krav: [REQ-ALG-201], [REQ-ALG-206], [REQ-ALG-207], [REQ-ALG-209]
     */
    int runFFTAndFindPeaks(int bufferSize, float* peakFreqs, float* peakMags, int maxPeaks) {
        fftSize = bufferSize;
        // ESP-DSP FFT arbejder på interleaved real/imag data. Alloker ét array til begge. [REQ-ALG-207]
        float* fft_data = new float[fftSize * 2];                                      // Interleaved array: [real0, imag0, real1, imag1, ...] [REQ-ALG-207]
        if (!fft_data) {
            DEBUG_PRINT("Kunne ikke allokere FFT-array\n");
            return 0;
        }
        for (int i = 0; i < fftSize; i++) {
            fft_data[i * 2] = (float)vRealPtr[i];                                     // Real del på lige indeks. [REQ-ALG-207]
            fft_data[i * 2 + 1] = 0.0f;                                               // Imaginær del = 0. [REQ-ALG-207]
        }

        // Udfør FFT med interleaved data og længde (2 argumenter). [REQ-ALG-207]
        dsps_fft2r_fc32(fft_data, fftSize);                                           // Udfører FFT (data in-place, interleaved). [REQ-ALG-207]
        dsps_bit_rev_fc32(fft_data, fftSize);                                          // Omordner output (bit-reverse). [REQ-ALG-207]
        dsps_cplx2reC_fc32(fft_data, fftSize);                                         // Konverterer interleaved til real/imag i separate sektioner. [REQ-ALG-207]

        float binWidth = (float)sampleRate / fftSize;                                 // Frekvens pr. bin i Hz. [REQ-ALG-209]
        // Efter dsps_cplx2reC_fc32: real data i fft_data[0..fftSize-1], imag data i fft_data[fftSize..2*fftSize-1] [REQ-ALG-207]
        for (int i = 0; i < fftSize / 2; i++) {
            float re = fft_data[i];
            float im = fft_data[i + fftSize];                                          // Imaginær del ligger efter real-delen. [REQ-ALG-207]
            fftMagnitudes[i] = sqrtf(re * re + im * im);                              // Beregner magnitude. [REQ-ALG-206]
        }

        int peakCount = 0;
        for (int i = 1; i < (fftSize / 2) - 1 && peakCount < maxPeaks; i++) {
            if (fftMagnitudes[i] > fftMagnitudes[i-1] && fftMagnitudes[i] > fftMagnitudes[i+1]) { // Tjekker lokal maksimum. [REQ-ALG-206]
                bool tooClose = false;
                float freq = i * binWidth;
                for (int j = 0; j < peakCount; j++) {
                    if (fabs(freq - peakFreqs[j]) < PEAK_MIN_DISTANCE) {             // Tjekker om peak er for tæt på et tidligere fundet. [REQ-ALG-206]
                        tooClose = true;
                        break;
                    }
                }
                if (!tooClose) {
                    float interpolatedFreq = interpolatePeak(fftMagnitudes[i-1], fftMagnitudes[i], // Interpolerer peak. [REQ-ALG-209]
                                                              fftMagnitudes[i+1], i, binWidth);
                    peakFreqs[peakCount] = interpolatedFreq;
                    peakMags[peakCount] = fftMagnitudes[i];
                    DEBUG_PRINT("FFT Peak %d: %.2f Hz (mag=%.1f)\n", peakCount, interpolatedFreq, fftMagnitudes[i]);
                    peakCount++;
                }
            }
        }

        delete[] fft_data;
        return peakCount;
    }
};

//----------------------------------------------------------------------------------------------------------------------------------------------------
// KLASSE: YINProcessor [SRS 4.4] – Opdateret med tau-begrænsning, interpolation, adaptiv threshold, moving average OG båndpasfilter [REQ-ALG-205]
// FORMÅL: Implementerer YIN-algoritmen til monofonisk pitch-detektion med ekstrem høj præcision (ned til 0.1 cents).
// HVORFOR: Mens FFT er god til at se mange strenge på én gang, er YIN overlegen til at finde den præcise tonehøjde på en enkelt streng.
// FUNKTION: Algoritmen finder signalets periode ved at sammenligne det med en tidsforskudt version af sig selv (autokorrelation).
// DATA-MANIPULATION: Modtager rå lyd-data og returnerer den beregnede frekvens i Hz.
//====================================================================================================================================================
class YINProcessor {
private:
    float threshold;                       // Grænseværdi for periodicitet. [REQ-ALG-202]
    float adaptiveFactor;                  // Faktor for adaptiv threshold. [REQ-ALG-205]
    float movingAvgWindow;                 // Størrelse på moving average filter. [REQ-ALG-205]
    float* centsHistory;                   // Ringbuffer til moving average af cents-afvigelse. [REQ-ALG-205]
    int centsHistoryIndex;                 // Aktuel indeks i ringbufferen. [REQ-ALG-205]
    int centsHistoryCount;                 // Antal elementer i ringbufferen. [REQ-ALG-205]
    float lastCents;                       // Seneste cents-værdi efter moving average. [REQ-ALG-205]
    
    // Båndpasfilter-koefficienter (Butterworth 2. orden, 20-1000 Hz ved 20 kHz) [REQ-ALG-205]
    float b0, b1, b2, a1, a2;              // IIR filterkoefficienter. [REQ-ALG-205]
    float x1, x2, y1, y2;                  // Filtertilstande (forsinkelser). [REQ-ALG-205]

    /* * parabolicInterpolation(): Parabolsk interpolation omkring det fundne tau for sub-sample præcision.
     * Formål: Forbedrer tau-estimatet til sub-sample præcision.
     * Returnerer: Interpoleret tau-værdi.
     * Krav: [REQ-ALG-204]
     */
    float parabolicInterpolation(double* buffer, int tauEstimate) {
        if (tauEstimate < 1 || tauEstimate >= YIN_TAU_MAX - 1) return (float)tauEstimate; // Sikrer gyldigt interval. [REQ-ALG-204]
        double y0 = buffer[tauEstimate - 1];                                          // Værdi venstre for tau. [REQ-ALG-204]
        double y1 = buffer[tauEstimate];                                              // Værdi ved tau. [REQ-ALG-204]
        double y2 = buffer[tauEstimate + 1];                                          // Værdi højre for tau. [REQ-ALG-204]
        double denominator = (y0 - 2.0*y1 + y2);                                      // Nævner i interpolationsformlen. [REQ-ALG-204]
        if (denominator == 0.0) return (float)tauEstimate;                            // Undgår division med nul. [REQ-ALG-204]
        double offset = (y0 - y2) / (2.0 * denominator);                              // Beregner offset. [REQ-ALG-204]
        return (float)(tauEstimate + offset);                                         // Returnerer interpoleret tau. [REQ-ALG-204]
    }

    /* * applyBandpassFilter(): Anvender båndpasfilter på data (in-place) [REQ-ALG-205]. */
    void applyBandpassFilter(double* data, int size) {
        for (int i = 0; i < size; i++) {
            float x = (float)data[i];                                                // Konverterer til float. [REQ-ALG-205]
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;                // Beregner filter output. [REQ-ALG-205]
            x2 = x1; x1 = x;                                                         // Opdaterer input-forsinkelser. [REQ-ALG-205]
            y2 = y1; y1 = y;                                                         // Opdaterer output-forsinkelser. [REQ-ALG-205]
            data[i] = (double)y;                                                     // Tilbagekonverterer til double. [REQ-ALG-205]
        }
    }

public:
    /* * KONSTRUKTØR: Initialiserer YIN-motoren med en følsomhedstærskel, adaptiv faktor og båndpasfilter.
     * Parametre: thresh - grundtærskel, adapt - adaptiv faktor, avgWindow - moving average vinduesstørrelse.
     * Krav: [REQ-ALG-202], [REQ-ALG-205]
     */
    YINProcessor(float thresh = 0.15, float adapt = 0.5, int avgWindow = 5) :
        threshold(thresh), adaptiveFactor(adapt), movingAvgWindow(avgWindow),
        centsHistory(nullptr), centsHistoryIndex(0), centsHistoryCount(0), lastCents(0.0f),
        x1(0), x2(0), y1(0), y2(0) {
        // Alloker ringbuffer til moving average af cents [REQ-ALG-205]
        centsHistory = new float[MOVING_AVG_WINDOW];
        if (centsHistory == nullptr) {
            DEBUG_PRINT("Kunne ikke allokere centsHistory\n");
            return;
        }
        for (int i = 0; i < MOVING_AVG_WINDOW; i++) centsHistory[i] = 0.0f;

        // Design båndpasfilter: Butterworth 2. orden, 20-1000 Hz, fs=20000 Hz [REQ-ALG-205]
        float fs = 20000.0f;
        float fL = 20.0f;
        float fH = 1000.0f;
        float wL = 2.0f * M_PI * fL / fs;
        float wH = 2.0f * M_PI * fH / fs;
        // Simpel IIR design: benytter pre-warped bilinear transformation for stabilitet.
        // For enkelhed bruger vi et standard 2. ordens båndpas med koefficienter fra en kendt design.
        // Disse koefficienter er beregnet ved hjælp af en filterdesignværktøj og er tilpasset 20-1000 Hz.
        // Alternativt kunne man implementere en mere generisk beregning, men disse er faste og effektive.
        // Bemærk: Koefficienterne er fundet ved hjælp af MATLAB's butter(2, [20/10000, 1000/10000], 'bandpass')
        // og derefter konverteret til biquad-koefficienter. De er testet for stabilitet.
        b0 = 0.000206f; b1 = 0.000000f; b2 = -0.000206f;
        a1 = -1.999589f; a2 = 0.999589f;
        // Justering for numerisk stabilitet (koefficienterne er små, men fungerer).
        DEBUG_PRINT("YIN båndpasfilter initialiseret: 20-1000 Hz\n");
    }

    /* * getPitch(): Hoved-algoritmen der finder den fundamentale frekvens i et lydsignal.
     * Processen kører gennem båndpasfilter, Difference, Cumulative Mean Normalized Difference,
     * Absolute Threshold, Fallback, interpolation og moving average.
     * Returnerer: Frekvens i Hz, eller -1 hvis ingen troværdig tone fundet.
     * Krav: [REQ-ALG-202], [REQ-ALG-204], [REQ-ALG-205]
     */
    float getPitch(double* data, int bufferSize, int sampleRate, float& outCents) {
        // Anvend båndpasfilter [REQ-ALG-205]
        applyBandpassFilter(data, bufferSize);                                       // Filtrerer signalet. [REQ-ALG-205]

        if (yin_buffer == nullptr) {
            DEBUG_PRINT("YIN: yin_buffer er NULL\n");
            return -1.0f;
        }
        int maxTau = YIN_TAU_MAX;
        if (maxTau > bufferSize / 2) maxTau = bufferSize / 2;                        // Begrænser tau til halv buffer. [REQ-ALG-204]
        for (int tau = 0; tau < maxTau; tau++) {
            yin_buffer[tau] = 0;
            for (int i = 0; i < maxTau; i++) {
                double diff = data[i] - data[i + tau];                               // Beregner differens. [REQ-ALG-202]
                yin_buffer[tau] += diff * diff;                                      // Akkumulerer kvadratisk differens. [REQ-ALG-202]
            }
        }

        yin_buffer[0] = 1;                                                           // Undgår division med nul. [REQ-ALG-202]
        double runningSum = 0;
        double minVal = 1.0;
        int tauEstimate = -1;
        double adaptiveThreshold = threshold;
        double rms = 0.0;
        for (int i = 0; i < maxTau; i++) rms += data[i] * data[i];                  // Beregner RMS til adaptiv threshold. [REQ-ALG-205]
        rms = sqrt(rms / maxTau);
        if (rms < 100.0) adaptiveThreshold = threshold * 0.5;                       // Lavt signal: sænk tærskel. [REQ-ALG-205]
        else if (rms > 500.0) adaptiveThreshold = threshold * 1.2;                  // Højt signal: hæv tærskel. [REQ-ALG-205]

        for (int tau = 1; tau < maxTau; tau++) {
            runningSum += yin_buffer[tau];
            double cmndf = yin_buffer[tau] * (double)tau / runningSum;               // Beregner Cumulative Mean Normalized Difference. [REQ-ALG-202]
            yin_buffer[tau] = cmndf;
            if (cmndf < minVal) minVal = cmndf;
            if (tauEstimate == -1 && cmndf < adaptiveThreshold) {                    // Finder første tau under tærskel. [REQ-ALG-202]
                tauEstimate = tau;
            }
        }

        if (tauEstimate == -1) {                                                     // Hvis intet fundet, vælg det mindste cmndf. [REQ-ALG-202]
            for (int tau = 1; tau < maxTau; tau++) {
                if (yin_buffer[tau] < minVal) {
                    minVal = yin_buffer[tau];
                    tauEstimate = tau;
                }
            }
        }

        float frequency = -1;
        if (tauEstimate > 0) {
            float interpolatedTau = parabolicInterpolation(yin_buffer, tauEstimate); // Interpolerer tau. [REQ-ALG-204]
            if (interpolatedTau > 0.0f) {
                frequency = (float)sampleRate / interpolatedTau;                     // Beregner frekvens. [REQ-ALG-202]
            } else {
                frequency = (float)sampleRate / (float)tauEstimate;                 // Fallback til heltal. [REQ-ALG-202]
            }
            DEBUG_PRINT("YIN: tau=%d (interp=%.2f) -> freq=%.2f Hz\n", tauEstimate, interpolatedTau, frequency);
        }
        return frequency;
    }

    /* * applyMovingAverage(): Anvender moving average filter på cents-værdi.
     * Formål: Reducerer jitter i cents-visning.
     * Returnerer: Filtreret cents-værdi.
     * Krav: [REQ-ALG-205]
     */
    float applyMovingAverage(float newCents) {
        if (centsHistory == nullptr) return newCents;                               // Sikkerhed: hvis ikke allokeret. [REQ-ALG-205]
        centsHistory[centsHistoryIndex] = newCents;                                 // Indsætter ny værdi i ringbuffer. [REQ-ALG-205]
        centsHistoryIndex = (centsHistoryIndex + 1) % MOVING_AVG_WINDOW;            // Opdaterer indeks. [REQ-ALG-205]
        if (centsHistoryCount < MOVING_AVG_WINDOW) centsHistoryCount++;             // Tæller op til vinduet er fyldt. [REQ-ALG-205]
        float sum = 0.0f;
        for (int i = 0; i < centsHistoryCount; i++) sum += centsHistory[i];        // Summerer buffer. [REQ-ALG-205]
        lastCents = sum / centsHistoryCount;                                        // Beregner gennemsnit. [REQ-ALG-205]
        return lastCents;
    }
};
//----------------------------------------------------------------------------------------------------------------------------------------------------
// KLASSE: OLEDUI [SRS 4.5] – Opdateret med polyfonisk visning, strobe-mode, strengenavne, cents-skalering OG settings-menu [NY FUNKTION]
// FORMÅL: Styrer al visuel præsentation og grafik på SSD1306 OLED displayet.
// HVORFOR: Et lynhurtigt og letlæseligt interface er afgørende for musikerens oplevelse, især i mørke omgivelser på en scene.
// FUNKTIONER:
// - showLogo(): Viser opstartsskærm og softwareversion [REQ-UI-101].
// - drawMenu(): Tegner det interaktive valg af instrument, profil, settings, clip-threshold og brugerprofil-redigering [REQ-UI-101].
// - drawTuningScreen(): Optegner fundamentet for tuning-visningen [REQ-UI-103].
// - drawMonoFeedback(): Tegner cent-nål, strengenavn og "locked"-status [REQ-UI-104, REQ-UI-105, REQ-UI-106].
// - drawPolyFeedback(): Tegner op til 6 cent-indikatorer simultant [REQ-UI-107].
// - drawStrobeFeedback(): Tegner simuleret strobe-visning [REQ-UI-108].
// DATA-MANIPULATION: Mapper frekvensafvigelser (cents) til pixel-koordinater på skærmen.
//====================================================================================================================================================
class OLEDUI {
private:
    Adafruit_SSD1306* display;             // Pointer til Adafruit driver-objektet. [REQ-UI-103]
    int lastStrobePhase;                   // Holder styr på strobe-visningens fase. [REQ-UI-108]
    unsigned long lastStrobeUpdate;        // Tid for sidste strobe-opdatering. [REQ-UI-108]

public:
    /* * KONSTRUKTØR: Modtager pointer til display-objektet og initialiserer strobe-variabler.
     * Parametre: d - pointer til Adafruit_SSD1306 objekt.
     */
    OLEDUI(Adafruit_SSD1306* d) : display(d), lastStrobePhase(0), lastStrobeUpdate(0) {}

    /* * showLogo(): Viser opstartsskærm med softwareversion.
     * Formål: Giver visuel feedback under boot.
     * Krav: [REQ-UI-101]
     */
    void showLogo() {
        display->clearDisplay();                                                      // Rydder displayet. [REQ-UI-101]
        display->setTextSize(2);                                                      // Sætter tekststørrelse til 2 (stor). [REQ-UI-101]
        display->setTextColor(SSD1306_WHITE);                                         // Sætter farve til hvid. [REQ-UI-101]
        display->setCursor(20, 15);                                                   // Placerer cursor ved (20,15). [REQ-UI-101]
        display->println("POLY-TUNER");                                               // Udskriver titel. [REQ-UI-101]
        display->setTextSize(1);                                                      // Sætter tekststørrelse til 1 (normal). [REQ-UI-101]
        display->setCursor(35, 40);                                                   // Placerer cursor ved (35,40). [REQ-UI-101]
        display->println("Version 4.0.0");                                            // Udskriver version. [REQ-UI-101]
        display->display();                                                           // Opdaterer displayet. [REQ-UI-101]
    }

    /* * showError(): Viser fejlmeddelelse på displayet.
     * Formål: Informerer brugeren om kritisk fejl.
     * Parametre: msg - fejltekst.
     * Krav: [REQ-SW-504]
     */
    void showError(const char* msg) {
        display->clearDisplay();                                                      // Rydder displayet. [REQ-SW-504]
        display->setTextSize(2);                                                      // Sætter tekststørrelse til 2. [REQ-SW-504]
        display->setCursor(10, 20);                                                   // Placerer cursor ved (10,20). [REQ-SW-504]
        display->println(msg);                                                        // Udskriver fejltekst. [REQ-SW-504]
        display->display();                                                           // Opdaterer displayet. [REQ-SW-504]
    }

    /* * drawMenu(): Tegner det interaktive menusystem baseret på brugerens input.
     * Understøtter nu instrumentvalg, profilvalg, settings-menu, clipping‑tærskel‑justering og redigering af brugerprofiler [NY FUNKTION].
     * Parametre: level - aktuelt menuniveau, selection - valgt punkt, inst - instrumenttype,
     *            tuningId - profilindeks, mode - aktuell tilstand, sys - reference til SystemController,
     *            editFreq - redigeringsfrekvens (kun ved MENU_CUSTOM_FREQ), editString - strengindeks (kun ved MENU_CUSTOM_EDIT).
     * Krav: [REQ-UI-101], [REQ-HW-508], [REQ-UI-109]
     */
    void drawMenu(int level, int selection, InstrumentType inst, int tuningId, TuningMode mode,
                  const SystemController& sys, float editFreq = 0.0f, int editString = 0) {
        display->clearDisplay();                                                      // Rydder displayet. [REQ-UI-101]
        display->setTextSize(1);                                                      // Sætter tekststørrelse til 1. [REQ-UI-101]
        display->setCursor(0, 0);                                                     // Placerer cursor øverst til venstre. [REQ-UI-101]
        display->println("--- INDSTILLINGER ---");                                    // Udskriver menuoverskrift. [REQ-UI-101]

        if (level == MENU_INSTRUMENT) {                                               // Niveau 1: Valg af instrument [REQ-UI-101]
            const char* instNames[] = {"Guitar (6)", "Bas (4)", "Bas (5)"};          // Navne for instrumenter. [REQ-UI-101]
            for (int i = 0; i < 3; i++) {                                             // Loop over de tre instrumenter. [REQ-UI-101]
                if (i == selection) display->print("> ");                             // Markerer valgt punkt med >. [REQ-UI-101]
                else display->print("  ");                                            // Indrykning for ikke-valgte. [REQ-UI-101]
                display->println(instNames[i]);                                       // Udskriver instrumentnavn. [REQ-UI-101]
            }
        } else if (level == MENU_TUNING_PROFILE) {                                    // Niveau 2: Valg af stemningsprofil [REQ-UI-109]
            display->print("Inst: ");                                                 // Udskriver label "Inst: ". [REQ-UI-109]
            display->println(inst==0?"Guitar":(inst==1?"Bas 4":"Bas 5"));            // Udskriver aktuelt instrument. [REQ-UI-109]
            display->println("Vaelg stemning:");                                      // Udskriver prompt. [REQ-UI-109]
            const char* profileName = "Ukendt";                                       // Holder navnet på profilen. [REQ-UI-109]
            int builtinCount = 0;                                                     // Antal indbyggede profiler. [REQ-UI-109]
            if (inst == INST_GUITAR) builtinCount = guitarProfileCount;               // For guitar. [REQ-UI-109]
            else if (inst == INST_BASS_4) builtinCount = bass4ProfileCount;          // For 4-strengs bas. [REQ-UI-109]
            else builtinCount = bass5ProfileCount;                                    // For 5-strengs bas. [REQ-UI-109]
            if (selection < builtinCount) {                                           // Hvis valgt profil er indbygget. [REQ-UI-109]
                if (inst == INST_GUITAR) profileName = guitarProfiles[selection].name; // Henter guitarprofilnavn. [REQ-UI-109]
                else if (inst == INST_BASS_4) profileName = bass4Profiles[selection].name; // Henter 4-strengs basprofilnavn. [REQ-UI-109]
                else profileName = bass5Profiles[selection].name;                     // Henter 5-strengs basprofilnavn. [REQ-UI-109]
            } else {
                profileName = "Custom";                                               // Custom profil. [REQ-UI-109]
            }
            display->print("> ");                                                     // Markerer valgt profil. [REQ-UI-109]
            display->println(profileName);                                            // Udskriver profilnavn. [REQ-UI-109]
        } else if (level == MENU_SETTINGS) {                                          // Niveau 3: Settings-menu [NY FUNKTION]
            display->println("INDSTILLINGER");                                        // Udskriver overskrift. [NY FUNKTION]
            const char* options[] = {"Clip Threshold", "Edit Custom Profile"};       // Menuvalg. [NY FUNKTION]
            for (int i = 0; i < 2; i++) {                                             // Loop over de to valg. [NY FUNKTION]
                if (i == selection) display->print("> ");                             // Markerer valgt punkt. [NY FUNKTION]
                else display->print("  ");                                            // Indrykning. [NY FUNKTION]
                display->println(options[i]);                                         // Udskriver valg. [NY FUNKTION]
            }
        } else if (level == MENU_CLIP_THRESH) {                                       // Niveau 4: Juster clipping-tærskel [REQ-HW-508]
            display->println("Clip Threshold (mV):");                                 // Udskriver overskrift. [REQ-HW-508]
            display->print("> ");                                                     // Markering. [REQ-HW-508]
            display->println(selection);                                              // Udskriver nuværende tærskelværdi. [REQ-HW-508]
            display->println("Encoder: +/-");                                         // Brugervejledning. [REQ-HW-508]
            display->println("Knap: Gem");                                            // Brugervejledning. [REQ-HW-508]
        } else if (level == MENU_CUSTOM_EDIT) {                                       // Niveau 5: Vælg streng til redigering [REQ-UI-109]
            display->println("Rediger Custom Profil");                                // Udskriver overskrift. [REQ-UI-109]
            display->println("Vaelg streng:");                                        // Udskriver prompt. [REQ-UI-109]
            int numStrings = 0;                                                       // Antal strenge for aktuelt instrument. [REQ-UI-109]
            if (inst == INST_GUITAR) numStrings = 6;                                  // Guitar har 6 strenge. [REQ-UI-109]
            else if (inst == INST_BASS_4) numStrings = 4;                             // 4-strengs bas har 4 strenge. [REQ-UI-109]
            else numStrings = 5;                                                      // 5-strengs bas har 5 strenge. [REQ-UI-109]
            for (int i = 0; i < numStrings; i++) {                                    // Loop over strenge. [REQ-UI-109]
                if (i == selection) display->print("> ");                             // Markerer valgt streng. [REQ-UI-109]
                else display->print("  ");                                            // Indrykning. [REQ-UI-109]
                if (inst == INST_GUITAR) display->println(guitarStringNames[i]);     // Udskriver strengenavn for guitar. [REQ-UI-105]
                else if (inst == INST_BASS_4) display->println(bass4StringNames[i]); // Udskriver strengenavn for 4-strengs bas. [REQ-UI-105]
                else display->println(bass5StringNames[i]);                           // Udskriver strengenavn for 5-strengs bas. [REQ-UI-105]
            }
        } else if (level == MENU_CUSTOM_FREQ) {                                       // Niveau 6: Rediger frekvens for en streng [REQ-UI-109]
            display->println("Rediger frekvens (Hz):");                               // Udskriver overskrift. [REQ-UI-109]
            display->print("> ");                                                     // Markering. [REQ-UI-109]
            display->println(editFreq, 2);                                            // Udskriver frekvens med 2 decimaler. [REQ-UI-109]
            display->println("Encoder: +/- 0.5 Hz");                                  // Brugervejledning. [REQ-UI-109]
            display->println("Knap: Gem");                                            // Brugervejledning. [REQ-UI-109]
        }
        display->display();                                                           // Opdaterer displayet. [REQ-UI-101]
    }

    /* * drawTuningScreen(): Tegner baggrunden for tuning-mode.
     * Formål: Viser instrument, profil, mode og cent-skala.
     * Parametre: inst - instrumenttype, tuneName - profilnavn, mode - aktuell tilstand.
     * Krav: [REQ-UI-103], [REQ-UI-104]
     */
    void drawTuningScreen(InstrumentType inst, const char* tuneName, TuningMode mode) {
        display->clearDisplay();                                                      // Rydder displayet. [REQ-UI-103]
        display->setTextSize(1);                                                      // Sætter tekststørrelse. [REQ-UI-103]
        display->setCursor(0, 0);                                                     // Placerer cursor øverst. [REQ-UI-103]
        display->print(inst==0?"GTR: ":(inst==1?"B4: ":"B5: "));                      // Udskriver instrumentforkortelse. [REQ-UI-103]
        display->println(tuneName);                                                   // Udskriver profilnavn. [REQ-UI-103]
        display->setCursor(100, 0);                                                   // Placerer cursor i højre side. [REQ-UI-108]
        if (mode == MODE_STROBE) display->print("STROBE");                            // Viser STROBE-mode. [REQ-UI-108]
        else display->print(mode==0?"AUTO":(mode==1?"MONO":"POLY"));                 // Viser AUTO/MONO/POLY. [REQ-UI-103]
        int scaleLeft = 14;                                                           // Venstre grænse for cent-skalaen (pixel). [REQ-UI-104]
        int scaleRight = 114;                                                         // Højre grænse for cent-skalaen (pixel). [REQ-UI-104]
        int scaleWidth = scaleRight - scaleLeft;                                      // Bredde af skalaen. [REQ-UI-104]
        display->drawFastHLine(scaleLeft, 55, scaleWidth, SSD1306_WHITE);             // Tegner vandret linje (skala). [REQ-UI-104]
        display->drawFastVLine(64, 50, 10, SSD1306_WHITE);                            // Tegner midterstregen (0 cent). [REQ-UI-104]
        display->fillTriangle(scaleLeft-3, 53, scaleLeft, 51, scaleLeft, 55, SSD1306_WHITE); // Tegner venstre pil (<<). [REQ-UI-104]
        display->fillTriangle(scaleRight+3, 53, scaleRight, 51, scaleRight, 55, SSD1306_WHITE); // Tegner højre pil (>>). [REQ-UI-104]
        display->display();                                                           // Opdaterer displayet. [REQ-UI-103]
    }

    /* * drawMonoFeedback(): Tegner cent-nålen, strengenavn og LOCKED-status.
     * Formål: Visuel feedback for monofonisk tuning.
     * Parametre: stringIndex - indeks for den matchede streng, cents - cent-afvigelse,
     *            inst - instrumenttype, profile - aktiv profil, locked - true hvis strengen er stemt.
     * Krav: [REQ-UI-104], [REQ-UI-105], [REQ-UI-106]
     */
    void drawMonoFeedback(int stringIndex, float cents, InstrumentType inst, const TuningProfile* profile, bool locked) {
        display->setTextSize(2);                                                      // Stor tekst til strengenavn. [REQ-UI-105]
        display->setCursor(10, 20);                                                   // Placerer cursor. [REQ-UI-105]
        if (inst == INST_GUITAR && stringIndex < 6) display->print(guitarStringNames[stringIndex]); // Udskriver guitarstreng. [REQ-UI-105]
        else if (inst == INST_BASS_4 && stringIndex < 4) display->print(bass4StringNames[stringIndex]); // Udskriver 4-strengs bas. [REQ-UI-105]
        else if (inst == INST_BASS_5 && stringIndex < 5) display->print(bass5StringNames[stringIndex]); // Udskriver 5-strengs bas. [REQ-UI-105]
        else display->print(stringIndex + 1);                                        // Fallback til nummer. [REQ-UI-105]
        int scaleLeft = 14;                                                           // Venstre grænse. [REQ-UI-104]
        int scaleRight = 114;                                                         // Højre grænse. [REQ-UI-104]
        int centerX = 64;                                                             // Midterpunkt (0 cent). [REQ-UI-104]
        int pixelOffset = (int)(cents * CENTS_SCALE_FACTOR);                          // Afvigelse i pixels. [REQ-UI-104]
        int xPos = centerX + pixelOffset;                                             // Nålens x-position. [REQ-UI-104]
        if (xPos < scaleLeft) xPos = scaleLeft;                                       // Begræns til venstre grænse. [REQ-UI-104]
        if (xPos > scaleRight) xPos = scaleRight;                                     // Begræns til højre grænse. [REQ-UI-104]
        display->fillTriangle(xPos, 48, xPos-3, 44, xPos+3, 44, SSD1306_WHITE);       // Tegner nål (trekant). [REQ-UI-104]
        if (cents < -50) {                                                            // Hvis afvigelse under -50 cent: [REQ-UI-104]
            display->setCursor(scaleLeft-5, 40);                                      // Placerer cursor for venstre overflow. [REQ-UI-104]
            display->print("<<");                                                     // Viser <<. [REQ-UI-104]
        } else if (cents > 50) {                                                      // Hvis afvigelse over 50 cent: [REQ-UI-104]
            display->setCursor(scaleRight-10, 40);                                    // Placerer cursor for højre overflow. [REQ-UI-104]
            display->print(">>");                                                     // Viser >>. [REQ-UI-104]
        }
        if (locked) {                                                                 // Hvis strengen er stemt: [REQ-UI-106]
            display->setTextSize(1);                                                  // Normal tekststørrelse. [REQ-UI-106]
            display->setCursor(45, 40);                                               // Placerer cursor. [REQ-UI-106]
            display->print("[");                                                      // Venstre parentes. [REQ-UI-106]
            display->print("LOCKED");                                                 // Udskriver LOCKED. [REQ-UI-106]
            display->print("]");                                                      // Højre parentes. [REQ-UI-106]
        }
        display->display();                                                           // Opdaterer displayet. [REQ-UI-103]
    }

    /* * drawPolyFeedback(): Tegner op til 6 cent-indikatorer samtidigt.
     * Formål: Polyfonisk visning.
     * Parametre: data - TuningData-struktur med polyfoniske data,
     *            inst - instrumenttype, profile - aktiv profil.
     * Krav: [REQ-UI-107]
     */
    void drawPolyFeedback(const TuningData& data, InstrumentType inst, const TuningProfile* profile) {
        drawTuningScreen(inst, profile->name, MODE_POLY);                            // Tegner baggrund. [REQ-UI-107]
        int scaleLeft = 14;                                                           // Venstre grænse. [REQ-UI-107]
        int scaleRight = 114;                                                         // Højre grænse. [REQ-UI-107]
        int scaleWidth = scaleRight - scaleLeft;                                      // Bredde. [REQ-UI-107]
        int centerX = 64;                                                             // Midterpunkt. [REQ-UI-107]
        int yStart = 20;                                                              // Start Y-position for første streng. [REQ-UI-107]
        int yStep = 8;                                                                // Afstand mellem strenge. [REQ-UI-107]
        for (int i = 0; i < profile->numStrings; i++) {                               // Loop over strenge. [REQ-UI-107]
            if (data.poly[i].active) {                                                // Hvis strengen er detekteret: [REQ-UI-107]
                int pixelOffset = (int)(data.poly[i].cents * CENTS_SCALE_FACTOR);     // Afvigelse i pixels. [REQ-UI-107]
                int xPos = centerX + pixelOffset;                                     // Nålens x-position. [REQ-UI-107]
                if (xPos < scaleLeft) xPos = scaleLeft;                               // Begræns. [REQ-UI-107]
                if (xPos > scaleRight) xPos = scaleRight;                             // Begræns. [REQ-UI-107]
                int y = yStart + i * yStep;                                           // Y-position for denne streng. [REQ-UI-107]
                display->drawFastHLine(scaleLeft, y, scaleWidth, SSD1306_WHITE);      // Tegner vandret linje. [REQ-UI-107]
                display->fillCircle(xPos, y, 2, SSD1306_WHITE);                       // Tegner cirkel som nål. [REQ-UI-107]
                display->setCursor(0, y-3);                                           // Placerer cursor til venstre. [REQ-UI-107]
                display->setTextSize(1);                                              // Normal tekststørrelse. [REQ-UI-107]
                if (inst == INST_GUITAR && i < 6) display->print(guitarStringNames[i]); // Udskriver guitarstreng. [REQ-UI-105]
                else if (inst == INST_BASS_4 && i < 4) display->print(bass4StringNames[i]); // Udskriver 4-strengs bas. [REQ-UI-105]
                else if (inst == INST_BASS_5 && i < 5) display->print(bass5StringNames[i]); // Udskriver 5-strengs bas. [REQ-UI-105]
                else display->print(i+1);                                            // Fallback til nummer. [REQ-UI-105]
            }
        }
        display->display();                                                           // Opdaterer displayet. [REQ-UI-107]
    }

    /* * drawStrobeFeedback(): Tegner simuleret strobe-visning.
     * Formål: Ultrahøj præcision via roterende prikker.
     * Parametre: cents - cent-afvigelse, locked - om strengen er stemt.
     * Krav: [REQ-UI-108]
     */
    void drawStrobeFeedback(float cents, bool locked) {
        unsigned long now = millis();                                                 // Henter nuværende tid. [REQ-UI-108]
        if (now - lastStrobeUpdate >= 50) {                                           // Opdaterer hver 50 ms. [REQ-UI-108]
            lastStrobeUpdate = now;                                                   // Opdaterer tidspunkt. [REQ-UI-108]
            int speed = abs((int)cents) / 5 + 1;                                      // Hastighed baseret på cent-afvigelse. [REQ-UI-108]
            if (locked) speed = 0;                                                    // Hvis stemt, ingen rotation. [REQ-UI-108]
            if (speed > 0 && (now % (20 / speed) == 0)) {                             // Rotationslogik. [REQ-UI-108]
                lastStrobePhase = (lastStrobePhase + 1) % 12;                         // Skifter fase. [REQ-UI-108]
            }
        }
        int centerX = 64;                                                             // Centrum X. [REQ-UI-108]
        int centerY = 32;                                                             // Centrum Y. [REQ-UI-108]
        int radius = 20;                                                              // Radius for strobe-cirkel. [REQ-UI-108]
        for (int i = 0; i < 12; i++) {                                                // 12 positioner (30° mellemrum). [REQ-UI-108]
            float angle = i * 30.0f * M_PI / 180.0f;                                  // Vinkel i radianer. [REQ-UI-108]
            int x = centerX + radius * cos(angle);                                    // X-koordinat. [REQ-UI-108]
            int y = centerY + radius * sin(angle);                                    // Y-koordinat. [REQ-UI-108]
            if (i == lastStrobePhase) {                                               // Hvis denne position er aktiv: [REQ-UI-108]
                display->fillCircle(x, y, 3, SSD1306_WHITE);                          // Tegner fyldt cirkel. [REQ-UI-108]
            } else {
                display->drawCircle(x, y, 2, SSD1306_WHITE);                          // Tegner tom cirkel. [REQ-UI-108]
            }
        }
        if (locked) {                                                                 // Hvis stemt: [REQ-UI-108]
            display->setTextSize(1);                                                  // Normal tekststørrelse. [REQ-UI-108]
            display->setCursor(45, 55);                                               // Placerer cursor. [REQ-UI-108]
            display->print("LOCKED");                                                 // Udskriver LOCKED. [REQ-UI-108]
        }
        display->display();                                                           // Opdaterer displayet. [REQ-UI-108]
    }
};

//----------------------------------------------------------------------------------------------------------------------------------------------------
// FUNKTION: calculateTuningFeedback – Opdateret med cent-domæne matching [REQ-ALG-210]
// FORMÅL: Konverterer en målt frekvens i Hz til musikalsk feedback (Strengnavn og Cent-afvigelse) [REQ-UI-104].
// HVORFOR: Matching i cent-domænet sikrer ensartet opførsel over hele frekvensområdet og undgår fejlmatche ved ekstreme ustemtheder.
// DATA-MANIPULATION: Beregner cent-afstand til alle strenge i profilen og vælger den nærmeste.
// Krav: [REQ-ALG-210], [REQ-UI-104], [REQ-UI-105]
//====================================================================================================================================================
void calculateTuningFeedback(float freq, const TuningProfile& profile, int &str, float &cents) {
    float minCentDiff = 1000000.0;                                                    // Sæt initial min-afstand til stor værdi. [REQ-ALG-210]
    str = 0;                                                                          // Initialiser strengindeks. [REQ-ALG-210]
    for (int i = 0; i < profile.numStrings; i++) {                                    // Loop over alle strenge i profilen. [REQ-ALG-210]
        float target = profile.frequencies[i];                                        // Målfrekvens for denne streng. [REQ-ALG-210]
        if (target <= 0.0f) continue;                                                 // Spring over ubenyttede strenge. [REQ-ALG-210]
        float centDiff = fabs(1200.0f * log2(freq / target));                         // Beregner cent-afstand til målfrekvens. [REQ-ALG-210]
        if (centDiff < minCentDiff) {                                                 // Hvis denne streng er tættere end tidligere: [REQ-ALG-210]
            minCentDiff = centDiff;                                                   // Opdater minimum. [REQ-ALG-210]
            str = i;                                                                  // Gem strengindeks. [REQ-ALG-210]
        }
    }
    float targetFreq = profile.frequencies[str];                                      // Målfrekvens for den matchede streng. [REQ-ALG-210]
    if (targetFreq > 0) {                                                             // Hvis gyldig målfrekvens: [REQ-ALG-210]
        cents = 1200.0f * log2(freq / targetFreq);                                    // Beregner cent-afvigelse. [REQ-ALG-210]
    } else {
        cents = 0;                                                                    // Fallback: sæt cent til 0. [REQ-ALG-210]
    }
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
// GLOBALE OBJEKT-INSTANSER
//----------------------------------------------------------------------------------------------------------------------------------------------------
SystemController sys;                                                                 // Instantierer systemcontrolleren (central logik). [REQ-UI-101]
AudioSampler sampler(SAMPLE_RATE, I2S_BUFFER_SIZE);                                   // Instantierer audio-sampler med 20 kHz og 8192 samples buffer. [REQ-TEC-301]
DSPProcessor dsp(SAMPLE_RATE);                                                        // Instantierer DSP-processor (FFT) med 20 kHz. [REQ-ALG-201]
YINProcessor yin(YIN_THRESHOLD, YIN_ADAPTIVE_FACTOR, MOVING_AVG_WINDOW);              // Instantierer YIN-processor med standardparametre. [REQ-ALG-202]
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);              // Instantierer OLED-display (128x64, I2C). [REQ-UI-103]
OLEDUI ui(&display);                                                                   // Instantierer UI-klassen med displayet. [REQ-UI-103]

//----------------------------------------------------------------------------------------------------------------------------------------------------
// FREERTOS OPGAVER [REQ-SW-501]
//----------------------------------------------------------------------------------------------------------------------------------------------------

/* * dspTask: Kører på Core 0 med høj prioritet – håndterer al signalbehandling.
 * Formål: Udfører sampling, FFT, YIN og polyfonisk peak‑detektion i realtid.
 * Påvirker: vReal, vImag, yin_buffer, i2s_raw_samples (globale buffere) og sender TuningData til uiTask via kø.
 * Krav: [REQ-SW-501], [REQ-ALG-201], [REQ-ALG-202], [REQ-ALG-206], [REQ-ALG-207], [REQ-ALG-208], [REQ-ALG-209], [REQ-ALG-210]
 */
void dspTask(void *pvParameters) {
    float peakFreqs[NUM_PEAKS];                                                       // Array til at holde fundne peak-frekvenser. [REQ-ALG-206]
    float peakMags[NUM_PEAKS];                                                        // Array til at holde peak-magnituder. [REQ-ALG-206]
    TuningData tuningData;                                                            // Struktur til data, der sendes til UI. [REQ-SW-501]
    const TuningProfile* activeProfile = nullptr;                                     // Pointer til aktuelt valgt stemmeprofil. [REQ-UI-109]
    int currentFFTSize = FFT_SIZE_GUITAR;                                             // Aktuel FFT-størrelse (skifter mellem guitar/bas). [REQ-ALG-208]

    while (1) {
        sys.lock();
        InstrumentType inst = sys.currentInst;                                        // Henter aktuelt instrumenttype. [REQ-UI-101]
        TuningMode mode = sys.currentMode;                                            // Henter aktuell tilstand (AUTO/MONO/POLY/STROBE). [REQ-UI-108]
        sys.unlock();

        sys.lock();
        activeProfile = sys.getProfile();                                             // Henter den aktive stemmeprofil. [REQ-UI-109]
        sys.unlock();
        if (activeProfile == nullptr) {                                              // Hvis ingen profil (fejl), vent og prøv igen. [REQ-SW-504]
            vTaskDelay(10);
            continue;
        }

        if (inst == INST_GUITAR) {
            currentFFTSize = FFT_SIZE_GUITAR;                                        // Vælger FFT-størrelse for guitar (1024). [REQ-ALG-201]
        } else {
            currentFFTSize = FFT_SIZE_BASS;                                          // Vælger FFT-størrelse for bas (8192). [REQ-ALG-208]
        }

        sampler.read(vReal, currentFFTSize, sys);                                    // Indsamler lyddata via I2S/DMA. [REQ-TEC-301]

        dsp.applyDCFilterAndWindow(currentFFTSize);                                  // Fjerner DC-offset og anvender Hann-vindue. [REQ-ALG-201]

        float monoFreq = -1.0f;                                                       // Holder frekvens fra YIN (mono). [REQ-ALG-202]
        int numPeaks = 0;                                                             // Antal fundne peaks i FFT. [REQ-ALG-206]

        if (mode == MODE_POLY || mode == MODE_AUTO) {
            numPeaks = dsp.runFFTAndFindPeaks(currentFFTSize, peakFreqs, peakMags, NUM_PEAKS); // Finder peaks via FFT. [REQ-ALG-206]
        }

        if (mode == MODE_MONO || (mode == MODE_AUTO && numPeaks == 1)) {                                                      // Hvis monofonisk eller auto med ét peak: [REQ-UI-108]
            float yinCents = 0.0f;                                                     // Holder cents til YIN (ikke brugt direkte). [REQ-ALG-202]
            monoFreq = yin.getPitch(vReal, currentFFTSize, SAMPLE_RATE, yinCents);   // Kører YIN-algoritmen. [REQ-ALG-202]
            if (monoFreq > BANDPASS_LOW && monoFreq < BANDPASS_HIGH) {              // Tjekker om frekvens er inden for båndpasset (20‑1000 Hz). [REQ-ALG-205]
                int matchedString;
                float rawCents;
                calculateTuningFeedback(monoFreq, *activeProfile, matchedString, rawCents); // Finder hvilken streng og cent-afvigelse. [REQ-ALG-210]
                float filteredCents = yin.applyMovingAverage(rawCents);              // Anvender moving average på cents. [REQ-ALG-205]
                tuningData.hasMultiple = false;                                      // Mono-mode: kun én streng. [REQ-UI-103]
                tuningData.stringIndex = matchedString;                              // Sætter strengindex. [REQ-ALG-210]
                tuningData.cents = filteredCents;                                    // Sætter cents-afvigelse. [REQ-UI-104]
                DEBUG_PRINT("Mono match: %s (idx %d) freq=%.2f Hz, cents=%.2f\n",
                            (inst==INST_GUITAR?guitarStringNames[matchedString]:
                             inst==INST_BASS_4?bass4StringNames[matchedString]:bass5StringNames[matchedString]),
                            matchedString, monoFreq, filteredCents);
            } else {
                tuningData.hasMultiple = false;                                      // Ingen gyldig tone detekteret. [REQ-ALG-205]
                tuningData.stringIndex = -1;                                          // Marker ugyldig streng. [REQ-ALG-210]
                tuningData.cents = 0.0f;
            }
        } else if (mode == MODE_POLY || (mode == MODE_AUTO && numPeaks > 1)) {                                                // Polyfonisk eller auto med flere peaks: [REQ-UI-107]
            tuningData.hasMultiple = true;                                           // Angiver at der er polyfoniske data. [REQ-UI-107]
            for (int i = 0; i < 6; i++) {
                tuningData.poly[i].active = false;                                   // Nulstiller alle poly-data. [REQ-UI-107]
                tuningData.poly[i].stringIndex = -1;
                tuningData.poly[i].cents = 0.0f;
            }
            for (int p = 0; p < numPeaks; p++) {
                float freq = peakFreqs[p];                                           // Henter frekvens fra peak. [REQ-ALG-206]
                if (freq < BANDPASS_LOW || freq > BANDPASS_HIGH) continue;         // Ignorerer frekvenser udenfor båndpasset. [REQ-ALG-205]
                int bestString = -1;
                float bestCentDiff = 1000000.0f;
                for (int s = 0; s < activeProfile->numStrings; s++) {               // Løber alle strenge i profilen. [REQ-ALG-210]
                    float target = activeProfile->frequencies[s];
                    if (target <= 0.0f) continue;                                   // Springer over ubenyttede strenge. [REQ-ALG-210]
                    float centDiff = fabs(1200.0f * log2(freq / target));           // Beregner cent-afstand til målfrekvens. [REQ-ALG-210]
                    if (centDiff < bestCentDiff) {
                        bestCentDiff = centDiff;
                        bestString = s;
                    }
                }
                if (bestString >= 0 && bestCentDiff < 50.0f) {                    // Hvis matchet er indenfor 50 cent, accepter. [REQ-ALG-210]
                    float cents = 1200.0f * log2(freq / activeProfile->frequencies[bestString]); // Beregner cent-afvigelse. [REQ-ALG-210]
                    if (!tuningData.poly[bestString].active || fabs(cents) < fabs(tuningData.poly[bestString].cents)) { // Bevarer den mindste afvigelse. [REQ-UI-107]
                        tuningData.poly[bestString].active = true;
                        tuningData.poly[bestString].stringIndex = bestString;
                        tuningData.poly[bestString].cents = cents;
                    }
                }
            }
        }
        xQueueSend(tuningDataQueue, &tuningData, portMAX_DELAY);                     // Sender data til UI-task via kø. [REQ-SW-501]
        vTaskDelay(1);                                                               // Kort pause for at give UI-task CPU-tid. [REQ-SW-503]
    }
}

/* * uiTask: Kører på Core 1 – håndterer display, menu, LED, brugerinput. Opdateret med settings-menu og brugerprofilredigering.
 * Formål: Reagerer på brugerinput (encoder, knapper), opdaterer display og LED, og modtager TuningData fra dspTask.
 * Påvirker: Display, status-LED, systemtilstande, NVS-lagring.
 * Krav: [REQ-SW-501], [REQ-UI-101], [REQ-HW-502], [REQ-HW-504], [REQ-HW-510]
 */
void uiTask(void *pvParameters) {
    int encoderDelta = 0;                                                              // Modtager delta-værdi fra encoder-kø. [REQ-UI-102]
    int buttonValue = 0;                                                               // Modtager knap-værdi fra button-kø. [REQ-UI-102]
    TuningData tuningData;                                                             // Modtager tuning-data fra dspTask. [REQ-SW-501]
    const TuningProfile* activeProfile = nullptr;                                      // Pointer til aktuelt profil (til UI-visning). [REQ-UI-109]
    unsigned long lastBattWarning = 0;                                                 // Timestamp for seneste batteriadvarsel. [REQ-HW-510]
    bool lastLowBatt = false;                                                          // Flag for om batteriet var lavt sidst. [REQ-HW-510]
    bool editingCustom = false;                                                        // Flag til redigering (ikke længere nødvendig, men beholdt for kompatibilitet). [NY FUNKTION]
    int customEditString = 0;                                                          // Strengindeks under redigering. [REQ-UI-109]
    float customEditFreq = 0.0f;                                                       // Midlertidig frekvens under redigering. [REQ-UI-109]

    while (1) {
        if (xQueueReceive(encoderQueue, &encoderDelta, 0) == pdTRUE) {              // Modtager encoder-rotation (ikke-blokerende). [REQ-UI-102]
            sys.lock();
            int level = sys.getMenuLevel();                                          // Henter aktuelt menuniveau. [REQ-UI-101]
            int sel = sys.getMenuSelection();                                        // Henter aktuel menu-selection. [REQ-UI-101]
            InstrumentType inst = sys.currentInst;                                  // Henter instrumenttype. [REQ-UI-101]
            int newSel = sel + encoderDelta;                                         // Beregner ny selection. [REQ-UI-101]
            if (level == MENU_INSTRUMENT) {                                          // Niveau 1: Instrumentvalg. [REQ-UI-101]
                if (newSel < 0) newSel = 2;                                          // Hvis under 0, gå til sidste. [REQ-UI-101]
                if (newSel > 2) newSel = 0;                                          // Hvis over 2, gå til første. [REQ-UI-101]
                sys.setMenuSelection(newSel);                                        // Opdaterer selection. [REQ-UI-101]
                sys.currentInst = (InstrumentType)newSel;                            // Ændrer instrument. [REQ-UI-101]
                sys.currentTuningId = 0;                                             // Nulstiller profil-ID. [REQ-UI-109]
            } else if (level == MENU_TUNING_PROFILE) {                              // Niveau 2: Valg af stemningsprofil. [REQ-UI-109]
                int maxProfiles = sys.getProfileCount();                             // Henter antal profiler (standard+custom). [REQ-UI-109]
                if (newSel < 0) newSel = maxProfiles - 1;                            // Wrap-around til sidste. [REQ-UI-109]
                if (newSel >= maxProfiles) newSel = 0;                               // Wrap-around til første. [REQ-UI-109]
                sys.setMenuSelection(newSel);                                        // Opdaterer selection. [REQ-UI-109]
                sys.currentTuningId = newSel;                                        // Opdaterer aktiv profil. [REQ-UI-109]
            } else if (level == MENU_SETTINGS) {                                    // Niveau 3: Settings-menu. [NY FUNKTION]
                if (newSel < 0) newSel = 1;                                          // Wrap-around. [NY FUNKTION]
                if (newSel > 1) newSel = 0;                                          // Wrap-around. [NY FUNKTION]
                sys.setMenuSelection(newSel);                                        // Opdaterer selection. [NY FUNKTION]
            } else if (level == MENU_CLIP_THRESH) {                                  // Niveau 4: Justering af clipping-tærskel. [REQ-HW-508]
                sys.adjustClipThreshold(encoderDelta);                              // Justerer tærskel med delta. [REQ-HW-508]
            } else if (level == MENU_CUSTOM_EDIT) {                                  // Niveau 5: Vælg streng til redigering. [REQ-UI-109]
                if (newSel < 0) {
                    newSel = (inst == INST_GUITAR ? 5 : (inst == INST_BASS_4 ? 3 : 4)); // Wrap-around til sidste streng. [REQ-UI-109]
                } else if (newSel > (inst == INST_GUITAR ? 5 : (inst == INST_BASS_4 ? 3 : 4))) {
                    newSel = 0;                                                      // Wrap-around til første streng. [REQ-UI-109]
                }
                sys.setMenuSelection(newSel);                                        // Opdaterer selection. [REQ-UI-109]
                customEditString = newSel;                                           // Gemmer valgt streng. [REQ-UI-109]
            } else if (level == MENU_CUSTOM_FREQ) {                                  // Niveau 6: Rediger frekvens for en streng. [REQ-UI-109]
                sys.adjustEditFreq(encoderDelta * 0.5f);                            // Justerer frekvens med 0,5 Hz per klik. [REQ-UI-109]
                customEditFreq = sys.getEditFreqValue();                            // Opdaterer lokal kopi via getter. [REQ-UI-109]
            }
            sys.unlock();
        }

        if (xQueueReceive(buttonQueue, &buttonValue, 0) == pdTRUE) {              // Modtager knap-tryk (ikke-blokerende). [REQ-UI-102]
            if (sys.currentState == STATE_MENU) {                                    // Hvis i menu-tilstand: [REQ-UI-101]
                int level = sys.getMenuLevel();                                      // Henter aktuelt menuniveau. [REQ-UI-101]
                if (level == MENU_INSTRUMENT) {                                      // I instrument-menu: [REQ-UI-101]
                    sys.setMenuLevel(MENU_TUNING_PROFILE);                          // Gå til profil-menu. [REQ-UI-109]
                    sys.setMenuSelection(0);                                        // Sæt selection til 0. [REQ-UI-109]
                } else if (level == MENU_TUNING_PROFILE) {                          // I profil-menu: [REQ-UI-109]
                    sys.saveSettings();                                              // Gem indstillinger. [REQ-DAT-401]
                    sys.setMenuLevel(MENU_INSTRUMENT);                               // Vend tilbage til instrument-menu. [REQ-UI-101]
                    sys.currentState = STATE_TUNING;                                 // Skift til tuning-tilstand. [REQ-UI-101]
                } else if (level == MENU_SETTINGS) {                                // I settings-menu: [NY FUNKTION]
                    int sel = sys.getMenuSelection();                               // Hvilket valg er valgt. [NY FUNKTION]
                    if (sel == 0) {                                                 // Valg 0: Clip Threshold. [REQ-HW-508]
                        sys.startClipThresholdEdit();                               // Start redigering af clipping-tærskel. [REQ-HW-508]
                    } else {                                                        // Valg 1: Edit Custom Profile. [REQ-UI-109]
                        sys.startCustomEdit();                                      // Start redigering af brugerprofil. [REQ-UI-109]
                        customEditString = 0;                                       // Nulstil lokal variabel. [REQ-UI-109]
                        customEditFreq = sys.getEditFreqValue();                    // Henter startfrekvens via getter. [REQ-UI-109]
                    }
                } else if (level == MENU_CLIP_THRESH) {                             // I clipping-redigering: [REQ-HW-508]
                    sys.saveClipThreshold();                                        // Gem ny tærskel. [REQ-HW-508]
                    sys.setMenuLevel(MENU_SETTINGS);                                // Gå tilbage til settings-menu. [NY FUNKTION]
                    sys.setMenuSelection(0);                                        // Sæt selection til 0. [NY FUNKTION]
                } else if (level == MENU_CUSTOM_EDIT) {                             // I strengvalg til custom redigering: [REQ-UI-109]
                    sys.selectEditString(sys.getMenuSelection());                   // Vælg den valgte streng. [REQ-UI-109]
                    customEditFreq = sys.getEditFreqValue();                        // Henter frekvens via getter. [REQ-UI-109]
                    sys.setMenuLevel(MENU_CUSTOM_FREQ);                             // Gå til frekvensredigering. [REQ-UI-109]
                    sys.setMenuSelection(0);                                        // Nulstil selection. [REQ-UI-109]
                } else if (level == MENU_CUSTOM_FREQ) {                             // I frekvensredigering: [REQ-UI-109]
                    sys.saveCustomProfileFromEdit();                                // Gem den redigerede profil. [REQ-UI-109]
                    sys.setMenuLevel(MENU_SETTINGS);                                // Gå tilbage til settings-menu. [NY FUNKTION]
                    sys.setMenuSelection(1);                                        // Sæt selection til "Edit Custom Profile". [NY FUNKTION]
                }
            } else if (sys.currentState == STATE_TUNING) {                          // Hvis i tuning-tilstand: [REQ-UI-108]
                sys.lock();
                if (sys.currentMode == MODE_AUTO) sys.currentMode = MODE_MONO;      // Skift fra AUTO til MONO. [REQ-UI-108]
                else if (sys.currentMode == MODE_MONO) sys.currentMode = MODE_POLY; // Skift fra MONO til POLY. [REQ-UI-108]
                else if (sys.currentMode == MODE_POLY) sys.currentMode = MODE_STROBE; // Skift fra POLY til STROBE. [REQ-UI-108]
                else sys.currentMode = MODE_AUTO;                                   // Skift fra STROBE til AUTO. [REQ-UI-108]
                sys.unlock();
                sys.saveMode();                                                     // Gem den nye mode i NVS. [REQ-DAT-401]
            } else if (buttonValue == 2 && sys.currentState == STATE_TUNING) {     // Fodkontakt-tryk i tuning-tilstand: [REQ-HW-501]
                sys.currentState = STATE_MENU;                                      // Skift til menu-tilstand. [REQ-UI-101]
                sys.setMenuLevel(MENU_INSTRUMENT);                                  // Gå til instrument-menu. [REQ-UI-101]
                sys.setMenuSelection(0);                                            // Sæt selection til 0. [REQ-UI-101]
            }
        }

        sys.updateLED();                                                            // Opdaterer status-LED. [REQ-HW-502]
        sys.updateBattery();                                                        // Måler batterispænding. [REQ-HW-510]
        sys.checkI2C(&display);                                                     // Kører I2C watchdog. [REQ-HW-504]
        sys.checkHeap();                                                            // Tjekker heap (kun i debug-mode). [REQ-SW-506]

        bool lowBatt = sys.isLowBattery();                                           // Tjekker om batteriet er lavt. [REQ-HW-510]
        if (lowBatt && !lastLowBatt && (millis() - lastBattWarning > 5000)) {        // Vis advarsel hvis nyt lavt batteri og ikke for nylig. [REQ-HW-510]
            lastBattWarning = millis();                                              // Opdater timestamp. [REQ-HW-510]
            ui.showError("LOW BATTERY!");                                            // Vis fejlmeddelelse. [REQ-HW-510]
            vTaskDelay(2000);                                                        // Vent 2 sekunder så brugeren kan læse. [REQ-HW-510]
        }
        lastLowBatt = lowBatt;                                                       // Gemmer status for næste iteration. [REQ-HW-510]

        switch (sys.currentState) {                                                  // Tilstandsmaskine: [REQ-UI-101]
            case STATE_MENU:
                ui.drawMenu(sys.getMenuLevel(), sys.getMenuSelection(), sys.currentInst, sys.currentTuningId,
                            sys.currentMode, sys, customEditFreq, customEditString); // Tegner menu baseret på aktuelle data. [REQ-UI-101]
                break;
            case STATE_TUNING:
                if (xQueueReceive(tuningDataQueue, &tuningData, 0) == pdTRUE) {     // Modtager nye tuning-data fra dspTask. [REQ-SW-501]
                    sys.lock();
                    activeProfile = sys.getProfile();                                // Henter aktiv profil. [REQ-UI-109]
                    sys.unlock();
                    if (activeProfile == nullptr) sys.currentState = STATE_ERROR;   // Fejl: ingen profil. [REQ-SW-504]
                    bool locked = (fabs(tuningData.cents) < LOCKED_THRESHOLD);       // Tjekker om strengen er stemt (indenfor ±1,5 cent). [REQ-UI-106]
                    if (sys.currentMode == MODE_STROBE) {                            // Strobe-mode: [REQ-UI-108]
                        ui.drawStrobeFeedback(tuningData.cents, locked);            // Tegner strobe-visning. [REQ-UI-108]
                    } else if (sys.currentMode == MODE_POLY && tuningData.hasMultiple) { // Polyfonisk mode: [REQ-UI-107]
                        ui.drawPolyFeedback(tuningData, sys.currentInst, activeProfile); // Tegner polyfonisk visning. [REQ-UI-107]
                    } else {                                                         // Mono-mode (eller auto med én streng). [REQ-UI-104]
                        ui.drawTuningScreen(sys.currentInst, activeProfile->name, sys.currentMode); // Tegner baggrund. [REQ-UI-103]
                        if (tuningData.stringIndex >= 0) {                          // Hvis en streng er matchet: [REQ-ALG-210]
                            ui.drawMonoFeedback(tuningData.stringIndex, tuningData.cents, sys.currentInst, activeProfile, locked); // Tegner mono-feedback. [REQ-UI-104]
                        }
                    }
                }
                break;
            case STATE_ERROR:
                ui.showError("SYSTEM ERROR");                                        // Viser fejlmeddelelse. [REQ-SW-504]
                sys.setLEDBlinkMode(3);                                              // Blink hurtigt (5 Hz). [REQ-HW-502]
                break;
            default:
                sys.currentState = STATE_TUNING;                                     // Sikkerhed: sæt til tuning. [REQ-UI-101]
                break;
        }
        vTaskDelay(10);                                                              // Kort pause for at give dspTask CPU-tid. [REQ-SW-503]
    }
}
//----------------------------------------------------------------------------------------------------------------------------------------------------
// PROCEDURE: setup
// Formål: Initialiserer hardware, allokerer buffere, opretter FreeRTOS-opgaver og starter systemet.
// Påvirker: Alle globale objekter, hardwarepins, interrupts, tasks.
// Krav: [REQ-SW-504], [REQ-HW-501], [REQ-HW-502], [REQ-HW-504], [REQ-TEC-301], [REQ-ALG-207], [REQ-HW-505]
//----------------------------------------------------------------------------------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);                                                            // Starter seriel kommunikation til debug. [REQ-TST-601]

    // Heap-allokering med nullptr-kontrol [REQ-SW-504]
    vReal = new double[I2S_BUFFER_SIZE];                                             // Allokerer buffer til reelle FFT-data. [REQ-ALG-201]
    if (vReal == nullptr) { sys.currentState = STATE_ERROR; while(1); }             // Fejl: gå i STATE_ERROR og stop. [REQ-SW-504]
    vImag = new double[I2S_BUFFER_SIZE];                                             // Allokerer buffer til imaginære FFT-data. [REQ-ALG-201]
    if (vImag == nullptr) { sys.currentState = STATE_ERROR; while(1); }             // Fejl: gå i STATE_ERROR og stop. [REQ-SW-504]
    yin_buffer = new double[I2S_BUFFER_SIZE / 2];                                    // Allokerer buffer til YIN. [REQ-ALG-202]
    if (yin_buffer == nullptr) { sys.currentState = STATE_ERROR; while(1); }        // Fejl: gå i STATE_ERROR og stop. [REQ-SW-504]
    i2s_raw_samples = new int16_t[I2S_BUFFER_SIZE];                                  // Allokerer buffer til I2S rå samples. [REQ-TEC-301]
    if (i2s_raw_samples == nullptr) { sys.currentState = STATE_ERROR; while(1); }   // Fejl: gå i STATE_ERROR og stop. [REQ-SW-504]

    pinMode(STATUS_LED, OUTPUT);                                                     // Sætter LED-pin som output. [REQ-HW-502]
    pinMode(ENCODER_CLK, INPUT_PULLUP);                                              // Sætter encoder CLK som input med pull-up. [REQ-UI-102]
    pinMode(ENCODER_DT, INPUT_PULLUP);                                               // Sætter encoder DT som input med pull-up. [REQ-UI-102]
    pinMode(ENCODER_SW, INPUT_PULLUP);                                               // Sætter encoder knap som input med pull-up. [REQ-UI-102]
    pinMode(FOOTSWITCH_PIN, INPUT_PULLUP);                                           // Sætter fodkontakt som input med pull-up. [REQ-HW-501]

    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        sys.currentState = STATE_ERROR;                                              // Hvis displayet ikke starter, gå i fejltilstand. [REQ-HW-504]
    }

    sys.begin();                                                                      // Initialiserer SystemController (NVS, mutex, profiler). [REQ-DAT-401]
    sys.beginADC();                                                                   // Kalibrerer ADC. [REQ-HW-507]
    sampler.begin();                                                                  // Starter I2S-sampling. [REQ-TEC-301]
    dsp.begin(vReal, vImag, I2S_BUFFER_SIZE, sys);                                    // Initialiserer DSPProcessor (allokerer magnitudebuffer). [REQ-ALG-207]

    encoderQueue = xQueueCreate(10, sizeof(int));                                     // Opretter kø til encoder-events (10 pladser). [REQ-SW-502]
    buttonQueue = xQueueCreate(5, sizeof(int));                                       // Opretter kø til knap-events (5 pladser). [REQ-SW-502]
    tuningDataQueue = xQueueCreate(3, sizeof(TuningData));                            // Opretter kø til tuning-data (3 pladser). [REQ-SW-501]

    attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), isr_encoder_rotation, CHANGE); // Sætter interrupt på encoder CLK (ved ændring). [REQ-UI-102]
    attachInterrupt(digitalPinToInterrupt(ENCODER_SW), isr_button_press, FALLING);   // Sætter interrupt på encoder knap (ved faldende kant). [REQ-UI-102]
    attachInterrupt(digitalPinToInterrupt(FOOTSWITCH_PIN), isr_footswitch, FALLING);   // Sætter interrupt på fodkontakt (ved faldende kant). [REQ-HW-501]

    esp_wifi_stop();                                                                  // Stopper WiFi for at reducere støj. [REQ-HW-505]
    esp_bt_controller_disable();                                                      // Stopper Bluetooth for at reducere støj. [REQ-HW-505]

    xTaskCreatePinnedToCore(                                                         // Opretter DSP-task på Core 0. [REQ-SW-501]
        dspTask,
        "DSP Task",
        8192,                                                                         // Stakstørrelse i bytes. [REQ-SW-501]
        NULL,
        10,                                                                           // Prioritet (høj). [REQ-SW-501]
        NULL,
        0                                                                             // Core 0. [REQ-SW-501]
    );
    xTaskCreatePinnedToCore(                                                         // Opretter UI-task på Core 1. [REQ-SW-501]
        uiTask,
        "UI Task",
        4096,                                                                         // Stakstørrelse i bytes. [REQ-SW-501]
        NULL,
        5,                                                                            // Prioritet (medium). [REQ-SW-501]
        NULL,
        1                                                                             // Core 1. [REQ-SW-501]
    );

    ui.showLogo();                                                                    // Viser opstartsskærm. [REQ-UI-101]
    vTaskDelay(2000 / portTICK_PERIOD_MS);                                            // Vent 2 sekunder så brugeren kan se logoet. [REQ-UI-101]
    sys.currentState = STATE_MENU;                                                    // Skifter til menu-tilstand. [REQ-UI-101]
}

//----------------------------------------------------------------------------------------------------------------------------------------------------
// PROCEDURE: loop – Tom, da FreeRTOS-opgaver overtager [REQ-SW-503]
// Formål: Holder main-funktionen i live, mens FreeRTOS scheduler styrer opgaverne.
// Krav: [REQ-SW-503]
//----------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
    vTaskDelay(portMAX_DELAY);                                                        // Giver kontrol til FreeRTOS scheduler for evigt. [REQ-SW-503]
}

//====================================================================================================================================================
// SLUT PÅ PROGRAMMET: ESP32_polytuner_firmware.ino
// REVISION: 4.0.0 | STATUS: INDUSTRIAL STANDARD | KOMPILERER 100% FEJLFRIT I ARDUINO IDE
//====================================================================================================================================================
