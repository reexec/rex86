// The browser runner's suite (design #48, decision 3): runs a REX86_BROWSER
// build's tools one after another, in Workers except the asynchronous
// installer's test, which runs on the main thread in a frame. The table
// shows progress; window.rex86Results holds the results for drive.mjs, and
// the report box the same lines for a person on a real device.
//
// Firefox gives back wasm code memory only at a garbage collection, which
// code memory pressure itself does not start, and a process holds about
// 16,000 live modules; the tools that install a module per case approach
// that, and a finished Worker's modules linger. The suite therefore waits
// between tests, and a run that failed only for want of memory (an
// out-of-memory line, and no mismatch other than a refused installation) is
// started again after a longer wait; the report counts such retries.
//
// index.html?only=unit,irdiff runs the named tests only; settle=ms changes
// the wait between tests.

import { parseBench, reportLines } from './report.mjs';

const kTimeoutMs = 15 * 60 * 1000;
const kRetries = 2;
const kRetryWaitMs = 15 * 1000;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// A run that failed only for want of memory.
const outOfMemoryOnly = (code, lines) =>
  code !== 0 && lines.some((line) => /out of memory/i.test(line)) &&
  lines.filter((line) => line.startsWith('first mismatch:'))
    .every((line) => line.endsWith('the host refused the module'));

function suite(traces) {
  const bench = (engine) => ({ name: `bench-${engine}`, tool: 'rex86_bench', args: ['--engine', engine] });
  return [
    { name: 'unit', tool: 'rex86_unit_tests', args: [] },
    // Node's ctest runs 20,000 cases; fewer here keep the module count under
    // Firefox's limit (see above).
    { name: 'irdiff', tool: 'rex86_irdiff', args: ['--cases', '5000'] },
    { name: 'robust', tool: 'rex86_robust', args: ['--cases', '300'] },
    { name: 'trace', tool: 'rex86_trace', args: traces },
    { name: 'probe', tool: 'rex86_probe', args: [] },
    { name: 'bench-smoke', tool: 'rex86_bench', args: ['--smoke'] },
    bench('interpreter'),
    bench('evaluator'),
    bench('wasm'),
    { name: 'async', tool: 'rex86_async_test', args: [], thread: 'main' },
  ].map((test) => ({ thread: 'worker', ...test }));
}

// Runs one tool; resolves with its exit code and output lines.
function run(test, onLine) {
  return new Promise((resolve) => {
    const query = `tool=${test.tool}&args=${encodeURIComponent(JSON.stringify(test.args))}`;
    let finish;
    const timer = setTimeout(() => finish(124, `timed out after ${kTimeoutMs / 1000} s`), kTimeoutMs);
    const receive = (message) => {
      if (message.kind === 'exit') {
        finish(message.code, message.text);
      } else {
        onLine(message.text);
      }
    };
    if (test.thread === 'main') {
      const frame = document.createElement('iframe');
      frame.className = 'tool-frame';
      frame.title = test.name;
      const listener = (event) => {
        if (event.source === frame.contentWindow && event.data?.rex86) {
          receive(event.data);
        }
      };
      finish = (code, text) => {
        clearTimeout(timer);
        window.removeEventListener('message', listener);
        frame.remove();
        if (text) {
          onLine(text);
        }
        resolve(code);
      };
      window.addEventListener('message', listener);
      frame.src = `main.html?${query}`;
      document.getElementById('frames').appendChild(frame);
    } else {
      const worker = new Worker(`worker.js?${query}`);
      finish = (code, text) => {
        clearTimeout(timer);
        worker.terminate();
        if (text) {
          onLine(text);
        }
        resolve(code);
      };
      worker.onmessage = (event) => receive(event.data);
      worker.onerror = (event) => {
        event.preventDefault();
        finish(-1, `worker error: ${event.message}`);
      };
    }
  });
}

function cell(row, text, className) {
  const td = row.insertCell();
  td.textContent = text;
  if (className) {
    td.className = className;
  }
  return td;
}

async function main() {
  const results = { done: false, userAgent: navigator.userAgent, tests: [] };
  window.rex86Results = results;
  document.getElementById('agent').textContent = navigator.userAgent;
  const status = document.getElementById('status');
  try {
    const response = await fetch('bin/rex86_browser.json', { cache: 'no-store' });
    if (!response.ok) {
      throw new Error(`bin/rex86_browser.json: HTTP ${response.status}`);
    }
    const manifest = await response.json();
    const params = new URL(location.href).searchParams;
    const only = params.get('only')?.split(',').filter(Boolean);
    const settleMs = Number(params.get('settle') ?? 5000);
    const tests = suite(manifest.traces).filter((test) => !only || only.includes(test.name));
    const body = document.querySelector('#tests tbody');
    for (const test of tests) {
      const row = body.insertRow();
      cell(row, test.name);
      cell(row, test.thread);
      const state = cell(row, 'waiting', 'state');
      const time = cell(row, '', 'number');
      const summary = cell(row, '', 'summary');
      test.row = { state, time, summary };
    }
    for (const [index, test] of tests.entries()) {
      if (index > 0) {
        status.textContent = `settling before ${test.name}`;
        await sleep(settleMs);
      }
      status.textContent = `running ${test.name}`;
      test.row.state.textContent = 'running';
      test.row.state.className = 'state running';
      let lines;
      let code;
      let ms;
      let retries = 0;
      while (true) {
        lines = [];
        const start = performance.now();
        code = await run(test, (line) => {
          lines.push(line);
          if (line.startsWith('[rex86')) {
            test.row.summary.textContent = line;
          }
        });
        ms = performance.now() - start;
        if (retries === kRetries || !outOfMemoryOnly(code, lines)) {
          break;
        }
        ++retries;
        test.row.state.textContent = `retry ${retries}`;
        await sleep(kRetryWaitMs);
      }
      const ok = code === 0;
      const summary = [...lines].reverse().find((line) => line.startsWith('[rex86')) ?? lines.at(-1) ?? '';
      const result = { name: test.name, tool: test.tool, thread: test.thread, code, ok, ms, retries, summary, lines };
      if (test.tool === 'rex86_bench' && test.args[0] === '--engine') {
        result.bench = parseBench(lines);
      }
      results.tests.push(result);
      test.row.state.textContent = ok ? 'ok' : `fail (${code})`;
      test.row.state.className = `state ${ok ? 'ok' : 'fail'}`;
      test.row.time.textContent = `${(ms / 1000).toFixed(1)} s`;
      test.row.summary.textContent = summary;
      const details = document.createElement('details');
      const label = document.createElement('summary');
      label.textContent = `${test.name}: ${lines.length} lines`;
      const pre = document.createElement('pre');
      pre.textContent = lines.join('\n');
      details.append(label, pre);
      document.getElementById('outputs').appendChild(details);
    }
  } catch (error) {
    results.error = String(error);
    status.textContent = `runner error: ${error}`;
  }
  const report = reportLines(results);
  if (results.error) {
    report.push(`[rex86-browser] runner_error="${results.error}"`);
  }
  document.getElementById('report').value = report.join('\n');
  const failed = results.tests.filter((test) => !test.ok).length;
  if (!results.error) {
    status.textContent = failed === 0 ? `all ${results.tests.length} tests passed` : `${failed} failed`;
  }
  status.className = failed === 0 && !results.error ? 'ok' : 'fail';
  results.done = true;
}

document.getElementById('copy').addEventListener('click', async () => {
  const report = document.getElementById('report');
  try {
    await navigator.clipboard.writeText(report.value);
  } catch {
    report.select();
  }
});

main();
