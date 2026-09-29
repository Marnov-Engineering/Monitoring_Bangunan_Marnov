// =====================================================
// WiFiMgr.ino
// STA, AP mode, disconnect, RSSI, IP tanpa String
// =====================================================

// =====================================================
// FUNCTIONS
// =====================================================

/**
 * Menginisialisasi modul WiFi: set mode STA dan disconnect
 * dari koneksi sebelumnya agar mulai dari keadaan bersih.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Mengubah mode WiFi ESP32.
 */
void vInitWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(100);
    Serial.println("[INFO] WiFi modul diinisialisasi.");
}

/**
 * Mencoba terhubung ke WiFi station (router) dengan timeout.
 * Menampilkan titik-titik di Serial selama menunggu.
 *
 * Input:
 * u32TimeoutMs - batas waktu koneksi dalam milidetik.
 *
 * Output: true jika berhasil terhubung sebelum timeout.
 * Side effect: Mengubah status WiFi ESP32.
 */
bool bConnectWifiStation(uint32_t u32TimeoutMs)
{
    Serial.printf(
        "[INFO] Menghubungkan ke WiFi: \"%s\"...\n",
        g_nodeConfig.szWifiSsid
    );

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_nodeConfig.szWifiSsid, g_nodeConfig.szWifiPassword);

    uint32_t u32StartMs = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - u32StartMs >= u32TimeoutMs)
        {
            Serial.println();
            Serial.println("[WARNING] Timeout konek WiFi.");
            return false;
        }

        delay(500);
        vFeedWatchdog();
        Serial.print(".");
    }

    Serial.println();

    char szIp[20];
    vGetLocalIpStr(szIp, sizeof(szIp));

    Serial.printf("[INFO] WiFi terhubung. IP: %s\n", szIp);
    return true;
}

/**
 * Memutus koneksi ke router dan mematikan radio WiFi.
 * Dipakai sebelum deep sleep agar radio tidak tetap nyala.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: WiFi station terputus, radio off.
 */
void vDisconnectWifiStation()
{
    Serial.println("[INFO] Memutus WiFi sebelum deep sleep.");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

/**
 * Menjalankan ESP32 sebagai Access Point untuk konfigurasi awal.
 *
 * Nama AP  : "MonitoringNode-Setup" (tanpa password)
 * IP       : 192.168.4.1
 * Tujuan   : Teknisi bisa konek dari HP/laptop dan buka
 *            browser ke 192.168.4.1 untuk mengisi config.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Mengubah mode WiFi menjadi AP.
 */
void vStartApMode()
{
    WiFi.mode(WIFI_AP);

    const char* pcPass = (strlen(AP_PASSWORD) > 0) ? AP_PASSWORD : nullptr;
    WiFi.softAP(AP_SSID, pcPass);

    char szIp[20];
    IPAddress ip = WiFi.softAPIP();
    snprintf(szIp, sizeof(szIp), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);

    Serial.println("[INFO] === AP MODE AKTIF ===");
    Serial.printf("[INFO] SSID    : %s\n", AP_SSID);
    Serial.printf("[INFO] IP      : %s\n", szIp);
    Serial.println("[INFO] Buka browser -> " AP_IP_STRING " (data + config).");
}

/**
 * Memeriksa apakah WiFi saat ini terhubung ke router (WL_CONNECTED).
 *
 * Input: Tidak ada.
 * Output: true jika WiFi status WL_CONNECTED.
 */
bool bIsWifiConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

/**
 * Membaca kekuatan sinyal WiFi (RSSI) dalam satuan dBm.
 * Nilai mendekati 0 berarti sinyal kuat (mis. -40 dBm = baik).
 * Nilai sangat negatif berarti sinyal lemah (mis. -90 dBm = buruk).
 *
 * Input: Tidak ada.
 * Output: RSSI dalam dBm. Mengembalikan 0 jika tidak terhubung.
 */
int8_t i8GetRssi()
{
    if (!bIsWifiConnected())
    {
        return 0;
    }

    return (int8_t)WiFi.RSSI();
}

/**
 * Mengisi buffer dengan IP lokal ESP32 dalam format string.
 * Contoh hasil: "192.168.1.105"
 *
 * Tidak menggunakan WiFi.localIP().toString() untuk menghindari
 * penggunaan kelas String.
 *
 * Input:
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer (minimal 16 byte untuk "255.255.255.255").
 *
 * Output: Tidak ada return value. Mengisi szBuffer.
 */
void vGetLocalIpStr(char* szBuffer, size_t uBufferSize)
{
    if (szBuffer == nullptr || uBufferSize == 0)
    {
        return;
    }

    if (!bIsWifiConnected())
    {
        snprintf(szBuffer, uBufferSize, "--");
        return;
    }

    IPAddress ip = WiFi.localIP();
    snprintf(
        szBuffer,
        uBufferSize,
        "%d.%d.%d.%d",
        ip[0], ip[1], ip[2], ip[3]
    );
}
