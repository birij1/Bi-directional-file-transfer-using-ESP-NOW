#include "SD_MMC.h"
#include <esp_now.h>
#include <WiFi.h>
#include <mbedtls/aes.h>

#define SD_MMC_CMD 38
#define SD_MMC_CLK 39
#define SD_MMC_D0  40

#define FILE_CHUNK_SIZE 192

const uint8_t aesKey[16] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46
};

uint8_t receiverMacAddress[] = {0x74, 0x4D, 0xBD, 0x8D, 0x26, 0xB0};
uint8_t buffer[FILE_CHUNK_SIZE + 10];
size_t chunkIndex = 0;
size_t totalBytesSent = 0;
size_t totalBytesReceived = 0;
size_t fileSize = 0;
bool fileSent = false;
bool waitingForFile = false;
File receivedFile;

void setup() {
    Serial.begin(115200);

    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("Card Mount Failed");
        return;
    }

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Initialization Failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    // Send ready signal to initiate file transfer
    uint8_t readySignal = 0x01;
    esp_now_send(receiverMacAddress, &readySignal, 1);
}

void loop() {
    if (!fileSent) {
        sendEncryptedFile("/Abst.pdf");
        fileSent = true;
    }
}

void sendEncryptedFile(const char *path) {
    File file = SD_MMC.open(path);
    if (!file) {
        Serial.println("Failed to open file");
        return;
    }

    fileSize = file.size();
    size_t bytesRead = 0;

    while (bytesRead < fileSize) {
        size_t toRead = min((size_t)FILE_CHUNK_SIZE, fileSize - bytesRead);
        size_t len = file.read(buffer, toRead);
        bytesRead += len;

        uint8_t encryptedBuffer[FILE_CHUNK_SIZE + 10];
        size_t alignedLen = ((len + 15) / 16) * 16;
        memset(buffer + len, 0, alignedLen - len);  // Pad with zeros

        aesEncrypt(buffer, encryptedBuffer, alignedLen);

        encryptedBuffer[len] = (chunkIndex >> 8) & 0xFF;
        encryptedBuffer[len + 1] = chunkIndex & 0xFF;

        esp_now_send(receiverMacAddress, encryptedBuffer, len + 2);
        chunkIndex++;
        totalBytesSent += len;

        float progress = ((float)totalBytesSent / fileSize) * 100;
        Serial.printf("Sent %d/%d bytes (%.2f%%)\n", totalBytesSent, fileSize, progress);

        delay(50);
    }

    file.close();
    Serial.println("File sent.");
    waitingForFile = true;  // Ready to receive file from receiver
}

void onDataReceived(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (waitingForFile) {
        if (!receivedFile) {
            receivedFile = SD_MMC.open("/received_file.pdf", FILE_WRITE);
        }

        uint8_t decryptedBuffer[FILE_CHUNK_SIZE];
        size_t chunkSize = len - 2;

        aesDecrypt(data, decryptedBuffer, chunkSize);
        receivedFile.write(decryptedBuffer, chunkSize);
        totalBytesReceived += chunkSize;

        float progress = ((float)totalBytesReceived / fileSize) * 100;
        Serial.printf("Received %d bytes (%.2f%%)\n", totalBytesReceived, progress);

        if (chunkSize < FILE_CHUNK_SIZE) {
            receivedFile.close();
            Serial.println("File received and saved.");
            waitingForFile = false;
        }
    }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.printf("Send status: %s\n", status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void aesEncrypt(const uint8_t *input, uint8_t *output, size_t length) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, aesKey, 128);

    for (size_t i = 0; i < length; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);
}

void aesDecrypt(const uint8_t *input, uint8_t *output, size_t length) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, aesKey, 128);

    for (size_t i = 0; i < length; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
    }
    mbedtls_aes_free(&aes);
}
