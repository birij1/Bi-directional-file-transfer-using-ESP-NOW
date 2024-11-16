#include "SD_MMC.h" // Enables communication with the SD card using the SDMMC interface.
#include <esp_now.h> // Allows ESP-NOW communication, a peer-to-peer wireless protocol.
#include <WiFi.h> // Provides Wi-Fi functionality (needed for ESP-NOW initialization).
#include <mbedtls/aes.h> // Library for AES encryption/decryption (secure data transfer).
#define SD_MMC_CMD 38 // GPIO pin used for CMD line of SD card.
#define SD_MMC_CLK 39 // GPIO pin used for the clock line of SD card.
#define SD_MMC_D0 40 // GPIO pin used for data line D0 (single-bit mode).
#define FILE_CHUNK_SIZE 192 // Maximum chunk size for each data packet sent via ESP-NOW.
const uint8_t aesKey[16] = { // 16-byte AES key for encryption.
0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
0x39, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46
};
uint8_t receiverMacAddress[] = {0xDC, 0xDA, 0x0C, 0x52, 0xCA, 0x3C}; // MAC address of the
receiver ESP32.
uint8_t buffer[FILE_CHUNK_SIZE + 10]; // Buffer to hold file chunks.
size_t chunkIndex = 0; // Index to track the chunk being sent.
size_t totalBytesSent = 0; // Tracks total bytes sent.
size_t totalBytesReceived = 0; // Tracks total bytes received (for feedback).
size_t fileSize = 0; // Size of the file being transferred.
bool fileSent = false; // Flag indicating if the file has been sent.
bool waitingForFile = false; // Flag to check if we're waiting for a file from the receiver.
File receivedFile; // File object to hold the incoming file.
void setup() {
Serial.begin(115200); // Start serial communication at 115200 baud for debugging.

SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0); // Set SD card pins.
if (!SD_MMC.begin("/sdcard", true)) { // Initialize SD card.
Serial.println("Card Mount Failed"); // Print error if failed.
return; // Stop if the SD card is not detected.

}

WiFi.mode(WIFI_STA); // Set Wi-Fi mode to station mode (required for ESP-NOW).

if (esp_now_init() != ESP_OK) { // Initialize ESP-NOW.
Serial.println("ESP-NOW Initialization Failed");
return;
}

esp_now_register_send_cb(onDataSent); // Register callback for send events.
esp_now_register_recv_cb(onDataReceived); // Register callback for receive events.

esp_now_peer_info_t peerInfo = {}; // Create peer info structure.
memcpy(peerInfo.peer_addr, receiverMacAddress, 6); // Assign receiver MAC address.
peerInfo.channel = 0; // Use channel 0 for simplicity.
peerInfo.encrypt = false; // No additional encryption (we handle it ourselves).

if (esp_now_add_peer(&peerInfo) != ESP_OK) { // Add the receiver as a peer.
Serial.println("Failed to add peer");
return;
}

uint8_t readySignal = 0x01; // Send a signal to indicate readiness to transfer.
esp_now_send(receiverMacAddress, &readySignal, 1); // Send signal to receiver.
}
Main loop
void loop() {
if (!fileSent) { // Check if the file has been sent.
sendEncryptedFile("/Abst.pdf"); // Send the encrypted file.

fileSent = true; // Mark file as sent.
}
}
void sendEncryptedFile(const char *path) {
File file = SD_MMC.open(path); // Open the file.
if (!file) { // Check if the file was opened successfully.
Serial.println("Failed to open file");
return;
}

fileSize = file.size(); // Get the size of the file.
size_t bytesRead = 0; // Track how many bytes have been read.

while (bytesRead < fileSize) { // Loop until all bytes are sent.
size_t toRead = min((size_t)FILE_CHUNK_SIZE, fileSize - bytesRead); // Calculate chunk size.
size_t len = file.read(buffer, toRead); // Read chunk into buffer.
bytesRead += len; // Update bytesRead.

uint8_t encryptedBuffer[FILE_CHUNK_SIZE + 10]; // Buffer for encrypted data.
size_t alignedLen = ((len + 15) / 16) * 16; // Align to AES block size.
memset(buffer + len, 0, alignedLen - len); // Pad with zeros.

aesEncrypt(buffer, encryptedBuffer, alignedLen); // Encrypt the chunk.

encryptedBuffer[len] = (chunkIndex >> 8) & 0xFF; // Add chunk index (MSB).
encryptedBuffer[len + 1] = chunkIndex & 0xFF; // Add chunk index (LSB).

esp_now_send(receiverMacAddress, encryptedBuffer, len + 2); // Send chunk.
chunkIndex++; // Increment chunk index.

totalBytesSent += len; // Update total bytes sent.

float progress = ((float)totalBytesSent / fileSize) * 100; // Calculate progress.
Serial.printf("Sent %d/%d bytes (%.2f%%)\n", totalBytesSent, fileSize, progress);

delay(50); // Short delay to prevent congestion.
}

file.close(); // Close the file.
Serial.println("File sent.");
waitingForFile = true; // Ready to receive the file.
}
void aesEncrypt(const uint8_t *input, uint8_t *output, size_t length) {
mbedtls_aes_context aes; // Create an AES context (holds encryption state)
mbedtls_aes_init(&aes); // Initialize the AES context
mbedtls_aes_setkey_enc(&aes, aesKey, 128); // Set encryption key (128-bit AES)

// Encrypt data in 16-byte (128-bit) blocks
for (size_t i = 0; i < length; i += 16) {
mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input + i, output + i);
}

mbedtls_aes_free(&aes); // Free resources used by the AES context
}
void aesDecrypt(const uint8_t *input, uint8_t *output, size_t length) {
mbedtls_aes_context aes; // Create an AES context (holds decryption state)
mbedtls_aes_init(&aes); // Initialize the AES context
mbedtls_aes_setkey_dec(&aes, aesKey, 128); // Set decryption key (128-bit AES)

// Decrypt data in 16-byte (128-bit) blocks
for (size_t i = 0; i < length; i += 16) {
mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, input + i, output + i);
}

mbedtls_aes_free(&aes); // Free resources used by the AES context
}
