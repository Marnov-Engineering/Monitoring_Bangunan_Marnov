// =====================================================
// Rtc.ino
// DS3231 pada I2C yang sama (SDA 21 / SCL 22), alamat 0x68
// Jam disimpan sebagai UTC. NTP menimpa RTC bila WiFi berhasil.
// =====================================================

#define DS3231_I2C_ADDR  0x68

/**
 * Mengubah satu byte BCD DS3231 ke biner.
 *
 * Input: u8Bcd
 * Output: nilai 0-99.
 */
static uint8_t u8BcdToBin(uint8_t u8Bcd)
{
    return (uint8_t)((u8Bcd >> 4) * 10 + (u8Bcd & 0x0F));
}

/**
 * Mengubah biner 0-99 ke BCD.
 *
 * Input: u8Bin
 * Output: BCD.
 */
static uint8_t u8BinToBcd(uint8_t u8Bin)
{
    return (uint8_t)(((u8Bin / 10) << 4) | (u8Bin % 10));
}

/**
 * Mengubah kalender UTC menjadi Unix epoch.
 * Tidak memakai mktime, karena mktime menganggap waktu lokal.
 *
 * Input: pUtc - struct tm yang sudah diisi sebagai UTC.
 * Output: epoch detik.
 */
static time_t tEpochFromUtcCalendar(const struct tm* pUtc)
{
    int iYear = pUtc->tm_year + 1900;
    int iMonth = pUtc->tm_mon + 1;
    int iDay = pUtc->tm_mday;

    iYear -= (iMonth <= 2) ? 1 : 0;
    const int iEra = (iYear >= 0 ? iYear : iYear - 399) / 400;
    const unsigned uYoe = (unsigned)(iYear - iEra * 400);
    const unsigned uDoy =
        (153U * (unsigned)(iMonth + (iMonth > 2 ? -3 : 9)) + 2U) / 5U
        + (unsigned)iDay - 1U;
    const unsigned uDoe = uYoe * 365U + uYoe / 4U - uYoe / 100U + uDoy;
    const int iDays = (int)(iEra * 146097 + (int)uDoe - 719468);

    return ((time_t)iDays * 86400)
        + (time_t)pUtc->tm_hour * 3600
        + (time_t)pUtc->tm_min * 60
        + (time_t)pUtc->tm_sec;
}

/**
 * Apakah DS3231 menjawab di alamat 0x68.
 *
 * Input: Tidak ada.
 * Output: true jika ACK.
 */
static bool bDs3231Present()
{
    Wire.beginTransmission(DS3231_I2C_ADDR);
    return (Wire.endTransmission() == 0);
}

/**
 * Menulis epoch UTC sistem ke DS3231.
 *
 * Input: Tidak ada. Membaca time().
 * Output: true jika tulis I2C sukses.
 */
bool bRtcSaveSystemTime()
{
    if (!bDs3231Present())
    {
        Serial.println("[WARNING] DS3231 tidak terdeteksi. RTC tidak disimpan.");
        return false;
    }

    time_t tNow;
    time(&tNow);
    struct tm tUtc;
    gmtime_r(&tNow, &tUtc);

    if (tUtc.tm_year < 120)
    {
        Serial.println("[WARNING] Waktu sistem belum valid. RTC tidak ditimpa.");
        return false;
    }

    uint8_t au8Reg[7];
    au8Reg[0] = u8BinToBcd((uint8_t)tUtc.tm_sec);
    au8Reg[1] = u8BinToBcd((uint8_t)tUtc.tm_min);
    au8Reg[2] = u8BinToBcd((uint8_t)tUtc.tm_hour); // 24 jam
    au8Reg[3] = u8BinToBcd((uint8_t)(tUtc.tm_wday + 1));
    au8Reg[4] = u8BinToBcd((uint8_t)tUtc.tm_mday);
    au8Reg[5] = u8BinToBcd((uint8_t)(tUtc.tm_mon + 1));
    au8Reg[6] = u8BinToBcd((uint8_t)((tUtc.tm_year + 1900) - 2000));

    Wire.beginTransmission(DS3231_I2C_ADDR);
    Wire.write(0x00);
    for (int i = 0; i < 7; i++)
    {
        Wire.write(au8Reg[i]);
    }
    uint8_t u8Err = Wire.endTransmission();

    if (u8Err != 0)
    {
        Serial.printf("[ERROR] Tulis DS3231 gagal, kode %u.\n", (unsigned)u8Err);
        return false;
    }

    Serial.println("[INFO] NTP/sistem tersalin ke DS3231 (UTC).");
    return true;
}

/**
 * Membaca DS3231 dan mengisi jam sistem ESP32 (UTC).
 * Dipakai saat boot supaya slot tetap jalan tanpa WiFi.
 *
 * Input: Tidak ada.
 * Output: true jika RTC valid (tahun >= 2020) dan jam sistem terisi.
 */
bool bRtcRestoreSystemTime()
{
    if (!bDs3231Present())
    {
        Serial.println("[WARNING] DS3231 tidak terdeteksi.");
        return false;
    }

    Wire.beginTransmission(DS3231_I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0)
    {
        return false;
    }

    uint8_t u8Got = Wire.requestFrom((int)DS3231_I2C_ADDR, 7);
    if (u8Got != 7)
    {
        Serial.println("[ERROR] Baca DS3231 tidak lengkap.");
        return false;
    }

    uint8_t u8Sec  = u8BcdToBin(Wire.read() & 0x7F);
    uint8_t u8Min  = u8BcdToBin(Wire.read() & 0x7F);
    uint8_t u8Hour = u8BcdToBin(Wire.read() & 0x3F);
    Wire.read(); // weekday
    uint8_t u8Day  = u8BcdToBin(Wire.read() & 0x3F);
    uint8_t u8Mon  = u8BcdToBin(Wire.read() & 0x1F);
    uint8_t u8Year = u8BcdToBin(Wire.read());

    if (u8Year < 20 || u8Mon < 1 || u8Mon > 12 || u8Day < 1)
    {
        Serial.println("[INFO] DS3231 belum diset (waktu tidak valid).");
        return false;
    }

    struct tm tUtc;
    memset(&tUtc, 0, sizeof(tUtc));
    tUtc.tm_sec  = u8Sec;
    tUtc.tm_min  = u8Min;
    tUtc.tm_hour = u8Hour;
    tUtc.tm_mday = u8Day;
    tUtc.tm_mon  = u8Mon - 1;
    tUtc.tm_year = (2000 + u8Year) - 1900;
    tUtc.tm_isdst = 0;

    // Epoch UTC tanpa timegm/mktime, supaya zona WIB tidak menggeser jam.
    time_t tEpoch = tEpochFromUtcCalendar(&tUtc);

    struct timeval tVal;
    tVal.tv_sec = tEpoch;
    tVal.tv_usec = 0;
    settimeofday(&tVal, nullptr);

    Serial.printf(
        "[INFO] Jam sistem diisi dari DS3231: 20%02u-%02u-%02u %02u:%02u:%02u UTC\n",
        (unsigned)u8Year,
        (unsigned)u8Mon,
        (unsigned)u8Day,
        (unsigned)u8Hour,
        (unsigned)u8Min,
        (unsigned)u8Sec
    );
    return true;
}
