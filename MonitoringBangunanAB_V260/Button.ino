// =====================================================
// Button.ino
// GPIO 36 ACTIVE LOW  deteksi tahan 3 detik
// =====================================================

#include "driver/rtc_io.h"

/**
 * Menginisialisasi pin tombol AP.
 * GPIO 36 input-only; pull-up harus eksternal.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vInitButton()
{
    // Setelah wake EXT0, lepas mode RTC agar digitalRead stabil.
    rtc_gpio_deinit((gpio_num_t)PIN_BUTTON_AP);

    pinMode(PIN_BUTTON_AP, INPUT);
    Serial.printf(
        "[INFO] Tombol AP GPIO %d ACTIVE LOW, tahan %lu ms. "
        "Wajib pull-up eksternal 10k ke 3.3V. "
        "Saat deep sleep: tekan untuk bangun, tahan 3s = AP langsung aktif. "
        "Di AP: lepas, lalu tahan 3s lagi = sleep.\n",
        PIN_BUTTON_AP,
        (unsigned long)BUTTON_HOLD_MS
    );
}

/**
 * Membaca status mentah tombol (tanpa debounce panjang).
 *
 * Input: Tidak ada.
 * Output: true jika tombol sedang ditekan (level aktif).
 */
bool bIsButtonPressedRaw()
{
    return (digitalRead(PIN_BUTTON_AP) == BUTTON_ACTIVE_LEVEL);
}

/**
 * Menunggu tombol ditahan terus selama u32HoldMs.
 * Jika dilepas sebelum hold selesai, atau window habis, return false.
 *
 * V2.3.0: begitu hold tercapai langsung return true, TIDAK menunggu
 * tombol dilepas. Pencegahan double-trigger dilakukan pemanggil
 * (vMaybeSleepFromAp menunggu tombol dilepas dulu sebelum dihitung lagi).
 *
 * Input:
 * u32HoldMs      - durasi tahan yang dibutuhkan.
 * u32MaxWindowMs - batas waktu total pengamatan.
 *
 * Output: true jika hold penuh tercapai.
 */
bool bWaitButtonHold(uint32_t u32HoldMs, uint32_t u32MaxWindowMs)
{
    uint32_t u32WindowStart = millis();

    // Tunggu sampai tombol ditekan, dalam window.
    while (!bIsButtonPressedRaw())
    {
        if (millis() - u32WindowStart >= u32MaxWindowMs)
        {
            return false;
        }
        delay(BUTTON_POLL_MS);
    }

    // Debounce singkat.
    delay(40);
    if (!bIsButtonPressedRaw())
    {
        return false;
    }

    uint32_t u32PressStart = millis();

    while (bIsButtonPressedRaw())
    {
        if (millis() - u32PressStart >= u32HoldMs)
        {
            Serial.println("[INFO] Hold tombol 3 detik terdeteksi.");
            return true;
        }

        if (millis() - u32WindowStart >= u32MaxWindowMs + u32HoldMs)
        {
            break;
        }

        delay(BUTTON_POLL_MS);
    }

    return false;
}
