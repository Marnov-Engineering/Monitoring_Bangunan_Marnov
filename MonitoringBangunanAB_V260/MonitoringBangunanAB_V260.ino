// =====================================================
// MonitoringBangunanAB_V260.ino
// Firmware V2.6.0 — alat monitoring bangunan (ESP32)
//
// PERUBAHAN V2.6.0
//   - Saat bangun (daya baru ON maupun bangun timer): kedip hijau diganti
//     running LED hijau -> kuning -> merah -> biru SATU putaran.
//   - Tes lampu: saat tombol ditekan, keempat LED menyala bersamaan.
//       * Bangun dari sleep karena tombol: LED langsung ON semua.
//         Dilepas sebelum 3 detik -> LED mati, sleep lagi.
//         Ditahan 3 detik -> masuk AP, LED berganti running terus-menerus.
//       * Di mode AP, tombol ditahan untuk mematikan AP: LED ON semua.
//         Dilepas sebelum 3 detik -> running AP lanjut.
//         Ditahan 3 detik -> sleep.
//   - FIRMWARE_VERSION diperbaiki menjadi "2.6.0" (V250 masih "2.4.0").
//
// PERUBAHAN V2.5.0
//   - Jumlah alat per rombongan kirim dan jeda antar rombongan bisa diatur
//     dari menu Pengaturan HMI Spreadsheet (default 3 alat / 10 detik).
//     Diambil hanya saat daya baru dinyalakan, disimpan ke config.json
//     hanya jika berbeda dari nilai yang tersimpan.
//
// PERUBAHAN V2.4.0
//   - Konsep DUA WAKE per slot interval (default 5 menit):
//       Wake BACA  : SEMUA node bangun tepat di kelipatan interval
//                    (Node B 6 detik lebih awal untuk single shot SCD41),
//                    baca sensor, tulis CSV, lalu tidur lagi.
//                    TANPA WiFi/NTP — jam dari DS3231 (dipulihkan di setup()).
//       Wake KIRIM : bangun lagi sesuai urutan rombongan (stagger),
//                    sambung WiFi + NTP, kirim antrian CSV, lalu tidur
//                    sampai wake BACA slot berikutnya.
//     Penanda "wake berikutnya = kirim" disimpan di RTC memory
//     (g_bPendingSend), hilang saat daya putus (aman, mulai ulang bersih).
//   - Siklus pertama setelah daya baru ON tetap GABUNGAN seperti V2.3.0
//     (WiFi + NTP + sync Sheet + baca + kirim), baru siklus berikutnya 2 wake.
//   - Jeda antar rombongan stagger naik dari 8 detik menjadi 10 detik.
//
// PERUBAHAN V2.3.0
//   - Mode gain VEML7700 (semua Node A): Autorange / 2x / 1x / 1/4x / 1/8x.
//     Satu nilai di menu Pengaturan HMI Spreadsheet, diambil saat daya
//     baru dinyalakan, disimpan di NVS.
//   - Halaman AP: kalibrasi hanya DITAMPILKAN (read-only).
//     Mengubah kalibrasi hanya dari HMI Spreadsheet.
//   - Halaman AP: baca sensor realtime untuk data kalibrasi
//     (/calmLux Node A, /calmEnv Node B), selain ADC baterai (/calmA).
//   - Tombol: begitu tahan 3 detik tercapai, AP langsung aktif
//     (tidak menunggu tombol dilepas). Untuk mematikan: tahan 3 detik lagi.
//   - Timeout AP bisa diatur dari halaman AP (disimpan di config.json).
//
// APA PROGRAM INI?
// ESP32 bangun sebentar, ambil data sensor, simpan ke flash,
// coba kirim ke Google Sheet, lalu tidur lagi (hemat baterai).
// Kalau kirim gagal, data TIDAK hilang — tetap di file /data-<NodeID>.csv
// dan akan dikirim belakangan (antrian / backfill).
//
// setup() — dijalankan SETIAP kali ESP32 menyala / bangun tidur
//   1) Watchdog, LED, saklar daya RTC (ON hanya saat bangun) dan SD (OFF),
//      jam DS3231, kalibrasi NVS, config WiFi/URL, file CSV
//   2) Jika WiFi atau URL Sheet belum diisi
//        -> nyalakan WiFi Access Point (mode setting)
//        -> tetap menyala sampai config disimpan (tanpa timeout)
//   3) Jika bangun karena tombol saat sleep (EXT0) dan ditahan ~3 detik
//        -> masuk mode AP maintenance (download/hapus/config)
//        -> AP langsung aktif saat 3 detik tercapai (tanpa menunggu lepas)
//        -> otomatis tidur lagi setelah timeout AP (default 2 menit,
//           bisa diubah di halaman AP; kecuali config kosong)
//   4) Jika normal (config OK, bangun timer) — V2.4.0:
//        -> bangun deep sleep + g_bPendingSend=false : wake BACA
//             (sensor + CSV, tanpa WiFi) lalu tidur sampai giliran kirim
//        -> bangun deep sleep + g_bPendingSend=true  : wake KIRIM
//             (WiFi + NTP + antrian CSV, langkah a & d di bawah) lalu tidur
//             sampai kelipatan interval berikutnya
//        -> daya baru ON: SATU siklus gabungan (a..f di bawah), lalu deep sleep
//             a. Sambung WiFi + sinkron jam (NTP) + tulis ke RTC
//                Jika daya baru dinyalakan (bukan bangun sleep):
//                ambil link API, versi, interval, timezone, dan kalibrasi
//                dari Sheet. Link/interval/timezone disimpan hanya bila berbeda.
//                Jika versi Sheet berbeda, unduh firmware (OTA) lalu restart.
//             b. Baca sensor + ADC baterai, koreksi y=m*x+c
//                (Node A = cahaya, Node B = suhu/RH/CO2)
//             c. Tulis hasil ke /data-<NodeID>.csv di flash (lebih dulu dari kirim)
//             d. Kirim antrian dalam SATU POST (paling banyak 25 baris:
//                2 jam ke belakang + sampel terbaru, interval 5 menit).
//                Jika muat, sampel siklus ini ikut di POST yang sama.
//             e. Jika sampel ini belum ikut POST di atas, kirim sendiri
//                (tetap bentuk rows, isinya satu baris).
//             f. Langsung deep sleep (cek tombol tidak dilakukan sebelum sleep)
//                AP dari sleep: tekan tombol (wake) lalu tahan ~3 detik
//
// loop() — biasanya TIDAK mengerjakan pengukuran
//   Pengukuran sudah selesai di setup() langkah 4, lalu tidur.
//   loop() hanya aktif saat mode AP (halaman web hidup):
//     - layani browser user (download CSV / hapus / ubah config)
//     - cek timeout AP atau tombol tahan 3 detik -> tidur
//   Setelah deep sleep, ESP32 seperti reset ulang -> setup() lagi
//   (bukan "lanjut loop seperti Arduino biasa terus-menerus").
//
// CARA PAKAI MODE AP
//   Sambung WiFi SSID: MonitoringNode-AP
//   Buka browser: http://192.168.4.1
//   Di sana bisa: unduh CSV, hapus data, ubah WiFi/URL/interval/timeout AP,
//   melihat faktor kalibrasi (read-only) dan mode gain VEML7700,
//   serta membaca sensor realtime untuk data kalibrasi.
//
// TOMBOL
//   GPIO36, ACTIVE LOW (tekan = ke GND), wajib pull-up 10k ke 3.3V
//   Saat deep sleep: tekan = bangun, terus tahan ~3 detik = AP
//     (AP langsung aktif tepat di detik ke-3, tidak menunggu tombol dilepas)
//     V2.6.0: selama ditahan keempat LED ON (tes lampu)
//   Saat mode AP: lepas tombol dulu, lalu tekan-tahan 3 detik = keluar AP / sleep
//     V2.6.0: selama ditahan keempat LED ON; dilepas cepat = running AP lanjut
//   Sebelum sleep setelah siklus: tombol TIDAK dicek (hemat waktu)
// =====================================================

#include <Arduino.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Adafruit_VEML7700.h>
#include <SensirionI2cScd4x.h>
#include <esp_sleep.h>
#include <esp_system.h>

#ifndef SCD41_I2C_ADDR_62
#define SCD41_I2C_ADDR_62 0x62
#endif

#ifndef NO_ERROR
#define NO_ERROR 0
#endif

// =====================================================
// FIRMWARE INFO
// =====================================================

// Harus sama persis dengan versi target di HMI (tombol Versi) agar OTA
// tidak berulang. V250 masih tertulis "2.4.0"; V260 diperbaiki.
#define FIRMWARE_VERSION            "2.6.0"

// =====================================================
// FILE SISTEM
// =====================================================

#define CONFIG_FILE_PATH            "/config.json"
#define DATA_FILE_LEGACY_PATH       "/data.csv"
#define DATA_FILE_TMP_PATH          "/data.tmp"
#define DATA_FILE_PATH_MAX          48
#define DATA_FILE_MAX_BYTES         400000UL
#define BACKFILL_BATCH_MAX          25U
// Satu POST, Node A dan Node B sama.
// 25 baris = 24 x 5 menit (2 jam) + 1 sampel terbaru. Muat di bawah 8 KB.
#define SHEET_BATCH_PAYLOAD_BYTES   8192U
// Satu baris (rows berisi 1 objek). Ukuran mengikuti Node B, dipakai juga oleh Node A.
#define SHEET_ROW_PAYLOAD_BYTES     768U
#define PREFS_NAMESPACE             "node"
#define PREFS_KEY_LAST_SENT         "last_sent"

// =====================================================
// KONFIGURASI DEFAULT
// =====================================================

#define DEFAULT_NODE_ID             "A-01"
#define DEFAULT_INTERVAL_S          300U
// URL Web App V2.5.0 (Apps Script Dashboard Monitoring).
// Dipakai bila config belum menyimpan script_url.
#define DEFAULT_SCRIPT_URL          "https://script.google.com/macros/s/AKfycbwUtHkrPu1fkj8IvPKlmp1LCuGm1rgT2GYir_ORFH0caEUhCiCcp5q77tEzSQFI0SnmIg/exec"
// Bin OTA. Firebase sering membalas 302 sebelum file sebenarnya.
#define OTA_FIRMWARE_HOST           "firebasestorage.googleapis.com"
#define OTA_FIRMWARE_PATH           "/v0/b/otastorage-503b9.appspot.com/o/MonitoringBangunan.bin?alt=media"
#define OTA_HTTP_TIMEOUT_MS         120000
#define INTERVAL_MIN_S              10U
#define INTERVAL_MAX_S              3600U
// Stagger per ROMBONGAN. V2.5.0: nilai aktif ada di g_nodeConfig
// (u32StaggerGroupSize, u32StaggerStepS), diatur dari HMI Spreadsheet
// dan diambil saat daya baru ON. Nilai di bawah hanya default untuk
// config baru / config lama yang belum punya key-nya.
// Contoh default 3 alat / 10 detik:
// A-01..03 +0 s, A-04..06 +10 s, A-07..09 +20 s, ... A-22..24 +70 s, A-25 +80 s.
// Stagger hanya berlaku untuk wake KIRIM. Wake BACA semua node serentak
// di kelipatan interval.
#define DEFAULT_STAGGER_GROUP_SIZE  3U
#define DEFAULT_STAGGER_STEP_S      10U
// Rentang yang diterima dari Sheet / config.json.
// 25 = jumlah node maksimum per tipe (A-01..A-25).
#define STAGGER_GROUP_SIZE_MIN      1U
#define STAGGER_GROUP_SIZE_MAX      25U
#define STAGGER_STEP_MIN_S          1U
#define STAGGER_STEP_MAX_S          120U
// Wake KIRIM dijadwalkan sekian detik sebelum detik kirim rombongan,
// supaya WiFi + NTP sudah siap tepat saat giliran kirim tiba.
#define WAKE_BEFORE_SEND_S          8U

// =====================================================
// WiFi / AP
// =====================================================

#define WIFI_CONNECT_TIMEOUT_MS     15000UL
#define WIFI_CONNECT_RETRY_COUNT    1U
#define AP_SSID                     "MonitoringNode-AP"
#define AP_PASSWORD                 ""
#define AP_IP_STRING                "192.168.4.1"
// Timeout AP maintenance (detik). Diubah dari halaman AP (form Pengaturan),
// disimpan di config.json sebagai ap_timeout_s.
// Tidak berlaku saat config belum lengkap (AP force stay).
#define DEFAULT_AP_TIMEOUT_S        120U
#define AP_TIMEOUT_MIN_S            30U
#define AP_TIMEOUT_MAX_S            1800U

// =====================================================
// TOMBOL
// =====================================================

// GPIO 36 input-only. ACTIVE LOW. Wajib pull-up eksternal 10k ke 3.3V.
#define PIN_BUTTON_AP               36
#define BUTTON_ACTIVE_LEVEL         LOW
#define BUTTON_HOLD_MS              3000UL
#define BUTTON_POLL_MS              20UL
#define BUTTON_HOLD_CHECK_WINDOW_MS 3500UL

// =====================================================
// PIN DARI SKEMA 2026-09-18
// =====================================================

// LED low-side 2N7002, HIGH = nyala.
// Urutan warna = asumsi LED1..4 (skema tidak menulis warna).
#define PIN_LED_GREEN               27
#define PIN_LED_YELLOW              25
#define PIN_LED_RED                 32
#define PIN_LED_BLUE                26

// Pola LED tambahan (arti PDF hijau/kuning/merah/biru tetap).
// Running LED 1 putaran saat bangun (daya baru ON / timer).
// 4 langkah x 160 ms = sekitar 0.64 s (V250: kedip hijau 3x = 0.9 s).
#define LED_BOOT_CHASE_STEP_MS      160UL
#define LED_AP_CHASE_MS             160UL
#define LED_OTA_SLOW_MS             500UL
#define LED_OTA_FAST_MS             80UL
#define LED_OTA_FAST_COUNT          5U
#define LED_SEND_OK_MS              100UL
#define LED_SEND_OK_COUNT           2U
#define OTA_WRITE_CHUNK_BYTES       1024U

// SVN / GPIO39 input-only = net BMS (ADC baterai).
#define PIN_ADC_BATTERY             39
#define BATTERY_ADC_FULL_SCALE      4095.0f
#define BATTERY_ADC_VREF_V          3.3f
// Pembagi ke BMS. Default 1:1 -> tegangan paket = 2 x tegangan pin.
#define BATTERY_R_TOP_OHM           100000.0f
#define BATTERY_R_BOT_OHM           100000.0f
// Default kalibrasi baterai jika NVS belum pernah disimpan.
// Dihitung dari multimeter vs ADC GPIO39, 23 Sep 2026:
// 3.6 V=3204.5 ... 2.7 V=2367.0
// x = ADC/4095*3.3*2, lalu y = m*x + c. Sisa galat sekitar +/- 10 mV.
#define BATTERY_CAL_DEFAULT_M       0.667031f
#define BATTERY_CAL_DEFAULT_C       0.164867f

// RTC_T -> Q7/Q8. HIGH menyalakan VCC_RTC hanya saat node bangun.
// Saat sleep pin ini LOW. Jam tetap jalan dari baterai eksternal DS3231.
// Tidak lagi dipakai untuk VEML: modul RTC menaikkan arus sleep jika rail digabung.
#define PIN_PWR_RTC                 33
// VEML7700_T / saklar daya sensor. HIGH = rail sensor ON.
// Dipakai GPIO17, terpisah dari RTC, supaya sleep tidak ikut beban modul RTC.
#define PIN_PWR_SENSOR              17
// SDCARD_T. Firmware tidak pakai SD; LOW = rail mati.
#define PIN_PWR_SDCARD              16

#define WDT_TIMEOUT_MS              120000UL

// Perkiraan arus untuk cetak estimasi otonomi (bukan pengukuran).
#define AUTONOMY_PACK_MAH           2000U
#define AUTONOMY_ACTIVE_S           25U
#define AUTONOMY_ACTIVE_MA          80U
#define AUTONOMY_SLEEP_UA           250U

#define PREFS_NAMESPACE_CALIB       "calib"

// =====================================================
// SINKRONISASI WAKTU
// =====================================================

#define NTP_SERVER_PRIMARY          "pool.ntp.org"
#define NTP_SERVER_SECONDARY        "time.google.com"
#define NTP_TIMEOUT_MS              10000UL
// Default zona Indonesia: WIB. Bisa diganti dari HMI Spreadsheet
// (WIB +07 / WITA +08 / WIT +09) saat daya baru ON.
#define DEFAULT_TIMEZONE_OFFSET_S   25200L
#define DEFAULT_TIMEZONE_STRING     "+07:00"

// =====================================================
// KONEKSI CLOUD
// =====================================================

#define HTTPS_TIMEOUT_MS            15000
// POST antrian 20 baris sempat putus di 15 detik (HTTP -11).
// 45 detik memberi waktu Apps Script menulis setValues.
// Tetap di bawah watchdog 120 detik.
#define HTTPS_POST_TIMEOUT_MS       45000
#define SEND_RETRY_COUNT            1U

// =====================================================
// I2C / SENSOR
// =====================================================

#define PIN_I2C_SDA                 21
#define PIN_I2C_SCL                 22
#define I2C_CLOCK_HZ                100000UL
#define I2C_TIMEOUT_MS              50

#define ALS_SATURATION_HIGH         50000
#define ALS_SATURATION_LOW          200

// Mode gain VEML7700 (Node A). Diatur dari HMI Spreadsheet, disimpan di NVS.
// "auto" = autorange: mulai 1/8x, naik/turun satu langkah per pembacaan.
// "2" / "1" / "1_4" / "1_8" = gain tetap 2x / 1x / 1/4x / 1/8x (tanpa autorange).
// Teks harus sama dengan GAIN_MODE_LIST di AppsScript_Dashboard_Monitoring.gs.
#define VEML_GAIN_MODE_AUTO         "auto"
#define VEML_GAIN_MODE_DEFAULT      VEML_GAIN_MODE_AUTO
#define VEML_GAIN_MODE_TEXT_MAX     8U
#define PREFS_KEY_VEML_GAIN         "veml_gain"

// Baca sensor realtime di mode AP (halaman /calmLux dan /calmEnv).
// Sensor hanya dibaca oleh loop() (satu pemilik bus I2C), handler web hanya
// mengambil salinan hasil terakhir.
// VEML7700: integrasi 100 ms, dibaca tiap 1 detik.
// SCD41: single shot ~5 detik (blocking), jadi jeda 6 detik antar ukur.
// Jika halaman tidak meminta data selama 15 detik, pengukuran berhenti
// (loop tidak terblokir SCD41 saat tidak ada yang melihat).
#define REALTIME_LUX_INTERVAL_MS    1000UL
#define REALTIME_ENV_INTERVAL_MS    6000UL
#define REALTIME_IDLE_STOP_MS       15000UL

#define SCD41_STOP_WAIT_MS          500UL
// measureSingleShot() menunggu 5 detik di dalam library.
// 6 detik = batas log jika perintah itu macet lebih lama.
#define SCD41_SINGLE_SHOT_TIMEOUT_MS 6000UL
// Node B bangun 6 detik sebelum slot agar single shot ~5 detik
// selesai di sekitar awal slot. Tidak ada sampel yang dibuang.
#define NODE_B_WAKE_EARLY_S         6U
#define SLEEP_MIN_S                 2U

// Batas masih boleh ikut slot saat ini setelah (awal_slot + stagger).
// Grace 70 s ditambahkan DI ATAS stagger node (A-25 = +80 s pada V2.4.0),
// untuk menutup WiFi lambat saat cold power.
#define SLOT_JOIN_GRACE_S           70U

// =====================================================
// STRUCT
// =====================================================

struct NodeConfig
{
    char     szNodeId[16];
    char     szWifiSsid[64];
    char     szWifiPassword[64];
    char     szScriptUrl[256];
    // Offset ISO, contoh "+07:00" / "+08:00" / "+09:00".
    // Diisi dari Spreadsheet saat cold power (atau default WIB).
    char     szTimezone[8];
    uint32_t u32IntervalS;
    // Timeout mode AP (detik). Diisi dari halaman AP. Default DEFAULT_AP_TIMEOUT_S.
    uint32_t u32ApTimeoutS;
    // Stagger kirim (V2.5.0). Diisi dari Sheet saat cold power.
    // Default DEFAULT_STAGGER_GROUP_SIZE / DEFAULT_STAGGER_STEP_S.
    uint32_t u32StaggerGroupSize;   // jumlah alat per rombongan
    uint32_t u32StaggerStepS;       // jeda antar rombongan (detik)
};

struct NodeAData
{
    float   fIlluminanceLux;
    uint8_t u8IlluminanceValid;
    uint8_t u8SensorStatus;
    float   fBatteryVoltage;
};

struct NodeBData
{
    float   fAirTemperatureC;
    uint8_t u8TempValid;
    float   fRelativeHumidityPct;
    uint8_t u8RhValid;
    float   fCo2Ppm;
    uint8_t u8Co2Valid;
    uint8_t u8SensorStatus;
    float   fBatteryVoltage;
};

// y = m * x + c per channel. Disimpan di NVS.
struct CalibrationSet
{
    float fLuxM;
    float fLuxC;
    float fTempM;
    float fTempC;
    float fRhM;
    float fRhC;
    float fCo2M;
    float fCo2C;
    float fBatteryM;
    float fBatteryC;
    // Mode gain VEML7700 (Node A saja). Nilai: "auto", "2", "1", "1_4", "1_8".
    // Ditulis hanya dari Sheet saat cold power (vApplyCalibrationFromSheetJson).
    // Dibaca bInitSensorHardware() dan bReadNodeASensor().
    char  szVemlGainMode[VEML_GAIN_MODE_TEXT_MAX];
};

// Hasil baca sensor MENTAH (sebelum y=m*x+c) untuk halaman kalibrasi AP.
// Ditulis loop() lewat vUpdateSensorRealtime(), dibaca handler web
// lewat vGetSensorRealtimeSnapshot() (disalin di dalam critical section).
struct SensorRealtime
{
    bool     bValid;          // true jika sudah ada minimal satu bacaan sukses
    bool     bLastOk;         // hasil pembacaan terakhir
    uint32_t u32UpdatedMs;    // millis() saat bacaan sukses terakhir
    uint32_t u32SampleCount;  // jumlah bacaan sukses sejak AP aktif
    // Node A (VEML7700)
    uint16_t u16Als;
    float    fLuxRaw;
    uint8_t  u8Gain;
    // Node B (SCD41)
    uint16_t u16Co2Raw;
    float    fTempRaw;
    float    fRhRaw;
};

struct SystemStatus
{
    bool      bIsNodeA;
    bool      bLastOk;
    uint32_t  u32SuccessCount;
    uint32_t  u32FailCount;
    char      szLastTimestamp[32];
    NodeAData lastNodeA;
    NodeBData lastNodeB;
};

enum eSystemState
{
    STATE_AP_MODE,
    STATE_CYCLE
};

// =====================================================
// VARIABEL GLOBAL
// =====================================================

NodeConfig   g_nodeConfig;
SystemStatus g_sysStatus = {};
Preferences  g_prefs;

static eSystemState g_eState = STATE_AP_MODE;

static bool     g_bApForceStay = false;
static uint32_t g_u32ApStartMs = 0;
// false saat AP baru dibuka dengan tombol masih ditekan.
// Menjadi true setelah tombol terlihat dilepas; baru setelah itu
// tahan 3 detik di AP dihitung untuk mematikan AP.
// Hanya dipakai task loop (tidak perlu volatile / mutex).
static bool     g_bApButtonArmed = true;

// =====================================================
// STATE DUA WAKE (RTC MEMORY) — V2.4.0
// -----------------------------------------------------
// RTC_DATA_ATTR = disimpan di RTC slow memory, BERTAHAN selama deep sleep.
// Saat daya baru ON / brownout / reset EN nilainya kembali ke nilai awal
// (false / 0). Itu disengaja: setelah daya putus, rangkaian baca -> kirim
// dimulai ulang dengan bersih lewat siklus gabungan cold power.
//
// g_bPendingSend
//   true  = wake timer berikutnya adalah wake KIRIM.
//   false = wake timer berikutnya adalah wake BACA.
//   Ditulis: vRunReadCycle() (set true), vRunSendCycle() (set false).
//   Dibaca : setup() untuk memilih jenis wake, dan u64GetSleepUsToNextWake().
//
// g_tPendingSlotEpoch
//   Epoch UTC awal slot yang datanya sudah ditulis ke CSV oleh wake BACA
//   dan belum dicoba kirim. Dipakai wake KIRIM untuk membentuk timestamp
//   yang sama persis dengan baris CSV (batas atas backfill).
//
// Hanya diakses dari alur utama setup() (tanpa RTOS task lain),
// jadi tidak perlu mutex / volatile.
// =====================================================

RTC_DATA_ATTR bool   g_bPendingSend      = false;
RTC_DATA_ATTR time_t g_tPendingSlotEpoch = 0;

Adafruit_VEML7700  g_veml7700;
SensirionI2cScd4x  g_scd41;
bool               g_bSensorHardwareOk = false;

// Objek yang sama dipakai NodeWebServer.ino (pola Gas_Monitoring_Sensor).
static AsyncWebServer server(80);

// =====================================================
// FUNCTION PROTOTYPES
// -----------------------------------------------------
// Dikelompokkan per file .ino tempat fungsi didefinisikan.
// Hanya deklarasi (forward declaration), bukan implementasi.
// =====================================================

// --- Config.ino --------------------------------------
// Load/save konfigurasi node (WiFi, URL Sheet, interval, stagger).
void vInitNodeConfig();
void vSetDefaultNodeConfig();
bool bLoadNodeConfig();
bool bSaveNodeConfig();
bool bIsNodeConfigValid();
bool bIsNodeTypeA();
int iGetNodeNumber();
uint32_t u32GetNodeStaggerS();
uint32_t u32GetStaggerGroupSize();
uint32_t u32GetStaggerStepS();
bool bIsStaggerGroupSizeValid(long i32Value);
bool bIsStaggerStepValid(long i32Value);
// Marker Preferences untuk backfill CSV -> Sheet.
void vInitBackfillMarker();
void vGetLastSentTimestamp(char* szBuffer, size_t uBufferSize);
void vSetLastSentTimestamp(const char* szTimestamp);
void vClearLastSentTimestamp();

// --- WiFiMgr.ino -------------------------------------
// Mode STA, AP, status koneksi, RSSI, IP lokal.
void vInitWifi();
bool bConnectWifiStation(uint32_t u32TimeoutMs);
void vDisconnectWifiStation();
void vStartApMode();
bool bIsWifiConnected();
int8_t i8GetRssi();
void vGetLocalIpStr(char* szBuffer, size_t uBufferSize);

// --- TimeSync.ino ------------------------------------
// NTP, timestamp lokal ISO+offset, slot sampling, hitung sleep.
bool bSyncNtp();
void vApplyNtpTimezoneOffset();
void vGetTimestampLocal(char* szBuffer, size_t uBufferSize);
void vFormatTimestampLocal(time_t tEpochUtc, char* szBuffer, size_t uBufferSize);
void vGetTimeLocalStr(char* szBuffer, size_t uBufferSize);
bool bIsTimeValid();
bool bIsTimezoneOffsetValid(const char* szTimezone);
long i32TimezoneOffsetSeconds(const char* szTimezone);
long i32GetConfiguredTimezoneOffsetS();
time_t tGetSlotStartEpoch(uint32_t u32IntervalS);
time_t tGetSendEpoch(time_t tSlotStart, uint32_t u32StaggerS);
time_t tGetNextSlotStartEpoch(uint32_t u32IntervalS);
uint64_t u64GetSleepUsToNextWake(void);
uint64_t u64GetSleepUsToNextReadWake(void);
uint64_t u64GetSleepUsToSendWake(time_t tSlotStart);

// --- CloudSheet.ino ----------------------------------
// POST data ke Google Sheet + backfill baris CSV belum terkirim.
bool bSendNodeAToSheet(const NodeAData* pData, const char* szTimestamp);
bool bSendNodeBToSheet(const NodeBData* pData, const char* szTimestamp);
bool bSendCsvLineToSheet(const char* szCsvLine);
bool bBackfillUnsentBefore(const char* szBeforeTimestampExclusive);
void vSyncScriptUrlFromSheet();

// --- Ota.ino -----------------------------------------
// Unduh bin Firebase lalu Update.begin / writeStream / Update.end.
void vUpdateFirmware();

// --- NodeWebServer.ino -------------------------------
// HTTP AP: setup seperti Gas WebServer.ino (softAP, rute, send_P, begin).
void vInitWebServer();
void vHandleWebServer();

// --- Sensor.ino --------------------------------------
// I2C, inisialisasi hardware, baca sensor Node A / Node B.
void vInitI2C();
void vScanI2CBus();
bool bInitSensorHardware();
uint8_t u8VemlGainFromMode(const char* szMode);
const char* pszVemlGainLabel(uint8_t u8Gain);
bool bReadNodeASensor(NodeAData* pData);
bool bReadNodeBSensor(NodeBData* pData);
void vMarkNodeASensorUnread(NodeAData* pData);
void vMarkNodeBSensorUnread(NodeBData* pData);
void vShutdownSensorsForSleep();
// Realtime AP (sejak V2.3.0).
void vRequestSensorRealtime();
void vUpdateSensorRealtime();
void vGetSensorRealtimeSnapshot(SensorRealtime* pOut);

// --- Gpio.ino ----------------------------------------
// LED, ADC BMS, saklar RTC/SD.
void vInitGpioHardware();
void vSetAllStatusLeds(bool bRecordOk, bool bStorageError, bool bSensorError, bool bWifiOk);
void vLedSignalBoot();
void vLedLampTestOn();
void vLedResetApChase();
void vLedUpdateApChase();
void vLedBeginOtaProgress();
void vLedTickOtaProgress();
void vLedSignalOtaFail();
void vLedSignalOtaSuccessThenRestart();
void vLedSignalSendOk();
void vLedsOffForSleep();
void vHoldOutputsForSleep();
float fReadBatteryVoltageCalibrated();
void vReadBatteryAdcDetail(
    float* pfAdc,
    float* pfPinVolt,
    float* pfPackRawVolt,
    float* pfPackCalibratedVolt
);

// --- Rtc.ino -----------------------------------------
// DS3231 UTC.
bool bRtcSaveSystemTime();
bool bRtcRestoreSystemTime();

// --- Calibration.ino ---------------------------------
// NVS y = m*x+c.
float fApplyCalibration(float fRaw, float fM, float fC);
void vInitCalibration();
bool bSaveCalibration();
bool bIsVemlGainModeValid(const char* szMode);
bool bIsVemlGainModeAuto();

// --- Watchdog.ino ------------------------------------
void vInitWatchdog();
void vFeedWatchdog();
void vStopWatchdogForSleep();
void vPrintAutonomyEstimate();

// --- Sleep.ino ---------------------------------------
// Deep sleep ESP32 (timer + tombol) dan tunggu epoch target.
void vEnterDeepSleepUs(uint64_t u64SleepUs);
void vWaitUntilEpoch(time_t tTargetEpoch);
bool bIsWakeFromApButton(void);
bool bIsColdPowerOn(void);

// --- Button.ino --------------------------------------
// Tombol GPIO hold untuk masuk/keluar mode AP.
void vInitButton();
bool bIsButtonPressedRaw();
bool bWaitButtonHold(uint32_t u32HoldMs, uint32_t u32MaxWindowMs);

// --- Storage.ino -------------------------------------
// LittleFS CSV per node: /data-<NodeID>.csv
void vBuildDataFilePath(char* szBuffer, size_t uBufferSize);
void vInitStorage();
bool bAppendNodeACsv(const NodeAData* pData, const char* szTimestamp);
bool bAppendNodeBCsv(const NodeBData* pData, const char* szTimestamp);
void vTrimDataFileIfNeeded();
bool bDeleteDataCsv();
size_t uGetDataFileSize();
size_t uGetLittleFsFreeBytes();
bool bDataFileExists();

// --- MonitoringBangunanAB_V260.ino (file utama) ------
// Orkestrasi siklus, mode AP, retry WiFi/kirim, banner.
void vPrintBootBanner();
void vPrintNodeSchedule();
bool bConnectWifiWithRetry();
void vRunOneCycle();
void vRunReadCycle();
void vRunSendCycle();
void vSyncClockAfterWifi(bool bWifiOk);
bool bComputeSlotToJoin(time_t* ptSlotStart);
void vEnterApMode(bool bForceStay);
void vMaybeSleepFromAp();
void vAfterCycleDecideSleepOrAp();
uint32_t u32GetApRemainSec(void);
uint32_t u32GetApTimeoutMs(void);
bool bIsApForceStay(void);
bool bSendWithRetryNodeA(const NodeAData* pData, const char* szTimestamp);
bool bSendWithRetryNodeB(const NodeBData* pData, const char* szTimestamp);

// =====================================================
// SETUP
// =====================================================

/**
 * Boot V2.6.0: LED (tes lampu / running 1 putaran), config, tombol AP,
 * lalu salah satu dari:
 * siklus gabungan (daya baru ON), wake BACA, atau wake KIRIM.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void setup()
{
    Serial.begin(115200);
    delay(300);

    vInitWatchdog();
    vFeedWatchdog();
    vPrintBootBanner();
    vInitGpioHardware();

    // V2.6.0: pola LED pertama langsung setelah GPIO siap.
    // Bangun karena tombol -> semua LED ON (tes lampu) sampai diputuskan
    // masuk AP (hold 3 detik) atau sleep lagi (dilepas lebih cepat).
    // Daya baru ON / bangun timer -> running LED 1 putaran.
    if (bIsWakeFromApButton())
    {
        vLedLampTestOn();
    }
    else
    {
        vLedSignalBoot();
    }

    vInitButton();
    vInitI2C();
    bRtcRestoreSystemTime();
    vInitNodeConfig();
    vInitCalibration();
    vPrintAutonomyEstimate();
    vInitBackfillMarker();
    vInitStorage();

    if (!bIsNodeConfigValid())
    {
        Serial.println("[INFO] Config belum lengkap. Masuk AP gabungan...");
        vEnterApMode(true);
        return;
    }

    Serial.printf(
        "[INFO] Config valid. Node: %s | Interval: %u detik | Timezone: %s\n",
        g_nodeConfig.szNodeId,
        g_nodeConfig.u32IntervalS,
        g_nodeConfig.szTimezone
    );
    vPrintNodeSchedule();

    // Bangun dari deep sleep karena tombol: wajib konfirmasi hold 3 detik.
    // Jika dilepas terlalu cepat (sentuhan singkat), tidur lagi.
    // Selama menunggu, keempat LED tetap ON (tes lampu dari awal setup()).
    // Hold OK -> vEnterApMode() mematikan LED lalu running AP.
    // Hold gagal -> vEnterDeepSleepUs() mematikan LED sebelum tidur.
    // Catatan: bangun timer TIDAK menunggu tombol (hemat waktu wake).
    if (bIsWakeFromApButton())
    {
        Serial.println(
            "[INFO] Bangun karena tombol (EXT0). "
            "Tahan terus 3 detik untuk masuk AP..."
        );

        if (bWaitButtonHold(BUTTON_HOLD_MS, BUTTON_HOLD_CHECK_WINDOW_MS))
        {
            Serial.println("[INFO] Hold OK. Masuk AP maintenance.");
            vEnterApMode(false);
            return;
        }

        Serial.println(
            "[INFO] Hold tidak lengkap setelah wake tombol. "
            "Sleep lagi sampai jadwal berikutnya."
        );
        vEnterDeepSleepUs(u64GetSleepUsToNextWake());
        return;
    }

    g_eState = STATE_CYCLE;

    // V2.4.0: pilih jenis wake.
    // Daya baru ON -> siklus gabungan (sync Sheet butuh WiFi di wake ini).
    // Bangun deep sleep -> wake KIRIM bila ada data tertunda, selain itu wake BACA.
    if (bIsColdPowerOn())
    {
        // RTC memory sudah kembali ke nilai awal, tapi ditegaskan agar
        // siklus gabungan selalu diakhiri sleep ke wake BACA.
        g_bPendingSend      = false;
        g_tPendingSlotEpoch = 0;

        Serial.println("[INFO] Jenis wake: GABUNGAN (daya baru ON).");
        vRunOneCycle();
    }
    else if (g_bPendingSend)
    {
        Serial.println("[INFO] Jenis wake: KIRIM (data slot tertunda di CSV).");
        vRunSendCycle();
    }
    else
    {
        Serial.println("[INFO] Jenis wake: BACA (kelipatan interval).");
        vRunReadCycle();
    }
}

// =====================================================
// LOOP
// =====================================================

/**
 * Loop hanya untuk mode AP: webserver + timeout/tombol sleep.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void loop()
{
    vFeedWatchdog();

    if (g_eState != STATE_AP_MODE)
    {
        return;
    }

    vHandleWebServer();
    vLedUpdateApChase();
    vUpdateSensorRealtime();
    vMaybeSleepFromAp();
}

// =====================================================
// AP MODE
// =====================================================

/**
 * Menyalakan AP gabungan (data + config).
 *
 * Input:
 * bForceStay - true jika config belum valid (jangan sleep timeout).
 *
 * Output: Tidak ada.
 */
void vEnterApMode(bool bForceStay)
{
    g_bApForceStay = bForceStay;
    g_u32ApStartMs = millis();
    g_eState       = STATE_AP_MODE;

    // AP dibuka tepat saat hold 3 detik tercapai, biasanya tombol masih
    // ditekan. Tombol baru "siap" mematikan AP setelah dilepas dulu,
    // supaya menahan lebih lama dari 3 detik tidak langsung menutup AP.
    g_bApButtonArmed = !bIsButtonPressedRaw();

    vLedResetApChase();

    // Sensor disiapkan untuk halaman baca realtime (/calmLux, /calmEnv).
    // Gagal di sini tidak menghentikan AP; halaman realtime akan kosong.
    if (!bInitSensorHardware())
    {
        Serial.println(
            "[WARNING] Sensor tidak siap di mode AP. "
            "Halaman baca realtime tidak akan menampilkan data."
        );
    }

    vInitWebServer();

    Serial.println("[INFO] AP aktif di http://" AP_IP_STRING);
    Serial.println("[INFO] LED: mode AP (running G-Y-R-B).");
    if (!bForceStay)
    {
        Serial.printf(
            "[INFO] Timeout AP %lu detik, atau lepas lalu tahan tombol "
            "3 detik lagi untuk sleep.\n",
            (unsigned long)(u32GetApTimeoutMs() / 1000UL)
        );
    }
}

/**
 * Timeout AP dari config (detik) dalam milidetik.
 * Nilai di luar rentang dikembalikan ke default agar AP tidak menyala
 * terlalu lama (boros baterai) atau terlalu singkat untuk dipakai.
 *
 * Input: Tidak ada.
 * Output: timeout AP dalam ms.
 */
uint32_t u32GetApTimeoutMs(void)
{
    uint32_t u32TimeoutS = g_nodeConfig.u32ApTimeoutS;
    if ((u32TimeoutS < AP_TIMEOUT_MIN_S) || (u32TimeoutS > AP_TIMEOUT_MAX_S))
    {
        u32TimeoutS = DEFAULT_AP_TIMEOUT_S;
    }
    return u32TimeoutS * 1000UL;
}

/**
 * Apakah AP sedang memaksa tetap hidup (config kosong).
 *
 * Input: Tidak ada.
 * Output: true jika force stay.
 */
bool bIsApForceStay(void)
{
    return g_bApForceStay;
}

/**
 * Di AP: timeout (kecuali force stay) atau tekan-tahan tombol 3s -> sleep.
 * Tombol hanya dihitung setelah sempat dilepas sejak AP aktif
 * (lihat g_bApButtonArmed).
 *
 * V2.6.0: selama tombol ditahan, keempat LED ON (tes lampu).
 * Dilepas sebelum 3 detik -> running LED AP dilanjutkan.
 * Tercapai 3 detik -> sleep (LED dimatikan di vEnterDeepSleepUs()).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 * Side effect: Mengubah LED; bisa masuk deep sleep.
 */
void vMaybeSleepFromAp()
{
    bool bPressed = bIsButtonPressedRaw();

    if (!g_bApButtonArmed)
    {
        if (!bPressed)
        {
            g_bApButtonArmed = true;
            Serial.println(
                "[INFO] Tombol dilepas. Tahan 3 detik lagi untuk mematikan AP."
            );
        }
    }
    else if (bPressed)
    {
        // bWaitButtonHold() blocking sampai dilepas / 3 detik, jadi
        // vLedUpdateApChase() tidak berjalan dan LED tetap ON selama ditahan.
        vLedLampTestOn();

        if (bWaitButtonHold(BUTTON_HOLD_MS, BUTTON_HOLD_MS + 200UL))
        {
            Serial.println("[INFO] Tombol 3 detik di AP. Sleep.");
            vEnterDeepSleepUs(u64GetSleepUsToNextWake());
            return;
        }

        Serial.println(
            "[INFO] Tombol dilepas sebelum 3 detik. AP tetap aktif, running LED lanjut."
        );
        vLedResetApChase();
    }

    if (g_bApForceStay)
    {
        return;
    }

    if (millis() - g_u32ApStartMs >= u32GetApTimeoutMs())
    {
        Serial.printf(
            "[INFO] Timeout AP %lu detik. Sleep.\n",
            (unsigned long)(u32GetApTimeoutMs() / 1000UL)
        );
        vEnterDeepSleepUs(u64GetSleepUsToNextWake());
    }
}

/**
 * Sisa detik timeout AP untuk ditampilkan di web.
 *
 * Input: Tidak ada.
 * Output: detik tersisa, atau 0 jika force stay / habis.
 */
uint32_t u32GetApRemainSec(void)
{
    if (g_bApForceStay)
    {
        return 0;
    }

    uint32_t u32TimeoutMs = u32GetApTimeoutMs();
    uint32_t u32Elapsed = millis() - g_u32ApStartMs;
    if (u32Elapsed >= u32TimeoutMs)
    {
        return 0;
    }

    return (u32TimeoutMs - u32Elapsed) / 1000UL;
}

/**
 * Setelah siklus: langsung deep sleep.
 * Tombol tidak dicek di sini agar waktu bangun sesingkat mungkin.
 * User yang ingin AP: tekan tombol saat sleep (EXT0 wake).
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vAfterCycleDecideSleepOrAp()
{
    Serial.println("[INFO] === Siklus selesai. Deep sleep ===");
    vEnterDeepSleepUs(u64GetSleepUsToNextWake());
}

// =====================================================
// SATU SIKLUS KERJA
// =====================================================

/**
 * Menyegarkan jam setelah percobaan sambung WiFi.
 *
 * WiFi ada: NTP sungguhan (tunggu SNTP completed), lalu tulis DS3231.
 * NTP gagal/timeout: jangan timpa RTC; isi ulang jam sistem dari DS3231.
 * WiFi tidak ada: tidak melakukan apa-apa (jam sistem tetap dari DS3231
 * yang sudah dipulihkan di setup()).
 *
 * Input:
 * bWifiOk - hasil bConnectWifiWithRetry().
 *
 * Output: Tidak ada.
 * Side effect: mengubah jam sistem ESP32 dan/atau menulis DS3231.
 */
void vSyncClockAfterWifi(bool bWifiOk)
{
    if (!bWifiOk)
    {
        return;
    }

    if (bSyncNtp())
    {
        bRtcSaveSystemTime();
        return;
    }

    Serial.println(
        "[WARNING] NTP gagal. Mengisi ulang jam sistem dari DS3231."
    );

    if (bRtcRestoreSystemTime())
    {
        Serial.println("[INFO] Jam sistem memakai RTC (DS3231).");
    }
    else
    {
        Serial.println(
            "[WARNING] RTC tidak tersedia. Jam sistem mungkin tidak valid."
        );
    }
}

/**
 * Menentukan slot yang diikuti berdasarkan jam sistem sekarang.
 *
 * Aturan (sama seperti V2.3.0):
 *   - Sisa slot <= max(WAKE_BEFORE_SEND_S, NODE_B_WAKE_EARLY_S) + 1 detik
 *     -> node dianggap bangun sedikit lebih awal, ikut slot BERIKUTNYA.
 *   - Sudah lewat (awal slot + stagger + SLOT_JOIN_GRACE_S)
 *     -> slot saat ini terlewat, tidak diikuti (return false).
 *   - Selain itu ikut slot SAAT INI.
 *
 * Input:
 * ptSlotStart - pointer tujuan epoch awal slot (diisi jika return true).
 *
 * Output: true jika ada slot yang boleh diikuti, false jika terlewat.
 */
bool bComputeSlotToJoin(time_t* ptSlotStart)
{
    if (ptSlotStart == nullptr)
    {
        return false;
    }

    uint32_t u32IntervalS = g_nodeConfig.u32IntervalS;
    if (u32IntervalS < INTERVAL_MIN_S)
    {
        u32IntervalS = INTERVAL_MIN_S;
    }

    uint32_t u32StaggerS = u32GetNodeStaggerS();

    time_t tNow;
    time(&tNow);

    time_t tThisSlot = tNow - (tNow % (time_t)u32IntervalS);
    time_t tNextSlot = tThisSlot + (time_t)u32IntervalS;
    time_t tIntoSlot = tNow - tThisSlot;

    uint32_t u32EarlyJoinS = WAKE_BEFORE_SEND_S;
    if (NODE_B_WAKE_EARLY_S > u32EarlyJoinS)
    {
        u32EarlyJoinS = NODE_B_WAKE_EARLY_S;
    }

    if (tIntoSlot >= (time_t)(u32IntervalS - u32EarlyJoinS - 1U))
    {
        *ptSlotStart = tNextSlot;
        return true;
    }

    if (tNow > (tThisSlot + (time_t)u32StaggerS + (time_t)SLOT_JOIN_GRACE_S))
    {
        Serial.printf(
            "[INFO] Slot saat ini sudah lewat "
            "(lewat awal+%u+%u s).\n",
            (unsigned)u32StaggerS,
            (unsigned)SLOT_JOIN_GRACE_S
        );
        return false;
    }

    *ptSlotStart = tThisSlot;
    return true;
}

/**
 * Wake BACA (V2.4.0): baca sensor tepat di kelipatan interval, tulis CSV,
 * lalu deep sleep sampai giliran wake KIRIM rombongan node ini.
 *
 * TANPA WiFi/NTP. Jam dari DS3231 yang dipulihkan di setup().
 * Data sensor diserahkan ke wake KIRIM lewat file CSV (bukan RAM),
 * karena deep sleep menghapus RAM biasa.
 *
 * Input: Tidak ada.
 * Output: Tidak ada (berakhir di deep sleep).
 * Side effect: I2C sensor, tulis CSV, LED, set g_bPendingSend.
 */
void vRunReadCycle()
{
    Serial.println("[INFO] === Wake BACA V2.6.0 ===");
    vFeedWatchdog();

    vSetAllStatusLeds(false, false, false, false);

    if (!bIsTimeValid())
    {
        Serial.println(
            "[WARNING] Jam RTC tidak valid. Tidak menulis CSV. Sleep."
        );
        vAfterCycleDecideSleepOrAp();
        return;
    }

    time_t tSlotStart = 0;
    if (!bComputeSlotToJoin(&tSlotStart))
    {
        // Contoh: bangun terlambat jauh (tertahan di AP). Tunggu slot berikutnya.
        vAfterCycleDecideSleepOrAp();
        return;
    }

    // Node A: jika bangun sedikit sebelum tanda slot (pembulatan jam RTC
    // ke detik / drift timer sleep), tunggu sampai tepat awal slot.
    // Node B sengaja bangun lebih awal (single shot SCD41 ~5 detik),
    // jadi langsung ukur tanpa menunggu.
    if (bIsNodeTypeA())
    {
        vWaitUntilEpoch(tSlotStart);
    }

    char szTimestamp[32];
    vFormatTimestampLocal(tSlotStart, szTimestamp, sizeof(szTimestamp));
    Serial.printf("[INFO] Slot timestamp (baca): %s\n", szTimestamp);

    vInitI2C();
    bool bHwOk = bInitSensorHardware();
    bool bCsvOk = false;
    uint8_t u8SensorStatus = 0;

    if (bIsNodeTypeA())
    {
        NodeAData data;

        if (!bHwOk || !bReadNodeASensor(&data))
        {
            vMarkNodeASensorUnread(&data);
        }

        bCsvOk = bAppendNodeACsv(&data, szTimestamp);
        u8SensorStatus = data.u8SensorStatus;

        g_sysStatus.lastNodeA = data;
        g_sysStatus.bIsNodeA  = true;
    }
    else
    {
        NodeBData data;

        if (!bHwOk || !bReadNodeBSensor(&data))
        {
            vMarkNodeBSensorUnread(&data);
        }

        bCsvOk = bAppendNodeBCsv(&data, szTimestamp);
        u8SensorStatus = data.u8SensorStatus;

        g_sysStatus.lastNodeB = data;
        g_sysStatus.bIsNodeA  = false;
    }

    // LED WiFi (biru) tetap mati: wake BACA tidak menyalakan WiFi.
    vSetAllStatusLeds(bCsvOk, !bCsvOk, (u8SensorStatus != 0), false);
    vTrimDataFileIfNeeded();

    if (!bCsvOk)
    {
        // Tanpa baris CSV, sampel ini tidak bisa dikirim wake KIRIM
        // (tidak ada salinan di RAM setelah deep sleep).
        // Wake KIRIM tetap dijadwalkan untuk antrian lama yang belum terkirim.
        Serial.println(
            "[ERROR] Tulis CSV gagal. Sampel slot ini tidak akan terkirim. "
            "Periksa ruang LittleFS / file data."
        );
    }

    g_bPendingSend      = true;
    g_tPendingSlotEpoch = tSlotStart;

    Serial.println("[INFO] === Wake BACA selesai. Deep sleep sampai wake KIRIM ===");
    vEnterDeepSleepUs(u64GetSleepUsToSendWake(tSlotStart));
}

/**
 * Wake KIRIM (V2.4.0): sambung WiFi + NTP, kirim antrian CSV sampai
 * slot tertunda (g_tPendingSlotEpoch), lalu deep sleep sampai wake BACA
 * slot berikutnya.
 *
 * Sync Sheet (link/interval/timezone/kalibrasi/versi) TIDAK dilakukan
 * di sini; hanya saat daya baru ON (siklus gabungan).
 *
 * Kirim hanya lewat backfill CSV. Jika gagal, baris tetap di CSV dan
 * last_sent tidak maju, jadi otomatis diulang pada wake KIRIM berikutnya.
 *
 * Input: Tidak ada.
 * Output: Tidak ada (berakhir di deep sleep).
 * Side effect: WiFi, NTP, tulis DS3231, POST Sheet, LED, clear g_bPendingSend.
 */
void vRunSendCycle()
{
    Serial.println("[INFO] === Wake KIRIM V2.6.0 ===");
    vFeedWatchdog();

    // Timestamp dibentuk dari slot yang ditulis wake BACA, SEBELUM NTP
    // sempat menggeser jam, supaya persis sama dengan baris CSV.
    char szTimestamp[32];
    vFormatTimestampLocal(g_tPendingSlotEpoch, szTimestamp, sizeof(szTimestamp));

    uint32_t u32StaggerS = u32GetNodeStaggerS();
    time_t tSendEpoch = tGetSendEpoch(g_tPendingSlotEpoch, u32StaggerS);

    Serial.printf(
        "[INFO] Slot tertunda: %s | Stagger rombongan +%u s (group size %u)\n",
        szTimestamp,
        (unsigned)u32StaggerS,
        (unsigned)u32GetStaggerGroupSize()
    );

    vSetAllStatusLeds(false, false, false, false);

    bool bWifiOk = bConnectWifiWithRetry();
    vSetAllStatusLeds(false, false, false, bWifiOk);
    vFeedWatchdog();

    vSyncClockAfterWifi(bWifiOk);
    vFeedWatchdog();

    bool bOk = false;

    if (bWifiOk)
    {
        // Jaga urutan rombongan: WiFi siap lebih awal (WAKE_BEFORE_SEND_S),
        // POST baru dimulai tepat di detik kirim node ini.
        vWaitUntilEpoch(tSendEpoch);

        // Satu POST: antrian lama + sampel slot ini (baris CSV dari wake BACA).
        bool bGapsRemain = bBackfillUnsentBefore(szTimestamp);

        if (bGapsRemain)
        {
            Serial.println(
                "[INFO] Antrian backfill belum habis / POST gagal. "
                "Sisa dikirim wake KIRIM berikutnya (tetap di CSV)."
            );
        }
        else
        {
            char szLastSent[32];
            vGetLastSentTimestamp(szLastSent, sizeof(szLastSent));
            bOk = (strcmp(szLastSent, szTimestamp) == 0);

            if (!bOk)
            {
                Serial.println(
                    "[WARNING] Sampel slot ini tidak ditemukan di antrian CSV "
                    "(kemungkinan tulis CSV gagal saat wake BACA)."
                );
            }
        }
    }
    else
    {
        Serial.println("[WARNING] WiFi gagal. Data aman di CSV.");
    }

    strncpy(
        g_sysStatus.szLastTimestamp,
        szTimestamp,
        sizeof(g_sysStatus.szLastTimestamp) - 1
    );
    g_sysStatus.szLastTimestamp[sizeof(g_sysStatus.szLastTimestamp) - 1] = '\0';
    g_sysStatus.bLastOk = bOk;

    if (bOk)
    {
        g_sysStatus.u32SuccessCount++;
        Serial.println("[INFO] Kiriman sukses.");
        vLedSignalSendOk();
    }
    else
    {
        g_sysStatus.u32FailCount++;
        Serial.println("[WARNING] Kiriman gagal / ditunda (CSV tetap ada).");
    }

    // Selesai mencoba (sukses atau tidak). Status kirim yang sebenarnya
    // dicatat oleh last_sent (NVS) + CSV, bukan oleh flag ini.
    g_bPendingSend      = false;
    g_tPendingSlotEpoch = 0;

    Serial.println("[INFO] === Wake KIRIM selesai. Deep sleep sampai wake BACA ===");
    vEnterDeepSleepUs(u64GetSleepUsToNextReadWake());
}

/**
 * Siklus GABUNGAN (khusus daya baru ON):
 * WiFi/NTP, sync Sheet, sensor, CSV, POST, backfill, lalu sleep ke wake BACA.
 *
 * Input: Tidak ada.
 * Output: Tidak ada.
 */
void vRunOneCycle()
{
    Serial.println("[INFO] === Mulai siklus gabungan V2.6.0 ===");
    vFeedWatchdog();

    vSetAllStatusLeds(false, false, false, false);

    bool bWifiOk = bConnectWifiWithRetry();
    vSetAllStatusLeds(false, false, false, bWifiOk);
    vFeedWatchdog();

    vSyncClockAfterWifi(bWifiOk);
    vFeedWatchdog();

    // Hanya saat daya baru ON, bukan setiap bangun deep sleep.
    // Urutan: WiFi sudah tersambung, NTP sudah dicoba ditulis ke RTC,
    // baru kemudian link API / interval / versi / kalibrasi dibanding yang tersimpan.
    if (bWifiOk && bIsColdPowerOn())
    {
        vSyncScriptUrlFromSheet();
    }
    vFeedWatchdog();

    if (!bIsTimeValid())
    {
        Serial.println(
            "[WARNING] Waktu tidak valid. Tidak menulis CSV. Sleep."
        );
        vAfterCycleDecideSleepOrAp();
        return;
    }

    uint32_t u32StaggerS = u32GetNodeStaggerS();

    time_t tSlotStart = 0;
    if (!bComputeSlotToJoin(&tSlotStart))
    {
        Serial.println("[INFO] Sleep sampai wake BACA berikutnya.");
        vEnterDeepSleepUs(u64GetSleepUsToNextWake());
        return;
    }

    time_t tSendEpoch = tGetSendEpoch(tSlotStart, u32StaggerS);

    char szTimestamp[32];
    vFormatTimestampLocal(tSlotStart, szTimestamp, sizeof(szTimestamp));

    Serial.printf(
        "[INFO] Slot timestamp: %s | Stagger rombongan +%u s "
        "(group size %u)\n",
        szTimestamp,
        u32StaggerS,
        (unsigned)u32GetStaggerGroupSize()
    );

    vInitI2C();
    bool bHwOk = bInitSensorHardware();
    bool bOk   = false;

    if (bIsNodeTypeA())
    {
        NodeAData data;

        if (!bHwOk || !bReadNodeASensor(&data))
        {
            vMarkNodeASensorUnread(&data);
        }

        bool bCsvOk = bAppendNodeACsv(&data, szTimestamp);
        vSetAllStatusLeds(
            bCsvOk,
            !bCsvOk,
            (data.u8SensorStatus != 0),
            bWifiOk
        );
        vTrimDataFileIfNeeded();

        vWaitUntilEpoch(tSendEpoch);

        if (bWifiOk || bConnectWifiWithRetry())
        {
            // Satu POST: antrian lama plus sampel ini jika masih muat.
            bool bGapsRemain = bBackfillUnsentBefore(szTimestamp);

            if (bGapsRemain)
            {
                // Masih ada antrian: jangan kirim/majukan ke sampel saat ini.
                // Sampel sudah aman di CSV; ikut terkirim setelah antrian bersih.
                Serial.println(
                    "[INFO] Antrian backfill belum habis. "
                    "Sampel saat ini ditunda kirim (tetap di CSV)."
                );
                bOk = false;
            }
            else
            {
                char szLastSent[32];
                vGetLastSentTimestamp(szLastSent, sizeof(szLastSent));
                if (strcmp(szLastSent, szTimestamp) == 0)
                {
                    // Antrian kosong atau sampel ini sudah di ujung POST yang sama.
                    bOk = true;
                }
                else
                {
                    bOk = bSendWithRetryNodeA(&data, szTimestamp);
                    if (bOk)
                    {
                        vSetLastSentTimestamp(szTimestamp);
                    }
                }
            }
        }
        else
        {
            Serial.println("[WARNING] WiFi gagal. Data aman di CSV.");
        }

        g_sysStatus.lastNodeA = data;
        g_sysStatus.bIsNodeA  = true;
    }
    else
    {
        NodeBData data;

        if (!bHwOk || !bReadNodeBSensor(&data))
        {
            vMarkNodeBSensorUnread(&data);
        }

        bool bCsvOk = bAppendNodeBCsv(&data, szTimestamp);
        vSetAllStatusLeds(
            bCsvOk,
            !bCsvOk,
            (data.u8SensorStatus != 0),
            bWifiOk
        );
        vTrimDataFileIfNeeded();

        vWaitUntilEpoch(tSendEpoch);

        if (bWifiOk || bConnectWifiWithRetry())
        {
            // Satu POST: antrian lama plus sampel ini jika masih muat.
            bool bGapsRemain = bBackfillUnsentBefore(szTimestamp);

            if (bGapsRemain)
            {
                Serial.println(
                    "[INFO] Antrian backfill belum habis. "
                    "Sampel saat ini ditunda kirim (tetap di CSV)."
                );
                bOk = false;
            }
            else
            {
                char szLastSent[32];
                vGetLastSentTimestamp(szLastSent, sizeof(szLastSent));
                if (strcmp(szLastSent, szTimestamp) == 0)
                {
                    bOk = true;
                }
                else
                {
                    bOk = bSendWithRetryNodeB(&data, szTimestamp);
                    if (bOk)
                    {
                        vSetLastSentTimestamp(szTimestamp);
                    }
                }
            }
        }
        else
        {
            Serial.println("[WARNING] WiFi gagal. Data aman di CSV.");
        }

        g_sysStatus.lastNodeB = data;
        g_sysStatus.bIsNodeA  = false;
    }

    strncpy(
        g_sysStatus.szLastTimestamp,
        szTimestamp,
        sizeof(g_sysStatus.szLastTimestamp) - 1
    );
    g_sysStatus.szLastTimestamp[sizeof(g_sysStatus.szLastTimestamp) - 1] = '\0';
    g_sysStatus.bLastOk = bOk;

    if (bOk)
    {
        g_sysStatus.u32SuccessCount++;
        Serial.println("[INFO] Kiriman sukses.");
        vLedSignalSendOk();
    }
    else
    {
        g_sysStatus.u32FailCount++;
        Serial.println("[WARNING] Kiriman gagal / ditunda (CSV tetap ada).");
    }

    vAfterCycleDecideSleepOrAp();
}

bool bConnectWifiWithRetry()
{
    vInitWifi();

    if (bConnectWifiStation(WIFI_CONNECT_TIMEOUT_MS))
    {
        return true;
    }

    for (uint32_t u32Try = 0; u32Try < WIFI_CONNECT_RETRY_COUNT; u32Try++)
    {
        Serial.println("[INFO] Retry konek WiFi...");
        delay(500);
        if (bConnectWifiStation(WIFI_CONNECT_TIMEOUT_MS))
        {
            return true;
        }
    }

    return false;
}

bool bSendWithRetryNodeA(const NodeAData* pData, const char* szTimestamp)
{
    if (bSendNodeAToSheet(pData, szTimestamp))
    {
        return true;
    }

    for (uint32_t u32Try = 0; u32Try < SEND_RETRY_COUNT; u32Try++)
    {
        Serial.println("[INFO] Retry POST Node A...");
        delay(500);
        if (bSendNodeAToSheet(pData, szTimestamp))
        {
            return true;
        }
    }

    return false;
}

bool bSendWithRetryNodeB(const NodeBData* pData, const char* szTimestamp)
{
    if (bSendNodeBToSheet(pData, szTimestamp))
    {
        return true;
    }

    for (uint32_t u32Try = 0; u32Try < SEND_RETRY_COUNT; u32Try++)
    {
        Serial.println("[INFO] Retry POST Node B...");
        delay(500);
        if (bSendNodeBToSheet(pData, szTimestamp))
        {
            return true;
        }
    }

    return false;
}

void vPrintNodeSchedule()
{
    int iNum = iGetNodeNumber();
    uint32_t u32Stagger = u32GetNodeStaggerS();
    uint32_t u32GroupSize = u32GetStaggerGroupSize();
    uint32_t u32StepS = u32GetStaggerStepS();
    uint32_t u32Group = 0;

    if (iNum >= 1)
    {
        u32Group = (uint32_t)(iNum - 1) / u32GroupSize;
    }

    Serial.printf(
        "[INFO] Jadwal %s: nomor %d, rombongan %u, stagger +%u s, "
        "interval %u s, wake KIRIM %u s sebelum giliran.\n",
        g_nodeConfig.szNodeId,
        iNum,
        (unsigned)(u32Group + 1U),
        (unsigned)u32Stagger,
        g_nodeConfig.u32IntervalS,
        (unsigned)WAKE_BEFORE_SEND_S
    );
    Serial.printf(
        "[INFO] V2.6.0: dua wake per slot. Wake BACA serentak di kelipatan "
        "interval (tanpa WiFi), wake KIRIM per rombongan %u alat / jeda %u s "
        "(diatur dari HMI). Daya baru ON = siklus gabungan + sync link/versi/"
        "interval/timezone/rombongan/jeda/kalibrasi dari Sheet. Beda versi = OTA.\n",
        (unsigned)u32GroupSize,
        (unsigned)u32StepS
    );
}

void vPrintBootBanner()
{
    Serial.println();
    Serial.println("=======================================");
    Serial.printf(" Monitoring Node | Firmware %s\n", FIRMWARE_VERSION);
    Serial.println(" CSV + backfill + AP maintenance");
    Serial.println("=======================================");
    Serial.println();
}
