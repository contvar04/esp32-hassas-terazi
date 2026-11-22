# ESP32 ile Wheatstone Köprülü Hassas Terazi

> **Proje dosyası — sürüm 1.0 · 8 Kasım 2026**  
> **Ders:** Ölçme ve Enstrümantasyon  
> **Proje kapsamı:** 0–5 kg yük hücreli tartı, HX711 24-bit ADC, ESP32, 16×2 LCD, Wi-Fi ile Huawei MatePad üzerinde canlı değer ve grafik.  
> **Yazılım dili:** C++ (Arduino framework / Arduino IDE).  
> **Durum:** Tasarım ve bağlantı planı hazır; henüz gerçek donanımla kurulup kalibre edilmedi.

---

## 1. Projenin amacı

Wheatstone köprülü bir **5 kg yük hücresi (load cell)** kullanarak uygulanan kütleyi ölçmek, köprüden gelen küçük **analog diferansiyel gerilimi** HX711 ile sayısallaştırmak, ESP32'de C++ ile kütleyi hesaplamak ve sonucu:

1. Terazi üstündeki **16×2 I²C LCD'de**, ve
2. **Wi-Fi üzerinden tablete** açılan yerel web sayfasında, canlı sayısal değer ve grafik olarak göstermek.

Sistem tartım, dara (tare), kalibrasyon ve ölçüm deneyi işlevlerini içerecek. İnternet bağlantısı zorunlu olmayacak: ESP32 kendi Wi-Fi ağını açacak.

### Başarı ölçütleri

- 0–5 kg aralığında uygun şekilde monte edilmiş sensörden veri okunması.
- Boş tartının sıfırlanması; bilinen kütleyle kalibrasyon.
- Aynı ölçümün hem LCD hem tablette gösterilmesi.
- Ağırlık değişince tablet grafiğinin güncellenmesi.
- Referans kütlelerle mutlak hata, yüzde hata, tekrarlanabilirlik, doğrusallık ve histerezisin incelenmesi.

**Uyarı:** “24-bit ADC” gerçek tartının 24-bit **etkin doğrulukta** olduğu veya kesin olarak 0,1 g / 1 g hassas olduğu anlamına gelmez. Gerçek doğruluk ve okunabilirlik kurulan sistem üzerinde ölçülmelidir.

---

## 2. Temel çalışma prensibi

```text
       Ölçülecek cisim (0–5 kg)
                    │ kuvvet
                    ▼
    5 kg LOAD CELL (metal kiriş)
    └─ Strain gauge dirençleri
       └─ Wheatstone köprüsü
                    │ çok küçük analog gerilim (mV)
                    ▼
               HX711 MODÜLÜ
       ┌─────────────────────────┐
       │ Diferansiyel yükselteç  │
       │ 24-bit ADC              │
       └─────────────────────────┘
                    │ DOUT + SCK, dijital veri
                    ▼
                ESP32 (C++)
       ┌─────────────────────────┐
       │ Okuma / ortalama        │
       │ Dara / kalibrasyon      │
       │ Gram / kg / Newton      │
       └─────────────────────────┘
                │           │
             I²C│           │Wi-Fi (kablosuz)
                ▼           ▼
           16×2 LCD     HUAWEI TABLET
           tartı üstü   tarayıcıda canlı
                       değer + grafik
```

**Kritik ayrım:** Tablet ölçüm yapmaz; ESP32'nin hesapladığı sonucu gösterir. HX711 ile ESP32 **kablolu**, ESP32 ile tablet **kablosuz** haberleşir.

### Wheatstone köprüsü nerede?

4 kablolu tam köprülü bir 5 kg yük hücresinde köprü, metal kiriş üzerine yapıştırılmış gerinim ölçerlerin (**strain gauge**) elektriksel bağlantısıdır. Genellikle kaplama altında olduğundan dört direnç çıplak gözle görünmez; **ayrı bir Wheatstone kartı** satın alınmaz.

Yaygın tam köprü bağlantısı:

```text
                E+ (köprü besleme +)
                        ●
                      /   \
                    R1     R2
                    /       \
      A− (sinyal) ●           ● A+ (sinyal)
                    \       /
                    R3     R4
                      \   /
                        ●
                E− (köprü besleme −)
```

- **E+ ve E−:** Köprüyü besleme uçları.
- **A+ ve A−:** Köprünün diferansiyel çıkışları; HX711'in kanal A girişine gider.
- Yük uygulanınca strain gauge dirençleri çok az değişir ve A+ ile A− arasında küçük bir gerilim oluşur.

İlgili sensörün **tam köprülü, 4 kablolu** olduğunun satıcı/veri sayfasından doğrulanması gerekir. Her “load cell” aynı bağlantıda değildir; üç telli yarım köprü sensörler de satılır.

### HX711'in görevi

- Wheatstone köprüsünün milivolt mertebesindeki sinyalini diferansiyel olarak alır.
- İçindeki programlanabilir kazançlı yükselteç (PGA) ile ölçüme uygun hale getirir.
- 24-bit ADC ile sayısal ölçüm üretir.
- ESP32'ye `DOUT/DT` ve `PD_SCK/SCK` üzerinden **HX711'e özgü senkron seri protokolle** veri aktarır; bu bağlantı UART, I²C veya SPI olarak adlandırılmaz.
- Tipik kullanımda 10 SPS veya 80 SPS örnekleme seçenekleri vardır; başlangıçta 10 SPS kullanmak uygundur. Seçim HX711 modülünün RATE düzenine bağlıdır.

ESP32'nin dahili ADC'sini kullanmak **gerekmiyor**. Ayrıca AD620 yükselteç modülü de bu tasarımda **gerekmiyor**.

---

## 3. Kesinleştirilen malzemeler (BOM)

| Parça | Miktar | Seçim / önemli koşul | Gerekli mi? |
|---|---:|---|---|
| **ESP32 DevKit V1**, tercihen 30 pin / ESP32-WROOM-32 | 1 | Wi-Fi ve yeterli GPIO; USB veri kablosu destekli | **Evet** |
| **5 kg, 4 kablolu tam köprülü load cell** | 1 | Tek noktalı (single-point) kiriş tipi; teknik çizim kontrol edilecek | **Evet** |
| **HX711 load cell ADC/yükselteç modülü** | 1 | **3,3 V ile kullanım ve ESP32 GPIO düzeyleri doğrulanacak** | **Evet** |
| **16×2 LCD + I²C (PCF8574) adaptörü** | 1 | Yaygın adres `0x27`, bazen `0x3F` | **Evet** |
| **I²C çift yönlü seviye dönüştürücü** | 1 | LCD'nin SDA/SCL hattında 5 V pull-up varsa 3,3 V ESP32'yi korumak için | **Koşula bağlı; alışveriş listesinde tutuldu** |
| Anlık buton (dara) | 1 | GPIO ile GND arasında, `INPUT_PULLUP` | **Evet** |
| Breadboard | 1 | İlk prototipte | **Evet** |
| Jumper kablolar | 1 set | Dişi–dişi; gerektiği yerde erkek–erkek/erkek–dişi | **Evet** |
| ESP32 USB **veri** kablosu | 1 | Kartın USB soket tipine göre | **Evet** |
| Tartım platformu (üst tabla) | 1 | Hafif, rijit; sensör ucuna bağlanabilir | **Evet** |
| Sabit alt tabla | 1 | Yük hücresinin sabit ucuna bağlantı | **Evet** |
| Vida/somun/standoff/ara parçalar | Yeterli | Sensörün **delik ölçüsüne göre** alınacak | **Evet** |
| Bilinen kütlede referans ağırlık(lar) | 1 veya birkaç | İlk kalibrasyon için örneğin **1000 g** | **Evet** |
| Huawei MatePad tablet | 1 | Mevcut tablet; Chrome/başka modern tarayıcı | Zaten var |

**Ayrıca alınmayacaklar:** Ayrı ADC, AD620, ikinci bir Wi-Fi/Bluetooth modülü, Arduino Uno, ayrı Wheatstone köprüsü deney seti. ESP32'de Wi-Fi dahili; tablet için ayrı uygulama şart değil.

**Ürün siparişinden önce satıcıda bakılacaklar:** Yük hücresinin *5 kg / 4 kablo / tam köprü* bilgisi; HX711 kartının 3,3 V besleme davranışı; LCD üzerindeki I²C kartı; sensörün mekanik boyut ve montaj delikleri; sete hangi kabloların/vidaların dahil olduğu.

**Örnek Türkiye satış sayfası (fiyat ve stok güncelliği garanti değil):**

- 5 kg + HX711 birlikte: <https://robotikdepo.com/5kg-yuk-hucresi-hx711-modul-agirlik-takimi-tarti-sensoru>
- 5 kg load cell tek başına: <https://www.robo90.com/5-kg-agirlik-sensoru-loadcell>
- HX711 modül: <https://teknobilsem.com/urun/agirlik-sensor-kuvvetlendirici-load-cell-amplifier-hx711/>

> **Alışverişte kritik tercih:** Aynı tip sensör ve HX711'i içeren bir set tercih edilebilir. Set fiyatı uygun olsa bile **ESP32, LCD, seviye dönüştürücü, mekanik platform** genellikle ayrıca gerekir.

---

## 4. Donanım bağlantıları (ESP32 DevKit V1)

### 4.1. 5 kg load cell → HX711

| 4 kablolu yük hücresindeki **yaygın** renk | HX711 terminali | Açıklama |
|---|---|---|
| Kırmızı | `E+` | Köprü besleme + |
| Siyah | `E−` | Köprü besleme − |
| Yeşil | `A+` | Diferansiyel sinyal + |
| Beyaz | `A−` | Diferansiyel sinyal − |

**Bu kablo renkleri evrensel standart değildir.** Satın alınan load cell'in ürün sayfası/datasheet üzerindeki **E+/E−/S+/S−** eşleştirmesi esas alınacak. Birçok yük hücresinde kablo uçları çıplaktır; jumper değil, HX711'in lehim terminaline, uygun klemense veya konnektöre güvenli şekilde takılır.

### 4.2. HX711 → ESP32

| HX711 | ESP32 DevKit V1 |
|---|---|
| `VCC` | `3V3` **(yalnızca 3,3 V uyumlu HX711 kartında)** |
| `GND` | `GND` |
| `DT`, `DOUT` | `GPIO16` |
| `SCK`, `CLK` | `GPIO4` |

Bazı HX711 modüllerinde `VCC`, `VDD`, `VIO` gibi farklı besleme terminalleri bulunabilir. **Kart modeline özel bağlantı belgesi kontrol edilmeden 5 V uygulanmaz.** ESP32 GPIO'ları 5 V toleranslı değildir. HX711 DOUT seviyesi ESP32'ye uygun olmalı; yük hücresi köprüsü de HX711 tarafından uygun şekilde beslenmeli.

### 4.3. 16×2 I²C LCD → ESP32

| LCD adaptörü / dönüştürücü | ESP32 |
|---|---|
| `SDA` (dönüştürücünün **LV** tarafı) | `GPIO21` |
| `SCL` (dönüştürücünün **LV** tarafı) | `GPIO22` |
| Seviye dönüştürücü `LV` | `3V3` |
| Seviye dönüştürücü `HV` | `5V` (ESP32 geliştirme kartının USB beslemesindeki 5 V hattı; **kart pin adını kontrol et**) |
| LCD adaptörü `SDA` / `SCL` | Dönüştürücünün **HV** tarafındaki karşılıkları |
| LCD adaptörü `VCC` | `5V` **(5 V LCD için)** |
| LCD adaptörü `GND` | `GND` |

- Dönüştürücü kullanılırsa **tüm devrenin GND'si ortak** olacak.
- 5 V ile beslenen PCF8574 I²C LCD adaptörlerinin SDA/SCL pull-up dirençleri çoğu zaman 5 V'a çıkar; **ESP32'ye doğrudan bağlamak güvenli olmayabilir**. 3,3 V ile güvenilir çalışan LCD/arayüz seçilirse dönüştürücü ihtiyacı yeniden değerlendirilir.
- LCD adresi `0x27` varsayıldı; çalışmazsa I²C tarama ile gerçek adresi kontrol edilir.
- LCD kontrastı adaptördeki küçük potansiyometreden ayarlanabilir.

### 4.4. Dara butonu → ESP32

| Buton ucu | ESP32 |
|---|---|
| 1. uç | `GPIO27` |
| 2. uç | `GND` |

Kodda `pinMode(27, INPUT_PULLUP)` kullanılacak. Basılı durumda pin **LOW** okunur. Haricî direnç şart değil; kontak sıçraması (debounce) yazılımla ele alınır.

### 4.5. Bağlantı özeti

```text
Yük hücresi (4 kablo)           HX711             ESP32
  E+ -------------------------- E+
  E− -------------------------- E−
  A+ -------------------------- A+
  A− -------------------------- A−
                                 VCC ------------ 3V3 (uyum kontrolü)
                                 GND ------------ GND
                                 DT  ------------ GPIO16
                                 SCK ------------ GPIO4

ESP32 GPIO21/22 ---- 3,3 V ⇄ 5 V I²C seviye dönüştürücü ---- LCD SDA/SCL
ESP32 5 V ------------------------------------------------- LCD VCC (5 V model)
ESP32 GND ----------------------------------------------- LCD GND
ESP32 GPIO27 ---- buton ---- ESP32 GND

ESP32 ~~~ Wi-Fi SoftAP ~~~ Huawei MatePad / Web tarayıcısı
```

**Güç:** İlk aşamada ESP32'yi USB kablosuyla bilgisayardan/uygun USB güç kaynağından besleyeceğiz. Motor, röle veya yüksek akım yükü yok. USB güç kaynağı ve kablo kalitesi ölçüm kararlılığını etkileyebilir.

---

## 5. Mekanik tasarım: 5 kg sensör nasıl monte edilir?

```text
         Tartılacak nesne
                ↓
      ┌────────────────────┐
      │  ÜST TARTIM TABLASI│
      └─────────┬──────────┘
                │ yalnızca hareketli uca bağlanır
    [Sabit uç]──[ LOAD CELL KİRİŞİ ]──[Hareketli uç]
         │                         ↑ esneme ölçülür
         ▼
      ALT TABLA / ŞASE
```

Bu şema **mekanik prensibi** anlatır; gerçek vida yerleri sensör modelinin üretici montaj çizimiyle belirlenir.

1. Yük hücresinin **sabit ucunu** rijit alt tablaya vidala.
2. Üst tartım platformunu **diğer, hareketli uca** uygun ara parça ve vidalarla bağla.
3. Üst tabla, gövdenin başka hiçbir yerine değip yükü sensörden **bypass etmemeli**.
4. Yük hücresinin iki ucunu aynı sabit tabana sıkıştırma; doğru esneyemez.
5. Yük uygulandığında bükülme ve aşırı eksen dışı yükleri sınırla. Kütleyi mümkünse platform merkezine yerleştir.
6. Sensör kablosu kirişin hareketini engellemeyecek ve gerilmeyecek şekilde sabitlensin.
7. Tasarım kapasitesinin üzerindeki yüklerden, darbelerden ve özellikle vida sıkarken sensörü zorlamaktan kaçın.
8. **Platformun kendi ağırlığı da sensöre yük bindirir.** Dara bunu ekranda sıfırlasa da sensörün fiziksel kapasitesini artırmaz.

Yük hücresi hazır alınacak; strain gauge'leri kendimiz yapıştırıp bir Wheatstone köprüsü üretmek **mümkün ama bu proje için gerekli değil**. Böyle bir yapım, uygun strain gauge, yapıştırıcı, sıcaklık dengelemesi ve karakterizasyon gerektirir; hata riski yükselir.

---

## 6. Yazılım mimarisi

### Karar verilen teknolojiler

- **ESP32 firmware dili:** C++
- **Geliştirme ortamı:** Arduino IDE + Espressif Arduino-ESP32 board paketi
- **HX711 sürücüsü:** [bogde/HX711](https://github.com/bogde/HX711)
- **LCD:** Uyumlu `LiquidCrystal_I2C` kütüphanesi (kütüphane varyantına göre yapıcı/fonksiyon imzası doğrulanacak)
- **Wi-Fi:** `WiFi.h`
- **Web sunucusu:** `WebServer.h`
- **Kalıcı ayarlar:** `Preferences.h` (ESP32 NVS)
- **Tablet arayüzü:** ESP32'den sunulan HTML + CSS + JavaScript; uygulama kurulumu gerektirmez.

### ESP32'nin görevleri

1. HX711 hazır mı kontrol etmek.
2. Ham ADC okumalarını almak.
3. Boş tartı ofsetini (dara) çıkarmak.
4. Kalibrasyon katsayısıyla kütleyi hesaplamak.
5. İsteğe göre filtrelenmiş/değişmeyen değerleri LCD'de göstermek.
6. `HassasTerazi` adlı yerel Wi-Fi ağı oluşturmak.
7. Tablete web sayfası ve `JSON` ölçüm verisi sunmak.
8. Fiziksel butondan ve web arayüzünden sıfırlama isteklerini almak.
9. Kalibrasyon katsayısını ESP32'nin kalıcı NVS belleğinde saklamak.

### Haberleşme ayrımı

| Haberleşme | Hat/protokol | Örnek kod |
|---|---|---|
| ESP32 ↔ HX711 | DOUT/SCK, HX711 seri protokolü | `scale.begin(16, 4)` |
| ESP32 ↔ LCD | I²C | `Wire.begin(21, 22)` |
| ESP32 ↔ bilgisayar | USB üzerinden seri arayüz | `Serial.begin(115200)` |
| ESP32 ↔ tablet | Wi-Fi 2,4 GHz + HTTP | `WiFi.softAP(...)` |

`Serial.begin(115200)` **HX711'i veya Wi-Fi'yi başlatmaz**; bilgisayardaki Serial Monitor'e hata ve ölçüm mesajı yazdırmak içindir.

---

## 7. HX711 okuma: en küçük C++ denemesi

Önce LCD ve Wi-Fi eklemeden yalnızca **ESP32 + HX711 + yük hücresi** çalıştırılır.

Arduino IDE → **Library Manager** → `HX711` (Bogdan Necula) kütüphanesini yükle:

```cpp
#include "HX711.h"

constexpr int HX_DOUT = 16;
constexpr int HX_SCK  = 4;

HX711 scale;

void setup() {
  Serial.begin(115200);
  scale.begin(HX_DOUT, HX_SCK);
  Serial.println("HX711 ham okuma basliyor");
}

void loop() {
  if (scale.wait_ready_timeout(1000)) {
    long raw = scale.read_average(5);
    Serial.print("ADC: ");
    Serial.println(raw);
  } else {
    Serial.println("HX711 hazir degil, baglantiyi kontrol et");
  }
  delay(200);
}
```

**Beklenen gözlem:** Boş tartıda ham değerler yaklaşık sabit kalır; yük koyunca değer değişir. Pozitif veya negatif yönde değişebilir. Önce bu farkı doğrulamadan gram hesabına geçilmez.

---

## 8. Kalibrasyon, dara ve hesaplamalar

### 8.1. Matematiksel model

Ham ADC değeri `R`, boş tartı ofseti `R0`, kalibrasyon katsayısı `K` ise:

\[
 m_{gram} = \frac{R-R_0}{K}
\]

Burada `K`, **ADC sayımı / gram** birimindedir. İşareti negatif olabilir; kalibrasyonda bunu **korumak gerekir**.

### 8.2. Örnek (gerçek ölçüm değil)

- Boş tartı ham değer: `R0 = 800000`
- Üzerinde tam `1000 g` varken: `R = 1200000`
- `K = (1200000 - 800000) / 1000 = 400 sayım/g`
- Sonradan `R = 1000000` okunursa: `m = (1000000 - 800000) / 400 = 500 g`

### 8.3. Kalibrasyon uygulaması

1. Mekanik montaj tamamlandıktan sonra tartıyı **boş** çalıştır.
2. `tare(15)` gibi bir komutla boş ofseti kaydet.
3. Teraziye **bilinen kütlede**, tercihen gerçek 1000 g referans ağırlığı koy.
4. Ofset çıkarılmış ham farkı örnekle (`get_value(15)`).
5. `K = ham_fark / 1000.0` hesapla (1 kg referans kullanılıyorsa).
6. `set_scale(K)` uygula.
7. `Preferences` ile `K` değerini kalıcı NVS hafızasına kaydet.
8. Ağırlığı kaldır ve sıfıra döndüğünü kontrol et. 500 g, 2 kg ve 4 kg gibi **farklı** referanslarla doğrula.

**Önemli:** Bir kez 1 kg ile kalibre etmek, 0–5 kg boyunca otomatik olarak doğru olduğunun kanıtı değildir. Tam aralıkta doğrulama gerekir. Kalibre edilen referans ağırlığı belirsizse sonuç da belirsizdir.

### 8.4. Kütle / kuvvet gösterimi

- `m_kg = m_g / 1000`
- Yerçekimi kaynaklı kuvvet: `F_N = m_kg × 9.80665` (standart yerçekimi yaklaşımı; yerel `g` kullanılırsa değişebilir).
- Ölçüm aslında **yük hücresine uygulanan kuvvete** dayanır; terazi bunu kalibrasyonla kütle biçiminde raporlar.

---

## 9. Tabletle Wi-Fi: ağ ve arayüz

### Seçilen yöntem: ESP32 SoftAP + yerel HTTP

```text
ESP32
  Wi-Fi ağı: HassasTerazi
  Şifre: kullanıcı tarafından belirlenecek, en az 8 karakter
  Tipik AP adresi: 192.168.4.1
                )))
          Huawei MatePad
  Wi-Fi'de HassasTerazi ağına bağlan
  Tarayıcı: http://192.168.4.1
```

- **İnternet gerekmez.** Tablet “Bu ağda internet yok” uyarısı verebilir; bağlantıyı korumalı.
- ESP32 normal 2,4 GHz Wi-Fi kullanır; tablette desteklenir.
- Kodu yüklediğimiz ESP32 kendi ağını ve web sayfasını sunar; Play Store/AppGallery uygulaması gerekmez.
- LCD bağlı kalmaya devam eder; tabletin bağlantısı kopsa bile fiziksel tartı ölçmeye devam edebilir.

### Web arayüzünde planlanan alanlar

- **Anlık değer:** Gram / kilogram / Newton seçimi.
- **Canlı grafik:** Zamana göre ağırlık (ör. her 250–500 ms güncelleme).
- **Dara butonu:** Teraziyi sıfırla.
- **Kalibrasyon ekranı:** Bilinen ağırlığı girip katsayıyı ayarla.
- **Bağlantı durumu:** Son veri zamanı, ESP32 erişilebilir mi.
- **Opsiyonel:** CSV/Excel'e veri aktarma ve deneme notları.

### Web yazılımı için önerilen küçük arayüz sözleşmesi

```text
GET  /                  -> HTML/CSS/JS tablet sayfası
GET  /api/weight        -> {"grams": 1250.2, "calibrated": true}
POST /api/tare          -> teraziyi sıfırla
POST /api/calibrate     -> known_g=1000, kalibrasyon kaydet
```

Örnek `1250.2` verisi **temsili**. Gerçekte JSON, HX711'den ölçülen kütleyi taşır. İlk sürümde basit HTTP polling yeterlidir; WebSocket **zorunlu değildir**. Kod geliştirilirken HTTP istekleri HX711 okumasını gereksiz bloke etmeyecek şekilde ele alınmalıdır.

### Basit ESP32 Wi-Fi C++ örneği (yalnızca bağlantı denemesi)

```cpp
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "HassasTerazi";
const char* pass = "DegistirBunu123";  // Ornek, gercek projede degistir
WebServer server(80);

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pass);
  server.on("/", []() {
    server.send(200, "text/html; charset=utf-8",
                "<h1>Hassas Terazi</h1><p>ESP32 baglandi.</p>");
  });
  server.begin();
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
}
```

**Bu Wi-Fi denemesi gerçek tartım koduyla birleştirilmemiştir.** Son yazılımda LCD, HX711, dara, kalibrasyon ve web sunucusu **tek C++ ESP32 firmware'i** içinde olacak.

---

## 10. Bütünleşik firmware için uygulama taslağı

İlk sürümün ana akışı:

```text
SETUP
 ├─ Serial / I²C / LCD / buton başlat
 ├─ HX711 başlat, veri geliyor mu kontrol et
 ├─ Kayıtlı kalibrasyon katsayısını NVS'den al
 ├─ Tartı BOŞKEN dara al
 ├─ Wi-Fi SoftAP aç ve HTTP rotalarını tanımla
 └─ "Terazi hazır" durumuna geç

LOOP
 ├─ HX711 yeni örnek hazırsa oku
 ├─ Offset + kalibrasyon ile gram hesabı yap
 ├─ Hafif bir filtreyle gösterim değerini güncelle
 ├─ LCD'yi belirli aralıkta yenile
 ├─ Web isteklerini işle; yeni değeri JSON olarak sun
 ├─ Fiziksel dara butonu / web dara isteğini işle
 └─ İstenirse kalibrasyon yap ve NVS'ye kaydet
```

**Yazılım ayrıntıları:**

- **HX711 ölçüm hızı:** İlk denemede 10 SPS.
- **LCD güncelleme:** Yaklaşık 250–500 ms.
- **Tablet güncelleme:** Yaklaşık 250–500 ms; ağ yüküne göre ayarlanabilir.
- **Filtre:** Örneğin 3–5 örneklik ortalama veya üstel yumuşatma; kalibrasyon ve ham deney verileri filtrelenmiş görüntü değerlerinden ayrı tutulmalı.
- **Hata yönetimi:** HX711 zaman aşımı, kayıp bağlantı, geçersiz kalibrasyon katsayısı, Wi-Fi bağlantı durumu.
- **NVS:** Son kullanılan kalibrasyon katsayısını korur; buna rağmen cihaz açılırken **boşken yeniden dara** gerekebilir.
- **LCD:** 16 karakter sınırına uygun kısa metinler (`"Agirlik: 1250 g"`, `"WiFi: Hazir"`).
- **Tablet:** Yalnızca LCD değerini kopyalamaz; canlı grafik ve ölçüm geçmişi de gösterebilir.

> **Not:** Bu doküman yazılım mimarisini, örnek kodları ve beklenen davranışı tarif eder. Alınacak kartların tam modelleri belli olmadan, test edilmemiş bir “her donanımda kesin çalışır” tümleşik firmware iddiasında bulunulmuyor. `LiquidCrystal_I2C` kütüphane varyantı, gerçek GPIO etiketleri, ekran beslemesi ve kalibrasyon uygulaması nihai firmware'den önce doğrulanacak.

---

## 11. Ölçüm deneyleri ve rapor

### Önerilen deney tablosu

| Referans kütle (g) | Ölçülen (g) | Mutlak hata (g) | Yüzde hata (%) |
|---:|---:|---:|---:|
| 0 | — | — | — |
| 100 | — | — | — |
| 500 | — | — | — |
| 1000 | — | — | — |
| 2000 | — | — | — |
| 4000 | — | — | — |
| 5000* | — | — | — |

\* **5 kg'ı test ederken** platformun kendi ağırlığı dahil toplam fiziksel yükün load cell sınırını aşmadığından emin olunmalı. Cihazı aşırı yüklemek gerekmez; erişilebilir güvenilir referanslarla test yapılabilir.

### Hesaplamalar

- **Mutlak hata:** `|ölçülen − referans|`.
- **Yüzde bağıl hata:** `100 × |ölçülen − referans| / referans`; **referans 0 g iken tanımsızdır**.
- **Tekrarlanabilirlik:** Aynı ağırlığı birkaç kez kaldırıp yeniden koyarak okuma dağılımını / standart sapmayı hesapla.
- **Histerezis:** Artan ve azalan yük dizilerinde **aynı kütledeki** okumaları karşılaştır.
- **Doğrusallık:** Farklı referans kütlelerde ölçülen–referans grafiğini ve sapmaları incele.
- **Sıfır kayması:** Boşta 5–10 dakika boyunca ölçüm yap, zamanla değişimi göster.
- **Kenar/merkez yük farkı:** Platformun farklı noktalarında aynı kütleyle kısa bir karşılaştırma yap (mekanik izin veriyorsa).

**Raporda:** Wheatstone köprüsü şeması, HX711 blok şeması, bağlantılar, yük hücresi montajı, kalibrasyon denklemi, deney tablosu, ölçüm grafiği, kaynak kodu ve fotoğraflar yer almalı.

---

## 12. Kurulum sırası ve en az doğrulama

Bu sıra gereksiz hata ayıklamayı önlemek içindir; her aşamada yalnız **değişen davranışı gösteren** kısa bir kontrol yeterli.

| Aşama | İş | Doğrudan kanıt |
|---|---|---|
| 1 | 5 kg load cell + HX711 + ESP32 bağlantısı | Seri ekranda yük değişince ham ADC değeri değişiyor |
| 2 | Mekanik montaj, dara, 1 kg kalibrasyon | Boşta yaklaşık 0 g ve 1 kg'da yaklaşık 1000 g |
| 3 | LCD ve I²C bağlantısı | LCD, aynı ağırlığı gösteriyor |
| 4 | Wi-Fi erişim noktası ve tablet web sayfası | Tablet `http://192.168.4.1` adresini açıyor |
| 5 | Web ekranını gerçek ölçüme bağla | Yük değişince LCD ve tablet birlikte değişiyor |
| 6 | Referans kütlelerle deney | Hata tablosu ve ölçüm grafiği oluşuyor |

İlk kontrolden sonra sırf emin olmak için sürekli bütün testleri tekrar etmeye gerek yok. Bağlantı/hardware güvenliği ve son ölçüm doğrulaması ayrı önem taşır.

---

## 13. Sık sorulan sorular

**Wheatstone köprüsünü ayrıca mı satın alıyoruz?**  
Hayır. Seçilen 4 kablolu tam köprülü yük hücresinde hazır. Ayrı kurmak istenirse strain gauge'lerle köprü oluşturmak mümkün, fakat ayrı bir üretim ve kalibrasyon çalışması olur.

**ESP32'ye jumper kablo takılır mı?**  
Evet. DevKit'in pin başlıklarına uygun dişi jumper takılabilir; gerekirse breadboard kullanılır. Çıplak load cell uçları için sağlam bağlantı/lehim/klemens gerekir.

**HX711 doğrudan ESP32'nin ADC'si mi?**  
Hayır; **haricî 24-bit ADC modülüdür**. ESP32 hazır dijital veriyi okur.

**`Serial.begin(115200)` hangi haberleşme?**  
Bilgisayarla USB üzerinden seri terminal/log haberleşmesi. HX711'in `DOUT/SCK` protokolü veya LCD'nin I²C protokolü değildir.

**Tablet için uygulama gerekli mi?**  
Hayır. ESP32 web sayfasını sunar; tablette tarayıcı yeterli.

**İnternet gerekli mi?**  
Hayır. ESP32'nin açtığı Wi-Fi ağına tablet bağlanır.

**Arduino Uno ile malzemeler tamamen farklı mı?**  
Hayır. Load cell ve HX711 aynı sınıf parçalar. Uno'da 5 V mantık seviyeleri ve dahili Wi-Fi yok; ESP32'de 3,3 V GPIO ve dahili Wi-Fi var. LCD seviye dönüştürücü ihtiyacı buna bağlı.

**C mi C++ mı?**  
Seçim **C++**. Arduino IDE'de HX711, LCD ve ESP32 Wi-Fi kütüphaneleriyle daha hızlı bütünleştirme sağlar. C ile de yapılabilir, ama o yaklaşım için genellikle ESP-IDF sürücülerini daha doğrudan yönetmek gerekir.

**1 kg ile kalibre edip 5 kg tartabilir miyiz?**  
Evet, ilk kalibrasyon 1 kg ile yapılabilir; **0–5 kg boyunca doğruluğu ayrıca kontrol etmek** gerekir.

**HX711 24 bitse terazi miligram hassas mı?**  
Hayır. ADC nominal bit çözünürlüğü ile sistemin gerçek etkin çözünürlüğü/doğruluğu aynı şey değildir.

---

## 14. Referans açık kaynak projeler

Aşağıdaki projeler **örnek mimari ve kod referansı**; bizim satın alacağımız sensör modelinin birebir aynı olması gerekmez.

| Proje | Kullanılan/önemli parça | Bizim için önemi |
|---|---|---|
| [bogde/HX711](https://github.com/bogde/HX711) | Arduino/ESP32 HX711 kütüphanesi | `begin`, `read_average`, `tare`, `get_value`, `set_scale`, `get_units` |
| [kchauxs/Esp32-load-cell-hx711](https://github.com/kchauxs/Esp32-load-cell-hx711) | ESP32, HX711, 16×2 I²C LCD, buton | En yakın fiziksel sistemlerden; C++/Arduino framework |
| [veendysuseno/ESP32-Digital-Scale](https://github.com/veendysuseno/ESP32-Digital-Scale) | ESP32, HX711, 16×2 LCD, Wi-Fi, HTTP/MQTT | Kablosuz ölçüm fikri |
| [CalPlug/Espressif_ESP32-Scale](https://github.com/CalPlug/Espressif_ESP32-Scale) | ESP32, HX711, BLE ile Android tablette tartım | Tabletli ölçümün açık kaynak örneği (Bluetooth yaklaşımı) |
| [Ninnh-temp/Digital-Scale](https://github.com/Ninnh-temp/Digital-Scale) | ESP32, HX711, LCD, **4×50 kg yarım köprü** | Hata/histerezis analizi örneği; **sensör kurulumu bizimkinden farklı** |
| [sandrozucche/esp32-hx711-lcd-scale](https://github.com/sandrozucche/esp32-hx711-lcd-scale) | ESP32, HX711, 20×4 LCD, ESP-IDF C | Alternatif yazılım mimarisi; prototip geliştirme aşamasında |

**İncelenmiş kod ayrıntısı:** `kchauxs` projesinin `src/main.cpp` dosyasında `knownWeight = {180,250,500,750,1000}` seçenekleri var; fakat ağırlık seçme döngüsündeki `weightIndex > 2` koşulu nedeniyle son seçenek olan 1000 g'a normal buton akışıyla ulaşılamıyor. Referans kodu **olduğu gibi kopyalamayacağız**.

**Not:** GitHub'da bir deponun bulunması ürünün laboratuvarda kanıtlanmış doğruluğunu garantilemez. Farklı platformlardaki genel HX711 sürücüleri, bizim 5 kg düzenimizin teknik ölçüm başarısını tek başına ispatlamaz.

---

## 15. Riskler, sınırlamalar, doğrulanacaklar

### Kesin karar verilenler

- ESP32 DevKit V1 tabanlı, **C++ / Arduino IDE**.
- Tek **5 kg, dört telli, tam köprülü** load cell.
- Haricî **HX711 ADC**.
- Fiziksel **16×2 I²C LCD** **ve** tablette görüntüleme.
- İnternetsiz **ESP32 SoftAP** bağlantısı.
- Fiziksel dara butonu; tablet üstünden dara/kalibrasyon seçeneği.
- Newton/gram/kg gösterimi; test verilerinin analizi.

### Sipariş/kurulum öncesi henüz doğrulanmayanlar

1. **Yük hücresinin kesin marka/modeli**; datasheet ve montaj delik aralıkları.
2. Load cell **renk–terminal eşleştirmesi**, tam köprü olduğu ve nominal duyarlılığı (mV/V).
3. HX711 modülünün **kesin modeli**, 3,3 V beslemede çalışma koşulları ve sinyal seviyeleri.
4. LCD modelinin 3,3/5 V davranışı ve I²C adresi; nihai seviye dönüştürücü bağlantısı.
5. Platformun ölçüleri, malzemesi, ağırlığı ve mekanik yük aktarımı.
6. Kalibrasyonda kullanılacak referans ağırlığın **gerçek değeri/doğruluğu**.
7. Gerçek sistemin çözünürlüğü, doğruluğu, kararlılığı ve kablosuz güncelleme hızı.
8. LCD kütüphanesinin tam sürümü ve bütünleşik firmware'in gerçek ESP32 üzerinde **derleme/çalışma testi**.

**Başarıyı belirleyen veri:** “Kod çalıştı” cümlesi değil; gerçek donanımda okunan değer, kalibrasyon sonucu, LCD/tablet çıktısı ve ölçüm hata tablosu.

---

## 16. Son proje özeti

**Bu proje, Wheatstone köprülü tek bir 5 kg yük hücresinin analog çıkışını HX711 üzerinde yükseltip sayısallaştırarak ESP32'ye ileten; ESP32'nin C++ yazılımıyla kütleyi hesaplayıp sonuçları hem kablolu 16×2 LCD'de hem de Wi-Fi üzerinden Huawei MatePad tarayıcısında canlı grafik olarak gösteren, kalibrasyon ve hata analizi yapılabilen elektronik terazidir.**

**Öncelikli sonraki iş:** Tam ürün modeline karar vermek → sensör montaj boyutunu doğrulamak → bağlantıyı kurmak → HX711 ham değerini gözlemlemek → kalibrasyon → LCD + web sayfası.
