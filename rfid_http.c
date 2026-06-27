#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#define WIFI_SSID      "chanti"
#define WIFI_PASS      "ravikumar"

#define SERVER_URL     "http://10.10.29.237/smart_hospital/save_scan.php?uid="

#define PIN_NUM_MISO   19
#define PIN_NUM_MOSI   23
#define PIN_NUM_CLK    18
#define PIN_NUM_CS     5
#define PIN_NUM_RST    22

static const char *TAG = "RFID_HTTP";

spi_device_handle_t spi;
char last_uid[50] = "";

/* MFRC522 Registers */
#define CommandReg        0x01
#define ComIEnReg         0x02
#define ComIrqReg         0x04
#define ErrorReg          0x06
#define FIFODataReg       0x09
#define FIFOLevelReg      0x0A
#define ControlReg        0x0C
#define BitFramingReg     0x0D
#define ModeReg           0x11
#define TxControlReg      0x14
#define TxASKReg          0x15
#define TModeReg          0x2A
#define TPrescalerReg     0x2B
#define TReloadRegH       0x2C
#define TReloadRegL       0x2D

#define PCD_IDLE          0x00
#define PCD_TRANSCEIVE    0x0C
#define PCD_RESETPHASE    0x0F

#define PICC_REQIDL       0x26
#define PICC_ANTICOLL     0x93

void write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    tx[0] = (reg << 1) & 0x7E;
    tx[1] = value;

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx
    };

    spi_device_transmit(spi, &t);
}

uint8_t read_reg(uint8_t reg)
{
    uint8_t tx[2];
    uint8_t rx[2];

    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;

    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = tx,
        .rx_buffer = rx
    };

    spi_device_transmit(spi, &t);
    return rx[1];
}

void set_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = read_reg(reg);
    write_reg(reg, tmp | mask);
}

void clear_bit_mask(uint8_t reg, uint8_t mask)
{
    uint8_t tmp = read_reg(reg);
    write_reg(reg, tmp & (~mask));
}

void antenna_on()
{
    uint8_t temp = read_reg(TxControlReg);
    if (!(temp & 0x03)) {
        set_bit_mask(TxControlReg, 0x03);
    }
}

void mfrc522_reset()
{
    write_reg(CommandReg, PCD_RESETPHASE);
}

void mfrc522_init()
{
    mfrc522_reset();

    write_reg(TModeReg, 0x8D);
    write_reg(TPrescalerReg, 0x3E);
    write_reg(TReloadRegL, 30);
    write_reg(TReloadRegH, 0);

    write_reg(TxASKReg, 0x40);
    write_reg(ModeReg, 0x3D);

    antenna_on();
}

uint8_t mfrc522_to_card(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                        uint8_t *backData, uint16_t *backLen)
{
    uint8_t status = 1;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t n;
    uint16_t i;

    if (command == PCD_TRANSCEIVE) {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    write_reg(ComIEnReg, irqEn | 0x80);
    clear_bit_mask(ComIrqReg, 0x80);
    set_bit_mask(FIFOLevelReg, 0x80);

    write_reg(CommandReg, PCD_IDLE);

    for (i = 0; i < sendLen; i++) {
        write_reg(FIFODataReg, sendData[i]);
    }

    write_reg(CommandReg, command);

    if (command == PCD_TRANSCEIVE) {
        set_bit_mask(BitFramingReg, 0x80);
    }

    i = 2000;

    do {
        n = read_reg(ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

    clear_bit_mask(BitFramingReg, 0x80);

    if (i != 0) {
        if (!(read_reg(ErrorReg) & 0x1B)) {
            status = 0;

            if (n & irqEn & 0x01) {
                status = 1;
            }

            if (command == PCD_TRANSCEIVE) {
                n = read_reg(FIFOLevelReg);
                uint8_t lastBits = read_reg(ControlReg) & 0x07;

                if (lastBits) {
                    *backLen = (n - 1) * 8 + lastBits;
                } else {
                    *backLen = n * 8;
                }

                if (n == 0) n = 1;
                if (n > 16) n = 16;

                for (i = 0; i < n; i++) {
                    backData[i] = read_reg(FIFODataReg);
                }
            }
        } else {
            status = 1;
        }
    }

    return status;
}

uint8_t request_card()
{
    uint8_t status;
    uint16_t backBits;
    uint8_t tagType[2];

    write_reg(BitFramingReg, 0x07);

    tagType[0] = PICC_REQIDL;
    status = mfrc522_to_card(PCD_TRANSCEIVE, tagType, 1, tagType, &backBits);

    if ((status != 0) || (backBits != 0x10)) {
        status = 1;
    }

    return status;
}

uint8_t anticoll(uint8_t *serNum)
{
    uint8_t status;
    uint8_t i;
    uint8_t serNumCheck = 0;
    uint16_t unLen;

    write_reg(BitFramingReg, 0x00);

    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20;

    status = mfrc522_to_card(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

    if (status == 0) {
        for (i = 0; i < 4; i++) {
            serNumCheck ^= serNum[i];
        }

        if (serNumCheck != serNum[4]) {
            status = 1;
        }
    }

    return status;
}

void wifi_init()
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Connecting WiFi...");
    esp_wifi_connect();

    vTaskDelay(pdMS_TO_TICKS(5000));
}

void send_uid_to_server(char *uid)
{
    char url[200];

    snprintf(url, sizeof(url), "%s%s", SERVER_URL, uid);

    for (int i = 0; url[i] != '\0'; i++) {
        if (url[i] == ' ') {
            url[i] = '+';
        }
    }

    ESP_LOGI(TAG, "URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Code: %d", status);
    } else {
        ESP_LOGE(TAG, "HTTP GET Failed");
    }

    esp_http_client_cleanup(client);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP-IDF RFID HTTP Project Started");

    wifi_init();

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 7,
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_RST, 1);

    mfrc522_init();

    ESP_LOGI(TAG, "Scan RFID Card...");

    while (1) {
        uint8_t uid_raw[5];

        if (request_card() == 0) {
            if (anticoll(uid_raw) == 0) {
                char uid[50];

                snprintf(uid, sizeof(uid), "%02X %02X %02X %02X",
                         uid_raw[0], uid_raw[1], uid_raw[2], uid_raw[3]);

                ESP_LOGI(TAG, "UID: %s", uid);

                if (strcmp(uid, last_uid) != 0) {
                    strcpy(last_uid, uid);
                    send_uid_to_server(uid);
                }

                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}