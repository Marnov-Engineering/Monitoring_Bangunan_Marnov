// =====================================================
// CloudSheet.ino
// HTTPS POST JSON ke Google Apps Script Web App
// =====================================================

/*
 * =====================================================
 * PANDUAN GOOGLE SPREADSHEET + DASHBOARD
 * =====================================================
 *
 * Jangan isi URL spreadsheet /edit ke ESP32.
 * Yang dipakai adalah URL Web App yang berakhiran /exec.
 *
 * Kode API + Dashboard untuk firmware ini ada di folder versi yang sama:
 *   Program Tes/MonitoringBangunanAB_V180/WebApiSpreadsheet/
 *   - AppsScript_Dashboard_Monitoring.gs
 *   - AppsScript_Dashboard.html
 * Perubahan link API (GET ?action=script_url) mulai di V1.6.0.
 * Jangan tempel salinan V1.5.0: versi itu belum punya endpoint ini.
 *
 * Ringkas:
 * 1. Spreadsheet > Extensions > Apps Script
 * 2. Paste isi file .gs tersebut, Save
 * 3. Run vInitDashboard (izinkan akses)
 * 4. Deploy > New deployment > Web app
 *    Execute as: Me | Access: Anyone
 * 5. Copy URL /exec ke form node (sekali, saat pertama pasang)
 * 6. Ganti link berikutnya lewat HMI (tombol Link API), bukan lewat tiap node
 * 7. Opsional: trigger tiap 1 menit → vRefreshDashboardStatus
 *
 * Dashboard:
 * Hijau  = OK
 * Kuning = data terlambat, cek WiFi
 * Merah  = sensor/baterai, turun ke lapangan
 * Abu    = offline / belum ada data
 *
 * timezone ditulis sebagai teks ('+07:00) agar Sheets
 * tidak menganggapnya rumus.
 * =====================================================
 */

// =====================================================
// FUNGSI INTERNAL
// =====================================================

/**
 * Melakukan HTTPS POST dengan payload JSON ke Google Apps Script Web App.
 *
 * URL yang benar berakhiran /exec, contoh:
 *   https://script.google.com/macros/s/AKfycb.../exec
 *
 * Bukan URL spreadsheet:
 *   https://docs.google.com/spreadsheets/d/.../edit
 *
 * Google Apps Script merespons 302 Found setelah doPost berhasil.
 * Jangan follow redirect: mengikuti 302 sebagai POST ke
 * script.googleusercontent.com sering menghasilkan HTTP 400.
 * Kode 200 atau 302 dianggap sukses.
 *
 * Input:
 * szJsonPayload - string JSON yang siap dikirim.
 *
 * Output: true jika HTTP 200 atau 302.
 * Side effect: Membuka koneksi HTTPS, mengirim data.
 */
static bool bDoHttpsPost(const char* szJsonPayload)
{
    if (strlen(g_nodeConfig.szScriptUrl) == 0)
    {
        Serial.println("[ERROR] Script URL belum dikonfigurasi.");
        return false;
    }

    // URL spreadsheet /edit tidak bisa menerima JSON dari ESP32.
    if (strstr(g_nodeConfig.szScriptUrl, "docs.google.com/spreadsheets") != nullptr)
    {
        Serial.println(
            "[ERROR] URL yang diisi adalah link Spreadsheet, bukan Apps Script."
        );
        Serial.println(
            "[ERROR] Ganti di /config dengan URL Web App yang berakhiran /exec"
        );
        Serial.println(
            "[ERROR] Contoh: https://script.google.com/macros/s/XXXX/exec"
        );
        return false;
    }

    if (strstr(g_nodeConfig.szScriptUrl, "/exec") == nullptr)
    {
        Serial.println(
            "[WARNING] Script URL tidak berakhiran /exec. "
            "Deploy Web App, lalu copy URL deployment."
        );
    }

    WiFiClientSecure client;

    // Untuk program tes: skip verifikasi sertifikat SSL.
    client.setInsecure();

    HTTPClient http;

    // Jangan ikuti redirect. 302 dari Apps Script = doPost sudah jalan.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(HTTPS_POST_TIMEOUT_MS);

    if (!http.begin(client, g_nodeConfig.szScriptUrl))
    {
        Serial.println("[ERROR] HTTPClient gagal diinisialisasi.");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    vFeedWatchdog();

    int iHttpCode = http.POST(
        (uint8_t*)szJsonPayload,
        (size_t)strlen(szJsonPayload)
    );

    // 200 OK atau 302 Found = Apps Script menerima data.
    bool bSuccess = (
        iHttpCode == HTTP_CODE_OK ||
        iHttpCode == HTTP_CODE_FOUND ||
        iHttpCode == HTTP_CODE_MOVED_PERMANENTLY
    );

    if (bSuccess)
    {
        Serial.printf("[INFO] POST sukses. HTTP %d\n", iHttpCode);
    }
    else
    {
        Serial.printf(
            "[ERROR] POST gagal. HTTP %d | Error: %s\n",
            iHttpCode,
            http.errorToString(iHttpCode).c_str()
        );

        char szResp[160];
        int iLen = http.getSize();
        WiFiClient* pStream = http.getStreamPtr();

        if (pStream != nullptr && iLen > 0)
        {
            int iRead = pStream->readBytes(
                (uint8_t*)szResp,
                sizeof(szResp) - 1
            );
            szResp[iRead] = '\0';
            Serial.printf("[ERROR] Respons server: %s\n", szResp);
        }
    }

    http.end();
    return bSuccess;
}

// =====================================================
// FUNGSI PUBLIK
// =====================================================

/**
 * Mengirim data Node A (iluminansi) ke Google Spreadsheet.
 *
 * Membangun JSON sesuai skema kolom yang telah disepakati,
 * kemudian memanggil bDoHttpsPost().
 *
 * Kolom: node_id, firmware_version, timestamp, timezone,
 *        illuminance_lux, illuminance_valid, sensor_status, battery_voltage
 *
 * Input:
 * pData       - data iluminansi yang akan dikirim.
 * szTimestamp - timestamp lokal ISO 8601, contoh "2026-09-22T10:35:00+08:00".
 *
 * Output: true jika data berhasil diterima Spreadsheet.
 */
bool bSendNodeAToSheet(const NodeAData* pData, const char* szTimestamp)
{
    if (pData == nullptr || szTimestamp == nullptr)
    {
        Serial.println("[ERROR] bSendNodeAToSheet: parameter null.");
        return false;
    }

    JsonDocument doc;
    JsonArray rows = doc["rows"].to<JsonArray>();
    JsonObject row = rows.add<JsonObject>();

    row["node_id"]           = g_nodeConfig.szNodeId;
    row["firmware_version"]  = FIRMWARE_VERSION;
    row["timestamp"]         = szTimestamp;
    row["timezone"]          = g_nodeConfig.szTimezone;

    // Lux kosong jika tidak valid, supaya Sheet tidak menyimpan angka palsu.
    if (pData->u8IlluminanceValid != 0)
    {
        row["illuminance_lux"] = roundf(pData->fIlluminanceLux * 10.0f) / 10.0f;
    }
    else
    {
        row["illuminance_lux"] = nullptr;
    }
    row["illuminance_valid"] = pData->u8IlluminanceValid;
    row["sensor_status"]     = pData->u8SensorStatus;
    row["battery_voltage"]   = roundf(pData->fBatteryVoltage * 100.0f) / 100.0f;

    char szPayload[SHEET_ROW_PAYLOAD_BYTES];
    size_t uLen = serializeJson(doc, szPayload, sizeof(szPayload));

    if (uLen == 0)
    {
        Serial.println("[ERROR] Gagal serialize JSON Node A.");
        return false;
    }

    Serial.printf("[DEBUG] Payload Node A: %s\n", szPayload);
    return bDoHttpsPost(szPayload);
}

/**
 * Mengirim data Node B (IAQ) ke Google Spreadsheet.
 *
 * Kolom: node_id, firmware_version, timestamp, timezone,
 *        air_temperature_c, air_temperature_valid,
 *        relative_humidity_pct, relative_humidity_valid,
 *        co2_ppm, co2_valid, sensor_status, battery_voltage
 *
 * Input:
 * pData       - data IAQ yang akan dikirim.
 * szTimestamp - timestamp lokal ISO 8601.
 *
 * Output: true jika data berhasil diterima Spreadsheet.
 */
bool bSendNodeBToSheet(const NodeBData* pData, const char* szTimestamp)
{
    if (pData == nullptr || szTimestamp == nullptr)
    {
        Serial.println("[ERROR] bSendNodeBToSheet: parameter null.");
        return false;
    }

    JsonDocument doc;
    JsonArray rows = doc["rows"].to<JsonArray>();
    JsonObject row = rows.add<JsonObject>();

    row["node_id"]                  = g_nodeConfig.szNodeId;
    row["firmware_version"]         = FIRMWARE_VERSION;
    row["timestamp"]                = szTimestamp;
    row["timezone"]                 = g_nodeConfig.szTimezone;

    if (pData->u8TempValid != 0)
    {
        row["air_temperature_c"] = roundf(pData->fAirTemperatureC * 10.0f) / 10.0f;
    }
    else
    {
        row["air_temperature_c"] = nullptr;
    }
    row["air_temperature_valid"] = pData->u8TempValid;

    if (pData->u8RhValid != 0)
    {
        row["relative_humidity_pct"] = roundf(pData->fRelativeHumidityPct * 10.0f) / 10.0f;
    }
    else
    {
        row["relative_humidity_pct"] = nullptr;
    }
    row["relative_humidity_valid"] = pData->u8RhValid;

    if (pData->u8Co2Valid != 0)
    {
        row["co2_ppm"] = roundf(pData->fCo2Ppm);
    }
    else
    {
        row["co2_ppm"] = nullptr;
    }
    row["co2_valid"] = pData->u8Co2Valid;
    row["sensor_status"]            = pData->u8SensorStatus;
    row["battery_voltage"]          = roundf(pData->fBatteryVoltage * 100.0f) / 100.0f;

    // Sama dengan Node A: satu objek di dalam rows. Antrian memakai SHEET_BATCH_PAYLOAD_BYTES.
    char szPayload[SHEET_ROW_PAYLOAD_BYTES];
    size_t uLen = serializeJson(doc, szPayload, sizeof(szPayload));

    if (uLen == 0)
    {
        Serial.println("[ERROR] Gagal serialize JSON Node B.");
        return false;
    }

    Serial.printf("[DEBUG] Payload Node B: %s\n", szPayload);
    return bDoHttpsPost(szPayload);
}

/**
 * Mengisi satu objek JSON dari satu baris CSV.
 * Baris disalin dulu karena parsing memotong koma di buffer lokal.
 *
 * Input:
 * row      - objek di dalam array "rows".
 * szCsvLine - satu baris data tanpa newline.
 *
 * Output: true jika kolom cukup untuk jenis node ini.
 * Side effect: tidak mengirim jaringan.
 */
static bool bFillSheetJsonFromCsvLine(JsonObject row, const char* szCsvLine)
{
    if (row.isNull() || szCsvLine == nullptr || szCsvLine[0] == '\0')
    {
        return false;
    }

    char szLine[320];
    snprintf(szLine, sizeof(szLine), "%s", szCsvLine);

    char* apFields[16];
    uint8_t u8Count = 0;
    char* psz = szLine;

    while (u8Count < 16)
    {
        apFields[u8Count++] = psz;
        char* pszComma = strchr(psz, ',');
        if (pszComma == nullptr)
        {
            break;
        }
        *pszComma = '\0';
        psz = pszComma + 1;
    }

    if (bIsNodeTypeA())
    {
        if (u8Count < 8)
        {
            Serial.println("[ERROR] CSV Node A kolom kurang.");
            return false;
        }

        row["node_id"]           = apFields[0];
        row["firmware_version"]  = apFields[1];
        row["timestamp"]         = apFields[2];
        row["timezone"]          = apFields[3];
        if (apFields[4][0] == '\0')
        {
            row["illuminance_lux"] = nullptr;
        }
        else
        {
            row["illuminance_lux"] = atof(apFields[4]);
        }
        row["illuminance_valid"] = atoi(apFields[5]);
        row["sensor_status"]     = atoi(apFields[6]);
        row["battery_voltage"]   = atof(apFields[7]);
        return true;
    }

    if (u8Count < 12)
    {
        Serial.println("[ERROR] CSV Node B kolom kurang.");
        return false;
    }

    row["node_id"]                 = apFields[0];
    row["firmware_version"]        = apFields[1];
    row["timestamp"]               = apFields[2];
    row["timezone"]                = apFields[3];
    if (apFields[4][0] == '\0')
    {
        row["air_temperature_c"] = nullptr;
    }
    else
    {
        row["air_temperature_c"] = atof(apFields[4]);
    }
    row["air_temperature_valid"] = atoi(apFields[5]);

    if (apFields[6][0] == '\0')
    {
        row["relative_humidity_pct"] = nullptr;
    }
    else
    {
        row["relative_humidity_pct"] = atof(apFields[6]);
    }
    row["relative_humidity_valid"] = atoi(apFields[7]);

    if (apFields[8][0] == '\0')
    {
        row["co2_ppm"] = nullptr;
    }
    else
    {
        row["co2_ppm"] = atof(apFields[8]);
    }
    row["co2_valid"] = atoi(apFields[9]);
    row["sensor_status"]           = atoi(apFields[10]);
    row["battery_voltage"]         = atof(apFields[11]);
    return true;
}

/**
 * Mengirim beberapa baris CSV dalam satu POST.
 * Bentuk JSON: {"rows":[ {...}, {...} ]}.
 * Satu baris pun memakai bentuk ini, supaya Apps Script hanya punya satu pintu.
 *
 * Input:
 * apLines  - pointer ke tiap baris CSV.
 * u8Count  - jumlah baris, 1 sampai BACKFILL_BATCH_MAX.
 *
 * Output: true jika HTTP 200 atau 302.
 * Side effect: satu koneksi HTTPS.
 */
static bool bSendCsvBatchToSheet(char apLines[][320], uint8_t u8Count)
{
    if (apLines == nullptr || u8Count == 0 || u8Count > BACKFILL_BATCH_MAX)
    {
        return false;
    }

    JsonDocument doc;
    JsonArray rows = doc["rows"].to<JsonArray>();

    for (uint8_t u8Index = 0; u8Index < u8Count; u8Index++)
    {
        JsonObject row = rows.add<JsonObject>();
        if (!bFillSheetJsonFromCsvLine(row, apLines[u8Index]))
        {
            return false;
        }
    }

    // Buffer statis: 8 KB di stack mudah menabrak batas task ESP32.
    static char szPayload[SHEET_BATCH_PAYLOAD_BYTES];
    size_t uLen = serializeJson(doc, szPayload, sizeof(szPayload));
    if (uLen == 0)
    {
        Serial.println(
            "[ERROR] JSON antrian tidak muat di buffer. "
            "Kurangi BACKFILL_BATCH_MAX atau perbesar SHEET_BATCH_PAYLOAD_BYTES."
        );
        return false;
    }

    Serial.printf(
        "[INFO] POST antrian: %u baris, %u byte.\n",
        (unsigned)u8Count,
        (unsigned)uLen
    );
    return bDoHttpsPost(szPayload);
}

/**
 * Mengirim satu baris CSV ke Sheet sebagai JSON rows berisi satu objek.
 *
 * Input:
 * szCsvLine - satu baris data tanpa newline.
 *
 * Output: true jika POST sukses.
 */
bool bSendCsvLineToSheet(const char* szCsvLine)
{
    if (szCsvLine == nullptr || szCsvLine[0] == '\0')
    {
        return false;
    }

    char aLine[1][320];
    snprintf(aLine[0], sizeof(aLine[0]), "%s", szCsvLine);
    return bSendCsvBatchToSheet(aLine, 1);
}


/**
 * Mengirim baris CSV yang belum terkirim, dalam satu POST.
 *
 * Mengirim baris dengan:
 *   last_sent < timestamp_baris <= szBeforeTimestampExclusive
 *
 * Sampel siklus ini ikut jika masih muat dalam BACKFILL_BATCH_MAX.
 * Kalau antrian kosong, POST tetap satu baris (sampel ini).
 * Kalau POST gagal, last_sent tidak maju dan seluruh tumpukan diulang.
 *
 * Input:
 * szBeforeTimestampExclusive - timestamp sampel saat ini (ikut jika muat).
 *                              Jika nullptr/kosong, batas atas tidak dipakai.
 *
 * Output:
 * true  = masih ada antrian yang belum terkirim (batch penuh, sisa, atau POST gagal).
 * false = tidak ada gap tersisa. Cek last_sent: jika sama dengan sampel ini,
 *         sampel sudah terkirim di POST ini dan tidak perlu POST kedua.
 */
bool bBackfillUnsentBefore(const char* szBeforeTimestampExclusive)
{
    if (!bIsWifiConnected())
    {
        return false;
    }

    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    if (!LittleFS.exists(szPath))
    {
        return false;
    }

    char szLastSent[32];
    vGetLastSentTimestamp(szLastSent, sizeof(szLastSent));

    File file = LittleFS.open(szPath, "r");
    if (!file)
    {
        return false;
    }

    // Lewati header.
    char szLine[320];
    if (file.available())
    {
        file.readBytesUntil('\n', szLine, sizeof(szLine) - 1);
    }

    // BSS, bukan stack: 25 baris x 320 byte.
    static char aBatchLines[BACKFILL_BATCH_MAX][320];
    static char aBatchTs[BACKFILL_BATCH_MAX][32];
    uint8_t u8BatchCount = 0;

    while (file.available() && u8BatchCount < BACKFILL_BATCH_MAX)
    {

        size_t uLen = file.readBytesUntil('\n', szLine, sizeof(szLine) - 1);
        szLine[uLen] = '\0';
        if (uLen > 0 && szLine[uLen - 1] == '\r')
        {
            szLine[uLen - 1] = '\0';
        }
        if (szLine[0] == '\0')
        {
            continue;
        }

        // timestamp = field ke-3 (index 2).
        char szCopy[320];
        snprintf(szCopy, sizeof(szCopy), "%s", szLine);

        char* pszTs = nullptr;
        char* psz = szCopy;
        int iField = 0;
        while (psz != nullptr)
        {
            char* pszComma = strchr(psz, ',');
            if (pszComma != nullptr)
            {
                *pszComma = '\0';
            }
            if (iField == 2)
            {
                pszTs = psz;
                break;
            }
            iField++;
            psz = (pszComma != nullptr) ? (pszComma + 1) : nullptr;
        }

        if (pszTs == nullptr)
        {
            continue;
        }

        // Sudah terkirim jika timestamp <= last_sent (ISO lexicographic).
        if (szLastSent[0] != '\0' && strcmp(pszTs, szLastSent) <= 0)
        {
            continue;
        }

        // Lewati yang lebih baru dari sampel siklus ini.
        // Timestamp yang sama dengan sampel ini BOLEH ikut, asal masih muat.
        if (szBeforeTimestampExclusive != nullptr
            && szBeforeTimestampExclusive[0] != '\0'
            && strcmp(pszTs, szBeforeTimestampExclusive) > 0)
        {
            continue;
        }

        snprintf(aBatchLines[u8BatchCount], sizeof(aBatchLines[u8BatchCount]), "%s", szLine);
        snprintf(aBatchTs[u8BatchCount], sizeof(aBatchTs[u8BatchCount]), "%s", pszTs);
        u8BatchCount++;
    }

    // Batch penuh: cek apakah file masih punya baris belum terkirim
    // sampai timestamp sampel ini (termasuk sampel ini).
    bool bGapsRemain = false;
    if (u8BatchCount >= BACKFILL_BATCH_MAX && file.available())
    {
        while (file.available())
        {
            size_t uLen = file.readBytesUntil('\n', szLine, sizeof(szLine) - 1);
            szLine[uLen] = '\0';
            if (uLen > 0 && szLine[uLen - 1] == '\r')
            {
                szLine[uLen - 1] = '\0';
            }
            if (szLine[0] == '\0')
            {
                continue;
            }

            char szCopy[320];
            snprintf(szCopy, sizeof(szCopy), "%s", szLine);

            char* pszTs = nullptr;
            char* psz = szCopy;
            int iField = 0;
            while (psz != nullptr)
            {
                char* pszComma = strchr(psz, ',');
                if (pszComma != nullptr)
                {
                    *pszComma = '\0';
                }
                if (iField == 2)
                {
                    pszTs = psz;
                    break;
                }
                iField++;
                psz = (pszComma != nullptr) ? (pszComma + 1) : nullptr;
            }

            if (pszTs == nullptr)
            {
                continue;
            }
            if (szLastSent[0] != '\0' && strcmp(pszTs, szLastSent) <= 0)
            {
                continue;
            }
            if (szBeforeTimestampExclusive != nullptr
                && szBeforeTimestampExclusive[0] != '\0'
                && strcmp(pszTs, szBeforeTimestampExclusive) > 0)
            {
                continue;
            }

            bGapsRemain = true;
            break;
        }
    }

    file.close();

    if (u8BatchCount == 0)
    {
        return false;
    }

    Serial.printf(
        "[INFO] Antrian dikumpulkan: %u baris (%s .. %s).\n",
        (unsigned)u8BatchCount,
        aBatchTs[0],
        aBatchTs[u8BatchCount - 1]
    );

    if (!bSendCsvBatchToSheet(aBatchLines, u8BatchCount))
    {
        Serial.println("[WARNING] Backfill gagal. Tumpukan yang sama diulang siklus berikutnya.");
        return true;
    }

    vSetLastSentTimestamp(aBatchTs[u8BatchCount - 1]);
    Serial.printf("[INFO] Backfill selesai: %u baris dalam 1 POST.\n", (unsigned)u8BatchCount);

    if (bGapsRemain)
    {
        Serial.println("[INFO] Backfill batch penuh; sisa antrian dilanjut siklus berikutnya.");
    }

    return bGapsRemain;
}

/**
 * Menilai apakah teks adalah URL Web App Apps Script.
 *
 * Input: szUrl
 * Output: true jika https://script.google.com/.../exec dan muat di config.
 */
static bool bIsScriptExecUrl(const char* szUrl)
{
    if (szUrl == nullptr || szUrl[0] == '\0')
    {
        return false;
    }

    if (strlen(szUrl) >= sizeof(g_nodeConfig.szScriptUrl))
    {
        return false;
    }

    if (strncmp(szUrl, "https://script.google.com/", 26) != 0)
    {
        return false;
    }

    if (strstr(szUrl, "/exec") == nullptr)
    {
        return false;
    }

    if (strchr(szUrl, ' ') != nullptr)
    {
        return false;
    }

    return true;
}

/**
 * Penampung tetap untuk HTTPClient::writeToStream().
 * Google Apps Script sering membalas Transfer-Encoding: chunked.
 * Membaca WiFiClient mentah ikut menyalin ukuran chunk, jadi JSON gagal.
 * writeToStream() yang mengisi kelas ini sudah membuang bingkai chunk.
 */
class FixedBufferStream : public Stream
{
public:
    FixedBufferStream(char* szBuffer, size_t uCapacity)
    {
        _szBuffer = szBuffer;
        _uCapacity = uCapacity;
        _uLength = 0;
        if ((szBuffer != nullptr) && (uCapacity > 0))
        {
            szBuffer[0] = '\0';
        }
    }

    size_t write(uint8_t u8Byte) override
    {
        // HTTPClient membatalkan unduhan bila write() mengembalikan 0.
        // Byte yang melebihi buffer dibuang, tetapi tetap dihitung berhasil.
        if ((_szBuffer != nullptr) && ((_uLength + 1) < _uCapacity))
        {
            _szBuffer[_uLength] = (char)u8Byte;
            _uLength++;
            _szBuffer[_uLength] = '\0';
        }

        return 1;
    }

    int available() override
    {
        return 0;
    }

    int read() override
    {
        return -1;
    }

    int peek() override
    {
        return -1;
    }

    size_t uLength() const
    {
        return _uLength;
    }

private:
    char* _szBuffer;
    size_t _uCapacity;
    size_t _uLength;
};

/**
 * Mencetak awal balasan agar Serial Monitor menunjukkan HTML atau JSON.
 *
 * Input: szBody - teks yang sudah diterima.
 * Output: Tidak ada.
 */
static void vLogReplyPrefix(const char* szBody)
{
    char szPrefix[72];
    size_t uIndex = 0;

    if (szBody == nullptr)
    {
        Serial.println("[WARNING] Isi balasan kosong.");
        return;
    }

    while ((szBody[uIndex] != '\0') && (uIndex < (sizeof(szPrefix) - 1)))
    {
        char cByte = szBody[uIndex];
        if ((cByte < 32) || (cByte > 126))
        {
            cByte = '.';
        }
        szPrefix[uIndex] = cByte;
        uIndex++;
    }

    szPrefix[uIndex] = '\0';
    Serial.printf("[WARNING] Awal balasan: %s\n", szPrefix);
}

/**
 * Mengambil satu URL tanpa mengikuti redirect di dalam HTTPClient.
 * Header Location disalin ke szLocation bila buffer itu diberikan.
 *
 * Input:
 * szUrl         - URL lengkap.
 * szBody        - penampung isi balasan.
 * uBodySize     - ukuran szBody.
 * szLocation    - penampung header Location. Boleh null.
 * uLocationSize - ukuran szLocation.
 *
 * Output: kode HTTP, atau -1 bila koneksi gagal.
 * Side effect: Membuka koneksi HTTPS.
 */
static int iFetchUrlBody(
    const char* szUrl,
    char* szBody,
    size_t uBodySize,
    char* szLocation,
    size_t uLocationSize
)
{
    if ((szUrl == nullptr) || (szBody == nullptr) || (uBodySize < 2))
    {
        return -1;
    }

    szBody[0] = '\0';
    if ((szLocation != nullptr) && (uLocationSize > 0))
    {
        szLocation[0] = '\0';
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    // Redirect Google pindah host. HTTPClient yang meneruskan redirect
    // pada koneksi yang sama sering menerima halaman HTML, bukan JSON.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(HTTPS_TIMEOUT_MS);

    vFeedWatchdog();

    if (!http.begin(client, szUrl))
    {
        Serial.println("[ERROR] HTTPClient gagal diinisialisasi.");
        return -1;
    }

    int iHttpCode = http.GET();
    vFeedWatchdog();

    if (iHttpCode > 0)
    {
        FixedBufferStream bodyStream(szBody, uBodySize);
        int iWrittenBody = http.writeToStream(&bodyStream);
        if (iWrittenBody < 0)
        {
            Serial.printf(
                "[WARNING] Baca isi HTTP %d gagal. Kode %d.\n",
                iHttpCode,
                iWrittenBody
            );
        }
    }

    if ((szLocation != nullptr) && (uLocationSize > 1) && (http.getLocation().length() > 0))
    {
        // getLocation() milik library mengembalikan String.
        // Disalin segera ke buffer tetap.
        snprintf(
            szLocation,
            uLocationSize,
            "%s",
            http.getLocation().c_str()
        );
    }

    http.end();
    return iHttpCode;
}

/**
 * Menyalin URL echo dari halaman HTML Google.
 * Escape \/, \u0026, \x26, dan &amp; diubah kembali.
 *
 * Input:
 * szHtml   - halaman yang diterima.
 * szOut    - tujuan URL.
 * uOutSize - ukuran szOut.
 *
 * Output: true jika URL https://script.googleusercontent.com/ ditemukan.
 */
static bool bExtractGoogleEchoUrl(const char* szHtml, char* szOut, size_t uOutSize)
{
    if ((szHtml == nullptr) || (szOut == nullptr) || (uOutSize < 16))
    {
        return false;
    }

    const char* pszHost = strstr(szHtml, "script.googleusercontent.com");
    if (pszHost == nullptr)
    {
        return false;
    }

    const char* pszStart = pszHost;
    int iBack = 0;
    while ((pszStart > szHtml) && (iBack < 24))
    {
        char cPrev = pszStart[-1];
        if ((cPrev == '"') || (cPrev == '\'') || (cPrev == ' ') || (cPrev == '='))
        {
            break;
        }
        pszStart--;
        iBack++;
    }

    size_t uOut = 0;
    const char* pszIn = pszStart;

    while ((*pszIn != '\0') && ((uOut + 1) < uOutSize))
    {
        if ((*pszIn == '"') || (*pszIn == '\'') || (*pszIn == ' ')
            || (*pszIn == '<') || (*pszIn == '>')
            || (*pszIn == '\r') || (*pszIn == '\n'))
        {
            break;
        }

        if ((pszIn[0] == '\\') && (pszIn[1] == '/'))
        {
            szOut[uOut] = '/';
            uOut++;
            pszIn += 2;
            continue;
        }

        if ((pszIn[0] == '\\') && (pszIn[1] == 'u')
            && (pszIn[2] == '0') && (pszIn[3] == '0')
            && (pszIn[4] == '2') && (pszIn[5] == '6'))
        {
            szOut[uOut] = '&';
            uOut++;
            pszIn += 6;
            continue;
        }

        if ((pszIn[0] == '\\') && (pszIn[1] == 'x')
            && (pszIn[2] == '2') && (pszIn[3] == '6'))
        {
            szOut[uOut] = '&';
            uOut++;
            pszIn += 4;
            continue;
        }

        if (strncmp(pszIn, "&amp;", 5) == 0)
        {
            szOut[uOut] = '&';
            uOut++;
            pszIn += 5;
            continue;
        }

        szOut[uOut] = *pszIn;
        uOut++;
        pszIn++;
    }

    szOut[uOut] = '\0';
    return (strstr(szOut, "https://script.googleusercontent.com/") == szOut);
}

static bool bIsFirmwareVersionText(const char* szVersion);
static void vApplyCalibrationFromSheetJson(JsonDocument& doc);
static void vApplyIntervalFromSheetJson(JsonDocument& doc);
static void vApplyTimezoneFromSheetJson(JsonDocument& doc);
static void vApplyStaggerFromSheetJson(JsonDocument& doc);

/**
 * Saat daya baru ON: minta link API, versi, interval, dan kalibrasi node ini.
 * Link/interval disimpan hanya jika berbeda. Kalibrasi ditulis ke NVS hanya jika berbeda.
 * Jika versi berbeda, OTA dijalankan.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Dapat menulis config bila URL atau interval berubah.
 */
void vSyncScriptUrlFromSheet()
{
    if (!bIsScriptExecUrl(g_nodeConfig.szScriptUrl))
    {
        Serial.println("[WARNING] Script URL lokal belum valid. Lewati cek link.");
        return;
    }

    char szRequest[320];
    const char* pszJoin = (strchr(g_nodeConfig.szScriptUrl, '?') != nullptr) ? "&" : "?";
    int iWritten = snprintf(
        szRequest,
        sizeof(szRequest),
        "%s%saction=script_url&node_id=%s",
        g_nodeConfig.szScriptUrl,
        pszJoin,
        g_nodeConfig.szNodeId
    );

    if (iWritten <= 0 || iWritten >= (int)sizeof(szRequest))
    {
        Serial.println("[ERROR] URL cek link tidak muat di buffer.");
        return;
    }

    Serial.println(
        "[INFO] Mengambil link API, versi, interval, timezone, rombongan, "
        "dan kalibrasi dari Sheet..."
    );

    // Buffer lebih besar: link + versi + objek kalibrasi satu node.
    static char szBody[4096];
    static char szLocation[1536];

    int iHttpCode = iFetchUrlBody(
        szRequest,
        szBody,
        sizeof(szBody),
        szLocation,
        sizeof(szLocation)
    );

    if (iHttpCode < 0)
    {
        Serial.println("[WARNING] Cek link API gagal. URL lama dipertahankan.");
        return;
    }

    // 302 Google: ambil Location, lalu koneksi baru ke script.googleusercontent.com.
    if (((iHttpCode == HTTP_CODE_FOUND)
            || (iHttpCode == HTTP_CODE_MOVED_PERMANENTLY)
            || (iHttpCode == HTTP_CODE_SEE_OTHER)
            || (iHttpCode == HTTP_CODE_TEMPORARY_REDIRECT))
        && (szLocation[0] != '\0'))
    {
        Serial.println("[INFO] Mengikuti redirect Google.");
        iHttpCode = iFetchUrlBody(
            szLocation,
            szBody,
            sizeof(szBody),
            nullptr,
            0
        );
    }

    const char* pszJson = szBody;
    while ((*pszJson == ' ') || (*pszJson == '\r') || (*pszJson == '\n') || (*pszJson == '\t'))
    {
        pszJson++;
    }

    // Browser menjalankan JavaScript di halaman ini. Node menyalin URL-nya lalu GET lagi.
    if ((pszJson[0] == '<') && bExtractGoogleEchoUrl(pszJson, szLocation, sizeof(szLocation)))
    {
        Serial.println("[INFO] Mengikuti tautan di halaman HTML Google.");
        iHttpCode = iFetchUrlBody(
            szLocation,
            szBody,
            sizeof(szBody),
            nullptr,
            0
        );
        pszJson = szBody;
        while ((*pszJson == ' ') || (*pszJson == '\r') || (*pszJson == '\n') || (*pszJson == '\t'))
        {
            pszJson++;
        }
    }

    if (iHttpCode != HTTP_CODE_OK)
    {
        Serial.printf(
            "[WARNING] Cek link API gagal. HTTP %d. URL lama dipertahankan.\n",
            iHttpCode
        );
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, pszJson);
    if (err)
    {
        Serial.printf(
            "[WARNING] Balasan link API bukan JSON (%s). URL lama dipertahankan.\n",
            err.c_str()
        );
        vLogReplyPrefix(pszJson);
        if (pszJson[0] == '<')
        {
            Serial.println(
                "[WARNING] Balasan masih halaman HTML Google. "
                "URL lama dipertahankan."
            );
        }
        return;
    }

    const char* pszNewUrl = doc["script_url"] | "";
    if (!bIsScriptExecUrl(pszNewUrl))
    {
        Serial.println("[WARNING] Link dari Sheet tidak valid. URL lama dipertahankan.");
    }
    else if (strcmp(pszNewUrl, g_nodeConfig.szScriptUrl) == 0)
    {
        Serial.println("[INFO] Link API sama dengan yang tersimpan. Tidak ditulis ulang.");
    }
    else
    {
        Serial.println("[INFO] Link API berbeda. Menyimpan URL baru.");
        Serial.printf("[INFO] Lama: %s\n", g_nodeConfig.szScriptUrl);
        Serial.printf("[INFO] Baru: %s\n", pszNewUrl);

        snprintf(
            g_nodeConfig.szScriptUrl,
            sizeof(g_nodeConfig.szScriptUrl),
            "%s",
            pszNewUrl
        );

        if (!bSaveNodeConfig())
        {
            Serial.println("[ERROR] URL baru gagal ditulis ke LittleFS.");
        }
    }

    // Interval, timezone, dan stagger dulu sebelum OTA,
    // supaya tersimpan meski OTA restart.
    vApplyIntervalFromSheetJson(doc);
    vApplyTimezoneFromSheetJson(doc);
    vApplyStaggerFromSheetJson(doc);

    const char* pszSheetVersion = doc["firmware_version"] | "";
    if (!bIsFirmwareVersionText(pszSheetVersion))
    {
        Serial.println("[WARNING] Versi dari Sheet tidak valid. OTA dilewati.");
    }
    else if (strcmp(pszSheetVersion, FIRMWARE_VERSION) == 0)
    {
        Serial.printf("[INFO] Versi sama (%s). OTA tidak dijalankan.\n", FIRMWARE_VERSION);
    }
    else
    {
        Serial.printf(
            "[INFO] Versi Sheet %s, firmware alat %s.\n",
            pszSheetVersion,
            FIRMWARE_VERSION
        );
        vUpdateFirmware();
        // Jika OTA berhasil, ESP.restart() dipanggil di dalam vUpdateFirmware().
        // Jika gagal, lanjutkan menerapkan kalibrasi dari balasan yang sama.
    }

    vApplyCalibrationFromSheetJson(doc);
}

/**
 * Menerapkan interval_s dari Sheet ke config node.
 * Hanya ditulis jika valid dan berbeda dari nilai lokal.
 * Dipakai saat cold power saja (pemanggil: vSyncScriptUrlFromSheet).
 *
 * Input: doc - JSON hasil GET script_url.
 * Output: Tidak ada.
 * Side effect: Dapat mengubah g_nodeConfig.u32IntervalS dan menulis LittleFS.
 */
static void vApplyIntervalFromSheetJson(JsonDocument& doc)
{
    if (doc["interval_s"].isNull())
    {
        Serial.println("[INFO] Interval dari Sheet tidak ada. Config lokal dibiarkan.");
        return;
    }

    long i32Interval = doc["interval_s"].as<long>();
    if (i32Interval < (long)INTERVAL_MIN_S || i32Interval > (long)INTERVAL_MAX_S)
    {
        Serial.printf(
            "[WARNING] Interval dari Sheet di luar rentang (%ld). Config lokal dibiarkan.\n",
            i32Interval
        );
        return;
    }

    uint32_t u32NewInterval = (uint32_t)i32Interval;

    if (u32NewInterval == g_nodeConfig.u32IntervalS)
    {
        Serial.printf(
            "[INFO] Interval sama (%u s). Tidak ditulis ulang.\n",
            (unsigned)u32NewInterval
        );
        return;
    }

    Serial.printf(
        "[INFO] Interval berbeda. Lama=%u s, baru=%u s. Menyimpan ke config.\n",
        (unsigned)g_nodeConfig.u32IntervalS,
        (unsigned)u32NewInterval
    );

    g_nodeConfig.u32IntervalS = u32NewInterval;

    if (!bSaveNodeConfig())
    {
        Serial.println("[ERROR] Interval baru gagal ditulis ke LittleFS.");
    }
}

/**
 * Menerapkan timezone dari Sheet ke config node.
 * Hanya WIB/WITA/WIT (+07/+08/+09). Ditulis jika berbeda dari lokal.
 * Dipakai saat cold power saja (pemanggil: vSyncScriptUrlFromSheet).
 *
 * Input: doc - JSON hasil GET script_url.
 * Output: Tidak ada.
 * Side effect: Dapat mengubah g_nodeConfig.szTimezone, LittleFS, dan offset NTP.
 */
static void vApplyTimezoneFromSheetJson(JsonDocument& doc)
{
    if (doc["timezone"].isNull())
    {
        Serial.println("[INFO] Timezone dari Sheet tidak ada. Config lokal dibiarkan.");
        return;
    }

    const char* pszTz = doc["timezone"] | "";
    if (!bIsTimezoneOffsetValid(pszTz))
    {
        Serial.printf(
            "[WARNING] Timezone dari Sheet tidak valid (%s). Config lokal dibiarkan.\n",
            pszTz
        );
        return;
    }

    if (strcmp(pszTz, g_nodeConfig.szTimezone) == 0)
    {
        Serial.printf(
            "[INFO] Timezone sama (%s). Tidak ditulis ulang.\n",
            pszTz
        );
        return;
    }

    Serial.printf(
        "[INFO] Timezone berbeda. Lama=%s, baru=%s. Menyimpan ke config.\n",
        g_nodeConfig.szTimezone,
        pszTz
    );

    snprintf(
        g_nodeConfig.szTimezone,
        sizeof(g_nodeConfig.szTimezone),
        "%s",
        pszTz
    );

    if (!bSaveNodeConfig())
    {
        Serial.println("[ERROR] Timezone baru gagal ditulis ke LittleFS.");
        return;
    }

    // Segarkan offset getLocalTime agar log lokal ikut zona baru.
    vApplyNtpTimezoneOffset();
}

/**
 * Menerapkan jumlah alat per rombongan dan jeda antar rombongan dari Sheet.
 * Field: stagger_group_size (1..25), stagger_step_s (1..120 detik).
 * Masing-masing field dicek sendiri: kosong / di luar rentang -> nilai
 * lokal dibiarkan. config.json hanya ditulis jika ada yang berbeda.
 * Dipakai saat cold power saja (pemanggil: vSyncScriptUrlFromSheet).
 *
 * Input: doc - JSON hasil GET script_url.
 * Output: Tidak ada.
 * Side effect: Dapat mengubah g_nodeConfig.u32StaggerGroupSize /
 *              u32StaggerStepS dan menulis LittleFS.
 */
static void vApplyStaggerFromSheetJson(JsonDocument& doc)
{
    uint32_t u32NewGroupSize = g_nodeConfig.u32StaggerGroupSize;
    uint32_t u32NewStepS     = g_nodeConfig.u32StaggerStepS;

    if (doc["stagger_group_size"].isNull())
    {
        Serial.println(
            "[INFO] Jumlah rombongan dari Sheet tidak ada "
            "(Apps Script lama?). Config lokal dibiarkan."
        );
    }
    else
    {
        long i32GroupSize = doc["stagger_group_size"].as<long>();
        if (bIsStaggerGroupSizeValid(i32GroupSize))
        {
            u32NewGroupSize = (uint32_t)i32GroupSize;
        }
        else
        {
            Serial.printf(
                "[WARNING] Jumlah rombongan dari Sheet di luar rentang (%ld). "
                "Config lokal dibiarkan.\n",
                i32GroupSize
            );
        }
    }

    if (doc["stagger_step_s"].isNull())
    {
        Serial.println(
            "[INFO] Jeda rombongan dari Sheet tidak ada "
            "(Apps Script lama?). Config lokal dibiarkan."
        );
    }
    else
    {
        long i32StepS = doc["stagger_step_s"].as<long>();
        if (bIsStaggerStepValid(i32StepS))
        {
            u32NewStepS = (uint32_t)i32StepS;
        }
        else
        {
            Serial.printf(
                "[WARNING] Jeda rombongan dari Sheet di luar rentang (%ld). "
                "Config lokal dibiarkan.\n",
                i32StepS
            );
        }
    }

    if (u32NewGroupSize == g_nodeConfig.u32StaggerGroupSize
        && u32NewStepS == g_nodeConfig.u32StaggerStepS)
    {
        Serial.printf(
            "[INFO] Rombongan sama (%u alat, jeda %u s). Tidak ditulis ulang.\n",
            (unsigned)u32NewGroupSize,
            (unsigned)u32NewStepS
        );
        return;
    }

    Serial.printf(
        "[INFO] Rombongan berbeda. Lama=%u alat / %u s, baru=%u alat / %u s. "
        "Menyimpan ke config.\n",
        (unsigned)g_nodeConfig.u32StaggerGroupSize,
        (unsigned)g_nodeConfig.u32StaggerStepS,
        (unsigned)u32NewGroupSize,
        (unsigned)u32NewStepS
    );

    g_nodeConfig.u32StaggerGroupSize = u32NewGroupSize;
    g_nodeConfig.u32StaggerStepS     = u32NewStepS;

    if (!bSaveNodeConfig())
    {
        Serial.println("[ERROR] Rombongan/jeda baru gagal ditulis ke LittleFS.");
        return;
    }

    Serial.printf(
        "[INFO] Jadwal kirim baru: stagger node ini +%u s.\n",
        (unsigned)u32GetNodeStaggerS()
    );
}

/**
 * Menerapkan objek calibration dari Sheet ke NVS.
 * Node A hanya lux + baterai. Node B hanya temp/RH/CO2 + baterai.
 * Jika objek tidak ada, NVS dibiarkan.
 *
 * Input: doc - JSON hasil GET script_url.
 * Output: Tidak ada.
 */
static void vApplyCalibrationFromSheetJson(JsonDocument& doc)
{
    JsonObjectConst calib = doc["calibration"].as<JsonObjectConst>();
    if (calib.isNull())
    {
        Serial.println("[INFO] Kalibrasi dari Sheet tidak ada. NVS dibiarkan.");
        return;
    }

    CalibrationSet tNext = g_calibration;
    bool bChanged = false;

    if (bIsNodeTypeA())
    {
        if (calib["lux_m"].isNull() || calib["lux_c"].isNull()
            || calib["bat_m"].isNull() || calib["bat_c"].isNull())
        {
            Serial.println("[WARNING] Kalibrasi Node A dari Sheet tidak lengkap. NVS dibiarkan.");
            return;
        }

        float fLuxM = calib["lux_m"] | g_calibration.fLuxM;
        float fLuxC = calib["lux_c"] | g_calibration.fLuxC;
        float fBatM = calib["bat_m"] | g_calibration.fBatteryM;
        float fBatC = calib["bat_c"] | g_calibration.fBatteryC;

        if ((fLuxM != g_calibration.fLuxM) || (fLuxC != g_calibration.fLuxC)
            || (fBatM != g_calibration.fBatteryM) || (fBatC != g_calibration.fBatteryC))
        {
            tNext.fLuxM = fLuxM;
            tNext.fLuxC = fLuxC;
            tNext.fBatteryM = fBatM;
            tNext.fBatteryC = fBatC;
            bChanged = true;
        }

        // Mode gain VEML7700 (V2.3.0). Satu nilai global dari menu Pengaturan,
        // dikirim Apps Script di dalam calibration untuk setiap Node A.
        // Nilai tidak dikenal -> autorange + log, supaya sensor tetap terbaca.
        const char* pszGainMode = calib["gain_mode"] | VEML_GAIN_MODE_DEFAULT;
        if (!bIsVemlGainModeValid(pszGainMode))
        {
            Serial.printf(
                "[WARNING] gain_mode dari Sheet tidak dikenal (%s). Pakai autorange.\n",
                pszGainMode
            );
            pszGainMode = VEML_GAIN_MODE_DEFAULT;
        }

        if (strcmp(pszGainMode, g_calibration.szVemlGainMode) != 0)
        {
            Serial.printf(
                "[INFO] Mode gain VEML berubah. Lama=%s, baru=%s. "
                "Berlaku mulai inisialisasi sensor berikutnya.\n",
                g_calibration.szVemlGainMode,
                pszGainMode
            );
            snprintf(
                tNext.szVemlGainMode,
                sizeof(tNext.szVemlGainMode),
                "%s",
                pszGainMode
            );
            bChanged = true;
        }
    }
    else
    {
        if (calib["temp_m"].isNull() || calib["temp_c"].isNull()
            || calib["rh_m"].isNull() || calib["rh_c"].isNull()
            || calib["co2_m"].isNull() || calib["co2_c"].isNull()
            || calib["bat_m"].isNull() || calib["bat_c"].isNull())
        {
            Serial.println("[WARNING] Kalibrasi Node B dari Sheet tidak lengkap. NVS dibiarkan.");
            return;
        }

        float fTempM = calib["temp_m"] | g_calibration.fTempM;
        float fTempC = calib["temp_c"] | g_calibration.fTempC;
        float fRhM = calib["rh_m"] | g_calibration.fRhM;
        float fRhC = calib["rh_c"] | g_calibration.fRhC;
        float fCo2M = calib["co2_m"] | g_calibration.fCo2M;
        float fCo2C = calib["co2_c"] | g_calibration.fCo2C;
        float fBatM = calib["bat_m"] | g_calibration.fBatteryM;
        float fBatC = calib["bat_c"] | g_calibration.fBatteryC;

        if ((fTempM != g_calibration.fTempM) || (fTempC != g_calibration.fTempC)
            || (fRhM != g_calibration.fRhM) || (fRhC != g_calibration.fRhC)
            || (fCo2M != g_calibration.fCo2M) || (fCo2C != g_calibration.fCo2C)
            || (fBatM != g_calibration.fBatteryM) || (fBatC != g_calibration.fBatteryC))
        {
            tNext.fTempM = fTempM;
            tNext.fTempC = fTempC;
            tNext.fRhM = fRhM;
            tNext.fRhC = fRhC;
            tNext.fCo2M = fCo2M;
            tNext.fCo2C = fCo2C;
            tNext.fBatteryM = fBatM;
            tNext.fBatteryC = fBatC;
            bChanged = true;
        }
    }

    if (!bChanged)
    {
        Serial.println("[INFO] Kalibrasi dari Sheet sama dengan NVS. Tidak ditulis ulang.");
        return;
    }

    g_calibration = tNext;
    if (bSaveCalibration())
    {
        Serial.println("[INFO] Kalibrasi dari Sheet tersimpan ke NVS.");
    }
}

/**
 * Menilai teks versi seperti 1.9.0.
 *
 * Input: szVersion
 * Output: true jika hanya angka dan titik, ada satu titik, panjang masuk akal.
 */
static bool bIsFirmwareVersionText(const char* szVersion)
{
    if ((szVersion == nullptr) || (szVersion[0] == '\0'))
    {
        return false;
    }

    size_t uLen = strlen(szVersion);
    if ((uLen < 3) || (uLen > 15))
    {
        return false;
    }

    bool bHasDot = false;
    for (size_t uIndex = 0; uIndex < uLen; uIndex++)
    {
        char cByte = szVersion[uIndex];
        if (cByte == '.')
        {
            bHasDot = true;
        }
        else if ((cByte < '0') || (cByte > '9'))
        {
            return false;
        }
    }

    return bHasDot;
}
