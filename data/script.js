// ===== Client logic for Traffic Control UI =====
// - Per-field edit locks to avoid overwriting while typing (mobile friendly).
// - Sync #map-container to image size to keep markers aligned.

let currentLight = null;

const imgMap = {
  'R': 'traffic_red.png',
  'Y': 'traffic_yellow.png',
  'G': 'traffic_green.png',
  'OFF': 'traffic_base.png'
};

const mapEl  = document.getElementById('map');
const contEl = document.getElementById('map-container');

function syncContainerToImage() {
  if (!mapEl || !contEl) return;
  const w = mapEl.clientWidth;
  const h = mapEl.clientHeight;
  if (w > 0 && h > 0) {
    contEl.style.width  = w + 'px';
    contEl.style.height = h + 'px';
  }
}
window.addEventListener('load', syncContainerToImage);
window.addEventListener('resize', syncContainerToImage);
window.addEventListener('orientationchange', syncContainerToImage);
mapEl?.addEventListener('load', syncContainerToImage);

// ---- Popup & light click ----
document.querySelectorAll('.light').forEach(el => {
  el.addEventListener('click', () => {
    currentLight = parseInt(el.dataset.id, 10);
    document.getElementById('light-id').innerText = currentLight;
    document.getElementById('popup').classList.remove('hidden');
    const hint = document.getElementById('manualHint');
    // Cập nhật hint: đang AUTO thì sẽ tự chuyển sang MANUAL khi chọn màu
    hint.textContent = (window._mode === 'auto')
      ? 'Đang AUTO — sẽ tự chuyển sang MANUAL khi bạn chọn màu.'
      : '';
  });
});
document.getElementById('close').onclick = () => {
  document.getElementById('popup').classList.add('hidden');
};

// ---- Manual set (auto-switch from AUTO -> MANUAL) ----
async function setLight(color) {
  try {
    if (currentLight == null) {
      alert('Chưa chọn cột đèn.');
      return;
    }

    // Nếu đang AUTO thì chuyển qua MANUAL trước để UI đồng bộ
    if (window._mode === 'auto') {
      const m = await fetch('/mode?set=manual', { cache: 'no-store' });
      if (!m.ok) {
        const t = await m.text().catch(()=>'');
        alert('Không chuyển được sang MANUAL: ' + t);
        return;
      }
      window._mode = 'manual'; // cập nhật tạm cho UI
    }

    // Gửi lệnh đặt màu
    const res = await fetch(`/set?lamp=${currentLight}&color=${color}`, { cache: 'no-store' });
    if (!res.ok) {
      const t = await res.text().catch(()=>'');
      alert('Lỗi khi đặt đèn: ' + t);
    }

    await refresh();
  } catch (e) {
    console.error(e);
    alert('Mạng bị lỗi. Thử lại nhé.');
  } finally {
    document.getElementById('popup').classList.add('hidden');
  }
}
window.setLight = setLight; // for inline onclick

// ---- Toolbar elements ----
const btnManual  = document.getElementById('btnManual');
const btnAuto    = document.getElementById('btnAuto');
const modeLabel  = document.getElementById('modeLabel');
const autoPanel  = document.getElementById('autoPanel');
const phaseText  = document.getElementById('phaseText');
const remainText = document.getElementById('remainText');

const tg = document.getElementById('tg');  // green
const ty = document.getElementById('ty');  // yellow
const tr = document.getElementById('tr');  // red (derived g+y)

// ---- Per-field Edit lock ----
const EDIT_GRACE_MS = 5000;
const edit = {
  g: { active: false, ts: 0 },
  y: { active: false, ts: 0 },
  r: { active: false, ts: 0 }
};
function mark(field, on) { edit[field].active = on; edit[field].ts = Date.now(); }
function touch(field) { edit[field].ts = Date.now(); }
function isLocked(field) {
  const st = edit[field];
  const now = Date.now();
  return st.active || (now - st.ts) < EDIT_GRACE_MS;
}

function parseMaybe(v) {
  if (v === '' || v === null || v === undefined) return null;
  const n = Number(v);
  return Number.isFinite(n) ? Math.round(n) : null;
}
function clampSec(n) {
  n = Math.round(n);
  if (n < 1) n = 1;
  if (n > 86400) n = 86400;
  return n;
}

function syncDerivedR() {
  const g = parseMaybe(tg.value);
  const y = parseMaybe(ty.value);
  if (g == null || y == null) return;
  if (!isLocked('r')) tr.value = g + y;
}
function handleRedEdited() {
  const r = parseMaybe(tr.value);
  const y = parseMaybe(ty.value);
  if (r == null || y == null) return;
  let g = r - y;
  if (!Number.isFinite(g)) return;
  if (g < 1) g = 1;
  tg.value = g;
  if (!isLocked('r')) tr.value = g + y;
}

btnManual.onclick = async () => {
  await fetch('/mode?set=manual', { cache: 'no-store' });
  await refresh();
};
btnAuto.onclick   = async () => {
  await fetch('/mode?set=auto',   { cache: 'no-store' });
  await refresh();
};

document.getElementById('btnSetTiming').onclick = async () => {
  try {
    let g = parseMaybe(tg.value);
    let y = parseMaybe(ty.value);
    if (g == null || y == null) { alert('Vui lòng nhập đủ thời gian G và Y (giây).'); return; }
    g = clampSec(g);
    y = clampSec(y);
    await fetch(`/timing?g=${g}&y=${y}`, { cache: 'no-store' });
    mark('g', false); mark('y', false); mark('r', false);
  } catch (e) {
    console.error(e);
  } finally {
    await refresh();
  }
};

// Lock controls while typing (focus / typing)
[['g', tg], ['y', ty], ['r', tr]].forEach(([k, el]) => {
  el.addEventListener('focus', () => mark(k, true));
  el.addEventListener('blur',  () => mark(k, false));
  el.addEventListener('pointerdown', () => mark(k, true));
  el.addEventListener('keydown', () => touch(k));
  el.addEventListener('input', () => {
    touch(k);
    if (el === tg || el === ty) syncDerivedR();
    if (el === tr) handleRedEdited();
  });
});

// ---- Helpers ----
function updateLamp(lamp, color) {
  const icon = document.querySelector(`.light[data-id="${lamp}"] img`);
  if (icon) {
    const src = imgMap[color] || imgMap['OFF'];
    if (icon.getAttribute('src') !== src) icon.setAttribute('src', src);
  }
}

// ---- Poll /status ----
async function refresh() {
  try {
    const s = await fetch('/status', { cache: 'no-store' }).then(r => r.json());

    window._mode = s.mode; // 'auto' | 'manual'
    if (modeLabel) modeLabel.textContent = s.mode ? s.mode.toUpperCase() : '?';

    if (s.mode === 'auto') {
      btnAuto?.classList.add('active'); btnManual?.classList.remove('active');
      autoPanel?.classList.remove('hidden');
    } else {
      btnManual?.classList.add('active'); btnAuto?.classList.remove('active');
      autoPanel?.classList.add('hidden');
    }

    if (s.timing) {
      if (!isLocked('g')) tg.value = s.timing.g ?? tg.value;
      if (!isLocked('y')) ty.value = s.timing.y ?? ty.value;
      const g = parseMaybe(tg.value);
      const y = parseMaybe(ty.value);
      const desiredR = (s.timing.r != null) ? s.timing.r : ((g ?? 0) + (y ?? 0));
      if (!isLocked('r')) tr.value = desiredR;
    }

    if (s.mode === 'auto') {
      const phaseMap = { A_G: 'A Green / B Red', A_Y: 'A Yellow / B Red', B_G: 'A Red / B Green', B_Y: 'A Red / B Yellow' };
      const p = s.phase || '?';
      if (phaseText)  phaseText.textContent  = `Phase: ${phaseMap[p] || p}`;
      const remain = Math.max(0, Math.round((s.t_remain_ms ?? 0) / 1000));
      if (remainText) remainText.textContent = `Remain: ${remain}s`;
    } else {
      if (phaseText)  phaseText.textContent  = '';
      if (remainText) remainText.textContent = '';
    }

    if (Array.isArray(s.lamps)) {
      for (const L of s.lamps) updateLamp(L.lamp, L.color);
    }

    syncContainerToImage();
  } catch (e) {
    console.error('Refresh error', e);
  }
}

syncContainerToImage();
refresh();
setInterval(refresh, 1000);
