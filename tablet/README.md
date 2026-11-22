# Hassas Terazi Tablet Arayüzü (`tablet/`)

Tek dosyalı (`index.html`) izleme, dara ve kalibrasyon arayüzü. Sayfa açıldığı protokole göre **iki farklı modda** çalışır:

1. **Çevrimdışı Demo Modu (`file://`):**
   - `index.html` dosyasına bilgisayarda veya tablette çift tıklanarak doğrudan açıldığında devreye girer.
   - Ağ, sunucu veya ESP32 bağlantısı gerektirmez; hiçbir `fetch` isteği yapmaz.
   - Üst rozette **"Demo — gerçek ölçüm değil"** etiketi görünür; demo yük sürgüsü ve demo durumu seçicisi açıktır.
2. **Canlı ESP32 Modu (`http://` veya `https://`):**
   - ESP32 Wi-Fi erişim noktasına (`HassasTerazi`, varsayılan IP: `http://192.168.4.1/`) bağlanıldığında aynı HTML dosyası ESP32 flash (`PROGMEM`) belleğinden sunulur.
   - Üst rozette **"Canlı — ESP32 bağlantısı"** etiketi görünür; demo yük sürgüsü ve demo durumu seçicisi otomatik olarak gizlenir.
   - Bağlantı koptuğunda veya sensör hatası oluştuğunda **asla otomatik olarak sahte demo moduna geçmez**; ağırlık göstergesi `—` olur ve grafik çizgisinde boşluk (`gap`) bırakılır.

---

## Birim ve Katsayı Ayrımı

- **Demo Modu (`file://`):** Sayfa içi simülasyon `TeraziCore.mass(raw, zero, factor, tare)` fonksiyonunu kullanır; buradaki demo katsayısı `gram / sayım` birimindedir ve yalnızca tarayıcı belleğinde yaşar.
- **Canlı Mod (`http://`):** ESP32 firmware'i kendi içinde `K = sayım / gram` katsayısıyla `m = (R - R0) / K` hesabını yapar ve `GET /api/weight` yanıtında doğrudan net gram (`grams`) değerini gönderir. Tablet arayüzü canlı modda demo katsayısını **kesinlikle tekrar uygulamaz**; yalnızca `TeraziCore.convert(grams)` ile `g`, `kg` ve `N` (Newton) birim dönüşümü yapar. Negatif net ağırlıklar korunur.

---

## Uygulanan HTTP API Sözleşmesi

Tüm istekler aynı origin üzerinde, `cache: 'no-store'` ve `AbortController` zaman aşımı (`1500 ms`) ile çakışmasız/sıralı (`~500 ms`) yürütülür:

- **`GET /`** → `text/html; charset=utf-8` (`PROGMEM` içerisinden `tablet/index.html`).
- **`GET /api/weight`** → Anlık durum ve ölçüm JSON nesnesi:
  ```json
  {
    "grams": 1250.20,
    "calibrated": true,
    "sensor_ok": true,
    "seq": 42,
    "sample_age_ms": 85,
    "busy": false,
    "operation": "none",
    "op_status": "idle",
    "op_id": 1,
    "error": null
  }
  ```
  - Yeni fiziksel sensör örneği gelmedikçe `seq` artmaz; tablet arayüzü tekrarlı `seq` değerlerinde grafiğe sahte taze nokta eklemez.
  - ESP32 yeniden başlatıldığında (`seq === 0` veya `seq < lastSeq`) grafikte kesinti (`gap`) bırakılır ve sıra takibi sıfırlanır.
  - `sample_age_ms > 1500 ms` olan bayat örnekler, `sensor_ok: false` veya `calibrated: false` durumları gerçek ağırlık olarak gösterilmez (`—`).
- **`POST /api/tare`** → Platform üzerindeki mevcut yükü yeni sıfır referansı (`R0`) yapmak için bloklamayan örnek toplama başlatır (`202 Accepted`).
- **`POST /api/calibrate`** → İstek gövdesi `{"known_g": 1000}`. Tür, sonluluk (`finite`), pozitiflik, kapasite (`0 < known_g <= 5000`) ve sıfır fark kontrollerinden geçerse `202 Accepted` döner.
  - Tablet arayüzü `202 Accepted` yanıtını **bitmiş başarı saymaz**; izleyen `GET /api/weight` yanıtlarında `op_status: "completed"` veya `"failed"` durumunu bekler.

---

## Test ve PROGMEM Başlık Üretimi

Depo kök dizininden:

```powershell
# 1. Demo ve canlı istemci birim/entegrasyon testleri (Node.js stdlib test runner)
node --test tablet/tests/core.test.cjs

# 2. tablet/index.html değiştikten sonra firmware/webui_html.h dosyasını güncelleme
node firmware/tools/generate_webui_header.cjs

# 3. PROGMEM başlığının kaynak HTML ile birebir güncel olduğunu doğrulama
node firmware/tools/generate_webui_header.cjs --check
```
