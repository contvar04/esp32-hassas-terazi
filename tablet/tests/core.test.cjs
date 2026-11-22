const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const htmlPath = path.join(__dirname, '..', 'index.html');

function loadTabletSandbox(options = {}) {
  const html = fs.readFileSync(htmlPath, 'utf8');
  const scripts = [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/gi)].map(m => m[1]);

  if (!options.withDom) {
    const sandbox = { console, Math, Number, Date, setInterval, clearInterval, setTimeout, clearTimeout };
    for (const script of scripts) vm.runInNewContext(script, sandbox);
    return { sandbox, core: sandbox.TeraziCore };
  }

  let nowMs = options.startMs ?? 100000;
  let nextTimerId = 1;
  const timers = new Map();
  const intervals = new Map();

  function makeElement(id, tag = 'div') {
    const listeners = {};
    return {
      id,
      tagName: tag.toUpperCase(),
      textContent: '',
      value: options.initialValues?.[id] ?? '',
      disabled: false,
      hidden: false,
      open: false,
      className: '',
      dataset: {},
      style: {},
      addEventListener(type, fn) {
        listeners[type] = listeners[type] || [];
        listeners[type].push(fn);
      },
      trigger(type, eventInit = {}) {
        const list = listeners[type] || [];
        for (const fn of list) {
          fn({ target: this, preventDefault() {}, ...eventInit });
        }
      },
      getBoundingClientRect() {
        return { width: 600, height: 240 };
      },
      getContext() {
        return {
          scale() {},
          beginPath() {},
          moveTo() {},
          lineTo() {},
          stroke() {},
          fillText() {},
          clearRect() {}
        };
      }
    };
  }

  const elementIds = [
    'modeBadge', 'brandSub', 'pageCaption', 'status', 'weight', 'unitLabel',
    'weightNote', 'tare', 'unit', 'chart', 'demoLoadPanel', 'load', 'loadValue',
    'demoStateSection', 'state', 'calibrationDetails', 'calibrationSummary',
    'calibrationHelp', 'known', 'calibrate', 'feedback', 'footerNote'
  ];

  const elements = {};
  for (const id of elementIds) {
    elements[id] = makeElement(id);
  }
  elements.unit.value = 'g';
  elements.state.value = 'normal';
  elements.load.value = '0';

  const presetButtons = [0, 500, 1000].map(val => {
    const btn = makeElement(`preset_${val}`, 'button');
    btn.dataset.load = String(val);
    return btn;
  });

  const windowListeners = {};
  const documentObj = {
    getElementById(id) {
      if (!elements[id]) elements[id] = makeElement(id);
      return elements[id];
    },
    querySelectorAll(sel) {
      if (sel === '[data-load]') return presetButtons;
      return [];
    }
  };

  class FakeAbortController {
    constructor() {
      this.aborted = false;
      const listeners = [];
      this.signal = {
        aborted: false,
        addEventListener(type, fn) {
          if (type === 'abort') listeners.push(fn);
        }
      };
      this._listeners = listeners;
    }
    abort() {
      if (this.aborted) return;
      this.aborted = true;
      this.signal.aborted = true;
      for (const fn of this._listeners) fn();
    }
  }

  const fetchCalls = [];
  const fetchImpl = options.fetch || (async () => {
    throw new Error('Unexpected fetch call');
  });

  function wrappedFetch(url, init = {}) {
    const call = { url, init, time: nowMs };
    fetchCalls.push(call);
    return fetchImpl(url, init, call);
  }

  const locationObj = {
    protocol: options.protocol || 'file:',
    origin: options.origin || 'null',
    href: options.href || 'file:///tablet/index.html'
  };

  const sandbox = {
    console,
    Math,
    Number,
    Date: {
      now: () => nowMs
    },
    Intl,
    AbortController: FakeAbortController,
    location: locationObj,
    window: {
      location: locationObj,
      devicePixelRatio: 1,
      addEventListener(type, fn) {
        windowListeners[type] = windowListeners[type] || [];
        windowListeners[type].push(fn);
      }
    },
    document: documentObj,
    fetch: wrappedFetch,
    setTimeout(fn, ms) {
      const id = nextTimerId++;
      timers.set(id, { id, fn, due: nowMs + Math.max(0, Number(ms) || 0) });
      return id;
    },
    clearTimeout(id) {
      timers.delete(id);
    },
    setInterval(fn, ms) {
      const id = nextTimerId++;
      intervals.set(id, { id, fn, interval: Math.max(1, Number(ms) || 500), nextDue: nowMs + Math.max(1, Number(ms) || 500) });
      return id;
    },
    clearInterval(id) {
      intervals.delete(id);
    }
  };

  for (const script of scripts) {
    vm.runInNewContext(script, sandbox);
  }

  async function flushMicrotasks(rounds = 6) {
    for (let i = 0; i < rounds; i++) {
      await Promise.resolve();
    }
  }

  async function advanceTime(deltaMs) {
    const target = nowMs + deltaMs;
    while (true) {
      let nextDue = Infinity;
      let nextTask = null;

      for (const t of timers.values()) {
        if (t.due <= target && t.due < nextDue) {
          nextDue = t.due;
          nextTask = { kind: 'timer', item: t };
        }
      }
      for (const iv of intervals.values()) {
        if (iv.nextDue <= target && iv.nextDue < nextDue) {
          nextDue = iv.nextDue;
          nextTask = { kind: 'interval', item: iv };
        }
      }

      if (!nextTask) break;
      nowMs = nextDue;
      if (nextTask.kind === 'timer') {
        timers.delete(nextTask.item.id);
        nextTask.item.fn();
      } else {
        nextTask.item.nextDue = nowMs + nextTask.item.interval;
        nextTask.item.fn();
      }
      await flushMicrotasks();
    }
    nowMs = target;
    await flushMicrotasks();
  }

  return {
    sandbox,
    core: sandbox.TeraziCore,
    elements,
    presetButtons,
    fetchCalls,
    getNow: () => nowMs,
    setNow: (ms) => { nowMs = ms; },
    flushMicrotasks,
    advanceTime
  };
}

const { core } = loadTabletSandbox();

test('gramı kg ve Newton birimlerine dönüştürür', () => {
  const v = core.convert(1250); assert.equal(v.g, 1250); assert.equal(v.kg, 1.25); assert.ok(Math.abs(v.n - 12.2583125) < 1e-10);
});

// Ham sıfır ve dara ayrı kavramlardır; negatif net yük korunur.
test('ham veriden net kütle ve dara hesaplar', () => {
  assert.equal(core.mass(1200, 200, 0.5, 100), 400);
  assert.equal(core.mass(200, 200, 0.5, 100), -100);
  assert.equal(core.tare(1200, 200, 0.5), 500);
});

test('kalibrasyon işaretli sensör kazancını destekler', () => {
  assert.equal(core.calibrate(1000, -500, 0), -2);
});
test('geçersiz kütle ve yüksüz kalibrasyonu reddeder', () => {
  for (const value of [0, -1, NaN, Infinity, '', 'abc']) assert.equal(core.calibrate(value, 100, 0), null);
  assert.equal(core.calibrate(100, 0, 0), null);
});
test('geçersiz durumu reddeder', () => {
  assert.equal(core.validState('normal'), true);
  assert.equal(core.validState('yok'), true);
  assert.equal(core.validState('kopuk'), true);
  assert.equal(core.validState('bilinmeyen'), false);
});
test('geçmiş en fazla belirlenen sayıda örnek tutar', () => {
  let history = [];
  for (let i=0; i<5; i++) history = core.pushHistory(history, i, 3);
  assert.deepEqual(Array.from(history), [2, 3, 4]);
});

test('protokole göre DEMO ve CANLI modu doğru ayırır, demo kontrollerini canlı modda gizler', async () => {
  assert.equal(typeof core.detectMode, 'function');
  assert.equal(core.detectMode('file:'), 'demo');
  assert.equal(core.detectMode('http:'), 'live');
  assert.equal(core.detectMode('https:'), 'live');

  const demoEnv = loadTabletSandbox({ withDom: true, protocol: 'file:' });
  await demoEnv.flushMicrotasks();
  assert.equal(demoEnv.fetchCalls.length, 0);
  assert.equal(demoEnv.elements.demoLoadPanel.hidden, false);
  assert.equal(demoEnv.elements.demoStateSection.hidden, false);
  assert.match(demoEnv.elements.modeBadge.textContent, /Demo/i);

  const liveEnv = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: async () => ({
      ok: true,
      status: 200,
      json: async () => ({ grams: 250.4, calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: 40 })
    })
  });
  await liveEnv.flushMicrotasks();
  assert.equal(liveEnv.elements.demoLoadPanel.hidden, true);
  assert.equal(liveEnv.elements.demoStateSection.hidden, true);
  assert.match(liveEnv.elements.modeBadge.textContent, /Canlı|Gerçek/i);
  assert.equal(liveEnv.fetchCalls.length, 1);
  assert.equal(liveEnv.fetchCalls[0].url, '/api/weight');
  assert.equal(liveEnv.fetchCalls[0].init.cache, 'no-store');
});

test('canlı snapshot doğrulaması tür, finite, stale, negatif net ve birim dönüşümünü denetler', async () => {
  assert.equal(typeof core.validateSnapshot, 'function');
  assert.equal(core.validateSnapshot(null).valid, false);
  assert.equal(core.validateSnapshot({ grams: NaN, calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: 10 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: Infinity, calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: 10 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: '100', calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: 10 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: 10, calibrated: 'yes', sensor_ok: true, seq: 1, sample_age_ms: 10 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: 10, calibrated: true, sensor_ok: true, seq: -1, sample_age_ms: 10 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: 10, calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: -5 }).valid, false);
  assert.equal(core.validateSnapshot({ grams: -125.5, calibrated: true, sensor_ok: true, seq: 3, sample_age_ms: 80 }).valid, true);

  let nextSnap = { grams: -125.5, calibrated: true, sensor_ok: true, seq: 10, sample_age_ms: 50 };
  const env = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: async () => ({
      ok: true,
      status: 200,
      json: async () => nextSnap
    })
  });
  await env.flushMicrotasks();
  // Sıfıra yuvarlanmayan negatif net değer ekranda ve geçmişte korunur.
  assert.equal(env.elements.weight.textContent, '-126');
  assert.equal(env.sandbox.TeraziLiveState.history.at(-1).value, -125.5);
  env.elements.unit.value = 'kg';
  env.elements.unit.trigger('change');
  assert.equal(env.elements.weight.textContent, '-0,126');
  env.elements.unit.value = 'n';
  env.elements.unit.trigger('change');
  assert.equal(env.elements.weight.textContent, '-1,236');
  nextSnap = { ...nextSnap, grams: -0.02, seq: 11 };
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '0,000');
  for (const [unit, expected] of [['kg', '0,000'], ['g', '0']]) {
    env.elements.unit.value = unit;
    env.elements.unit.trigger('change');
    assert.equal(env.elements.weight.textContent, expected);
  }
  env.elements.unit.value = 'n';
  env.elements.unit.trigger('change');
  nextSnap = { ...nextSnap, grams: 125.5, seq: 12 };
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '1,236');
});

test('g, kg ve N göstergeleri ortak ESP32 gramını, grafik ise kesirli ölçümü kullanır', async () => {
  const env = loadTabletSandbox({ withDom: true, protocol: 'http:', fetch: async () => ({
    ok: true, status: 200, json: async () => ({ grams: 67.36, display_grams: 68, calibrated: true,
      sensor_ok: true, seq: 20, sample_age_ms: 20 })
  }) });
  await env.flushMicrotasks();
  assert.equal(env.elements.weight.textContent, '68');
  assert.equal(env.sandbox.TeraziLiveState.history.at(-1).value, 67.36);
  env.elements.unit.value = 'kg';
  env.elements.unit.trigger('change');
  assert.equal(env.elements.weight.textContent, '0,068');
  env.elements.unit.value = 'n';
  env.elements.unit.trigger('change');
  assert.equal(env.elements.weight.textContent, '0,667');
});

test('tekrarlı seq, stale veri, sensör yok, kalibrasyonsuz, kopma, reboot seq sıfırlama ve reconnect grafik/durum geçişlerini yönetir', async () => {
  const responses = [
    // 1. İlk geçerli ölçüm (seq 5)
    { ok: true, status: 200, body: { grams: 100.0, calibrated: true, sensor_ok: true, seq: 5, sample_age_ms: 40 } },
    // 2. Tekrarlı seq (seq 5) -> grafiğe yeni nokta eklenmemeli
    { ok: true, status: 200, body: { grams: 100.0, calibrated: true, sensor_ok: true, seq: 5, sample_age_ms: 420 } },
    // 3. Stale örnek (sample_age_ms: 2500) -> eski değer gerçek diye sunulmamalı, grafikte gap (null)
    { ok: true, status: 200, body: { grams: 100.0, calibrated: true, sensor_ok: true, seq: 5, sample_age_ms: 2500 } },
    // 4. Sensör koptu (sensor_ok: false)
    { ok: true, status: 200, body: { grams: null, calibrated: true, sensor_ok: false, seq: 5, sample_age_ms: 3000 } },
    // 5. Kalibrasyonsuz durum (calibrated: false)
    { ok: true, status: 200, body: { grams: null, calibrated: false, sensor_ok: true, seq: 6, sample_age_ms: 30 } },
    // 6. Ağ hatası / kopma -> sahte demoya geçmemeli
    { networkError: true },
    // 7. ESP32 reboot oldu: seq 0 (henüz örnek yok)
    { ok: true, status: 200, body: { grams: null, calibrated: true, sensor_ok: false, seq: 0, sample_age_ms: null } },
    // 8. Reboot sonrası ilk yeni örnek (seq 1 < eski seq 6) -> kabul edilmeli ve görüntülenmeli
    { ok: true, status: 200, body: { grams: 500.0, calibrated: true, sensor_ok: true, seq: 1, sample_age_ms: 25 } }
  ];

  let idx = 0;
  const env = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: async () => {
      const item = responses[Math.min(idx++, responses.length - 1)];
      if (item.networkError) throw new Error('Network down');
      return {
        ok: item.ok,
        status: item.status,
        json: async () => item.body
      };
    }
  });

  // 1. seq 5
  await env.flushMicrotasks();
  assert.equal(env.elements.weight.textContent, '100');
  const histLenAfterFirst = env.sandbox.TeraziLiveState.history.length;
  assert.equal(histLenAfterFirst, 1);
  assert.equal(env.sandbox.TeraziLiveState.history[0].value, 100.0);

  // 2. Tekrarlı seq 5 -> yeni fiziksel ölçüm yok, grafiğe taze örnek eklenmez
  await env.advanceTime(500);
  assert.equal(env.sandbox.TeraziLiveState.history.length, 1);

  // 3. Stale (2500 ms) -> ağırlık '—' olur, grafiğe null (gap) eklenir
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '—');
  assert.equal(env.sandbox.TeraziLiveState.history.at(-1).value, null);

  // 4. Sensör yok (sensor_ok: false) -> dara ve kalibrasyon devre dışı
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '—');
  assert.equal(env.elements.tare.disabled, true);
  assert.equal(env.elements.calibrate.disabled, true);

  // 5. Kalibrasyonsuz (sensor_ok: true, calibrated: false) -> ağırlık '—', fakat dara/kalibre butonları açık
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '—');
  assert.equal(env.elements.tare.disabled, false);
  assert.equal(env.elements.calibrate.disabled, false);

  // 6. Ağ kopması -> sahte demoya geçmez, demo kontrolleri gizli kalır
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '—');
  assert.equal(env.elements.demoLoadPanel.hidden, true);
  assert.equal(env.sandbox.TeraziLiveState.mode, 'live');

  // 7. Reboot seq 0 -> ağırlık '—'
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '—');

  // Reboot sonrası seq 1 (500 g) -> reconnect ve yeni ölçüm kabul edilir
  await env.advanceTime(500);
  assert.equal(env.elements.weight.textContent, '500');
  assert.equal(env.sandbox.TeraziLiveState.history.at(-1).value, 500.0);
});

test('reboot veya sıra gerilemesinde bekleyen (pending) dara/kalibrasyon komutunu kesin iptal eder ve yeni cihaz işlemiyle eşlemez', async () => {
  let snap = {
    grams: 100.0,
    calibrated: true,
    sensor_ok: true,
    seq: 100,
    sample_age_ms: 10,
    busy: false,
    operation: 'none',
    op_status: 'idle',
    op_id: 0,
    error: null
  };

  const env = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: async (url) => {
      if (url === '/api/tare') {
        return {
          ok: true,
          status: 202,
          json: async () => ({ ok: true, status: 'accepted', operation: 'tare', busy: true, op_id: 2 })
        };
      }
      return {
        ok: true,
        status: 200,
        json: async () => ({ ...snap })
      };
    }
  });

  // 1. Başlangıç (seq 100) ve Dara al tıklaması (pendingOp: { type: 'tare', opId: 2 })
  await env.flushMicrotasks();
  assert.equal(env.elements.weight.textContent, '100');
  env.elements.tare.trigger('click');
  await env.flushMicrotasks();
  assert.match(env.elements.feedback.textContent, /Dara kabul edildi/i);
  assert.equal(env.sandbox.TeraziLiveState.pendingOp?.type, 'tare');
  assert.equal(env.sandbox.TeraziLiveState.pendingOp?.opId, 2);

  // 2. Senaryo A: Cihaz yeniden başladı (seq 100 -> seq 4) ve yeni oturumda tesadüfen aynı tip + op_id=2 ile busy durumda!
  // Eski pendingOp kesinlikle iptal edilmeli, ardından yeni işlem tamamlandığında "Dara sıfırlandı." YAZMAMALI!
  snap = {
    ...snap,
    seq: 4,
    busy: true,
    operation: 'tare',
    op_status: 'busy',
    op_id: 2,
    grams: null
  };
  await env.advanceTime(500);
  assert.equal(env.sandbox.TeraziLiveState.pendingOp, null);
  assert.match(env.elements.feedback.textContent, /yeniden başlatıldı/i);

  snap = {
    ...snap,
    seq: 7,
    busy: false,
    operation: 'tare',
    op_status: 'completed',
    op_id: 2,
    grams: 0.0
  };
  await env.advanceTime(500);
  assert.doesNotMatch(env.elements.feedback.textContent, /Dara sıfırlandı/i);
  assert.match(env.elements.feedback.textContent, /yeniden başlatıldı/i);
  assert.equal(env.elements.weight.textContent, '0');

  // 3. Senaryo B: Yeni bir dara başlatıldı (seq 50 -> op_id 2), cihaz reboot oldu ve ilk görülen reboot snapshot'ı
  // zaten op_status='completed', operation='tare', op_id=2 (seq 5 < 50) olarak geldi -> yine yanlış başarı olmamalı!
  snap = {
    ...snap,
    seq: 50,
    busy: false,
    operation: 'none',
    op_status: 'idle',
    op_id: 1,
    grams: 120.0
  };
  await env.advanceTime(500);
  env.elements.tare.trigger('click');
  await env.flushMicrotasks();
  assert.equal(env.sandbox.TeraziLiveState.pendingOp?.type, 'tare');
  assert.equal(env.sandbox.TeraziLiveState.pendingOp?.opId, 2);

  snap = {
    ...snap,
    seq: 5,
    busy: false,
    operation: 'tare',
    op_status: 'completed',
    op_id: 2,
    grams: 0.0
  };
  await env.advanceTime(500);
  assert.equal(env.sandbox.TeraziLiveState.pendingOp, null);
  assert.doesNotMatch(env.elements.feedback.textContent, /Dara sıfırlandı/i);
  assert.match(env.elements.feedback.textContent, /yeniden başlatıldı/i);
});

test('sıralı polling çakışmaz, timeout uygular; POST 202 yanıtını bitmiş başarı sanmaz ve GET sonucunu bekler', async () => {
  let currentSnapshot = {
    grams: 200.0,
    calibrated: true,
    sensor_ok: true,
    seq: 1,
    sample_age_ms: 20,
    busy: false,
    operation: 'none',
    op_status: 'idle',
    op_id: 0,
    error: null
  };
  let postCount = 0;

  const env = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: async (url, init) => {
      if (url === '/api/weight') {
        return {
          ok: true,
          status: 200,
          json: async () => ({ ...currentSnapshot })
        };
      }
      if (url === '/api/calibrate') {
        postCount++;
        // 202 Accepted: henüz bitmedi, örnek toplanıyor
        currentSnapshot = {
          grams: null,
          calibrated: true,
          sensor_ok: true,
          seq: 1,
          sample_age_ms: 30,
          busy: true,
          operation: 'calibrate',
          op_status: 'busy',
          op_id: 7,
          error: null
        };
        return {
          ok: true,
          status: 202,
          json: async () => ({ ok: true, status: 'accepted', operation: 'calibrate', busy: true, op_id: 7 })
        };
      }
      throw new Error('Unexpected URL: ' + url);
    }
  });

  await env.flushMicrotasks();
  assert.equal(env.elements.weight.textContent, '200');

  // Geçersiz bilinen kütle (ör. 6000 g > 5000 g kapasite) -> POST gönderilmez
  env.elements.known.value = '6000';
  env.elements.calibrate.trigger('click');
  await env.flushMicrotasks();
  assert.equal(postCount, 0);

  // Geçerli bilinen kütle (1000 g) -> POST /api/calibrate
  env.elements.known.value = '1000';
  env.elements.calibrate.trigger('click');
  // Çift tıklama koruması: işlem sürerken ikinci tıklama yok sayılır
  env.elements.calibrate.trigger('click');
  await env.flushMicrotasks();
  assert.equal(postCount, 1);

  // 202 Accepted geldiğinde tamamlandı/kaydedildi denmemeli!
  assert.doesNotMatch(env.elements.feedback.textContent, /tamamlandı|kaydedildi/i);
  assert.equal(env.elements.calibrate.disabled, true);

  // Sonraki GET poll: hâlâ busy: true
  await env.advanceTime(500);
  assert.doesNotMatch(env.elements.feedback.textContent, /tamamlandı|kaydedildi/i);

  // Eğer kalibrasyon NVS hatası nedeniyle failed olursa yanlış başarı gösterilmez
  currentSnapshot = {
    grams: 200.0,
    calibrated: true,
    sensor_ok: true,
    seq: 4,
    sample_age_ms: 25,
    busy: false,
    operation: 'calibrate',
    op_status: 'failed',
    op_id: 7,
    error: 'NVS yazma hatasi; onceki kalibrasyon korundu.'
  };
  await env.advanceTime(500);
  assert.match(env.elements.feedback.textContent, /NVS yazma hatasi/i);
  assert.doesNotMatch(env.elements.feedback.textContent, /tamamlandı/i);

  // Şimdi başarılı 202 -> completed akışını doğrula
  env.elements.calibrate.trigger('click');
  await env.flushMicrotasks();
  assert.equal(postCount, 2);
  currentSnapshot = {
    grams: 1000.0,
    calibrated: true,
    sensor_ok: true,
    seq: 8,
    sample_age_ms: 20,
    busy: false,
    operation: 'calibrate',
    op_status: 'completed',
    op_id: 7,
    error: null
  };
  await env.advanceTime(500);
  assert.match(env.elements.feedback.textContent, /tamamlandı|kaydedildi/i);
  assert.equal(env.elements.weight.textContent, '1.000');

  // POST/GET yarışı ve geç yanıt koruması: POST başladığında havada kalan eski GET yanıtı yok sayılmalı
  let resolveSlowGet = null;
  const raceEnv = loadTabletSandbox({
    withDom: true,
    protocol: 'http:',
    fetch: (url, init) => {
      if (url === '/api/weight') {
        if (!resolveSlowGet) {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: async () => ({ grams: 350.0, calibrated: true, sensor_ok: true, seq: 10, sample_age_ms: 15, busy: false })
          });
        }
        return new Promise((resolve, reject) => {
          init.signal.addEventListener('abort', () => reject(new Error('Aborted')));
          resolveSlowGet(resolve);
        });
      }
      if (url === '/api/tare') {
        return Promise.resolve({
          ok: true,
          status: 202,
          json: async () => ({ ok: true, status: 'accepted', operation: 'tare', busy: true, op_id: 9 })
        });
      }
    }
  });
  await raceEnv.flushMicrotasks();
  assert.equal(raceEnv.elements.weight.textContent, '350');

  let slowGetResolver = null;
  resolveSlowGet = (r) => { slowGetResolver = r; };
  await raceEnv.advanceTime(500); // Yavaş GET başladı ve henüz dönmedi
  assert.ok(slowGetResolver !== null);

  // Kullanıcı bu sırada Dara al'a bastı -> eski GET iptal edilmeli / geç yanıtı ezmemeli
  raceEnv.elements.tare.trigger('click');
  await raceEnv.flushMicrotasks();
  // Eski GET gecikmeli olarak eski 350 g ile dönmeye çalışsa bile kabul edilmez
  slowGetResolver({
    ok: true,
    status: 200,
    json: async () => ({ grams: 350.0, calibrated: true, sensor_ok: true, seq: 11, sample_age_ms: 15, busy: false })
  });
  await raceEnv.flushMicrotasks();
  assert.equal(raceEnv.elements.weight.textContent, '—');
});

test('PROGMEM HTML üreticisi güncellik kontrolünü ve C++ raw literal delimiter güvenliğini doğrular', () => {
  const gen = require(path.join(__dirname, '..', '..', 'firmware', 'tools', 'generate_webui_header.cjs'));
  const checkRes = gen.generateOrCheck({ checkOnly: true });
  assert.equal(checkRes.ok, true, checkRes.reason);

  // Kapanış sınırlayıcısı HTML içinde geçerse hata fırlatmalıdır
  assert.throws(
    () => gen.buildHeaderContent('<html>)TERAZI_HTML"</html>', 'TERAZI_HTML'),
    /Güvenlik hatası/
  );
});
