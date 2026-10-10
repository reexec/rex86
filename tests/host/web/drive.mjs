// The browser runner's Playwright driver (design #48, decision 3): serves a
// REX86_BROWSER build, opens index.html in each browser, waits for the
// suite and prints its report as [rex86-browser] lines. Exits 1 when a test
// fails or a browser cannot run the suite.
//
//   npm ci && npx playwright install chromium firefox webkit
//   node drive.mjs --bin <build>/bin [--browsers chromium,firefox,webkit] [--only unit,irdiff]
//                  [--settle ms]
//
// With GITHUB_STEP_SUMMARY set, a table per browser goes to the job summary.

import fs from 'node:fs';
import path from 'node:path';
import { chromium, firefox, webkit } from 'playwright';

import { reportLines } from './report.mjs';
import { startServer } from './serve.mjs';

const kSuiteTimeoutMs = 60 * 60 * 1000;
const engines = { chromium, firefox, webkit };

function parseArguments(argv) {
  const options = { bin: null, browsers: ['chromium', 'firefox', 'webkit'], only: null, settle: null };
  for (let i = 0; i < argv.length; ++i) {
    const value = argv[i + 1];
    if (argv[i] === '--bin' && value) {
      options.bin = path.resolve(value);
    } else if (argv[i] === '--browsers' && value) {
      options.browsers = value.split(',').filter(Boolean);
    } else if (argv[i] === '--only' && value) {
      options.only = value;
    } else if (argv[i] === '--settle' && /^[0-9]+$/.test(value ?? '')) {
      options.settle = value;
    } else {
      return null;
    }
    ++i;
  }
  const known = options.browsers.every((name) => name in engines);
  return options.bin && known ? options : null;
}

function summaryTable(name, version, results) {
  const rows = results.tests.map((test) =>
    `| ${test.name} | ${test.thread} | ${test.ok ? 'ok' : `**fail (${test.code})**`} | ` +
    `${(test.ms / 1000).toFixed(1)} s | \`${test.summary.replaceAll('|', '\\|')}\` |`);
  const bench = results.tests
    .filter((test) => test.bench)
    .map((test) => `${test.name}: ` +
      Object.entries(test.bench.mips).map(([workload, mips]) => `${workload} ${mips}`).join(', '));
  return [
    `### ${name} ${version}`,
    '',
    '| test | thread | result | time | last line |',
    '|---|---|---|---|---|',
    ...rows,
    '',
    ...(bench.length ? ['MIPS (CI runners vary; not a recorded figure):', '', ...bench.map((line) => `* ${line}`), ''] : []),
  ].join('\n');
}

async function runBrowser(name, origin, { only, settle }) {
  const browser = await engines[name].launch();
  try {
    const page = await browser.newPage();
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(String(error)));
    const params = new URLSearchParams();
    if (only) {
      params.set('only', only);
    }
    if (settle !== null) {
      params.set('settle', settle);
    }
    const query = params.size ? `?${params}` : '';
    await page.goto(`${origin}/index.html${query}`);
    await page.waitForFunction(() => window.rex86Results?.done === true, null,
      { timeout: kSuiteTimeoutMs, polling: 1000 });
    const results = await page.evaluate(() => window.rex86Results);
    return { version: browser.version(), results, pageErrors };
  } finally {
    await browser.close();
  }
}

const options = parseArguments(process.argv.slice(2));
if (!options) {
  console.error('usage: node drive.mjs --bin <REX86_BROWSER build>/bin ' +
    '[--browsers chromium,firefox,webkit] [--only test,...] [--settle ms]');
  process.exit(2);
}
if (!fs.existsSync(path.join(options.bin, 'rex86_browser.json'))) {
  console.error(`${options.bin} holds no REX86_BROWSER build (rex86_browser.json is missing)`);
  process.exit(2);
}

const server = await startServer(options.bin);
const origin = `http://127.0.0.1:${server.address().port}`;
let failed = false;
const summaries = [];
for (const name of options.browsers) {
  const prefix = `browser=${name} `;
  try {
    const { version, results, pageErrors } = await runBrowser(name, origin, options);
    console.log(`[rex86-browser] ${prefix}version=${version}`);
    for (const line of reportLines(results, prefix)) {
      console.log(line);
    }
    for (const test of results.tests.filter((t) => !t.ok)) {
      console.log(`[rex86-browser] ${prefix}output test=${test.name}:`);
      for (const line of test.lines.slice(-40)) {
        console.log(`  ${line}`);
      }
    }
    if (results.error || pageErrors.length) {
      console.log(`[rex86-browser] ${prefix}runner_error="${[results.error, ...pageErrors].filter(Boolean).join('; ')}"`);
    }
    if (results.error || pageErrors.length || results.tests.length === 0 || results.tests.some((t) => !t.ok)) {
      failed = true;
    }
    summaries.push(summaryTable(name, version, results));
  } catch (error) {
    console.log(`[rex86-browser] ${prefix}result=fail runner_error="${String(error).split('\n')[0]}"`);
    summaries.push(`### ${name}\n\n**The browser could not run the suite:** ${String(error).split('\n')[0]}\n`);
    failed = true;
  }
}
server.close();
if (process.env.GITHUB_STEP_SUMMARY) {
  fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY, `## Browser tests\n\n${summaries.join('\n')}\n`);
}
console.log(`[rex86-browser] browsers=${options.browsers.join(',')} result=${failed ? 'fail' : 'ok'}`);
process.exit(failed ? 1 : 0);
