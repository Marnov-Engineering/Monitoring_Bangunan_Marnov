// =====================================================
// Storage.ino
// LittleFS CSV per node: /data-<NodeID>.csv
// =====================================================

static const char* CSV_HEADER_A =
    "node_id,firmware_version,timestamp,timezone,"
    "illuminance_lux,illuminance_valid,sensor_status,battery_voltage\n";

static const char* CSV_HEADER_B =
    "node_id,firmware_version,timestamp,timezone,"
    "air_temperature_c,air_temperature_valid,"
    "relative_humidity_pct,relative_humidity_valid,"
    "co2_ppm,co2_valid,sensor_status,battery_voltage\n";

/**
 * Menyusun path file data sesuai Node ID.
 * Contoh: Node B-02 -> "/data-B-02.csv"
 *
 * Karakter tidak aman untuk nama file diganti '_'.
 *
 * Input:
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer (minimal DATA_FILE_PATH_MAX).
 *
 * Output: Tidak ada. Buffer diisi path lengkap.
 */
void vBuildDataFilePath(char* szBuffer, size_t uBufferSize)
{
    if (szBuffer == nullptr || uBufferSize == 0)
    {
        return;
    }

    char szSafeId[16];
    size_t uOut = 0;

    const char* pszId = g_nodeConfig.szNodeId;
    if (pszId == nullptr || pszId[0] == '\0')
    {
        pszId = DEFAULT_NODE_ID;
    }

    for (size_t i = 0; pszId[i] != '\0' && uOut < (sizeof(szSafeId) - 1); i++)
    {
        char c = pszId[i];
        bool bOk =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            (c == '-') ||
            (c == '_');

        szSafeId[uOut++] = bOk ? c : '_';
    }
    szSafeId[uOut] = '\0';

    if (szSafeId[0] == '\0')
    {
        snprintf(szSafeId, sizeof(szSafeId), "UNKNOWN");
    }

    snprintf(szBuffer, uBufferSize, "/data-%s.csv", szSafeId);
}

/**
 * Migrasi file lama "/data.csv" ke "/data-<NodeID>.csv" jika perlu.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
static void vMigrateLegacyDataFileIfNeeded()
{
    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    if (LittleFS.exists(szPath))
    {
        return;
    }

    if (!LittleFS.exists(DATA_FILE_LEGACY_PATH))
    {
        return;
    }

    if (LittleFS.rename(DATA_FILE_LEGACY_PATH, szPath))
    {
        Serial.printf(
            "[INFO] Migrasi %s -> %s\n",
            DATA_FILE_LEGACY_PATH,
            szPath
        );
    }
    else
    {
        Serial.println("[WARNING] Migrasi /data.csv gagal.");
    }
}

/**
 * Memastikan file data ada dengan header yang sesuai jenis node.
 *
 * Input: Tidak ada.
 * Output: true jika file siap.
 */
static bool bEnsureDataFileHeader()
{
    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    if (LittleFS.exists(szPath))
    {
        return true;
    }

    File file = LittleFS.open(szPath, "w");
    if (!file)
    {
        Serial.printf("[ERROR] Gagal membuat %s.\n", szPath);
        return false;
    }

    const char* pszHeader = bIsNodeTypeA() ? CSV_HEADER_A : CSV_HEADER_B;
    file.print(pszHeader);
    file.flush();
    file.close();

    Serial.printf("[INFO] %s dibuat dengan header.\n", szPath);
    return true;
}

/**
 * Menginisialisasi storage data CSV per Node ID.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitStorage()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("[ERROR] LittleFS gagal di Storage.");
        return;
    }

    vMigrateLegacyDataFileIfNeeded();
    bEnsureDataFileHeader();

    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    Serial.printf(
        "[INFO] Data CSV file=%s size=%u bytes, free=%u bytes, max=%lu\n",
        szPath,
        (unsigned)uGetDataFileSize(),
        (unsigned)uGetLittleFsFreeBytes(),
        (unsigned long)DATA_FILE_MAX_BYTES
    );
}

/**
 * Apakah file data node ini ada.
 *
 * Input: Tidak ada.
 * Output: true jika ada.
 */
bool bDataFileExists()
{
    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));
    return LittleFS.exists(szPath);
}

/**
 * Ukuran file data CSV node ini.
 *
 * Input: Tidak ada.
 * Output: ukuran byte.
 */
size_t uGetDataFileSize()
{
    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    if (!LittleFS.exists(szPath))
    {
        return 0;
    }

    File file = LittleFS.open(szPath, "r");
    if (!file)
    {
        return 0;
    }

    size_t uSize = file.size();
    file.close();
    return uSize;
}

/**
 * Sisa ruang LittleFS.
 *
 * Input: Tidak ada.
 * Output: byte bebas.
 */
size_t uGetLittleFsFreeBytes()
{
    size_t uTotal = LittleFS.totalBytes();
    size_t uUsed  = LittleFS.usedBytes();
    if (uTotal <= uUsed)
    {
        return 0;
    }
    return uTotal - uUsed;
}

/**
 * Append satu baris ke CSV lalu flush.
 *
 * Input:
 * szLine - baris lengkap termasuk newline.
 *
 * Output: true jika sukses.
 */
static bool bAppendRawLine(const char* szLine)
{
    if (szLine == nullptr)
    {
        return false;
    }

    if (!bEnsureDataFileHeader())
    {
        return false;
    }

    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    File file = LittleFS.open(szPath, "a");
    if (!file)
    {
        Serial.printf("[ERROR] Gagal append %s.\n", szPath);
        return false;
    }

    size_t uWritten = file.print(szLine);
    file.flush();
    file.close();

    if (uWritten == 0)
    {
        Serial.println("[ERROR] Append CSV menulis 0 byte.");
        return false;
    }

    Serial.printf("[INFO] CSV append OK (%u byte baris).\n", (unsigned)uWritten);
    return true;
}

/**
 * Menulis rekaman Node A ke CSV.
 *
 * Input:
 * pData       - data iluminansi.
 * szTimestamp - timestamp ISO 8601.
 *
 * Output: true jika sukses.
 */
bool bAppendNodeACsv(const NodeAData* pData, const char* szTimestamp)
{
    if (pData == nullptr || szTimestamp == nullptr)
    {
        return false;
    }

    char szLux[16];
    szLux[0] = '\0';
    if (pData->u8IlluminanceValid != 0)
    {
        snprintf(
            szLux,
            sizeof(szLux),
            "%.1f",
            roundf(pData->fIlluminanceLux * 10.0f) / 10.0f
        );
    }

    char szLine[256];
    snprintf(
        szLine,
        sizeof(szLine),
        "%s,%s,%s,%s,%s,%u,%u,%.2f\n",
        g_nodeConfig.szNodeId,
        FIRMWARE_VERSION,
        szTimestamp,
        g_nodeConfig.szTimezone,
        szLux,
        (unsigned)pData->u8IlluminanceValid,
        (unsigned)pData->u8SensorStatus,
        roundf(pData->fBatteryVoltage * 100.0f) / 100.0f
    );

    return bAppendRawLine(szLine);
}

/**
 * Menulis rekaman Node B ke CSV.
 *
 * Input:
 * pData       - data IAQ.
 * szTimestamp - timestamp ISO 8601.
 *
 * Output: true jika sukses.
 */
bool bAppendNodeBCsv(const NodeBData* pData, const char* szTimestamp)
{
    if (pData == nullptr || szTimestamp == nullptr)
    {
        return false;
    }

    char szTemp[16];
    char szRh[16];
    char szCo2[16];
    szTemp[0] = '\0';
    szRh[0] = '\0';
    szCo2[0] = '\0';

    if (pData->u8TempValid != 0)
    {
        snprintf(
            szTemp,
            sizeof(szTemp),
            "%.1f",
            roundf(pData->fAirTemperatureC * 10.0f) / 10.0f
        );
    }
    if (pData->u8RhValid != 0)
    {
        snprintf(
            szRh,
            sizeof(szRh),
            "%.1f",
            roundf(pData->fRelativeHumidityPct * 10.0f) / 10.0f
        );
    }
    if (pData->u8Co2Valid != 0)
    {
        snprintf(
            szCo2,
            sizeof(szCo2),
            "%.0f",
            roundf(pData->fCo2Ppm)
        );
    }

    char szLine[320];
    snprintf(
        szLine,
        sizeof(szLine),
        "%s,%s,%s,%s,%s,%u,%s,%u,%s,%u,%u,%.2f\n",
        g_nodeConfig.szNodeId,
        FIRMWARE_VERSION,
        szTimestamp,
        g_nodeConfig.szTimezone,
        szTemp,
        (unsigned)pData->u8TempValid,
        szRh,
        (unsigned)pData->u8RhValid,
        szCo2,
        (unsigned)pData->u8Co2Valid,
        (unsigned)pData->u8SensorStatus,
        roundf(pData->fBatteryVoltage * 100.0f) / 100.0f
    );

    return bAppendRawLine(szLine);
}

/**
 * Jika file melebihi batas, hapus baris data terlama sampai muat.
 * Header baris 1 selalu dipertahankan.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vTrimDataFileIfNeeded()
{
    size_t uSize = uGetDataFileSize();
    if (uSize <= DATA_FILE_MAX_BYTES)
    {
        return;
    }

    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    Serial.printf(
        "[WARNING] CSV %s %u byte > max %lu. Trim baris terlama...\n",
        szPath,
        (unsigned)uSize,
        (unsigned long)DATA_FILE_MAX_BYTES
    );

    File src = LittleFS.open(szPath, "r");
    if (!src)
    {
        Serial.println("[ERROR] Trim: gagal buka sumber.");
        return;
    }

    char szHeader[256];
    size_t uHeaderLen = src.readBytesUntil('\n', szHeader, sizeof(szHeader) - 2);
    szHeader[uHeaderLen] = '\0';
    if (uHeaderLen > 0 && szHeader[uHeaderLen - 1] == '\r')
    {
        szHeader[uHeaderLen - 1] = '\0';
    }

    size_t uNeedDrop = (uSize - DATA_FILE_MAX_BYTES) + 200UL;

    size_t uDropped = 0;
    char szSkip[320];
    while (uDropped < uNeedDrop && src.available())
    {
        size_t uLineLen = src.readBytesUntil('\n', szSkip, sizeof(szSkip) - 1);
        uDropped += (uLineLen + 1);
    }

    File dst = LittleFS.open(DATA_FILE_TMP_PATH, "w");
    if (!dst)
    {
        src.close();
        Serial.println("[ERROR] Trim: gagal buka tmp.");
        return;
    }

    dst.print(szHeader);
    dst.print("\n");

    while (src.available())
    {
        size_t uLineLen = src.readBytesUntil('\n', szSkip, sizeof(szSkip) - 1);
        szSkip[uLineLen] = '\0';
        if (uLineLen > 0 && szSkip[uLineLen - 1] == '\r')
        {
            szSkip[uLineLen - 1] = '\0';
        }
        if (szSkip[0] == '\0')
        {
            continue;
        }
        dst.print(szSkip);
        dst.print("\n");
    }

    src.close();
    dst.flush();
    dst.close();

    LittleFS.remove(szPath);
    LittleFS.rename(DATA_FILE_TMP_PATH, szPath);

    Serial.printf(
        "[INFO] Trim selesai. Ukuran baru=%u bytes.\n",
        (unsigned)uGetDataFileSize()
    );
}

/**
 * Menghapus file data CSV node ini dan membersihkan marker backfill.
 *
 * Input: Tidak ada.
 * Output: true jika file tidak ada atau berhasil dihapus.
 */
bool bDeleteDataCsv()
{
    bool bOk = true;
    char szPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(szPath, sizeof(szPath));

    if (LittleFS.exists(szPath))
    {
        bOk = LittleFS.remove(szPath);
    }

    if (LittleFS.exists(DATA_FILE_TMP_PATH))
    {
        LittleFS.remove(DATA_FILE_TMP_PATH);
    }

    // Hapus juga sisa file lama jika masih ada.
    if (LittleFS.exists(DATA_FILE_LEGACY_PATH))
    {
        LittleFS.remove(DATA_FILE_LEGACY_PATH);
    }

    vClearLastSentTimestamp();

    if (bOk)
    {
        Serial.printf("[INFO] %s dihapus.\n", szPath);
        bEnsureDataFileHeader();
    }
    else
    {
        Serial.printf("[ERROR] Gagal hapus %s.\n", szPath);
    }

    return bOk;
}
