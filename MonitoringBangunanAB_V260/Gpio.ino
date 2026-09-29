// =====================================================
// Gpio.ino
// LED status, ADC baterai (BMS), saklar daya SD card
// Pin dari skema SCH_Node-AB-Monitoring-Bangunan_2026-09-18
//
// Arti LED (PDF + pola tambahan V1.9.0, diperbarui V2.6.0):
//   Hijau  = rekam CSV OK / +biru = kirim Sheet OK
//   Kuning = error storage
//   Merah  = error sensor
//   Biru   = WiFi OK
//   Bangun (daya baru ON / timer) = running LED G-Y-R-B 1 putaran (V2.6.0,
//            menggantikan kedip hijau)
//   Tombol ditekan (bangun dari sleep / tahan untuk mematikan AP)
//          = semua LED ON (tes lampu, V2.6.0)
//   AP     = running LED G-Y-R-B terus-menerus
//   OTA    = semua kedip lambat; sukses = kedip cepat lalu restart
// =====================================================

#include <driver/gpio.h>

// State chase LED mode AP (non-blocking).
static uint8_t  g_u8ApChaseStep    = 0;
static uint32_t g_u32ApChaseLastMs = 0;

// State kedip lambat OTA (semua LED bersama).
static bool     g_bOtaLedOn        = false;
static uint32_t g_u32OtaLedLastMs  = 0;

/**
 * Melepas kunci GPIO setelah deep sleep.
 * Tanpa ini, digitalWrite diabaikan karena gpio_hold_en.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
static void vReleaseGpioHoldsAfterWake()
{
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)PIN_LED_GREEN);
    gpio_hold_dis((gpio_num_t)PIN_LED_YELLOW);
    gpio_hold_dis((gpio_num_t)PIN_LED_RED);
    gpio_hold_dis((gpio_num_t)PIN_LED_BLUE);
    gpio_hold_dis((gpio_num_t)PIN_PWR_RTC);
    gpio_hold_dis((gpio_num_t)PIN_PWR_SENSOR);
    gpio_hold_dis((gpio_num_t)PIN_PWR_SDCARD);
}

/**
 * Menyalakan atau mematikan satu LED (active HIGH, low-side 2N7002).
 *
 * Input:
 * iPin    - GPIO LED.
 * bIsOn   - true = menyala.
 *
 * Output: Tidak ada.
 */
static void vSetLedPin(int iPin, bool bIsOn)
{
    digitalWrite(iPin, bIsOn ? HIGH : LOW);
}

/**
 * Menyalakan atau mematikan keempat LED sekaligus.
 *
 * Input: bIsOn - true = semua ON.
 * Output: Tidak ada.
 */
static void vSetAllLedsSame(bool bIsOn)
{
    vSetLedPin(PIN_LED_GREEN, bIsOn);
    vSetLedPin(PIN_LED_YELLOW, bIsOn);
    vSetLedPin(PIN_LED_RED, bIsOn);
    vSetLedPin(PIN_LED_BLUE, bIsOn);
}

/**
 * Inisialisasi LED, ADC baterai, dan saklar daya yang ada di skema.
 *
 * LED (asumsi urutan skema, silkscreen belum tertulis warna):
 *   LED1 IO27 hijau  = rekam OK
 *   LED2 IO25 kuning = error storage
 *   LED3 IO32 merah  = error sensor
 *   LED4 IO26 biru   = WiFi
 *
 * BMS  = GPIO39 (SVN), ADC, input-only.
 * RTC_T = GPIO33, HIGH = rail VCC_RTC menyala (Q7+Q8).
 * DS3231 punya baterai eksternal, jadi RTC_T hanya HIGH saat node bangun
 * (supaya I2C bisa baca/tulis). Sebelum sleep pin ini LOW.
 * SENSOR_T = GPIO17, HIGH = rail VEML7700 menyala.
 * Terpisah dari RTC supaya modul RTC tidak menambah arus sleep.
 * SDCARD_T = GPIO16, HIGH = rail +SDCard menyala. Firmware pakai LittleFS,
 *            jadi rail SD selalu LOW.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitGpioHardware()
{
    vReleaseGpioHoldsAfterWake();

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    vSetAllStatusLeds(false, false, false, false);

    pinMode(PIN_PWR_RTC, OUTPUT);
    // RTC harus hidup sebelum baca jam. MOSFET butuh waktu sebentar.
    digitalWrite(PIN_PWR_RTC, HIGH);

    pinMode(PIN_PWR_SENSOR, OUTPUT);
    // Rail VEML terpisah dari RTC.
    digitalWrite(PIN_PWR_SENSOR, HIGH);
    delay(30);

    pinMode(PIN_PWR_SDCARD, OUTPUT);
    // Tidak memakai microSD. Matikan rail-nya.
    digitalWrite(PIN_PWR_SDCARD, LOW);

    analogReadResolution(12);
    analogSetPinAttenuation(PIN_ADC_BATTERY, ADC_11db);

    Serial.println(
        "[INFO] GPIO: LED 27/25/32/26, BMS ADC39, "
        "RTC_T 33 ON, SENSOR_T 17 ON, SD_T 16 OFF."
    );
}

/**
 * Memperbarui empat LED status. Boleh lebih dari satu menyala.
 *
 * Input:
 * bRecordOk      - hijau.
 * bStorageError  - kuning.
 * bSensorError   - merah.
 * bWifiOk        - biru.
 *
 * Output: Tidak ada.
 */
void vSetAllStatusLeds(
    bool bRecordOk,
    bool bStorageError,
    bool bSensorError,
    bool bWifiOk
)
{
    vSetLedPin(PIN_LED_GREEN, bRecordOk);
    vSetLedPin(PIN_LED_YELLOW, bStorageError);
    vSetLedPin(PIN_LED_RED, bSensorError);
    vSetLedPin(PIN_LED_BLUE, bWifiOk);
}

/**
 * Mematikan semua LED sebelum deep sleep (hemat arus).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vLedsOffForSleep()
{
    vSetAllStatusLeds(false, false, false, false);
}

/**
 * Tanda alat bangun (daya baru ON atau bangun timer):
 * running LED hijau -> kuning -> merah -> biru, SATU putaran, lalu semua mati.
 * V2.6.0: menggantikan kedip hijau 3x. Urutan sama dengan running LED AP,
 * tetapi hanya sekali sehingga tidak dikira mode AP.
 * Tidak dipakai saat bangun karena tombol (di sana semua LED ON = tes lampu).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Blocking sekitar 4 x LED_BOOT_CHASE_STEP_MS, LED berganti.
 */
void vLedSignalBoot()
{
    Serial.println("[INFO] LED: boot (running G-Y-R-B 1 putaran).");

    const int aiChasePins[4] =
    {
        PIN_LED_GREEN,
        PIN_LED_YELLOW,
        PIN_LED_RED,
        PIN_LED_BLUE
    };

    for (uint8_t u8Step = 0; u8Step < 4U; u8Step++)
    {
        vSetAllLedsSame(false);
        vSetLedPin(aiChasePins[u8Step], true);
        delay(LED_BOOT_CHASE_STEP_MS);
        vFeedWatchdog();
    }

    vSetAllLedsSame(false);
}

/**
 * Tes lampu: keempat LED menyala bersamaan.
 * Dipakai saat tombol ditekan:
 *   - alat bangun dari deep sleep karena tombol (EXT0), dan
 *   - tombol ditahan di mode AP untuk mematikan AP.
 * Tujuannya teknisi bisa langsung melihat keempat LED masih berfungsi.
 * LED tetap ON sampai pemanggil mengganti pola (running AP / sleep).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Keempat LED ON.
 */
void vLedLampTestOn()
{
    vSetAllLedsSame(true);
    Serial.println("[INFO] LED: tes lampu (semua ON).");
}

/**
 * Reset state chase sebelum masuk mode AP.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vLedResetApChase()
{
    g_u8ApChaseStep    = 0;
    g_u32ApChaseLastMs = 0;
    vSetAllStatusLeds(false, false, false, false);
}

/**
 * Running LED mode AP (non-blocking): hijau -> kuning -> merah -> biru.
 * Panggil dari loop() saat STATE_AP_MODE.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Mengubah LED secara bergantian.
 */
void vLedUpdateApChase()
{
    uint32_t u32NowMs = millis();

    if ((g_u32ApChaseLastMs != 0)
        && ((u32NowMs - g_u32ApChaseLastMs) < LED_AP_CHASE_MS))
    {
        return;
    }

    g_u32ApChaseLastMs = u32NowMs;

    vSetAllStatusLeds(false, false, false, false);

    switch (g_u8ApChaseStep % 4U)
    {
        case 0:  vSetLedPin(PIN_LED_GREEN, true);  break;
        case 1:  vSetLedPin(PIN_LED_YELLOW, true); break;
        case 2:  vSetLedPin(PIN_LED_RED, true);    break;
        default: vSetLedPin(PIN_LED_BLUE, true);   break;
    }

    g_u8ApChaseStep++;
}

/**
 * Mulai pola OTA: semua LED kedip lambat bersama.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vLedBeginOtaProgress()
{
    g_bOtaLedOn       = false;
    g_u32OtaLedLastMs = 0;
    vSetAllLedsSame(false);
    Serial.println("[INFO] LED: OTA progress (semua kedip lambat).");
}

/**
 * Tick kedip lambat OTA. Panggil berkala selama unduh/tulis.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vLedTickOtaProgress()
{
    uint32_t u32NowMs = millis();

    if ((g_u32OtaLedLastMs != 0)
        && ((u32NowMs - g_u32OtaLedLastMs) < LED_OTA_SLOW_MS))
    {
        return;
    }

    g_u32OtaLedLastMs = u32NowMs;
    g_bOtaLedOn = !g_bOtaLedOn;
    vSetAllLedsSame(g_bOtaLedOn);
}

/**
 * OTA gagal: matikan LED.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vLedSignalOtaFail()
{
    vSetAllLedsSame(false);
    Serial.println("[INFO] LED: OTA gagal (LED OFF).");
}

/**
 * OTA sukses: semua LED kedip cepat, lalu restart.
 *
 * Input: Tidak ada.
 * Output: Tidak ada (fungsi tidak kembali jika restart berhasil).
 * Side effect: ESP.restart().
 */
void vLedSignalOtaSuccessThenRestart()
{
    Serial.println("[INFO] LED: OTA sukses (kedip cepat) lalu restart.");

    for (uint8_t u8 = 0; u8 < LED_OTA_FAST_COUNT; u8++)
    {
        vSetAllLedsSame(true);
        delay(LED_OTA_FAST_MS);
        vSetAllLedsSame(false);
        delay(LED_OTA_FAST_MS);
    }

    delay(200);
    ESP.restart();
}

/**
 * Kirim Sheet sukses: hijau + biru kedip cepat bersama.
 * Artinya data tersimpan lokal DAN terkirim ke cloud.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Blocking singkat.
 */
void vLedSignalSendOk()
{
    Serial.println("[INFO] LED: kirim Sheet sukses (hijau+biru kedip).");

    for (uint8_t u8 = 0; u8 < LED_SEND_OK_COUNT; u8++)
    {
        vSetLedPin(PIN_LED_GREEN, true);
        vSetLedPin(PIN_LED_BLUE, true);
        vSetLedPin(PIN_LED_YELLOW, false);
        vSetLedPin(PIN_LED_RED, false);
        delay(LED_SEND_OK_MS);
        vSetLedPin(PIN_LED_GREEN, false);
        vSetLedPin(PIN_LED_BLUE, false);
        delay(LED_SEND_OK_MS);
        vFeedWatchdog();
    }
}

/**
 * Mematikan semua pin trigger lalu menguncinya LOW selama deep sleep.
 *
 * Pin trigger (saklar MOSFET, active HIGH):
 *   RTC_T     GPIO33 — dimatikan. Jam DS3231 dipegang baterai eksternal.
 *   SENSOR_T  GPIO17 — dimatikan. Rail VEML7700 putus.
 *   SDCARD_T  GPIO16 — sudah mati (LittleFS, bukan microSD).
 * LED1..LED4 juga LOW supaya tidak menyala saat tidur.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: rail VCC_RTC, VEML7700, dan +SDCard mati sampai node bangun lagi.
 */
void vHoldOutputsForSleep()
{
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_BLUE, LOW);
    digitalWrite(PIN_PWR_RTC, LOW);
    digitalWrite(PIN_PWR_SENSOR, LOW);
    digitalWrite(PIN_PWR_SDCARD, LOW);

    gpio_hold_en((gpio_num_t)PIN_LED_GREEN);
    gpio_hold_en((gpio_num_t)PIN_LED_YELLOW);
    gpio_hold_en((gpio_num_t)PIN_LED_RED);
    gpio_hold_en((gpio_num_t)PIN_LED_BLUE);
    gpio_hold_en((gpio_num_t)PIN_PWR_RTC);
    gpio_hold_en((gpio_num_t)PIN_PWR_SENSOR);
    gpio_hold_en((gpio_num_t)PIN_PWR_SDCARD);
    gpio_deep_sleep_hold_en();

    Serial.println(
        "[INFO] Sleep: semua pin trigger LOW "
        "(RTC_T 33, SENSOR_T 17, SD_T 16, LED 27/25/32/26)."
    );
}

/**
 * Membaca ADC baterai GPIO39 beberapa kali.
 *
 * pfAdc                 - rata-rata hitungan ADC 12 bit.
 * pfPinVolt             - tegangan di pin, sebelum pembagi.
 * pfPackRawVolt         - tegangan paket setelah pembagi, sebelum y=m*x+c.
 * pfPackCalibratedVolt  - tegangan paket setelah kalibrasi.
 *
 * Output: Tidak ada. Pointer null dilewati.
 * Side effect: Membaca ADC.
 */
void vReadBatteryAdcDetail(
    float* pfAdc,
    float* pfPinVolt,
    float* pfPackRawVolt,
    float* pfPackCalibratedVolt
)
{
    uint32_t u32Sum = 0;
    const int iSamples = 20;

    for (int i = 0; i < iSamples; i++)
    {
        u32Sum += (uint32_t)analogRead(PIN_ADC_BATTERY);
        delay(2);
    }

    float fAdc = (float)u32Sum / (float)iSamples;
    float fPinVolt = (fAdc / BATTERY_ADC_FULL_SCALE) * BATTERY_ADC_VREF_V;
    float fRatio =
        (BATTERY_R_TOP_OHM + BATTERY_R_BOT_OHM) / BATTERY_R_BOT_OHM;
    float fPackRaw = fPinVolt * fRatio;
    float fPackCal = fApplyCalibration(
        fPackRaw,
        g_calibration.fBatteryM,
        g_calibration.fBatteryC
    );

    if (pfAdc != nullptr)
    {
        *pfAdc = fAdc;
    }
    if (pfPinVolt != nullptr)
    {
        *pfPinVolt = fPinVolt;
    }
    if (pfPackRawVolt != nullptr)
    {
        *pfPackRawVolt = fPackRaw;
    }
    if (pfPackCalibratedVolt != nullptr)
    {
        *pfPackCalibratedVolt = fPackCal;
    }
}

/**
 * Membaca tegangan baterai setelah pembagi resistor, lalu kalibrasi y=m*x+c.
 *
 * Skema: net BMS ke GPIO39. Rasio default 2.0 (R atas = R bawah).
 * Jika resistor di PCB berbeda, ubah BATTERY_R_TOP_OHM / BATTERY_R_BOT_OHM.
 *
 * Input: Tidak ada.
 * Output: Tegangan baterai dalam Volt (setelah kalibrasi).
 */
float fReadBatteryVoltageCalibrated()
{
    float fPackCalibrated = 0.0f;
    vReadBatteryAdcDetail(nullptr, nullptr, nullptr, &fPackCalibrated);
    return fPackCalibrated;
}
