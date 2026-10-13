// The web demo's Playwright check (design #62, decision 4): serves a
// REX86_BROWSER build with demo.html and, in each browser, runs a live
// workload on every engine for a few frames and then measures every workload
// once, as a visitor would. Prints [rex86-demo-browser] lines; exits 1 when a
// check does not hold.
//
//   node drive_demo.mjs --bin <build>/bin [--browsers chromium,firefox,webkit]

import { chromium, firefox, webkit } from 'playwright';

import { startServer } from './serve.mjs';

const kTimeoutMs = 10 * 60 * 1000;
const engines = { chromium, firefox, webkit };

function parseArguments(argv) {
  const options = { bin: null, browsers: ['chromium', 'firefox', 'webkit'] };
  for (let i = 0; i < argv.length; i += 2) {
    const value = argv[i + 1];
    if (argv[i] === '--bin' && value) {
      options.bin = value;
    } else if (argv[i] === '--browsers' && value) {
      options.browsers = value.split(',').filter(Boolean);
    } else {
      return null;
    }
  }
  return options.bin && options.browsers.every((name) => name in engines) ? options : null;
}

async function runPage(page, url) {
  await page.goto(url);
  await page.waitForFunction(() => window.rex86Demo?.done === true, null, { timeout: kTimeoutMs, polling: 250 });
  return page.evaluate(() => {
    const { ok, error, failure, frames, mips, lines, measured, info } = window.rex86Demo;
    return { ok, error, failure, frames, mips, lines, measured, workloads: info?.workloads ?? [] };
  });
}

async function checkBrowser(name, origin) {
  const browser = await engines[name].launch();
  const prefix = `[rex86-demo-browser] browser=${name}`;
  const failures = [];
  try {
    const page = await browser.newPage();
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(String(error)));
    const kFrames = 12;
    for (const engine of ['interpreter', 'wasm', 'evaluator']) {
      const result = await runPage(page,
        `${origin}/demo.html?workload=mixed&engine=${engine}&budget=250000&frames=${kFrames}&autostart=1`);
      const ok = result.ok === true && !result.error && result.frames === kFrames && result.mips > 0;
      console.log(`${prefix} check=live engine=${engine} frames=${result.frames} mips=${result.mips?.toFixed(2)} ` +
        `result=${ok ? 'ok' : 'fail'}${ok ? '' : ` error="${result.error ?? result.failure}"`}`);
      if (!ok) {
        failures.push(`live-${engine}`);
      }
    }
    const measured = await runPage(page, `${origin}/demo.html?engine=wasm&budget=250000&measure=1&mframes=2`);
    for (const line of measured.lines) {
      console.log(`${prefix} ${line}`);
    }
    const measureOk = measured.ok === true && !measured.error && measured.measured.length === measured.workloads.length &&
      measured.measured.every((row) => row.ok && row.mips > 0);
    console.log(`${prefix} check=measure workloads=${measured.measured.length} result=${measureOk ? 'ok' : 'fail'}`);
    if (!measureOk) {
      failures.push('measure');
    }
    if (pageErrors.length) {
      console.log(`${prefix} page_errors="${pageErrors.join('; ')}"`);
      failures.push('page-errors');
    }
  } finally {
    await browser.close();
  }
  return failures;
}

const options = parseArguments(process.argv.slice(2));
if (!options) {
  console.error('usage: node drive_demo.mjs --bin <build>/bin [--browsers chromium,firefox,webkit]');
  process.exit(2);
}
const server = await startServer(options.bin);
const origin = `http://127.0.0.1:${server.address().port}`;
let failed = false;
for (const name of options.browsers) {
  try {
    const failures = await checkBrowser(name, origin);
    console.log(`[rex86-demo-browser] browser=${name} result=${failures.length ? `fail failed=${failures.join(',')}` : 'ok'}`);
    failed ||= failures.length > 0;
  } catch (error) {
    console.log(`[rex86-demo-browser] browser=${name} result=fail runner_error="${String(error).split('\n')[0]}"`);
    failed = true;
  }
}
server.close();
console.log(`[rex86-demo-browser] browsers=${options.browsers.join(',')} result=${failed ? 'fail' : 'ok'}`);
process.exit(failed ? 1 : 0);
