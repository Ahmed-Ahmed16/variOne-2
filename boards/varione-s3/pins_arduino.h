#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t SDA = 8;
static const uint8_t SCL = 9;

static const uint8_t SS = 10;
static const uint8_t MOSI = 11;
static const uint8_t MISO = 13;
static const uint8_t SCK = 12;

#define HAS_BTN 1
#define HAS_5_BUTTONS
#define L_BTN 39
#define UP_BTN 47
#define R_BTN 41
#define DW_BTN 42
#define SEL_BTN 45
#define OK_BTN 45
#define BACK_BTN 46
#define BTN_ALIAS "OK"
#define BTN_ACT LOW

#define TXLED -1
#define LED_ON HIGH
#define LED_OFF LOW

#define GROVE_SDA 8
#define GROVE_SCL 9
#define BAD_TX TX
#define BAD_RX RX

#define SDCARD_CS 16
#define SDCARD_SCK 6
#define SDCARD_MISO 5
#define SDCARD_MOSI 7

#define CC1101_GDO0_PIN 4
#define CC1101_SS_PIN 10
#define CC1101_MOSI_PIN 11
#define CC1101_SCK_PIN 12
#define CC1101_MISO_PIN 13

#define NRF24_CE_PIN 17
#define NRF24_SS_PIN 15
#define NRF24_MOSI_PIN 11
#define NRF24_SCK_PIN 12
#define NRF24_MISO_PIN 13

#define SPI_SCK_PIN 12
#define SPI_MISO_PIN 13
#define SPI_MOSI_PIN 11
#define SPI_SS_PIN 10

#define FP 1
#define FM 1
#define FG 2

#define HAS_SCREEN 1
#define ROTATION 1
#define MINBRIGHT 160

#endif
