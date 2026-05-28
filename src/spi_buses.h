#pragma once
// Two independent SPI buses for the NM-EPD-420 board.
//
//   SPI            (default, FSPI/SPI2): EPD only
//                   pins SCK=2, MOSI=1, MISO=10, CS=3 (init in Display::init)
//
//   spiPeripheral  (HSPI/SPI3):          SD card + LoRa
//                   pins SCK=9, MOSI=10, MISO=11, CS=SD_CS=7 / LoRa_NSS=8
//
// Using two physical SPI controllers means EPD and SD/LoRa never need to
// dance with SPI.end()/begin(). Each test just SPI_PER.begin() once and the
// other bus is unaffected.
//
// The EPD bus's MOSI (GPIO1) and the SD bus's MOSI (GPIO10) happen to clash
// only in pin numbering with the EPD MISO field (GPIO10, NC on board).
// In practice EPD never reads MISO, and SD bus owns GPIO10 as its MOSI/CMD,
// so there is no electrical conflict as long as both controllers are
// independent (which they are now).

#include <SPI.h>
#include "config.h"

extern SPIClass spiPeripheral;

// Initialise the peripheral bus once at boot. Idempotent (.begin twice is
// harmless on ESP32-S3 if pins are unchanged).
inline void initPeripheralSpi() {
    spiPeripheral.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI);
    // CS lines stay GPIO-controlled by their owners, not given to the SPI
    // driver, so we don't pass a CS pin to begin(). Both SD and LoRa drive
    // their own CS via digitalWrite.
}
