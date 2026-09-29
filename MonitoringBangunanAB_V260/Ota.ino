// =====================================================
// Ota.ino
// Unduh firmware dari Firebase Storage.
// Pola sama dengan OTA_Function.ino proyek Tensi Meter:
// GET, Update.begin(ukuran), tulis chunk + LED kedip lambat,
// Update.end, LED kedip cepat, restart.
// =====================================================

/**
 * Mengunduh MonitoringBangunan.bin lalu menulisnya ke partisi OTA.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Jika berhasil, LED kedip cepat lalu ESP32 restart.
 *              Jika gagal, LED OFF, watchdog dinyalakan lagi, fungsi kembali.
 */
void vUpdateFirmware()
{
    char szUrl[192];
    int iWritten = snprintf(
        szUrl,
        sizeof(szUrl),
        "https://%s%s",
        OTA_FIRMWARE_HOST,
        OTA_FIRMWARE_PATH
    );

    if (iWritten <= 0 || iWritten >= (int)sizeof(szUrl))
    {
        Serial.println("[ERROR] URL OTA tidak muat di buffer.");
        return;
    }

    Serial.println("[INFO] Versi berbeda. Mengunduh firmware OTA...");
    Serial.printf("[INFO] %s\n", szUrl);

    vLedBeginOtaProgress();

    // writeStream memblokir tanpa memberi makan watchdog.
    // Batas watchdog sama dengan timeout HTTP (120 detik).
    vStopWatchdogForSleep();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    if (!http.begin(client, szUrl))
    {
        Serial.println("[ERROR] HTTPClient OTA gagal diinisialisasi.");
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    vLedTickOtaProgress();
    int iHttpCode = http.GET();
    if (iHttpCode <= 0)
    {
        Serial.printf(
            "[ERROR] OTA HTTP gagal: %s\n",
            http.errorToString(iHttpCode).c_str()
        );
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    if (iHttpCode != HTTP_CODE_OK)
    {
        Serial.printf("[ERROR] OTA HTTP status bukan 200: %d\n", iHttpCode);
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    int iContentLen = http.getSize();
    Serial.printf("[INFO] OTA Content-Length: %d\n", iContentLen);
    if (iContentLen <= 0)
    {
        Serial.println("[ERROR] OTA tidak mengirim Content-Length. Dibatalkan.");
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    if (!Update.begin((size_t)iContentLen))
    {
        Serial.println(
            "[ERROR] OTA tidak cukup ruang. "
            "Pakai skema partisi dengan dua slot app."
        );
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    WiFiClient* pStream = http.getStreamPtr();
    size_t uWritten = 0;
    uint8_t au8Chunk[OTA_WRITE_CHUNK_BYTES];

    // Tulis per chunk supaya LED bisa kedip lambat selama unduh.
    uint32_t u32WaitStartMs = millis();
    while ((pStream != nullptr) && (uWritten < (size_t)iContentLen))
    {
        vLedTickOtaProgress();

        size_t uAvail = pStream->available();
        if (uAvail == 0)
        {
            if (!http.connected())
            {
                break;
            }

            // Jangan menunggu tanpa batas jika stream diam.
            if (millis() - u32WaitStartMs >= OTA_HTTP_TIMEOUT_MS)
            {
                Serial.println("[ERROR] OTA timeout menunggu data stream.");
                break;
            }

            delay(1);
            continue;
        }

        u32WaitStartMs = millis();

        size_t uToRead = uAvail;
        if (uToRead > sizeof(au8Chunk))
        {
            uToRead = sizeof(au8Chunk);
        }

        size_t uRemain = (size_t)iContentLen - uWritten;
        if (uToRead > uRemain)
        {
            uToRead = uRemain;
        }

        int iGot = pStream->readBytes(
            (char*)au8Chunk,
            uToRead
        );

        if (iGot <= 0)
        {
            break;
        }

        size_t uChunkWritten = Update.write(au8Chunk, (size_t)iGot);
        if (uChunkWritten != (size_t)iGot)
        {
            Serial.println("[ERROR] OTA Update.write gagal sebagian.");
            break;
        }

        uWritten += uChunkWritten;
    }

    Serial.printf(
        "[INFO] OTA %u/%d byte tertulis.\n",
        (unsigned)uWritten,
        iContentLen
    );

    if (uWritten != (size_t)iContentLen)
    {
        Serial.println("[ERROR] OTA hanya menulis sebagian file. Dibatalkan.");
        Update.abort();
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    if (!Update.end())
    {
        Serial.printf(
            "[ERROR] Update.end gagal. Kode %u\n",
            (unsigned)Update.getError()
        );
        http.end();
        vLedSignalOtaFail();
        vInitWatchdog();
        return;
    }

    http.end();

    if (Update.isFinished())
    {
        Serial.println("[INFO] OTA selesai. LED kedip cepat lalu restart.");
        // Tidak menghidupkan ulang watchdog: restart segera.
        vLedSignalOtaSuccessThenRestart();
        return;
    }

    Serial.printf(
        "[ERROR] Update.isFinished gagal. Kode %u\n",
        (unsigned)Update.getError()
    );
    vLedSignalOtaFail();
    vInitWatchdog();
}
