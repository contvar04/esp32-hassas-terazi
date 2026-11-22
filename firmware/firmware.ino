/*
 * ============================================================================
 * ESP32 WHEATSTONE KÖPRÜLÜ HASSAS TERAZİ — ANA ÜRETİM FIRMWARE (firmware.ino)
 * ----------------------------------------------------------------------------
 * Donanım Mimarisi:
 *   - Denetleyici : ESP32 (ESP-32S / WROOM 30 pin DevKit V1)
 *   - Sensör/ADC  : 5 kg 4 telli tam köprü yük hücresi + HX711 24-bit ADC
 *                   (DOUT = GPIO16, PD_SCK = GPIO4, Kanal A Kazanç = 128)
 *   - Ekran       : 20x4 I2C LCD (PCF8574 adaptörlü, SDA = GPIO21, SCL = GPIO22)
 *                   + 3,3V <-> 5V çift yönlü seviye dönüştürücü
 *   - Dara Butonu : GPIO27 (INPUT_PULLUP, aktif-LOW, yazılımsal debounce)
 *   - Kablosuz Ağ : ESP32 Wi-Fi SoftAP + dahili WebServer (HTTP port 80)
 *
 * Matematiksel Model ve Birimler:
 *   - Ham ADC okuması (R)       : [ham ADC sayımı] (24-bit işaretli tam sayı)
 *   - Boş platform referansı(R0): [ham ADC sayımı] (açılış sıfırlaması veya dara ofseti)
 *   - Kalibrasyon katsayısı (K) : [ham ADC sayımı / gram] (NVS'de saklanır; negatif K geçerlidir)
 *   - Net kütle (m_gram)        : m = (R - R0) / K  [gram]
 *   - Kilogram (m_kg)           : m_gram / 1000.0   [kg]
 *   - Kuvvet (F_N)              : m_kg * 9.80665    [Newton]
 *
 * Kritik Tasarım Kararları ve Bilinen Fiziksel Sınırlar:
 *   1. AÇILIŞTA BOŞ PLATFORM BEKLENTİSİ:
 *      Cihaz enerjilendiğinde tartım platformunun BOŞ olduğu varsayılır.
 *      setup() içinde bloklayıcı sensör beklemesi yapılmaz; boş referans (R0)
 *      loop() içinde gelen ilk STARTUP_ZERO_SAMPLE_COUNT (3) geçerli örnekle
 *      bloklamadan toplanır.
 *   2. BLOKLAMAYAN HX711 OKUMA VE YARIŞ PENCERESİ SINIRI:
 *      bogde/HX711 kütüphanesinin read() fonksiyonu içeride wait_ready() çağırır;
 *      read_average() ve tare() ise çoklu örnek bekleyerek sistemi kilitler.
 *      Bu nedenle read_average() ve tare() HİÇ KULLANILMAZ. Her loop() adımında
 *      yalnızca scale.is_ready() == true olduğunda tekil scale.read() çağrılır.
 *      Bilinen sınır: is_ready() kontrolü ile hemen sonraki read() arasındaki
 *      birkaç mikrosaniyelik pencerede fiziksel DOUT kablosu koparsa sürücü içi
 *      wait_ready() bekleyebilir; ESP32 kritik bölge (portENTER_CRITICAL) zamanlama
 *      korumasını bozmamak için özel bit-bang sürücü yazılmamıştır.
 *   3. WEBSERVER TEK İSTEMCİ SINIRI:
 *      Arduino-ESP32 yerleşik WebServer sınıfı tek istemciyi sırayla işler ve
 *      istek okuma/gönderme sırasında ağ beklemeleri içerir; tamamen asenkron
 *      olduğu iddia edilmez. Ancak HTTP işleyicileri içinde sensör örneği
 *      beklenmez; dara ve kalibrasyon istekleri 202 Accepted döndürüp örnek
 *      toplamayı loop() akışına devreder.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HX711.h>
#include <LiquidCrystal_PCF8574.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "terazi_config.h"
#include "webui_html.h"

// ============================================================================
// BÖLÜM 1: DONANIM NESNELERİ VE ÇALIŞMA ZAMANI DURUM MAKİNESİ
// ============================================================================

HX711 scale;
LiquidCrystal_PCF8574 lcd(LCD_I2C_ADDR);
WebServer server(HTTP_PORT);

// Asenkron (loop tabanlı) işlem türleri ve durumları
enum class OperationType : uint8_t {
    NONE = 0,
    STARTUP_ZERO,
    TARE,
    CALIBRATE
};

enum class OperationStatus : uint8_t {
    IDLE = 0,
    BUSY,
    COMPLETED,
    FAILED
};

struct RuntimeState {
    // Sensör ve zamanlama durumu
    bool hasEverReadValidSample = false;
    bool sensorOk = false;
    bool inTimeoutError = false;
    unsigned long bootMs = 0;
    unsigned long lastValidSampleMs = 0;
    unsigned long lastErrorReportMs = 0;
    uint32_t sampleSeq = 0;
    long lastRawSample = 0;

    // Boş referans (R0) ve kalibrasyon katsayısı (K: sayım / gram)
    bool hasZeroReference = false;
    double rawZeroOffset = 0.0;
    bool calibrated = false;
    float scaleFactor = 0.0f;

    // Hareketli ortalama filtre penceresi (eski örneklerin yeni yükle karışmasını önlemek için)
    long filterBuf[FILTER_WINDOW_SIZE] = {0};
    uint8_t filterCount = 0;
    uint8_t filterIndex = 0;

    // Bloklamayan örnek toplama işlemi (açılış sıfırlaması, dara, kalibrasyon)
    OperationType opType = OperationType::NONE;
    OperationStatus opStatus = OperationStatus::IDLE;
    bool busy = false;
    uint32_t opId = 0;
    uint8_t opTargetSamples = 0;
    uint8_t opCollectedSamples = 0;
    double opAccumSum = 0.0;
    long opMinRaw = 0;
    long opMaxRaw = 0;
    unsigned long opSettleMs = 0;
    float opTargetKnownGrams = 0.0f;
    unsigned long opStartMs = 0;
    char errorMsg[96] = {0};

    // Fiziksel dara butonu debounce durumu (INPUT_PULLUP: HIGH=bırakıldı, LOW=basılı)
    int lastRawButtonState = HIGH;
    int stableButtonState = HIGH;
    bool tareButtonArmed = false;
    unsigned long lastButtonEdgeMs = 0;

    // 20x4 I2C LCD önbelleği (titreşimsiz / clear'sız satır güncellemesi için)
    bool lcdInitialized = false;
    bool wifiApOk = false;
    unsigned long lastLcdUpdateMs = 0;
    bool hasDisplayGrams = false;
    double displayGrams = 0.0;
    char lcdCache[LCD_ROWS][LCD_COLS + 1] = {{0}};
};

static RuntimeState g_state;

// ============================================================================
// BÖLÜM 2: YARDIMCI FONKSİYONLAR (FİLTRE, DOĞRULAMA, NVS KALICILIK)
// ============================================================================

static void setErrorText(const char* msg) {
    if (!msg) {
        g_state.errorMsg[0] = '\0';
        return;
    }
    snprintf(g_state.errorMsg, sizeof(g_state.errorMsg), "%s", msg);
}

// Kalibrasyon katsayısının (K: sayım/gram) geçerli olup olmadığını denetler.
// Kural: 0, NaN veya Infinity geçersizdir; negatif veya pozitif sonlu değerler geçerlidir.
static bool isValidScaleFactor(float k) {
    return std::isfinite(k) && (k != 0.0f);
}

// HX711 24-bit işaretli ADC değerinin doyma (saturasyon) sınırları içinde olup olmadığını denetler.
// Normal negatif veya sıfır değerler (ör. 0, -1, -450000) geçerlidir; uç raylar (-8388608, +8388607) hatadır.
static bool isValidAdcSample(long raw) {
    return (raw > HX711_RAW_SAT_MIN) && (raw < HX711_RAW_SAT_MAX);
}

// Hareketli ortalama penceresini sıfırlar (yeni dara/kalibrasyon öncesi eski örnekleri temizler).
static void clearFilterWindow() {
    g_state.hasDisplayGrams = false;
    for (uint8_t i = 0; i < FILTER_WINDOW_SIZE; ++i) {
        g_state.filterBuf[i] = 0;
    }
    g_state.filterCount = 0;
    g_state.filterIndex = 0;
}

// Filtre penceresini bilinen taze bir referans değerle doldurur.
static void seedFilterWindow(long sample) {
    for (uint8_t i = 0; i < FILTER_WINDOW_SIZE; ++i) {
        g_state.filterBuf[i] = sample;
    }
    g_state.filterCount = FILTER_WINDOW_SIZE;
    g_state.filterIndex = 0;
}

// Yeni geçerli ham örneği hareketli ortalama penceresine ekler.
static void pushFilterSample(long sample) {
    g_state.filterBuf[g_state.filterIndex] = sample;
    g_state.filterIndex = static_cast<uint8_t>((g_state.filterIndex + 1U) % FILTER_WINDOW_SIZE);
    if (g_state.filterCount < FILTER_WINDOW_SIZE) {
        g_state.filterCount++;
    }
}

// Filtrelenmiş ortalama ham ADC değerini döndürür (birim: ham ADC sayımı).
static double getFilteredRaw() {
    if (g_state.filterCount == 0) {
        return static_cast<double>(g_state.lastRawSample);
    }
    double sum = 0.0;
    for (uint8_t i = 0; i < g_state.filterCount; ++i) {
        sum += static_cast<double>(g_state.filterBuf[i]);
    }
    return sum / static_cast<double>(g_state.filterCount);
}

// NVS'den kayıtlı kalibrasyon katsayısını (K) yükler.
static void loadCalibrationFromNvs() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        g_state.calibrated = false;
        g_state.scaleFactor = 0.0f;
        return;
    }

    bool hasKey = prefs.isKey(NVS_KEY_SCALE);
    float savedK = hasKey ? prefs.getFloat(NVS_KEY_SCALE, 0.0f) : 0.0f;
    prefs.end();

    if (hasKey && isValidScaleFactor(savedK)) {
        g_state.scaleFactor = savedK;
        g_state.calibrated = true;
    } else {
        g_state.scaleFactor = 0.0f;
        g_state.calibrated = false;
    }
}

// Yeni kalibrasyon katsayısını (K) NVS'ye yazar ve putFloat->putBytes->nvs_commit dönüşünü doğrular.
static bool saveCalibrationToNvs(float candidateK) {
    if (!isValidScaleFactor(candidateK)) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    size_t written = prefs.putFloat(NVS_KEY_SCALE, candidateK);
    prefs.end();
    return (written == sizeof(float));
}

// Bloklamayan örnek toplama işlemini başlatır (setup, buton veya HTTP komutu tarafından çağrılır).
static void startSamplingOperation(OperationType type, uint8_t sampleCount, float knownGrams, unsigned long nowMs,
                                   unsigned long settleMs = 0UL) {
    g_state.opType = type;
    g_state.opStatus = OperationStatus::BUSY;
    g_state.busy = true;
    g_state.opId++;
    g_state.opTargetSamples = sampleCount;
    g_state.opCollectedSamples = 0;
    g_state.opAccumSum = 0.0;
    g_state.opTargetKnownGrams = knownGrams;
    g_state.opStartMs = nowMs;
    g_state.opSettleMs = settleMs;
    setErrorText("");
    // Eski filtre örneklerinin yeni işlemle karışmaması için pencereyi temizle
    clearFilterWindow();
}

// Geçerli net ağırlığı (gram) hesaplar; ölçüm sunulmaya uygun değilse false döner.
static bool computeNetGrams(double& outGrams) {
    if (!g_state.sensorOk || !g_state.calibrated || !g_state.hasZeroReference || g_state.busy) {
        return false;
    }
    if (!isValidScaleFactor(g_state.scaleFactor) || g_state.filterCount == 0) {
        return false;
    }
    const double rawAvg = getFilteredRaw();
    const double netG = (rawAvg - g_state.rawZeroOffset) / static_cast<double>(g_state.scaleFactor);
    if (!std::isfinite(netG)) {
        return false;
    }
    // -0.00 gösterimini önlemek için çok küçük sıfır etrafındaki değeri normalize et
    outGrams = (std::fabs(netG) < 0.0005) ? 0.0 : netG;
    return true;
}

static const char* operationTypeToStr(OperationType t) {
    switch (t) {
        case OperationType::STARTUP_ZERO: return "startup_zero";
        case OperationType::TARE:         return "tare";
        case OperationType::CALIBRATE:    return "calibrate";
        default:                          return "none";
    }
}

static const char* operationStatusToStr(OperationStatus s) {
    switch (s) {
        case OperationStatus::BUSY:      return "busy";
        case OperationStatus::COMPLETED: return "completed";
        case OperationStatus::FAILED:    return "failed";
        default:                         return "idle";
    }
}

// ============================================================================
// BÖLÜM 3: KATILIKLI JSON GÖVDE AYRIŞTIRICISI (POST /api/calibrate)
// ============================================================================

// RFC 8259 JSON boşluk karakterlerini (' ', '\t', '\n', '\r') denetler.
static bool isJsonWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

// RFC 8259 Bölüm 6 JSON sayı dilbilgisini (grammar) denetler:
//   number = [ minus ] int [ frac ] [ exp ]
//   int    = zero / ( digit1-9 *digit )
//   frac   = decimal-point 1*digit
//   exp    = e [ minus / plus ] 1*digit
// Tamsayı kısmında 01 gibi baştaki sıfırları, 1. gibi rakamsız noktayı,
// .5 gibi tamsayısız noktayı, +10 gibi artı işaretini ve 0x1p0 hex biçimlerini reddeder.
static bool consumeRfcJsonNumberToken(const char* s, size_t len, size_t& ioIdx) {
    size_t i = ioIdx;
    if (i >= len) return false;

    if (s[i] == '-') {
        i++;
        if (i >= len) return false;
    }

    if (s[i] == '0') {
        i++;
        // RFC 8259: 0'dan sonra doğrudan başka rakam gelemez (ör. 01, 00 geçersizdir)
        if (i < len && isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    } else if (s[i] >= '1' && s[i] <= '9') {
        i++;
        while (i < len && isdigit(static_cast<unsigned char>(s[i]))) {
            i++;
        }
    } else {
        return false;
    }

    // Opsiyonel kesir kısmı: '.' sonrasında en az 1 rakam zorunludur
    if (i < len && s[i] == '.') {
        i++;
        if (i >= len || !isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
        while (i < len && isdigit(static_cast<unsigned char>(s[i]))) {
            i++;
        }
    }

    // Opsiyonel üs kısmı: 'e' veya 'E', opsiyonel '+'/'-', sonrasında en az 1 rakam zorunludur
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        if (i >= len || !isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
        while (i < len && isdigit(static_cast<unsigned char>(s[i]))) {
            i++;
        }
    }

    ioIdx = i;
    return true;
}

// Yalnızca geçerli {"known_g": <sonlu_sayı>} nesnesini kabul eder.
// Metin ("1000"), boolean, null, NaN, Infinity, RFC dışı sayı sözdizimi veya fazladan bozuk karakterleri reddeder.
static bool parseCalibrateJsonBody(const String& body, float& outKnownGrams, const char*& outErr) {
    const char* s = body.c_str();
    const size_t len = body.length();
    if (len == 0) {
        outErr = "Bos istek govdesi.";
        return false;
    }

    size_t i = 0;
    while (i < len && isJsonWhitespace(s[i])) i++;
    if (i >= len || s[i] != '{') {
        outErr = "Gecersiz JSON nesnesi.";
        return false;
    }
    i++;

    while (i < len && isJsonWhitespace(s[i])) i++;
    const char* keyLiteral = "\"known_g\"";
    const size_t keyLen = strlen(keyLiteral);
    if (i + keyLen > len || strncmp(s + i, keyLiteral, keyLen) != 0) {
        outErr = "known_g alani bulunamadi.";
        return false;
    }
    i += keyLen;

    while (i < len && isJsonWhitespace(s[i])) i++;
    if (i >= len || s[i] != ':') {
        outErr = "JSON sozdizimi hatasi (: bekleniyordu).";
        return false;
    }
    i++;

    while (i < len && isJsonWhitespace(s[i])) i++;
    if (i >= len) {
        outErr = "known_g degeri eksik.";
        return false;
    }

    const size_t numStart = i;
    if (!consumeRfcJsonNumberToken(s, len, i)) {
        outErr = "known_g gecerli bir RFC JSON sayisi olmalidir.";
        return false;
    }
    const size_t numEnd = i;

    while (i < len && isJsonWhitespace(s[i])) i++;
    if (i >= len || s[i] != '}') {
        outErr = "JSON nesnesi beklenmeyen karakter iceriyor.";
        return false;
    }
    i++;
    while (i < len && isJsonWhitespace(s[i])) i++;
    if (i != len) {
        outErr = "JSON sonrasinda fazladan veri var.";
        return false;
    }

    char* endPtr = nullptr;
    const double parsedVal = strtod(s + numStart, &endPtr);
    if (endPtr != (s + numEnd)) {
        outErr = "known_g sayisi okunamadi.";
        return false;
    }

    if (!std::isfinite(parsedVal) || parsedVal <= 0.0 || parsedVal > static_cast<double>(MAX_CAPACITY_G)) {
        outErr = "known_g 0 ile 5000 g arasinda sonlu pozitif bir sayi olmalidir.";
        return false;
    }

    // Hedef float tipinde de (ör. 1e-50 -> 0.0f underflow) sonlu, pozitif ve kapasite dahilinde olduğunu doğrula
    const float targetFloat = static_cast<float>(parsedVal);
    if (!std::isfinite(targetFloat) || targetFloat <= 0.0f || targetFloat > MAX_CAPACITY_G) {
        outErr = "known_g float hassasiyetinde 0 ile 5000 g arasinda pozitif olmalidir.";
        return false;
    }

    outKnownGrams = targetFloat;
    outErr = nullptr;
    return true;
}

// ============================================================================
// BÖLÜM 4: 20x4 I2C LCD EKRAN YÖNETİMİ (TİTREŞİMSİZ VE KOPMAYA DAYANIKLI)
// ============================================================================
// Not: Standart HD44780 karakter ROM'unda çok baytlı UTF-8 Türkçe harfler
// bozuk sembol ürettiği için LCD satırlarında temiz ASCII Türkçe kullanılır.

static void formatLcdRowPadded(char (&dest)[LCD_COLS + 1], const char* text) {
    size_t idx = 0;
    if (text) {
        while (text[idx] != '\0' && idx < LCD_COLS) {
            dest[idx] = text[idx];
            idx++;
        }
    }
    while (idx < LCD_COLS) {
        dest[idx] = ' ';
        idx++;
    }
    dest[LCD_COLS] = '\0';
}

// LCD için biçimlendirilmiş sayıda yalnızca eksi sıfırın işaretini kaldır.
static void hideLcdNegativeZero(char* line) {
    for (char* p = line; *p != '\0'; ++p) {
        if (*p != '-' || p[1] != '0') continue;
        char* end = p + 1;
        while (*end == '0' || *end == '.') ++end;
        if (*end < '1' || *end > '9') *p = ' ';
    }
}

static void updateLcdIfDue(unsigned long nowMs) {
    if ((unsigned long)(nowMs - g_state.lastLcdUpdateMs) < LCD_UPDATE_INTERVAL_MS) {
        return;
    }
    g_state.lastLcdUpdateMs = nowMs;

    // LCD bağlı değilse I2C hattını ve web/ölçüm döngüsünü bloklama!
    if (!lcd.isConnected()) {
        g_state.lcdInitialized = false;
        return;
    }

    // LCD yeni bağlandıysa veya yeniden enerjilendiyse başlat ve önbelleği temizle
    if (!g_state.lcdInitialized) {
        lcd.begin(LCD_COLS, LCD_ROWS, Wire);
        lcd.setBacklight(LCD_BACKLIGHT_ON);
        for (uint8_t r = 0; r < LCD_ROWS; ++r) {
            g_state.lcdCache[r][0] = '\0';
        }
        g_state.lcdInitialized = true;
    }

    char rows[LCD_ROWS][LCD_COLS + 1];
    char lineBuf[32];

    // Satır 0: Başlık ve genel durum
    if (g_state.busy) {
        formatLcdRowPadded(rows[0], "HASSAS TERAZI [ISLM]");
    } else if (!g_state.sensorOk) {
        formatLcdRowPadded(rows[0], "HASSAS TERAZI [HATA]");
    } else if (!g_state.calibrated) {
        formatLcdRowPadded(rows[0], "HASSAS TERAZI [KLBR]");
    } else {
        formatLcdRowPadded(rows[0], "HASSAS TERAZI  [OK] ");
    }

    // Satır 1 ve 2: Net ağırlık (g / kg / N) veya durum açıklaması
    double netG = 0.0;
    if (computeNetGrams(netG)) {
        snprintf(lineBuf, sizeof(lineBuf), "Net: %9.0f g", g_state.displayGrams);
        hideLcdNegativeZero(lineBuf);
        formatLcdRowPadded(rows[1], lineBuf);

        // Her gösterim birimini aynı kararlı tam gram değerinden türet.
        const double netKg = g_state.displayGrams / 1000.0;
        const double netN = netKg * 9.80665;
        snprintf(lineBuf, sizeof(lineBuf), "%6.3fkg %7.2fN", netKg, netN);
        hideLcdNegativeZero(lineBuf);
        formatLcdRowPadded(rows[2], lineBuf);
    } else if (g_state.busy) {
        if (g_state.opType == OperationType::STARTUP_ZERO) {
            formatLcdRowPadded(rows[1], "Acilis sifirlamasi  ");
            formatLcdRowPadded(rows[2], "Platformu bosaltin  ");
        } else if (g_state.opType == OperationType::TARE) {
            formatLcdRowPadded(rows[1], "Dara aliniyor...    ");
            formatLcdRowPadded(rows[2], "Lutfen bekleyin     ");
        } else {
            formatLcdRowPadded(rows[1], "Kalibre ediliyor... ");
            formatLcdRowPadded(rows[2], "Yuk sabit kalsin    ");
        }
    } else if (!g_state.sensorOk) {
        formatLcdRowPadded(rows[1], "Net: SENSOR YOK     ");
        formatLcdRowPadded(rows[2], "HX711 kontrol edin  ");
    } else {
        formatLcdRowPadded(rows[1], "Net: KALIBRE DEGIL  ");
        formatLcdRowPadded(rows[2], "Webden kalibre edin ");
    }

    // Satır 3: İşlem hatası veya Wi-Fi AP durumu / IP bilgisi
    if (g_state.opStatus == OperationStatus::FAILED && g_state.errorMsg[0] != '\0') {
        formatLcdRowPadded(rows[3], "HATA: Islem basarisz");
    } else if (!g_state.wifiApOk) {
        formatLcdRowPadded(rows[3], "AP:HATA (WiFi yok)  ");
    } else {
        String ipStr = WiFi.softAPIP().toString();
        snprintf(lineBuf, sizeof(lineBuf), "AP:%s", ipStr.c_str());
        formatLcdRowPadded(rows[3], lineBuf);
    }

    // Yalnızca içeriği değişen satırları yaz (lcd.clear() çağrılmaz, titreşim olmaz)
    for (uint8_t r = 0; r < LCD_ROWS; ++r) {
        if (strncmp(g_state.lcdCache[r], rows[r], LCD_COLS) != 0) {
            lcd.setCursor(0, r);
            lcd.print(rows[r]);
            memcpy(g_state.lcdCache[r], rows[r], LCD_COLS + 1);
        }
    }
}

// ============================================================================
// BÖLÜM 5: HTTP API İŞLEYİCİLERİ (TAM 4 ROTA)
// ============================================================================

static void sendNoStoreHeaders() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
}

// JSON metin alanında özel karakterleri güvenli kaçış (escape) ile yazar.
static void appendJsonEscapedString(String& out, const char* text) {
    if (!text) {
        out += "null";
        return;
    }
    out += "\"";
    for (size_t i = 0; text[i] != '\0'; ++i) {
        const char c = text[i];
        if (c == '\"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c >= 0x20) {
            out += c;
        }
    }
    out += "\"";
}

// 1. GET / -> Tablet arayüzünü flash PROGMEM'den sunar
static void handleGetRoot() {
    sendNoStoreHeaders();
    server.send_P(200, "text/html; charset=utf-8", TABLET_INDEX_HTML, TABLET_INDEX_HTML_LEN);
}

// 2. GET /api/weight -> Anlık ölçüm ve işlem durumunu döndürür (NaN/Infinity içermez)
static void handleGetWeight() {
    const unsigned long nowMs = millis();

    // Son geçerli örnekten bu yana geçen süre (32-bit wrap-around güvenli çıkarma)
    bool hasSampleAge = g_state.hasEverReadValidSample;
    unsigned long sampleAgeMs = hasSampleAge ? (unsigned long)(nowMs - g_state.lastValidSampleMs) : 0UL;
    if (hasSampleAge && sampleAgeMs >= READY_TIMEOUT_MS) {
        g_state.sensorOk = false;
    }

    double netGrams = 0.0;
    const bool hasValidGrams = computeNetGrams(netGrams);

    char numBuf[32];
    String json = "{";

    // grams: number | null
    json += "\"grams\":";
    if (hasValidGrams && std::isfinite(netGrams)) {
        snprintf(numBuf, sizeof(numBuf), "%.2f", netGrams);
        json += numBuf;
    } else {
        json += "null";
    }

    // LCD ile paylaşılan, geçiş payı uygulanmış tam gram: number | null
    json += ",\"display_grams\":";
    if (hasValidGrams && g_state.hasDisplayGrams) {
        snprintf(numBuf, sizeof(numBuf), "%.0f", g_state.displayGrams);
        json += numBuf;
    } else {
        json += "null";
    }

    json += ",\"calibrated\":";
    json += (g_state.calibrated ? "true" : "false");

    // sensor_ok: boolean
    json += ",\"sensor_ok\":";
    json += (g_state.sensorOk ? "true" : "false");

    // seq: number (yalnız yeni geçerli sensör örneğinde artar)
    snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(g_state.sampleSeq));
    json += ",\"seq\":";
    json += numBuf;

    // sample_age_ms: number | null
    json += ",\"sample_age_ms\":";
    if (hasSampleAge) {
        snprintf(numBuf, sizeof(numBuf), "%lu", sampleAgeMs);
        json += numBuf;
    } else {
        json += "null";
    }

    // Ek durum izleme alanları (202 Accepted işlemlerinin tamamlanma/hata takibi için)
    json += ",\"busy\":";
    json += (g_state.busy ? "true" : "false");

    json += ",\"operation\":\"";
    json += operationTypeToStr(g_state.opType);
    json += "\"";

    json += ",\"op_status\":\"";
    json += operationStatusToStr(g_state.opStatus);
    json += "\"";

    snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(g_state.opId));
    json += ",\"op_id\":";
    json += numBuf;

    json += ",\"error\":";
    if (g_state.errorMsg[0] != '\0') {
        appendJsonEscapedString(json, g_state.errorMsg);
    } else {
        json += "null";
    }

    json += "}";

    sendNoStoreHeaders();
    server.send(200, "application/json; charset=utf-8", json);
}

// 3. POST /api/tare -> Bloklamayan dara alma işlemini başlatır (flash belleğe yazmaz)
static void handlePostTare() {
    sendNoStoreHeaders();
    const unsigned long nowMs = millis();

    if (g_state.hasEverReadValidSample && (unsigned long)(nowMs - g_state.lastValidSampleMs) >= READY_TIMEOUT_MS) {
        g_state.sensorOk = false;
    }

    if (!g_state.sensorOk || !g_state.hasEverReadValidSample) {
        server.send(503, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"Sensor hazir degil veya baglanti hatasi.\"}");
        return;
    }

    if (g_state.busy) {
        server.send(409, "application/json; charset=utf-8",
                    "{\"ok\":false,\"busy\":true,\"error\":\"Baska bir sensor islemi devam ediyor.\"}");
        return;
    }

    startSamplingOperation(OperationType::TARE, TARE_SAMPLE_COUNT, 0.0f, nowMs);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"status\":\"accepted\",\"operation\":\"tare\",\"busy\":true,\"op_id\":%lu}",
             static_cast<unsigned long>(g_state.opId));
    server.send(202, "application/json; charset=utf-8", resp);
}

// 4. POST /api/calibrate -> Boş referansa (R0) göre bilinen yükle kalibrasyon başlatır
static void handlePostCalibrate() {
    sendNoStoreHeaders();
    const unsigned long nowMs = millis();

    const int contentLen = server.clientContentLength();
    const String body = server.arg("plain");
    if (contentLen > static_cast<int>(MAX_JSON_BODY_BYTES) || body.length() > MAX_JSON_BODY_BYTES) {
        server.send(413, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"Istek govdesi cok buyuk (maksimum 256 bayt).\"}");
        return;
    }

    float knownGrams = 0.0f;
    const char* parseErr = nullptr;
    if (!parseCalibrateJsonBody(body, knownGrams, parseErr)) {
        String errJson = "{\"ok\":false,\"error\":";
        appendJsonEscapedString(errJson, parseErr ? parseErr : "Gecersiz kalibrasyon verisi.");
        errJson += "}";
        server.send(400, "application/json; charset=utf-8", errJson);
        return;
    }

    if (g_state.hasEverReadValidSample && (unsigned long)(nowMs - g_state.lastValidSampleMs) >= READY_TIMEOUT_MS) {
        g_state.sensorOk = false;
    }

    if (!g_state.sensorOk || !g_state.hasEverReadValidSample) {
        server.send(503, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"Sensor hazir degil veya baglanti hatasi.\"}");
        return;
    }

    if (g_state.busy) {
        server.send(409, "application/json; charset=utf-8",
                    "{\"ok\":false,\"busy\":true,\"error\":\"Baska bir sensor islemi devam ediyor.\"}");
        return;
    }

    if (!g_state.hasZeroReference) {
        server.send(409, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"Once platform bosken dara/sifirlama alinmalidir.\"}");
        return;
    }

    // Anlık sıfır fark kontrolü: platformda boş referansa (R0) kıyasla anlamlı bir yük farkı olmalıdır
    const double currentRawDiff = static_cast<double>(g_state.lastRawSample) - g_state.rawZeroOffset;
    if (std::fabs(currentRawDiff) < MIN_CALIBRATION_RAW_DIFF) {
        server.send(422, "application/json; charset=utf-8",
                    "{\"ok\":false,\"error\":\"Sifir fark: platformda bilinen yuk algilanmadi.\"}");
        return;
    }

    startSamplingOperation(OperationType::CALIBRATE, CALIBRATE_SAMPLE_COUNT, knownGrams, nowMs);

    char resp[140];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"status\":\"accepted\",\"operation\":\"calibrate\",\"busy\":true,\"op_id\":%lu}",
             static_cast<unsigned long>(g_state.opId));
    server.send(202, "application/json; charset=utf-8", resp);
}

static void handleNotFound() {
    sendNoStoreHeaders();
    server.send(404, "application/json; charset=utf-8",
                "{\"ok\":false,\"error\":\"Bilinmeyen rota veya desteklenmeyen HTTP metodu.\"}");
}

// ============================================================================
// BÖLÜM 6: SENSÖR ÖRNEKLEME, İŞLEM TAMAMLAMA VE BUTON DEBOUNCE AKIŞI
// ============================================================================

static void finalizeActiveOperation() {
    if (!g_state.busy || g_state.opCollectedSamples == 0) {
        return;
    }

    double avgRaw = g_state.opAccumSum / static_cast<double>(g_state.opCollectedSamples);
    if (g_state.opType == OperationType::TARE && g_state.opCollectedSamples > 2) {
        avgRaw = (g_state.opAccumSum - g_state.opMinRaw - g_state.opMaxRaw) /
                 static_cast<double>(g_state.opCollectedSamples - 2);
    }
    const long seededRaw = static_cast<long>(avgRaw >= 0.0 ? (avgRaw + 0.5) : (avgRaw - 0.5));

    if (g_state.opType == OperationType::STARTUP_ZERO || g_state.opType == OperationType::TARE) {
        g_state.rawZeroOffset = avgRaw;
        g_state.hasZeroReference = true;
        seedFilterWindow(seededRaw);
        g_state.busy = false;
        g_state.opStatus = OperationStatus::COMPLETED;
        setErrorText("");
        Serial.println(g_state.opType == OperationType::STARTUP_ZERO
                       ? "BILGI: Acilis bos platform sifirlamasi tamamlandi."
                       : "BILGI: Dara islemi tamamlandi.");
        return;
    }

    if (g_state.opType == OperationType::CALIBRATE) {
        const double rawDiff = avgRaw - g_state.rawZeroOffset;
        if (std::fabs(rawDiff) < MIN_CALIBRATION_RAW_DIFF || g_state.opTargetKnownGrams <= 0.0f) {
            g_state.busy = false;
            g_state.opStatus = OperationStatus::FAILED;
            setErrorText("Kalibrasyon hatasi: yuk farki yetersiz (sifir fark).");
            seedFilterWindow(g_state.lastRawSample);
            Serial.println("HATA: Kalibrasyon yuk farki yetersiz, onceki ayar korundu.");
            return;
        }

        const float candidateK = static_cast<float>(rawDiff / static_cast<double>(g_state.opTargetKnownGrams));
        if (!isValidScaleFactor(candidateK)) {
            g_state.busy = false;
            g_state.opStatus = OperationStatus::FAILED;
            setErrorText("Kalibrasyon hatasi: gecersiz katsayi hesaplandi.");
            seedFilterWindow(g_state.lastRawSample);
            return;
        }

        // Kalıcı NVS belleğe yazmayı dene; başarısız olursa önceki geçerli K korunur!
        if (!saveCalibrationToNvs(candidateK)) {
            g_state.busy = false;
            g_state.opStatus = OperationStatus::FAILED;
            setErrorText("NVS yazma hatasi; onceki kalibrasyon korundu.");
            seedFilterWindow(g_state.lastRawSample);
            Serial.println("HATA: NVS yazma basarisiz! Onceki kalibrasyon korundu.");
            return;
        }

        g_state.scaleFactor = candidateK;
        g_state.calibrated = true;
        seedFilterWindow(seededRaw);
        g_state.busy = false;
        g_state.opStatus = OperationStatus::COMPLETED;
        setErrorText("");
        Serial.println("BILGI: Kalibrasyon tamamlandi ve NVS bellege kaydedildi.");
    }
}

static void abortUserOperationOnSensorFault(const char* reason) {
    setErrorText(reason);
    if (!g_state.busy) {
        return;
    }
    if (g_state.opType == OperationType::STARTUP_ZERO) {
        // Açılış sıfırlaması sırasında sensör henüz hazır değilse birikimi sıfırla,
        // sensör geldiğinde 3 ardışık sağlam örnekle otomatik tamamlanmasını sağla.
        g_state.opCollectedSamples = 0;
        g_state.opAccumSum = 0.0;
    } else {
        // Kullanıcı dara veya kalibrasyon isteği sırasında sensör koptuysa işlemi başarısız sonlandır.
        g_state.busy = false;
        g_state.opStatus = OperationStatus::FAILED;
    }
}

static void pollHx711NonBlocking(unsigned long nowMs) {
    if (scale.is_ready()) {
        // Yalnızca is_ready() true olduğunda tekil read() çağrılır (bloklayan read_average/tare kullanılmaz)
        const long rawSample = scale.read();

        if (!isValidAdcSample(rawSample)) {
            g_state.sensorOk = false;
            clearFilterWindow();
            abortUserOperationOnSensorFault("HX711 ADC doyma (saturasyon) veya gecersiz veri hatasi.");
            Serial.println("HATA: HX711 doyma (saturasyon) degeri okundu.");
            return;
        }

        // Yeni örneği kaydetmeden önce, önceki geçerli örnekten bu yana zaman aşımı (stale gap >= READY_TIMEOUT_MS)
        // oluşup oluşmadığını veya sensörün hata/zaman aşımı durumundan yeni çıkıp çıkmadığını kontrol et.
        const bool hadStaleGap = g_state.hasEverReadValidSample &&
                                 ((unsigned long)(nowMs - g_state.lastValidSampleMs) >= READY_TIMEOUT_MS);
        const bool recoveringFromFault = (!g_state.sensorOk) || g_state.inTimeoutError || hadStaleGap;

        if (hadStaleGap && g_state.busy) {
            // Uzun boşluk sonrasında ilk loop'ta örnek geldiyse de zaman aşımına uğrayan işlemi ele al
            abortUserOperationOnSensorFault("HX711 sensor zaman asimi; baglantiyi kontrol edin.");
        }

        if (recoveringFromFault) {
            // Eski dönemden kalan filtre örneklerinin yeni zaman dönemiyle karışmasını engelle!
            clearFilterWindow();
        }

        if (g_state.inTimeoutError) {
            Serial.println("BILGI: HX711 veri akisi normale dondu.");
            g_state.inTimeoutError = false;
        }

        g_state.hasEverReadValidSample = true;
        g_state.sensorOk = true;
        g_state.lastValidSampleMs = nowMs;
        g_state.lastRawSample = rawSample;
        g_state.sampleSeq++;

        if (g_state.busy) {
            if (static_cast<unsigned long>(nowMs - g_state.opStartMs) >= g_state.opSettleMs) {
                if (g_state.opCollectedSamples == 0 || rawSample < g_state.opMinRaw) g_state.opMinRaw = rawSample;
                if (g_state.opCollectedSamples == 0 || rawSample > g_state.opMaxRaw) g_state.opMaxRaw = rawSample;
                g_state.opAccumSum += static_cast<double>(rawSample);
                g_state.opCollectedSamples++;
                if (g_state.opCollectedSamples >= g_state.opTargetSamples) {
                    finalizeActiveOperation();
                }
            }
        } else {
            pushFilterSample(rawSample);
            // Eğer daha önce sensör zaman aşımı mesajı kalmışsa ve işlem hatası yoksa temizle
            if (g_state.opStatus != OperationStatus::FAILED) {
                setErrorText("");
            }
        }

        // USB teşhisi: yalnız yeni örneği ve ekran yuvarlaması öncesi net gramı yaz.
        // Dara/kalibrasyon sürerken ya da katsayı yokken sahte gram üretme.
        double netGrams = 0.0;
        char diagnostic[112];
        if (computeNetGrams(netGrams)) {
            // LCD ile tablet aynı tam gramı kullanır. Geçiş sınırına her yönde 0,20 g ekle.
            constexpr double transitionG = 0.70;
            if (!g_state.hasDisplayGrams || netGrams >= g_state.displayGrams + transitionG ||
                netGrams <= g_state.displayGrams - transitionG) {
                const double rounded = std::round(netGrams);
                g_state.displayGrams = rounded == 0.0 ? 0.0 : rounded;
                g_state.hasDisplayGrams = true;
            }
            snprintf(diagnostic, sizeof(diagnostic), "TERAZI ms=%lu raw=%ld net_g=%.3f display_g=%.0f",
                     nowMs, rawSample, netGrams, g_state.displayGrams);
        } else {
            snprintf(diagnostic, sizeof(diagnostic), "TERAZI ms=%lu raw=%ld net_g=NA", nowMs, rawSample);
        }
        Serial.println(diagnostic);
        return;
    }

    // Sensör hazır değilken zaman aşımı kontrolü (32-bit unsigned çıkarma ile millis taşmasına dayanıklı)
    const unsigned long refMs = g_state.hasEverReadValidSample ? g_state.lastValidSampleMs : g_state.bootMs;
    if ((unsigned long)(nowMs - refMs) >= READY_TIMEOUT_MS) {
        g_state.sensorOk = false;
        clearFilterWindow();
        abortUserOperationOnSensorFault("HX711 sensor zaman asimi; baglantiyi kontrol edin.");

        if (!g_state.inTimeoutError || (unsigned long)(nowMs - g_state.lastErrorReportMs) >= ERROR_REPORT_INTERVAL_MS) {
            Serial.println("UYARI: HX711 hazir degil (DOUT=HIGH), baglantiyi kontrol edin.");
            g_state.lastErrorReportMs = nowMs;
            g_state.inTimeoutError = true;
        }
    }
}

static void pollPhysicalTareButton(unsigned long nowMs) {
    const int rawBtn = digitalRead(PIN_TARE_BUTTON);
    if (rawBtn != g_state.lastRawButtonState) {
        g_state.lastRawButtonState = rawBtn;
        g_state.lastButtonEdgeMs = nowMs;
    }

    if ((unsigned long)(nowMs - g_state.lastButtonEdgeMs) >= BUTTON_DEBOUNCE_MS) {
        if (rawBtn != g_state.stableButtonState) {
            g_state.stableButtonState = rawBtn;
            // Yalnız kararlı bir basıştan sonraki bırakış dara alabilir.
            if (g_state.stableButtonState == LOW) {
                g_state.tareButtonArmed = g_state.sensorOk && !g_state.busy;
                if (g_state.tareButtonArmed) Serial.println("BILGI: Fiziksel dara butonuna basildi; birakilmasi bekleniyor.");
            } else if (g_state.tareButtonArmed) {
                g_state.tareButtonArmed = false;
                if (g_state.sensorOk && !g_state.busy) {
                    startSamplingOperation(OperationType::TARE, TARE_SAMPLE_COUNT, 0.0f, nowMs, PHYSICAL_TARE_SETTLE_MS);
                    Serial.println("BILGI: Fiziksel dara butonu birakildi; oturma beklemesi basladi.");
                } else {
                    Serial.println("UYARI: Sensor hazir degilken veya islem surerken fiziksel dara reddedildi.");
                }
            }
        }
    }
}

// Test ortamında statik durumu temizlemek için yardımcı sıfırlayıcı
void resetFirmwareStateForTest() {
    g_state = RuntimeState();
}

// ============================================================================
// BÖLÜM 7: ARDUINO STANDART GİRİŞ NOKTALARI (setup & loop)
// ============================================================================

void setup() {
    const unsigned long nowMs = millis();
    g_state = RuntimeState();
    g_state.bootMs = nowMs;
    g_state.lastButtonEdgeMs = nowMs;

    Serial.begin(SERIAL_BAUD);
    Serial.println("BILGI: Hassas Terazi baslatiliyor. Lutfen acilista platformu BOS birakin.");

    // Fiziksel dara butonu girişi
    pinMode(PIN_TARE_BUTTON, INPUT_PULLUP);
    g_state.lastRawButtonState = digitalRead(PIN_TARE_BUTTON);
    g_state.stableButtonState = g_state.lastRawButtonState;

    // HX711 başlatma (bloklayan okuma yapılmaz)
    scale.begin(PIN_HX711_DOUT, PIN_HX711_SCK, HX711_GAIN_CHANNEL_A);

    // Kayıtlı kalibrasyon katsayısını (K) NVS'den yükle
    loadCalibrationFromNvs();

    // Açılışta boş platform sıfırlamasını loop() içinde bloklamadan toplamak üzere başlat
    startSamplingOperation(OperationType::STARTUP_ZERO, STARTUP_ZERO_SAMPLE_COUNT, 0.0f, nowMs);

    // I2C ve 20x4 LCD başlatma (LCD takılı olmasa bile sistem bloklamadan devam eder)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    lcd.begin(LCD_COLS, LCD_ROWS, Wire);
    if (lcd.isConnected()) {
        lcd.setBacklight(LCD_BACKLIGHT_ON);
        g_state.lcdInitialized = true;
    } else {
        g_state.lcdInitialized = false;
        Serial.println("UYARI: 20x4 I2C LCD algilanamadi; web ve olcum calismaya devam ediyor.");
    }

    // Wi-Fi SoftAP ve 4 HTTP rotasını başlat (dönüş değerini kontrol et; başarısızsa sahte hazır/IP basma)
    const bool modeOk = WiFi.mode(WIFI_AP);
    const bool apOk = modeOk && WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    g_state.wifiApOk = apOk;

    server.on("/", HTTP_GET, handleGetRoot);
    server.on("/api/weight", HTTP_GET, handleGetWeight);
    server.on("/api/tare", HTTP_POST, handlePostTare);
    server.on("/api/calibrate", HTTP_POST, handlePostCalibrate);
    server.onNotFound(handleNotFound);
    server.begin();

    if (g_state.wifiApOk) {
        Serial.print("BILGI: Wi-Fi AP hazir, IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("HATA: Wi-Fi AP baslatilamadi! Donanim olcumu yerel olarak devam ediyor.");
    }

    if (g_state.lcdInitialized) {
        updateLcdIfDue(nowMs + LCD_UPDATE_INTERVAL_MS);
    }
}

void loop() {
    const unsigned long nowMs = millis();

    // 1. HX711 hazırsa tekil örnek oku ve aktif işlemi / filtreyi ilerlet
    pollHx711NonBlocking(nowMs);

    // 2. Fiziksel dara butonunu debounce ile denetle
    pollPhysicalTareButton(nowMs);

    // 3. HTTP istemci isteklerini işle
    server.handleClient();

    // 4. 20x4 LCD ekranı belirli aralıklarla titreşimsiz yenile
    updateLcdIfDue(nowMs);
}
