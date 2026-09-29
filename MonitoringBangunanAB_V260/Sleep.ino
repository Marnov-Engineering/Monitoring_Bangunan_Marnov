// =====================================================
// Sleep.ino
// Deep sleep: timer + tombol GPIO36 (ext0), tunggu epoch
// =====================================================

/**
 * Apakah bangun dari deep sleep karena tombol AP (GPIO36)?
 *
 * Input: Tidak ada.
 * Output: true jika wakeup cause = EXT0 (tombol).
 */
bool bIsWakeFromApButton(void)
{
    return (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
}

/**
 * Membedakan daya baru dinyalakan dari bangun deep sleep.
 *
 * Input: Tidak ada.
 * Output: true pada power-on, reset EN, atau brownout.
 *         false setelah deep sleep atau restart software.
 */
bool bIsColdPowerOn(void)
{
    esp_reset_reason_t eReason = esp_reset_reason();

    if (eReason == ESP_RST_POWERON
        || eReason == ESP_RST_BROWNOUT
        || eReason == ESP_RST_EXT)
    {
        Serial.printf("[INFO] Daya baru / reset keras. Alasan reset: %d\n", (int)eReason);
        return true;
    }

    return false;
}

/**
 * Memutus WiFi lalu deep sleep.
 * Bangun karena:
 *   - timer (jadwal slot berikutnya), ATAU
 *   - tombol GPIO36 ditekan (level LOW / ACTIVE LOW)
 *
 * Setelah bangun karena tombol, setup() harus konfirmasi
 * tahan 3 detik sebelum masuk AP (hindari sentuhan singkat).
 *
 * Input:
 * u64SleepUs - durasi sleep timer dalam mikrodetik.
 *
 * Output: Tidak mengembalikan nilai (ESP32 restart saat wake).
 */
void vEnterDeepSleepUs(uint64_t u64SleepUs)
{
    if (u64SleepUs < ((uint64_t)SLEEP_MIN_S * 1000000ULL))
    {
        u64SleepUs = (uint64_t)SLEEP_MIN_S * 1000000ULL;
    }

    vDisconnectWifiStation();
    vShutdownSensorsForSleep();
    vLedsOffForSleep();
    vHoldOutputsForSleep();
    vStopWatchdogForSleep();

    // GPIO36 tidak punya pull internal. Andalkan pull-up eksternal 10k.
    // ACTIVE LOW -> bangun saat level 0 (tombol ke GND).
    esp_err_t eExt = esp_sleep_enable_ext0_wakeup(
        (gpio_num_t)PIN_BUTTON_AP,
        0
    );
    if (eExt != ESP_OK)
    {
        Serial.printf(
            "[WARNING] Gagal aktifkan wake tombol GPIO%d (err=%d). "
            "Hanya timer yang aktif.\n",
            PIN_BUTTON_AP,
            (int)eExt
        );
    }
    else
    {
        Serial.printf(
            "[INFO] Wake sources: timer + tombol GPIO%d (tahan 3s setelah bangun = AP).\n",
            PIN_BUTTON_AP
        );
    }

    esp_sleep_enable_timer_wakeup(u64SleepUs);

    Serial.printf(
        "[INFO] Deep sleep %llu us (~%llu detik)...\n",
        (unsigned long long)u64SleepUs,
        (unsigned long long)(u64SleepUs / 1000000ULL)
    );
    Serial.flush();

    esp_deep_sleep_start();
}

/**
 * Menunggu sampai epoch UTC target, dengan timeout aman.
 * Jika sudah lewat target, langsung kembali (kirim segera).
 *
 * Input:
 * tTargetEpoch - detik UTC yang ditunggu.
 *
 * Output: Tidak mengembalikan nilai.
 * Side effect: delay singkat berulang.
 */
void vWaitUntilEpoch(time_t tTargetEpoch)
{
    if (!bIsTimeValid() || tTargetEpoch <= 0)
    {
        return;
    }

    time_t tNow;
    time(&tNow);

    if (tNow >= tTargetEpoch)
    {
        Serial.println("[INFO] Target kirim sudah lewat. Lanjut segera.");
        return;
    }

    long lWaitS = (long)(tTargetEpoch - tNow);
    Serial.printf("[INFO] Menunggu %ld detik sampai detik kirim...\n", lWaitS);

    // Batas tunggu: maksimal interval agar tidak macet.
    uint32_t u32IntervalS = g_nodeConfig.u32IntervalS;
    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    uint32_t u32StartMs = millis();
    uint32_t u32MaxWaitMs = (u32IntervalS + 5U) * 1000UL;

    while (true)
    {
        time(&tNow);

        if (tNow >= tTargetEpoch)
        {
            break;
        }

        if (millis() - u32StartMs >= u32MaxWaitMs)
        {
            Serial.println(
                "[WARNING] Timeout menunggu detik kirim. Lanjut."
            );
            break;
        }

        delay(200);
        vFeedWatchdog();
    }
}
