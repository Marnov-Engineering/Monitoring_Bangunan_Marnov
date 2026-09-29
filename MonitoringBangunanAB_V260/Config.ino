// =====================================================
// Config.ino
// LittleFS, muat/simpan config, jenis node, jeda kirim
// =====================================================

// =====================================================
// FUNCTIONS
// =====================================================

/**
 * Mount LittleFS lalu muat konfigurasi node (WiFi, URL Sheet, interval).
 * Jika file config tidak ada / gagal dibaca, isi g_nodeConfig dengan default.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Memodifikasi g_nodeConfig. Mengakses LittleFS.
 */
void vInitNodeConfig()
{
    // true = format ulang filesystem jika mount gagal.
    // Ini hanya terjadi pertama kali atau jika filesystem rusak.
    if (!LittleFS.begin(true))
    {
        Serial.println("[ERROR] LittleFS gagal dimount. Menggunakan config default.");
        vSetDefaultNodeConfig();
        return;
    }

    Serial.println("[INFO] LittleFS berhasil dimount.");

    if (!bLoadNodeConfig())
    {
        Serial.println("[INFO] Config tidak ditemukan. Menggunakan nilai default.");
        vSetDefaultNodeConfig();
    }
}

/**
 * Mengisi g_nodeConfig dengan nilai default.
 * Dipanggil ketika file config belum ada atau tidak bisa dibaca.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Menimpa seluruh field g_nodeConfig.
 */
void vSetDefaultNodeConfig()
{
    snprintf(
        g_nodeConfig.szNodeId,
        sizeof(g_nodeConfig.szNodeId),
        "%s",
        DEFAULT_NODE_ID
    );

    // WiFi dikosongkan agar boot pertama masuk AP sampai SSID diisi.
    // Link API sudah terisi default deployment (bisa diganti dari HMI).
    g_nodeConfig.szWifiSsid[0]     = '\0';
    g_nodeConfig.szWifiPassword[0] = '\0';
    snprintf(
        g_nodeConfig.szScriptUrl,
        sizeof(g_nodeConfig.szScriptUrl),
        "%s",
        DEFAULT_SCRIPT_URL
    );
    g_nodeConfig.u32IntervalS      = DEFAULT_INTERVAL_S;
    g_nodeConfig.u32ApTimeoutS     = DEFAULT_AP_TIMEOUT_S;
    snprintf(
        g_nodeConfig.szTimezone,
        sizeof(g_nodeConfig.szTimezone),
        "%s",
        DEFAULT_TIMEZONE_STRING
    );
    g_nodeConfig.u32StaggerGroupSize = DEFAULT_STAGGER_GROUP_SIZE;
    g_nodeConfig.u32StaggerStepS     = DEFAULT_STAGGER_STEP_S;
}

/**
 * Memeriksa jumlah alat per rombongan masih di rentang yang diizinkan.
 *
 * Input:
 * i32Value - nilai dari config.json / Sheet (boleh negatif = tidak valid).
 *
 * Output: true jika STAGGER_GROUP_SIZE_MIN..STAGGER_GROUP_SIZE_MAX.
 */
bool bIsStaggerGroupSizeValid(long i32Value)
{
    return (i32Value >= (long)STAGGER_GROUP_SIZE_MIN)
        && (i32Value <= (long)STAGGER_GROUP_SIZE_MAX);
}

/**
 * Memeriksa jeda antar rombongan masih di rentang yang diizinkan.
 *
 * Input:
 * i32Value - nilai detik dari config.json / Sheet.
 *
 * Output: true jika STAGGER_STEP_MIN_S..STAGGER_STEP_MAX_S.
 */
bool bIsStaggerStepValid(long i32Value)
{
    return (i32Value >= (long)STAGGER_STEP_MIN_S)
        && (i32Value <= (long)STAGGER_STEP_MAX_S);
}

/**
 * Jumlah alat per rombongan yang aman dipakai (tidak pernah 0).
 * Nilai rusak di RAM dikembalikan ke default agar tidak terjadi
 * pembagian dengan nol di perhitungan stagger.
 *
 * Input: Tidak ada.
 * Output: jumlah alat per rombongan.
 */
uint32_t u32GetStaggerGroupSize()
{
    if (!bIsStaggerGroupSizeValid((long)g_nodeConfig.u32StaggerGroupSize))
    {
        return DEFAULT_STAGGER_GROUP_SIZE;
    }

    return g_nodeConfig.u32StaggerGroupSize;
}

/**
 * Jeda antar rombongan (detik) yang aman dipakai.
 *
 * Input: Tidak ada.
 * Output: jeda detik.
 */
uint32_t u32GetStaggerStepS()
{
    if (!bIsStaggerStepValid((long)g_nodeConfig.u32StaggerStepS))
    {
        return DEFAULT_STAGGER_STEP_S;
    }

    return g_nodeConfig.u32StaggerStepS;
}

/**
 * Memuat konfigurasi node dari file JSON di LittleFS ke g_nodeConfig.
 *
 * Input: Tidak ada.
 * Output: true jika berhasil dimuat dan diparse.
 *         false jika file tidak ada atau format JSON salah.
 * Side effect: Mengubah g_nodeConfig jika berhasil.
 */
bool bLoadNodeConfig()
{
    File file = LittleFS.open(CONFIG_FILE_PATH, "r");

    if (!file)
    {
        Serial.println("[INFO] File config tidak ditemukan: " CONFIG_FILE_PATH);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err)
    {
        Serial.printf(
            "[ERROR] Parse config JSON gagal: %s\n",
            err.c_str()
        );
        return false;
    }

    // Salin setiap field dengan batas ukuran buffer.
    // Operator | (pipe) pada ArduinoJson memberikan nilai default
    // jika key tidak ditemukan di JSON.
    snprintf(
        g_nodeConfig.szNodeId,
        sizeof(g_nodeConfig.szNodeId),
        "%s",
        (const char*)(doc["node_id"] | DEFAULT_NODE_ID)
    );
    snprintf(
        g_nodeConfig.szWifiSsid,
        sizeof(g_nodeConfig.szWifiSsid),
        "%s",
        (const char*)(doc["wifi_ssid"] | "")
    );
    snprintf(
        g_nodeConfig.szWifiPassword,
        sizeof(g_nodeConfig.szWifiPassword),
        "%s",
        (const char*)(doc["wifi_password"] | "")
    );
    snprintf(
        g_nodeConfig.szScriptUrl,
        sizeof(g_nodeConfig.szScriptUrl),
        "%s",
        (const char*)(doc["script_url"] | DEFAULT_SCRIPT_URL)
    );

    // Config lama yang script_url-nya kosong ikut memakai link default.
    if (g_nodeConfig.szScriptUrl[0] == '\0')
    {
        snprintf(
            g_nodeConfig.szScriptUrl,
            sizeof(g_nodeConfig.szScriptUrl),
            "%s",
            DEFAULT_SCRIPT_URL
        );
    }

    g_nodeConfig.u32IntervalS = doc["interval_s"] | DEFAULT_INTERVAL_S;

    // Batasi interval agar tidak di luar rentang wajar.
    if (g_nodeConfig.u32IntervalS < INTERVAL_MIN_S)
    {
        g_nodeConfig.u32IntervalS = INTERVAL_MIN_S;
    }
    if (g_nodeConfig.u32IntervalS > INTERVAL_MAX_S)
    {
        g_nodeConfig.u32IntervalS = INTERVAL_MAX_S;
    }

    // Timeout AP (V2.3.0). Config lama tanpa key -> default 120 detik.
    g_nodeConfig.u32ApTimeoutS = doc["ap_timeout_s"] | DEFAULT_AP_TIMEOUT_S;
    if (g_nodeConfig.u32ApTimeoutS < AP_TIMEOUT_MIN_S)
    {
        g_nodeConfig.u32ApTimeoutS = AP_TIMEOUT_MIN_S;
    }
    if (g_nodeConfig.u32ApTimeoutS > AP_TIMEOUT_MAX_S)
    {
        g_nodeConfig.u32ApTimeoutS = AP_TIMEOUT_MAX_S;
    }

    // Timezone dari config; config lama tanpa key -> default WIB.
    snprintf(
        g_nodeConfig.szTimezone,
        sizeof(g_nodeConfig.szTimezone),
        "%s",
        (const char*)(doc["timezone"] | DEFAULT_TIMEZONE_STRING)
    );
    if (!bIsTimezoneOffsetValid(g_nodeConfig.szTimezone))
    {
        Serial.printf(
            "[WARNING] Timezone config tidak valid (%s). Pakai %s.\n",
            g_nodeConfig.szTimezone,
            DEFAULT_TIMEZONE_STRING
        );
        snprintf(
            g_nodeConfig.szTimezone,
            sizeof(g_nodeConfig.szTimezone),
            "%s",
            DEFAULT_TIMEZONE_STRING
        );
    }

    // Stagger (V2.5.0). Config lama tanpa key -> default 3 alat / 10 detik.
    long i32GroupSize = doc["stagger_group_size"] | (long)DEFAULT_STAGGER_GROUP_SIZE;
    if (!bIsStaggerGroupSizeValid(i32GroupSize))
    {
        Serial.printf(
            "[WARNING] stagger_group_size config tidak valid (%ld). Pakai %u.\n",
            i32GroupSize,
            (unsigned)DEFAULT_STAGGER_GROUP_SIZE
        );
        i32GroupSize = (long)DEFAULT_STAGGER_GROUP_SIZE;
    }
    g_nodeConfig.u32StaggerGroupSize = (uint32_t)i32GroupSize;

    long i32StepS = doc["stagger_step_s"] | (long)DEFAULT_STAGGER_STEP_S;
    if (!bIsStaggerStepValid(i32StepS))
    {
        Serial.printf(
            "[WARNING] stagger_step_s config tidak valid (%ld). Pakai %u.\n",
            i32StepS,
            (unsigned)DEFAULT_STAGGER_STEP_S
        );
        i32StepS = (long)DEFAULT_STAGGER_STEP_S;
    }
    g_nodeConfig.u32StaggerStepS = (uint32_t)i32StepS;

    Serial.printf(
        "[INFO] Config dimuat: Node=%s, SSID=%s, Interval=%us, Timezone=%s, "
        "Timeout AP=%us, Rombongan=%u alat, Jeda=%us\n",
        g_nodeConfig.szNodeId,
        g_nodeConfig.szWifiSsid,
        g_nodeConfig.u32IntervalS,
        g_nodeConfig.szTimezone,
        g_nodeConfig.u32ApTimeoutS,
        (unsigned)g_nodeConfig.u32StaggerGroupSize,
        (unsigned)g_nodeConfig.u32StaggerStepS
    );

    return true;
}

/**
 * Menyimpan g_nodeConfig (konfigurasi node) ke file JSON di LittleFS.
 *
 * Input: Tidak ada.
 * Output: true jika berhasil disimpan.
 * Side effect: Menulis ke LittleFS.
 */
bool bSaveNodeConfig()
{
    JsonDocument doc;

    doc["node_id"]       = g_nodeConfig.szNodeId;
    doc["wifi_ssid"]     = g_nodeConfig.szWifiSsid;
    doc["wifi_password"] = g_nodeConfig.szWifiPassword;
    doc["script_url"]    = g_nodeConfig.szScriptUrl;
    doc["interval_s"]    = g_nodeConfig.u32IntervalS;
    doc["timezone"]      = g_nodeConfig.szTimezone;
    doc["ap_timeout_s"]  = g_nodeConfig.u32ApTimeoutS;
    doc["stagger_group_size"] = g_nodeConfig.u32StaggerGroupSize;
    doc["stagger_step_s"]     = g_nodeConfig.u32StaggerStepS;

    File file = LittleFS.open(CONFIG_FILE_PATH, "w");

    if (!file)
    {
        Serial.println("[ERROR] Gagal membuka file config untuk ditulis.");
        return false;
    }

    size_t uWritten = serializeJson(doc, file);
    file.close();

    if (uWritten == 0)
    {
        Serial.println("[ERROR] Gagal menulis config JSON ke LittleFS.");
        return false;
    }

    Serial.printf(
        "[INFO] Config tersimpan: Node=%s, SSID=%s, Interval=%us, Timezone=%s, "
        "Timeout AP=%us\n",
        g_nodeConfig.szNodeId,
        g_nodeConfig.szWifiSsid,
        g_nodeConfig.u32IntervalS,
        g_nodeConfig.szTimezone,
        g_nodeConfig.u32ApTimeoutS
    );

    return true;
}

/**
 * Memeriksa apakah konfigurasi node sudah cukup untuk mulai bekerja.
 * Minimal: SSID WiFi dan Script URL harus diisi.
 *
 * Input: Tidak ada.
 * Output: true jika SSID dan Script URL tidak kosong.
 */
bool bIsNodeConfigValid()
{
    bool bSsidOk  = (strlen(g_nodeConfig.szWifiSsid) > 0);
    bool bUrlOk   = (strlen(g_nodeConfig.szScriptUrl) > 0);

    return (bSsidOk && bUrlOk);
}

/**
 * Memeriksa apakah node ini berjenis A (iluminansi).
 * Jenis node ditentukan dari karakter pertama Node ID.
 * Contoh: "A-01" -> Node A, "B-02" -> Node B.
 *
 * Input: Tidak ada.
 * Output: true jika karakter pertama Node ID adalah 'A' atau 'a'.
 */
bool bIsNodeTypeA()
{
    char cFirst = g_nodeConfig.szNodeId[0];
    return (cFirst == 'A' || cFirst == 'a');
}

/**
 * Mengambil nomor urut dari Node ID.
 *
 * Contoh:
 * "A-01" -> 1
 * "A-02" -> 2
 * "B-03" -> 3
 *
 * Jika tidak ada angka, dianggap node 1 (slot detik 00).
 *
 * Input: Tidak ada. Membaca g_nodeConfig.szNodeId.
 *
 * Output:
 * Nomor node, minimum 1.
 */
int iGetNodeNumber()
{
    const char* pszId = g_nodeConfig.szNodeId;
    int iIndex = 0;

    while (pszId[iIndex] != '\0')
    {
        if (pszId[iIndex] >= '0' && pszId[iIndex] <= '9')
        {
            int iNumber = atoi(&pszId[iIndex]);

            if (iNumber < 1)
            {
                return 1;
            }

            return iNumber;
        }

        iIndex++;
    }

    Serial.println(
        "[WARNING] Node ID tidak mengandung angka. "
        "Jeda kirim memakai slot node 1 (detik 00)."
    );

    return 1;
}

/**
 * Menghitung jeda kirim node ini di dalam satu interval.
 *
 * Satu rombongan berbagi detik kirim yang sama:
 *   group  = (nomor - 1) / jumlah_alat_per_rombongan
 *   offset = group * jeda_antar_rombongan
 * V2.5.0: kedua nilai dari g_nodeConfig (diatur di HMI, sync saat daya
 * baru ON). Jika offset melebihi interval, dibungkus modulo interval.
 *
 * Contoh default 3 alat / 10 detik, interval 300 detik:
 *   A-01..A-03 -> +0 s
 *   A-04..A-06 -> +10 s
 *   A-07..A-09 -> +20 s
 *   A-22..A-24 -> +70 s
 *   A-25       -> +80 s
 *
 * V2.4.0: offset ini hanya untuk wake KIRIM. Wake BACA semua node
 * serentak di awal slot.
 *
 * Input: Tidak ada.
 * Output: Offset detik dari awal slot interval.
 */
uint32_t u32GetNodeStaggerS()
{
    int iNodeNumber = iGetNodeNumber();

    if (iNodeNumber < 1)
    {
        iNodeNumber = 1;
    }

    // Rombongan 0,1,2,... tiap u32GetStaggerGroupSize() node.
    uint32_t u32Group = (uint32_t)(iNodeNumber - 1) / u32GetStaggerGroupSize();
    uint32_t u32OffsetS = u32Group * u32GetStaggerStepS();

    uint32_t u32IntervalS = g_nodeConfig.u32IntervalS;

    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    return (u32OffsetS % u32IntervalS);
}

// =====================================================
// BACKFILL MARKER (Preferences / NVS)
// =====================================================

/**
 * Membuka namespace Preferences untuk marker backfill.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitBackfillMarker()
{
    if (!g_prefs.begin(PREFS_NAMESPACE, false))
    {
        Serial.println("[WARNING] Preferences gagal dibuka.");
        return;
    }

    Serial.println("[INFO] Preferences backfill siap.");
}

/**
 * Membaca timestamp terakhir yang sukses dikirim.
 *
 * Input:
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer.
 *
 * Output: Tidak ada. Buffer diisi string kosong jika belum ada.
 */
void vGetLastSentTimestamp(char* szBuffer, size_t uBufferSize)
{
    if (szBuffer == nullptr || uBufferSize == 0)
    {
        return;
    }

    szBuffer[0] = '\0';
    g_prefs.getString(PREFS_KEY_LAST_SENT, szBuffer, uBufferSize);
}

/**
 * Menyimpan timestamp terakhir yang sukses dikirim.
 *
 * Input:
 * szTimestamp - ISO 8601 lokal dengan offset, contoh 2026-09-22T10:35:00+08:00.
 *
 * Output: Tidak ada.
 */
void vSetLastSentTimestamp(const char* szTimestamp)
{
    if (szTimestamp == nullptr)
    {
        return;
    }

    g_prefs.putString(PREFS_KEY_LAST_SENT, szTimestamp);
    Serial.printf("[INFO] last_sent_timestamp = %s\n", szTimestamp);
}

/**
 * Menghapus marker backfill (setelah hapus file CSV).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vClearLastSentTimestamp()
{
    g_prefs.remove(PREFS_KEY_LAST_SENT);
    Serial.println("[INFO] last_sent_timestamp dihapus.");
}
