// Kontrol manual pompa menggunakan Blynk Virtual Pin V7.
// V0-V6 tetap digunakan oleh datastream yang sudah ada.
// Ganti placeholder token/API key dengan kredensial BARU setelah
// kredensial lama dirotasi karena sebelumnya sudah terpapar.\n\n// Konfigurasi Blynk IoT: 
#define BLYNK_TEMPLATE_ID "TMPL6Gs2GHgrL"
#define BLYNK_TEMPLATE_NAME "Sistem penyiraman Otomatis dengan AI"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>

// =====================================================
// 1. KONFIGURASI WIFI
// =====================================================
const char* ssid = "UConnect-AX";
const char* password = "";

// =====================================================
// 2. KONFIGURASI GEMINI API
// =====================================================
const char* geminiAPIKey = "YOUR_GEMINI_API_KEY";
const char* geminiModel = "gemini-3.5-flash";
const char* geminiURL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash:generateContent";

// =====================================================
// 3. KONFIGURASI DHT22
// =====================================================
// DHT22 terhubung ke D1 / GPIO5
#define DHTPIN D1
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// =====================================================
// 4. SENSOR KELEMBAPAN TANAH
// =====================================================
#define SOIL_PIN A0

// Kalibrasi sementara
// RAW 1024 = sangat kering
// RAW 350  = sangat basah
const int SOIL_DRY = 1024;
const int SOIL_WET = 350;

// =====================================================
// 5. RELAY DAN POMPA
// ====================================================
// Relay IN -> D5 / GPIO14
#define RELAY_PIN D5

// Relay aktif LOW (LOW = ON, HIGH = OFF)

// =====================================================
// 6. KONFIGURASI BMKG
// =====================================================
const char* bmkgURL = "https://bmkg-proxy-tanaman.s22210414.workers.dev/";

// =====================================================
// 7. INTERVAL SISTEM
// =====================================================
// Siklus utama setiap 60 detik
const unsigned long SYSTEM_INTERVAL = 60000UL;

// DHT22 dibaca setiap siklus sistem (interval sistem 60 detik).
// Pompa maksimal menyala 10 detik
const unsigned long PUMP_MAX_DURATION = 10000UL;

// Retry Gemini setelah HTTP 429
const unsigned long DEFAULT_RETRY_DELAY = 35000UL;

// =====================================================
// 8. VARIABEL SENSOR TANAH
// =====================================================
int nilaiSoilRaw = 0;
int kelembapanTanah = 0;
String statusTanah = "";

// =====================================================
// 9. VARIABEL DHT22
// =====================================================
// DHT hanya sebagai data pendukung
float suhuUdara = 0.0;
float kelembapanUdara = 0.0;
bool dhtValid = false;

// =====================================================
// 10. VARIABEL BMKG
// =====================================================
String bmkgWaktu = "";
String bmkgCuaca = "";
String bmkgArahAngin = "";
float suhuBMKG = 0.0;
float kelembapanBMKG = 0.0;
float kecepatanAngin = 0.0;
float curahHujanBMKG = 0.0;
bool bmkgDataValid = false;

// =====================================================
// 11. VARIABEL GEMINI
// =====================================================
String keputusanAI = "TIDAK MENYIRAM";
String sumberKeputusan = "BELUM ADA";
String modelVersion = "";
String finishReason = "";

// =====================================================
// 12. VARIABEL SISTEM
// =====================================================
unsigned long lastSystemRun = 0;
bool systemRunning = false;

// =====================================================
// 13. VARIABEL POMPA
// =====================================================
bool pumpRunning = false;
unsigned long pumpStartMillis = 0;
// Status kontrol manual dari tombol Blynk Virtual Pin V7.
bool manualPumpActive = false;

// =====================================================
// 14. VARIABEL RETRY GEMINI
// =====================================================
unsigned long geminiRetryUntil = 0;

// =====================================================
// FUNGSI MEMATIKAN POMPA
// =====================================================
void pumpOff() {
  // Tambahan notifikasi: kirim hanya jika pompa sebelumnya benar-benar ON.
  bool wasRunning = pumpRunning;
  digitalWrite(RELAY_PIN, HIGH);
  pumpRunning = false;
  manualPumpActive = false;
  Serial.println("Relay : OFF");
  Serial.println("Pompa : OFF");
  if (Blynk.connected()) {
    Blynk.virtualWrite(V6, 0);
    if (wasRunning) {
      Blynk.logEvent("pompa_selesai", "Penyiraman selesai: pompa air berhenti.");
      Serial.println("Notifikasi Blynk terkirim: pompa_selesai");
    }
  }
}

// =====================================================
// FUNGSI MENYALAKAN POMPA
// =====================================================
void pumpOn() {
  if (!pumpRunning) {
    digitalWrite(RELAY_PIN, LOW);
    pumpRunning = true;
    pumpStartMillis = millis();
    Serial.println("Relay : ON");
    Serial.println("Pompa : ON");
    if (Blynk.connected()) {
      Blynk.virtualWrite(V6, 1);
      // Tambahan notifikasi saat pompa berubah dari OFF menjadi ON.
      Blynk.logEvent("pompa_dimulai", "Penyiraman dimulai: pompa air aktif.");
      Serial.println("Notifikasi Blynk terkirim: pompa_dimulai");
    }
    Serial.println("Timer pompa dimulai. Maksimal 10 detik.");
  }
}

// =====================================================
// TOMBOL PENYIRAMAN MANUAL DARI BLYNK (V7)
// =====================================================
// Atur datastream V7 sebagai Integer (0-1) dan widget Button
// dalam mode PUSH. Tombol hanya memberi perintah saat bernilai 1.
BLYNK_WRITE(V7) {
  int buttonState = param.asInt();

  if (buttonState == 1) {
    Serial.println();
    Serial.println("===== KONTROL MANUAL BLYNK =====");

    if (!pumpRunning) {
      manualPumpActive = true;
      Serial.println("Perintah manual diterima dari Blynk.");
      Serial.println("Keputusan otomatis dilewati sementara.");
      pumpOn();  // Pompa tetap dibatasi timer maksimal 10 detik.
    } else {
      Serial.println("Pompa sedang ON.");
      Serial.println("Perintah manual diabaikan demi keamanan.");
    }
  }
}

// =====================================================
// FUNGSI TIMER POMPA
// =====================================================
void updatePumpTimer() {
  if (!pumpRunning) {
    return;
  }
  unsigned long currentMillis = millis();
  if (currentMillis - pumpStartMillis >= PUMP_MAX_DURATION) {
    Serial.println();
    Serial.println("Timer 10 detik pompa telah selesai.");
    Serial.println("Pompa dimatikan otomatis.");
    pumpOff();
  }
}

// =====================================================
// MEMBACA SENSOR TANAH
// =====================================================
void readSoilMoisture() {
  nilaiSoilRaw = analogRead(SOIL_PIN);
  kelembapanTanah = map(nilaiSoilRaw, SOIL_DRY, SOIL_WET, 0, 100);
  kelembapanTanah = constrain(kelembapanTanah, 0, 100);

  // Status tanah
  if (kelembapanTanah < 40) {
    statusTanah = "KERING";
  } else if (kelembapanTanah < 70) {
    statusTanah = "CUKUP LEMBAB";
  } else {
    statusTanah = "LEMBAB/BASAH";
  }

  Serial.println();
  Serial.println("===== SENSOR TANAH =====");
  Serial.print("RAW : ");
  Serial.println(nilaiSoilRaw);
  Serial.print("Kelembapan tanah : ");
  Serial.print(kelembapanTanah);
  Serial.println(" %");
  Serial.print("Status tanah : ");
  Serial.println(statusTanah);
}

// =====================================================
// MEMBACA DHT22
// =====================================================
void readDHT22() {
  Serial.println();
  Serial.println("===== SENSOR DHT22 =====");
  float h = NAN;
  float t = NAN;
  // Coba baca maksimal 2 kali. Jeda 2,2 detik sesuai kebutuhan DHT22.
  for (int attempt = 1; attempt <= 2; attempt++) {
    h = dht.readHumidity();
    t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      break;
    }
    if (attempt < 2) {
      Serial.println("Pembacaan DHT22 gagal; mencoba sekali lagi dalam 2,2 detik...");
      delay(2200);
    }
  }
  if (isnan(h) || isnan(t)) {
    dhtValid = false;
    Serial.println("DHT22 tidak dapat dibaca setelah 2 percobaan.");
    Serial.println("Periksa kabel, catu daya, dan kemungkinan gangguan listrik.");
    Serial.println("Sistem tetap melanjutkan proses dengan DHT22 sebagai data pendukung.");
    return;
  }

  suhuUdara = t;
  kelembapanUdara = h;
  dhtValid = true;
  Serial.print("Suhu udara : ");
  Serial.print(suhuUdara, 2);
  Serial.println(" C");
  Serial.print("Kelembapan udara : ");
  Serial.print(kelembapanUdara, 2);
  Serial.println(" %");
  Serial.println("Status DHT22 : VALID");
}

// =====================================================
// MENGAMBIL DATA BMKG
// =====================================================

bool getBMKGData() {
  bmkgDataValid = false;

  Serial.println();
  Serial.println("===== DATA BMKG =====");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);
  client.setSSLVersion(BR_TLS12, BR_TLS12);

  HTTPClient https;
  https.useHTTP10(true);
  https.setTimeout(20000);

  if (!https.begin(client, bmkgURL)) {
    Serial.println("Gagal memulai koneksi BMKG.");
    return false;
  }

  int httpCode = https.GET();

  Serial.print("HTTP BMKG: ");
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Gagal mendapatkan data BMKG.");

    if (httpCode < 0) {
      Serial.println(https.errorToString(httpCode));
    }

    https.end();
    return false;
  }

  Serial.println("Koneksi ke BMKG Worker berhasil.");

  // Ambil ukuran respons
  int contentLength = https.getSize();

  Serial.print("Ukuran JSON BMKG: ");
  Serial.println(contentLength);

  if (contentLength <= 0 || contentLength > 20000) {
    Serial.println("Ukuran JSON tidak valid.");
    https.end();
    return false;
  }

  // Kapasitas dokumen diperbesar
  StaticJsonDocument<6000> filter;

  filter["lokasi"]["desa"] = true;
  filter["lokasi"]["kecamatan"] = true;
  filter["lokasi"]["kotkab"] = true;
  filter["lokasi"]["provinsi"] = true;

  filter["data"][0]["cuaca"][0][0]["local_datetime"] = true;
  filter["data"][0]["cuaca"][0][0]["t"] = true;
  filter["data"][0]["cuaca"][0][0]["hu"] = true;
  filter["data"][0]["cuaca"][0][0]["weather_desc"] = true;
  filter["data"][0]["cuaca"][0][0]["tp"] = true;
  filter["data"][0]["cuaca"][0][0]["ws"] = true;
  filter["data"][0]["cuaca"][0][0]["wd"] = true;

  // Dokumen hasil parsing
  StaticJsonDocument<6000> doc;

  // Baca langsung dari stream HTTP
  DeserializationError error = deserializeJson(
    doc,
    *https.getStreamPtr(),
    DeserializationOption::Filter(filter)
  );

  https.end();

  if (error) {
    Serial.print("JSON BMKG gagal diproses: ");
    Serial.println(error.c_str());
    return false;
  }

  // Ambil data lokasi
  JsonObject lokasi = doc["lokasi"];

  // Ambil prakiraan cuaca pertama
  JsonObject cuaca = doc["data"][0]["cuaca"][0][0];

  if (lokasi.isNull() || cuaca.isNull()) {
    Serial.println("Struktur JSON BMKG tidak sesuai.");
    return false;
  }

  // Simpan data BMKG
  bmkgWaktu = cuaca["local_datetime"].as<String>();
  suhuBMKG = cuaca["t"].as<float>();
  kelembapanBMKG = cuaca["hu"].as<float>();
  bmkgCuaca = cuaca["weather_desc"].as<String>();
  kecepatanAngin = cuaca["ws"].as<float>();
  bmkgArahAngin = cuaca["wd"].as<String>();

  curahHujanBMKG = cuaca["tp"] | 0.0;

  // Tampilkan lokasi
  Serial.println();
  Serial.println("===== HASIL PEMBACAAN BMKG =====");

  Serial.print("Desa/Kelurahan: ");
  Serial.println(lokasi["desa"].as<String>());

  Serial.print("Kecamatan: ");
  Serial.println(lokasi["kecamatan"].as<String>());

  Serial.print("Kabupaten/Kota: ");
  Serial.println(lokasi["kotkab"].as<String>());

  Serial.print("Provinsi: ");
  Serial.println(lokasi["provinsi"].as<String>());

  // Tampilkan cuaca
  Serial.print("Waktu: ");
  Serial.println(bmkgWaktu);

  Serial.print("Suhu BMKG: ");
  Serial.print(suhuBMKG, 2);
  Serial.println(" C");

  Serial.print("Kelembapan BMKG: ");
  Serial.print(kelembapanBMKG, 2);
  Serial.println(" %");

  Serial.print("Cuaca: ");
  Serial.println(bmkgCuaca);

  Serial.print("Curah hujan: ");
  Serial.print(curahHujanBMKG, 2);
  Serial.println(" mm");

  Serial.print("Kecepatan angin: ");
  Serial.print(kecepatanAngin, 2);
  Serial.println(" m/s");

  Serial.print("Arah angin: ");
  Serial.println(bmkgArahAngin);

  // Validasi hasil
  bmkgDataValid =
    !bmkgWaktu.isEmpty() &&
    !bmkgCuaca.isEmpty() &&
    !lokasi["desa"].isNull();

  if (bmkgDataValid) {
    Serial.println("Data BMKG berhasil dibaca.");
  } else {
    Serial.println("Data BMKG tidak lengkap.");
  }

  Serial.println("===== DATA BMKG SELESAI =====");

  return bmkgDataValid;
}


// =====================================================
// INFORMASI PERANGKAT ESP8266
// =====================================================
void tampilkanVersiESP8266() {
  Serial.println();
  Serial.println("===== INFORMASI ESP8266 =====");

  Serial.print("Versi Core ESP8266: ");
  Serial.println(ESP.getCoreVersion());

  Serial.print("Versi SDK: ");
  Serial.println(ESP.getSdkVersion());

  Serial.print("Frekuensi CPU: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");

  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());

  Serial.print("Free Sketch Space: ");
  Serial.println(ESP.getFreeSketchSpace());

  Serial.println("===== INFORMASI SELESAI =====");
}


// =====================================================
// MEMERIKSA PREDIKSI HUJAN BMKG
// =====================================================
bool bmkgPredictsRain() {
  if (!bmkgDataValid) {
    return false;
  }

  String forecast = bmkgCuaca;
  forecast.toLowerCase();

  // Prioritas hujan berdasarkan deskripsi dan curah hujan BMKG.
  if (forecast.indexOf("hujan") >= 0) {
    return true;
  }

  if (curahHujanBMKG > 0.0) {
    return true;
  }

  return false;
}

// =====================================================
// FUNGSI MEMANGGIL GEMINI
// =====================================================
bool askGemini() {
  if (millis() < geminiRetryUntil) {
    Serial.println("Gemini masih dalam masa retry.");
    return false;
  }

  Serial.println();
  Serial.println("===== GEMINI AI =====");
  Serial.println("Mengirim data ke Gemini API...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.useHTTP10(true);

  if (!https.begin(client, geminiURL)) {
    Serial.println("Gagal memulai koneksi Gemini.");
    return false;
  }

  https.setTimeout(20000);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-goog-api-key", geminiAPIKey);

  // Prompt
  String prompt = "";
  prompt += "Anda adalah sistem pengambil keputusan penyiraman tanaman.\n\n";
  prompt += "PARAMETER UTAMA:\n";
  prompt += "Kelembapan tanah: " + String(kelembapanTanah) + "%\n";
  prompt += "Status tanah: " + statusTanah + "\n\n";
  prompt += "DATA CUACA BMKG:\n";
  prompt += "Suhu BMKG: " + String(suhuBMKG, 2) + " C\n";
  prompt += "Kelembapan BMKG: " + String(kelembapanBMKG, 2) + "%\n";
  prompt += "Cuaca: " + bmkgCuaca + "\n";
  prompt += "Kecepatan angin: " + String(kecepatanAngin, 2) + " m/s\n";
  prompt += "Arah angin: " + bmkgArahAngin + "\n\n";
  prompt += "DATA DHT22 SEBAGAI DATA PENDUKUNG:\n";

  if (dhtValid) {
    prompt += "Suhu udara: " + String(suhuUdara, 2) + " C\n";
    prompt += "Kelembapan udara: " + String(kelembapanUdara, 2) + "%\n";
  } else {
    prompt += "Data DHT22 tidak tersedia.\n";
  }

  prompt += "\n";
  prompt += "ATURAN KEPUTUSAN (IKUTI PRIORITAS BERIKUT):\n";
  prompt += "1. PRIORITAS TERTINGGI: jika deskripsi cuaca BMKG menunjukkan hujan atau jenis hujan apa pun, wajib pilih TIDAK MENYIRAM.\n";
  prompt += "2. Jika BMKG memprediksi hujan, jangan memilih MENYIRAM meskipun tanah kering.\n";
  prompt += "3. Jika BMKG tidak menunjukkan hujan, kelembapan tanah adalah parameter utama.\n";
  prompt += "4. Data DHT22 hanya sebagai parameter pendukung dan bukan syarat wajib.\n";
  prompt += "5. Jika tanah kering dan kondisi mendukung penyiraman, pilih MENYIRAM.\n";
  prompt += "6. Jika tanah sudah lembab atau basah, pilih TIDAK MENYIRAM.\n\n";
  prompt += "OUTPUT HANYA BOLEH:\n";
  prompt += "MENYIRAM\n";
  prompt += "atau\n";
  prompt += "TIDAK MENYIRAM\n";
  prompt += "Jangan memberikan penjelasan tambahan.";

  // JSON Request
  StaticJsonDocument<3500> requestDoc;
  JsonArray contents = requestDoc["contents"].to<JsonArray>();
  JsonObject content = contents.add<JsonObject>();
  content["role"] = "user";
  JsonArray parts = content["parts"].to<JsonArray>();
  JsonObject part = parts.add<JsonObject>();
  part["text"] = prompt;

  JsonObject generationConfig = requestDoc["generationConfig"].to<JsonObject>();
  generationConfig["maxOutputTokens"] = 32;
  JsonObject thinkingConfig = generationConfig["thinkingConfig"].to<JsonObject>();
  thinkingConfig["thinkingLevel"] = "MINIMAL";

  String requestBody;
  serializeJson(requestDoc, requestBody);

  Serial.println("Request Gemini dibuat.");
  Serial.println("Menunggu respons Gemini...");

  int httpCode = https.POST(requestBody);
  Serial.print("HTTP Gemini Response Code: ");
  Serial.println(httpCode);

  if (httpCode == 429) {
    Serial.println();
    Serial.println("Gemini HTTP 429 - QUOTA / RATE LIMIT.");
    String errorResponse = https.getString();
    Serial.println(errorResponse);

    // Gunakan retryDelay dari respons API jika tersedia.
    unsigned long retryDelayMs = DEFAULT_RETRY_DELAY;
    int retryPos = errorResponse.indexOf("retryDelay");
    if (retryPos >= 0) {
      int colonPos = errorResponse.indexOf(':', retryPos);
      int quoteStart = errorResponse.indexOf('"', colonPos + 1);
      int secondsEnd = errorResponse.indexOf('s', quoteStart + 1);
      if (colonPos >= 0 && quoteStart >= 0 && secondsEnd > quoteStart) {
        String retrySecondsText = errorResponse.substring(quoteStart + 1, secondsEnd);
        unsigned long retrySeconds = strtoul(retrySecondsText.c_str(), nullptr, 10);
        if (retrySeconds > 0 && retrySeconds < 172800UL) {
          retryDelayMs = retrySeconds * 1000UL;
        }
      }
    }

    geminiRetryUntil = millis() + retryDelayMs;
    Serial.println();
    Serial.print("Gemini akan dicoba kembali setelah sekitar ");
    Serial.print(retryDelayMs / 1000UL);
    Serial.println(" detik, sesuai informasi retry API.");

    https.end();
    return false;
  }

  if (httpCode <= 0) {
    Serial.print("Gagal koneksi Gemini: ");
    Serial.println(https.errorToString(httpCode));
    https.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Respons Gemini mengalami error:");
    String errorResponse = https.getString();
    Serial.println(errorResponse);
    https.end();
    return false;
  }

  // Parse Response
  String responseBody = https.getString();
  Serial.println();
  Serial.println("===== RESPONSE GEMINI =====");
  Serial.println(responseBody);
  Serial.println("===== END RESPONSE =====");

  StaticJsonDocument<8000> responseDoc;
  DeserializationError error = deserializeJson(responseDoc, responseBody);

  if (error) {
    Serial.print("JSON Gemini gagal diproses: ");
    Serial.println(error.c_str());
    https.end();
    return false;
  }

  modelVersion = responseDoc["modelVersion"].as<String>();
  finishReason = responseDoc["candidates"][0]["finishReason"].as<String>();

  String outputGemini = responseDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
  outputGemini.trim();
  outputGemini.toUpperCase();
  outputGemini.replace("\"", "");
  outputGemini.trim();

  Serial.println();
  Serial.println("===== HASIL GEMINI =====");
  Serial.print("Model : ");
  Serial.println(geminiModel);
  Serial.print("Finish reason : ");
  Serial.println(finishReason);
  Serial.print("Output Gemini : ");
  Serial.println(outputGemini);

  // Pastikan respons Gemini selesai dengan normal.
  if (finishReason != "STOP") {
    keputusanAI = "TIDAK MENYIRAM";
    Serial.println("Respons Gemini tidak selesai secara normal.");
    Serial.println("Keputusan akan menggunakan aturan cadangan sensor tanah.");
    https.end();
    return false;
  }

  // Hanya dua keluaran yang diizinkan.
  if (outputGemini == "MENYIRAM") {
    keputusanAI = "MENYIRAM";
    Serial.println("Output AI valid: MENYIRAM");
  } else if (outputGemini == "TIDAK MENYIRAM") {
    keputusanAI = "TIDAK MENYIRAM";
    Serial.println("Output AI valid: TIDAK MENYIRAM");
  } else {
    keputusanAI = "TIDAK MENYIRAM";
    Serial.println("Output AI tidak valid.");
    Serial.println("Keputusan akan menggunakan aturan cadangan sensor tanah.");
    https.end();
    return false;
  }

  https.end();
  return true;
}

// =====================================================
// KONTROL POMPA BERDASARKAN AI
// =====================================================
void controlPump() {
  // Selama penyiraman manual berlangsung, jangan biarkan keputusan
  // otomatis BMKG/Gemini mematikan pompa sebelum timer 10 detik selesai.
  if (manualPumpActive) {
    Serial.println();
    Serial.println("===== KONTROL POMPA =====");
    Serial.println("Kontrol manual Blynk sedang aktif.");
    Serial.println("Keputusan otomatis dilewati sementara.");
    Serial.println("Pompa akan berhenti otomatis setelah maksimal 10 detik.");
    return;
  }

  // Validasi program: prediksi hujan BMKG mengalahkan keputusan Gemini/fallback.
  if (bmkgPredictsRain()) {
    if (keputusanAI != "TIDAK MENYIRAM") {
      Serial.println("ATURAN PRIORITAS BMKG: prediksi hujan terdeteksi.");
      Serial.println("Keputusan MENYIRAM dibatalkan menjadi TIDAK MENYIRAM.");
    } else {
      Serial.println("ATURAN PRIORITAS BMKG: prediksi hujan; pompa harus OFF.");
    }
    keputusanAI = "TIDAK MENYIRAM";
    sumberKeputusan = "ATURAN PRIORITAS BMKG";
  }

  Serial.println();
  Serial.println("===== KONTROL POMPA =====");
  Serial.print("Keputusan : ");
  Serial.println(keputusanAI);

  if (keputusanAI == "MENYIRAM") {
    if (!pumpRunning) {
      pumpOn();
    } else {
      Serial.println("Pompa masih ON.");
      Serial.println("Timer 10 detik masih berjalan.");
    }
  } else {
    if (pumpRunning) {
      Serial.println("AI memutuskan TIDAK MENYIRAM.");
      Serial.println("Pompa dihentikan.");
    }
    pumpOff();
  }

  Serial.println("==========================================");
}

// =====================================================
// TAMPILKAN STATUS SISTEM
// =====================================================
void printSystemStatus() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("       STATUS SISTEM");
  Serial.println("==========================================");
  Serial.print("Kelembapan tanah : ");
  Serial.print(kelembapanTanah);
  Serial.println(" %");
  Serial.print("Status tanah : ");
  Serial.println(statusTanah);
  Serial.print("DHT22 : ");

  if (dhtValid) {
    Serial.println("VALID");
    Serial.print("Suhu udara : ");
    Serial.print(suhuUdara, 2);
    Serial.println(" C");
    Serial.print("Kelembapan udara : ");
    Serial.print(kelembapanUdara, 2);
    Serial.println(" %");
  } else {
    Serial.println("TIDAK TERBACA - DATA PENDUKUNG");
  }

  Serial.print("Keputusan sistem : ");
  Serial.println(keputusanAI);
  Serial.print("Sumber keputusan : ");
  Serial.println(sumberKeputusan);
  Serial.print("Pompa : ");

  if (pumpRunning) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  Serial.println("==========================================");
}

// =====================================================
// ATURAN CADANGAN SAAT GEMINI/BMKG TIDAK TERSEDIA
// =====================================================
void applySoilFallback(const String& reason) {
  Serial.println();
  Serial.println("===== ATURAN CADANGAN SENSOR TANAH =====");
  Serial.print("Alasan: ");
  Serial.println(reason);
  Serial.print("Kelembapan tanah terbaca: ");
  Serial.print(kelembapanTanah);
  Serial.println(" %");

  sumberKeputusan = "ATURAN CADANGAN SENSOR TANAH";

  if (kelembapanTanah < 40) {
    keputusanAI = "MENYIRAM";
    Serial.println("Tanah di bawah 40%: sistem cadangan memilih MENYIRAM.");
  } else {
    keputusanAI = "TIDAK MENYIRAM";
    Serial.println("Tanah 40% atau lebih: sistem cadangan memilih TIDAK MENYIRAM.");
  }

  Serial.println("Catatan: keputusan ini berasal dari aturan ambang, bukan Gemini AI.");
}

// =====================================================
// KIRIM DATA KE BLYNK
// =====================================================
void syncBlynkData() {
  if (!Blynk.connected()) {
    return;
  }

  Blynk.virtualWrite(V0, kelembapanTanah);

  if (dhtValid) {
    Blynk.virtualWrite(V1, suhuUdara);
    Blynk.virtualWrite(V2, kelembapanUdara);
  }

  if (bmkgCuaca.length() > 0) {
    Blynk.virtualWrite(V4, bmkgCuaca);
  }

  if (keputusanAI.length() > 0) {
    Blynk.virtualWrite(V5, keputusanAI);
  }

  Blynk.virtualWrite(V6, pumpRunning ? 1 : 0);
  Serial.println("Data terbaru dikirim ke Blynk.");
}

// =====================================================
// MENJALANKAN SATU SIKLUS SISTEM
// =====================================================
void runSystemCycle() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("       MEMULAI SIKLUS SISTEM");
  Serial.println("==========================================");

  // 1. SENSOR TANAH
  readSoilMoisture();

  // 2. DHT22
  readDHT22();

  // 3. BMKG
  bool bmkgOK = getBMKGData();

  if (!bmkgOK) {
    Serial.println();
    Serial.println("BMKG gagal diperoleh.");
    Serial.println("Gemini tidak dijalankan karena data cuaca tidak tersedia.");

    applySoilFallback("Data BMKG tidak tersedia");

    Serial.println();
    Serial.println("KEPUTUSAN AKHIR SISTEM:");
    Serial.println(keputusanAI);

    controlPump();
    printSystemStatus();
    syncBlynkData();

    Serial.println();
    Serial.println("SIKLUS SELESAI");
    return;
  }

  // 4. GEMINI
  bool geminiOK = askGemini();
  if (!geminiOK) {
    Serial.println();
    Serial.println("Gemini gagal diproses atau sedang terkena batas kuota.");
    applySoilFallback("Gemini API gagal, timeout, atau kuota habis");
  } else {
    sumberKeputusan = "GEMINI AI";
  }

  // 5. HASIL AKHIR
  Serial.println();
  Serial.print("KEPUTUSAN AKHIR SISTEM: ");
  Serial.println(keputusanAI);
  Serial.print("Sumber keputusan: ");
  Serial.println(sumberKeputusan);

  // 6. KONTROL POMPA & STATUS
  controlPump();
  printSystemStatus();
  syncBlynkData();

  Serial.println();
  Serial.println("==========================================");
  Serial.println("           SIKLUS SELESAI");
  Serial.println("==========================================");
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" SISTEM PENYIRAMAN TANAMAN");
  Serial.println(" IoT + AI + BMKG");
  Serial.println("==========================================");

  // Inisialisasi DHT
  dht.begin();
  Serial.println("DHT22 berhasil diinisialisasi.");

  // Inisialisasi Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pumpRunning = false;
  Serial.println("Relay berhasil diinisialisasi.");
  Serial.println("Pompa dalam kondisi OFF.");

  // Koneksi WiFi
  Serial.println();
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi berhasil terhubung.");
    Serial.print("IP Address : ");
    Serial.println(WiFi.localIP());

    tampilkanVersiESP8266();
    Serial.println("Koneksi WiFi siap.");
    Serial.println("BMKG akan diperiksa pada siklus sistem.");

  } else {
    Serial.println("WiFi belum terhubung.");
  }

  // BLYNK
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.config(BLYNK_AUTH_TOKEN);
    Serial.println("Mencoba menghubungkan ke Blynk...");
    if (Blynk.connect(1000)) {
      Serial.println("Blynk berhasil terhubung.");
      syncBlynkData();
    } else {
      Serial.println("Blynk belum terhubung; sistem sensor tetap berjalan.");
    }
  }

  // Supaya siklus pertama langsung dijalankan
  lastSystemRun = millis() - SYSTEM_INTERVAL;

  Serial.println();
  Serial.println("Sistem siap digunakan.");
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  updatePumpTimer();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  // Cek WiFi
  if (WiFi.status() != WL_CONNECTED) {
    pumpOff();
    Serial.println();
    Serial.println("WiFi terputus.");

    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect >= 10000) {
      lastReconnect = millis();
      Serial.println("Mencoba menghubungkan kembali...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
    return;
  }

  // Interval Sistem
  unsigned long currentMillis = millis();
  if (currentMillis - lastSystemRun >= SYSTEM_INTERVAL) {
    lastSystemRun = currentMillis;
    runSystemCycle();
  }
}
