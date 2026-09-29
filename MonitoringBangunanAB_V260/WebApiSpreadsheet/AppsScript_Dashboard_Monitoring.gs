/**
 * SALINAN API UNTUK FIRMWARE V2.5.0
 * Lokasi acuan: Program Tes/MonitoringBangunanAB_V250/WebApiSpreadsheet
 * Perubahan link API mulai di V1.6.0.
 * Versi target + OTA MULAI di V1.7.0.
 * Pengaturan HMI (usia, low bat, interval) + sync interval cold-power: V1.8.0
 * Pola LED boot/AP/kirim/OTA: V1.9.0
 *
 * Monitoring Bangunan — API + Dashboard operator (V2.5.0)
 *
 * CARA PASANG
 * 1. Spreadsheet > Extensions > Apps Script
 * 2. Paste SELURUH file .gs ini, Save
 * 3. Jalankan sekali vInitDashboard (izinkan akses)
 * 4. File > New > HTML, namai: Dashboard
 *    Paste isi AppsScript_Dashboard.html
 * 5. Deploy > New deployment > Web app
 *    Execute as: Me | Who has access: Anyone
 * 6. URL /exec → ESP32 POST + buka di browser untuk HMI
 * 7. Ganti link: tombol Link API. Ganti versi: tombol Versi.
 *    Pengaturan dashboard: tombol Pengaturan (usia, low bat, interval, timezone,
 *    jumlah alat per rombongan, jeda antar rombongan).
 *    Node V2.5.0 mengambil link, versi, interval, timezone, rombongan/jeda,
 *    kalibrasi, dan mode gain VEML7700 (Node A) saat daya baru ON.
 *    Jika versi target berbeda dari firmware di alat, node mengunduh bin OTA.
 *    Deployment lama harus tetap hidup sampai semua node sempat menyala,
 *    karena node menanyakan link baru lewat URL yang masih tersimpan.
 *
 * V2.5.0
 * - Pengaturan HMI: jumlah alat per rombongan kirim (default 3, 1–25) dan
 *   jeda antar rombongan (default 10 s, 1–120 s). Disimpan di Script
 *   Properties STAGGER_GROUP_SIZE / STAGGER_STEP_S.
 * - GET ?action=script_url membalas stagger_group_size dan stagger_step_s.
 *   Node mengambilnya saat daya baru ON dan menyimpan hanya bila berbeda.
 * - HMI lebih cepat: setting B3:B5 dibaca sekali per eksekusi (A3:C5 satu
 *   panggilan) dan hanya ditulis bila sel kosong/rusak. Sebelumnya tiap poll
 *   HMI dan tiap POST menulis setNote/setNumberFormat berulang kali.
 * - POST lebih cepat: baris Dashboard node ditulis dengan satu setValues.
 * - doGet menyisipkan data dashboard pertama ke halaman (template), jadi
 *   kartu node tampil tanpa menunggu google.script.run pertama.
 * - Grafik lebih cepat: kolom C dibaca per blok dari bawah untuk mencari
 *   jendela baris rentang, lalu hanya kolom yang dipakai (A..H / A..L) untuk
 *   jendela itu yang dibaca. Timezone di-cache per eksekusi (sebelumnya
 *   Script Properties dibaca per baris). Hasil di-cache 60 s (CacheService).
 * - Grafik > 500 titik diringkas merata di seluruh rentang. Sebelumnya hanya
 *   500 titik pertama yang dikirim, sehingga data terbaru hilang.
 *
 * V2.4.0
 * - Firmware saja (API/HMI tidak berubah): dua wake per slot.
 *   Wake BACA serentak di kelipatan interval (tanpa WiFi), wake KIRIM
 *   per rombongan 3 node dengan jeda 10 s (sebelumnya 8 s).
 *
 * V2.3.0
 * - Mode gain VEML7700 satu nilai untuk semua Node A: 'auto' | '2' | '1' | '1_4' | '1_8'.
 *   Dipilih di menu Pengaturan (bukan kalibrasi per node), karena sensor di tempat yang sama.
 *   Ikut dikirim di GET ?action=script_url pada objek calibration Node A.
 *   Node mengambilnya saat daya baru dinyalakan.
 * - Halaman AP node tidak lagi bisa mengubah kalibrasi. HMI ini satu-satunya
 *   tempat mengubah kalibrasi dan mode gain.
 *
 * V2.2.0
 * - doPost tidak lagi menulis ulang seluruh sheet Dashboard.
 *   Refresh semua node membuat POST lebih lama dari timeout ESP32 (15 s),
 *   node mengulang baris yang sama, dan Sheet menggandakan data.
 * - Status dashboard node yang baru POST dihitung untuk baris itu saja.
 * - Baris log dengan node_id + timestamp yang sudah ada tidak ditulis lagi.
 * - POST boleh berisi {"rows":[...]} sampai 25 baris sekaligus.
 *   Ditulis dengan satu setValues. Satu baris pun memakai bentuk rows.
 *
 * V2.1.0
 * - Timestamp lokal jam dinding tanpa suffix zona (contoh 2026-09-25T19:55:00);
 *   offset tetap di kolom timezone terpisah
 * - Timezone global dari HMI (WIB +07 / WITA +08 / WIT +09), sync cold power
 * - Grafik HMI: filter tanggal dari–sampai + jam mulai/selesai (bukan hanya N terakhir)
 * - HMI poll: status dihitung di memori (tanpa tulis Sheet tiap refresh); poll 10 s
 *
 * V2.0.0
 * - Firmware: stagger rombongan 3 node, jeda 8 s, wake 8 s sebelum kirim
 *
 * V1.9.0
 * - Firmware: pola LED boot / AP running / kirim Sheet OK / OTA
 *
 * V1.8.0
 * - Pengaturan HMI: batas usia (B3), baterai low (B4), interval (Script Properties)
 * - GET ?action=script_url membalas interval_s global
 * - Node menerapkan interval hanya saat daya baru ON dan nilai berbeda
 *
 * V1.7.0
 * - Versi target di Script Properties
 * - GET ?action=script_url juga membalas firmware_version
 * - Kalibrasi per Node ID di dialog detail HMI (konfirmasi 1 kali)
 * - GET ?action=script_url&node_id=A-01 membalas calibration hanya untuk node itu
 *
 * V1.6.0
 * - Link API disimpan di Script Properties
 * - GET ?action=script_url untuk node
 * - HMI: ganti link dengan pengaman dua langkah
 *
 * V1.4.0
 * - Hapus semua data node dari HMI (dengan konfirmasi)
 * - Endpoint getNodeChartData untuk halaman grafik
 */

var DASHBOARD_NAME = 'Dashboard';
var DEFAULT_MAX_AGE_S = 900;
var DEFAULT_BATTERY_LOW_V = 3.50;
var DEFAULT_INTERVAL_S = 300;
var DEFAULT_TIMEZONE = '+07:00';
// Link Web App default (sama dengan firmware DEFAULT_SCRIPT_URL V2.1.0).
var DEFAULT_SCRIPT_EXEC_URL =
  'https://script.google.com/macros/s/AKfycbwUtHkrPu1fkj8IvPKlmp1LCuGm1rgT2GYir_ORFH0caEUhCiCcp5q77tEzSQFI0SnmIg/exec';
var DEFAULT_FIRMWARE_VERSION = '2.5.0';
var INTERVAL_MIN_S = 10;
var INTERVAL_MAX_S = 3600;
// Stagger kirim (V2.5.0). Rentang harus sama dengan firmware
// STAGGER_GROUP_SIZE_MIN/MAX dan STAGGER_STEP_MIN_S/MAX_S.
var DEFAULT_STAGGER_GROUP_SIZE = 3;
var STAGGER_GROUP_SIZE_MIN = 1;
var STAGGER_GROUP_SIZE_MAX = 25;
var DEFAULT_STAGGER_STEP_S = 10;
var STAGGER_STEP_MIN_S = 1;
var STAGGER_STEP_MAX_S = 120;
var CHART_MAX_POINTS = 48;
var CHART_RANGE_MAX_POINTS = 500;
// Grafik V2.5.0: kolom yang dibaca (1-based, mulai kolom A).
// Node A: C timestamp, E lux, F valid, H baterai → A..H (8 kolom).
// Node B: C timestamp, E..J suhu/RH/CO2 + valid, L baterai → A..L (12 kolom).
var CHART_READ_COLS_NODE_A = 8;
var CHART_READ_COLS_NODE_B = 12;
// Pencarian jendela rentang: kolom C dibaca per blok dari bawah.
var CHART_SCAN_CHUNK_ROWS = 2000;
// Berhenti setelah sekian baris berturut-turut lebih tua dari awal rentang.
var CHART_SCAN_STOP_OLDER_ROWS = 50;
// Cache hasil grafik (CacheService): umur 60 s, maks ~100 KB per entri.
var CHART_CACHE_TTL_S = 60;
var CHART_CACHE_MAX_CHARS = 95000;
// Mode gain VEML7700 Node A. Nilai string harus sama dengan firmware V2.3.0.
var GAIN_MODE_LIST = ['auto', '2', '1', '1_4', '1_8'];
var DEFAULT_GAIN_MODE = 'auto';

var NODE_LIST = [
  'A-01','A-02','A-03','A-04','A-05','A-06','A-07','A-08','A-09','A-10',
  'A-11','A-12','A-13','A-14','A-15','A-16','A-17','A-18','A-19','A-20',
  'A-21','A-22','A-23','A-24','A-25',
  'B-01','B-02','B-03'
];

var COLOR_OK      = '#C8E6C9';
var COLOR_WARN    = '#FFF59D';
var COLOR_ERROR   = '#EF9A9A';
var COLOR_OFFLINE = '#E0E0E0';
var COLOR_HEADER  = '#1565C0';

/**
 * Format firmware sebagai teks agar Sheets tidak mengubah "1.3.0" jadi tanggal.
 * Contoh output: Version-1.3.0
 */
function fFirmwareLabel_(raw) {
  var s = '';
  if (raw === null || raw === undefined) {
    s = '';
  } else if (Object.prototype.toString.call(raw) === '[object Date]') {
    // Sudah terkorupsi jadi Date di sheet — tidak bisa dipulihkan akurat.
    s = '';
  } else {
    s = String(raw).trim();
  }

  // Buang prefix lama / noise tanggal panjang.
  if (s.indexOf('Version-') === 0) {
    s = s.substring(8);
  }
  if (s.indexOf('GMT') >= 0 || s.indexOf('Waktu Indonesia') >= 0) {
    s = '';
  }

  if (!s) {
    return 'Version-unknown';
  }
  return 'Version-' + s;
}

// =====================================================
// WEB APP — ESP32 POST + HMI GET
// =====================================================

function doPost(e) {
  try {
    var data = JSON.parse(e.postData.contents);

    // Satu POST, banyak baris. Firmware V2.2.0 selalu mengirim bentuk ini,
    // termasuk ketika antrian kosong (rows berisi satu objek).
    if (data.rows && data.rows.length) {
      var lastRow = vAppendNodeRowsBatch_(data.rows);
      if (!lastRow) {
        return jsonOut_('ERROR: rows tanpa node_id');
      }
      vUpsertDashboardRow_(lastRow);
      return jsonOut_('OK');
    }

    var nodeId = data.node_id;

    if (!nodeId) {
      return jsonOut_('ERROR: node_id kosong');
    }

    vAppendNodeRow_(data);
    vUpsertDashboardRow_(data);

    // Jangan panggil vRefreshDashboardStatus() di sini.
    // Menulis ulang semua baris dashboard membuat doPost lebih lama dari
    // timeout HTTPS ESP32 (15 detik). Node mencatat HTTP -11, tidak
    // memajukan antrian, lalu mengirim timestamp yang sama lagi.
    // Status node yang baru masuk dihitung di vUpsertDashboardRow_.
    // Refresh seluruh dashboard tetap dipakai saat pengaturan HMI berubah.

    return jsonOut_('OK');
  } catch (err) {
    return jsonOut_('ERROR: ' + err.toString());
  }
}

function doGet(e) {
  var action = (e && e.parameter && e.parameter.action)
    ? String(e.parameter.action)
    : '';

  if (action === 'script_url') {
    var nodeId = (e && e.parameter && e.parameter.node_id)
      ? String(e.parameter.node_id)
      : '';
    var payload = {
      ok: true,
      script_url: fGetStoredScriptUrl_(),
      firmware_version: fGetStoredFirmwareVersion_(),
      interval_s: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_(),
      stagger_group_size: fGetStoredStaggerGroupSize_(),
      stagger_step_s: fGetStoredStaggerStepS_()
    };
    var calib = fGetNodeCalibration_(nodeId);
    if (calib) {
      // Gain VEML7700 global, bukan per node. Menimpa nilai lama yang
      // sempat tersimpan di kalibrasi per-node.
      if (String(nodeId).trim().charAt(0).toUpperCase() === 'A') {
        calib.gain_mode = fGetStoredGainMode_();
      }
      payload.calibration = calib;
    }
    return ContentService
      .createTextOutput(JSON.stringify(payload))
      .setMimeType(ContentService.MimeType.JSON);
  }

  // V2.5.0: data dashboard pertama disisipkan langsung ke halaman, jadi HMI
  // tidak perlu satu kali bolak-balik google.script.run sebelum kartu node
  // tampil. Jika gagal, halaman tetap terbuka dan HMI memuat data sendiri.
  var template = HtmlService.createTemplateFromFile('Dashboard');
  template.initialJson = fInitialDashboardJson_();

  return template
    .evaluate()
    .setTitle('HMI Monitoring Bangunan')
    .setXFrameOptionsMode(HtmlService.XFrameOptionsMode.ALLOWALL)
    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

/**
 * JSON getDashboardData() untuk disisipkan ke halaman HMI saat doGet.
 * Karakter "<" di-escape agar isi data (misalnya teks "</script>") tidak
 * bisa menutup tag script di halaman.
 * Output: string JSON, atau string kosong jika pembacaan gagal.
 */
function fInitialDashboardJson_() {
  try {
    return JSON.stringify(getDashboardData()).replace(/</g, '\\u003c');
  } catch (err) {
    return '';
  }
}

/**
 * Link Web App yang dipakai node.
 * Jika belum pernah disimpan dari HMI, pakai URL deployment yang sedang menjawab.
 */
function fIsScriptExecUrl_(url) {
  url = String(url || '').trim();
  if (!url) {
    return false;
  }
  if (url.indexOf('https://script.google.com/') !== 0) {
    return false;
  }
  if (url.indexOf('/exec') < 0) {
    return false;
  }
  if (url.indexOf(' ') >= 0 || url.length > 250) {
    return false;
  }
  if (url.indexOf('docs.google.com/spreadsheets') >= 0) {
    return false;
  }
  return true;
}

function fIsFirmwareVersion_(version) {
  version = String(version || '').trim();
  if (!version || version.length > 15) {
    return false;
  }
  return /^[0-9]+(\.[0-9]+)+$/.test(version);
}

function fGetStoredFirmwareVersion_() {
  var stored = PropertiesService.getScriptProperties().getProperty('FIRMWARE_VERSION');
  if (stored && fIsFirmwareVersion_(stored)) {
    return String(stored).trim();
  }
  return DEFAULT_FIRMWARE_VERSION;
}

/**
 * Timezone global (WIB/WITA/WIT) di Script Properties.
 * Dipakai semua node; diambil saat daya baru ON.
 */
function fIsTimezoneOffset_(tz) {
  tz = String(tz || '').trim();
  return tz === '+07:00' || tz === '+08:00' || tz === '+09:00';
}

function fNormalizeTimezone_(tz) {
  tz = String(tz || '').trim();
  if (fIsTimezoneOffset_(tz)) {
    return tz;
  }
  return DEFAULT_TIMEZONE;
}

// Cache timezone untuk SATU eksekusi. fGetStoredTimezone_ dipanggil per baris
// saat parsing timestamp grafik/status; tanpa cache setiap baris membaca
// Script Properties (ribuan baca untuk grafik rentang panjang).
// Di-reset di setDashboardSettings setelah TIMEZONE ditulis.
var g_timezoneCache_ = null;

function fGetStoredTimezone_() {
  if (g_timezoneCache_ !== null) {
    return g_timezoneCache_;
  }
  var stored = PropertiesService.getScriptProperties().getProperty('TIMEZONE');
  if (stored === null || stored === undefined || String(stored).trim() === '') {
    g_timezoneCache_ = DEFAULT_TIMEZONE;
  } else {
    g_timezoneCache_ = fNormalizeTimezone_(stored);
  }
  return g_timezoneCache_;
}

/**
 * Interval sampling global (detik) di Script Properties.
 * Dipakai semua node; diambil saat daya baru ON.
 */
function fClampIntervalS_(value) {
  var n = Math.round(Number(value));
  if (isNaN(n)) {
    return DEFAULT_INTERVAL_S;
  }
  if (n < INTERVAL_MIN_S) {
    return INTERVAL_MIN_S;
  }
  if (n > INTERVAL_MAX_S) {
    return INTERVAL_MAX_S;
  }
  return n;
}

function fGetStoredIntervalS_() {
  var stored = PropertiesService.getScriptProperties().getProperty('SAMPLE_INTERVAL_S');
  if (stored === null || stored === undefined || String(stored).trim() === '') {
    return DEFAULT_INTERVAL_S;
  }
  return fClampIntervalS_(stored);
}

/**
 * Bilangan bulat dalam rentang [minV, maxV]. Selain itu -> null.
 */
function fIntInRange_(value, minV, maxV) {
  var n = Number(value);
  if (isNaN(n) || Math.round(n) !== n) {
    return null;
  }
  if (n < minV || n > maxV) {
    return null;
  }
  return n;
}

/**
 * Jumlah alat per rombongan kirim (global, semua node).
 * Diambil node saat daya baru ON.
 */
function fGetStoredStaggerGroupSize_() {
  var stored = PropertiesService.getScriptProperties().getProperty('STAGGER_GROUP_SIZE');
  var n = fIntInRange_(stored, STAGGER_GROUP_SIZE_MIN, STAGGER_GROUP_SIZE_MAX);
  if (stored === null || String(stored).trim() === '' || n === null) {
    return DEFAULT_STAGGER_GROUP_SIZE;
  }
  return n;
}

/**
 * Jeda antar rombongan kirim (detik, global, semua node).
 * Diambil node saat daya baru ON.
 */
function fGetStoredStaggerStepS_() {
  var stored = PropertiesService.getScriptProperties().getProperty('STAGGER_STEP_S');
  var n = fIntInRange_(stored, STAGGER_STEP_MIN_S, STAGGER_STEP_MAX_S);
  if (stored === null || String(stored).trim() === '' || n === null) {
    return DEFAULT_STAGGER_STEP_S;
  }
  return n;
}

// Cache setting Dashboard (B3 usia, B4 low bat) untuk SATU eksekusi saja.
// Variabel global Apps Script hidup per eksekusi, jadi tiap poll HMI / POST
// node tetap membaca nilai terbaru dari Sheet, tetapi cukup sekali.
// Wajib di-reset (vInvalidateDashboardSettings_) setelah menulis B3:B5.
var g_dashSettingsCache_ = null;

/**
 * Baca setting Dashboard A3:C5 dalam SATU panggilan Sheet dan perbaiki sel
 * yang kosong/rusak. Menulis ke Sheet HANYA bila perlu perbaikan, karena
 * tulis Sheet adalah operasi paling mahal (V2.5.0: sebelumnya setNote dan
 * setNumberFormat ditulis setiap poll HMI dan setiap POST node).
 *
 * Output: { sh, maxAge, batteryLow } atau null jika sheet Dashboard belum ada.
 */
function fReadDashboardSettings_() {
  if (g_dashSettingsCache_) {
    return g_dashSettingsCache_;
  }

  var sh = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(DASHBOARD_NAME);
  if (!sh) {
    return null;
  }

  // Baris 3..5, kolom A..C. Index [baris][kolom]: [0]=baris 3, [0][1]=B3.
  var block = sh.getRange('A3:C5').getValues();

  if (!String(block[0][0] || '').trim()) {
    sh.getRange('A3').setValue('Batas usia data (detik)');
  }
  var maxAge = Number(block[0][1]);
  if (!maxAge || maxAge < 10) {
    maxAge = DEFAULT_MAX_AGE_S;
    sh.getRange('B3').setValue(maxAge).setNote(
      'Tes interval 30 detik: 90–180. Production interval 5 menit: 600–900. ' +
      'Bisa diubah dari HMI tombol Pengaturan.'
    );
  }
  if (!String(block[0][2] || '').trim()) {
    sh.getRange('C3').setValue('Terakhir diperbarui');
  }

  if (!String(block[1][0] || '').trim()) {
    sh.getRange('A4').setValue('Baterai low (V)');
  }
  var lowBat = Number(block[1][1]);
  if (!lowBat || lowBat < 2.5 || lowBat > 5.0) {
    lowBat = DEFAULT_BATTERY_LOW_V;
    sh.getRange('B4').setValue(lowBat).setNote(
      'Di bawah nilai ini status menjadi BATERAI LOW. Default 3.50 V. ' +
      'Bisa diubah dari HMI tombol Pengaturan.'
    );
  }

  if (!String(block[2][0] || '').trim()) {
    sh.getRange('A5').setValue('Timezone lokasi');
  }
  // B5 harus teks. Jika format bukan teks, Sheets mengubah "+07:00" jadi jam
  // (Date), sehingga pengecekan di bawah gagal dan sel diperbaiki di sini.
  var tzCell = String(block[2][1] || '').trim();
  if (!fIsTimezoneOffset_(tzCell)) {
    sh.getRange('B5')
      .setNumberFormat('@')
      .setValue(fGetStoredTimezone_())
      .setNote(
        'WIB +07:00 | WITA +08:00 (Bali) | WIT +09:00. ' +
        'Diubah dari HMI Pengaturan. Node ambil saat cold power.'
      );
  }

  g_dashSettingsCache_ = {
    sh: sh,
    maxAge: Math.round(maxAge),
    batteryLow: Math.round(lowBat * 100) / 100
  };
  return g_dashSettingsCache_;
}

/**
 * Buang cache setting agar pembacaan berikutnya mengambil ulang dari Sheet.
 * Dipanggil setelah B3:B5 ditulis (setDashboardSettings, vInitDashboard).
 */
function vInvalidateDashboardSettings_() {
  g_dashSettingsCache_ = null;
}

/**
 * Pastikan baris setting Status ada (B3 usia, B4 low bat, B5 timezone)
 * tanpa wipe sheet. Mengembalikan sheet Dashboard atau null.
 */
function fEnsureDashboardSettingsRows_() {
  var settings = fReadDashboardSettings_();
  return settings ? settings.sh : null;
}

function fGetMaxAgeS_() {
  var settings = fReadDashboardSettings_();
  return settings ? settings.maxAge : DEFAULT_MAX_AGE_S;
}

function fGetBatteryLowV_() {
  var settings = fReadDashboardSettings_();
  return settings ? settings.batteryLow : DEFAULT_BATTERY_LOW_V;
}

function fGetStoredScriptUrl_() {
  var stored = PropertiesService.getScriptProperties().getProperty('SCRIPT_API_URL');
  if (stored && fIsScriptExecUrl_(stored)) {
    return String(stored).trim();
  }

  var live = '';
  try {
    live = ScriptApp.getService().getUrl() || '';
  } catch (err) {
    live = '';
  }
  if (fIsScriptExecUrl_(live)) {
    return String(live).trim();
  }

  // Fallback: URL default V2.1.0 (sama dengan firmware).
  return DEFAULT_SCRIPT_EXEC_URL;
}

/**
 * Dibaca HMI untuk menampilkan link yang sekarang tersimpan.
 */
function getScriptApiUrl() {
  return {
    ok: true,
    script_url: fGetStoredScriptUrl_(),
    firmware_version: fGetStoredFirmwareVersion_()
  };
}

/**
 * Simpan link API saja. confirmText harus persis "GANTI LINK".
 * Versi target tidak diubah di sini.
 */
function setScriptApiUrl(newUrl, confirmText) {
  if (String(confirmText || '').trim().toUpperCase() !== 'GANTI LINK') {
    return {
      ok: false,
      message: 'Konfirmasi salah. Ketik GANTI LINK.',
      script_url: fGetStoredScriptUrl_()
    };
  }

  var url = String(newUrl || '').trim();
  if (!fIsScriptExecUrl_(url)) {
    return {
      ok: false,
      message: 'Link harus https://script.google.com/.../exec. Bukan link spreadsheet /edit.',
      script_url: fGetStoredScriptUrl_()
    };
  }

  PropertiesService.getScriptProperties().setProperty('SCRIPT_API_URL', url);

  return {
    ok: true,
    message: 'Link API tersimpan. Node mengambilnya saat daya baru dinyalakan.',
    script_url: url
  };
}

/**
 * Simpan versi target saja. confirmText harus persis "GANTI VERSI".
 * Link API tidak diubah di sini.
 * Jika angka ini berbeda dari firmware di alat, node mengunduh bin saat daya baru ON.
 */
function setFirmwareVersion(newVersion, confirmText) {
  if (String(confirmText || '').trim().toUpperCase() !== 'GANTI VERSI') {
    return {
      ok: false,
      message: 'Konfirmasi salah. Ketik GANTI VERSI.',
      firmware_version: fGetStoredFirmwareVersion_()
    };
  }

  var version = String(newVersion || '').trim();
  if (!fIsFirmwareVersion_(version)) {
    return {
      ok: false,
      message: 'Versi harus seperti 1.9.0',
      firmware_version: fGetStoredFirmwareVersion_()
    };
  }

  PropertiesService.getScriptProperties().setProperty('FIRMWARE_VERSION', version);

  return {
    ok: true,
    message: 'Versi tersimpan. Jika berbeda dari firmware di alat, node mengunduh bin saat daya baru dinyalakan.',
    firmware_version: version
  };
}

/**
 * Dibaca HMI untuk mengisi form Pengaturan.
 */
function getDashboardSettings() {
  return {
    ok: true,
    maxAge: fGetMaxAgeS_(),
    batteryLow: fGetBatteryLowV_(),
    intervalS: fGetStoredIntervalS_(),
    intervalMin: INTERVAL_MIN_S,
    intervalMax: INTERVAL_MAX_S,
    timezone: fGetStoredTimezone_(),
    gainMode: fGetStoredGainMode_(),
    staggerGroupSize: fGetStoredStaggerGroupSize_(),
    staggerGroupSizeMin: STAGGER_GROUP_SIZE_MIN,
    staggerGroupSizeMax: STAGGER_GROUP_SIZE_MAX,
    staggerStepS: fGetStoredStaggerStepS_(),
    staggerStepMin: STAGGER_STEP_MIN_S,
    staggerStepMax: STAGGER_STEP_MAX_S
  };
}

/**
 * Simpan pengaturan dashboard. confirmText harus persis "SIMPAN SETTING".
 * maxAge -> B3, batteryLow -> B4, intervalS -> SAMPLE_INTERVAL_S,
 * timezone -> TIMEZONE + B5.
 * gainMode -> VEML_GAIN_MODE (satu nilai untuk semua Node A).
 * staggerGroupSize -> STAGGER_GROUP_SIZE, staggerStepS -> STAGGER_STEP_S (V2.5.0).
 */
function setDashboardSettings(maxAge, batteryLow, intervalS, timezone, gainMode,
    staggerGroupSize, staggerStepS, confirmText) {
  if (String(confirmText || '').trim().toUpperCase() !== 'SIMPAN SETTING') {
    return {
      ok: false,
      message: 'Konfirmasi salah. Ketik SIMPAN SETTING.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }

  var age = Math.round(Number(maxAge));
  if (isNaN(age) || age < 10 || age > 86400) {
    return {
      ok: false,
      message: 'Batas usia harus angka 10–86400 detik.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }

  var low = Math.round(Number(batteryLow) * 100) / 100;
  if (isNaN(low) || low < 2.5 || low > 5.0) {
    return {
      ok: false,
      message: 'Baterai low harus angka 2.50–5.00 V.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }

  var intervalRaw = Number(intervalS);
  if (isNaN(intervalRaw) || intervalRaw < INTERVAL_MIN_S || intervalRaw > INTERVAL_MAX_S) {
    return {
      ok: false,
      message: 'Interval harus ' + INTERVAL_MIN_S + '–' + INTERVAL_MAX_S + ' detik.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }
  var interval = fClampIntervalS_(intervalRaw);

  if (!fIsTimezoneOffset_(timezone)) {
    return {
      ok: false,
      message: 'Timezone harus +07:00 (WIB), +08:00 (WITA), atau +09:00 (WIT).',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }
  var tz = fNormalizeTimezone_(timezone);

  if (!fIsValidGainMode_(gainMode)) {
    return {
      ok: false,
      message: 'Mode gain harus Autorange, 2x, 1x, 1/4x, atau 1/8x.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }
  var gain = String(gainMode).trim();

  var groupSize = fIntInRange_(staggerGroupSize, STAGGER_GROUP_SIZE_MIN, STAGGER_GROUP_SIZE_MAX);
  if (groupSize === null) {
    return {
      ok: false,
      message: 'Jumlah alat per rombongan harus bilangan bulat ' +
        STAGGER_GROUP_SIZE_MIN + '–' + STAGGER_GROUP_SIZE_MAX + '.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }

  var stepS = fIntInRange_(staggerStepS, STAGGER_STEP_MIN_S, STAGGER_STEP_MAX_S);
  if (stepS === null) {
    return {
      ok: false,
      message: 'Jeda antar rombongan harus bilangan bulat ' +
        STAGGER_STEP_MIN_S + '–' + STAGGER_STEP_MAX_S + ' detik.',
      maxAge: fGetMaxAgeS_(),
      batteryLow: fGetBatteryLowV_(),
      intervalS: fGetStoredIntervalS_(),
      timezone: fGetStoredTimezone_()
    };
  }

  // Hanya peringatan (tidak menolak simpan): interval tes pendek memang bisa
  // lebih kecil dari offset. Firmware membungkus offset dengan modulo interval,
  // sehingga node akhir bisa kirim bersamaan dengan rombongan awal.
  var lastOffsetS = Math.floor((STAGGER_GROUP_SIZE_MAX - 1) / groupSize) * stepS;
  var staggerWarning = '';
  if (lastOffsetS >= interval) {
    staggerWarning =
      ' PERINGATAN: node ke-' + STAGGER_GROUP_SIZE_MAX + ' dijadwalkan +' + lastOffsetS +
      ' s, melebihi interval ' + interval + ' s (jadwal dibungkus ke awal slot).';
  }

  var sh = fEnsureDashboardSettingsRows_();
  if (!sh) {
    vInitDashboard();
    sh = fEnsureDashboardSettingsRows_();
  }

  // B5 diformat teks DULU agar "+07:00" tidak diubah Sheets menjadi jam.
  // Lalu A3:B5 ditulis dalam satu setValues.
  sh.getRange('B5').setNumberFormat('@');
  sh.getRange('A3:B5').setValues([
    ['Batas usia data (detik)', age],
    ['Baterai low (V)', low],
    ['Timezone lokasi', tz]
  ]);
  // vRefreshDashboardStatus di bawah harus membaca usia/low bat yang baru.
  vInvalidateDashboardSettings_();
  PropertiesService.getScriptProperties().setProperty(
    'SAMPLE_INTERVAL_S',
    String(interval)
  );
  PropertiesService.getScriptProperties().setProperty('TIMEZONE', tz);
  g_timezoneCache_ = null;
  PropertiesService.getScriptProperties().setProperty('VEML_GAIN_MODE', gain);
  PropertiesService.getScriptProperties().setProperty('STAGGER_GROUP_SIZE', String(groupSize));
  PropertiesService.getScriptProperties().setProperty('STAGGER_STEP_S', String(stepS));

  vRefreshDashboardStatus();

  return {
    ok: true,
    message:
      'Pengaturan tersimpan. Usia/low bat langsung dipakai dashboard. ' +
      'Interval, timezone, mode gain VEML7700, jumlah alat per rombongan, dan jeda ' +
      'diambil semua node saat daya baru dinyalakan.' + staggerWarning,
    maxAge: age,
    batteryLow: low,
    intervalS: interval,
    timezone: tz,
    gainMode: gain,
    staggerGroupSize: groupSize,
    staggerStepS: stepS
  };
}

/**
 * Mode gain VEML7700 global. Satu nilai untuk semua Node A.
 * Kosong atau tidak dikenal -> autorange.
 */
function fGetStoredGainMode_() {
  var raw = PropertiesService.getScriptProperties().getProperty('VEML_GAIN_MODE');
  if (!fIsValidGainMode_(raw)) {
    return DEFAULT_GAIN_MODE;
  }
  return String(raw).trim();
}

/**
 * Default kalibrasi sama dengan firmware.
 * Sensor m=1 c=0. Baterai dari regresi multimeter.
 */
function fDefaultCalibration_(nodeId) {
  var isA = String(nodeId || '').charAt(0).toUpperCase() === 'A';
  if (isA) {
    return {
      lux_m: 1,
      lux_c: 0,
      bat_m: 0.667031,
      bat_c: 0.164867
    };
  }
  return {
    temp_m: 1,
    temp_c: 0,
    rh_m: 1,
    rh_c: 0,
    co2_m: 1,
    co2_c: 0,
    bat_m: 0.667031,
    bat_c: 0.164867
  };
}

function fIsKnownNodeId_(nodeId) {
  nodeId = String(nodeId || '').trim().toUpperCase();
  for (var i = 0; i < NODE_LIST.length; i++) {
    if (NODE_LIST[i] === nodeId) {
      return true;
    }
  }
  return false;
}

function fCalibPropertyKey_(nodeId) {
  return 'CALIB_' + String(nodeId || '').trim().toUpperCase();
}

function fIsFiniteNumber_(value) {
  var n = Number(value);
  return isFinite(n);
}

/**
 * Mode gain VEML7700 yang dikenal firmware V2.3.0.
 * 'auto' = autorange (mulai 1/8x lalu naik/turun satu langkah per siklus).
 * Selain itu gain tetap: 2x, 1x, 1/4x, 1/8x.
 */
function fIsValidGainMode_(value) {
  var s = String(value === null || value === undefined ? '' : value).trim();
  for (var i = 0; i < GAIN_MODE_LIST.length; i++) {
    if (GAIN_MODE_LIST[i] === s) {
      return true;
    }
  }
  return false;
}

/**
 * Membaca kalibrasi satu node. Null jika node_id kosong/tidak dikenal.
 * Jika belum pernah disimpan, mengembalikan default.
 */
function fGetNodeCalibration_(nodeId) {
  nodeId = String(nodeId || '').trim().toUpperCase();
  if (!fIsKnownNodeId_(nodeId)) {
    return null;
  }

  var raw = PropertiesService.getScriptProperties().getProperty(fCalibPropertyKey_(nodeId));
  if (!raw) {
    return fDefaultCalibration_(nodeId);
  }

  try {
    var parsed = JSON.parse(raw);
    if (!parsed || typeof parsed !== 'object') {
      return fDefaultCalibration_(nodeId);
    }
    return parsed;
  } catch (err) {
    return fDefaultCalibration_(nodeId);
  }
}

/**
 * Dipanggil HMI saat dialog detail dibuka.
 */
function getNodeCalibration(nodeId) {
  nodeId = String(nodeId || '').trim().toUpperCase();
  if (!fIsKnownNodeId_(nodeId)) {
    return {
      ok: false,
      message: 'Node ID tidak dikenal.',
      node_id: nodeId,
      calibration: null
    };
  }

  return {
    ok: true,
    node_id: nodeId,
    jenis: nodeId.charAt(0) === 'A' ? 'Iluminansi' : 'IAQ',
    calibration: fGetNodeCalibration_(nodeId)
  };
}

/**
 * Menyimpan kalibrasi satu node. Konfirmasi satu langkah dari HMI.
 * Menyimpan A-01 tidak mengubah node lain.
 */
function setNodeCalibration(nodeId, calibObj) {
  nodeId = String(nodeId || '').trim().toUpperCase();
  if (!fIsKnownNodeId_(nodeId)) {
    return {
      ok: false,
      message: 'Node ID tidak dikenal.',
      node_id: nodeId
    };
  }

  var isA = nodeId.charAt(0) === 'A';
  var src = calibObj || {};
  var out = {};

  function takePair(prefix) {
    var mKey = prefix + '_m';
    var cKey = prefix + '_c';
    if (!fIsFiniteNumber_(src[mKey]) || !fIsFiniteNumber_(src[cKey])) {
      return false;
    }
    out[mKey] = Number(src[mKey]);
    out[cKey] = Number(src[cKey]);
    return true;
  }

  if (isA) {
    if (!takePair('lux') || !takePair('bat')) {
      return {
        ok: false,
        message: 'Isi lux m/c dan baterai m/c dengan angka.',
        node_id: nodeId
      };
    }
  } else {
    if (!takePair('temp') || !takePair('rh') || !takePair('co2') || !takePair('bat')) {
      return {
        ok: false,
        message: 'Isi suhu, RH, CO2, dan baterai m/c dengan angka.',
        node_id: nodeId
      };
    }
  }

  PropertiesService.getScriptProperties().setProperty(
    fCalibPropertyKey_(nodeId),
    JSON.stringify(out)
  );

  return {
    ok: true,
    message: 'Kalibrasi ' + nodeId + ' tersimpan. Node mengambilnya saat daya baru dinyalakan.',
    node_id: nodeId,
    calibration: out
  };
}

/**
 * Dipanggil dari HMI setiap ~20 detik (V2.5.0, sebelumnya 10 detik).
 *
 * Optimasi: status/usia dihitung di memori dari kolom last timestamp + battery
 * + sensor, TANPA menulis ulang seluruh Dashboard (tulis Sheet mahal).
 * POST node hanya memperbarui satu baris. Seluruh Dashboard ditulis ulang
 * saat pengaturan HMI berubah (vRefreshDashboardStatus).
 */
function getDashboardData() {
  var sh = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(DASHBOARD_NAME);
  if (!sh) {
    vInitDashboard();
    sh = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(DASHBOARD_NAME);
  }

  // Satu kali baca A3:C5 (cache per eksekusi), dipakai kedua getter.
  var maxAge = fGetMaxAgeS_();
  var batteryLow = fGetBatteryLowV_();
  var now = new Date();
  var last = sh.getLastRow();
  var nodes = [];
  var summary = { total: 0, ok: 0, warn: 0, error: 0, offline: 0 };

  if (last >= 6) {
    var n = last - 5;
    // Baca sekali: id, jenis, (status lama diabaikan), age lama, last ts, nilai, bat, sensor, fw, action lama
    var values = sh.getRange(6, 1, n, 10).getValues();

    for (var i = 0; i < n; i++) {
      var lastUtc = String(values[i][4] || '');
      var battery = Number(values[i][6]);
      var sensorStatus = values[i][7];
      var info = fComputeStatus_(
        lastUtc,
        battery,
        sensorStatus,
        now,
        maxAge,
        batteryLow
      );
      var status = info.status;

      var node = {
        id: String(values[i][0] || ''),
        jenis: String(values[i][1] || ''),
        status: status,
        age: info.age,
        lastUtc: lastUtc,
        nilai: String(values[i][5] || ''),
        battery: values[i][6],
        sensorStatus: sensorStatus,
        firmware: fFirmwareLabel_(values[i][8]),
        action: info.action
      };
      nodes.push(node);
      summary.total++;
      if (status === 'OK') { summary.ok++; }
      else if (status === 'TERLAMBAT') { summary.warn++; }
      else if (status === 'ERROR SENSOR' || status === 'BATERAI LOW') { summary.error++; }
      else { summary.offline++; }
    }
  }

  return {
    updated: now.toISOString(),
    maxAge: maxAge,
    batteryLow: batteryLow,
    intervalS: fGetStoredIntervalS_(),
    timezone: fGetStoredTimezone_(),
    summary: summary,
    nodes: nodes
  };
}

/**
 * Data grafik histori satu node.
 *
 * Mode rentang (utama V2.1.0):
 *   getNodeChartData(nodeId, { from: '2026-09-21T08:00:00+07:00', to: '...' })
 *
 * Mode lama (kompatibel):
 *   getNodeChartData(nodeId, 48)  → N baris terakhir
 *
 * Output: { nodeId, jenis, labels[], series{}, from, to, truncated, totalMatched }
 */
function fChartNumber_(cell, validCell) {
  if (validCell !== undefined && validCell !== '' && Number(validCell) === 0) {
    return null;
  }
  if (cell === '' || cell === null) {
    return null;
  }
  var n = Number(cell);
  return isNaN(n) ? null : n;
}

/**
 * Parse timestamp Sheet (teks ISO lokal/UTC atau objek Date) ke epoch ms.
 * Jika string tanpa Z / offset (contoh 2026-09-25T19:55:00),
 * tempel timezone global Sheet agar usia/grafik tetap akurat.
 */
function fNormalizeIsoForParse_(s) {
  s = String(s || '').trim();
  if (!s || s === '--') {
    return '';
  }
  if (s.charAt(0) === "'") {
    s = s.substring(1);
  }
  // Sudah ada Z atau +07:00 / -05:00 di akhir.
  if (/[zZ]$/.test(s) || /[+-]\d{2}:\d{2}$/.test(s)) {
    return s;
  }
  // Bentuk lokal tanpa offset → lampirkan timezone HMI/Sheet.
  if (/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}$/.test(s)) {
    return s + fGetStoredTimezone_();
  }
  return s;
}

/**
 * Teks timestamp untuk sel Sheet.
 * Awalan ' memaksa format teks, supaya Google Sheet tidak mengubah
 * "2026-09-28T12:45:00" menjadi tanggal yang tampil "2026-09-28 12:45:00".
 * Apostrof tidak ikut terlihat di sel. Pembaca (fTimestampKey_, grafik)
 * sudah membuang apostrof itu.
 */
function fSheetTimestampText_(value) {
  var s = String(value || '').trim();
  if (!s) {
    return '';
  }
  if (s.charAt(0) === "'") {
    return s;
  }
  return "'" + s;
}

/**
 * Label sumbu grafik: selalu ada tanggal dan jam.
 * Date dari sel Sheet diubah ke teks, supaya HMI tidak hanya menampilkan tanggal.
 */
function fChartLabel_(raw) {
  var zone = 'Asia/Jakarta';
  var tz = String(fGetStoredTimezone_() || '');
  if (tz.indexOf('+08') >= 0) {
    zone = 'Asia/Makassar';
  } else if (tz.indexOf('+09') >= 0) {
    zone = 'Asia/Jayapura';
  }

  if (Object.prototype.toString.call(raw) === '[object Date]' && !isNaN(raw.getTime())) {
    return Utilities.formatDate(raw, zone, "yyyy-MM-dd'T'HH:mm:ss");
  }

  var s = String(raw || '').trim();
  if (s.charAt(0) === "'") {
    s = s.substring(1);
  }
  return s;
}

function fParseChartTimestampMs_(raw) {
  if (Object.prototype.toString.call(raw) === '[object Date]') {
    var msDate = raw.getTime();
    return isNaN(msDate) ? NaN : msDate;
  }
  var s = fNormalizeIsoForParse_(raw);
  if (!s) {
    return NaN;
  }
  var ms = new Date(s).getTime();
  return isNaN(ms) ? NaN : ms;
}

function fEmptyChartPayload_(nodeId, isNodeA, fromIso, toIso) {
  return {
    nodeId: nodeId,
    jenis: isNodeA ? 'Iluminansi' : 'IAQ',
    labels: [],
    series: isNodeA
      ? { illuminance: [], battery: [] }
      : { temperature: [], humidity: [], co2: [], battery: [] },
    from: fromIso || '',
    to: toIso || '',
    truncated: false,
    totalMatched: 0
  };
}

function getNodeChartData(nodeId, rangeOrPoints) {
  nodeId = String(nodeId || '');
  var isNodeA = (nodeId.charAt(0).toUpperCase() === 'A');
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(nodeId);

  var useRange = false;
  var fromMs = NaN;
  var toMs = NaN;
  var fromIso = '';
  var toIso = '';
  var maxPoints = CHART_MAX_POINTS;

  if (rangeOrPoints && typeof rangeOrPoints === 'object') {
    fromIso = String(rangeOrPoints.from || '').trim();
    toIso = String(rangeOrPoints.to || '').trim();
    fromMs = fParseChartTimestampMs_(fromIso);
    toMs = fParseChartTimestampMs_(toIso);
    useRange = !isNaN(fromMs) && !isNaN(toMs);
    if (useRange && toMs < fromMs) {
      var tmpMs = fromMs;
      fromMs = toMs;
      toMs = tmpMs;
      var tmpIso = fromIso;
      fromIso = toIso;
      toIso = tmpIso;
    }
  } else {
    maxPoints = Number(rangeOrPoints) || CHART_MAX_POINTS;
    if (maxPoints < 5) { maxPoints = 5; }
    if (maxPoints > 200) { maxPoints = 200; }
  }

  var empty = fEmptyChartPayload_(nodeId, isNodeA, fromIso, toIso);
  if (!sheet || sheet.getLastRow() < 2) {
    return empty;
  }

  if (useRange && (isNaN(fromMs) || isNaN(toMs))) {
    empty.message = 'Rentang tanggal/jam tidak valid.';
    return empty;
  }

  // Mode rentang: coba ambil hasil yang sama dari cache (dibuat < 60 s lalu).
  var cacheKey = '';
  if (useRange) {
    cacheKey = fChartCacheKey_(nodeId, fromIso, toIso);
    var cached = fReadChartCache_(cacheKey);
    if (cached) {
      return cached;
    }
  }

  var last = sheet.getLastRow();
  // Hanya kolom yang dipakai grafik, bukan getLastColumn().
  var readCols = Math.min(
    isNodeA ? CHART_READ_COLS_NODE_A : CHART_READ_COLS_NODE_B,
    Math.max(1, sheet.getLastColumn())
  );
  var values;

  if (useRange) {
    // V2.5.0: cari dulu jendela baris yang masuk rentang (hanya kolom C,
    // dibaca per blok dari bawah), lalu baca kolom data untuk jendela itu
    // saja. Sebelumnya seluruh histori (semua baris, semua kolom) dibaca.
    var rowWindow = fFindChartRowWindow_(sheet, last, fromMs, toMs);
    if (!rowWindow) {
      return empty;
    }
    values = sheet.getRange(
      rowWindow.firstRow,
      1,
      rowWindow.lastRow - rowWindow.firstRow + 1,
      readCols
    ).getValues();
  } else {
    var startRow = Math.max(2, last - maxPoints + 1);
    values = sheet.getRange(startRow, 1, last - startRow + 1, readCols).getValues();
  }

  var labels = [];
  var sIll = [];
  var sTemp = [];
  var sRh = [];
  var sCo2 = [];
  var sBatt = [];

  for (var i = 0; i < values.length; i++) {
    var tsRaw = values[i][2];
    if (useRange) {
      // Jendela bisa berisi baris di luar rentang jika urutan waktu di sheet
      // tidak rapi (misalnya backfill), jadi tetap disaring per baris.
      var rowMs = fParseChartTimestampMs_(tsRaw);
      if (isNaN(rowMs) || rowMs < fromMs || rowMs > toMs) {
        continue;
      }
    }

    labels.push(fChartLabel_(tsRaw));
    if (isNodeA) {
      sIll.push(fChartNumber_(values[i][4], values[i][5]));
      sBatt.push(fChartNumber_(values[i][7], 1));
    } else {
      sTemp.push(fChartNumber_(values[i][4], values[i][5]));
      sRh.push(fChartNumber_(values[i][6], values[i][7]));
      sCo2.push(fChartNumber_(values[i][8], values[i][9]));
      sBatt.push(fChartNumber_(values[i][11], 1));
    }
  }

  var totalMatched = labels.length;

  // V2.5.0: jika titik lebih dari batas, ambil titik MERATA di seluruh
  // rentang (titik pertama dan terakhir selalu ikut). Sebelumnya hanya
  // 500 titik pertama yang dikirim, sehingga data terbaru tidak tampil.
  var downsampled = false;
  if (useRange && totalMatched > CHART_RANGE_MAX_POINTS) {
    var pick = fDownsampleIndices_(totalMatched, CHART_RANGE_MAX_POINTS);
    labels = fPickByIndices_(labels, pick);
    sIll = fPickByIndices_(sIll, pick);
    sTemp = fPickByIndices_(sTemp, pick);
    sRh = fPickByIndices_(sRh, pick);
    sCo2 = fPickByIndices_(sCo2, pick);
    sBatt = fPickByIndices_(sBatt, pick);
    downsampled = true;
  }

  var result;
  if (isNodeA) {
    result = {
      nodeId: nodeId,
      jenis: 'Iluminansi',
      labels: labels,
      series: { illuminance: sIll, battery: sBatt },
      from: fromIso,
      to: toIso,
      truncated: downsampled,
      downsampled: downsampled,
      totalMatched: totalMatched
    };
  } else {
    result = {
      nodeId: nodeId,
      jenis: 'IAQ',
      labels: labels,
      series: {
        temperature: sTemp,
        humidity: sRh,
        co2: sCo2,
        battery: sBatt
      },
      from: fromIso,
      to: toIso,
      truncated: downsampled,
      downsampled: downsampled,
      totalMatched: totalMatched
    };
  }

  if (useRange) {
    vWriteChartCache_(cacheKey, result);
  }
  return result;
}

/**
 * Cari jendela baris (firstRow..lastRow) di sheet node yang timestamp-nya
 * masuk rentang [fromMs, toMs].
 *
 * Cara kerja:
 * - Hanya kolom C (timestamp) yang dibaca, per blok CHART_SCAN_CHUNK_ROWS
 *   baris dari bawah ke atas, karena node menulis baris urut waktu dan
 *   rentang yang sering dilihat adalah data terbaru.
 * - Pencarian berhenti setelah CHART_SCAN_STOP_OLDER_ROWS baris berturut-turut
 *   lebih tua dari fromMs. Toleransi ini menjaga hasil tetap benar bila ada
 *   sedikit baris yang tidak urut (misalnya hasil backfill).
 *
 * Input: sheet node, last = getLastRow(), fromMs/toMs epoch ms.
 * Output: { firstRow, lastRow } (nomor baris Sheet), atau null jika tidak ada
 * baris yang cocok.
 */
function fFindChartRowWindow_(sheet, last, fromMs, toMs) {
  var firstRow = -1;
  var lastRow = -1;
  var olderRun = 0;
  var endRow = last;

  while (endRow >= 2) {
    var startRow = Math.max(2, endRow - CHART_SCAN_CHUNK_ROWS + 1);
    var col = sheet.getRange(startRow, 3, endRow - startRow + 1, 1).getValues();

    for (var i = col.length - 1; i >= 0; i--) {
      var ms = fParseChartTimestampMs_(col[i][0]);
      if (isNaN(ms)) {
        continue;
      }
      if (ms > toMs) {
        olderRun = 0;
        continue;
      }
      if (ms >= fromMs) {
        var rowNumber = startRow + i;
        if (lastRow < 0) {
          lastRow = rowNumber;
        }
        firstRow = rowNumber;
        olderRun = 0;
        continue;
      }
      // ms < fromMs: baris lebih tua dari rentang.
      olderRun++;
      if (olderRun >= CHART_SCAN_STOP_OLDER_ROWS) {
        return (lastRow < 0) ? null : { firstRow: firstRow, lastRow: lastRow };
      }
    }
    endRow = startRow - 1;
  }

  return (lastRow < 0) ? null : { firstRow: firstRow, lastRow: lastRow };
}

/**
 * Index titik yang diambil merata dari n titik menjadi maxPoints titik.
 * Titik pertama (0) dan terakhir (n-1) selalu ikut.
 *
 * Input: n jumlah titik asli, maxPoints jumlah titik hasil (>= 2).
 * Output: array index naik, panjang maxPoints.
 */
function fDownsampleIndices_(n, maxPoints) {
  var indices = [];
  var step = (n - 1) / (maxPoints - 1);
  for (var k = 0; k < maxPoints; k++) {
    indices.push(Math.round(k * step));
  }
  return indices;
}

/**
 * Ambil elemen array sesuai daftar index. Array kosong (series yang tidak
 * dipakai tipe node ini) dikembalikan kosong.
 */
function fPickByIndices_(arr, indices) {
  if (!arr.length) {
    return arr;
  }
  var out = [];
  for (var i = 0; i < indices.length; i++) {
    out.push(arr[indices[i]]);
  }
  return out;
}

/**
 * Kunci cache grafik. Memuat "generasi" dari Script Properties
 * (CHART_CACHE_GEN) yang diganti saat data dihapus dari HMI, supaya cache
 * lama otomatis tidak terpakai lagi.
 */
function fChartCacheKey_(nodeId, fromIso, toIso) {
  var gen = PropertiesService.getScriptProperties().getProperty('CHART_CACHE_GEN') || '0';
  return 'chart|' + gen + '|' + nodeId + '|' + fromIso + '|' + toIso;
}

/**
 * Baca hasil grafik dari CacheService. Output: objek payload atau null.
 * Kegagalan cache tidak boleh menggagalkan grafik, jadi error diabaikan.
 */
function fReadChartCache_(key) {
  try {
    var text = CacheService.getScriptCache().get(key);
    return text ? JSON.parse(text) : null;
  } catch (err) {
    return null;
  }
}

/**
 * Simpan hasil grafik ke CacheService selama CHART_CACHE_TTL_S detik.
 * Dilewati jika JSON melebihi batas satu entri cache (100 KB).
 */
function vWriteChartCache_(key, payload) {
  try {
    var text = JSON.stringify(payload);
    if (text.length <= CHART_CACHE_MAX_CHARS) {
      CacheService.getScriptCache().put(key, text, CHART_CACHE_TTL_S);
    }
  } catch (err) {
    // Cache hanya percepatan; grafik tetap dikirim walau cache gagal.
  }
}

/**
 * Hapus SEMUA data histori node + reset Dashboard ke awal.
 * Dipanggil dari HMI setelah konfirmasi ketik HAPUS SEMUA.
 *
 * Input: confirmText harus "HAPUS SEMUA"
 * Output: { ok, message, clearedSheets }
 */
function deleteAllNodeData(confirmText) {
  if (String(confirmText || '').trim().toUpperCase() !== 'HAPUS SEMUA') {
    return {
      ok: false,
      message: 'Konfirmasi salah. Ketik HAPUS SEMUA.',
      clearedSheets: 0
    };
  }

  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var cleared = 0;

  for (var i = 0; i < NODE_LIST.length; i++) {
    var id = NODE_LIST[i];
    var sh = ss.getSheetByName(id);
    if (!sh) {
      continue;
    }

    var last = sh.getLastRow();
    var cols = Math.max(1, sh.getLastColumn());
    if (last >= 2) {
      sh.getRange(2, 1, last - 1, cols).clearContent();
      cleared++;
    } else if (last === 1) {
      cleared++;
    }
  }

  // Reset halaman Dashboard (status awal).
  vInitDashboard();

  // Ganti generasi cache grafik agar grafik tidak menampilkan data lama
  // yang masih tersimpan di CacheService (umur maks CHART_CACHE_TTL_S).
  PropertiesService.getScriptProperties().setProperty(
    'CHART_CACHE_GEN',
    String(new Date().getTime())
  );

  return {
    ok: true,
    message: 'Data histori dihapus. Dashboard di-reset. Pengisian dimulai dari awal saat node kirim lagi.',
    clearedSheets: cleared
  };
}

// =====================================================
// INIT DASHBOARD
// =====================================================

function vInitDashboard() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sh = ss.getSheetByName(DASHBOARD_NAME);

  if (!sh) {
    sh = ss.insertSheet(DASHBOARD_NAME, 0);
  }

  sh.clear();
  sh.setHiddenGridlines(true);

  sh.getRange('A1:L1').merge();
  sh.getRange('A1').setValue('MONITORING BANGUNAN — STATUS NODE')
    .setFontSize(16).setFontWeight('bold').setFontColor('#1565C0');

  sh.getRange('A2:L2').merge();
  sh.getRange('A2').setValue(
    'Hijau = normal. Kuning = data terlambat (cek WiFi). ' +
    'Merah = sensor/baterai bermasalah, turun ke lapangan. ' +
    'Abu-abu = offline atau belum pernah kirim.'
  ).setWrap(true);

  sh.getRange('A3').setValue('Batas usia data (detik)');
  sh.getRange('B3').setValue(DEFAULT_MAX_AGE_S);
  sh.getRange('C3').setValue('Terakhir diperbarui');
  sh.getRange('D3').setValue(new Date());
  sh.getRange('B3').setNote(
    'Tes interval 30 detik: 90–180. Production interval 5 menit: 600–900. ' +
    'Bisa diubah dari HMI tombol Pengaturan.'
  );

  sh.getRange('A4').setValue('Baterai low (V)');
  sh.getRange('B4').setValue(DEFAULT_BATTERY_LOW_V);
  sh.getRange('B4').setNote(
    'Di bawah nilai ini status menjadi BATERAI LOW. Default 3.50 V. ' +
    'Bisa diubah dari HMI tombol Pengaturan.'
  );

  if (!PropertiesService.getScriptProperties().getProperty('SAMPLE_INTERVAL_S')) {
    PropertiesService.getScriptProperties().setProperty(
      'SAMPLE_INTERVAL_S',
      String(DEFAULT_INTERVAL_S)
    );
  }

  // Seed link API + versi target V2.1.0 bila belum pernah disimpan dari HMI.
  if (!PropertiesService.getScriptProperties().getProperty('SCRIPT_API_URL')) {
    PropertiesService.getScriptProperties().setProperty(
      'SCRIPT_API_URL',
      DEFAULT_SCRIPT_EXEC_URL
    );
  }
  if (!PropertiesService.getScriptProperties().getProperty('FIRMWARE_VERSION')) {
    PropertiesService.getScriptProperties().setProperty(
      'FIRMWARE_VERSION',
      DEFAULT_FIRMWARE_VERSION
    );
  }

  var headers = [
    'Node ID',
    'Jenis',
    'STATUS',
    'Usia data (detik)',
    'Kiriman terakhir',
    'Nilai terakhir',
    'Baterai (V)',
    'Sensor status',
    'Firmware',
    'Tindakan operator'
  ];

  sh.getRange(5, 1, 1, headers.length).setValues([headers])
    .setFontWeight('bold')
    .setBackground(COLOR_HEADER)
    .setFontColor('#ffffff');

  var rows = [];
  for (var i = 0; i < NODE_LIST.length; i++) {
    var id = NODE_LIST[i];
    var jenis = (id.charAt(0) === 'A') ? 'Iluminansi' : 'IAQ';
    rows.push([
      id,
      jenis,
      'BELUM ADA DATA',
      '',
      '',
      '',
      '',
      '',
      '',
      'Belum pernah mengirim. Cek apakah alat sudah dipasang dan online.'
    ]);
  }

  sh.getRange(6, 1, rows.length, headers.length).setValues(rows);
  sh.getRange(6, 3, rows.length, 1).setBackground(COLOR_OFFLINE);

  sh.setColumnWidth(1, 90);
  sh.setColumnWidth(2, 110);
  sh.setColumnWidth(3, 140);
  sh.setColumnWidth(4, 130);
  sh.setColumnWidth(5, 190);
  sh.setColumnWidth(6, 280);
  sh.setColumnWidth(7, 100);
  sh.setColumnWidth(8, 120);
  sh.setColumnWidth(9, 110);
  sh.setColumnWidth(10, 420);

  sh.setFrozenRows(5);
  sh.getRange('D6:D').setNumberFormat('@');
  sh.getRange('E6:E').setNumberFormat('@');
  sh.getRange('I6:I').setNumberFormat('@'); // firmware teks (hindari 1.3.0 → tanggal)

  // Sheet baru saja di-clear dan diisi ulang; cache setting lama tidak berlaku.
  vInvalidateDashboardSettings_();
}

// =====================================================
// TULIS LOG PER NODE
// =====================================================

/**
 * Kunci pembanding timestamp, supaya string ISO dan tanggal Sheet dianggap sama.
 * Contoh yang sama: "2026-09-27T16:25:00" dan Date yang tampil "2026-09-27 16:25:00".
 */
function fTimestampKey_(value) {
  if (Object.prototype.toString.call(value) === '[object Date]' && !isNaN(value.getTime())) {
    return Utilities.formatDate(value, 'Asia/Jakarta', 'yyyy-MM-dd HH:mm:ss');
  }

  var s = String(value || '').trim();
  if (s.charAt(0) === "'") {
    s = s.substring(1);
  }
  var matched = s.match(/^(\d{4}-\d{2}-\d{2})[ T](\d{1,2}):(\d{2}):(\d{2})/);
  if (!matched) {
    return s;
  }
  var hour = ('0' + matched[2]).slice(-2);
  return matched[1] + ' ' + hour + ':' + matched[3] + ':' + matched[4];
}

/**
 * True jika timestamp ini sudah ada di log node.
 * Hanya 80 baris terakhir: ulangan POST selalu menempel di bawah,
 * dan membaca seluruh sheet memperlambat doPost.
 */
function bNodeTimestampAlreadyLogged_(sheet, ts) {
  var key = fTimestampKey_(ts);
  if (!key) {
    return false;
  }

  var last = sheet.getLastRow();
  if (last < 2) {
    return false;
  }

  var start = Math.max(2, last - 79);
  var count = last - start + 1;
  var values = sheet.getRange(start, 3, count, 1).getValues();
  for (var i = 0; i < values.length; i++) {
    if (fTimestampKey_(values[i][0]) === key) {
      return true;
    }
  }
  return false;
}

/**
 * Satu baris log, urutan kolom sama dengan vAppendNodeRow_.
 */
function fNodeLogValues_(data, isNodeA) {
  var ts = fSheetTimestampText_(data.timestamp || data.timestamp_utc || '');
  var tzRaw = data.timezone || data.timezone_offset || DEFAULT_TIMEZONE;
  var offsetText = "'" + String(fNormalizeTimezone_(tzRaw));

  if (isNodeA) {
    return [
      data.node_id,
      fFirmwareLabel_(data.firmware_version),
      ts,
      offsetText,
      data.illuminance_valid === 0 || data.illuminance_lux === null || data.illuminance_lux === ''
        ? ''
        : data.illuminance_lux,
      data.illuminance_valid,
      data.sensor_status,
      data.battery_voltage
    ];
  }

  return [
    data.node_id,
    fFirmwareLabel_(data.firmware_version),
    ts,
    offsetText,
    data.air_temperature_valid === 0 || data.air_temperature_c === null || data.air_temperature_c === ''
      ? ''
      : data.air_temperature_c,
    data.air_temperature_valid,
    data.relative_humidity_valid === 0 || data.relative_humidity_pct === null || data.relative_humidity_pct === ''
      ? ''
      : data.relative_humidity_pct,
    data.relative_humidity_valid,
    data.co2_valid === 0 || data.co2_ppm === null || data.co2_ppm === ''
      ? ''
      : data.co2_ppm,
    data.co2_valid,
    data.sensor_status,
    data.battery_voltage
  ];
}

/**
 * Menulis banyak baris satu node dengan satu setValues.
 * Timestamp yang sudah ada di 80 baris terakhir, atau dobel di POST ini, dilewati.
 * Mengembalikan objek terakhir yang punya node_id, untuk update dashboard.
 */
function vAppendNodeRowsBatch_(rows) {
  var lastData = null;
  var i;
  for (i = 0; i < rows.length; i++) {
    if (rows[i] && rows[i].node_id) {
      lastData = rows[i];
    }
  }
  if (!lastData) {
    return null;
  }

  var nodeId = String(lastData.node_id);
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sheet = ss.getSheetByName(nodeId);
  var isNodeA = (nodeId.charAt(0).toUpperCase() === 'A');
  if (!sheet) {
    sheet = ss.insertSheet(nodeId);
  }

  if (sheet.getLastRow() === 0) {
    if (isNodeA) {
      sheet.appendRow([
        'node_id', 'firmware_version', 'timestamp', 'timezone',
        'illuminance_lux', 'illuminance_valid', 'sensor_status', 'battery_voltage'
      ]);
    } else {
      sheet.appendRow([
        'node_id', 'firmware_version', 'timestamp', 'timezone',
        'air_temperature_c', 'air_temperature_valid',
        'relative_humidity_pct', 'relative_humidity_valid',
        'co2_ppm', 'co2_valid', 'sensor_status', 'battery_voltage'
      ]);
    }
    var headerRange = sheet.getRange(1, 1, 1, sheet.getLastColumn());
    headerRange.setFontWeight('bold');
    headerRange.setBackground(COLOR_HEADER);
    headerRange.setFontColor('#ffffff');
    // Kolom C timestamp dan D timezone tetap teks (huruf T tidak diubah Sheet).
    sheet.getRange('C:C').setNumberFormat('@');
    sheet.getRange('D:D').setNumberFormat('@');
  }

  var known = {};
  var last = sheet.getLastRow();
  if (last >= 2) {
    var start = Math.max(2, last - 79);
    var count = last - start + 1;
    var existing = sheet.getRange(start, 3, count, 1).getValues();
    for (i = 0; i < existing.length; i++) {
      var oldKey = fTimestampKey_(existing[i][0]);
      if (oldKey) {
        known[oldKey] = true;
      }
    }
  }

  var values = [];
  for (i = 0; i < rows.length; i++) {
    var row = rows[i];
    if (!row || String(row.node_id || '') !== nodeId) {
      continue;
    }
    var ts = row.timestamp || row.timestamp_utc || '';
    var key = fTimestampKey_(ts);
    if (key && known[key]) {
      continue;
    }
    if (key) {
      known[key] = true;
    }
    values.push(fNodeLogValues_(row, isNodeA));
  }

  if (values.length > 0) {
    var width = values[0].length;
    sheet.getRange(sheet.getLastRow() + 1, 1, values.length, width).setValues(values);
    var firstNew = sheet.getLastRow() - values.length + 1;
    sheet.getRange(firstNew, 2, values.length, 1).setNumberFormat('@');
  }

  return lastData;
}

function vAppendNodeRow_(data) {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var nodeId = String(data.node_id);
  var sheet = ss.getSheetByName(nodeId);
  var isNodeA = (nodeId.charAt(0).toUpperCase() === 'A');
  // V2.1.0: timestamp + timezone. Terima juga nama field firmware lama.
  var ts = data.timestamp || data.timestamp_utc || '';
  var tzRaw = data.timezone || data.timezone_offset || DEFAULT_TIMEZONE;
  var offsetText = "'" + String(fNormalizeTimezone_(tzRaw));

  if (!sheet) {
    sheet = ss.insertSheet(nodeId);
  }

  if (sheet.getLastRow() === 0) {
    if (isNodeA) {
      sheet.appendRow([
        'node_id', 'firmware_version', 'timestamp', 'timezone',
        'illuminance_lux', 'illuminance_valid', 'sensor_status', 'battery_voltage'
      ]);
    } else {
      sheet.appendRow([
        'node_id', 'firmware_version', 'timestamp', 'timezone',
        'air_temperature_c', 'air_temperature_valid',
        'relative_humidity_pct', 'relative_humidity_valid',
        'co2_ppm', 'co2_valid', 'sensor_status', 'battery_voltage'
      ]);
    }
    var headerRange = sheet.getRange(1, 1, 1, sheet.getLastColumn());
    headerRange.setFontWeight('bold');
    headerRange.setBackground(COLOR_HEADER);
    headerRange.setFontColor('#ffffff');
    sheet.getRange('C:C').setNumberFormat('@');
    sheet.getRange('D:D').setNumberFormat('@');
  }

  // Ulangan setelah timeout ESP32 tidak boleh menambah baris kedua.
  if (bNodeTimestampAlreadyLogged_(sheet, ts)) {
    return;
  }

  if (isNodeA) {
    sheet.appendRow([
      data.node_id,
      fFirmwareLabel_(data.firmware_version),
      fSheetTimestampText_(ts),
      offsetText,
      data.illuminance_valid === 0 || data.illuminance_lux === null || data.illuminance_lux === ''
        ? ''
        : data.illuminance_lux,
      data.illuminance_valid,
      data.sensor_status,
      data.battery_voltage
    ]);
  } else {
    sheet.appendRow([
      data.node_id,
      fFirmwareLabel_(data.firmware_version),
      fSheetTimestampText_(ts),
      offsetText,
      data.air_temperature_valid === 0 || data.air_temperature_c === null || data.air_temperature_c === ''
        ? ''
        : data.air_temperature_c,
      data.air_temperature_valid,
      data.relative_humidity_valid === 0 || data.relative_humidity_pct === null || data.relative_humidity_pct === ''
        ? ''
        : data.relative_humidity_pct,
      data.relative_humidity_valid,
      data.co2_valid === 0 || data.co2_ppm === null || data.co2_ppm === ''
        ? ''
        : data.co2_ppm,
      data.co2_valid,
      data.sensor_status,
      data.battery_voltage
    ]);
  }

  // Kolom B = firmware: paksa format teks.
  sheet.getRange(sheet.getLastRow(), 2).setNumberFormat('@');
}

// =====================================================
// UPDATE SATU BARIS DASHBOARD
// =====================================================

function vUpsertDashboardRow_(data) {
  var sh = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(DASHBOARD_NAME);
  if (!sh) {
    vInitDashboard();
    sh = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(DASHBOARD_NAME);
  }

  var nodeId = String(data.node_id);
  var row = iFindDashboardRow_(sh, nodeId);
  if (row < 0) {
    return;
  }

  var isNodeA = (nodeId.charAt(0).toUpperCase() === 'A');
  var nilai = '';

  if (isNodeA) {
    nilai = (Number(data.illuminance_valid) === 0 || data.illuminance_lux === null || data.illuminance_lux === '')
      ? 'Lux tidak terbaca'
      : ('Lux ' + data.illuminance_lux + ' | valid ' + data.illuminance_valid);
  } else {
    var bTempOk = Number(data.air_temperature_valid) !== 0 && data.air_temperature_c !== null && data.air_temperature_c !== '';
    var bRhOk = Number(data.relative_humidity_valid) !== 0 && data.relative_humidity_pct !== null && data.relative_humidity_pct !== '';
    var bCo2Ok = Number(data.co2_valid) !== 0 && data.co2_ppm !== null && data.co2_ppm !== '';
    nilai = 'T ' + (bTempOk ? data.air_temperature_c : '-') + ' C | RH ' +
      (bRhOk ? data.relative_humidity_pct : '-') + ' % | CO2 ' +
      (bCo2Ok ? data.co2_ppm : '-') + ' ppm';
  }

  var tsText = fSheetTimestampText_(data.timestamp || data.timestamp_utc || '');

  // Status satu baris saja. Jangan tulis ulang seluruh dashboard di doPost.
  var info = fComputeStatus_(
    tsText,
    Number(data.battery_voltage),
    data.sensor_status,
    new Date(),
    fGetMaxAgeS_(),
    fGetBatteryLowV_()
  );

  // V2.5.0: kolom C..J ditulis dalam SATU setValues (sebelumnya 8 tulis
  // terpisah + 2 setNumberFormat). Kolom E (timestamp) dan I (firmware)
  // diformat teks lebih dulu dalam satu panggilan RangeList, supaya
  // "2026-09-27T16:25:00" dan "1.3.0" tidak diubah Sheets menjadi tanggal.
  sh.getRangeList(['E' + row, 'I' + row]).setNumberFormat('@');
  sh.getRange(row, 3, 1, 8).setValues([[
    info.status,
    info.age,
    tsText,
    nilai,
    // setValues menolak undefined; field yang tidak dikirim node ditulis kosong.
    (data.battery_voltage === undefined || data.battery_voltage === null) ? '' : data.battery_voltage,
    (data.sensor_status === undefined || data.sensor_status === null) ? '' : data.sensor_status,
    fFirmwareLabel_(data.firmware_version),
    info.action
  ]]);
  sh.getRange(row, 3).setBackground(info.color);
  sh.getRange('D3').setValue(new Date());
}

function iFindDashboardRow_(sh, nodeId) {
  var last = sh.getLastRow();
  if (last < 6) {
    return -1;
  }
  var ids = sh.getRange(6, 1, last - 5, 1).getValues();
  for (var i = 0; i < ids.length; i++) {
    if (String(ids[i][0]) === nodeId) {
      return 6 + i;
    }
  }
  return -1;
}

/**
 * Tulis ulang status + warna ke sheet Dashboard.
 * Dipakai saat pengaturan HMI berubah — bukan tiap POST node dan bukan tiap poll HMI.
 */
function vRefreshDashboardStatus() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var sh = ss.getSheetByName(DASHBOARD_NAME);
  if (!sh) {
    return;
  }

  var maxAge = fGetMaxAgeS_();
  var batteryLow = fGetBatteryLowV_();

  sh.getRange('D3').setValue(new Date());

  var last = sh.getLastRow();
  if (last < 6) {
    return;
  }

  var now = new Date();
  var n = last - 5;
  var values = sh.getRange(6, 1, n, 10).getValues();
  var colors = [];
  var out = [];

  for (var i = 0; i < n; i++) {
    var lastUtc = String(values[i][4] || '');
    var battery = Number(values[i][6]);
    var sensorStatus = values[i][7];
    var info = fComputeStatus_(
      lastUtc,
      battery,
      sensorStatus,
      now,
      maxAge,
      batteryLow
    );

    out.push([
      values[i][0],
      values[i][1],
      info.status,
      info.age,
      values[i][4],
      values[i][5],
      values[i][6],
      values[i][7],
      values[i][8],
      info.action
    ]);
    colors.push([info.color]);
  }

  sh.getRange(6, 1, n, 10).setValues(out);
  sh.getRange(6, 3, n, 1).setBackgrounds(colors);
}

function fComputeStatus_(lastUtc, battery, sensorStatus, now, maxAge, batteryLow) {
  if (!lastUtc) {
    return {
      status: 'BELUM ADA DATA',
      age: '',
      color: COLOR_OFFLINE,
      action: 'Belum pernah mengirim. Cek pemasangan, daya, dan konfigurasi WiFi.'
    };
  }

  var t = new Date(fNormalizeIsoForParse_(lastUtc));
  var ageSec = Math.round((now.getTime() - t.getTime()) / 1000);
  if (isNaN(ageSec)) {
    ageSec = '';
  } else if (ageSec < 0) {
    // Timestamp slot bisa sedikit di masa depan vs jam server Google:
    // - Node B bangun lebih awal (wake early) tapi stamp ke slot berikutnya
    // - skew NTP ESP32 vs server Apps Script
    // Usia negatif tidak bermakna untuk operator → clamp ke 0 (baru saja).
    ageSec = 0;
  }

  var lowThresh = Number(batteryLow);
  if (!lowThresh || lowThresh < 2.5 || lowThresh > 5.0) {
    lowThresh = DEFAULT_BATTERY_LOW_V;
  }

  var sensorBad = (sensorStatus !== '' && Number(sensorStatus) !== 0);
  var battLow = (!isNaN(battery) && battery > 0 && battery < lowThresh);

  if (sensorBad) {
    return {
      status: 'ERROR SENSOR',
      age: ageSec,
      color: COLOR_ERROR,
      action: 'Turun ke lapangan. Cek sensor, kabel, dan LED merah pada alat.'
    };
  }

  if (battLow) {
    return {
      status: 'BATERAI LOW',
      age: ageSec,
      color: COLOR_ERROR,
      action: 'Turun ke lapangan. Cek adaptor 5V, backup baterai, dan sambungan daya.'
    };
  }

  if (ageSec !== '' && ageSec > maxAge * 3) {
    return {
      status: 'OFFLINE',
      age: ageSec,
      color: COLOR_OFFLINE,
      action: 'Tidak ada data lama. Turun ke lapangan: cek daya, WiFi, dan apakah alat masih menyala.'
    };
  }

  if (ageSec !== '' && ageSec > maxAge) {
    return {
      status: 'TERLAMBAT',
      age: ageSec,
      color: COLOR_WARN,
      action: 'Data telat. Cek dulu WiFi/router dari kantor. Jika terus kuning, turun ke lapangan.'
    };
  }

  return {
    status: 'OK',
    age: ageSec,
    color: COLOR_OK,
    action: 'Normal. Tidak perlu ke lapangan.'
  };
}

function jsonOut_(text) {
  return ContentService
    .createTextOutput(text)
    .setMimeType(ContentService.MimeType.TEXT);
}
