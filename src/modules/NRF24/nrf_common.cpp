#include "nrf_common.h"
#include "../../core/mykeyboard.h"
#include <driver/gpio.h>      // gpio_set_direction — raw pad I/O enable, bypasses periman
#include <esp32-hal-matrix.h> // pinMatrixOutAttach for the raw GPIO-matrix re-mux
#include <soc/gpio_sig_map.h> // FSPI*/SPI3_* peripheral signal indices (ESP32-S3)

RF24 NRFradio(bruceConfigPins.NRF24_bus.io0, bruceConfigPins.NRF24_bus.cs);
HardwareSerial NRFSerial = HardwareSerial(2); // Uses UART2 for External NRF's
SPIClass *NRFSPI;

// Set true in nrf_start only when the NRF rides its own SPI3/HSPI peripheral on
// pins physically shared with the TFT (the VariOne S3 case). Gates the re-mux
// helpers so they stay no-ops on boards with dedicated NRF pins.
static bool nrf_needsBusRemux = false;

// VariOne S3: SPI3 is started on these two THROWAWAY GPIOs (both unused on this board)
// instead of the real SCK/MOSI (12/11). That keeps the ESP32 peripheral manager from
// ever claiming the shared pins 11/12/13 — which would tear down the TFT's SPI2 bus.
// The real pins are wired to SPI3 purely through the GPIO matrix (nrf_claimBus /
// pinMatrixInAttach below); one output signal can legally drive several pads, so the
// dummy pins just carry an extra harmless copy of SCK/MOSI to nothing.
#define NRF_SPI3_DUMMY_SCK 1
#define NRF_SPI3_DUMMY_MOSI 21
static uint8_t nrfBusDbgClaimCount = 0;
static uint8_t nrfBusDbgReleaseCount = 0;

void nrf_info() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.setTextColor(TFT_RED, bruceConfig.bgColor);
    tft.drawCentreString("_Disclaimer_", tftWidth / 2, 10, 1);
    tft.setTextColor(TFT_WHITE, bruceConfig.bgColor);
    tft.setTextSize(FP);
    tft.setCursor(15, 33);
    padprintln("These functions were made to be used in a controlled environment for STUDY only.");
    padprintln("");
    padprintln("DO NOT use these functions to harm people or companies, you can go to jail!");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    padprintln("");
    padprintln(
        "This device is VERY sensible to noise, so long wires or passing near VCC line can make "
        "things go wrong."
    );
    delay(1000);
    while (!check(AnyKeyPress));
}

bool nrf_start(NRF24_MODE mode) {
    bool result = false;
    if (mode == NRF_MODE_DISABLED) return false;

    if (CHECK_NRF_UART(mode)) {
        if (USBserial.getSerialOutput() == &Serial1) {
            displayError("(E) UART already in use", true);
            return false;
        }
        NRFSerial.begin(115200, SERIAL_8N1, bruceConfigPins.uart_bus.rx, bruceConfigPins.uart_bus.tx);
        Serial.println("NRF24 on Serial Started");
        result = true;
    };

    if (!CHECK_NRF_SPI(mode)) return result;
    pinMode(bruceConfigPins.NRF24_bus.cs, OUTPUT);
    digitalWrite(bruceConfigPins.NRF24_bus.cs, HIGH);
    pinMode(bruceConfigPins.NRF24_bus.io0, OUTPUT);
    digitalWrite(bruceConfigPins.NRF24_bus.io0, LOW);

    // Deselect the OTHER chips that share the SPI bus pins (SCK/MOSI/MISO 12/11/13):
    // CC1101 (CS 10) and the SD card (CS 16). On the very first NRF use after boot
    // their CS lines are still in the power-on state (not yet driven HIGH by their
    // own driver), so they keep driving MISO and the NRF reads nothing -> begin=0,
    // isChipConnected=0, "Fail Starting radio". Running RF/RFID first is what used
    // to deselect them as a side effect (hence "NRF worked after using RF/RFID").
    // These CS pins are separate from the shared bus pins, so driving them is safe.
    if (bruceConfigPins.CC1101_bus.cs != GPIO_NUM_NC) {
        pinMode(bruceConfigPins.CC1101_bus.cs, OUTPUT);
        digitalWrite(bruceConfigPins.CC1101_bus.cs, HIGH);
    }
    if (bruceConfigPins.SDCARD_bus.cs != GPIO_NUM_NC) {
        pinMode(bruceConfigPins.SDCARD_bus.cs, OUTPUT);
        digitalWrite(bruceConfigPins.SDCARD_bus.cs, HIGH);
    }

    if (bruceConfigPins.NRF24_bus.mosi == (gpio_num_t)TFT_MOSI &&
        bruceConfigPins.NRF24_bus.mosi != GPIO_NUM_NC &&
        // VariOne S3: TFT (SPI2/LovyanGFX) and CC1101+NRF (HSPI) all sit on GPIO
        // 11/12, but the NRF must ride the CC1101 HSPI instance (CC_NRF_SPI), NOT
        // the TFT instance — the TFT bus has MISO=-1, so RF24::begin() can't read
        // and returns 0. Skip the TFT branch when the NRF shares the CC1101 MOSI.
        bruceConfigPins.NRF24_bus.mosi != bruceConfigPins.CC1101_bus.mosi) {
        // (T_EMBED), CORE2 and others
#if TFT_MOSI > 0 // condition for Headless and 8bit displays (no SPI bus)
        NRFSPI = &tft.getSPIinstance();
#else
        NRFSPI = &SPI;
#endif

    } else if (bruceConfigPins.NRF24_bus.mosi == bruceConfigPins.SDCARD_bus.mosi) {
        // CC1101 shares SPI with SDCard (Cardputer and CYDs)

        NRFSPI = &sdcardSPI;
    } else if (bruceConfigPins.NRF24_bus.mosi == bruceConfigPins.CC1101_bus.mosi &&
               bruceConfigPins.NRF24_bus.mosi != bruceConfigPins.SDCARD_bus.mosi) {
        // VariOne S3: CC1101 rides the LovyanGFX TFT SPI driver (CC1101_bus.mosi ==
        // TFT_MOSI -> initRfModule() uses tft.getSPIinstance(), see rf_utils.cpp:222),
        // which lives on SPI2/FSPI = the TFT's host. The NRF cannot share that: the
        // TFT instance has MISO=-1 (RF24 can't read), and routing a second Arduino
        // SPIClass (CC_NRF_SPI is also FSPI/SPI2) onto the same SPI2 peripheral makes
        // it fight LovyanGFX -> every NRF transfer spins forever. Give the NRF its own
        // isolated SPI3/HSPI peripheral on the same physical pins (11/12/13, real
        // MISO=13). Different host = no register war with the TFT; the GPIO matrix
        // re-muxes 11/12 on each begin/transaction.
        static SPIClass nrfHSPI(HSPI); // SPI3_HOST, free on this board
        NRFSPI = &nrfHSPI;
        nrf_needsBusRemux = true; // shares pins 11/12 with the TFT -> re-mux on handoff
    } else {
        NRFSPI = &SPI;
    }
    NRFSPI->end();
    delay(10);
    if (nrf_needsBusRemux) {
        // Start SPI3 on throwaway pins so periman never touches the TFT-shared 11/12/13.
        NRFSPI->begin((int8_t)NRF_SPI3_DUMMY_SCK, (int8_t)-1, (int8_t)NRF_SPI3_DUMMY_MOSI);
        // Enable the real pads' I/O drivers via raw IDF (NOT pinMode — that runs periman
        // and would tear down the bus owning the shared pin). SCK/MOSI drive out, MISO in.
        gpio_set_direction((gpio_num_t)bruceConfigPins.NRF24_bus.sck, GPIO_MODE_OUTPUT);
        gpio_set_direction((gpio_num_t)bruceConfigPins.NRF24_bus.mosi, GPIO_MODE_OUTPUT);
        gpio_set_direction((gpio_num_t)bruceConfigPins.NRF24_bus.miso, GPIO_MODE_INPUT);
        // Raw-route the real NRF MISO (13) to SPI3's Q input (GPIO matrix only — pin 13
        // is shared with the CC1101, so we must NOT periman-attach it).
        pinMatrixInAttach((uint8_t)bruceConfigPins.NRF24_bus.miso, SPI3_Q_OUT_IDX, false);
        // Raw-route the real SCK/MOSI (12/11) onto SPI3 before RF24 talks to the chip.
        nrf_claimBus();
    } else {
        NRFSPI->begin(
            (int8_t)bruceConfigPins.NRF24_bus.sck,
            (int8_t)bruceConfigPins.NRF24_bus.miso,
            (int8_t)bruceConfigPins.NRF24_bus.mosi
        );
    }
    delay(10); // power/settle before RF24::begin (RF24 also delays 5ms internally)

    // Retry begin(): the FIRST RF24::begin() after boot frequently returns 0 on
    // this shared-bus wiring (the chip needs the SPI bus exercised before it
    // answers — which is why running RF/RFID/Jammer first used to "fix" it). Each
    // begin() does real register reads/writes, so retrying exercises the bus and
    // succeeds within a few tries. Gate on begin() only; isChipConnected() reads
    // unreliably right after begin (observed begin=1 while isChipConnected=0 yet
    // the radio worked).
    bool beginOk = false;
    for (int attempt = 0; attempt < 20 && !beginOk; attempt++) {
        beginOk = NRFradio.begin(
            NRFSPI, rf24_gpio_pin_t(bruceConfigPins.NRF24_bus.io0),
            rf24_gpio_pin_t(bruceConfigPins.NRF24_bus.cs)
        );
        if (!beginOk) {
            delay(15);
            if (nrf_needsBusRemux) {
                // Cold boot (NRF launched FIRST, before RF/RFID) sometimes needs the SPI3
                // routing re-seated harder than a single SCK/MOSI claim. Every few tries,
                // also re-attach the MISO matrix input — replaces the "run RF first" ritual.
                if ((attempt & 3) == 3) {
                    pinMatrixInAttach(
                        (uint8_t)bruceConfigPins.NRF24_bus.miso, SPI3_Q_OUT_IDX, false
                    );
                }
                nrf_claimBus(); // re-assert the SPI3 SCK/MOSI matrix
            }
        }
    }
    Serial.printf(
        "[NRF DBG] begin=%d isChipConnected=%d\n", beginOk ? 1 : 0, NRFradio.isChipConnected() ? 1 : 0
    );
    if (beginOk) {
        result = true;
    } else {
        // claim left the shared pins on SPI3; give them back so the caller's
        // displayError() can draw to the TFT.
        nrf_releaseBusToDisplay();
        return false;
    }
    return result;
}

// ── Shared-pin re-mux (VariOne S3) ──────────────────────────────────────────
// The NRF (SPI3/HSPI) and TFT (SPI2/FSPI) share the physical SCK/MOSI pins (12/11).
// We switch ownership with the *raw* GPIO matrix only (pinMatrixOutAttach) — the same
// routing that spiAttach* does internally, but WITHOUT perimanSetPinBus, which would
// destructively deinit whichever bus currently owns the pin. MISO (13) is the NRF's
// exclusively (TFT MISO = -1), attached once in nrf_start, so it is never re-muxed.
//
// ESP32-S3 peripheral signal indices (soc/gpio_sig_map.h): FSPI = SPI2 (display),
// SPI3 = the NRF's HSPI bus.

// Point the shared SCK/MOSI pins at SPI3 so the NRF can drive them. Call before NRF
// SPI activity when the TFT may have taken the pins.
void nrf_claimBus() {
    if (!nrf_needsBusRemux) return;
    // GPIO matrix ONLY — no pinMode()/spiAttach(): both call perimanClearPinBus(), which
    // tears the SPI bus down. The pads' output-enable was already set by nrf_start's
    // begin() (and by lgfx at boot) and persists; we just pick which peripheral drives.
    pinMatrixOutAttach((uint8_t)bruceConfigPins.NRF24_bus.sck, SPI3_CLK_OUT_IDX, false, false);
    pinMatrixOutAttach((uint8_t)bruceConfigPins.NRF24_bus.mosi, SPI3_D_IN_IDX, false, false);
    if (nrfBusDbgClaimCount < 4) {
        Serial.printf("[NRF BUS] claim #%u (SPI3)\n", nrfBusDbgClaimCount++);
        Serial.flush();
    }
}

// Point the shared SCK/MOSI pins back at FSPI/SPI2 so the TFT (LovyanGFX) can draw.
// Call before any TFT draw after NRF SPI activity.
void nrf_releaseBusToDisplay() {
    if (!nrf_needsBusRemux) return;
    // GPIO matrix ONLY (see nrf_claimBus). lgfx reprograms the SPI2 registers on its next
    // beginTransaction, so pointing the pads back at FSPI is enough to revive the display.
    pinMatrixOutAttach((uint8_t)bruceConfigPins.NRF24_bus.sck, FSPICLK_OUT_IDX, false, false);
    pinMatrixOutAttach((uint8_t)bruceConfigPins.NRF24_bus.mosi, FSPID_IN_IDX, false, false);
    if (nrfBusDbgReleaseCount < 4) {
        Serial.printf("[NRF BUS] release #%u (SPI2)\n", nrfBusDbgReleaseCount++);
        Serial.flush();
    }
}

NRF24_MODE nrf_setMode() {
    NRF24_MODE mode = NRF_MODE_DISABLED;
    // VariOne has no external UART nRF jammer module — only the on-board nRF24 on SPI.
    // Drop the dead "SPI UART" / "SPI BOTH" options; user picks SPI or backs out.
    options = {
        {"SPI Mode",  [&]() { mode = NRF_MODE_SPI; } },
        {"Main Menu", [=]() { returnToMenu = true; } }
    };
    loopOptions(options);
    return mode;
}
