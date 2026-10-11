// The two-version benchmark comparison (design #52, decision 4): runs the
// rex86_bench of versions A and B on this device in Workers started from
// each version's own directory, alternating which goes first each round so a
// phone heating up does not land on one version, and shows the median MIPS
// per engine and workload with B/A, marked only past ±5% when the two
// sides' runs do not overlap. Results stay on the page; the report box
// holds [rex86-compare] lines for a person to carry, and
// window.rex86Compare holds them for drive_pages.mjs.
//
// The root comes from the newest tag and each version directory from its
// own, so this file relies only on the contract the design states:
// vX/worker.js?tool=&args= starts a tool, its messages are
// { kind: 'out' | 'err' | 'exit', text, code }, and the benchmark prints
// "[rex86-bench] version=", "workload=<name> ... mips=<value> ... verified=ok",
// "workload=<name> verified=fail" and "engine_available=false".
//
// compare.html?a=0.0.23&b=0.0.24 preselects the versions; engines=, rounds=,
// frames= (passed to rex86_bench --frames), settle=ms and autostart=1 serve
// the CI smoke.

const kTimeoutMs = 15 * 60 * 1000;
const kEngines = ['interpreter', 'wasm', 'evaluator'];
const kBetter = 1.05;
const kWorse = 0.95;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const quote = (text) => `"${String(text).replaceAll('"', "'")}"`;
const $ = (id) => document.getElementById(id);

const state = { done: false, ok: null, error: null, lines: [] };
window.rex86Compare = state;

// Runs one version's benchmark; resolves with its exit code and output lines.
function runBench(version, engine, frames) {
  return new Promise((resolve) => {
    const args = ['--engine', engine, ...(frames ? ['--frames', String(frames)] : [])];
    const worker = new Worker(`v${version}/worker.js?tool=rex86_bench` +
      `&args=${encodeURIComponent(JSON.stringify(args))}`);
    const lines = [];
    const finish = (code, text) => {
      clearTimeout(timer);
      worker.terminate();
      if (text) {
        lines.push(text);
      }
      resolve({ code, lines });
    };
    const timer = setTimeout(() => finish(124, `timed out after ${kTimeoutMs / 1000} s`), kTimeoutMs);
    worker.onmessage = ({ data }) => {
      if (data.kind === 'exit') {
        finish(data.code, data.text);
      } else {
        lines.push(data.text);
      }
    };
    worker.onerror = (event) => {
      event.preventDefault();
      finish(-1, `worker error: ${event.message}`);
    };
  });
}

// A run's outcome: 'ok' with { workload: mips }, 'unavailable' when the
// version lacks the engine, or 'fail' with a reason.
function judge(version, { code, lines }) {
  const mips = {};
  let reported = null;
  const failed = [];
  for (const line of lines) {
    if (/^\[rex86-bench\] engine=\S+ engine_available=false/.test(line)) {
      return { outcome: 'unavailable', mips };
    }
    const head = /^\[rex86-bench\] version=(\S+)/.exec(line);
    if (head) {
      reported = head[1];
    }
    const workload = /^\[rex86-bench\] workload=(\S+) frames=.* mips=([0-9.]+).* verified=ok/.exec(line);
    if (workload) {
      mips[workload[1]] = Number(workload[2]);
    }
    const bad = /^\[rex86-bench\] workload=(\S+) verified=fail/.exec(line);
    if (bad) {
      failed.push(bad[1]);
    }
  }
  if (reported === null && code !== 0) {
    return { outcome: 'fail', mips, reason: `exit ${code}: ${lines.at(-1) ?? ''}` };
  }
  if (reported !== version) {
    return { outcome: 'fail', mips, reason: `the build reports version ${reported ?? '(none)'}, not ${version}` };
  }
  if (failed.length) {
    return { outcome: 'fail', mips, reason: `verified=fail: ${failed.join(',')}` };
  }
  if (code !== 0) {
    return { outcome: 'fail', mips, reason: `exit ${code}: ${lines.at(-1) ?? ''}` };
  }
  if (Object.keys(mips).length === 0) {
    return { outcome: 'fail', mips, reason: 'no workload figures' };
  }
  return { outcome: 'ok', mips };
}

function median(values) {
  const sorted = [...values].sort((x, y) => x - y);
  const middle = sorted.length >> 1;
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function cell(row, text, className) {
  const td = row.insertCell();
  td.textContent = text;
  if (className) {
    td.className = className;
  }
}

// One table per engine: workloads as rows, A's and B's medians and B/A.
function renderTables(a, b, engines, runs) {
  const container = $('tables');
  container.replaceChildren();
  const summary = [];
  for (const engine of engines) {
    // Runs are told apart by side, not version: A against itself measures
    // this device's run-to-run variation.
    const of = (side) => runs.filter((run) => run.engine === engine && run.side === side);
    const [runsA, runsB] = [of('A'), of('B')];
    const heading = document.createElement('h2');
    heading.textContent = engine;
    container.append(heading);
    const unavailable = [[runsA, a], [runsB, b]].filter(([list]) =>
      list.some((run) => run.outcome === 'unavailable')).map(([, version]) => `v${version}`);
    if (unavailable.length) {
      const note = document.createElement('p');
      note.className = 'muted';
      note.textContent = `not available in ${[...new Set(unavailable)].join(' and ')}`;
      container.append(note);
    }
    const workloads = [...new Set([...runsA, ...runsB].flatMap((run) =>
      run.outcome === 'ok' ? Object.keys(run.mips) : []))];
    if (workloads.length === 0) {
      continue;
    }
    const wrap = document.createElement('div');
    wrap.className = 'table-wrap';
    const table = document.createElement('table');
    table.createTHead().innerHTML =
      `<tr><th>workload</th><th class="number">A v${a}</th><th class="number">B v${b}</th><th class="number">B/A</th></tr>`;
    const body = table.createTBody();
    for (const workload of workloads) {
      const values = (list) => list.filter((run) => run.outcome === 'ok' && workload in run.mips)
        .map((run) => run.mips[workload]);
      const [valuesA, valuesB] = [values(runsA), values(runsB)];
      const medianA = valuesA.length ? median(valuesA) : null;
      const medianB = valuesB.length ? median(valuesB) : null;
      const ratio = medianA && medianB ? medianB / medianA : null;
      // A difference is marked only past the bound and when the two sides'
      // runs do not overlap: the same build against itself already moves
      // the median by more than 5% on some workloads.
      const separate = ratio !== null &&
        (Math.min(...valuesB) > Math.max(...valuesA) || Math.max(...valuesB) < Math.min(...valuesA));
      const tone = !separate ? '' : ratio >= kBetter ? ' better' : ratio <= kWorse ? ' worse' : '';
      const row = body.insertRow();
      cell(row, workload);
      cell(row, medianA === null ? '—' : medianA.toFixed(1), 'number');
      cell(row, medianB === null ? '—' : medianB.toFixed(1), 'number');
      cell(row, ratio === null ? '—' : `${ratio.toFixed(2)}×`, `number${tone}`);
      row.title = `A: ${valuesA.map((v) => v.toFixed(1)).join(', ') || '—'}\nB: ${valuesB.map((v) => v.toFixed(1)).join(', ') || '—'}`;
      summary.push({ engine, workload, medianA, medianB, ratio, valuesA, valuesB, separate,
        runs: Math.min(valuesA.length, valuesB.length) });
    }
    wrap.append(table);
    container.append(wrap);
  }
  return summary;
}

function addRunOutput(run) {
  const details = document.createElement('details');
  const label = document.createElement('summary');
  label.textContent = `round ${run.round} ${run.side} v${run.version} ${run.engine}: ${run.outcome}` +
    (run.reason ? ` (${run.reason})` : '');
  label.className = run.outcome === 'fail' ? 'fail' : '';
  const pre = document.createElement('pre');
  pre.textContent = run.lines.join('\n');
  details.append(label, pre);
  $('runs').append(details);
}

async function compare(a, b, engines, rounds, frames, settleMs) {
  const status = $('status');
  $('start').disabled = true;
  $('tables').replaceChildren();
  $('runs').replaceChildren();
  $('report').value = '';
  state.done = false;
  const plan = [];
  for (let round = 1; round <= rounds; ++round) {
    const sides = [{ side: 'A', version: a }, { side: 'B', version: b }];
    const order = round % 2 ? sides : [...sides].reverse();
    for (const engine of engines) {
      for (const side of order) {
        plan.push({ round, engine, ...side });
      }
    }
  }
  const runs = [];
  for (const [index, step] of plan.entries()) {
    if (index > 0) {
      await sleep(settleMs);
    }
    status.textContent = `running ${index + 1}/${plan.length}: round ${step.round}, ${step.side} v${step.version}, ${step.engine}`;
    status.className = 'running';
    const output = await runBench(step.version, step.engine, frames);
    const run = { ...step, ...judge(step.version, output), lines: output.lines };
    runs.push(run);
    addRunOutput(run);
    renderTables(a, b, engines, runs);
  }
  const summary = renderTables(a, b, engines, runs);
  const failedRuns = runs.filter((run) => run.outcome === 'fail');
  const lines = [
    `[rex86-compare] user_agent=${quote(navigator.userAgent)}`,
    `[rex86-compare] a=${a} b=${b} engines=${engines.join(',')} rounds=${rounds} frames=${frames ?? 'default'}`,
    ...runs.map((run) => `[rex86-compare] run round=${run.round} side=${run.side} version=${run.version} engine=${run.engine} ` +
      `result=${run.outcome}${run.reason ? ` reason=${quote(run.reason)}` : ''}`),
    ...summary.map((row) => `[rex86-compare] engine=${row.engine} workload=${row.workload} ` +
      `a_mips=${row.medianA?.toFixed(3) ?? 'none'} b_mips=${row.medianB?.toFixed(3) ?? 'none'} ` +
      `ratio=${row.ratio?.toFixed(3) ?? 'none'} runs=${row.runs} separate=${row.separate} ` +
      `a_runs=${row.valuesA.map((v) => v.toFixed(1)).join(',') || 'none'} ` +
      `b_runs=${row.valuesB.map((v) => v.toFixed(1)).join(',') || 'none'}`),
    `[rex86-compare] result=${failedRuns.length === 0 ? 'ok' : 'fail'}`,
  ];
  $('report').value = lines.join('\n');
  status.textContent = failedRuns.length === 0
    ? `done: ${runs.length} runs`
    : `done: ${failedRuns.length} of ${runs.length} runs failed (see Runs)`;
  status.className = failedRuns.length === 0 ? 'ok' : 'fail';
  $('start').disabled = false;
  Object.assign(state, { done: true, ok: failedRuns.length === 0, lines });
}

async function main() {
  $('agent').textContent = navigator.userAgent;
  const params = new URL(location.href).searchParams;
  let versions;
  try {
    const response = await fetch('versions.json', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(`versions.json: HTTP ${response.status}`);
    }
    versions = (await response.json()).map((entry) => entry.version);
  } catch (error) {
    Object.assign(state, { done: true, ok: false, error: String(error) });
    $('status').textContent = `could not load the version list: ${error}`;
    $('status').className = 'fail';
    return;
  }
  for (const id of ['a', 'b']) {
    for (const version of versions) {
      $(id).add(new Option(`v${version}`, version));
    }
  }
  const pick = (name, fallback) => (versions.includes(params.get(name)) ? params.get(name) : fallback);
  $('a').value = pick('a', versions[1] ?? versions[0]);
  $('b').value = pick('b', versions[0]);
  const engines = params.get('engines')?.split(',').filter((e) => kEngines.includes(e));
  if (engines?.length) {
    for (const box of document.querySelectorAll('input[name=engine]')) {
      box.checked = engines.includes(box.value);
    }
  }
  const rounds = Number(params.get('rounds'));
  if (rounds >= 1 && rounds <= 7) {
    $('rounds').value = String(rounds);
  }
  const frames = /^[1-9][0-9]*$/.test(params.get('frames') ?? '') ? Number(params.get('frames')) : null;
  const settleMs = /^[0-9]+$/.test(params.get('settle') ?? '') ? Number(params.get('settle')) : 2000;
  const start = () => {
    const chosen = [...document.querySelectorAll('input[name=engine]:checked')].map((box) => box.value);
    if (chosen.length === 0) {
      $('status').textContent = 'pick at least one engine';
      return;
    }
    compare($('a').value, $('b').value, chosen, Number($('rounds').value), frames, settleMs)
      .catch((error) => {
        Object.assign(state, { done: true, ok: false, error: String(error) });
        $('status').textContent = `runner error: ${error}`;
        $('status').className = 'fail';
      });
  };
  $('form').addEventListener('submit', (event) => {
    event.preventDefault();
    start();
  });
  if (params.get('autostart') === '1') {
    start();
  }
}

$('copy').addEventListener('click', async () => {
  const report = $('report');
  try {
    await navigator.clipboard.writeText(report.value);
  } catch {
    report.select();
  }
});

main();
