// Halaman AP disimpan di flash (PROGMEM).
// Nilai yang berubah diisi lewat GET /api/status, bukan dirakit di RAM.
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitoring Node</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#eef3f7;color:#1f2937}
.wrap{max-width:520px;margin:0 auto;padding:18px 16px 40px}
h1{font-size:22px;margin:0 0 4px;color:#0f4c81}
.sub{color:#6b7280;font-size:13px;margin-bottom:16px}
.panel{background:#fff;border-radius:14px;padding:16px;margin:0 0 14px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
h2{font-size:15px;margin:0 0 12px;color:#0f4c81}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:8px 0;border-bottom:1px solid #f1f5f9}
td:first-child{color:#6b7280;width:48%}
.btn{display:block;width:100%;text-align:center;padding:12px 14px;margin:8px 0 0;border:none;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;text-decoration:none}
.btn-primary{background:#0f4c81;color:#fff}
.btn-danger{background:#b91c1c;color:#fff}
.btn-muted{background:#e5e7eb;color:#111}
.btn-ok{background:#15803d;color:#fff}
label{display:block;font-weight:600;margin:12px 0 4px;font-size:13px;color:#374151}
input,textarea{width:100%;padding:10px;border:1px solid #d1d5db;border-radius:8px;font-size:14px}
textarea{min-height:64px;font-family:monospace;font-size:12px}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.hint{font-size:12px;color:#9ca3af;margin-top:4px}
.warn{background:#fff7ed;border:1px solid #fdba74;border-radius:10px;padding:12px;font-size:14px;margin-bottom:12px}
.msg{font-weight:700;color:#15803d}
</style></head><body>
<div class="wrap">
<h1 id="title">Node</h1>
<div class="sub" id="sub">Memuat...</div>
<div class="warn" id="banner">Menghubungi node...</div>
<p class="msg" id="flash"></p>
<div class="panel"><h2>File data lokal</h2>
<table>
<tr><td>File</td><td id="file">-</td></tr>
<tr><td>Ukuran</td><td id="size">-</td></tr>
<tr><td>Batas</td><td id="max">-</td></tr>
<tr><td>Sisa LittleFS</td><td id="free">-</td></tr>
<tr><td>Jenis node</td><td id="kind">-</td></tr>
</table>
<a class="btn btn-primary" href="/download">Download CSV</a>
<button class="btn btn-danger" type="button" id="btnAskDelete">Hapus file data</button>
<form id="formDelete" method="POST" action="/delete" hidden>
<p>Semua rekaman lokal akan dihapus. Marker backfill ikut direset.</p>
<button class="btn btn-danger" type="submit">Ya, hapus sekarang</button>
<button class="btn btn-muted" type="button" id="btnCancelDelete">Batal</button>
</form>
</div>
<div class="panel"><h2>Pengaturan</h2>
<form method="POST" action="/save">
<label>Node ID</label>
<input name="node_id" id="node_id" maxlength="15" required>
<div class="hint">A-01..A-25 atau B-01..B-03</div>
<label>WiFi SSID</label>
<input name="wifi_ssid" id="wifi_ssid" maxlength="63" required>
<label>WiFi Password</label>
<input type="password" name="wifi_password" id="wifi_password" maxlength="63">
<div class="hint" id="passHint">Kosongkan untuk mempertahankan password lama</div>
<label>Google Apps Script URL</label>
<textarea name="script_url" id="script_url" maxlength="255" required></textarea>
<div class="hint">Isi sekali di sini. Setelah itu link diganti dari HMI Spreadsheet. Node mengambil link baru hanya saat daya baru dinyalakan.</div>
<label>Interval (detik)</label>
<input type="number" name="interval_s" id="interval_s" required>
<div class="hint">Default 300 (5 menit). Timestamp di awal slot; kirim +stagger 5s.</div>
<label>Timeout AP (detik)</label>
<input type="number" name="ap_timeout_s" id="ap_timeout_s" required>
<div class="hint" id="apTimeoutHint">Default 120 (2 menit). Setelah waktu ini halaman AP mati dan node tidur.</div>
<button class="btn btn-ok" type="submit">Simpan &amp; Restart</button>
</form></div>
<div class="panel"><h2>Versi firmware</h2>
<p id="fwVer" style="font-size:22px;font-weight:700;margin:0;color:#0f4c81">-</p>
<div class="hint">Tidak diubah di halaman ini. Angka target diubah di HMI Spreadsheet. Jika berbeda, node mengunduh firmware saat daya baru dinyalakan.</div>
</div>
<div class="panel" id="calA" hidden><h2>Kalibrasi Node A (VEML7700 + baterai)</h2>
<table>
<tr><td>Lux m / c</td><td id="lux_mc">-</td></tr>
<tr><td>Baterai m / c</td><td id="bat_mc_a">-</td></tr>
<tr><td>Mode gain VEML7700</td><td id="gain_mode">-</td></tr>
</table>
<div class="hint">Hanya tampilan nilai tersimpan. Kalibrasi dan mode gain diubah dari HMI Spreadsheet, diterapkan saat node baru dinyalakan.</div>
<a class="btn btn-primary" href="/calmLux">Baca ALS / lux realtime</a>
<a class="btn btn-primary" href="/calmA">Baca ADC baterai realtime</a>
</div>
<div class="panel" id="calB" hidden><h2>Kalibrasi Node B (SCD41 + baterai)</h2>
<table>
<tr><td>Suhu m / c</td><td id="temp_mc">-</td></tr>
<tr><td>RH m / c</td><td id="rh_mc">-</td></tr>
<tr><td>CO2 m / c</td><td id="co2_mc">-</td></tr>
<tr><td>Baterai m / c</td><td id="bat_mc_b">-</td></tr>
</table>
<div class="hint">Hanya tampilan nilai tersimpan. Kalibrasi diubah dari HMI Spreadsheet, diterapkan saat node baru dinyalakan.</div>
<a class="btn btn-primary" href="/calmEnv">Baca suhu / RH / CO2 realtime</a>
<a class="btn btn-primary" href="/calmA">Baca ADC baterai realtime</a>
</div>
</div>
<script>
var gFilled = false;
var GAIN_TEXT = {"auto":"Autorange","2":"Gain 2x","1":"Gain 1x","1_4":"Gain 1/4x","1_8":"Gain 1/8x"};
function setVal(id, v){ var el = document.getElementById(id); if (el) el.value = v; }
function setText(id, v){ var el = document.getElementById(id); if (el) el.textContent = v; }
function mc(m, c){ return Number(m).toFixed(4) + " / " + Number(c).toFixed(4); }
function refresh(){
  fetch("/api/status").then(function(r){ return r.json(); }).then(function(d){
    document.getElementById("title").textContent = "Node " + d.node_id;
    document.getElementById("sub").textContent = "Firmware " + d.firmware + " ? AP data + pengaturan";
    var banner = document.getElementById("banner");
    if (d.force_stay){
      banner.textContent = "Config belum lengkap. Isi WiFi dan Script URL di bawah.";
    } else {
      banner.innerHTML = "Mode maintenance. Timeout sleep: <b>" + d.remain_s + "</b> detik (setelan " + d.ap_timeout_s + " s). Atau lepas lalu tahan tombol GPIO36 selama 3 detik.";
    }
    document.getElementById("file").textContent = d.file;
    document.getElementById("size").textContent = d.file_size + " byte (" + (d.file_size/1024).toFixed(1) + " KB)";
    document.getElementById("max").textContent = d.file_max + " byte";
    document.getElementById("free").textContent = d.fs_free + " byte";
    document.getElementById("kind").textContent = d.kind;
    document.getElementById("fwVer").textContent = d.firmware;
    // Kalibrasi read-only: selalu disegarkan dari nilai tersimpan di node.
    setText("lux_mc", mc(d.lux_m, d.lux_c));
    setText("bat_mc_a", mc(d.bat_m, d.bat_c));
    setText("gain_mode", GAIN_TEXT[d.veml_gain_mode] || d.veml_gain_mode);
    setText("temp_mc", mc(d.temp_m, d.temp_c));
    setText("rh_mc", mc(d.rh_m, d.rh_c));
    setText("co2_mc", mc(d.co2_m, d.co2_c));
    setText("bat_mc_b", mc(d.bat_m, d.bat_c));
    if (!gFilled){
      setVal("node_id", d.node_id);
      setVal("wifi_ssid", d.wifi_ssid);
      setVal("script_url", d.script_url);
      setVal("interval_s", d.interval_s);
      document.getElementById("interval_s").min = d.interval_min;
      document.getElementById("interval_s").max = d.interval_max;
      document.getElementById("passHint").textContent = d.password_set ? "Password tersimpan. Kosongkan untuk mempertahankan." : "Password belum diisi.";
      setVal("ap_timeout_s", d.ap_timeout_s);
      document.getElementById("ap_timeout_s").min = d.ap_timeout_min;
      document.getElementById("ap_timeout_s").max = d.ap_timeout_max;
      setText("apTimeoutHint", "Rentang " + d.ap_timeout_min + ".." + d.ap_timeout_max + " detik. Default 120 (2 menit). Setelah waktu ini halaman AP mati dan node tidur.");
      document.getElementById("calA").hidden = !d.is_node_a;
      document.getElementById("calB").hidden = d.is_node_a;
      gFilled = true;
    }
  }).catch(function(){
    document.getElementById("banner").textContent = "Gagal memuat status.";
  });
}
document.getElementById("btnAskDelete").onclick = function(){ document.getElementById("formDelete").hidden = false; };
document.getElementById("btnCancelDelete").onclick = function(){ document.getElementById("formDelete").hidden = true; };
var q = new URLSearchParams(location.search).get("msg");
if (q === "hapus") document.getElementById("flash").textContent = "File data dihapus.";
refresh();
setInterval(refresh, 5000);
</script>
</body></html>
)rawliteral";

const char CALM_A_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ADC Baterai</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#eef3f7;color:#1f2937}
.wrap{max-width:520px;margin:0 auto;padding:18px 16px 40px}
h1{font-size:22px;margin:0 0 4px;color:#0f4c81}
.sub{color:#6b7280;font-size:13px;margin-bottom:16px}
.panel{background:#fff;border-radius:14px;padding:16px;margin:0 0 14px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
.big{font-size:42px;font-weight:700;color:#0f4c81;margin:8px 0}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:8px 0;border-bottom:1px solid #f1f5f9}
td:first-child{color:#6b7280;width:55%}
.hint{font-size:12px;color:#6b7280}
a{color:#0f4c81}
</style></head><body>
<div class="wrap">
<h1>ADC baterai</h1>
<div class="sub">GPIO39 / BMS. Bandingkan dengan multimeter.</div>
<div class="panel">
<div class="hint">Tegangan paket sebelum kalibrasi</div>
<div class="big" id="raw">-</div>
<table>
<tr><td>ADC rata-rata</td><td id="adc">-</td></tr>
<tr><td>Tegangan pin</td><td id="pin">-</td></tr>
<tr><td>Setelah kalibrasi y=m*x+c</td><td id="cal">-</td></tr>
<tr><td>m / c baterai</td><td id="mc">-</td></tr>
</table>
<p class="hint">Ukur tegangan paket dengan multimeter dan catat bersama angka besar di atas. Hitung m dan c, lalu isi di HMI Spreadsheet (panel kalibrasi node). Pembagi default 100k/100k.</p>
</div>
<p><a href="/">Kembali</a></p>
</div>
<script>
function tick(){
  fetch("/api/battery").then(function(r){ return r.json(); }).then(function(d){
    document.getElementById("raw").textContent = d.pack_raw_v.toFixed(3) + " V";
    document.getElementById("adc").textContent = d.adc.toFixed(1);
    document.getElementById("pin").textContent = d.pin_v.toFixed(3) + " V";
    document.getElementById("cal").textContent = d.pack_cal_v.toFixed(3) + " V";
    document.getElementById("mc").textContent = d.bat_m.toFixed(4) + " / " + d.bat_c.toFixed(4);
  }).catch(function(){
    document.getElementById("raw").textContent = "gagal";
  });
}
tick();
setInterval(tick, 1000);
</script>
</body></html>
)rawliteral";

// Halaman baca VEML7700 realtime (Node A). Data dari GET /api/lux.
// Nilai mentah dibaca pada gain yang sedang aktif (tanpa autorange).
const char CALM_LUX_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lux Realtime</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#eef3f7;color:#1f2937}
.wrap{max-width:520px;margin:0 auto;padding:18px 16px 40px}
h1{font-size:22px;margin:0 0 4px;color:#0f4c81}
.sub{color:#6b7280;font-size:13px;margin-bottom:16px}
.panel{background:#fff;border-radius:14px;padding:16px;margin:0 0 14px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
.big{font-size:42px;font-weight:700;color:#0f4c81;margin:8px 0}
table{width:100%;border-collapse:collapse;font-size:14px}
td{padding:8px 0;border-bottom:1px solid #f1f5f9}
td:first-child{color:#6b7280;width:55%}
.hint{font-size:12px;color:#6b7280}
.bad{color:#b91c1c;font-weight:700}
a{color:#0f4c81}
</style></head><body>
<div class="wrap">
<h1>VEML7700 realtime</h1>
<div class="sub">Bandingkan dengan luxmeter referensi. Update tiap 1 detik.</div>
<div class="panel">
<div class="hint">Lux mentah (sebelum kalibrasi)</div>
<div class="big" id="raw">-</div>
<p class="bad" id="err"></p>
<table>
<tr><td>ALS mentah</td><td id="als">-</td></tr>
<tr><td>Gain aktif</td><td id="gain">-</td></tr>
<tr><td>Mode gain tersimpan</td><td id="mode">-</td></tr>
<tr><td>Setelah kalibrasi y=m*x+c</td><td id="cal">-</td></tr>
<tr><td>m / c lux</td><td id="mc">-</td></tr>
<tr><td>Jumlah sampel</td><td id="cnt">-</td></tr>
<tr><td>Umur data</td><td id="age">-</td></tr>
</table>
<p class="hint">Catat lux mentah dan nilai luxmeter pada beberapa tingkat cahaya dengan gain yang sama. Hitung m dan c, lalu isi di HMI Spreadsheet. Gain tetap dipilih di HMI dan berlaku setelah node dinyalakan ulang.</p>
</div>
<p><a href="/">Kembali</a></p>
</div>
<script>
var GAIN_TEXT = {"auto":"Autorange","2":"Gain 2x","1":"Gain 1x","1_4":"Gain 1/4x","1_8":"Gain 1/8x"};
function tick(){
  fetch("/api/lux").then(function(r){ return r.json(); }).then(function(d){
    var err = "";
    if (!d.is_node_a) err = "Node ini bukan Node A.";
    else if (!d.hw_ok) err = "VEML7700 tidak terdeteksi.";
    else if (!d.valid) err = "Menunggu bacaan pertama...";
    else if (!d.last_ok) err = "Bacaan terakhir gagal.";
    document.getElementById("err").textContent = err;
    document.getElementById("mode").textContent = GAIN_TEXT[d.gain_mode] || d.gain_mode;
    document.getElementById("mc").textContent = d.lux_m.toFixed(4) + " / " + d.lux_c.toFixed(4);
    if (!d.valid) return;
    document.getElementById("raw").textContent = d.lux_raw.toFixed(2) + " lx";
    document.getElementById("als").textContent = d.als;
    document.getElementById("gain").textContent = d.gain;
    document.getElementById("cal").textContent = d.lux_cal.toFixed(2) + " lx";
    document.getElementById("cnt").textContent = d.count;
    document.getElementById("age").textContent = (d.age_ms / 1000).toFixed(1) + " s";
  }).catch(function(){
    document.getElementById("err").textContent = "Gagal menghubungi node.";
  });
}
tick();
setInterval(tick, 1000);
</script>
</body></html>
)rawliteral";

// Halaman baca SCD41 realtime (Node B). Data dari GET /api/env.
// Satu single shot SCD41 butuh ~5 detik, node mengukur tiap 6 detik.
const char CALM_ENV_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SCD41 Realtime</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:Segoe UI,Arial,sans-serif;background:#eef3f7;color:#1f2937}
.wrap{max-width:520px;margin:0 auto;padding:18px 16px 40px}
h1{font-size:22px;margin:0 0 4px;color:#0f4c81}
.sub{color:#6b7280;font-size:13px;margin-bottom:16px}
.panel{background:#fff;border-radius:14px;padding:16px;margin:0 0 14px;box-shadow:0 1px 3px rgba(0,0,0,.08)}
table{width:100%;border-collapse:collapse;font-size:14px}
th{text-align:left;color:#6b7280;font-weight:600;padding:6px 0;border-bottom:1px solid #e5e7eb}
td{padding:8px 0;border-bottom:1px solid #f1f5f9}
.v{font-size:20px;font-weight:700;color:#0f4c81}
.hint{font-size:12px;color:#6b7280}
.bad{color:#b91c1c;font-weight:700}
a{color:#0f4c81}
</style></head><body>
<div class="wrap">
<h1>SCD41 realtime</h1>
<div class="sub">Bandingkan dengan alat referensi. Satu ukur ~5 detik, update tiap 6 detik.</div>
<div class="panel">
<p class="bad" id="err"></p>
<table>
<tr><th></th><th>Mentah</th><th>Kalibrasi</th><th>m / c</th></tr>
<tr><td>Suhu (C)</td><td class="v" id="tRaw">-</td><td id="tCal">-</td><td id="tMc">-</td></tr>
<tr><td>RH (%)</td><td class="v" id="hRaw">-</td><td id="hCal">-</td><td id="hMc">-</td></tr>
<tr><td>CO2 (ppm)</td><td class="v" id="cRaw">-</td><td id="cCal">-</td><td id="cMc">-</td></tr>
</table>
<p class="hint">Jumlah sampel: <b id="cnt">-</b> | Umur data: <b id="age">-</b></p>
<p class="hint">Catat nilai mentah dan nilai alat referensi pada beberapa kondisi. Hitung m dan c, lalu isi di HMI Spreadsheet.</p>
</div>
<p><a href="/">Kembali</a></p>
</div>
<script>
function mc(m, c){ return m.toFixed(3) + " / " + c.toFixed(3); }
function tick(){
  fetch("/api/env").then(function(r){ return r.json(); }).then(function(d){
    var err = "";
    if (d.is_node_a) err = "Node ini bukan Node B.";
    else if (!d.hw_ok) err = "SCD41 tidak terdeteksi.";
    else if (!d.valid) err = "Mengukur... (bacaan pertama sekitar 5 detik)";
    else if (!d.last_ok) err = "Bacaan terakhir gagal.";
    document.getElementById("err").textContent = err;
    document.getElementById("tMc").textContent = mc(d.temp_m, d.temp_c);
    document.getElementById("hMc").textContent = mc(d.rh_m, d.rh_c);
    document.getElementById("cMc").textContent = mc(d.co2_m, d.co2_c);
    if (!d.valid) return;
    document.getElementById("tRaw").textContent = d.temp_raw.toFixed(2);
    document.getElementById("tCal").textContent = d.temp_cal.toFixed(2);
    document.getElementById("hRaw").textContent = d.rh_raw.toFixed(1);
    document.getElementById("hCal").textContent = d.rh_cal.toFixed(1);
    document.getElementById("cRaw").textContent = d.co2_raw;
    document.getElementById("cCal").textContent = d.co2_cal.toFixed(0);
    document.getElementById("cnt").textContent = d.count;
    document.getElementById("age").textContent = (d.age_ms / 1000).toFixed(1) + " s";
  }).catch(function(){
    document.getElementById("err").textContent = "Gagal menghubungi node.";
  });
}
tick();
setInterval(tick, 2000);
</script>
</body></html>
)rawliteral";

const char SAVED_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tersimpan</title></head>
<body><h2>Tersimpan. Restart...</h2></body></html>
)rawliteral";
