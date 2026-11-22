#ifndef TERAZI_CONFIG_H
#define TERAZI_CONFIG_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

/*
 * ============================================================================
 * MERKEZİ DONANIM, ÖLÇÜM, AĞ VE KALICILIK AYARLARI (terazi_config.h)
 * ----------------------------------------------------------------------------
 * Hedef Kart  : ESP32 (ESP-32S / WROOM sınıfı 30 pin DevKit V1)
 * Sensör      : HX711 24-bit ADC + 5 kg 4 telli tam köprü (Wheatstone) yük hücresi
 * Ekran       : 20x4 I2C LCD (PCF8574 adaptörlü) + 3,3V <-> 5V seviye dönüştürücü
 *
 * NOT: Fiziksel donanım henüz kargodadır. Buradaki pin, adres ve eşik değerleri
 * proje tasarım planına göre merkezi sabitler olarak tanımlanmıştır; donanım
 * teslim alındığında yalnızca bu dosyadan kolayca güncellenebilir.
 * ============================================================================
 */

// ----------------------------------------------------------------------------
// 1. GPIO PİN ATAMALARI (ESP32-WROOM 30 Pin Planı)
// ----------------------------------------------------------------------------
// HX711 veri çıkışı (DOUT / DT).
// Neden GPIO16: WROOM modüllerde genel amaçlı giriş/çıkış için uygundur.
// Uyarı: WROVER kartlarda GPIO16/17 PSRAM tarafından kullanılır.
constexpr uint8_t PIN_HX711_DOUT = 16;

// HX711 saat girişi (PD_SCK / SCK).
// Neden GPIO4: Çıkış olarak kararlı çalışır; 60 us'den uzun HIGH kalmamalıdır.
constexpr uint8_t PIN_HX711_SCK = 4;

// I2C veri (SDA) ve saat (SCL) hatları.
// Neden GPIO21 / GPIO22: ESP32 Arduino çekirdeğinin standart donanım I2C pinleridir.
// 5 V pull-up içeren PCF8574 LCD modülleriyle çift yönlü seviye dönüştürücü kullanılmalıdır.
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;

// Fiziksel dara (sıfırlama) butonu pini.
// Neden GPIO27 + INPUT_PULLUP: Harici direnç gerektirmeden buton GND'ye bağlanır;
// basılı değilken HIGH, basıldığında LOW okunur.
constexpr uint8_t PIN_TARE_BUTTON = 27;

// ----------------------------------------------------------------------------
// 2. 20x4 I2C LCD AYARLARI (LiquidCrystal_PCF8574 v2.3.0)
// ----------------------------------------------------------------------------
// Gercek LCD, I2C taramasinda 0x27 adresinde ardisik yanit verdi.
// Urun sayfasindaki 0x3F yerine eldeki kartta dogrulanan adres kullanilir.
constexpr uint8_t LCD_I2C_ADDR = 0x27;
constexpr uint8_t LCD_COLS = 20;
constexpr uint8_t LCD_ROWS = 4;
constexpr uint8_t LCD_BACKLIGHT_ON = 255;

// LCD yenileme periyodu (birim: milisaniye).
// Neden 250 ms: Saniyede 4 güncelleme insan gözü için akıcıdır ve I2C hattını gereksiz meşgul etmez.
constexpr unsigned long LCD_UPDATE_INTERVAL_MS = 250UL;

// ----------------------------------------------------------------------------
// 3. HX711 ADC VE FİZİKSEL ÖLÇÜM EŞİKLERİ
// ----------------------------------------------------------------------------
// Kanal A kazanç seçimi (128: +/-20 mV diferansiyel tam ölçek giriş aralığı).
constexpr uint8_t HX711_GAIN_CHANNEL_A = 128;

// 24-bit ikinin tümleyeni (signed 24-bit) ADC doyma (saturasyon) sınırları (birim: ham ADC sayımı).
// Geçerli 24-bit aralık [-8388608, +8388607] olup uç ray değerleri (-8388608 ve +8388607)
// analog/dijital doyma (saturasyon) veya kopuk hat göstergesidir.
// Normal negatif veya sıfır ham değerler (ör. 0, -1, -250000) tamamen geçerlidir.
constexpr long HX711_RAW_SAT_MIN = -8388608L;
constexpr long HX711_RAW_SAT_MAX = 8388607L;

// Yük hücresi nominal üst kapasitesi (birim: gram).
constexpr float MAX_CAPACITY_G = 5000.0f;

// Kalibrasyon sırasında yüklü ham ortalama ile boş referans (R0) arasındaki
// izin verilen en küçük mutlak fark (birim: ham ADC sayımı).
// Neden: Yüksüz veya sıfır farkla kalibrasyon yapılmasını (sıfıra bölme / gürültü katsayısı) önler.
constexpr double MIN_CALIBRATION_RAW_DIFF = 10.0;

// ----------------------------------------------------------------------------
// 4. ÖRNEKLEME, FİLTRE VE ZAMANLAMA SABİTLERİ
// ----------------------------------------------------------------------------
// Açılış sıfırlaması (boş platform referansı R0), dara ve kalibrasyon işlemlerinde
// loop() içinde bloklamadan toplanacak taze örnek sayıları (birim: örnek adedi).
// 10 SPS hızında 3 örnek yaklaşık 300 ms sürer; HTTP veya setup() içinde beklenmez.
constexpr uint8_t STARTUP_ZERO_SAMPLE_COUNT = 3;
constexpr uint8_t TARE_SAMPLE_COUNT = 11; // En küçük ve en büyük örneği çıkar; kalan 9'u ortala.
constexpr unsigned long PHYSICAL_TARE_SETTLE_MS = 300UL; // Kararlı bırakıştan sonra örnekleme beklemesi.
constexpr uint8_t CALIBRATE_SAMPLE_COUNT = 3;

// Normal ölçüm akışında kullanılan hareketli ortalama pencere boyutu (birim: örnek adedi).
// Yeni dara veya kalibrasyon başladığında bu pencere sıfırlanır; eski örnekler yeni yükle karışmaz.
constexpr uint8_t FILTER_WINDOW_SIZE = 3;

// Sensör hazır (DOUT == LOW) bekleme zaman aşımı (birim: milisaniye).
// Bu süre boyunca yeni geçerli örnek gelmezse sensor_ok = false olur ve eski ağırlık sunulmaz.
constexpr unsigned long READY_TIMEOUT_MS = 1000UL;

// Seri port hata mesajı hız sınırlama aralığı (birim: milisaniye).
constexpr unsigned long ERROR_REPORT_INTERVAL_MS = 2000UL;

// Fiziksel dara butonu ark (sıçrama / debounce) süzme süresi (birim: milisaniye).
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50UL;

// Seri haberleşme hızı (birim: bit/saniye - baud).
constexpr unsigned long SERIAL_BAUD = 115200UL;

// ----------------------------------------------------------------------------
// 5. KALICI BELLEK (ESP32 NVS / Preferences) AYARLARI
// ----------------------------------------------------------------------------
// NVS isim alanı ve kalibrasyon katsayısı anahtarı (en fazla 15 karakter).
// Saklanan K katsayısının birimi: [ham ADC sayımı / gram]. Negatif K geçerlidir;
// 0, NaN veya Infinity değerler kalibrasyonsuz kabul edilir.
constexpr const char* NVS_NAMESPACE = "terazi";
constexpr const char* NVS_KEY_SCALE = "scale_k";

// ----------------------------------------------------------------------------
// 6. WI-FI SOFTAP VE YEREL HTTP SUNUCU AYARLARI
// ----------------------------------------------------------------------------
// İnternetsiz yerel erişim noktası (SoftAP) kimliği ve parolası (WPA2 için en az 8 karakter).
constexpr const char* WIFI_AP_SSID = "HassasTerazi";

#if defined(ARDUINO_ARCH_ESP32)
#include "wifi_local.h" // Yayımlanmayan yerel parola dosyası.
#else
constexpr const char* WIFI_AP_PASS = "host-test-only"; // Yalnızca sahte Wi-Fi ile çalışan host testleri.
#endif
constexpr uint16_t HTTP_PORT = 80;

// POST /api/calibrate JSON gövdesi için kabul edilen en büyük boyut (birim: bayt).
constexpr size_t MAX_JSON_BODY_BYTES = 256U;

#endif // TERAZI_CONFIG_H
