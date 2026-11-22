# HX711 Ham ADC Okuma Testi (`hx711_raw_test`)

Bu örnek, **ESP32 (ESP-32S / WROOM sınıfı 30 pin)** geliştirme kartı ve **HX711** 24-bit ADC modülü üzerinden tek bir **5 kg, 4 telli tam köprü (Wheatstone)** yük hücresinden gelen **tekil ham işaretli (signed) ADC değerlerini** seri portta gözlemlemek için hazırlanmıştır.

> **Donanım Durumu:** Gerçek fiziksel donanım henüz teslim alınmamış ve fiziksel devre üzerinde denenmemiştir. Bu aşamada kod yalnızca host davranış testleri ve ESP32 hedef derlemesi ile doğrulanmıştır.

---

## 1. Neyi Ölçer ve Kapsam Sınırı

- **Ölçülen veri:** Yalnızca HX711'in hazır (`DOUT == LOW`) olduğu her yeni örnekte `scale.read()` ile alınan **tek bir 24-bit ikinin tümleyeni (32-bit `long` olarak işaret genişletmeli)** ham ADC sayısıdır (`HAM_ADC: <değer>`).
- **Kapsam dışı olanlar:** Bu testte ortalama/filtre (`read_average`), dara (`tare`), kalibrasyon katsayısı (`set_scale`), gram/kg/Newton hesabı, I²C LCD, Wi-Fi/HTTP sunucusu veya sahte demo verisi **bulunmaz**.
- **İşaretli ve sıfır değerler:** Ham ADC çıktısının `0` veya negatif (ör. `-1`, `-125430`) olması tek başına arıza değildir; Wheatstone köprüsünün başlangıç ofsetine ve yükleme yönüne göre negatif veya pozitif tam sayılar normaldir.

---

## 2. Donanım Güvenliği ve Kablolama Uyarıları

1. **Model Doğrulanmadan 5 V Uygulamayın:**
   - ESP32 GPIO pinleri **5 V toleranslı değildir** (3,3 V mantık seviyesiyle çalışır).
   - Bilinmeyen bir HX711 modülünü doğrudan uyumlu varsaymayın; eldeki kartın veri sayfası ve devre şeması (`VCC` ile dijital lojik beslemesinin ayrık olup olmadığı) doğrulanmadan **kesinlikle 5 V vermeyin**.
   - Doğrulanmamış kartlar için ezbere gerilim bölücü devre reçetelerine güvenmeyin; yalnızca 3,3 V besleme ve 3,3 V `DOUT` lojik düzeyi doğrulanan modülü ESP32'nin `3V3` hattıyla kullanın.
2. **Yük Hücresi Kablo Renkleri Evrensel Değildir:**
   - Piyasadaki 4 telli yük hücrelerinde sık görülen renk eşleşmesi (`Kırmızı: E+`, `Siyah: E−`, `Yeşil: A+`, `Beyaz: A−`) **evrensel bir standart değildir**.
   - Enerji vermeden önce satın alınan yük hücresinin kendi teknik dokümanındaki `E+ / E− / A+ (S+) / A− (S−)` uçlarını doğrulayın.

---

## 3. Planlanan Pin Bağlantıları

Aşağıdaki pinler **ESP32-WROOM (30 pin DevKit V1)** sınıfı kart için **plan niteliğindedir**:

| HX711 Pini | ESP32 Pini (Planlanan) | Kod Sabiti (`constexpr`) | Açıklama |
|---|---|---|---|
| `VCC` | `3V3` | — | Yalnızca 3,3 V uyumu doğrulanan modülde |
| `GND` | `GND` | — | Ortak toprak |
| `DT` / `DOUT` | `GPIO16` | `PIN_HX711_DOUT = 16` | Veri çıkışı (yalnız WROOM; WROVER kartlarda PSRAM kullanır) |
| `SCK` / `PD_SCK` | `GPIO4` | `PIN_HX711_SCK = 4` | Saat girişi |

Pin numaraları `hx711_raw_test.ino` dosyasının en üstündeki `constexpr` sabitlerinden kolayca değiştirilebilir.

---

## 4. Gerekli Kütüphane ve İncelenen Sürüm

- **Kütüphane:** [`bogde/HX711`](https://github.com/bogde/HX711) (*HX711 Arduino Library* — Bogdan Necula)
- **İncelenen ve Doğrulanan Sürüm:** `0.7.5`
- **Neden bu kütüphane korundu:**
  - `ARDUINO_ARCH_ESP32` altında 24-bit okuma ve kazanç darbeleri sırasında FreeRTOS görev/kesme gecikmelerinin `PD_SCK` hattını 60 µs'den uzun süre `HIGH` bırakıp HX711'i istem dışı uyku (power-down) moduna sokmasını önleyen `portENTER_CRITICAL(&mux)` / `portEXIT_CRITICAL(&mux)` korumasına sahiptir.
  - Hızlı işlemcilerde saat darbe genişliğini koruyan `shiftInSlow()` (`delayMicroseconds(1)`) desteğini içerir. Bu korumalar devre dışı bırakılmamıştır.

---

## 5. Zaman Aşımı (Timeout) Sınırı ve Teknik Ödünleşim

`bogde/HX711` v0.7.5 kaynak kodu (`src/HX711.cpp`) incelendiğinde:

1. `HX711::read()` fonksiyonu ilk satırında koşulsuz olarak bloklayıcı `wait_ready()` (`while (!is_ready()) { delay(0); }`) çağırır.
2. `HX711::wait_ready_timeout()` yalnızca çağrıldığı anda `DOUT == LOW` olana kadar süre sınırı uygular; peşinden çağrılan `read()` veya `read_average()` için **uçtan uca bloklamama garantisi vermez**. Özellikle `read_average(N)` ilk örnekten sonra kablo koparsa içeride sonsuz döngüde kilitlenir.
3. **Uygulanan En Küçük Güvenli Yaklaşım:**
   - `read_average()` hiç kullanılmaz.
   - Her `loop()` adımında önce bloklamayan `scale.is_ready()` kontrol edilir; hazır değilse `scale.read()` **kesinlikle çağrılmaz**.
   - Sensör `READY_TIMEOUT_MS` (`1000 ms`) boyunca hazır olmazsa seri portu boğmadan `ERROR_REPORT_INTERVAL_MS` (`2000 ms`) aralıklarla periyodik uyarı mesajı üretilir.
   - Sensör yeniden veri üretmeye başladığında otomatik toparlanma mesajı basılır ve ham örnek okumaya devam edilir. Zaman hesapları 32-bit `unsigned long` çıkarma aritmetiğiyle `millis()` taşmasına (wrap-around) dayanıklıdır.
4. **Bilinen Sınır (Yarış Penceresi):**
   - `scale.is_ready()` kontrolünün `true` dönmesi ile hemen sonraki `scale.read()` içindeki `wait_ready()` satırı arasındaki birkaç mikrosaniyelik pencerede fiziksel `DOUT` hattı kopar veya `HIGH` durumuna geçerse, kütüphanenin iç `wait_ready()` döngüsü yine bloklayabilir. Resmi kütüphanenin ESP32 kritik bölge ve zamanlama korumasını bozmamak için sıfırdan sürücü yazılmamış, bu dar donanım yarış penceresi bilinçli bir sınır olarak bırakılmıştır.

---

## 6. Arduino IDE ile Açma, Derleme ve Kullanım

1. Arduino IDE'yi açın ve **Library Manager** üzerinden `HX711 Arduino Library` (*Bogdan Necula*, sürüm `0.7.5`) kütüphanesini yükleyin.
2. `examples/hx711_raw_test/hx711_raw_test.ino` dosyasını açın.
3. **Tools → Board** menüsünden kullandığınız **ESP32 Dev Module** (ESP32-WROOM-32) kartını seçin ve kodu derleyin.
4. Donanım güvenlik kontrolleri tamamlandıktan sonra kodu yükleyin ve **Serial Monitor** penceresini **`115200` baud** hızında açın.

### Donanım Geldiğinde Adım Adım Doğrulama (Yükle – Değişimi Gözle – Yükü Kaldır)

1. **Boşta Gözlem:** Terazi yüksüzken seri ekranda `HAM_ADC: <değer>` satırlarının düzenli aktığını ve değerlerin dar bir gürültü aralığında kaldığını gözlemleyin.
2. **Yükle ve Değişimi Gözle:** Yük hücresine kapasite sınırları içinde bilinen/güvenli bir kuvvet veya kütle uygulayın; ham ADC sayısının belirgin biçimde (bağlantı yönüne göre artarak veya azalarak) değiştiğini doğrulayın.
3. **Yükü Kaldır:** Yükü kaldırın ve ham ADC değerinin başlangıçtaki yüksüz banda geri döndüğünü gözlemleyin.
4. **Hata ve Toparlanma Kontrolü:** HX711 hazır olmadığında seri ekranda taşma yapmadan 2 saniyede bir anlaşılır zaman aşımı uyarısı verildiğini, bağlantı düzeldiğinde ise otomatik toparlanma mesajıyla okumaların sürdüğünü doğrulayın.

---

## 7. Test ve Doğrulama Komutları

Komutları depo kökünden çalıştırın. Yerel derleme çıktıları `.pio/` altında kalır:

### Host Davranış Testi (MinGW `g++`)

```powershell
New-Item -ItemType Directory -Force -Path .pio | Out-Null
g++ -std=c++11 -Wall -Wextra -I examples/hx711_raw_test/tests/mocks examples/hx711_raw_test/tests/test_hx711_raw.cpp -o .pio/test_hx711_raw.exe
& .pio/test_hx711_raw.exe
```

### ESP32 Hedef Derleme Doğrulaması (PlatformIO — Scratch Üzerinde)

```powershell
pio run -e hx711_raw
```
