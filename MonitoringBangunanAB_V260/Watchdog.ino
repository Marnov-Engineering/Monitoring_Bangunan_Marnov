// =====================================================
// Watchdog.ino
// Task WDT: reset chip jika siklus macet (bukan saat deep sleep)
// =====================================================

#include <esp_task_wdt.h>

/**
 * Mengaktifkan task watchdog untuk task yang sedang berjalan (setup/loop).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitWatchdog()
{
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_config_t tCfg = {};
    tCfg.timeout_ms = WDT_TIMEOUT_MS;
    tCfg.idle_core_mask = 0;
    tCfg.trigger_panic = true;

    esp_err_t eErr = esp_task_wdt_init(&tCfg);
    if (eErr == ESP_ERR_INVALID_STATE)
    {
        eErr = esp_task_wdt_reconfigure(&tCfg);
    }
#else
    esp_err_t eErr = esp_task_wdt_init(WDT_TIMEOUT_MS / 1000UL, true);
#endif

    if (eErr != ESP_OK && eErr != ESP_ERR_INVALID_STATE)
    {
        Serial.printf("[WARNING] Watchdog init kode %d.\n", (int)eErr);
    }

    eErr = esp_task_wdt_add(NULL);
    if (eErr != ESP_OK && eErr != ESP_ERR_INVALID_STATE)
    {
        Serial.printf("[WARNING] Watchdog add kode %d.\n", (int)eErr);
    }

    Serial.printf("[INFO] Watchdog aktif, timeout %lu ms.\n", (unsigned long)WDT_TIMEOUT_MS);
}

/**
 * Memberi makan watchdog. Panggil di proses yang bisa lebih lama dari timeout.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vFeedWatchdog()
{
    esp_task_wdt_reset();
}

/**
 * Melepas task watchdog sebelum deep sleep.
 * Deep sleep mematikan inti digital; timer WDT tidak boleh
 * dianggap masih mengawasi task yang sudah berhenti.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vStopWatchdogForSleep()
{
    esp_task_wdt_delete(NULL);
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    esp_task_wdt_deinit();
#endif
}

/**
 * Perkiraan otonomi kasar (bukan uji lapangan 8 hari).
 * Dicetak sekali saat boot agar asumsi arus terlihat.
 *
 * Asumsi (ubah #define jika pengukuran beda):
 * - kapasitas paket AUTONOMY_PACK_MAH, pemakaian 80%
 * - aktif AUTONOMY_ACTIVE_MA selama AUTONOMY_ACTIVE_S tiap interval
 * - tidur AUTONOMY_SLEEP_UA
 *
 * Saat sleep: SENSOR_T (GPIO17) memutus rail VEML, SCD41 di powerDown,
 * RTC_T (GPIO33) LOW; jam DS3231 dipegang baterai eksternal.
 * Ukur arus tidur nyata jika angka ini dipakai untuk keputusan baterai.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vPrintAutonomyEstimate()
{
    uint32_t u32IntervalS = g_nodeConfig.u32IntervalS;
    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = DEFAULT_INTERVAL_S;
    }

    float fActiveS = (float)AUTONOMY_ACTIVE_S;
    if (fActiveS > (float)u32IntervalS)
    {
        fActiveS = (float)u32IntervalS;
    }

    float fSleepS = (float)u32IntervalS - fActiveS;
    float fAvgMa =
        ((fActiveS * (float)AUTONOMY_ACTIVE_MA) +
         (fSleepS * ((float)AUTONOMY_SLEEP_UA / 1000.0f))) /
        (float)u32IntervalS;

    float fUsableMah = (float)AUTONOMY_PACK_MAH * 0.80f;
    float fHours = 0.0f;
    if (fAvgMa > 0.01f)
    {
        fHours = fUsableMah / fAvgMa;
    }
    float fDays = fHours / 24.0f;

    Serial.printf(
        "[INFO] Estimasi otonomi: rata-rata %.2f mA, ~%.1f hari "
        "(paket %u mAh, 80%%, aktif %u s @ %u mA, tidur %u uA). "
        "Ini hitungan, bukan uji 8 hari.\n",
        fAvgMa,
        fDays,
        (unsigned)AUTONOMY_PACK_MAH,
        (unsigned)AUTONOMY_ACTIVE_S,
        (unsigned)AUTONOMY_ACTIVE_MA,
        (unsigned)AUTONOMY_SLEEP_UA
    );

    if (fDays >= 8.0f)
    {
        Serial.println("[INFO] Estimasi >= 8 hari TERPENUHI pada asumsi di atas.");
    }
    else
    {
        Serial.println(
            "[WARNING] Estimasi < 8 hari. Ukur arus tidur nyata di PCB."
        );
    }
}
