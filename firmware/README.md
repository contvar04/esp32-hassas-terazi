# ESP32 Wheatstone Köprülü Hassas Terazi — Ana Firmware (`firmware/`)

Bu dizin, **ESP32 (ESP-32S / WROOM 30 pin DevKit V1)** geliştirme kartı üzerinde çalışan, **HX711 + 5 kg 4 telli tam köprü yük hücresi**, **20×4 I²C LCD (`LiquidCrystal_PCF8574`)**, **fiziksel dara butonu**, **NVS kalıcı kalibrasyon belleği (`Preferences`)** ve **Wi-Fi SoftAP + HTTP sunucusunu (`WebServer`)** tek uygulamada birleştiren Arduino C++ üretim yazılımını içerir.

> Prototipte LCD, HX711 ve tablet bağlantısı denendi. Bu depodaki otomatik testler gerçek donanım üzerinde yeni bir ölçüm değildir.

---

## 1. Merkezi Pin, Adres ve Eşik Ayarları (`terazi_config.h`)

Tüm donanım pinleri ve ölçüm eşikleri [`terazi_config.h`](terazi_config.h) dosyasında açıklamalı `constexpr` sabitler olarak tanımlanmıştır:

| Bileşen / Sinyal | ESP32 Pini / Değer | Sabit Adı | Açıklama ve Birim |
|---|---|---|---|
| HX711 `DOUT` (`DT`) | `GPIO16` | `PIN_HX711_DOUT` | Veri çıkışı (yalnız WROOM; WROVER kartlarda PSRAM kullanır) |
| HX711 `PD_SCK` (`SCK`) | `GPIO4` | `PIN_HX711_SCK` | Saat girişi (Kanal A, kazanç `128`) |
| I²C `SDA` | `GPIO21` | `PIN_I2C_SDA` | 3,3V ↔ 5V çift yönlü seviye dönüştürücü LV tarafı |
| I²C `SCL` | `GPIO22` | `PIN_I2C_SCL` | 3,3V ↔ 5V çift yönlü seviye dönüştürücü LV tarafı |
| Fiziksel Dara Butonu | `GPIO27` | `PIN_TARE_BUTTON` | `INPUT_PULLUP` (GND'ye bağlı, basılıyken `LOW`, `50 ms` debounce) |
| 20×4 I²C LCD Adresi | `0x27` | `LCD_I2C_ADDR` | Lehim sonrası gerçek I²C taramasında art arda doğrulandı; ürün sayfasındaki `0x3F` eldeki kartla eşleşmedi |
| Maksimum Kapasite | `5000.0 g` | `MAX_CAPACITY_G` | 5 kg tam köprü yük hücresi nominal üst sınırı (`gram`) |
| Sensör Zaman Aşımı | `1000 ms` | `READY_TIMEOUT_MS` | Yeni örnek gelmezse `sensor_ok = false` eşiği (`milisaniye`) |

---

## 2. Açılışta Boş Platform Beklentisi ve Matematiksel Model

1. **Açılış Sıfırlaması (Boş Platform Beklentisi):**
   - Cihaz enerjilendiğinde tartım platformunun **BOŞ** olması beklenir.
   - `setup()` fonksiyonu sensör bekleyerek sistemi bloklamaz (`read_average` veya `tare` çağrılmaz).
   - Boş platform referansı `R0`, `loop()` içinde `scale.is_ready()` oldukça tekil `scale.read()` çağrılarıyla toplanan ilk `3` geçerli örneğin ortalaması alınarak belirlenir.
2. **Kütle Hesabı ve İşaretli Katsayı (`K`):**
   - Kalibrasyon katsayısı `K`, **`[ham ADC sayımı / gram]`** birimindedir:
     $$K = \frac{R_{\text{yüklü}} - R_0}{m_{\text{bilinen\_gram}}}$$
   - Yük hücresinin montaj veya diferansiyel sinyal yönüne göre `K` **negatif veya pozitif** olabilir; işaret olduğu gibi korunur.
   - Net kütle:
     $$m_{\text{gram}} = \frac{R - R_0}{K}$$
   - Dara alındıktan sonra yük kaldırıldığında oluşan **negatif net ağırlıklar** ve sıfır değeri geçerli ölçüm olarak korunur.
   - 24-bit işaretli ADC uç doyma (saturasyon) değerleri (`-8388608` ve `+8388607`) hata olarak işaretlenir ve gerçek ağırlık gibi sunulmaz.
3. **Kalıcı Bellek (ESP32 NVS):**
   - Yalnızca başarılı kalibrasyon sonunda `K` katsayısı NVS belleğe (`Preferences`, `"terazi"` / `"scale_k"`) yazılır.
   - Normal ölçüm ve dara işlemleri flash belleğe **yazmaz** (flash aşınması önlenir).
   - `Preferences::putFloat` (`putBytes` → `nvs_commit`) dönüş değeri kontrol edilir; NVS yazma hatasında önceki geçerli `K` korunur ve `op_status: "failed"` raporlanır.
   - Açılışta NVS'de kayıt yoksa veya `0`, `NaN`, `Infinity` okunursa cihaz kalibrasyonsuz (`calibrated = false`) başlar.

---

## 3. İki Adımlı Kalibrasyon Akışı

1. **Adım 1 (Boş Referans `R0`):** Platformu tamamen boşaltın ve fiziksel dara butonuna veya tablet arayüzündeki **"Dara al"** düğmesine basın (`POST /api/tare`). `loop()` içinde taze örnekler toplanarak `R0` güncellenir ve hareketli ortalama filtresi sıfırlanır.
2. **Adım 2 (Bilinen Yük `K`):** Platforma bilinen kütleyi (`0 < known_g <= 5000 g`) yerleştirin, tablet arayüzünden kütleyi girip **"Kalibre et"** düğmesine basın (`POST /api/calibrate`). Eski filtre örnekleri temizlenir, taze örnekler toplanır, `K` hesaplanıp NVS'ye kaydedilir.

---

## 4. Bilinen Fiziksel ve Mimari Sınırlar

- **`HX711` Okuma Yarış Penceresi:** `bogde/HX711` v0.7.5 içindeki `read()` fonksiyonu `wait_ready()` içerir. Normal akışta yalnızca `scale.is_ready() == true` iken tekil `scale.read()` çağrılarak bloklama önlenir. Ancak `is_ready()` ile hemen sonraki `read()` arasındaki birkaç mikrosaniyelik pencerede fiziksel `DOUT` kablosu koparsa sürücü içi döngü bekleyebilir. ESP32 kritik bölge (`portENTER_CRITICAL`) zamanlama korumasını bozmamak için özel bit-bang sürücü yazılmamıştır.
- **`WebServer` Tek İstemci Yapısı:** Arduino-ESP32 dahili `WebServer` kütüphanesi tek istemciyi sırayla işler ve HTTP başlık/gövde okuma sırasında soket zaman aşımı beklemeleri içerir; tamamen bloklamayan asenkron sunucu değildir.
- **20×4 LCD Bağlantısı:** LCD takılı değilken (`lcd.isConnected() == false`) I²C yazmaları atlanır; web sunucusu ve ölçüm etkilenmez. Ekran bağlıyken `lcd.clear()` kullanılmadan yalnızca değişen satırlar `250 ms` aralıkla güncellenir.

---

## 5. Arduino IDE ile Açma ve HTML Başlığı Güncelleme

[`webui_html.h`](webui_html.h) dosyası depoda hazırdır. ESP32 hedefi için önce [`wifi_local.example.h`](wifi_local.example.h) dosyasını `wifi_local.h` olarak kopyalayın ve örnek metni kendi Wi-Fi parolanızla değiştirin. Yerel parola dosyası Git'e eklenmez. Ardından Arduino IDE'de [`firmware.ino`](firmware.ino) dosyasını açabilirsiniz:

1. Arduino IDE **Library Manager** üzerinden şu kütüphaneleri yükleyin:
   - `HX711 Arduino Library` — *Bogdan Necula* (`0.7.5`)
   - `LiquidCrystal_PCF8574` — *Matthias Hertel* (`2.3.0`)
2. `firmware/firmware.ino` dosyasını açın, kart olarak **ESP32 Dev Module** seçin ve derleyin.
3. Eğer [`tablet/index.html`](../tablet/index.html) dosyasında değişiklik yaparsanız, derlemeden önce depo kökünden şu komutu çalıştırarak `firmware/webui_html.h` dosyasını güncelleyin:
   ```powershell
   node firmware/tools/generate_webui_header.cjs
   node firmware/tools/generate_webui_header.cjs --check
   ```

---

## 6. Test ve Hedef Derleme Komutları

Komutları depo kökünden çalıştırın. Yerel derleme çıktıları `.pio/` altında kalır:

```powershell
New-Item -ItemType Directory -Force -Path .pio | Out-Null

# 1. Host C++ davranış testleri (MinGW g++)
g++ -std=c++11 -Wall -Wextra -I firmware/tests/mocks firmware/tests/test_firmware.cpp -o .pio/test_firmware.exe
& .pio/test_firmware.exe

# 2. Gerçek ESP32 hedef derlemesi (PlatformIO)
pio run -d firmware -e terazi_main
```
