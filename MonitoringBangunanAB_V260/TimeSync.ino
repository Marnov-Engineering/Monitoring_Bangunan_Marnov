// =====================================================
// TimeSync.ino
// NTP, timestamp lokal ISO+offset, slot, durasi deep sleep
// V2.4.0: durasi sleep terpisah untuk wake BACA dan wake KIRIM
// =====================================================

// Status SNTP (SNTP_SYNC_STATUS_COMPLETED) dipakai agar tidak menganggap
// jam dari DS3231 sebagai "NTP sukses" sebelum server benar-benar menjawab.
#include "esp_sntp.h"
// gettimeofday() untuk hitung sleep dengan resolusi mikrodetik.
#include <sys/time.h>

// =====================================================
// FUNCTIONS
// =====================================================

/**
 * Memeriksa string timezone Indonesia yang didukung.
 *
 * Input:
 * szTimezone - contoh "+07:00", "+08:00", "+09:00".
 *
 * Output: true jika salah satu dari WIB / WITA / WIT.
 */
bool bIsTimezoneOffsetValid(const char* szTimezone)
{
    if (szTimezone == nullptr)
    {
        return false;
    }

    if (strcmp(szTimezone, "+07:00") == 0)
    {
        return true;
    }
    if (strcmp(szTimezone, "+08:00") == 0)
    {
        return true;
    }
    if (strcmp(szTimezone, "+09:00") == 0)
    {
        return true;
    }

    return false;
}

/**
 * Mengubah string timezone ISO menjadi offset detik dari UTC.
 *
 * Input:
 * szTimezone - "+07:00" / "+08:00" / "+09:00" (atau tidak valid).
 *
 * Output: detik offset; default WIB jika tidak valid.
 */
long i32TimezoneOffsetSeconds(const char* szTimezone)
{
    if (szTimezone == nullptr)
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    // Format tetap: +HH:MM (6 karakter + null).
    if (strlen(szTimezone) != 6)
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    if (szTimezone[0] != '+' && szTimezone[0] != '-')
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    if (szTimezone[3] != ':')
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    int iHour =
        (szTimezone[1] - '0') * 10 +
        (szTimezone[2] - '0');
    int iMin =
        (szTimezone[4] - '0') * 10 +
        (szTimezone[5] - '0');

    if (iHour < 0 || iHour > 14 || iMin < 0 || iMin > 59)
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    long i32Sec = (long)iHour * 3600L + (long)iMin * 60L;
    if (szTimezone[0] == '-')
    {
        i32Sec = -i32Sec;
    }

    return i32Sec;
}

/**
 * Offset detik zona waktu yang tersimpan di config node.
 *
 * Input: Tidak ada.
 * Output: detik offset dari UTC.
 */
long i32GetConfiguredTimezoneOffsetS()
{
    if (!bIsTimezoneOffsetValid(g_nodeConfig.szTimezone))
    {
        return DEFAULT_TIMEZONE_OFFSET_S;
    }

    return i32TimezoneOffsetSeconds(g_nodeConfig.szTimezone);
}

/**
 * Mengatur offset zona untuk getLocalTime / strftime lokal.
 * Dipanggil saat NTP sync dan setelah timezone dari Sheet berubah.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Memanggil configTime dengan offset config.
 */
void vApplyNtpTimezoneOffset()
{
    long i32OffsetS = i32GetConfiguredTimezoneOffsetS();

    configTime(
        i32OffsetS,
        0,
        NTP_SERVER_PRIMARY,
        NTP_SERVER_SECONDARY
    );

    Serial.printf(
        "[INFO] Offset NTP/local time: %s (%ld s).\n",
        g_nodeConfig.szTimezone[0] != '\0'
            ? g_nodeConfig.szTimezone
            : DEFAULT_TIMEZONE_STRING,
        i32OffsetS
    );
}

/**
 * Sinkronisasi waktu sistem ESP32 dari server NTP.
 * Memerlukan koneksi WiFi aktif sebelum dipanggil.
 *
 * Penting:
 * Sebelum fungsi ini dipanggil, setup() biasanya sudah mengisi jam sistem
 * dari DS3231. getLocalTime() akan langsung sukses (tahun valid), jadi
 * TIDAK boleh dipakai sebagai bukti NTP selesai.
 * Bukti yang benar: sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED.
 *
 * Input: Tidak ada.
 * Output: true jika SNTP selesai dan waktu lokal valid.
 *         false jika timeout / gagal (pemanggil harus pakai jam RTC).
 * Side effect: Jika sukses, mengubah waktu sistem ESP32 ke waktu NTP.
 */
bool bSyncNtp()
{
    Serial.println("[INFO] Sinkronisasi NTP dimulai...");

    // Offset zona dari config (WIB/WITA/WIT). Setelah deep sleep ESP reboot,
    // status SNTP mulai dari RESET; tunggu COMPLETED = server menjawab.
    vApplyNtpTimezoneOffset();

    uint32_t u32StartMs = millis();

    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
    {
        if (millis() - u32StartMs >= NTP_TIMEOUT_MS)
        {
            Serial.println(
                "[WARNING] Timeout sinkronisasi NTP "
                "(SNTP belum COMPLETED). Jam RTC akan dipakai."
            );
            return false;
        }

        delay(200);
        vFeedWatchdog();
    }

    struct tm timeInfo;

    // Setelah SNTP completed, pastikan kalender lokal masih masuk akal.
    if (!getLocalTime(&timeInfo) || timeInfo.tm_year < 120)
    {
        Serial.println(
            "[WARNING] SNTP completed tetapi waktu lokal tidak valid. "
            "Jam RTC akan dipakai."
        );
        return false;
    }

    char szTimeBuf[32];
    strftime(szTimeBuf, sizeof(szTimeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
    Serial.printf(
        "[INFO] NTP tersinkron (SNTP completed). Waktu lokal: %s %s\n",
        szTimeBuf,
        g_nodeConfig.szTimezone
    );

    return true;
}

/**
 * Menghasilkan timestamp lokal ISO 8601 dengan offset zona.
 *
 * Input:
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer (disarankan >= 32).
 *
 * Output: Tidak ada. Mengisi szBuffer.
 */
void vGetTimestampLocal(char* szBuffer, size_t uBufferSize)
{
    time_t tNow;
    time(&tNow);
    vFormatTimestampLocal(tNow, szBuffer, uBufferSize);
}

/**
 * Memformat epoch UTC menjadi timestamp lokal ISO 8601 (tanpa suffix zona).
 *
 * Contoh: tEpoch = 2026-09-22 02:35:00 UTC, timezone +08:00
 *         -> "2026-09-22T10:35:00"
 * Offset zona tetap ditulis di kolom terpisah "timezone".
 *
 * Slot scheduling tetap memakai epoch UTC; string ini hanya untuk
 * pencatatan Sheet/CSV agar operator melihat jam dinding lokasi.
 *
 * Input:
 * tEpochUtc   - detik UTC (epoch).
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer.
 *
 * Output: Tidak ada. Mengisi szBuffer.
 */
void vFormatTimestampLocal(time_t tEpochUtc, char* szBuffer, size_t uBufferSize)
{
    if (szBuffer == nullptr || uBufferSize == 0)
    {
        return;
    }

    if (!bIsTimeValid() || tEpochUtc <= 0)
    {
        snprintf(szBuffer, uBufferSize, "--");
        return;
    }

    const char* pszTz = g_nodeConfig.szTimezone;
    if (!bIsTimezoneOffsetValid(pszTz))
    {
        pszTz = DEFAULT_TIMEZONE_STRING;
    }

    long i32OffsetS = i32TimezoneOffsetSeconds(pszTz);

    // Wall-clock lokal = UTC + offset; breakdown via gmtime agar tidak
    // bergantung lagi pada state configTime.
    time_t tLocalWall = tEpochUtc + (time_t)i32OffsetS;
    struct tm localTm;
    gmtime_r(&tLocalWall, &localTm);

    // Hanya jam dinding lokal; kolom timezone di Sheet menyimpan +07:00 dll.
    strftime(szBuffer, uBufferSize, "%Y-%m-%dT%H:%M:%S", &localTm);
}

/**
 * Menghasilkan string waktu lokal untuk log Serial.
 *
 * Input:
 * szBuffer    - buffer tujuan.
 * uBufferSize - ukuran buffer.
 *
 * Output: Tidak ada. Mengisi szBuffer.
 */
void vGetTimeLocalStr(char* szBuffer, size_t uBufferSize)
{
    if (szBuffer == nullptr || uBufferSize == 0)
    {
        return;
    }

    struct tm timeInfo;

    if (!getLocalTime(&timeInfo))
    {
        snprintf(szBuffer, uBufferSize, "--");
        return;
    }

    char szBase[24];
    strftime(szBase, sizeof(szBase), "%Y-%m-%d %H:%M:%S", &timeInfo);

    const char* pszTz = bIsTimezoneOffsetValid(g_nodeConfig.szTimezone)
        ? g_nodeConfig.szTimezone
        : DEFAULT_TIMEZONE_STRING;

    snprintf(szBuffer, uBufferSize, "%s %s", szBase, pszTz);
}

/**
 * Memeriksa apakah waktu sistem ESP32 sudah valid (sudah sync NTP/RTC).
 *
 * Input: Tidak ada.
 * Output: true jika waktu valid.
 */
bool bIsTimeValid()
{
    struct tm timeInfo;

    if (!getLocalTime(&timeInfo))
    {
        return false;
    }

    return (timeInfo.tm_year >= 120);
}

/**
 * Menghitung awal slot interval saat ini.
 * Contoh interval 300: ...:00, :05, :10, ...
 *
 * Input:
 * u32IntervalS - interval dalam detik.
 *
 * Output: epoch UTC awal slot.
 */
time_t tGetSlotStartEpoch(uint32_t u32IntervalS)
{
    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    time_t tNow;
    time(&tNow);

    return tNow - (tNow % (time_t)u32IntervalS);
}

/**
 * Menghitung detik kirim = awal slot + stagger node.
 *
 * Input:
 * tSlotStart   - epoch awal slot.
 * u32StaggerS  - jeda detik node.
 *
 * Output: epoch detik kirim WiFi.
 */
time_t tGetSendEpoch(time_t tSlotStart, uint32_t u32StaggerS)
{
    return tSlotStart + (time_t)u32StaggerS;
}

/**
 * Menghitung awal slot berikutnya setelah sekarang.
 *
 * Input:
 * u32IntervalS - interval dalam detik.
 *
 * Output: epoch UTC awal slot berikutnya.
 */
time_t tGetNextSlotStartEpoch(uint32_t u32IntervalS)
{
    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    time_t tNow;
    time(&tNow);

    time_t tThisSlot = tNow - (tNow % (time_t)u32IntervalS);
    return tThisSlot + (time_t)u32IntervalS;
}

/**
 * Interval kerja yang aman (tidak kurang dari INTERVAL_MIN_S).
 *
 * Input: Tidak ada.
 * Output: interval detik dari config, dibatasi minimum.
 */
static uint32_t u32GetSafeIntervalS(void)
{
    uint32_t u32IntervalS = g_nodeConfig.u32IntervalS;

    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    return u32IntervalS;
}

/**
 * Menghitung durasi sleep (mikrodetik) dari sekarang sampai epoch target.
 *
 * Memakai gettimeofday() (resolusi mikrodetik), bukan time() yang
 * dibulatkan ke detik, supaya wake BACA jatuh sedekat mungkin dengan
 * tanda kelipatan interval (misal tepat hh:05:00, bukan hh:04:59.4).
 *
 * Input:
 * tTargetEpoch - epoch UTC tujuan bangun.
 *
 * Output: durasi sleep mikrodetik, minimal SLEEP_MIN_S.
 */
static uint64_t u64SleepUsUntilEpoch(time_t tTargetEpoch)
{
    struct timeval tvNow;
    gettimeofday(&tvNow, nullptr);

    int64_t i64NowUs =
        (int64_t)tvNow.tv_sec * 1000000LL + (int64_t)tvNow.tv_usec;
    int64_t i64TargetUs = (int64_t)tTargetEpoch * 1000000LL;
    int64_t i64SleepUs = i64TargetUs - i64NowUs;

    int64_t i64MinUs = (int64_t)SLEEP_MIN_S * 1000000LL;
    if (i64SleepUs < i64MinUs)
    {
        i64SleepUs = i64MinUs;
    }

    return (uint64_t)i64SleepUs;
}

/**
 * Epoch target wake BACA untuk awal slot tertentu.
 *
 * Node A: tepat di awal slot (kelipatan interval).
 * Node B: NODE_B_WAKE_EARLY_S lebih awal, agar single shot SCD41
 *         (~5 detik) selesai di sekitar awal slot.
 *
 * Input:
 * tSlotStart - epoch awal slot.
 *
 * Output: epoch UTC saat node harus bangun untuk membaca sensor.
 */
static time_t tGetReadWakeEpoch(time_t tSlotStart)
{
    if (bIsNodeTypeA())
    {
        return tSlotStart;
    }

    return tSlotStart - (time_t)NODE_B_WAKE_EARLY_S;
}

/**
 * Menghitung durasi deep sleep sampai wake BACA berikutnya (V2.4.0).
 *
 * Semua node bangun serentak di kelipatan interval (misal tiap 5 menit:
 * :00, :05, :10, ...). Stagger TIDAK dipakai di sini; stagger hanya untuk
 * wake KIRIM. Jika target wake slot berikutnya sudah lewat (misal wake
 * KIRIM rombongan terakhir + WiFi lambat melewati batas), loncat satu slot.
 *
 * Jika waktu belum valid, tidur sebesar satu interval.
 *
 * Input: Tidak ada.
 * Output: Durasi sleep dalam mikrodetik.
 */
uint64_t u64GetSleepUsToNextReadWake(void)
{
    uint32_t u32IntervalS = u32GetSafeIntervalS();

    if (!bIsTimeValid())
    {
        Serial.printf(
            "[WARNING] Waktu tidak valid. Sleep %u detik.\n",
            u32IntervalS
        );
        return (uint64_t)u32IntervalS * 1000000ULL;
    }

    time_t tNow;
    time(&tNow);

    time_t tNextSlot = tGetNextSlotStartEpoch(u32IntervalS);
    time_t tWake = tGetReadWakeEpoch(tNextSlot);

    // Batas SLEEP_MIN_S: target yang terlalu mepet juga dianggap lewat,
    // supaya tidak bangun sesudah awal slot tanpa sengaja.
    if (tWake <= tNow + (time_t)SLEEP_MIN_S)
    {
        tNextSlot = tNextSlot + (time_t)u32IntervalS;
        tWake = tGetReadWakeEpoch(tNextSlot);
    }

    uint64_t u64SleepUs = u64SleepUsUntilEpoch(tWake);

    Serial.printf(
        "[INFO] Sleep ~%llu detik sampai wake BACA (slot %ld%s).\n",
        (unsigned long long)(u64SleepUs / 1000000ULL),
        (long)tNextSlot,
        bIsNodeTypeA() ? "" : ", Node B lebih awal untuk SCD41"
    );

    return u64SleepUs;
}

/**
 * Menghitung durasi deep sleep dari wake BACA sampai wake KIRIM (V2.4.0).
 *
 * Detik kirim = awal slot + stagger rombongan.
 * Wake KIRIM  = detik kirim - WAKE_BEFORE_SEND_S (waktu untuk WiFi + NTP).
 * Jika target sudah lewat / terlalu mepet, sleep minimum SLEEP_MIN_S.
 *
 * Input:
 * tSlotStart - epoch awal slot yang datanya baru ditulis ke CSV.
 *
 * Output: Durasi sleep dalam mikrodetik.
 */
uint64_t u64GetSleepUsToSendWake(time_t tSlotStart)
{
    if (!bIsTimeValid() || tSlotStart <= 0)
    {
        Serial.println(
            "[WARNING] Waktu/slot tidak valid untuk wake KIRIM. "
            "Sleep minimum."
        );
        return (uint64_t)SLEEP_MIN_S * 1000000ULL;
    }

    uint32_t u32StaggerS = u32GetNodeStaggerS();
    time_t tSend = tGetSendEpoch(tSlotStart, u32StaggerS);
    time_t tWake = tSend - (time_t)WAKE_BEFORE_SEND_S;

    uint64_t u64SleepUs = u64SleepUsUntilEpoch(tWake);

    Serial.printf(
        "[INFO] Sleep ~%llu detik sampai wake KIRIM "
        "(slot %ld, kirim +%u s, margin %u s).\n",
        (unsigned long long)(u64SleepUs / 1000000ULL),
        (long)tSlotStart,
        (unsigned)u32StaggerS,
        (unsigned)WAKE_BEFORE_SEND_S
    );

    return u64SleepUs;
}

/**
 * Durasi deep sleep sampai wake berikutnya, sesuai state dua wake.
 *
 * Dipakai oleh jalur yang tidak tahu konteks siklus (timeout AP,
 * tombol di AP, wake tombol tanpa hold, siklus gabungan cold power):
 *   - g_bPendingSend = true  -> ke wake KIRIM slot yang tertunda
 *   - g_bPendingSend = false -> ke wake BACA slot berikutnya
 *
 * Input: Tidak ada.
 * Output: Durasi sleep dalam mikrodetik.
 */
uint64_t u64GetSleepUsToNextWake(void)
{
    if (g_bPendingSend && g_tPendingSlotEpoch > 0)
    {
        return u64GetSleepUsToSendWake(g_tPendingSlotEpoch);
    }

    return u64GetSleepUsToNextReadWake();
}
