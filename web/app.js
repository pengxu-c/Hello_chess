/* ============================================================
   app.js - 前端逻辑
   - 通过 SSE (/api/events) 接收状态变化，Canvas 绘制棋盘
   - 点击落子 POST /api/move；按钮调用 undo/save/resumes/games/stats/quit
   - 最后一手用彩色圆环标记
   ============================================================ */

const canvas = document.getElementById('board');
const ctx = canvas.getContext('2d');

const el = {
  conn: document.getElementById('conn'),
  status: document.getElementById('meta-status'),
  turn: document.getElementById('meta-turn'),
  moves: document.getElementById('meta-moves'),
  size: document.getElementById('meta-size'),
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
};

let state = null;
let geometry = { margin: 0, gap: 0 };
let players = [];

/* ---------- 通用请求 ---------- */
async function api(path, body) {
  try {
    const opt = body
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : {};
    const res = await fetch(path, opt);
    return res.ok ? await res.json() : null;
  } catch (e) {
    console.warn('request failed:', path, e);
    return null;
  }
}

/* ---------- 绘制 ---------- */
function computeGeometry(n) {
  const size = canvas.width;
  const margin = Math.round(size * 0.05);
  geometry = { margin, gap: (size - margin * 2) / (n - 1) };
}

function drawBoard(st) {
  if (!st || !st.boardSize) return;
  const n = st.boardSize;
  const size = canvas.width;
  computeGeometry(n);
  const { margin, gap } = geometry;

  ctx.fillStyle = '#dcb079';
  ctx.fillRect(0, 0, size, size);

  ctx.strokeStyle = '#6b4a25';
  ctx.lineWidth = 1;
  for (let i = 0; i < n; i++) {
    const p = Math.round(margin + i * gap) + 0.5;
    ctx.beginPath(); ctx.moveTo(margin, p); ctx.lineTo(size - margin, p); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p, margin); ctx.lineTo(p, size - margin); ctx.stroke();
  }

  if (n >= 15) {
    const pts = [[3, 3], [3, n - 4], [n - 4, 3], [n - 4, n - 4], [(n - 1) / 2, (n - 1) / 2]];
    ctx.fillStyle = '#6b4a25';
    for (const [r, c] of pts) {
      ctx.beginPath();
      ctx.arc(margin + c * gap, margin + r * gap, 3, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  const radius = gap * 0.42;
  const cells = st.cells || [];
  for (let r = 0; r < n; r++) {
    for (let c = 0; c < n; c++) {
      const v = cells[r * n + c];
      if (!v) continue;
      const x = margin + c * gap;
      const y = margin + r * gap;
      ctx.beginPath();
      ctx.arc(x, y, radius, 0, Math.PI * 2);
      ctx.fillStyle = v > 0 ? '#111318' : '#fafafa';
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = v > 0 ? '#000' : '#b9b9b9';
      ctx.stroke();
    }
  }

  drawRing(st.lastBlack, radius, '#4c8dff');
  drawRing(st.lastWhite, radius, '#ff6b6b');
}

function drawRing(pos, radius, color) {
  if (!pos || pos[0] < 0 || pos[1] < 0) return;
  const { margin, gap } = geometry;
  const x = margin + pos[1] * gap;
  const y = margin + pos[0] * gap;
  ctx.beginPath();
  ctx.arc(x, y, radius * 0.72, 0, Math.PI * 2);
  ctx.lineWidth = 2.5;
  ctx.strokeStyle = color;
  ctx.stroke();
}

/* ---------- 状态渲染 ---------- */
function applyState(st) {
  state = st;
  drawBoard(st);

  el.status.textContent = st.status;
  el.turn.textContent = st.status === 'InProgress' ? (st.turn > 0 ? 'Black' : 'White') : '-';
  el.moves.textContent = st.moveCount;
  el.size.textContent = st.boardSize + ' x ' + st.boardSize;
  el.black.textContent = st.players ? st.players.black : '-';
  el.white.textContent = st.players ? st.players.white : '-';
  el.message.textContent = st.thinking ? 'AI thinking...' : (st.message || '');

  document.getElementById('btn-undo').disabled = !st.canUndo;

  updateOverlay(st);
}

function updateOverlay(st) {
  const map = { BlackWin: 'Black wins', WhiteWin: 'White wins', Draw: 'Draw' };
  if (map[st.status]) {
    el.overlayTitle.textContent = map[st.status];
    el.overlayText.textContent =
      `${st.players.black} vs ${st.players.white} - ${st.moveCount} moves`;
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
    try { applyState(JSON.parse(ev.data)); } catch (e) { console.warn(e); }
  };
  es.onerror = () => setConn(false);   // EventSource 会自动重连
}

/* ---------- 落子 ---------- */
canvas.addEventListener('click', (ev) => {
  if (!state || state.status !== 'InProgress' || !state.humanTurn || state.thinking) return;

  const rect = canvas.getBoundingClientRect();
  const x = (ev.clientX - rect.left) * (canvas.width / rect.width);
  const y = (ev.clientY - rect.top) * (canvas.height / rect.height);

  const { margin, gap } = geometry;
  const c = Math.round((x - margin) / gap);
  const r = Math.round((y - margin) / gap);
  const n = state.boardSize;

  if (r < 0 || r >= n || c < 0 || c >= n) return;
  if (state.cells[r * n + c] !== 0) return;
  api('/api/move', { r, c });
});

/* ---------- 对话框 ---------- */
function openNewGame() {
  el.modalNew.classList.remove('hidden');
}
function closeNewGame() {
  el.modalNew.classList.add('hidden');
}
function openList(title, html) {
  el.listTitle.textContent = title;
  el.listBody.innerHTML = html;
  el.modalList.classList.remove('hidden');
}
function closeList() {
  el.modalList.classList.add('hidden');
}

/* ---------- 棋手下拉 ---------- */
function fillPlayers() {
  const opts = players.map(p => `<option value="${p.id}">${p.name}</option>`).join('');
  el.selP1.innerHTML = opts;
  el.selP2.innerHTML = opts;
  el.selP1.value = '1';   // 黑：人类
  el.selP2.value = '5';   // 白：Minimax++
}

/* ---------- 按钮绑定 ---------- */
document.getElementById('btn-new').addEventListener('click', openNewGame);
document.getElementById('btn-again').addEventListener('click', openNewGame);
document.getElementById('btn-close').addEventListener('click', () => el.overlay.classList.add('hidden'));
document.getElementById('btn-cancel').addEventListener('click', closeNewGame);
document.getElementById('btn-list-close').addEventListener('click', closeList);

document.getElementById('btn-start').addEventListener('click', async () => {
  const boardSize = parseInt(el.inSize.value, 10) || 15;
  const winLength = parseInt(el.inWin.value, 10) || 5;
  const p1Type = parseInt(el.selP1.value, 10) || 1;
  const p2Type = parseInt(el.selP2.value, 10) || 2;
  const storageEnabled = el.inStorage.checked;
  closeNewGame();
  el.overlay.classList.add('hidden');
  await api('/api/newgame', { boardSize, winLength, p1Type, p2Type, storageEnabled });
});

document.getElementById('btn-undo').addEventListener('click', () => api('/api/undo', { n: 1 }));

document.getElementById('btn-save').addEventListener('click', async () => {
  const r = await api('/api/save', { note: 'saved from web' });
  el.message.textContent = (r && r.ok) ? 'Resume saved.' : 'Save failed (storage off?)';
});

document.getElementById('btn-load').addEventListener('click', async () => {
  const list = await api('/api/resumes') || [];
  if (!list.length) { openList('Resumes', '<div class="list-empty">No saved resumes.</div>'); return; }
  const html = list.map(r =>
    `<div class="list-item" data-id="${r.id}">
       <div>${r.id}</div>
       <div class="sub">${r.boardSize}x${r.boardSize} - ${r.black} vs ${r.white} - ${r.moves} moves</div>
     </div>`).join('');
  openList('Resumes', html);
  el.listBody.querySelectorAll('.list-item').forEach(node => {
    node.addEventListener('click', async () => {
      await api('/api/load', { id: node.dataset.id });
      closeList();
    });
  });
});

document.getElementById('btn-games').addEventListener('click', async () => {
  const list = await api('/api/games') || [];
  if (!list.length) { openList('Replays', '<div class="list-empty">No saved games.</div>'); return; }
  const html = list.map(g =>
    `<div class="list-item" data-id="${g.id}">
       <div>${g.id}</div>
       <div class="sub">${g.boardSize}x${g.boardSize} - ${g.black} vs ${g.white} - ${g.moves} moves</div>
     </div>`).join('');
  openList('Replays', html);
  el.listBody.querySelectorAll('.list-item').forEach(node => {
    node.addEventListener('click', async () => {
      const r = await api('/api/replay', { id: node.dataset.id, step: -1 });
      closeList();
      if (r && r.ok) {
        // 用回放结果覆盖当前显示（下一次 SSE 推送会恢复实时状态）
        const last = r.last || [-1, -1];
        const pseudo = {
          status: 'Replay', boardSize: r.boardSize, cells: r.cells,
          turn: 1, lastBlack: r.lastColor > 0 ? last : [-1, -1],
          lastWhite: r.lastColor < 0 ? last : [-1, -1],
          moveCount: r.step, canUndo: false, thinking: false, humanTurn: false,
          players: { black: r.black, white: r.white },
          message: `Replay ${r.step}/${r.total}`,
        };
        state = pseudo;
        drawBoard(pseudo);
        el.message.textContent = pseudo.message;
      }
    });
  });
});

document.getElementById('btn-stats').addEventListener('click', async () => {
  const s = await api('/api/stats');
  if (!s) { openList('Statistics', '<div class="list-empty">Unavailable.</div>'); return; }
  const html = `
    <div class="list-item">Total games <b>${s.totalGames}</b></div>
    <div class="list-item">Black wins <b>${s.blackWins}</b></div>
    <div class="list-item">White wins <b>${s.whiteWins}</b></div>
    <div class="list-item">Draws <b>${s.draws}</b></div>
    <div class="list-item">Aborts <b>${s.aborts}</b></div>
    <div class="list-item">Black moves <b>${s.blackTotalMoves}</b></div>
    <div class="list-item">White moves <b>${s.whiteTotalMoves}</b></div>`;
  openList('Statistics', html);
});

document.getElementById('btn-quit').addEventListener('click', async () => {
  await api('/api/quit', {});
  document.body.innerHTML =
    '<div style="padding:48px;text-align:center;font:16px system-ui;color:#e8eaed">' +
    'Server stopped. You can close this tab.</div>';
});

/* ---------- 启动 ---------- */
async function init() {
  const st = await api('/api/state');
  if (st) applyState(st);
  players = (await api('/api/players')) || [];
  fillPlayers();
  subscribe();
}

init();
window.addEventListener('resize', () => drawBoard(state));