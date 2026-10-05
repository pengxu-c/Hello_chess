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

/* ---------- 通信 ---------- */

// 发一条命令给内核。所有对棋局的操作都走这里（唯一写入口）。
async function cmd(op, payload) {
  return api('/api/cmd', Object.assign({ op }, payload || {}));
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
  el.toast.className = 'toast show' + (kind ? ' toast--' + kind : '');
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

/* ---------- 主题 ---------- */
function readThemeColors() {
  const cs = getComputedStyle(document.documentElement);
  const get = (k) => cs.getPropertyValue(k).trim();
  themeColors = { board: get('--board'), line: get('--board-line'), coord: get('--board-coord') };
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

/* ---------- 几何 ---------- */
function computeGeometry(n) {
  const size = canvas.width;
  const margin = Math.round(size * 0.075);
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

  ctx.fillStyle = themeColors.board;
  ctx.fillRect(0, 0, size, size);

  ctx.strokeStyle = themeColors.line;
  ctx.lineWidth = 1;
  for (let i = 0; i < n; i++) {
    const p = Math.round(margin + i * gap) + 0.5;
    ctx.beginPath(); ctx.moveTo(margin, p); ctx.lineTo(size - margin, p); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p, margin); ctx.lineTo(p, size - margin); ctx.stroke();
  }

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

  drawHover(v, n, margin, gap, radius);
  drawRing(v.lastBlack, n, margin, gap, radius, '#4c8dff');
  drawRing(v.lastWhite, n, margin, gap, radius, '#ff6b6b');
  drawAnim(n, margin, gap, radius);
}

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

function updateMeta(v) {
  if (!v) return;
  const p = v.players || { black: '-', white: '-' };
  el.status.textContent = v.status;
  el.turn.textContent = (v.status === 'InProgress' || v.status === 'Replay')
    ? (v.turn > 0 ? 'Black' : 'White') : '-';
  el.moves.textContent = v.moveCount;
  el.size.textContent = v.boardSize + ' x ' + v.boardSize;
  el.connect.textContent = (v.winLength || 5) + ' in a row';
  el.black.textContent = p.black || '-';
  el.white.textContent = p.white || '-';
  el.message.textContent = v.thinking ? 'AI thinking...'
    : (v.message || (mode === 'replay' ? 'Replaying' : ''));
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
      else { prevState = state; state = st; }
    } catch (e) { console.warn(e); }
  };
  es.onerror = () => setConn(false);
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
function openNewGame() { el.modalNew.classList.remove('hidden'); }
function closeNewGame() { el.modalNew.classList.add('hidden'); }
function openList(title, html) {
  el.listTitle.textContent = title;
  el.listBody.innerHTML = html;
  el.modalList.classList.remove('hidden');
}
function closeList() { el.modalList.classList.add('hidden'); }

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
  const black = el.selP1.value || 'human';
  const white = el.selP2.value || 'human';
  const storageEnabled = el.inStorage.checked;

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
  await cmd('new', { boardSize, winLength, black, white, storageEnabled });
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

  if (ev.key === 'Escape') {
    closeNewGame(); closeList(); el.overlay.classList.add('hidden');
    return;
  }
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
  subscribe();
}

init();
window.addEventListener('resize', () => render());
