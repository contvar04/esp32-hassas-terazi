# I²C adres tarama testi

Bu bağımsız teşhis yazılımı ana terazi kodunu değiştirmez. Karta yüklendiğinde ana firmware yerine çalışır; Wi-Fi ve tablet sayfası bu sırada kapalıdır.

- SDA: GPIO21 / D21; SCL: GPIO22 / D22.
- Seri hız: 115200 baud; I²C: 100 kHz, işlem zaman aşımı 50 ms.
- Ayrılmış adresleri hariç tutarak 0x08–0x77 aralığında yalnız adres ACK yoklaması yapar; veri baytı veya LCD komutu yazmaz.
- SDA veya SCL başlangıçta LOW ise tarama atlanır. Normal NACK cihaz sayılmaz; diğer hata kodları ayrı raporlanır.
- Her tamamlanan denemeden sonra 3 saniye bekler. ACK alınması LCD yazı gösterimini veya lehimsiz bağlantının güvenilirliğini kanıtlamaz.

Depo kökünden:

```text
pio run -d examples/i2c_scan
pio run -d examples/i2c_scan -t upload --upload-port COM9
pio device monitor --port COM9 --baud 115200
```

COM9 bu makinede tespit edilen porttur; sonraki bağlantılarda yeniden kontrol edilir. Ana firmware'e dönmek için `pio run -d firmware -t upload --upload-port COM9` kullanılır. Yüklemeden önce portu kullanan monitör kapatılır.

Host tarama testini `g++ -std=c++11 -Wall -Wextra examples/i2c_scan/tests/test_scan.cpp -o <proje dışı çıktı>` ile derleyip çalıştırın. Sahte bus ile ACK/NACK, hata, adres aralığı ve LOW hat durumları sınanır; gerçek elektriksel test değildir.

USB takılıyken kablo/pin oynatmayın. LCD 5 V besleniyorsa veri hatları ESP32'ye uygun seviye dönüştürücü üzerinden bağlanmalıdır.
