// =====================================================
// Calibration.ino
// Koreksi linier y = m * x + c di NVS (Preferences)
// Channel: lux, temp, rh, co2, battery
// Sensor default m=1, c=0 (tidak mengubah bacaan).
// Baterai memakai BATTERY_CAL_DEFAULT_M / C dari pengukuran multimeter.
// V2.3.0: mode gain VEML7700 ikut disimpan di namespace yang sama.
// Semua nilai di sini hanya diubah dari HMI Spreadsheet (cold power),
// halaman AP hanya menampilkan.
// =====================================================

CalibrationSet g_calibration = {
    1.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 0.0f,
    BATTERY_CAL_DEFAULT_M, BATTERY_CAL_DEFAULT_C,
    VEML_GAIN_MODE_DEFAULT
};

static Preferences g_prefsCalib;
static bool g_bCalibNvsOpen = false;

/**
 * Menerapkan kalibrasi linier.
 *
 * EDIT PERSAMAAN HANYA DI FUNCTION INI.
 * y = m * x + c
 * x = bacaan mentah sensor atau tegangan baterai setelah pembagi
 * y = nilai yang disimpan ke CSV dan dikirim ke Sheet
 *
 * Input:
 * fRaw - nilai mentah.
 * fM   - slope.
 * fC   - offset.
 *
 * Output: nilai terkoreksi.
 */
float fApplyCalibration(float fRaw, float fM, float fC)
{
    return (fM * fRaw) + fC;
}

/**
 * Mengecek teks mode gain VEML7700.
 *
 * Input:
 * szMode - teks dari Sheet atau NVS.
 *
 * Output:
 * true jika salah satu dari "auto", "2", "1", "1_4", "1_8".
 */
bool bIsVemlGainModeValid(const char* szMode)
{
    if (szMode == nullptr)
    {
        return false;
    }

    return (strcmp(szMode, VEML_GAIN_MODE_AUTO) == 0)
        || (strcmp(szMode, "2") == 0)
        || (strcmp(szMode, "1") == 0)
        || (strcmp(szMode, "1_4") == 0)
        || (strcmp(szMode, "1_8") == 0);
}

/**
 * Apakah mode gain tersimpan adalah autorange.
 *
 * Input: Tidak ada.
 * Output: true jika g_calibration.szVemlGainMode == "auto".
 */
bool bIsVemlGainModeAuto()
{
    return (strcmp(g_calibration.szVemlGainMode, VEML_GAIN_MODE_AUTO) == 0);
}

/**
 * Mengisi default kalibrasi.
 * Sensor: m=1, c=0.
 * Baterai: hasil regresi multimeter vs ADC, dipakai selama NVS kosong.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
static void vSetDefaultCalibration()
{
    g_calibration.fLuxM = 1.0f;
    g_calibration.fLuxC = 0.0f;
    g_calibration.fTempM = 1.0f;
    g_calibration.fTempC = 0.0f;
    g_calibration.fRhM = 1.0f;
    g_calibration.fRhC = 0.0f;
    g_calibration.fCo2M = 1.0f;
    g_calibration.fCo2C = 0.0f;
    g_calibration.fBatteryM = BATTERY_CAL_DEFAULT_M;
    g_calibration.fBatteryC = BATTERY_CAL_DEFAULT_C;
    snprintf(
        g_calibration.szVemlGainMode,
        sizeof(g_calibration.szVemlGainMode),
        "%s",
        VEML_GAIN_MODE_DEFAULT
    );
}

/**
 * Memuat mode gain VEML7700 dari NVS.
 * Key belum ada (NVS dari firmware lama) atau isi tidak sah -> tetap "auto".
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Mengubah g_calibration.szVemlGainMode.
 */
static void vLoadVemlGainModeFromNvs()
{
    if (!g_prefsCalib.isKey(PREFS_KEY_VEML_GAIN))
    {
        return;
    }

    char szMode[VEML_GAIN_MODE_TEXT_MAX];
    g_prefsCalib.getString(PREFS_KEY_VEML_GAIN, szMode, sizeof(szMode));

    if (!bIsVemlGainModeValid(szMode))
    {
        Serial.printf(
            "[WARNING] Mode gain VEML di NVS tidak sah (%s). Pakai autorange.\n",
            szMode
        );
        return;
    }

    snprintf(
        g_calibration.szVemlGainMode,
        sizeof(g_calibration.szVemlGainMode),
        "%s",
        szMode
    );
}

/**
 * Membuka NVS kalibrasi dan memuat faktor. Jika kosong, pakai default.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitCalibration()
{
    vSetDefaultCalibration();

    if (!g_prefsCalib.begin(PREFS_NAMESPACE_CALIB, false))
    {
        g_bCalibNvsOpen = false;
        Serial.println(
            "[WARNING] NVS kalibrasi gagal dibuka. "
            "Sensor m=1 c=0, baterai pakai default pengukuran."
        );
        return;
    }

    g_bCalibNvsOpen = true;

    // Mode gain dibaca terpisah dari faktor m/c karena NVS lama bisa
    // punya faktor tanpa key veml_gain (atau sebaliknya).
    vLoadVemlGainModeFromNvs();
    Serial.printf(
        "[INFO] Mode gain VEML7700 tersimpan: %s\n",
        g_calibration.szVemlGainMode
    );

    if (!g_prefsCalib.isKey("lux_m"))
    {
        Serial.printf(
            "[INFO] Kalibrasi belum disimpan. Sensor m=1 c=0. "
            "Baterai default m=%.6f c=%.6f\n",
            g_calibration.fBatteryM,
            g_calibration.fBatteryC
        );
        return;
    }

    g_calibration.fLuxM = g_prefsCalib.getFloat("lux_m", 1.0f);
    g_calibration.fLuxC = g_prefsCalib.getFloat("lux_c", 0.0f);
    g_calibration.fTempM = g_prefsCalib.getFloat("temp_m", 1.0f);
    g_calibration.fTempC = g_prefsCalib.getFloat("temp_c", 0.0f);
    g_calibration.fRhM = g_prefsCalib.getFloat("rh_m", 1.0f);
    g_calibration.fRhC = g_prefsCalib.getFloat("rh_c", 0.0f);
    g_calibration.fCo2M = g_prefsCalib.getFloat("co2_m", 1.0f);
    g_calibration.fCo2C = g_prefsCalib.getFloat("co2_c", 0.0f);
    g_calibration.fBatteryM = g_prefsCalib.getFloat("bat_m", BATTERY_CAL_DEFAULT_M);
    g_calibration.fBatteryC = g_prefsCalib.getFloat("bat_c", BATTERY_CAL_DEFAULT_C);

    Serial.printf(
        "[INFO] Kalibrasi NVS: lux m=%.4f c=%.3f | T m=%.4f c=%.3f | "
        "RH m=%.4f c=%.3f | CO2 m=%.4f c=%.3f | bat m=%.4f c=%.3f\n",
        g_calibration.fLuxM, g_calibration.fLuxC,
        g_calibration.fTempM, g_calibration.fTempC,
        g_calibration.fRhM, g_calibration.fRhC,
        g_calibration.fCo2M, g_calibration.fCo2C,
        g_calibration.fBatteryM, g_calibration.fBatteryC
    );
}

/**
 * Menyimpan g_calibration ke NVS.
 *
 * Input: Tidak ada.
 * Output: true jika namespace terbuka.
 */
bool bSaveCalibration()
{
    if (!g_bCalibNvsOpen)
    {
        g_bCalibNvsOpen = g_prefsCalib.begin(PREFS_NAMESPACE_CALIB, false);
    }

    if (!g_bCalibNvsOpen)
    {
        Serial.println("[ERROR] NVS kalibrasi tidak terbuka. Tidak tersimpan.");
        return false;
    }

    g_prefsCalib.putFloat("lux_m", g_calibration.fLuxM);
    g_prefsCalib.putFloat("lux_c", g_calibration.fLuxC);
    g_prefsCalib.putFloat("temp_m", g_calibration.fTempM);
    g_prefsCalib.putFloat("temp_c", g_calibration.fTempC);
    g_prefsCalib.putFloat("rh_m", g_calibration.fRhM);
    g_prefsCalib.putFloat("rh_c", g_calibration.fRhC);
    g_prefsCalib.putFloat("co2_m", g_calibration.fCo2M);
    g_prefsCalib.putFloat("co2_c", g_calibration.fCo2C);
    g_prefsCalib.putFloat("bat_m", g_calibration.fBatteryM);
    g_prefsCalib.putFloat("bat_c", g_calibration.fBatteryC);
    g_prefsCalib.putString(PREFS_KEY_VEML_GAIN, g_calibration.szVemlGainMode);

    Serial.println("[INFO] Kalibrasi tersimpan di NVS.");
    return true;
}
