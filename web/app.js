/* ============================================================
   app.js - 前端逻辑
   职责：
     - 通过 SSE (/api/events) 接收实时状态，用 Canvas 绘制棋盘与棋子
     - 鼠标落子 POST /api/move；侧栏按钮调用 undo/abort/save/load/games/stats/quit
     - 回放模式：本地控制条（首/上一步/播放/下一步/末步/进度滑块）
     - 交互增强：悬停预览、落子动画、坐标标尺、翻转、Toast、快捷键
     - 主题：浅色/深色切换，写入 localStorage
   约定：Canva 内部固定 720x720，靠 CSS 缩放；坐标换算见 eventToCell()
   ============================================================ */

const canvas = document.getElementById('board');
const ctx = canvas.getContext('2d');

const el = {
  conn: document.getElementById('conn'),
  status: document.getElementById('meta-status'),
  turn: document.getElementById('meta-turn'),
  moves: document.getElementById('meta-moves'),
  size: document.getElementById('meta-size'),
  connect: document.getElementById('meta-connect'),
  black: document.getElementById('meta-black'),
  white: document.getElementById('meta-white'),
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
let state = null;          // 最新实时快照（SSE/首次拉取）
let prevState = null;      // 上一帧快照，用于检测新落子以触发动画
let mode = 'live';         // 'live' | 'replay'
let replay = null;         // { id, total, step }
let replayView = null;     // 回放时用于绘制的伪状态
let replayTimer = null;    // 自动播放定时器
let flipView = false;      // 是否翻转棋盘（黑方视角切换）
let geometry = { margin: 0, gap: 0 };
let hover = null;          // { r, c } 悬停预览（逻辑坐标）
let anim = null;           // { r, c, color, start } 落子动画
let rafId = 0, animRaf = 0;
let players = [];
let themeColors = {};
let toastTimer = null;

/* ---------- 通用工具 ---------- */

// 统一请求：body 存在则 POST JSON；失败统一 Toast 并返回 null
async function api(path, body) {
  try {
    const opt = body
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : {};
    const res = await fetch(path, opt);
    if (!res.ok) {
      let msg = 'HTTP ' + res.status;
      try { const j = await res.json(); if (j && j.error) msg = j.error; } catch (_) {}
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

// 右下角轻提示
function toast(msg, kind) {
  el.toast.textContent = msg;
  el.toast.className = 'toast show' + (kind ? ' toast--' + kind : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.toast.className = 'toast hidden'; }, 2600);
}

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function samePos(a, b) { return a && b && a[0] === b[0] && a[1] === b[1]; }
function posValid(p) { return p && p[0] >= 0 && p[1] >= 0; }

/* ---------- 主题 ---------- */

// 从 CSS 变量读取画布用色，保证与主题一致
function readThemeColors() {
  const cs = getComputedStyle(document.documentElement);
  const get = (k) => cs.getPropertyValue(k).trim();
  themeColors = {
    board: get('--board'),
    line: get('--board-line'),
    coord: get('--board-coord'),
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
  const saved = localStorage.getItem('gomoku-theme');
  const t = saved || (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
  document.documentElement.setAttribute('data-theme', t);
  readThemeColors();
}

/* ---------- 几何 / 坐标变换 ---------- */

// 按棋盘规模计算边距与格距（边距预留坐标标尺空间）
function computeGeometry(n) {
  const size = canvas.width;
  const margin = Math.round(size * 0.075);
  geometry = { margin, gap: (size - margin * 2) / (n - 1) };
}

// 逻辑坐标 → 屏幕坐标（翻转时上下左右镜像）
function toDisplay(r, c, n) {
  return flipView ? [n - 1 - r, n - 1 - c] : [r, c];
}

// 屏幕坐标（行列） → 逻辑坐标
function fromDisplay(dr, dc, n) {
  return flipView ? [n - 1 - dr, n - 1 - dc] : [dr, dc];
}

/* ---------- 绘制 ---------- */

function currentView() { return (mode === 'replay' && replayView) ? replayView : state; }

function scheduleRender() {
  if (!rafId) rafId = requestAnimationFrame(() => { rafId = 0; render(); });
}

// 主绘制：整盘重绘（网格 → 星位 → 坐标 → 棋子 → 悬停 → 标记 → 动画）
function render() {
  const v = currentView();
  if (!v || !v.boardSize) return;
  const n = v.boardSize;
  const size = canvas.width;
  computeGeometry(n);
  const { margin, gap } = geometry;

  ctx.fillStyle = themeColors.board;
  ctx.fillRect(0, 0, size, size);

  // 网格线
  ctx.strokeStyle = themeColors.line;
  ctx.lineWidth = 1;
  for (let i = 0; i < n; i++) {
    const p = Math.round(margin + i * gap) + 0.5;
    ctx.beginPath(); ctx.moveTo(margin, p); ctx.lineTo(size - margin, p); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p, margin); ctx.lineTo(p, size - margin); ctx.stroke();
  }

  // 星位（15 路及以上，沿用五子棋惯用点位）
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

  // 棋子
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

  drawHover(v, n, margin, gap, radius);

  // 最后一手彩色圆环（继承既有设计：黑手蓝环、白手红环）
  drawRing(v.lastBlack, n, margin, gap, radius, '#4c8dff');
  drawRing(v.lastWhite, n, margin, gap, radius, '#ff6b6b');

  drawAnim(n, margin, gap, radius);
}

// 坐标标尺：列用 A.. 字母，行用 1.. 数字
function drawCoords(n, margin, gap) {
  ctx.fillStyle = themeColors.coord;
  ctx.font = `${Math.max(10, Math.floor(gap * 0.42))}px "Segoe UI", system-ui, sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  for (let d = 0; d < n; d++) {
    const [lr, lc] = fromDisplay(0, d, n);
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

// 画一颗棋子；scale 用于落子动画
function drawStone(x, y, radius, val, scale) {
  const rr = radius * (scale || 1);
  ctx.beginPath();
  ctx.arc(x, y, rr, 0, Math.PI * 2);
  ctx.fillStyle = val > 0 ? '#111318' : '#fafafa';
  ctx.globalAlpha = (scale && scale < 1) ? 0.35 + 0.65 * scale : 1;
  ctx.fill();
  ctx.globalAlpha = 1;
  ctx.lineWidth = 1.5;
  ctx.strokeStyle = val > 0 ? '#000' : '#b9b9b9';
  ctx.stroke();
}

function drawRing(pos, n, margin, gap, radius, color) {
  if (!posValid(pos)) return;
  const [dr, dc] = toDisplay(pos[0], pos[1], n);
  ctx.beginPath();
  ctx.arc(margin + dc * gap, margin + dr * gap, radius * 0.72, 0, Math.PI * 2);
  ctx.lineWidth = 2.5;
  ctx.strokeStyle = color;
  ctx.stroke();
}

// 悬停预览：仅在人类可落子的空点显示半透明落子提示
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

// 落子动画：新落子棋子由 1.5 倍缩小到 1 倍并淡入
function drawAnim(n, margin, gap, radius) {
  if (!anim) return;
  const t = clamp((performance.now() - anim.start) / 220, 0, 1);
  const scale = 1.5 - 0.5 * t;
  const [dr, dc] = toDisplay(anim.r, anim.c, n);
  drawStone(margin + dc * gap, margin + dr * gap, radius, anim.color, scale);
}

function animTick(ts) {
  animRaf = 0;
  if (!anim) { render(); return; }
  if (ts - anim.start >= 220) { anim = null; render(); return; }
  render();
  animRaf = requestAnimationFrame(animTick);
}

function startAnim(pos, color) {
  anim = { r: pos[0], c: pos[1], color, start: performance.now() };
  if (!animRaf) animRaf = requestAnimationFrame(animTick);
}

/* ---------- 状态渲染 ---------- */

function applyState(st) {
  // 检测新落子：步数增加且最后一手位置变化 → 触发动画
  if (prevState && mode === 'live' && st.moveCount > prevState.moveCount) {
    if (!samePos(prevState.lastBlack, st.lastBlack) && posValid(st.lastBlack)) startAnim(st.lastBlack, 1);
    else if (!samePos(prevState.lastWhite, st.lastWhite) && posValid(st.lastWhite)) startAnim(st.lastWhite, -1);
  }
  prevState = state;
  state = st;
  if (mode === 'live') render();
  updateMeta(st);
  updateOverlay(st);
  updateControls(st);
}

// 侧栏信息行
function updateMeta(v) {
  if (!v) return;
  el.status.textContent = v.status;
  el.turn.textContent = (v.status === 'InProgress' || v.status === 'Replay')
    ? (v.turn > 0 ? 'Black' : 'White') : '-';
  el.moves.textContent = v.moveCount;
  el.size.textContent = v.boardSize + ' x ' + v.boardSize;
  el.connect.textContent = (v.winLength || (state && state.winLength) || 5) + ' in a row';
  el.black.textContent = v.players ? v.players.black : '-';
  el.white.textContent = v.players ? v.players.white : '-';
  el.message.textContent = v.thinking ? 'AI thinking...' : (v.message || (mode === 'replay' ? 'Replaying' : ''));
}

// 按钮可用性：回放中禁用对局操作
function updateControls(v) {
  const live = mode === 'live';
  const st = live ? v : state;
  document.getElementById('btn-undo').disabled = !(live && st && st.canUndo);
  document.getElementById('btn-abort').disabled = !(live && st && st.status !== 'Idle');
  document.getElementById('btn-save').disabled = !(live && st && st.status !== 'Idle');
}

function updateOverlay(st) {
  if (mode !== 'live') { el.overlay.classList.add('hidden'); return; }
  const map = { BlackWin: 'Black wins', WhiteWin: 'White wins', Draw: 'Draw' };
  if (map[st.status]) {
    el.overlayTitle.textContent = map[st.status];
    const p = st.players || { black: '-', white: '-' };
    el.overlayText.textContent = `${p.black} vs ${p.white} - ${st.moveCount} moves`;
    el.overlay.classList.remove('hidden');
  } else {
    el.overlay.classList.add('hidden');
  }
}

function setConn(ok) {
  el.conn.textContent = ok ? 'live' : 'reconnecting';
  el.conn.className = 'badge ' + (ok ? 'badge--on' : 'badge--off');
}

/* ---------- SSE ---------- */
function subscribe() {
  const es = new EventSource('/api/events');
  es.onopen = () => setConn(true);
  es.onmessage = (ev) => {
    try {
      const st = JSON.parse(ev.data);
      if (mode === 'live') applyState(st);
      else { prevState = state; state = st; }   // 回放中只更新数据，不打乱画面
    } catch (e) { console.warn(e); }
  };
  es.onerror = () => setConn(false);   // EventSource 会自动重连
}

/* ---------- 鼠标交互 ---------- */

// 事件坐标 → 逻辑格子（含翻转还原）；返回 null 表示落在盘外
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

// 当前是否允许人类落子
function canPlay() {
  return mode === 'live' && state && state.status === 'InProgress' && state.humanTurn && !state.thinking;
}

canvas.addEventListener('mousemove', (ev) => {
  const cell = canPlay() ? eventToCell(ev) : null;
  const changed = (cell ? cell.r !== (hover && hover.r) || cell.c !== (hover && hover.c) : hover !== null);
  hover = cell;
  if (changed) scheduleRender();
});

canvas.addEventListener('mouseleave', () => {
  if (hover) { hover = null; scheduleRender(); }
});

canvas.addEventListener('click', (ev) => {
  if (!canPlay()) return;
  const cell = eventToCell(ev);
  if (!cell) return;
  const n = state.boardSize;
  if (state.cells[cell.r * n + cell.c] !== 0) return;
  hover = null;
  api('/api/move', { r: cell.r, c: cell.c });
});

/* ---------- 对话框 ---------- */
function openNewGame() { el.modalNew.classList.remove('hidden'); }
function closeNewGame() { el.modalNew.classList.add('hidden'); }
function openList(title, html) {
  el.listTitle.textContent = title;
  el.listBody.innerHTML = html;
  el.modalList.classList.remove('hidden');
}
function closeList() { el.modalList.classList.add('hidden'); }

/* ---------- 棋手下拉（含 API 配置徽标） ---------- */
function fillPlayers() {
  const opts = players.map(p =>
    `<option value="${p.id}" data-api-ready="${p.apiReady}">${p.name}${p.apiReady ? '' : ' (no API)'}</option>`
  ).join('');
  el.selP1.innerHTML = opts;
  el.selP2.innerHTML = opts;
  el.selP1.value = '1';   // 黑：人类
  el.selP2.value = '5';   // 白：Minimax++
  el.selP1.addEventListener('change', updateNewNote);
  el.selP2.addEventListener('change', updateNewNote);
}

// 选择到未配置的 API AI 时，提示将自动回退到 Minimax++
function updateNewNote() {
  const bad = [el.selP1, el.selP2].filter(s => {
    const o = s.selectedOptions[0];
    return o && o.dataset.apiReady === 'false';
  });
  if (bad.length) {
    const names = bad.map(s => s.selectedOptions[0].textContent).join(', ');
    el.newNote.textContent = `${names}: API not configured, will fall back to Minimax++.`;
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

// 由 /api/replay 响应构造可绘制的伪状态
function buildReplayView(r) {
  return {
    boardSize: r.boardSize,
    winLength: (state && state.winLength) || 5,
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

async function setReplayStep(step) {
  if (!replay) return;
  const s = clamp(step, 0, replay.total);
  const r = await api('/api/replay', { id: replay.id, step: s });
  if (!r || !r.ok) return;
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
  el.rpPlay.innerHTML = '&#9654;';   // ▶
}

function togglePlay() {
  if (!replay) return;
  if (replayTimer) { stopPlay(); return; }
  if (replay.step >= replay.total) replay.step = 0;
  el.rpPlay.innerHTML = '&#10074;&#10074;';   // ❚❚
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

/* ---------- 按钮绑定 ---------- */
document.getElementById('btn-new').addEventListener('click', openNewGame);
document.getElementById('btn-again').addEventListener('click', () => { el.overlay.classList.add('hidden'); openNewGame(); });
document.getElementById('btn-close').addEventListener('click', () => el.overlay.classList.add('hidden'));
document.getElementById('btn-cancel').addEventListener('click', closeNewGame);
document.getElementById('btn-list-close').addEventListener('click', closeList);
document.getElementById('btn-theme').addEventListener('click', toggleTheme);
document.getElementById('btn-flip').addEventListener('click', () => {
  flipView = !flipView;
  document.getElementById('btn-flip').classList.toggle('btn--on', flipView);
  render();
});

document.getElementById('btn-start').addEventListener('click', async () => {
  const boardSize = parseInt(el.inSize.value, 10);
  const winLength = parseInt(el.inWin.value, 10);
  const p1Type = parseInt(el.selP1.value, 10) || 1;
  const p2Type = parseInt(el.selP2.value, 10) || 2;
  const storageEnabled = el.inStorage.checked;

  // 前端校验：范围与「连珠数不得超过边长」
  if (!Number.isInteger(boardSize) || boardSize < 4 || boardSize > 30) {
    showNewError('Board size must be between 4 and 30.'); return;
  }
  if (!Number.isInteger(winLength) || winLength < 4 || winLength > 15) {
    showNewError('Win length must be between 4 and 15.'); return;
  }
  if (winLength > boardSize) {
    showNewError('Win length cannot exceed board size.'); return;
  }
  showNewError('');
  closeNewGame();
  el.overlay.classList.add('hidden');
  exitReplayIfNeeded();
  prevState = null;
  await api('/api/newgame', { boardSize, winLength, p1Type, p2Type, storageEnabled });
});

document.getElementById('btn-undo').addEventListener('click', () => api('/api/undo', { n: 1 }));

document.getElementById('btn-abort').addEventListener('click', async () => {
  if (!state || state.status === 'Idle') return;
  const r = await api('/api/abort', {});
  if (r && r.ok) toast('Game aborted', 'ok');
});

document.getElementById('btn-save').addEventListener('click', async () => {
  const r = await api('/api/save', { note: 'saved from web' });
  if (r && r.ok) toast('Resume saved', 'ok');
  else if (r) toast('Save unavailable (storage off?)', 'error');
});

document.getElementById('btn-load').addEventListener('click', async () => {
  const list = await api('/api/resumes') || [];
  if (!list.length) { openList('Resumes', '<div class="list-empty">No saved resumes.</div>'); return; }
  const html = list.map(r =>
    `<div class="list-item" data-id="${r.id}">
       <div class="rowline"><span>${r.id}</span><span class="sub">${r.moves} moves</span></div>
       <div class="sub">${r.boardSize}x${r.boardSize} - ${r.black} vs ${r.white}${r.note ? ' - ' + r.note : ''}</div>
     </div>`).join('');
  openList('Resumes', html);
  el.listBody.querySelectorAll('.list-item').forEach(node => {
    node.addEventListener('click', async () => {
      closeList();
      exitReplayIfNeeded();
      prevState = null;
      await api('/api/load', { id: node.dataset.id });
    });
  });
});

document.getElementById('btn-games').addEventListener('click', async () => {
  const list = await api('/api/games') || [];
  if (!list.length) { openList('Replays', '<div class="list-empty">No saved games.</div>'); return; }
  const html = list.map(g =>
    `<div class="list-item" data-id="${g.id}">
       <div class="rowline"><span>${g.id}</span><span class="sub">${g.moves} moves</span></div>
       <div class="sub">${g.boardSize}x${g.boardSize} - ${g.black} vs ${g.white}</div>
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
  const html = `
    <div class="list-item"><div class="rowline"><span>Total games</span><b>${s.totalGames}</b></div></div>
    <div class="list-item"><div class="rowline"><span>Black wins</span><b>${s.blackWins}</b></div></div>
    <div class="list-item"><div class="rowline"><span>White wins</span><b>${s.whiteWins}</b></div></div>
    <div class="list-item"><div class="rowline"><span>Draws</span><b>${s.draws}</b></div></div>
    <div class="list-item"><div class="rowline"><span>Aborts</span><b>${s.aborts}</b></div></div>
    <div class="list-item"><div class="rowline"><span>Black moves</span><b>${s.blackTotalMoves}</b></div></div>
    <div class="list-item"><div class="rowline"><span>White moves</span><b>${s.whiteTotalMoves}</b></div></div>`;
  openList('Statistics', html);
});

document.getElementById('btn-quit').addEventListener('click', async () => {
  await api('/api/quit', {});
  document.body.innerHTML =
    '<div style="padding:48px;text-align:center;font:16px system-ui;color:#8a8f98">' +
    'Server stopped. You can close this tab.</div>';
});

/* ---------- 回放控制条按钮 ---------- */
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

  if (ev.key === 'Escape') {
    closeNewGame(); closeList(); el.overlay.classList.add('hidden');
    return;
  }
  // 回放模式下用左右方向键步进
  if (mode === 'replay') {
    if (ev.key === 'ArrowLeft') { stopPlay(); if (replay) setReplayStep(replay.step - 1); }
    if (ev.key === 'ArrowRight') { stopPlay(); if (replay) setReplayStep(replay.step + 1); }
    if (ev.key === ' ') { ev.preventDefault(); togglePlay(); }
    return;
  }
  const k = ev.key.toLowerCase();
  if (k === 'n') openNewGame();
  else if (k === 'u') { if (state && state.canUndo) api('/api/undo', { n: 1 }); }
  else if (k === 's') document.getElementById('btn-save').click();
  else if (k === 'r') document.getElementById('btn-games').click();
  else if (k === 't') toggleTheme();
  else if (k === 'f') document.getElementById('btn-flip').click();
});

// 回放中若用户开新局/载入，先退出回放
function exitReplayIfNeeded() { if (mode === 'replay') exitReplay(); }

/* ---------- 启动 ---------- */
async function init() {
  initTheme();
  const st = await api('/api/state');
  if (st) applyState(st);
  players = (await api('/api/players')) || [];
  fillPlayers();
  updateNewNote();
  subscribe();
}

init();
window.addEventListener('resize', () => render());