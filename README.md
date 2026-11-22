# ESP32 Wheatstone Köprülü Hassas Terazi

5 kg nominal kapasiteli bir yük hücresi, HX711 ve ESP32 kullanan Ölçme ve Enstrümantasyon dersi prototipi. ESP32 ölçümü 20×4 I²C LCD'de gösterir ve kendi yerel Wi-Fi ağı üzerinden tablet tarayıcısına canlı ölçüm ve grafik sunar. İnternet gerekmez.

- **Donanım, pinler ve kalibrasyon:** [Firmware rehberi](firmware/README.md)
- **Tablet ve HTTP API:** [Tablet rehberi](tablet/README.md)
- **Teşhis:** [HX711 ham ölçüm](examples/hx711_raw_test/README.md) ve [I²C adres tarayıcı](examples/i2c_scan/README.md)
- **Simülasyon:** [`simulasyon/`](simulasyon/) ideal Wheatstone köprüsü modelini, 21 yük noktasını ve arşivlenmiş yazılım test çıktılarını içerir. Bunlar fiziksel ölçüm değildir.
- **Tasarım günlüğü:** [İlk proje dokümanı](ESP32_Wheatstone_Koprulu_Hassas_Terazi_Proje_Dokumani.md) tarihsel plandır; gerçek prototipte LCD 20×4 ve `0x27` adresindedir.

## Hızlı başlangıç

1. `firmware/wifi_local.example.h` dosyasını aynı klasörde `wifi_local.h` olarak kopyala. Örnek metni kendi parolanla değiştir (8–63 karakter). `wifi_local.h` Git tarafından izlenmez.
2. PlatformIO ile depo kökünde `pio run -d firmware -e terazi_main` çalıştır. Karta yükleme ve bağlantılar için firmware rehberini kullan.
3. Açılışta platformu boş bırak. Tabletle `HassasTerazi` ağına bağlan ve tarayıcıda `http://192.168.4.1` adresini aç. Bilinen bir kütleyle kalibrasyonu tamamla.

Tablet arayüzünü donanım olmadan görmek için `tablet/index.html` dosyasını tarayıcıda aç. Bu demo gerçek sensör verisi göstermez.
