// =====================================================
// NodeWebServer.ino
// Cara yang sama dengan Gas_Monitoring_Sensor/WebServer.ino:
// nyalakan AP, daftarkan rute, kirim HTML PROGMEM lewat send_P, begin().
// =====================================================

#include "NodeWebPage.h"

static bool     g_bRestartPending = false;
static uint32_t g_u32RestartAtMs  = 0;
static bool     g_bWebStarted     = false;

#define RESTART_DELAY_MS 1500UL

/**
 * Mengambil satu field POST. Nilai hanya sah selama handler berjalan.
 *
 * Input:
 * pRequest - permintaan AsyncWebServer.
 * szName   - nama field form.
 *
 * Output: pointer teks, atau nullptr jika tidak ada.
 */
static const char* pszGetPostArg(AsyncWebServerRequest* pRequest, const char* szName)
{
    if (pRequest == nullptr || szName == nullptr)
    {
        return nullptr;
    }
    if (!pRequest->hasParam(szName, true))
    {
        return nullptr;
    }
    return pRequest->getParam(szName, true)->value().c_str();
}

/**
 * Menyalin teks ke JSON dan mengganti tanda kutip agar payload tidak pecah.
 *
 * Input:
 * szOut    - buffer tujuan.
 * uOutSize - ukuran buffer.
 * szIn     - teks sumber.
 *
 * Output: Tidak ada.
 */
static void vCopyJsonText(char* szOut, size_t uOutSize, const char* szIn)
{
    size_t uOut = 0;
    if (szOut == nullptr || uOutSize == 0)
    {
        return;
    }
    if (szIn == nullptr)
    {
        szOut[0] = '\0';
        return;
    }
    while (szIn[0] != '\0' && uOut + 1 < uOutSize)
    {
        char cChar = szIn[0];
        if (cChar == '"' || cChar == '\\')
        {
            cChar = '\'';
        }
        szOut[uOut] = cChar;
        uOut++;
        szIn++;
    }
    szOut[uOut] = '\0';
}

/**
 * Status node untuk diisi halaman (bukan HTML).
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleApiStatus(AsyncWebServerRequest* pRequest)
{
    char szFile[DATA_FILE_PATH_MAX];
    char szNode[24];
    char szSsid[68];
    char szUrl[280];
    // Isi terpanjang sekitar 1.1 KB (URL 255 karakter + kalibrasi + timeout).
    char szJson[1400];

    vBuildDataFilePath(szFile, sizeof(szFile));
    vCopyJsonText(szNode, sizeof(szNode), g_nodeConfig.szNodeId);
    vCopyJsonText(szSsid, sizeof(szSsid), g_nodeConfig.szWifiSsid);
    vCopyJsonText(szUrl, sizeof(szUrl), g_nodeConfig.szScriptUrl);

    int iLen = snprintf(
        szJson,
        sizeof(szJson),
        "{\"node_id\":\"%s\",\"firmware\":\"%s\",\"force_stay\":%s,"
        "\"remain_s\":%u,\"file\":\"%s\",\"file_size\":%u,\"file_max\":%lu,"
        "\"fs_free\":%u,\"kind\":\"%s\",\"wifi_ssid\":\"%s\","
        "\"script_url\":\"%s\",\"interval_s\":%u,\"interval_min\":%u,"
        "\"interval_max\":%u,\"password_set\":%s,\"is_node_a\":%s,"
        "\"lux_m\":%.4f,\"lux_c\":%.4f,\"temp_m\":%.4f,\"temp_c\":%.4f,"
        "\"rh_m\":%.4f,\"rh_c\":%.4f,\"co2_m\":%.4f,\"co2_c\":%.4f,"
        "\"bat_m\":%.4f,\"bat_c\":%.4f,\"veml_gain_mode\":\"%s\","
        "\"ap_timeout_s\":%u,\"ap_timeout_min\":%u,\"ap_timeout_max\":%u}",
        szNode,
        FIRMWARE_VERSION,
        bIsApForceStay() ? "true" : "false",
        (unsigned)u32GetApRemainSec(),
        szFile,
        (unsigned)uGetDataFileSize(),
        (unsigned long)DATA_FILE_MAX_BYTES,
        (unsigned)uGetLittleFsFreeBytes(),
        bIsNodeTypeA() ? "A - Iluminansi" : "B - IAQ",
        szSsid,
        szUrl,
        (unsigned)g_nodeConfig.u32IntervalS,
        (unsigned)INTERVAL_MIN_S,
        (unsigned)INTERVAL_MAX_S,
        (strlen(g_nodeConfig.szWifiPassword) > 0) ? "true" : "false",
        bIsNodeTypeA() ? "true" : "false",
        g_calibration.fLuxM, g_calibration.fLuxC,
        g_calibration.fTempM, g_calibration.fTempC,
        g_calibration.fRhM, g_calibration.fRhC,
        g_calibration.fCo2M, g_calibration.fCo2C,
        g_calibration.fBatteryM, g_calibration.fBatteryC,
        g_calibration.szVemlGainMode,
        (unsigned)g_nodeConfig.u32ApTimeoutS,
        (unsigned)AP_TIMEOUT_MIN_S,
        (unsigned)AP_TIMEOUT_MAX_S
    );

    if ((iLen < 0) || ((size_t)iLen >= sizeof(szJson)))
    {
        Serial.println("[ERROR] /api/status: buffer JSON terlalu kecil, isi terpotong.");
        pRequest->send(500, "text/plain", "Status terlalu panjang");
        return;
    }

    pRequest->send(200, "application/json", szJson);
}

/**
 * Data VEML7700 mentah realtime untuk halaman /calmLux (Node A).
 * Tidak menyentuh I2C: hanya menyalin hasil terakhir dari loop()
 * dan menandai bahwa halaman masih dibuka.
 * lux_raw dibandingkan dengan luxmeter referensi untuk mencari m dan c.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleApiLux(AsyncWebServerRequest* pRequest)
{
    SensorRealtime tSnap;
    char szJson[320];

    vRequestSensorRealtime();
    vGetSensorRealtimeSnapshot(&tSnap);

    float fLuxCal = fApplyCalibration(
        tSnap.fLuxRaw,
        g_calibration.fLuxM,
        g_calibration.fLuxC
    );
    uint32_t u32AgeMs = tSnap.bValid ? (millis() - tSnap.u32UpdatedMs) : 0;

    snprintf(
        szJson,
        sizeof(szJson),
        "{\"is_node_a\":%s,\"hw_ok\":%s,\"valid\":%s,\"last_ok\":%s,"
        "\"age_ms\":%lu,\"count\":%lu,\"als\":%u,\"lux_raw\":%.2f,"
        "\"lux_cal\":%.2f,\"gain\":\"%s\",\"gain_mode\":\"%s\","
        "\"lux_m\":%.4f,\"lux_c\":%.4f}",
        bIsNodeTypeA() ? "true" : "false",
        g_bSensorHardwareOk ? "true" : "false",
        tSnap.bValid ? "true" : "false",
        tSnap.bLastOk ? "true" : "false",
        (unsigned long)u32AgeMs,
        (unsigned long)tSnap.u32SampleCount,
        (unsigned)tSnap.u16Als,
        tSnap.fLuxRaw,
        fLuxCal,
        tSnap.bValid ? pszVemlGainLabel(tSnap.u8Gain) : "-",
        g_calibration.szVemlGainMode,
        g_calibration.fLuxM,
        g_calibration.fLuxC
    );

    pRequest->send(200, "application/json", szJson);
}

/**
 * Data SCD41 mentah realtime untuk halaman /calmEnv (Node B).
 * Sama seperti /api/lux: hanya salinan hasil loop(), tidak memblokir
 * handler selama single shot ~5 detik.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleApiEnv(AsyncWebServerRequest* pRequest)
{
    SensorRealtime tSnap;
    char szJson[400];

    vRequestSensorRealtime();
    vGetSensorRealtimeSnapshot(&tSnap);

    float fTempCal = fApplyCalibration(tSnap.fTempRaw, g_calibration.fTempM, g_calibration.fTempC);
    float fRhCal   = fApplyCalibration(tSnap.fRhRaw, g_calibration.fRhM, g_calibration.fRhC);
    float fCo2Cal  = fApplyCalibration((float)tSnap.u16Co2Raw, g_calibration.fCo2M, g_calibration.fCo2C);
    uint32_t u32AgeMs = tSnap.bValid ? (millis() - tSnap.u32UpdatedMs) : 0;

    snprintf(
        szJson,
        sizeof(szJson),
        "{\"is_node_a\":%s,\"hw_ok\":%s,\"valid\":%s,\"last_ok\":%s,"
        "\"age_ms\":%lu,\"count\":%lu,"
        "\"temp_raw\":%.2f,\"temp_cal\":%.2f,\"rh_raw\":%.2f,\"rh_cal\":%.2f,"
        "\"co2_raw\":%u,\"co2_cal\":%.1f,"
        "\"temp_m\":%.4f,\"temp_c\":%.4f,\"rh_m\":%.4f,\"rh_c\":%.4f,"
        "\"co2_m\":%.4f,\"co2_c\":%.4f}",
        bIsNodeTypeA() ? "true" : "false",
        g_bSensorHardwareOk ? "true" : "false",
        tSnap.bValid ? "true" : "false",
        tSnap.bLastOk ? "true" : "false",
        (unsigned long)u32AgeMs,
        (unsigned long)tSnap.u32SampleCount,
        tSnap.fTempRaw,
        fTempCal,
        tSnap.fRhRaw,
        fRhCal,
        (unsigned)tSnap.u16Co2Raw,
        fCo2Cal,
        g_calibration.fTempM, g_calibration.fTempC,
        g_calibration.fRhM, g_calibration.fRhC,
        g_calibration.fCo2M, g_calibration.fCo2C
    );

    pRequest->send(200, "application/json", szJson);
}

/**
 * ADC baterai realtime untuk halaman /calmA.
 * pack_raw_v dibandingkan dengan multimeter sebelum mengisi m dan c.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleApiBattery(AsyncWebServerRequest* pRequest)
{
    float fAdc = 0.0f;
    float fPinVolt = 0.0f;
    float fPackRaw = 0.0f;
    float fPackCal = 0.0f;
    char szJson[192];

    vReadBatteryAdcDetail(&fAdc, &fPinVolt, &fPackRaw, &fPackCal);

    snprintf(
        szJson,
        sizeof(szJson),
        "{\"adc\":%.1f,\"pin_v\":%.3f,\"pack_raw_v\":%.3f,"
        "\"pack_cal_v\":%.3f,\"bat_m\":%.4f,\"bat_c\":%.4f}",
        fAdc,
        fPinVolt,
        fPackRaw,
        fPackCal,
        g_calibration.fBatteryM,
        g_calibration.fBatteryC
    );

    pRequest->send(200, "application/json", szJson);
}

/**
 * Download file CSV sebagai attachment.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleDownload(AsyncWebServerRequest* pRequest)
{
    static char s_szDownloadPath[DATA_FILE_PATH_MAX];
    vBuildDataFilePath(s_szDownloadPath, sizeof(s_szDownloadPath));

    if (!LittleFS.exists(s_szDownloadPath))
    {
        char szMsg[96];
        snprintf(szMsg, sizeof(szMsg), "File %s belum ada.", s_szDownloadPath);
        pRequest->send(404, "text/plain", szMsg);
        return;
    }

    pRequest->send(
        LittleFS,
        s_szDownloadPath,
        "text/csv",
        true
    );
}

/**
 * Menghapus CSV lalu kembali ke halaman utama.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleDelete(AsyncWebServerRequest* pRequest)
{
    if (bDeleteDataCsv())
    {
        pRequest->redirect("/?msg=hapus");
    }
    else
    {
        pRequest->send(500, "text/plain", "Gagal menghapus file.");
    }
}

/**
 * Menyimpan config lalu restart.
 *
 * Input: pRequest
 * Output: Tidak ada.
 */
static void vHandleSave(AsyncWebServerRequest* pRequest)
{
    const char* pszNodeId   = pszGetPostArg(pRequest, "node_id");
    const char* pszSsid     = pszGetPostArg(pRequest, "wifi_ssid");
    const char* pszPassword = pszGetPostArg(pRequest, "wifi_password");
    const char* pszUrl      = pszGetPostArg(pRequest, "script_url");
    const char* pszInterval = pszGetPostArg(pRequest, "interval_s");
    const char* pszApTimeout = pszGetPostArg(pRequest, "ap_timeout_s");

    if (pszNodeId != nullptr && pszNodeId[0] != '\0')
    {
        snprintf(g_nodeConfig.szNodeId, sizeof(g_nodeConfig.szNodeId), "%s", pszNodeId);
    }
    if (pszSsid != nullptr)
    {
        snprintf(g_nodeConfig.szWifiSsid, sizeof(g_nodeConfig.szWifiSsid), "%s", pszSsid);
    }
    if (pszPassword != nullptr && pszPassword[0] != '\0')
    {
        snprintf(
            g_nodeConfig.szWifiPassword,
            sizeof(g_nodeConfig.szWifiPassword),
            "%s",
            pszPassword
        );
    }
    if (pszUrl != nullptr && pszUrl[0] != '\0')
    {
        snprintf(g_nodeConfig.szScriptUrl, sizeof(g_nodeConfig.szScriptUrl), "%s", pszUrl);
    }
    if (pszInterval != nullptr && pszInterval[0] != '\0')
    {
        uint32_t u32Val = (uint32_t)atoi(pszInterval);
        if (u32Val < INTERVAL_MIN_S) { u32Val = INTERVAL_MIN_S; }
        if (u32Val > INTERVAL_MAX_S) { u32Val = INTERVAL_MAX_S; }
        g_nodeConfig.u32IntervalS = u32Val;
    }
    if (pszApTimeout != nullptr && pszApTimeout[0] != '\0')
    {
        // Dibatasi ke rentang AP_TIMEOUT_MIN_S..AP_TIMEOUT_MAX_S.
        uint32_t u32Val = (uint32_t)atoi(pszApTimeout);
        if (u32Val < AP_TIMEOUT_MIN_S) { u32Val = AP_TIMEOUT_MIN_S; }
        if (u32Val > AP_TIMEOUT_MAX_S) { u32Val = AP_TIMEOUT_MAX_S; }
        g_nodeConfig.u32ApTimeoutS = u32Val;
    }

    if (!bSaveNodeConfig())
    {
        pRequest->send(500, "text/plain", "Gagal simpan config");
        return;
    }

    static const size_t uSavedLen = strlen_P(SAVED_HTML);
    pRequest->send_P(200, "text/html", (uint8_t*)SAVED_HTML, uSavedLen);
    g_bRestartPending = true;
    g_u32RestartAtMs  = millis();
}

/**
 * Menyalakan AP lalu AsyncWebServer.
 * Urutan sama dengan setupWebServer() di Gas_Monitoring_Sensor:
 * softAP, jeda singkat, daftar rute, begin().
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitWebServer()
{
    if (g_bWebStarted)
    {
        return;
    }

    vStartApMode();
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print("[INFO] AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Cast ke uint8_t* wajib.
        // Tanpa itu const char* disalin ke String sementara.
        static const size_t htmlContentLength = strlen_P(INDEX_HTML);
        request->send_P(200, "text/html", (uint8_t*)INDEX_HTML, htmlContentLength);
    });

    server.on("/api/status", HTTP_GET, vHandleApiStatus);
    server.on("/api/battery", HTTP_GET, vHandleApiBattery);
    server.on("/calmA", HTTP_GET, [](AsyncWebServerRequest* request) {
        static const size_t htmlContentLength = strlen_P(CALM_A_HTML);
        request->send_P(200, "text/html", (uint8_t*)CALM_A_HTML, htmlContentLength);
    });
    // Baca sensor realtime untuk data kalibrasi (V2.3.0).
    server.on("/api/lux", HTTP_GET, vHandleApiLux);
    server.on("/api/env", HTTP_GET, vHandleApiEnv);
    server.on("/calmLux", HTTP_GET, [](AsyncWebServerRequest* request) {
        static const size_t htmlContentLength = strlen_P(CALM_LUX_HTML);
        request->send_P(200, "text/html", (uint8_t*)CALM_LUX_HTML, htmlContentLength);
    });
    server.on("/calmEnv", HTTP_GET, [](AsyncWebServerRequest* request) {
        static const size_t htmlContentLength = strlen_P(CALM_ENV_HTML);
        request->send_P(200, "text/html", (uint8_t*)CALM_ENV_HTML, htmlContentLength);
    });
    server.on("/download", HTTP_GET, vHandleDownload);
    server.on("/delete", HTTP_POST, vHandleDelete);
    server.on("/save", HTTP_POST, vHandleSave);
    // /save-calib dihapus di V2.3.0: kalibrasi hanya diubah dari HMI Spreadsheet.
    // POST lama ke rute ini akan jatuh ke onNotFound (404).
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        char szMessage[256];
        const char* pszMethod = (request->method() == HTTP_GET) ? "GET" : "POST";

        snprintf(
            szMessage,
            sizeof(szMessage),
            "404 - File Not Found\n\nURI: %s\nMethod: %s\n",
            request->url().c_str(),
            pszMethod
        );

        request->send(404, "text/plain", szMessage);
    });

    server.begin();
    g_bWebStarted = true;
    Serial.println("[INFO] AsyncWebServer aktif.");
}

/**
 * Jadwal restart setelah simpan config.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vHandleWebServer()
{
    if (!g_bRestartPending)
    {
        return;
    }

    if (millis() - g_u32RestartAtMs >= RESTART_DELAY_MS)
    {
        Serial.println("[INFO] Restart sekarang...");
        ESP.restart();
    }
}
