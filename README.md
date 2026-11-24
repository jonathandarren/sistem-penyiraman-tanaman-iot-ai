# Sistem Penyiraman Tanaman Pintar Berbasis IoT dengan Integrasi AI dan Data Cuaca Real-Time

## Deskripsi Proyek

Proyek ini merupakan implementasi sistem penyiraman tanaman pintar berbasis Internet of Things (IoT) yang mengintegrasikan sensor, informasi cuaca BMKG, dan Artificial Intelligence (AI) untuk membantu menentukan keputusan penyiraman tanaman secara otomatis.

Sistem dirancang untuk memantau kondisi kelembapan tanah, suhu udara, dan kelembapan udara. Data sensor dikombinasikan dengan informasi cuaca dari BMKG, kemudian dikirimkan ke Gemini AI melalui API untuk menghasilkan rekomendasi apakah tanaman perlu disiram atau tidak.

Keputusan tersebut digunakan oleh NodeMCU ESP8266 untuk mengendalikan pompa air melalui modul relay. Pengguna juga dapat memantau kondisi sistem melalui aplikasi Blynk.

## Tujuan Proyek

- Mengembangkan sistem penyiraman tanaman otomatis berbasis IoT.
- Memantau kelembapan tanah dan kondisi udara menggunakan sensor.
- Mengintegrasikan informasi cuaca BMKG sebagai salah satu pertimbangan penyiraman.
- Memanfaatkan Gemini AI untuk membantu proses pengambilan keputusan.
- Memungkinkan pemantauan dan pengendalian sistem melalui aplikasi Blynk.

## Komponen Perangkat Keras

| Komponen | Fungsi |
|---|---|
| NodeMCU ESP8266 | Mikrokontroler dan koneksi internet |
| Sensor kelembapan tanah | Mengukur kelembapan tanah |
| DHT22 | Mengukur suhu dan kelembapan udara |
| Modul relay | Mengendalikan pompa air |
| Pompa air 12V | Menyalurkan air untuk penyiraman |
| Adaptor 12V | Sumber daya pompa |

## Teknologi dan Platform

- **Arduino IDE** — pemrograman mikrokontroler.
- **ESP8266** — konektivitas Wi-Fi dan pengendalian perangkat.
- **BMKG API** — memperoleh informasi prakiraan cuaca.
- **Gemini AI API** — membantu menentukan keputusan penyiraman berdasarkan data masukan.
- **Blynk IoT** — pemantauan dan pengendalian sistem melalui dashboard.

## Cara Kerja Sistem

1. NodeMCU ESP8266 membaca nilai kelembapan tanah dari sensor.
2. Sensor DHT22 membaca suhu dan kelembapan udara.
3. Sistem mengambil informasi cuaca dari BMKG melalui layanan proxy.
4. Data sensor dan informasi cuaca dikirimkan ke Gemini AI.
5. Gemini AI memberikan keputusan berupa `MENYIRAM` atau `TIDAK MENYIRAM`.
6. Sistem memeriksa kondisi cuaca dan menerapkan pengamanan sebelum mengendalikan pompa.
7. Jika penyiraman diperlukan, NodeMCU mengaktifkan relay dan pompa air.
8. Pompa memiliki durasi penyiraman otomatis maksimal 10 detik berdasarkan mekanisme timer program.
9. Informasi kondisi sensor, cuaca, keputusan, dan status pompa dikirimkan ke Blynk.

Apabila layanan Gemini tidak dapat diakses, sistem memiliki mekanisme keputusan cadangan berdasarkan kondisi kelembapan tanah. Mekanisme cadangan ini bukan keputusan dari Gemini AI.

## Fitur Sistem

- Pemantauan kelembapan tanah.
- Pemantauan suhu dan kelembapan udara.
- Integrasi data cuaca BMKG.
- Pengambilan keputusan penyiraman menggunakan Gemini AI.
- Pengendalian pompa air otomatis.
- Pengendalian pompa secara manual melalui Blynk.
- Pemantauan status sistem melalui dashboard Blynk.
- Mekanisme keputusan cadangan ketika layanan AI gagal diakses.
- Pembatasan durasi penyiraman melalui timer program.

## Struktur Repository

```text
sistem-penyiraman-tanaman-iot-ai/
│
├── README.md
└── Sistem_Penyiraman_IoT_AI.ino
```

## Catatan Keamanan

Untuk menjaga keamanan sistem, jangan mengunggah informasi sensitif seperti:

- Blynk Auth Token.
- Gemini API Key.
- Password Wi-Fi.
- Kredensial akun atau layanan lainnya.

Gunakan placeholder atau konfigurasi lokal untuk menyimpan kredensial tersebut.

## Status Pengembangan

Proyek ini dikembangkan sebagai bagian dari penelitian skripsi mengenai sistem penyiraman tanaman pintar berbasis IoT dengan integrasi AI berdasarkan data cuaca real-time.

Pengembangan dan pengujian sistem dilakukan secara bertahap, meliputi pengujian sensor, pengambilan data cuaca, integrasi AI, pengendalian pompa, dan pemantauan melalui Blynk.

## Penulis

Proyek penelitian skripsi.

**Catatan:** Repository ini merupakan dokumentasi source code dan pengembangan prototipe sistem.
