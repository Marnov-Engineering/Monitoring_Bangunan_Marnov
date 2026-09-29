// =====================================================
// Sensor.ino
// I2C, VEML7700 (Node A), SCD41 datasheet (Node B)
// =====================================================

// true setelah begin() sukses, supaya sleep bisa kirim perintah low-power
// hanya ke chip yang benar-benar sudah diajak bicara.
static bool g_bVemlSessionOpen = false;
static bool g_bScd41SessionOpen = false;

// Alamat I2C VEML7700 (datasheet / library Adafruit).
#define VEML7700_I2C_ADDR 0x10

/**
 * Mencetak error library Sensirion tanpa objek String.
 *
 * Input:
 * szContext - nama perintah yang gagal.
 * i16Error  - kode error library.
 *
 * Output: Tidak mengembalikan nilai.
 */
static void vLogSensirionError(const char* szContext, int16_t i16Error)
{
    char szError[80];
    errorToString(i16Error, szError, sizeof(szError));

    Serial.printf(
        "[ERROR] SCD41 %s gagal (kode %d): %s\n",
        szContext,
        (int)i16Error,
        szError
    );
}

/**
 * Memindai alamat I2C 0x01..0x7E untuk troubleshooting.
 *
 * Input: Tidak ada.
 * Output: Tidak mengembalikan nilai.
 */
void vScanI2CBus()
{
    Serial.println("[INFO] Scan I2C...");

    uint8_t u8Found = 0;

    for (uint8_t u8Address = 1; u8Address < 127; u8Address++)
    {
        Wire.beginTransmission(u8Address);
        uint8_t u8Error = Wire.endTransmission();

        if (u8Error == 0)
        {
            Serial.printf("[INFO] Perangkat I2C di 0x%02X\n", u8Address);
            u8Found++;
        }
    }

    if (u8Found == 0)
    {
        Serial.println(
            "[WARNING] Tidak ada perangkat I2C. "
            "Periksa VCC 3.3V, GND, SDA, SCL."
        );
    }
    else
    {
        Serial.printf("[INFO] Total perangkat I2C: %u\n", u8Found);
    }
}

/**
 * Menginisialisasi bus I2C ESP32.
 *
 * Input: Tidak ada.
 * Output: Tidak mengembalikan nilai.
 */
void vInitI2C()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    Serial.printf(
        "[INFO] I2C SDA=%d SCL=%d clock=%lu Hz\n",
        PIN_I2C_SDA,
        PIN_I2C_SCL,
        (unsigned long)I2C_CLOCK_HZ
    );

    vScanI2CBus();
}

/**
 * Menyesuaikan gain VEML7700 agar ALS tidak saturasi.
 *
 * Input:
 * u16Als - nilai raw ALS.
 *
 * Output: Tidak mengembalikan nilai.
 */
static void vApplyVemlAutoRange(uint16_t u16Als)
{
    uint8_t u8Gain    = g_veml7700.getGain();
    uint8_t u8NewGain = u8Gain;

    if (u16Als > ALS_SATURATION_HIGH)
    {
        if (u8Gain == VEML7700_GAIN_2)
        {
            u8NewGain = VEML7700_GAIN_1;
        }
        else if (u8Gain == VEML7700_GAIN_1)
        {
            u8NewGain = VEML7700_GAIN_1_4;
        }
        else if (u8Gain == VEML7700_GAIN_1_4)
        {
            u8NewGain = VEML7700_GAIN_1_8;
        }
    }
    else if (u16Als < ALS_SATURATION_LOW)
    {
        if (u8Gain == VEML7700_GAIN_1_8)
        {
            u8NewGain = VEML7700_GAIN_1_4;
        }
        else if (u8Gain == VEML7700_GAIN_1_4)
        {
            u8NewGain = VEML7700_GAIN_1;
        }
        else if (u8Gain == VEML7700_GAIN_1)
        {
            u8NewGain = VEML7700_GAIN_2;
        }
    }

    if (u8NewGain != u8Gain)
    {
        g_veml7700.setGain(u8NewGain);
        Serial.printf(
            "[INFO] VEML7700 auto-range gain berubah (ALS=%u)\n",
            (unsigned)u16Als
        );
    }
}

/**
 * Mengubah teks mode gain (NVS/Sheet) menjadi konstanta gain library.
 * Mode "auto" memakai gain awal 1/8x (paling tidak sensitif) seperti
 * firmware sebelumnya, lalu vApplyVemlAutoRange() menyesuaikan.
 *
 * Input:
 * szMode - "auto", "2", "1", "1_4", atau "1_8".
 *
 * Output:
 * Konstanta VEML7700_GAIN_x. Teks tidak dikenal -> VEML7700_GAIN_1_8.
 */
uint8_t u8VemlGainFromMode(const char* szMode)
{
    if (szMode == nullptr)
    {
        return VEML7700_GAIN_1_8;
    }
    if (strcmp(szMode, "2") == 0)
    {
        return VEML7700_GAIN_2;
    }
    if (strcmp(szMode, "1") == 0)
    {
        return VEML7700_GAIN_1;
    }
    if (strcmp(szMode, "1_4") == 0)
    {
        return VEML7700_GAIN_1_4;
    }
    return VEML7700_GAIN_1_8;
}

/**
 * Label gain untuk log dan halaman AP.
 *
 * Input:
 * u8Gain - konstanta VEML7700_GAIN_x (hasil getGain()).
 *
 * Output:
 * Teks konstan "2x", "1x", "1/4x", "1/8x", atau "?".
 */
const char* pszVemlGainLabel(uint8_t u8Gain)
{
    if (u8Gain == VEML7700_GAIN_2)
    {
        return "2x";
    }
    if (u8Gain == VEML7700_GAIN_1)
    {
        return "1x";
    }
    if (u8Gain == VEML7700_GAIN_1_4)
    {
        return "1/4x";
    }
    if (u8Gain == VEML7700_GAIN_1_8)
    {
        return "1/8x";
    }
    return "?";
}

/**
 * Menginisialisasi sensor sesuai jenis node.
 *
 * Input: Tidak ada.
 * Output: true jika hardware siap.
 */
bool bInitSensorHardware()
{
    if (bIsNodeTypeA())
    {
        if (!g_veml7700.begin())
        {
            g_bSensorHardwareOk = false;
            Serial.println(
                "[ERROR] VEML7700 tidak terdeteksi (alamat 0x10)."
            );
            return false;
        }

        // Gain awal dari mode tersimpan (NVS, asal HMI Spreadsheet).
        // Autorange mulai dari 1/8x; gain tetap langsung dipasang dan
        // tidak diubah lagi selama siklus.
        uint8_t u8StartGain = u8VemlGainFromMode(g_calibration.szVemlGainMode);
        g_veml7700.setGain(u8StartGain);
        g_veml7700.setIntegrationTime(VEML7700_IT_100MS);
        g_veml7700.enable(true);

        g_bVemlSessionOpen = true;
        g_bSensorHardwareOk = true;
        Serial.printf(
            "[INFO] VEML7700 siap. Mode gain=%s, gain awal=%s.\n",
            bIsVemlGainModeAuto() ? "autorange" : "tetap",
            pszVemlGainLabel(u8StartGain)
        );
        return true;
    }

    g_scd41.begin(Wire, SCD41_I2C_ADDR_62);
    g_bScd41SessionOpen = true;

    // Dari powerDown siklus sebelumnya. wakeUp tidak di-ACK oleh SCD41.
    int16_t i16Error = g_scd41.wakeUp();
    if (i16Error != NO_ERROR)
    {
        vLogSensirionError("wakeUp", i16Error);
    }

    // Jika firmware lama sempat menyalakan mode periodic, kembalikan ke idle.
    // Error di sini normal bila sensor memang sudah idle.
    i16Error = g_scd41.stopPeriodicMeasurement();
    if (i16Error == NO_ERROR)
    {
        delay(SCD41_STOP_WAIT_MS);
        vFeedWatchdog();
    }

    uint64_t u64Serial = 0;
    i16Error = g_scd41.getSerialNumber(u64Serial);
    if (i16Error != NO_ERROR)
    {
        g_bSensorHardwareOk = false;
        vLogSensirionError("getSerialNumber", i16Error);
        Serial.println(
            "[ERROR] SCD41 tidak terdeteksi (alamat 0x62)."
        );
        return false;
    }

    g_bSensorHardwareOk = true;
    Serial.println(
        "[INFO] SCD41 siap. Mode single shot (~5 detik), "
        "satu bacaan langsung dipakai."
    );
    return true;
}

/**
 * Membaca satu measurement SCD41 setelah single shot selesai.
 *
 * Input:
 * pu16Co2 - pointer CO2 ppm.
 * pfTemp  - pointer suhu.
 * pfRh    - pointer RH.
 *
 * Output: true jika baca berhasil dan CO2 != 0.
 */
static bool bReadScd41Once(uint16_t* pu16Co2, float* pfTemp, float* pfRh)
{
    uint16_t u16Co2 = 0;
    float fTemp     = 0.0f;
    float fRh       = 0.0f;

    int16_t i16Error = g_scd41.readMeasurement(u16Co2, fTemp, fRh);
    if (i16Error != NO_ERROR)
    {
        vLogSensirionError("readMeasurement", i16Error);
        return false;
    }

    if (u16Co2 == 0 || isnan(fTemp) || isnan(fRh))
    {
        Serial.println("[WARNING] SCD41 sample belum lengkap.");
        return false;
    }

    *pu16Co2 = u16Co2;
    *pfTemp  = fTemp;
    *pfRh    = fRh;
    return true;
}

/**
 * Membaca Node A (VEML7700).
 *
 * Input:
 * pData - pointer hasil.
 *
 * Output: true jika baca berhasil.
 */
bool bReadNodeASensor(NodeAData* pData)
{
    if (pData == nullptr)
    {
        return false;
    }

    if (!g_bSensorHardwareOk)
    {
        return false;
    }

    float fLux        = g_veml7700.readLux();
    uint16_t u16Als   = g_veml7700.readALS();
    g_veml7700.readWhite();

    if (isnan(fLux) || fLux < 0.0f)
    {
        Serial.println("[ERROR] Pembacaan lux tidak valid.");
        return false;
    }

    // Autorange hanya jika mode tersimpan "auto".
    // Mode gain tetap: satu bacaan di atas sudah final (gain tidak berubah).
    if (bIsVemlGainModeAuto())
    {
        vApplyVemlAutoRange(u16Als);

        // Baca ulang setelah auto-range agar lux sesuai gain baru.
        fLux = g_veml7700.readLux();
        if (isnan(fLux) || fLux < 0.0f)
        {
            return false;
        }
    }

    pData->fIlluminanceLux = fApplyCalibration(
        fLux,
        g_calibration.fLuxM,
        g_calibration.fLuxC
    );
    pData->u8IlluminanceValid = 1;
    pData->u8SensorStatus     = 0;
    pData->fBatteryVoltage    = fReadBatteryVoltageCalibrated();

    return true;
}

/**
 * Membaca Node B dengan satu single shot SCD41.
 * Perintah 0x219D, selesai sekitar 5 detik, hasil langsung dipakai.
 *
 * Input:
 * pData - pointer hasil.
 *
 * Output: true jika bacaan berhasil.
 * Side effect: Jika gagal, pData boleh diisi caller dengan valid=0.
 */
bool bReadNodeBSensor(NodeBData* pData)
{
    if (pData == nullptr)
    {
        return false;
    }

    if (!g_bSensorHardwareOk)
    {
        return false;
    }

    vFeedWatchdog();

    uint32_t u32StartMs = millis();
    int16_t i16Error = g_scd41.measureSingleShot();
    vFeedWatchdog();

    if (i16Error != NO_ERROR)
    {
        vLogSensirionError("measureSingleShot", i16Error);
        return false;
    }

    if ((millis() - u32StartMs) > SCD41_SINGLE_SHOT_TIMEOUT_MS)
    {
        Serial.println("[ERROR] SCD41 single shot lebih lama dari 6 detik.");
        return false;
    }

    uint16_t u16Co2 = 0;
    float fTemp     = 0.0f;
    float fRh       = 0.0f;

    if (!bReadScd41Once(&u16Co2, &fTemp, &fRh))
    {
        Serial.println("[WARNING] Gagal baca single shot SCD41.");
        return false;
    }

    Serial.printf(
        "[INFO] SCD41 single shot: CO2=%u T=%.2f RH=%.1f (%lu ms)\n",
        (unsigned)u16Co2,
        fTemp,
        fRh,
        (unsigned long)(millis() - u32StartMs)
    );

    pData->fAirTemperatureC = fApplyCalibration(
        fTemp,
        g_calibration.fTempM,
        g_calibration.fTempC
    );
    pData->u8TempValid = 1;
    pData->fRelativeHumidityPct = fApplyCalibration(
        fRh,
        g_calibration.fRhM,
        g_calibration.fRhC
    );
    pData->u8RhValid = 1;
    pData->fCo2Ppm = fApplyCalibration(
        (float)u16Co2,
        g_calibration.fCo2M,
        g_calibration.fCo2C
    );
    pData->u8Co2Valid = 1;
    pData->u8SensorStatus = 0;
    pData->fBatteryVoltage = fReadBatteryVoltageCalibrated();
    return true;
}

/**
 * Mengecek apakah sebuah alamat I2C menjawab ACK.
 *
 * Input: u8Address - alamat 7-bit.
 * Output: true jika ACK.
 */
static bool bI2cAddressAck(uint8_t u8Address)
{
    Wire.beginTransmission(u8Address);
    return (Wire.endTransmission() == 0);
}

/**
 * Menulis ALS_SD = 1 pada VEML7700 (datasheet Vishay, register 00h bit 0).
 * Arus shutdown tipikal 0,5 µA selama VCC masih ada.
 * Dipanggil selagi SENSOR_T (GPIO17) masih HIGH.
 * Setelah shutdown I2C, sleep menurunkan SENSOR_T supaya rail VEML putus.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
static void vVemlEnterShutdown()
{
    if (!g_bVemlSessionOpen)
    {
        if (!bI2cAddressAck(VEML7700_I2C_ADDR))
        {
            return;
        }

        if (!g_veml7700.begin())
        {
            return;
        }

        g_bVemlSessionOpen = true;
    }

    // enable(false) menulis ALS_SD = 1.
    g_veml7700.enable(false);
    g_bVemlSessionOpen = false;
    Serial.println(
        "[INFO] VEML7700 shutdown (ALS_SD=1, tipikal 0.5 uA) sebelum sleep."
    );
}

/**
 * SCD41 single shot sudah kembali ke idle.
 * stopPeriodic hanya pengaman jika mode periodic masih aktif.
 * Lalu powerDown (perintah 0x36E0). Arus sleep sekitar 2,5 µA.
 * Bangun berikutnya memakai wakeUp() di bInitSensorHardware().
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
static void vScd41EnterPowerDown()
{
    if (!g_bScd41SessionOpen)
    {
        if (!bI2cAddressAck(SCD41_I2C_ADDR_62))
        {
            return;
        }

        g_scd41.begin(Wire, SCD41_I2C_ADDR_62);
        g_bScd41SessionOpen = true;
    }

    int16_t i16Error = g_scd41.stopPeriodicMeasurement();
    if (i16Error == NO_ERROR)
    {
        // Datasheet: tunggu 500 ms setelah stop sebelum perintah lain.
        delay(SCD41_STOP_WAIT_MS);
        vFeedWatchdog();
    }

    i16Error = g_scd41.powerDown();
    g_bScd41SessionOpen = false;

    if (i16Error != NO_ERROR)
    {
        vLogSensirionError("powerDown", i16Error);
        return;
    }

    Serial.println("[INFO] SCD41 power down (~2.5 uA) sebelum sleep.");
}

/**
 * Menandai bacaan Node A gagal.
 * Kolom lux tidak diisi (valid = 0). Baterai tetap dari ADC.
 *
 * Input: pData - hasil yang akan ditulis ke CSV.
 * Output: Tidak ada.
 */
void vMarkNodeASensorUnread(NodeAData* pData)
{
    if (pData == nullptr)
    {
        return;
    }

    pData->fIlluminanceLux    = 0.0f;
    pData->u8IlluminanceValid = 0;
    pData->u8SensorStatus     = 1;
    pData->fBatteryVoltage    = fReadBatteryVoltageCalibrated();

    Serial.println(
        "[WARNING] VEML7700 tidak terbaca. Kolom lux dikosongkan."
    );
}

/**
 * Menandai bacaan Node B gagal.
 * Kolom suhu, RH, dan CO2 tidak diisi (valid = 0). Baterai tetap dari ADC.
 *
 * Input: pData - hasil yang akan ditulis ke CSV.
 * Output: Tidak ada.
 */
void vMarkNodeBSensorUnread(NodeBData* pData)
{
    if (pData == nullptr)
    {
        return;
    }

    pData->fAirTemperatureC     = 0.0f;
    pData->u8TempValid          = 0;
    pData->fRelativeHumidityPct = 0.0f;
    pData->u8RhValid            = 0;
    pData->fCo2Ppm              = 0.0f;
    pData->u8Co2Valid           = 0;
    pData->u8SensorStatus       = 1;
    pData->fBatteryVoltage      = fReadBatteryVoltageCalibrated();

    Serial.println(
        "[WARNING] SCD41 tidak terbaca. Kolom suhu, RH, dan CO2 dikosongkan."
    );
}

/**
 * Menurunkan konsumsi sensor sebelum deep sleep.
 * Perintah I2C dikirim SELAGI SENSOR_T masih HIGH.
 * Setelah fungsi ini, sleep menurunkan SENSOR_T (GPIO17) dan RTC_T (GPIO33).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vShutdownSensorsForSleep()
{
    vVemlEnterShutdown();
    vScd41EnterPowerDown();
}

// =====================================================
// REALTIME AP (V2.3.0) — data mentah untuk kalibrasi lapangan
// =====================================================

// Snapshot terakhir. Ditulis vUpdateSensorRealtime() (task loop),
// dibaca vGetSensorRealtimeSnapshot() (task AsyncTCP).
// Disalin utuh di dalam critical section agar tidak terbaca setengah jadi.
static SensorRealtime g_sensorRealtime = {};
static portMUX_TYPE   g_muxSensorRealtime = portMUX_INITIALIZER_UNLOCKED;

// millis() saat halaman terakhir meminta data realtime.
// 0 = belum pernah diminta -> loop tidak membaca sensor.
static volatile uint32_t g_u32RealtimeRequestMs = 0;
static uint32_t          g_u32RealtimeLastReadMs = 0;

/**
 * Menandai bahwa halaman realtime sedang dibuka.
 * Dipanggil handler /api/lux dan /api/env setiap kali di-poll.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: loop() mulai/lanjut membaca sensor selama
 *              REALTIME_IDLE_STOP_MS sejak panggilan ini.
 */
void vRequestSensorRealtime()
{
    uint32_t u32Now = millis();

    // 0 dipakai sebagai "belum diminta", jadi hindari nilai 0 tepat.
    if (u32Now == 0)
    {
        u32Now = 1;
    }
    g_u32RealtimeRequestMs = u32Now;
}

/**
 * Menyalin snapshot realtime terakhir.
 *
 * Input:
 * pOut - tujuan salinan.
 *
 * Output: Tidak ada.
 */
void vGetSensorRealtimeSnapshot(SensorRealtime* pOut)
{
    if (pOut == nullptr)
    {
        return;
    }

    portENTER_CRITICAL(&g_muxSensorRealtime);
    *pOut = g_sensorRealtime;
    portEXIT_CRITICAL(&g_muxSensorRealtime);
}

/**
 * Membaca VEML7700 satu kali pada gain yang sedang aktif.
 * Tidak menjalankan autorange: angka mentah harus stabil saat dibandingkan
 * dengan luxmeter referensi. Gain ikut dilaporkan agar data kalibrasi
 * tercatat per gain.
 *
 * Input:
 * pNext - snapshot yang diisi.
 *
 * Output: true jika bacaan sah.
 */
static bool bReadVemlRealtime(SensorRealtime* pNext)
{
    float fLux      = g_veml7700.readLux();
    uint16_t u16Als = g_veml7700.readALS();

    if (isnan(fLux) || fLux < 0.0f)
    {
        Serial.println("[WARNING] Realtime VEML7700: lux tidak valid.");
        return false;
    }

    pNext->u16Als  = u16Als;
    pNext->fLuxRaw = fLux;
    pNext->u8Gain  = g_veml7700.getGain();
    return true;
}

/**
 * Menjalankan satu single shot SCD41 (~5 detik, blocking).
 *
 * Input:
 * pNext - snapshot yang diisi.
 *
 * Output: true jika bacaan sah.
 */
static bool bReadScd41Realtime(SensorRealtime* pNext)
{
    vFeedWatchdog();
    int16_t i16Error = g_scd41.measureSingleShot();
    vFeedWatchdog();

    if (i16Error != NO_ERROR)
    {
        vLogSensirionError("measureSingleShot (realtime)", i16Error);
        return false;
    }

    uint16_t u16Co2 = 0;
    float fTemp     = 0.0f;
    float fRh       = 0.0f;

    if (!bReadScd41Once(&u16Co2, &fTemp, &fRh))
    {
        return false;
    }

    pNext->u16Co2Raw = u16Co2;
    pNext->fTempRaw  = fTemp;
    pNext->fRhRaw    = fRh;
    return true;
}

/**
 * Membaca sensor untuk halaman realtime bila sedang dibuka.
 * Dipanggil dari loop() saat mode AP. Tidak membaca apa pun jika
 * halaman realtime tidak di-poll dalam REALTIME_IDLE_STOP_MS terakhir.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Akses I2C ke VEML7700 / SCD41, memperbarui g_sensorRealtime.
 *              Node B memblokir loop ~5 detik per pengukuran.
 */
void vUpdateSensorRealtime()
{
    uint32_t u32RequestMs = g_u32RealtimeRequestMs;
    if (u32RequestMs == 0)
    {
        return;
    }

    uint32_t u32Now = millis();
    if (u32Now - u32RequestMs >= REALTIME_IDLE_STOP_MS)
    {
        return;
    }

    if (!g_bSensorHardwareOk)
    {
        return;
    }

    bool bNodeA = bIsNodeTypeA();
    uint32_t u32IntervalMs = bNodeA ? REALTIME_LUX_INTERVAL_MS : REALTIME_ENV_INTERVAL_MS;

    // Bacaan pertama langsung dilakukan; berikutnya mengikuti interval.
    if ((g_u32RealtimeLastReadMs != 0) && (u32Now - g_u32RealtimeLastReadMs < u32IntervalMs))
    {
        return;
    }
    g_u32RealtimeLastReadMs = u32Now;
    if (g_u32RealtimeLastReadMs == 0)
    {
        g_u32RealtimeLastReadMs = 1;
    }

    SensorRealtime tNext;
    vGetSensorRealtimeSnapshot(&tNext);

    bool bOk = bNodeA ? bReadVemlRealtime(&tNext) : bReadScd41Realtime(&tNext);

    tNext.bLastOk = bOk;
    if (bOk)
    {
        tNext.bValid = true;
        tNext.u32UpdatedMs = millis();
        tNext.u32SampleCount++;
    }

    portENTER_CRITICAL(&g_muxSensorRealtime);
    g_sensorRealtime = tNext;
    portEXIT_CRITICAL(&g_muxSensorRealtime);
}
