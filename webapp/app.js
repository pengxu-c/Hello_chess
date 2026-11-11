/* ============================================================
   app.js - 前端界面（微内核版）

   与后端的全部交互只有三类：
     GET  /api/events    SSE：内核状态快照，只读，单向推送
     POST /api/cmd       { op, ... }：发一条命令给内核
     GET  /api/players   /api/games  /api/resumes  /api/stats
     POST /api/replay    回放定位（只读查询）

   设计要点：本文件里没有一处提到"棋盘数据结构"以外的内核细节 ——
   玩家是插件、界面是插件、存储也是插件，前端只认这份 JSON 协议。
   想换皮肤只改 CSS；想换玩法只改 op 字符串。
   ============================================================ */

const canvas = document.getElementById('board');
const ctx = canvas.getContext('2d');

const el = {
  conn: document.getElementById('conn'),
  connText: document.getElementById('conn-text'),
  status: document.getElementById('meta-status'),
  turn: document.getElementById('meta-turn'),
  turnBox: document.getElementById('turn-box'),
  moves: document.getElementById('meta-moves'),
  size: document.getElementById('meta-size'),
  connect: document.getElementById('meta-connect'),
  black: document.getElementById('meta-black'),
  white: document.getElementById('meta-white'),
  cardBlack: document.getElementById('card-black'),
  cardWhite: document.getElementById('card-white'),
  thinking: document.getElementById('thinking'),
  message: document.getElementById('message'),
  overlay: document.getElementById('overlay'),
  overlayTitle: document.getElementById('overlay-title'),
  overlayText: document.getElementById('overlay-text'),
  modalNew: document.getElementById('modal-new'),
  modalList: document.getElementById('modal-list'),
  listTitle: document.getElementById('list-title'),
  listBody: document.getElementById('list-body'),
  selP1: document.getElementById('sel-p1'),
  selP2: document.getElementById('sel-p2'),
  swapBlack: document.getElementById('swap-black'),
  swapWhite: document.getElementById('swap-white'),
  inSize: document.getElementById('in-size'),
  inWin: document.getElementById('in-win'),
  inStorage: document.getElementById('in-storage'),
  newNote: document.getElementById('new-note'),
  newError: document.getElementById('new-error'),
  replayBar: document.getElementById('replay-bar'),
  rpSlider: document.getElementById('rp-slider'),
  rpCount: document.getElementById('rp-count'),
  rpPlay: document.getElementById('rp-play'),
  toast: document.getElementById('toast'),
};

/* ---------- 视图状态 ---------- */
let state = null;          // 最新实时快照
let prevState = null;      // 上一帧（检测新落子以播放动画）
let mode = 'live';         // 'live' | 'replay'
let replay = null;         // { id, total, step }
let replayView = null;     // 回放时的伪状态
let replayTimer = null;
let flipView = false;
let geometry = { margin: 0, gap: 0 };
let hover = null;
let anim = null;
let rafId = 0, animRaf = 0;
let players = [];
let themeColors = {};
let toastTimer = null;
let lastMessage = '';      // 避免同一条内核消息反复弹

// SSE 连接生命周期：模块级持有，便于 quit/pagehide 时主动关闭，
// 避免浏览器自动重连占满服务端 SSE 名额。
let es = null;             // 当前 EventSource
let sseRetry = 0;          // 连续重连次数（指数退避）
let sseRetryTimer = null;  // 退避计时器
let sseClosed = false;     // 主动关闭后不再自动重连
const SSE_MAX_RETRY = 6;   // 最大重连次数，超限停止并提示

/* ---------- 通信 ---------- */

// 发一条命令给内核。所有对棋局的操作都走这里（唯一写入口）。
const inflightCmds = new Map();
async function cmd(op, payload) {
  const key = op + ':' + JSON.stringify(payload || {});
  // 同一条命令在途时直接复用，避免连点/双击发出两手 move 或两次 undo。
  if (inflightCmds.has(key)) return inflightCmds.get(key);
  const task = api('/api/cmd', Object.assign({ op }, payload || {}));
  inflightCmds.set(key, task);
  try { return await task; } finally { inflightCmds.delete(key); }
}

async function api(path, body) {
  try {
    const opt = body
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : {};
    const res = await fetch(path, opt);
    if (!res.ok) {
      let msg = 'HTTP ' + res.status;
      try { const j = await res.json(); if (j && (j.error || j.message)) msg = j.error || j.message; } catch (_) {}
      toast(msg, 'error');
      return null;
    }
    return await res.json();
  } catch (e) {
    console.warn('request failed:', path, e);
    toast('Server unreachable', 'error');
    return null;
  }
}

function toast(msg, kind) {
  el.toast.textContent = msg;
  el.toast.className = 'toast' + (kind ? ' toast--' + kind : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.toast.className = 'toast hidden'; }, 2600);
}

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function samePos(a, b) { return a && b && a[0] === b[0] && a[1] === b[1]; }
function posValid(p) { return Array.isArray(p) && p[0] >= 0 && p[1] >= 0; }
function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, ch => (
    { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]
  ));
}

// 玩家座位既可能是实时快照里的 {id,name} 对象，也可能是回放里的字符串。
const label = (v) => (typeof v === 'string' ? v : (v && v.name) || '-');

/* ---------- 主题 ---------- */
function readThemeColors() {
  const cs = getComputedStyle(document.documentElement);
  const get = (k) => cs.getPropertyValue(k).trim();
  themeColors = {
    board: get('--board-1'),
    board2: get('--board-2'),
    line: get('--board-line'),
    coord: get('--board-coord'),
    ink: get('--ink'),
    accent: get('--accent'),
    brass: get('--brass'),
  };
}
function applyTheme(t) {
  document.documentElement.setAttribute('data-theme', t);
  localStorage.setItem('gomoku-theme', t);
  readThemeColors();
  render();
}
function toggleTheme() {
  const cur = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
  applyTheme(cur === 'dark' ? 'light' : 'dark');
}
function initTheme() {
  const q = new URLSearchParams(location.search).get('theme');
  if (q === 'dark' || q === 'light') {
    applyTheme(q);
    return;
  }
  const saved = localStorage.getItem('gomoku-theme');
  const t = saved || (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  document.documentElement.setAttribute('data-theme', t);
  readThemeColors();
}

/* ---------- 几何 ---------- */
function computeGeometry(n) {
  const size = canvas.width;
  // 基础留白按比例；再用棋子的实际半径反推一个下限：
  // 小棋盘（如 9 路）格子大、棋子也大，固定 7.5% 会让最外圈棋子
  // 压住 A/B/C 这些列标，所以这里取两者较大值。
  const base = Math.round(size * 0.075);
  const need = Math.ceil((0.84 * size + 12 * (n - 1)) / (n + 0.68));
  const margin = Math.max(base, need);
  geometry = { margin, gap: (size - margin * 2) / (n - 1) };
}
function toDisplay(r, c, n) { return flipView ? [n - 1 - r, n - 1 - c] : [r, c]; }
function fromDisplay(dr, dc, n) { return flipView ? [n - 1 - dr, n - 1 - dc] : [dr, dc]; }

/* ---------- 绘制 ---------- */
function currentView() { return (mode === 'replay' && replayView) ? replayView : state; }
function scheduleRender() {
  if (!rafId) rafId = requestAnimationFrame(() => { rafId = 0; render(); });
}

function render() {
  const v = currentView();
  if (!v || !v.boardSize) return;
  const n = v.boardSize;
  const size = canvas.width;
  computeGeometry(n);
  const { margin, gap } = geometry;

  // 木纹底：斜向双层渐变 + 中心提亮，比纯色更像真实棋盘
  const wood = ctx.createLinearGradient(0, 0, size, size);
  wood.addColorStop(0, themeColors.board);
  wood.addColorStop(.48, themeColors.board);
  wood.addColorStop(1, themeColors.board2);
  ctx.fillStyle = wood;
  ctx.fillRect(0, 0, size, size);

  const glow = ctx.createRadialGradient(size * .38, size * .3, size * .05,
                                        size * .5, size * .5, size * .78);
  glow.addColorStop(0, 'rgba(255,255,255,.10)');
  glow.addColorStop(.55, 'rgba(255,255,255,0)');
  glow.addColorStop(1, 'rgba(60,40,15,.10)');
  ctx.fillStyle = glow;
  ctx.fillRect(0, 0, size, size);

  // 极淡的斜纹，模拟木料纹理；透明度压到几乎看不见，只提供质感
  ctx.save();
  ctx.globalAlpha = .035;
  ctx.strokeStyle = '#5b3d18';
  ctx.lineWidth = 1;
  for (let i = -size; i < size * 2; i += 26) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i + size, size);
    ctx.stroke();
  }
  ctx.restore();

  ctx.strokeStyle = themeColors.line;
  ctx.lineWidth = 1;
  ctx.globalAlpha = .9;
  for (let i = 0; i < n; i++) {
    const p = Math.round(margin + i * gap) + 0.5;
    ctx.beginPath(); ctx.moveTo(margin, p); ctx.lineTo(size - margin, p); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p, margin); ctx.lineTo(p, size - margin); ctx.stroke();
  }
  ctx.globalAlpha = 1;

  if (n >= 15) {
    const mid = (n - 1) / 2;
    const pts = [[3, 3], [3, n - 4], [n - 4, 3], [n - 4, n - 4], [mid, mid]];
    ctx.fillStyle = themeColors.line;
    for (const [r, c] of pts) {
      const [dr, dc] = toDisplay(r, c, n);
      ctx.beginPath();
      ctx.arc(margin + dc * gap, margin + dr * gap, Math.max(2, gap * 0.08), 0, Math.PI * 2);
      ctx.fill();
    }
  }

  drawCoords(n, margin, gap);

  const radius = gap * 0.42;
  const cells = v.cells || [];
  for (let r = 0; r < n; r++) {
    for (let c = 0; c < n; c++) {
      const val = cells[r * n + c];
      if (!val) continue;
      const [dr, dc] = toDisplay(r, c, n);
      drawStone(margin + dc * gap, margin + dr * gap, radius, val, 1);
    }
  }

  drawWinLine(v, n, margin, gap);
  drawLastDot(v.lastBlack, n, margin, gap, radius, 1);
  drawLastDot(v.lastWhite, n, margin, gap, radius, -1);
  drawHover(v, n, margin, gap, radius);
  drawAnim(n, margin, gap, radius);
}

function drawCoords(n, margin, gap) {
  ctx.fillStyle = themeColors.coord;
  ctx.font = `${Math.max(10, Math.floor(gap * 0.42))}px "Segoe UI", system-ui, sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  for (let d = 0; d < n; d++) {
    // 列标签取第 0 行的第 d 列；行标签必须取第 d 行的第 0 列。
    const [, lc] = fromDisplay(0, d, n);
    const [lr] = fromDisplay(d, 0, n);
    const colLabel = String.fromCharCode(65 + lc);
    const colX = margin + d * gap;
    ctx.fillText(colLabel, colX, margin * 0.5);
    ctx.fillText(colLabel, colX, canvas.height - margin * 0.5);
    const rowLabel = String(lr + 1);
    const rowY = margin + d * gap;
    ctx.fillText(rowLabel, margin * 0.5, rowY);
    ctx.fillText(rowLabel, canvas.width - margin * 0.5, rowY);
  }
}

function drawStone(x, y, radius, val, scale) {
  const rr = radius * (scale || 1);
  const isBlack = val > 0;

  ctx.save();
  ctx.globalAlpha = (scale && scale < 1) ? 0.35 + 0.65 * scale : 1;
  ctx.shadowColor = isBlack ? 'rgba(0,0,0,.55)' : 'rgba(70,50,25,.40)';
  ctx.shadowBlur = rr * 0.55;
  ctx.shadowOffsetY = rr * 0.20;

  const g = ctx.createRadialGradient(
    x - rr * 0.34, y - rr * 0.38, rr * 0.12,
    x, y, rr * 1.08
  );
  if (isBlack) {
    g.addColorStop(0, '#5f646d');
    g.addColorStop(.42, '#262a30');
    g.addColorStop(1, '#08090b');
  } else {
    g.addColorStop(0, '#ffffff');
    g.addColorStop(.52, '#f6f1e6');
    g.addColorStop(1, '#cfc4b0');
  }
  ctx.beginPath();
  ctx.arc(x, y, rr, 0, Math.PI * 2);
  ctx.fillStyle = g;
  ctx.fill();

  // 去掉投影再画一圈极细轮廓，避免边缘糊在木纹里
  ctx.shadowColor = 'transparent';
  ctx.lineWidth = Math.max(0.6, rr * 0.05);
  ctx.strokeStyle = isBlack ? 'rgba(255,255,255,.14)' : 'rgba(120,95,60,.28)';
  ctx.stroke();
  ctx.restore();
}

// 从最后一手反推五连的两端。只用于"画一条纪念线"，不参与胜负判定
// （胜负由后端 MatchState 决定，前端不重算规则）。
function findWinLine(v, n) {
  const color = v.status === 'BlackWin' ? 1 : v.status === 'WhiteWin' ? -1 : 0;
  if (!color) return null;
  const last = color > 0 ? v.lastBlack : v.lastWhite;
  if (!posValid(last)) return null;
  const cells = v.cells || [];
  const win = v.winLength || 5;
  const at = (r, c) => (r >= 0 && r < n && c >= 0 && c < n) ? cells[r * n + c] : 0;
  if (at(last[0], last[1]) !== color) return null;

  const dirs = [[0, 1], [1, 0], [1, 1], [1, -1]];
  for (const [dr, dc] of dirs) {
    let a = last.slice(), b = last.slice();
    while (at(a[0] - dr, a[1] - dc) === color) { a = [a[0] - dr, a[1] - dc]; }
    while (at(b[0] + dr, b[1] + dc) === color) { b = [b[0] + dr, b[1] + dc]; }
    const len = Math.abs(b[0] - a[0]) + Math.abs(b[1] - a[1]) + 1;
    if (len >= win) return [a, b];
  }
  return null;
}

function drawWinLine(v, n, margin, gap) {
  const line = findWinLine(v, n);
  if (!line) return;
  const [a, b] = line;
  const [ar, ac] = toDisplay(a[0], a[1], n);
  const [br, bc] = toDisplay(b[0], b[1], n);
  const x1 = margin + ac * gap, y1 = margin + ar * gap;
  const x2 = margin + bc * gap, y2 = margin + br * gap;

  ctx.save();
  ctx.lineCap = 'round';
  ctx.lineWidth = Math.max(3, gap * 0.11);
  ctx.strokeStyle = themeColors.brass || '#b07a2c';
  ctx.shadowColor = themeColors.brass || '#b07a2c';
  ctx.shadowBlur = gap * 0.7;
  ctx.globalAlpha = .9;
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();

  // 两端各点一个小圆，像棋子被串起来
  ctx.globalAlpha = 1;
  ctx.fillStyle = themeColors.brass || '#b07a2c';
  for (const [x, y] of [[x1, y1], [x2, y2]]) {
    ctx.beginPath();
    ctx.arc(x, y, Math.max(2.5, gap * 0.09), 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

// 最后一手：在棋子上点一个反差色小点，比画圈干净
function drawLastDot(pos, n, margin, gap, radius, val) {
  if (!posValid(pos)) return;
  const [dr, dc] = toDisplay(pos[0], pos[1], n);
  ctx.save();
  ctx.beginPath();
  ctx.arc(margin + dc * gap, margin + dr * gap,
          Math.max(1.8, radius * 0.17), 0, Math.PI * 2);
  ctx.fillStyle = val > 0 ? 'rgba(255,255,255,.72)' : 'rgba(28,20,10,.55)';
  ctx.fill();
  ctx.restore();
}

function drawHover(v, n, margin, gap, radius) {
  if (mode !== 'live' || !hover) return;
  if (!v || v.status !== 'InProgress' || !v.humanTurn || v.thinking) return;
  if (hover.r < 0 || hover.r >= n || hover.c < 0 || hover.c >= n) return;
  if ((v.cells || [])[hover.r * n + hover.c] !== 0) return;
  const [dr, dc] = toDisplay(hover.r, hover.c, n);
  ctx.beginPath();
  ctx.arc(margin + dc * gap, margin + dr * gap, radius, 0, Math.PI * 2);
  ctx.globalAlpha = 0.35;
  ctx.fillStyle = v.turn > 0 ? '#111318' : '#fafafa';
  ctx.fill();
  ctx.globalAlpha = 1;
}

function drawAnim(n, margin, gap, radius) {
  if (!anim) return;
  const t = clamp((performance.now() - anim.start) / 220, 0, 1);
  const scale = 1.5 - 0.5 * t;
  const [dr, dc] = toDisplay(anim.r, anim.c, n);
  drawStone(margin + dc * gap, margin + dr * gap, radius, anim.color, scale);
}
function animTick(ts) {
  animRaf = 0;
  if (!anim || ts - anim.start >= 220) { anim = null; render(); return; }
  render();
  animRaf = requestAnimationFrame(animTick);
}
function startAnim(pos, color) {
  anim = { r: pos[0], c: pos[1], color, start: performance.now() };
  if (!animRaf) animRaf = requestAnimationFrame(animTick);
}

/* ---------- 状态应用 ---------- */
function applyState(st) {
  if (prevState && mode === 'live' && st.moveCount > prevState.moveCount) {
    if (!samePos(prevState.lastBlack, st.lastBlack) && posValid(st.lastBlack)) startAnim(st.lastBlack, 1);
    else if (!samePos(prevState.lastWhite, st.lastWhite) && posValid(st.lastWhite)) startAnim(st.lastWhite, -1);
  }
  prevState = state;
  state = st;

  if (st.message && st.message !== lastMessage) {
    lastMessage = st.message;
    toast(st.message, st.status === 'InProgress' ? '' : 'ok');
  } else if (!st.message) {
    lastMessage = '';
  }

  if (mode === 'live') render();
  updateMeta(st);
  updateOverlay(st);
  updateControls(st);
}

const STATUS_TEXT = {
  Idle: 'Ready', InProgress: 'In progress', BlackWin: 'Black wins',
  WhiteWin: 'White wins', Draw: 'Draw', Replay: 'Replay',
};

function updateMeta(v) {
  if (!v) return;
  const p = v.players || { black: '-', white: '-' };
  const inPlay = v.status === 'InProgress';
  const over = !inPlay && v.status !== 'Idle' && v.status !== 'Replay';

  el.status.textContent = STATUS_TEXT[v.status] || v.status || 'Ready';
  el.status.className = 'status-pill' + (inPlay ? ' is-live' : (over ? ' is-over' : ''));

  const thinking = !!v.thinking;
  let turnText = '-';
  if (inPlay) {
    if (thinking) turnText = 'Computer thinking';
    else if (v.humanTurn) turnText = 'Your move';
    else turnText = (v.turn > 0 ? 'Black' : 'White') + ' to move';
  } else if (v.status === 'Replay') {
    turnText = 'Replay';
  } else if (over) {
    turnText = STATUS_TEXT[v.status];
  }
  el.turn.textContent = turnText;
  el.turnBox.classList.toggle('is-idle', !inPlay);
  el.turnBox.classList.toggle('is-thinking', thinking);
  el.thinking.classList.toggle('hidden', !thinking);

  el.moves.textContent = v.moveCount;
  el.size.textContent = v.boardSize + ' x ' + v.boardSize;
  el.connect.textContent = (v.winLength || 5);
  el.black.textContent = label(p.black);
  el.white.textContent = label(p.white);

  el.cardBlack.classList.toggle('is-turn', inPlay && v.turn > 0);
  el.cardWhite.classList.toggle('is-turn', inPlay && v.turn < 0);

  el.message.textContent = thinking ? 'AI thinking, please wait...'
    : (v.message || (mode === 'replay' ? 'Replaying a saved game' : ''));
}

function updateControls(v) {
  const live = mode === 'live';
  const st = live ? v : state;
  document.getElementById('btn-undo').disabled = !(live && st && st.canUndo);
  document.getElementById('btn-abort').disabled = !(live && st && st.status !== 'Idle');
  document.getElementById('btn-save').disabled = !(live && st && st.status !== 'Idle');
  const canSwap = !!(live && st && st.status === 'InProgress');
  document.getElementById('btn-swap-black').disabled = !canSwap;
  document.getElementById('btn-swap-white').disabled = !canSwap;
  el.swapBlack.disabled = !canSwap;
  el.swapWhite.disabled = !canSwap;
}

function updateOverlay(st) {
  if (mode !== 'live') { el.overlay.classList.add('hidden'); return; }
  const map = { BlackWin: 'Black wins', WhiteWin: 'White wins', Draw: 'Draw' };
  if (map[st.status]) {
    el.overlayTitle.textContent = map[st.status];
    const p = st.players || { black: '-', white: '-' };
    el.overlayText.textContent = `${label(p.black)} vs ${label(p.white)} - ${st.moveCount} moves`;
    el.overlay.classList.remove('hidden');
  } else {
    el.overlay.classList.add('hidden');
  }
}

function setConn(ok) {
  el.conn.className = 'conn ' + (ok ? 'conn--on' : 'conn--off');
  el.connText.textContent = ok ? 'live' : 'reconnecting';
}

/* ---------- SSE ---------- */
// 主动关闭：清退避计时器并 close()，阻止浏览器自动重连占满服务端 SSE 名额。
function closeEvents() {
  sseClosed = true;
  if (sseRetryTimer) { clearTimeout(sseRetryTimer); sseRetryTimer = null; }
  if (es) { es.close(); es = null; }
}

function subscribe() {
  if (sseClosed) return;
  if (sseRetryTimer) { clearTimeout(sseRetryTimer); sseRetryTimer = null; }
  if (es) es.close();
  es = new EventSource('/api/events');
  es.onopen = () => { sseRetry = 0; setConn(true); };
  es.onmessage = (ev) => {
    try {
      const st = JSON.parse(ev.data);
      if (mode === 'live') applyState(st);
      else { prevState = state; state = st; }
    } catch (e) { console.warn(e); }
  };
  es.onerror = () => {
    setConn(false);
    if (sseClosed) return;                 // 已主动关闭，不再重连
    if (es) { es.close(); es = null; }     // 接管重连节奏，不让浏览器立即自动重连
    if (sseRetry >= SSE_MAX_RETRY) {
      toast('Connection lost. Reload the page to reconnect.', 'error');
      return;
    }
    const delay = Math.min(1000 * Math.pow(2, sseRetry), 30000);
    ++sseRetry;
    sseRetryTimer = setTimeout(() => { sseRetryTimer = null; subscribe(); }, delay);
  };
}

/* ---------- 鼠标 ---------- */
function eventToCell(ev) {
  const v = currentView();
  if (!v || !v.boardSize) return null;
  const rect = canvas.getBoundingClientRect();
  const x = (ev.clientX - rect.left) * (canvas.width / rect.width);
  const y = (ev.clientY - rect.top) * (canvas.height / rect.height);
  const { margin, gap } = geometry;
  const n = v.boardSize;
  const dc = Math.round((x - margin) / gap);
  const dr = Math.round((y - margin) / gap);
  if (dr < 0 || dr >= n || dc < 0 || dc >= n) return null;
  const [r, c] = fromDisplay(dr, dc, n);
  return { r, c };
}

function canPlay() {
  return mode === 'live' && state && state.status === 'InProgress' && state.humanTurn && !state.thinking;
}

canvas.addEventListener('mousemove', (ev) => {
  const cell = canPlay() ? eventToCell(ev) : null;
  const changed = (cell ? cell.r !== (hover && hover.r) || cell.c !== (hover && hover.c) : hover !== null);
  hover = cell;
  if (changed) scheduleRender();
});
canvas.addEventListener('mouseleave', () => { if (hover) { hover = null; scheduleRender(); } });
canvas.addEventListener('click', (ev) => {
  if (!canPlay()) return;
  const cell = eventToCell(ev);
  if (!cell) return;
  const n = state.boardSize;
  if (state.cells[cell.r * n + cell.c] !== 0) return;
  hover = null;
  cmd('move', { r: cell.r, c: cell.c });
});

/* ---------- 对话框 ---------- */
function syncPresets() {
  const size = parseInt(el.inSize.value, 10);
  const win = parseInt(el.inWin.value, 10);
  document.querySelectorAll('.preset').forEach(btn => {
    const hit = Number(btn.dataset.size) === size && Number(btn.dataset.win) === win;
    btn.classList.toggle('preset--on', hit);
  });
}

function openNewGame() {
  // 预填当前设置，换局时不用重选一遍
  if (state && state.boardSize) {
    el.inSize.value = state.boardSize;
    el.inWin.value = state.winLength || 5;
  }
  if (state && state.players) {
    if (state.players.black && state.players.black.id) el.selP1.value = state.players.black.id;
    if (state.players.white && state.players.white.id) el.selP2.value = state.players.white.id;
  }
  syncPresets();
  el.modalNew.classList.remove('hidden');
  setTimeout(() => { try { el.inSize.focus(); el.inSize.select(); } catch (_) {} }, 30);
}
function closeNewGame() { el.modalNew.classList.add('hidden'); }
function openList(title, html) {
  el.listTitle.textContent = title;
  el.listBody.innerHTML = html;
  el.modalList.classList.remove('hidden');
}
function closeList() { el.modalList.classList.add('hidden'); }

// 真正下发"开新局"。校验与后端一致，但前端先拦一遍，避免点了没反应。
async function startGameWith(boardSize, winLength, black, white, storageEnabled) {
  if (!Number.isInteger(boardSize) || boardSize < 4 || boardSize > 30) {
    showNewError('Board size must be between 4 and 30.'); return false;
  }
  if (!Number.isInteger(winLength) || winLength < 4 || winLength > 15) {
    showNewError('Win length must be between 4 and 15.'); return false;
  }
  if (winLength > boardSize) {
    showNewError('Win length cannot exceed board size.'); return false;
  }
  showNewError('');
  closeNewGame();
  el.overlay.classList.add('hidden');
  exitReplayIfNeeded();
  prevState = null;
  await cmd('new', { boardSize, winLength, black, white, storageEnabled });
  return true;
}

/* ---------- 玩家目录（下拉框完全由后端目录驱动） ---------- */
function playerOptions(selectedId) {
  return players.map(p => {
    const label = p.name + (p.ready ? '' : ' (not configured)');
    const sel = (p.id === selectedId) ? ' selected' : '';
    const dis = p.ready ? '' : ' data-unready="1"';
    return `<option value="${esc(p.id)}"${sel}${dis}>${esc(label)}</option>`;
  }).join('');
}

function fillPlayers() {
  el.selP1.innerHTML = playerOptions('human');
  el.selP2.innerHTML = playerOptions('minimax');
  el.selP1.addEventListener('change', updateNewNote);
  el.selP2.addEventListener('change', updateNewNote);
  fillSwapSelects();
}

// 局中换人下拉：默认跟随当前座位
function fillSwapSelects() {
  if (!players.length) return;
  el.swapBlack.innerHTML = playerOptions(state ? state.players.black.id : 'human');
  el.swapWhite.innerHTML = playerOptions(state ? state.players.white.id : 'minimax');
}
function syncSwapSelects() {
  if (!state || !state.players) return;
  el.swapBlack.value = state.players.black.id;
  el.swapWhite.value = state.players.white.id;
}

// 选到未配置的插件时提示会自动回退
function updateNewNote() {
  const bad = [el.selP1, el.selP2].filter(s => {
    const o = s.selectedOptions[0];
    return o && o.dataset.unready === '1';
  });
  if (bad.length) {
    const names = bad.map(s => s.selectedOptions[0].textContent).join(', ');
    el.newNote.textContent = `${names}: will fall back to the declared fallback player.`;
    el.newNote.classList.remove('hidden');
  } else {
    el.newNote.classList.add('hidden');
  }
}

function showNewError(msg) {
  if (!msg) { el.newError.classList.add('hidden'); return; }
  el.newError.textContent = msg;
  el.newError.classList.remove('hidden');
}

/* ---------- 回放 ---------- */
function buildReplayView(r) {
  return {
    boardSize: r.boardSize,
    winLength: r.winLength || (state && state.winLength) || 5,
    cells: r.cells,
    status: 'Replay',
    turn: (r.step % 2 === 1) ? 1 : -1,
    moveCount: r.step,
    canUndo: false,
    thinking: false,
    humanTurn: false,
    players: { black: r.black, white: r.white },
    lastBlack: (r.last && r.lastColor > 0) ? r.last : [-1, -1],
    lastWhite: (r.last && r.lastColor < 0) ? r.last : [-1, -1],
    message: `Replay ${r.step}/${r.total}`,
  };
}

async function startReplay(id) {
  const r = await api('/api/replay', { id, step: -1 });
  if (!r || !r.ok) return;
  replay = { id, total: r.total, step: r.step };
  replayView = buildReplayView(r);
  mode = 'replay';
  el.replayBar.classList.remove('hidden');
  el.rpSlider.max = String(r.total);
  el.rpSlider.value = String(r.step);
  stopPlay();
  updateReplayBar();
  render();
  updateMeta(replayView);
  updateControls(replayView);
}

let replaySeq = 0;
async function setReplayStep(step) {
  if (!replay) return;
  const s = clamp(step, 0, replay.total);
  const seq = ++replaySeq;   // 滑块连拖/自动播放时，只认最后一次请求的响应
  const r = await api('/api/replay', { id: replay.id, step: s });
  if (!r || !r.ok || seq !== replaySeq) return;
  replay.step = r.step;
  replayView = buildReplayView(r);
  el.rpSlider.value = String(r.step);
  updateReplayBar();
  render();
  updateMeta(replayView);
}

function updateReplayBar() {
  if (!replay) return;
  el.rpCount.textContent = `${replay.step} / ${replay.total}`;
}
function stopPlay() {
  if (replayTimer) { clearInterval(replayTimer); replayTimer = null; }
  el.rpPlay.innerHTML = '&#9654;';
}
function togglePlay() {
  if (!replay) return;
  if (replayTimer) { stopPlay(); return; }
  if (replay.step >= replay.total) replay.step = 0;
  el.rpPlay.innerHTML = '&#10074;&#10074;';
  replayTimer = setInterval(() => {
    if (!replay || replay.step >= replay.total) { stopPlay(); return; }
    setReplayStep(replay.step + 1);
  }, 350);
}
function exitReplay() {
  stopPlay();
  mode = 'live';
  replay = null;
  replayView = null;
  el.replayBar.classList.add('hidden');
  if (state) {
    render();
    updateMeta(state);
    updateOverlay(state);
    updateControls(state);
  }
}
function exitReplayIfNeeded() { if (mode === 'replay') exitReplay(); }

/* ---------- 按钮 ---------- */
document.getElementById('btn-new').addEventListener('click', openNewGame);
// 再来一局：直接沿用当前设置开，不再让用户过一遍对话框
document.getElementById('btn-again').addEventListener('click', () => {
  el.overlay.classList.add('hidden');
  const size = (state && state.boardSize) || parseInt(el.inSize.value, 10) || 15;
  const win = (state && state.winLength) || parseInt(el.inWin.value, 10) || 5;
  const black = state && state.players && state.players.black ? state.players.black.id : 'human';
  const white = state && state.players && state.players.white ? state.players.white.id : 'minimax';
  startGameWith(size, win, black, white, true);
});
document.getElementById('btn-close').addEventListener('click', () => el.overlay.classList.add('hidden'));
document.getElementById('btn-cancel').addEventListener('click', closeNewGame);
document.getElementById('btn-list-close').addEventListener('click', closeList);
document.getElementById('btn-theme').addEventListener('click', toggleTheme);
document.getElementById('btn-flip').addEventListener('click', () => {
  flipView = !flipView;
  document.getElementById('btn-flip').classList.toggle('btn--on', flipView);
  render();
});

// 预设：一格点下去就填好尺寸，点 Start 即开局
document.querySelectorAll('.preset').forEach(btn => {
  btn.addEventListener('click', () => {
    el.inSize.value = btn.dataset.size;
    el.inWin.value = btn.dataset.win;
    syncPresets();
  });
});
el.inSize.addEventListener('input', syncPresets);
el.inWin.addEventListener('input', syncPresets);

document.getElementById('btn-start').addEventListener('click', () => {
  startGameWith(
    parseInt(el.inSize.value, 10),
    parseInt(el.inWin.value, 10),
    el.selP1.value || 'human',
    el.selP2.value || 'human',
    el.inStorage.checked
  );
});

document.getElementById('btn-undo').addEventListener('click', () => cmd('undo', { steps: 1 }));

document.getElementById('btn-abort').addEventListener('click', async () => {
  if (!state || state.status === 'Idle') return;
  await cmd('abort');
});

document.getElementById('btn-save').addEventListener('click', async () => {
  await cmd('save', { note: 'saved from web' });
});

document.getElementById('btn-swap-black').addEventListener('click', () => {
  cmd('swap', { seat: 0, playerId: el.swapBlack.value });
});
document.getElementById('btn-swap-white').addEventListener('click', () => {
  cmd('swap', { seat: 1, playerId: el.swapWhite.value });
});

document.getElementById('btn-load').addEventListener('click', async () => {
  const list = await api('/api/resumes') || [];
  if (!list.length) { openList('Resumes', '<div class="list-empty">No saved resumes.</div>'); return; }
  const html = list.map(r =>
    `<div class="list-item" data-id="${esc(r.id)}">
       <div class="rowline"><span>${esc(r.id)}</span><span class="sub">${r.moves} moves</span></div>
       <div class="sub">${r.boardSize}x${r.boardSize} - ${esc(r.black)} vs ${esc(r.white)}${r.note ? ' - ' + esc(r.note) : ''}</div>
     </div>`).join('');
  openList('Resumes', html);
  el.listBody.querySelectorAll('.list-item').forEach(node => {
    node.addEventListener('click', async () => {
      closeList();
      exitReplayIfNeeded();
      prevState = null;
      await cmd('load', { id: node.dataset.id });
    });
  });
});

document.getElementById('btn-games').addEventListener('click', async () => {
  const list = await api('/api/games') || [];
  if (!list.length) { openList('Replays', '<div class="list-empty">No saved games.</div>'); return; }
  const html = list.map(g =>
    `<div class="list-item" data-id="${esc(g.id)}">
       <div class="rowline"><span>${esc(g.id)}</span><span class="sub">${g.moves} moves</span></div>
       <div class="sub">${g.boardSize}x${g.boardSize} - ${esc(g.black)} vs ${esc(g.white)}</div>
     </div>`).join('');
  openList('Replays', html);
  el.listBody.querySelectorAll('.list-item').forEach(node => {
    node.addEventListener('click', async () => {
      closeList();
      await startReplay(node.dataset.id);
    });
  });
});

document.getElementById('btn-stats').addEventListener('click', async () => {
  const s = await api('/api/stats');
  if (!s) { openList('Statistics', '<div class="list-empty">Unavailable.</div>'); return; }
  const row = (label, key) =>
    `<div class="list-item"><div class="rowline"><span>${label}</span><b>${s[key] || 0}</b></div></div>`;
  openList('Statistics',
    row('Total games', 'totalGames') +
    row('Black wins', 'blackWins') +
    row('White wins', 'whiteWins') +
    row('Draws', 'draws') +
    row('Aborts', 'aborts') +
    row('Black moves', 'blackTotalMoves') +
    row('White moves', 'whiteTotalMoves'));
});

document.getElementById('btn-quit').addEventListener('click', async () => {
  await cmd('quit');
  closeEvents();
  document.body.innerHTML =
    '<div style="padding:48px;text-align:center;font:16px system-ui;color:#8a8f98">' +
    'Server stopped. You can close this tab.</div>';
});

/* ---------- 回放控制条 ---------- */
document.getElementById('rp-first').addEventListener('click', () => { stopPlay(); setReplayStep(0); });
document.getElementById('rp-prev').addEventListener('click', () => { stopPlay(); if (replay) setReplayStep(replay.step - 1); });
document.getElementById('rp-next').addEventListener('click', () => { stopPlay(); if (replay) setReplayStep(replay.step + 1); });
document.getElementById('rp-last').addEventListener('click', () => { stopPlay(); if (replay) setReplayStep(replay.total); });
document.getElementById('rp-play').addEventListener('click', togglePlay);
document.getElementById('rp-exit').addEventListener('click', exitReplay);
el.rpSlider.addEventListener('input', () => { stopPlay(); setReplayStep(parseInt(el.rpSlider.value, 10)); });

/* ---------- 快捷键 ---------- */
document.addEventListener('keydown', (ev) => {
  const tag = (ev.target && ev.target.tagName) || '';
  if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
  // 别抢系统/浏览器快捷键：Ctrl+R 应该刷新页面，不是打开回放。
  if (ev.ctrlKey || ev.metaKey || ev.altKey) return;

  if (ev.key === 'Escape') {
    closeNewGame(); closeList(); el.overlay.classList.add('hidden');
    return;
  }
  const modalOpen = !el.modalNew.classList.contains('hidden') ||
                    !el.modalList.classList.contains('hidden');
  if (modalOpen) return;

  if (mode === 'replay') {
    if (ev.key === 'ArrowLeft') { stopPlay(); if (replay) setReplayStep(replay.step - 1); }
    if (ev.key === 'ArrowRight') { stopPlay(); if (replay) setReplayStep(replay.step + 1); }
    if (ev.key === ' ') { ev.preventDefault(); togglePlay(); }
    return;
  }
  const k = ev.key.toLowerCase();
  if (k === 'n') openNewGame();
  else if (k === 'u') { if (state && state.canUndo) cmd('undo', { steps: 1 }); }
  else if (k === 's') document.getElementById('btn-save').click();
  else if (k === 'r') document.getElementById('btn-games').click();
  else if (k === 't') toggleTheme();
  else if (k === 'f') document.getElementById('btn-flip').click();
});

/* ---------- 启动 ---------- */
async function init() {
  initTheme();
  const st = await api('/api/state');
  if (st && st.boardSize) applyState(st);
  players = (await api('/api/players')) || [];
  fillPlayers();
  syncSwapSelects();
  updateNewNote();
  syncPresets();
  subscribe();
}

init();
window.addEventListener('resize', () => render());
window.addEventListener('pagehide', closeEvents);
