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

uint8_t senderMacAddress[] = {0x30, 0x30, 0xF9, 0x54, 0x50, 0xE8};  // Adjust as needed
uint8_t buffer[FILE_CHUNK_SIZE + 16];  // Increased size for alignment
size_t chunkIndex = 0;
size_t totalBytesReceived = 0;
size_t fileSize = 0;
bool fileReceived = false;
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
    memcpy(peerInfo.peer_addr, senderMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }
}

void loop() {
    if (fileReceived) {
        Serial.println("File received, preparing to send back.");
        sendEncryptedFile("/tosender.pdf");  // Replace with your file path
        fileReceived = false;  // Reset the flag
    }
}

void onDataReceived(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len < 2) return;  // Ignore invalid packets

    uint8_t decryptedBuffer[FILE_CHUNK_SIZE];
    size_t chunkSize = len - 2;  // Subtract metadata bytes

    aesDecrypt(data, decryptedBuffer, chunkSize);

    if (!receivedFile) {
        receivedFile = SD_MMC.open("/received_file.pdf", FILE_WRITE);
    }
    if (receivedFile) {
        receivedFile.write(decryptedBuffer, chunkSize);
        totalBytesReceived += chunkSize;

        // Calculate progress safely
        float progress = (fileSize > 0) ? ((float)totalBytesReceived / fileSize) * 100 : 0;
        Serial.printf("Received %d bytes (%.2f%%)\n", totalBytesReceived, progress);

        if (chunkSize < FILE_CHUNK_SIZE) {  // End of file detected
            receivedFile.close();
            Serial.println("File received and saved.");
            fileReceived = true;
        }
    }
}

void sendEncryptedFile(const char *path) {
    File file = SD_MMC.open(path);
    if (!file) {
        Serial.println("Failed to open file");
        return;
    }

    size_t fileSize = file.size();
    size_t bytesRead = 0;

    while (bytesRead < fileSize) {
        size_t toRead = min((size_t)FILE_CHUNK_SIZE, fileSize - bytesRead);
        size_t len = file.read(buffer, toRead);
        bytesRead += len;

        uint8_t encryptedBuffer[FILE_CHUNK_SIZE + 16];
        size_t alignedLen = ((len + 15) / 16) * 16;  // Align to AES block size
        memset(buffer + len, 0, alignedLen - len);   // Zero padding

        aesEncrypt(buffer, encryptedBuffer, alignedLen);

        encryptedBuffer[len] = (chunkIndex >> 8) & 0xFF;
        encryptedBuffer[len + 1] = chunkIndex & 0xFF;

        esp_now_send(senderMacAddress, encryptedBuffer, alignedLen + 2);
        chunkIndex++;

        float progress = ((float)bytesRead / fileSize) * 100;
        Serial.printf("Sent %d/%d bytes (%.2f%%)\n", bytesRead, fileSize, progress);

        delay(50);  // Allow transmission time
    }

    file.close();
    Serial.println("File sent.");
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
