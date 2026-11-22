#!/usr/bin/env node
/**
 * ============================================================================
 * Tablet HTML -> ESP32 PROGMEM C++ Başlık Üreticisi ve Güncellik Denetleyicisi
 * ----------------------------------------------------------------------------
 * Amaç:
 *   tablet/index.html dosyasını tek doğruluk kaynağı (single source of truth)
 *   olarak okuyup firmware/webui_html.h içine C++11 raw string literal ve
 *   PROGMEM dizisi olarak gömer. Elle iki ayrı HTML kopyası yönetme hatasını
 *   ortadan kaldırır.
 *
 * Kullanım:
 *   Üretim / Güncelleme : node firmware/tools/generate_webui_header.cjs
 *   Güncellik Kontrolü  : node firmware/tools/generate_webui_header.cjs --check
 * ============================================================================
 */

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const REPO_ROOT = path.resolve(__dirname, '..', '..');
const DEFAULT_HTML_PATH = path.join(REPO_ROOT, 'tablet', 'index.html');
const DEFAULT_HEADER_PATH = path.join(REPO_ROOT, 'firmware', 'webui_html.h');
const RAW_DELIMITER = 'TERAZI_HTML';

function buildHeaderContent(rawHtml, delimiter = RAW_DELIMITER) {
  if (typeof rawHtml !== 'string' || rawHtml.length === 0) {
    throw new Error('Girdi HTML metni boş veya geçersiz.');
  }
  if (!/^[A-Za-z0-9_]{1,16}$/.test(delimiter)) {
    throw new Error(`Geçersiz C++ raw literal sınırlayıcısı (delimiter): ${delimiter}`);
  }

  const closingToken = `)${delimiter}"`;
  if (rawHtml.includes(closingToken)) {
    throw new Error(
      `Güvenlik hatası: HTML içeriği C++ raw literal kapanış dizisini (${closingToken}) içeriyor!`
    );
  }

  // Platformlar arası deterministik çıktı için satır sonlarını LF (\n) olarak normalize et
  const normalizedHtml = rawHtml.replace(/\r\n/g, '\n');
  const sha256 = crypto.createHash('sha256').update(normalizedHtml, 'utf8').digest('hex');
  const byteLength = Buffer.byteLength(normalizedHtml, 'utf8');

  return [
    '// ============================================================================',
    '// OTOMATİK ÜRETİLEN DOSYA — ELLE DÜZENLEMEYİN',
    '// Kaynak  : tablet/index.html',
    '// Üretici : node firmware/tools/generate_webui_header.cjs',
    `// SHA-256 : ${sha256}`,
    `// Boyut   : ${byteLength} bayt`,
    '// ============================================================================',
    '#ifndef TERAZI_WEBUI_HTML_H',
    '#define TERAZI_WEBUI_HTML_H',
    '',
    '#include <Arduino.h>',
    '#include <stddef.h>',
    '',
    `constexpr size_t TABLET_INDEX_HTML_LEN = ${byteLength}U;`,
    '',
    `const char TABLET_INDEX_HTML[] PROGMEM = R"${delimiter}(${normalizedHtml})${delimiter}";`,
    '',
    '#endif // TERAZI_WEBUI_HTML_H',
    ''
  ].join('\n');
}

function generateOrCheck(options = {}) {
  const htmlPath = options.htmlPath || DEFAULT_HTML_PATH;
  const headerPath = options.headerPath || DEFAULT_HEADER_PATH;
  const checkOnly = Boolean(options.checkOnly);
  const delimiter = options.delimiter || RAW_DELIMITER;

  if (!fs.existsSync(htmlPath)) {
    throw new Error(`Kaynak HTML dosyası bulunamadı: ${htmlPath}`);
  }

  const rawHtml = fs.readFileSync(htmlPath, 'utf8');
  const expectedHeader = buildHeaderContent(rawHtml, delimiter);

  if (checkOnly) {
    if (!fs.existsSync(headerPath)) {
      return {
        ok: false,
        reason: `Hedef başlık dosyası mevcut değil: ${headerPath}`
      };
    }
    const currentHeader = fs.readFileSync(headerPath, 'utf8').replace(/\r\n/g, '\n');
    if (currentHeader !== expectedHeader) {
      return {
        ok: false,
        reason: 'firmware/webui_html.h dosyası tablet/index.html ile güncel değil. Lütfen "node firmware/tools/generate_webui_header.cjs" komutunu çalıştırın.'
      };
    }
    return { ok: true, reason: 'firmware/webui_html.h güncel ve delimiter güvenli.' };
  }

  fs.mkdirSync(path.dirname(headerPath), { recursive: true });
  fs.writeFileSync(headerPath, expectedHeader, 'utf8');
  return { ok: true, reason: `firmware/webui_html.h üretildi (${Buffer.byteLength(expectedHeader, 'utf8')} bayt).` };
}

if (require.main === module) {
  const checkOnly = process.argv.includes('--check');
  try {
    const result = generateOrCheck({ checkOnly });
    if (!result.ok) {
      console.error(`[HATA] ${result.reason}`);
      process.exit(1);
    }
    console.log(`[TAMAM] ${result.reason}`);
  } catch (err) {
    console.error(`[HATA] ${err.message}`);
    process.exit(1);
  }
}

module.exports = {
  RAW_DELIMITER,
  buildHeaderContent,
  generateOrCheck
};
