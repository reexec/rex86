// The two-version benchmark comparison (design #52, decision 4): runs the
// rex86_bench of versions A and B on this device in Workers started from
// each version's own directory, alternating which goes first each round so a
// phone heating up does not land on one version, and shows the median MIPS
// per engine and workload with B/A, marked only past ±5% when the two
// sides' runs do not overlap. With the A/A control (#59, on by default) a
// third side A2 runs version A again in the same session; a row is marked
// only when A2/A would not be and B/A moves further from 1 than A2/A, and a
// row whose A2/A would be marked shows as unsteady. Results stay on the
// page; the report box holds [rex86-compare] lines for a person to carry,
// and window.rex86Compare holds them for drive_pages.mjs.
//
// The root comes from the newest tag and each version directory from its
// own, so this file relies only on the contract the design states:
// vX/worker.js?tool=&args= starts a tool, its messages are
// { kind: 'out' | 'err' | 'exit', text, code }, and the benchmark prints
// "[rex86-bench] version=", "workload=<name> ... mips=<value> ... verified=ok",
// "workload=<name> verified=fail" and "engine_available=false".
//
// compare.html?a=0.0.23&b=0.0.24 preselects the versions; engines=, rounds=,
// control=0|1, frames= (passed to rex86_bench --frames), settle=ms and
// autostart=1 serve the CI smoke.

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

// Two sides' runs: the median ratio, whether no run of one overlaps a run of
// the other, and the tone past the bounds when they do not.
function contrast(base, other) {
  const ratio = base.length && other.length ? median(other) / median(base) : null;
  const separate = ratio !== null &&
    (Math.min(...other) > Math.max(...base) || Math.max(...other) < Math.min(...base));
  const tone = !separate ? '' : ratio >= kBetter ? 'better' : ratio <= kWorse ? 'worse' : '';
  return { ratio, separate, tone };
}

function cell(row, text, className) {
  const td = row.insertCell();
  td.textContent = text;
  if (className) {
    td.className = className;
  }
}

// One table per engine: workloads as rows, A's and B's medians and B/A,
// and A2/A with the control.
function renderTables(a, b, engines, control, runs) {
  const container = $('tables');
  container.replaceChildren();
  const summary = [];
  for (const engine of engines) {
    // Runs are told apart by side, not version: A against itself measures
    // this device's run-to-run variation.
    const of = (side) => runs.filter((run) => run.engine === engine && run.side === side);
    const [runsA, runsB, runsA2] = [of('A'), of('B'), of('A2')];
    const heading = document.createElement('h2');
    heading.textContent = engine;
    container.append(heading);
    const unavailable = [[runsA, a], [runsB, b], [runsA2, a]].filter(([list]) =>
      list.some((run) => run.outcome === 'unavailable')).map(([, version]) => `v${version}`);
    if (unavailable.length) {
      const note = document.createElement('p');
      note.className = 'muted';
      note.textContent = `not available in ${[...new Set(unavailable)].join(' and ')}`;
      container.append(note);
    }
    const workloads = [...new Set([...runsA, ...runsB, ...runsA2].flatMap((run) =>
      run.outcome === 'ok' ? Object.keys(run.mips) : []))];
    if (workloads.length === 0) {
      continue;
    }
    const wrap = document.createElement('div');
    wrap.className = 'table-wrap';
    const table = document.createElement('table');
    table.createTHead().innerHTML =
      `<tr><th>workload</th><th class="number">A v${a}</th><th class="number">B v${b}</th><th class="number">B/A</th>` +
      (control ? '<th class="number">A2/A</th>' : '') + '</tr>';
    const body = table.createTBody();
    for (const workload of workloads) {
      const values = (list) => list.filter((run) => run.outcome === 'ok' && workload in run.mips)
        .map((run) => run.mips[workload]);
      const [valuesA, valuesB, valuesA2] = [values(runsA), values(runsB), values(runsA2)];
      const medianA = valuesA.length ? median(valuesA) : null;
      const medianB = valuesB.length ? median(valuesB) : null;
      const medianA2 = valuesA2.length ? median(valuesA2) : null;
      // A difference is marked only past the bound and when the two sides'
      // runs do not overlap: the same build against itself already moves
      // the median by more than 5% on some workloads. With the control, A
      // against itself in this session must not meet that test, and B/A
      // must move further from 1 than A2/A did.
      const { ratio, separate, tone } = contrast(valuesA, valuesB);
      const check = control ? contrast(valuesA, valuesA2) : null;
      const distance = (r) => Math.abs(Math.log(r));
      let mark = tone || 'none';
      if (check && check.tone) {
        mark = 'unsteady';
      } else if (check && tone && (check.ratio === null || distance(ratio) <= distance(check.ratio))) {
        mark = 'none';
      }
      const row = body.insertRow();
      cell(row, workload);
      cell(row, medianA === null ? '—' : medianA.toFixed(1), 'number');
      cell(row, medianB === null ? '—' : medianB.toFixed(1), 'number');
      const shown = mark === 'better' || mark === 'worse' || mark === 'unsteady' ? ` ${mark}` : '';
      cell(row, ratio === null ? '—' : `${ratio.toFixed(2)}×`, `number${shown}`);
      if (control) {
        cell(row, check.ratio === null ? '—' : `${check.ratio.toFixed(2)}×`,
          `number${check.tone ? ' unsteady' : ''}`);
      }
      row.title = `A: ${valuesA.map((v) => v.toFixed(1)).join(', ') || '—'}\nB: ${valuesB.map((v) => v.toFixed(1)).join(', ') || '—'}` +
        (control ? `\nA2: ${valuesA2.map((v) => v.toFixed(1)).join(', ') || '—'}` : '') +
        (mark === 'unsteady' ? '\nA against itself moved past ±5% with separate runs: unsteady on this device' : '');
      summary.push({ engine, workload, medianA, medianB, medianA2, ratio, valuesA, valuesB, valuesA2, separate,
        control: check, mark, runs: Math.min(valuesA.length, valuesB.length) });
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

async function compare(a, b, engines, rounds, control, frames, settleMs) {
  const status = $('status');
  $('start').disabled = true;
  $('tables').replaceChildren();
  $('runs').replaceChildren();
  $('report').value = '';
  state.done = false;
  const plan = [];
  for (let round = 1; round <= rounds; ++round) {
    // Two sides alternate (ABBA); three rotate, so each side takes each
    // place in turn.
    const sides = [{ side: 'A', version: a }, { side: 'B', version: b },
      ...(control ? [{ side: 'A2', version: a }] : [])];
    const shift = (round - 1) % sides.length;
    const order = control ? [...sides.slice(shift), ...sides.slice(0, shift)]
      : round % 2 ? sides : [...sides].reverse();
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
    renderTables(a, b, engines, control, runs);
  }
  const summary = renderTables(a, b, engines, control, runs);
  const failedRuns = runs.filter((run) => run.outcome === 'fail');
  const list = (values) => values.map((v) => v.toFixed(1)).join(',') || 'none';
  const lines = [
    `[rex86-compare] user_agent=${quote(navigator.userAgent)}`,
    `[rex86-compare] a=${a} b=${b} engines=${engines.join(',')} rounds=${rounds} ` +
      `control=${control ? 'on' : 'off'} frames=${frames ?? 'default'}`,
    ...runs.map((run) => `[rex86-compare] run round=${run.round} side=${run.side} version=${run.version} engine=${run.engine} ` +
      `result=${run.outcome}${run.reason ? ` reason=${quote(run.reason)}` : ''}`),
    ...summary.map((row) => `[rex86-compare] engine=${row.engine} workload=${row.workload} ` +
      `a_mips=${row.medianA?.toFixed(3) ?? 'none'} b_mips=${row.medianB?.toFixed(3) ?? 'none'} ` +
      `ratio=${row.ratio?.toFixed(3) ?? 'none'} runs=${row.runs} separate=${row.separate} ` +
      (row.control ? `a2_mips=${row.medianA2?.toFixed(3) ?? 'none'} ` +
        `control_ratio=${row.control.ratio?.toFixed(3) ?? 'none'} control_separate=${row.control.separate} ` : '') +
      `mark=${row.mark} a_runs=${list(row.valuesA)} b_runs=${list(row.valuesB)}` +
      (row.control ? ` a2_runs=${list(row.valuesA2)}` : '')),
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
  if (params.get('control') === '0' || params.get('control') === '1') {
    $('control').checked = params.get('control') === '1';
  }
  const frames = /^[1-9][0-9]*$/.test(params.get('frames') ?? '') ? Number(params.get('frames')) : null;
  const settleMs = /^[0-9]+$/.test(params.get('settle') ?? '') ? Number(params.get('settle')) : 2000;
  const start = () => {
    const chosen = [...document.querySelectorAll('input[name=engine]:checked')].map((box) => box.value);
    if (chosen.length === 0) {
      $('status').textContent = 'pick at least one engine';
      return;
    }
    compare($('a').value, $('b').value, chosen, Number($('rounds').value), $('control').checked, frames, settleMs)
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
