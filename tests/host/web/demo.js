// The web demo page (design #62, decision 2): drives demo_worker.js and
// draws what it reports. The Worker runs the core a frame at a time; this
// page keeps the frame history, the rolling MIPS, the board conversion and
// the measurement table, and holds its state in window.rex86Demo for
// drive_demo.mjs.
//
// demo.html?workload=alu&engine=wasm&budget=250000&frames=30&autostart=1 runs
// 30 frames and stops; measure=1&mframes=2 measures every workload instead
// (both for the CI smoke).

const kIpc = 1.0;  // the benchmark's assumption (design #27, decision 4)
const kHistory = 120;
const kWindowMs = 1000;

const $ = (id) => document.getElementById(id);
const quote = (text) => `"${String(text).replaceAll('"', "'")}"`;
const format = (value, digits = 1) => (Number.isFinite(value) ? value.toFixed(digits) : '—');
const group = (value) => (Number.isFinite(value) ? Math.round(value).toLocaleString('en-US') : '—');

const state = {
  ready: false, error: null, info: null, running: false, measuring: false, frames: 0, failure: '',
  ok: null, done: false, lines: [], measured: [], mips: null, wasmFileBytes: null,
};
window.rex86Demo = state;

const worker = new Worker('demo_worker.js');
let history = [];
let runStarted = null;
let lastStats = null;
let heapBytes = 0;
let stopAfter = 0;
// The current run, which the Worker echoes, and its frame time so far.
let run = 0;
let runMs = 0;

function percentile(values, p) {
  if (values.length === 0) {
    return 0;
  }
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.max(0, Math.ceil(p * sorted.length) - 1)];
}

function setStatus(text, className) {
  const status = $('status');
  status.textContent = text;
  status.className = className;
}

function selected() {
  return {
    workload: Number($('workload').value),
    engine: Number($('engine').value),
    budget: Number($('budget').value),
  };
}

function rollingMips() {
  let steps = 0;
  let ms = 0;
  for (let i = history.length - 1; i >= 0 && ms < kWindowMs; --i) {
    steps += history[i].steps;
    ms += history[i].ms;
  }
  return ms > 0 ? steps / ms / 1000 : null;
}

function drawChart() {
  const canvas = $('chart');
  const ratio = window.devicePixelRatio || 1;
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(height * ratio);
  const context = canvas.getContext('2d');
  context.scale(ratio, ratio);
  context.clearRect(0, 0, width, height);
  const style = getComputedStyle(document.documentElement);
  const values = history.map((frame) => frame.steps / frame.ms / 1000);
  if (values.length === 0) {
    return;
  }
  const top = Math.max(...values) * 1.15 || 1;
  context.fillStyle = style.getPropertyValue('--muted');
  context.font = '11px system-ui, sans-serif';
  context.fillText(`${format(top, 1)} MIPS`, 6, 13);
  context.strokeStyle = style.getPropertyValue('--accent');
  context.lineWidth = 2;
  context.beginPath();
  const step = width / Math.max(kHistory - 1, 1);
  values.forEach((value, i) => {
    const x = (kHistory - values.length + i) * step;
    const y = height - 4 - (value / top) * (height - 20);
    if (i === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  });
  context.stroke();
}

function drawBoards(mips) {
  const body = $('boards');
  body.replaceChildren();
  for (const board of state.info?.boards ?? []) {
    const row = body.insertRow();
    const ratio = mips ? mips / (board.clock_mhz * kIpc) : null;
    const frameMs = mips ? (board.clock_mhz * kIpc * 1000) / (60 * mips) : null;
    for (const [text, number] of [[board.name, false], [board.cpu, false],
      [ratio === null ? '—' : `${ratio.toFixed(3)}×`, true],
      [frameMs === null ? '—' : `${group(frameMs)} ms`, true]]) {
      const cell = row.insertCell();
      cell.textContent = text;
      if (number) {
        cell.className = 'number';
      }
    }
  }
}

function drawDetails() {
  const s = lastStats ?? {};
  const rows = [
    ['blocks translated', group(s.blocks_translated)],
    ['translated block runs', group(s.block_runs)],
    ['left to the interpreter', group(s.interpreter_exits)],
    ['modules installed (wasm)', group(s.wasm_modules)],
    ['modules evicted', group(s.modules_evicted)],
    ['translations dropped (code changed)', group(s.blocks_invalidated)],
    ['engine memory', s.engine_bytes ? `${format(s.engine_bytes / 1048576, 2)} MiB` : '—'],
    ['generated wasm', s.wasm_bytes ? `${format(s.wasm_bytes / 1024, 0)} KiB` : '—'],
    ['wasm heap', heapBytes ? `${format(heapBytes / 1048576, 1)} MiB` : '—'],
    ['core module', state.wasmFileBytes ? `${format(state.wasmFileBytes / 1024, 0)} KiB` : '—'],
  ];
  const body = $('details');
  body.replaceChildren();
  for (const [label, value] of rows) {
    const row = body.insertRow();
    row.insertCell().textContent = label;
    const cell = row.insertCell();
    cell.textContent = value;
    cell.className = 'number';
  }
}

function drawReadouts() {
  const s = lastStats ?? {};
  const mips = rollingMips();
  state.mips = mips;
  $('mips').textContent = format(mips);
  $('mips-run').textContent = runStarted === null || runMs === 0 ? '—' : format(s.retired / runMs / 1000);
  $('frame-ms').textContent = history.length ? `${format(history.at(-1).ms, 1)} ms` : '—';
  $('retired').textContent = group(s.retired);
  $('laps').textContent = group(s.laps);
  $('translated-share').textContent = s.retired ? `${format((100 * s.translated_steps) / s.retired, 0)}%` : '—';
  drawBoards(mips);
  drawDetails();
  drawChart();
}

function report() {
  const { workload, engine, budget } = selected();
  const lines = [
    `[rex86-demo] user_agent=${quote(navigator.userAgent)}`,
    `[rex86-demo] version=${state.info?.version} core_module_bytes=${state.wasmFileBytes ?? 'unknown'} ` +
      `heap_bytes=${heapBytes} hardware_concurrency=${navigator.hardwareConcurrency ?? 'unknown'}`,
  ];
  if (lastStats && !state.measuring) {
    lines.push(`[rex86-demo] live workload=${state.info.workloads[workload]} engine=${state.info.engines[engine]} ` +
      `frame_budget=${budget} frames=${state.frames} mips=${format(state.mips, 3)} laps=${lastStats.laps} ` +
      `blocks_translated=${lastStats.blocks_translated} modules=${lastStats.wasm_modules} ` +
      `engine_bytes=${lastStats.engine_bytes} result=${state.failure ? 'fail' : 'ok'}`);
  }
  for (const row of state.measured) {
    lines.push(`[rex86-demo] workload=${row.workload} engine=${row.engine} frames=${row.frames} ` +
      `frame_budget=${row.budget} mips=${format(row.mips, 3)} frame_ms_p50=${format(row.p50, 3)} ` +
      `frame_ms_p99=${format(row.p99, 3)} first_frame_ms=${format(row.firstFrameMs, 3)} ` +
      `engine_bytes=${row.engineBytes} verified=${row.ok ? 'ok' : 'fail'}` +
      (row.ok ? '' : ` reason=${quote(row.failure)}`));
  }
  state.lines = lines;
  $('report').value = lines.join('\n');
}

function setRunning(running) {
  state.running = running;
  $('run').textContent = running ? 'Stop' : 'Start';
  $('measure').disabled = running || state.measuring || !state.ready;
}

function start() {
  history = [];
  lastStats = null;
  state.frames = 0;
  state.failure = '';
  state.ok = null;
  state.done = false;
  runStarted = performance.now();
  runMs = 0;
  run += 1;
  const { workload, engine, budget } = selected();
  setStatus(`running ${state.info.workloads[workload]} on the ${state.info.engines[engine]}`, 'running');
  setRunning(true);
  worker.postMessage({ type: 'start', run, workload, engine, budget });
}

function stop(text, className) {
  worker.postMessage({ type: 'stop' });
  setRunning(false);
  setStatus(text, className);
  state.done = true;
  report();
}

function onFrame({ run: frameRun, ok, stats, failure, heapBytes: heap }) {
  if (!state.running || frameRun !== run) {
    return;
  }
  heapBytes = heap;
  lastStats = stats;
  if (!ok) {
    state.failure = failure || 'the frame failed';
    state.ok = false;
    drawReadouts();
    stop(`stopped: ${state.failure}`, 'fail');
    return;
  }
  state.frames += 1;
  const ms = Math.max(stats.frame_ms, 0.001);
  runMs += ms;
  history.push({ steps: Number($('budget').value), ms });
  if (history.length > kHistory) {
    history.shift();
  }
  drawReadouts();
  if (stopAfter > 0 && state.frames >= stopAfter) {
    state.ok = true;
    stop(`stopped after ${state.frames} frames`, 'ok');
  }
}

function measure() {
  const { engine, budget } = selected();
  const frames = Number($('measure-frames').value);
  state.measuring = true;
  state.measured = [];
  state.done = false;
  state.ok = null;
  $('measured').replaceChildren();
  $('measure').disabled = true;
  $('run').disabled = true;
  setStatus(`measuring every workload on the ${state.info.engines[engine]}`, 'running');
  worker.postMessage({
    type: 'measure', engine, frames, budget, workloads: state.info.workloads.map((_, index) => index),
  });
  state.measureSettings = { engine, frames, budget };
}

function onMeasured(message) {
  const { engine, frames, budget } = state.measureSettings;
  const totalMs = message.frameMs.reduce((sum, ms) => sum + ms, 0);
  const row = {
    workload: state.info.workloads[message.workload], engine: state.info.engines[engine], frames, budget,
    mips: totalMs > 0 ? (message.frameMs.length * budget) / totalMs / 1000 : null,
    p50: percentile(message.frameMs, 0.5), p99: percentile(message.frameMs, 0.99),
    firstFrameMs: message.firstFrameMs, ok: message.ok, failure: message.failure,
    engineBytes: message.stats.engine_bytes,
  };
  heapBytes = message.heapBytes;
  state.measured.push(row);
  const tr = $('measured').insertRow();
  for (const [text, number] of [[row.workload, false], [format(row.mips), true], [`${format(row.p50)} ms`, true],
    [`${format(row.p99)} ms`, true], [`${format(row.firstFrameMs)} ms`, true],
    [row.ok ? 'verified' : `failed: ${row.failure}`, false]]) {
    const cell = tr.insertCell();
    cell.textContent = text;
    cell.className = number ? 'number' : row.ok ? 'ok' : 'fail';
  }
  setStatus(`measured ${state.measured.length} of ${state.info.workloads.length}`, 'running');
  report();
}

function onMeasureDone() {
  state.measuring = false;
  state.ok = state.measured.every((row) => row.ok);
  state.done = true;
  $('run').disabled = false;
  setRunning(false);
  setStatus(state.ok ? 'measured every workload, all verified' : 'measured, with failures (see the table)',
    state.ok ? 'ok' : 'fail');
  drawDetails();
  report();
}

worker.onmessage = ({ data }) => {
  if (data.type === 'ready') {
    state.info = data.info;
    state.wasmFileBytes = data.wasmFileBytes;
    onReady();
  } else if (data.type === 'frame') {
    onFrame(data);
  } else if (data.type === 'measured') {
    onMeasured(data);
  } else if (data.type === 'measure-done') {
    onMeasureDone();
  } else if (data.type === 'error') {
    state.error = data.text;
    state.done = true;
    setStatus(data.text, 'fail');
  }
};
worker.onerror = (event) => {
  event.preventDefault();
  state.error = `worker error: ${event.message}`;
  state.done = true;
  setStatus(state.error, 'fail');
};

function onReady() {
  const params = new URL(location.href).searchParams;
  for (const [id, names] of [['workload', state.info.workloads], ['engine', state.info.engines]]) {
    const select = $(id);
    names.forEach((name, index) => select.add(new Option(name, String(index))));
    const wanted = names.indexOf(params.get(id));
    select.value = String(wanted >= 0 ? wanted : names.indexOf(id === 'workload' ? 'mixed' : 'wasm'));
    if (select.value === '-1' || select.value === '') {
      select.value = '0';
    }
  }
  if (/^[1-9][0-9]*$/.test(params.get('budget') ?? '')) {
    const option = new Option(Number(params.get('budget')).toLocaleString('en-US'), params.get('budget'));
    $('budget').add(option);
    $('budget').value = params.get('budget');
  }
  if (/^[1-9][0-9]*$/.test(params.get('mframes') ?? '')) {
    $('measure-frames').add(new Option(params.get('mframes')));
    $('measure-frames').value = params.get('mframes');
  }
  stopAfter = /^[1-9][0-9]*$/.test(params.get('frames') ?? '') ? Number(params.get('frames')) : 0;
  state.ready = true;
  $('run').disabled = false;
  $('measure').disabled = false;
  setStatus(`ready: rex86 ${state.info.version}`, 'muted');
  drawReadouts();
  report();
  if (params.get('measure') === '1') {
    measure();
  } else if (params.get('autostart') === '1') {
    start();
  }
}

$('agent').textContent = navigator.userAgent;
$('run').addEventListener('click', () => {
  if (state.running) {
    stop(`stopped after ${state.frames} frames`, 'muted');
  } else {
    start();
  }
});
for (const id of ['workload', 'engine', 'budget']) {
  $(id).addEventListener('change', () => {
    if (state.running) {
      start();
    }
  });
}
$('measure').addEventListener('click', measure);
$('copy').addEventListener('click', async () => {
  report();
  try {
    await navigator.clipboard.writeText($('report').value);
  } catch {
    $('report').select();
  }
});
window.addEventListener('resize', drawChart);
