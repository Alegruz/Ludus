"use strict";
const canvas = document.getElementById('canvas');
const status = document.getElementById('status');
const pause = document.getElementById('pause');
document.getElementById('restart').onclick = () => location.reload();
// The RHI replaces the canvas when switching from WebGPU to WebGL 2. Delegate
// input so fallback preserves gameplay controls on the replacement canvas.
document.addEventListener('click', event => {
  if (event.target.id === 'canvas') event.target.focus();
});
var Module = {
  canvas,
  heldActions: 0,
  pendingActions: 0,
  paused: false,
  onAbort: () => { status.textContent = 'The sample could not start. Try restarting or another browser.'; status.dataset.state = 'failed'; },
  printErr: message => console.error(message),
  onRuntimeInitialized: function() {
    if (typeof Module._SetPaused === 'function') pause.hidden = false;
  },
};
const actions = new Map([['KeyA', 1], ['ArrowLeft', 1], ['KeyD', 2], ['ArrowRight', 2], ['Space', 4]]);
const held = new Set();
function updateInput() { Module.heldActions = [...held].reduce((bits, key) => bits | actions.get(key), 0); }
function clearInput() { held.clear(); Module.pendingActions = 0; updateInput(); }
document.addEventListener('keydown', event => {
  if (event.code === 'Escape' && parent !== window) parent.postMessage({type: 'ludus-close-player'}, location.origin);
  if (event.target.id === 'canvas' && actions.has(event.code)) { held.add(event.code); Module.pendingActions |= actions.get(event.code); updateInput(); event.preventDefault(); }
});
document.addEventListener('keyup', event => {
  if (event.target.id === 'canvas' && actions.has(event.code)) { held.delete(event.code); updateInput(); event.preventDefault(); }
});
document.addEventListener('blur', event => {
  if (event.target.id === 'canvas') clearInput();
}, true);
window.addEventListener('blur', clearInput);
document.addEventListener('visibilitychange', () => { if (document.hidden) clearInput(); });
pause.onclick = () => {
  Module.paused = !Module.paused;
  Module._SetPaused(Module.paused ? 1 : 0);
  pause.textContent = Module.paused ? 'Resume' : 'Pause';
};
globalThis.ludusPlayerStatus = function(state, frames) {
  status.dataset.state = state;
  status.dataset.frames = frames;
  status.textContent = state === 'playing' ? 'Playing' : state === 'paused' ? 'Paused' : state === 'loading' ? 'Loading graphics…' : 'Graphics could not start. Try restarting or another browser.';
};
const id = location.pathname.split('/').filter(Boolean).at(-2);
document.getElementById('title').textContent = {'cornell-box':'Cornell Box', 'live-edit-game':'Live Edit Game', 'scripted-game':'Scripted Game'}[id] || 'Ludus sample';
if (id === 'cornell-box') document.getElementById('instructions').textContent = 'Cornell Box: the sample renders the lit scene using the browser graphics backend.';
if (id === 'scripted-game') document.getElementById('instructions').textContent = 'Click the canvas, then press Space twice to open the encounter. The Luau interaction turns the scene green.';
