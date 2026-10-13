// The Pages site's Playwright check (design #52, decision 5): serves a site
// assembled by scripts/pages_site.mjs and, in each browser, checks that the
// version list shows every version, that a version page's benchmark smoke
// runs from its directory, that its web demo (#62), when it has one, runs
// there and is linked from the root, and that a comparison of two versions
// ends with the expected result. Prints [rex86-pages] lines; exits 1 when a
// check does not hold.
//
//   node drive_pages.mjs --site <dir> --compare A,B [--expect ok|fail]
//                        [--browsers chromium,firefox] [--rounds N] [--frames N|default]
//                        [--control on|off]
//
// --expect fail serves the version check: a directory named for a version
// its build does not report must fail the comparison. --control on (the
// default) also checks that every workload row carries the A/A control's
// ratio (#59).

import fs from 'node:fs';
import path from 'node:path';
import { chromium, firefox, webkit } from 'playwright';

import { startSiteServer } from './serve.mjs';

const kTimeoutMs = 30 * 60 * 1000;
const engines = { chromium, firefox, webkit };

function parseArguments(argv) {
  const options = { site: null, compare: null, expect: 'ok', browsers: ['chromium'], rounds: '1', frames: '2', control: 'on' };
  for (let i = 0; i < argv.length; i += 2) {
    const value = argv[i + 1];
    if (argv[i] === '--site' && value) {
      options.site = path.resolve(value);
    } else if (argv[i] === '--compare' && /^[0-9.]+,[0-9.]+$/.test(value ?? '')) {
      options.compare = value.split(',');
    } else if (argv[i] === '--expect' && (value === 'ok' || value === 'fail')) {
      options.expect = value;
    } else if (argv[i] === '--browsers' && value) {
      options.browsers = value.split(',').filter(Boolean);
    } else if (argv[i] === '--rounds' && /^[1-7]$/.test(value ?? '')) {
      options.rounds = value;
    } else if (argv[i] === '--control' && (value === 'on' || value === 'off')) {
      options.control = value;
    } else if (argv[i] === '--frames' && /^([1-9][0-9]*|default)$/.test(value ?? '')) {
      options.frames = value;
    } else {
      return null;
    }
  }
  const known = options.browsers.every((name) => name in engines);
  return options.site && options.compare && known ? options : null;
}

async function waitFor(page, global) {
  await page.waitForFunction((name) => window[name]?.done === true, global,
    { timeout: kTimeoutMs, polling: 500 });
  return page.evaluate((name) => window[name], global);
}

async function checkBrowser(name, origin, versions, { site: siteDir, compare, expect, rounds, frames, control }) {
  const browser = await engines[name].launch();
  const prefix = `[rex86-pages] browser=${name}`;
  const failures = [];
  try {
    const page = await browser.newPage();
    const pageErrors = [];
    page.on('pageerror', (error) => pageErrors.push(String(error)));

    await page.goto(`${origin}/`);
    const list = await waitFor(page, 'rex86Versions');
    const listOk = !list.error && list.count === versions.length;
    console.log(`${prefix} check=list count=${list.count} expected=${versions.length} ` +
      `result=${listOk ? 'ok' : 'fail'}${list.error ? ` error="${list.error}"` : ''}`);
    if (!listOk) {
      failures.push('list');
    }

    const [a, b] = compare;
    await page.goto(`${origin}/v${b}/index.html?only=bench-smoke`);
    const suite = await waitFor(page, 'rex86Results');
    const smoke = suite.tests[0];
    const smokeOk = !suite.error && suite.tests.length === 1 && smoke.ok;
    console.log(`${prefix} check=version-page version=${b} result=${smokeOk ? 'ok' : 'fail'} ` +
      `summary="${smoke?.summary ?? suite.error ?? ''}"`);
    if (!smokeOk) {
      failures.push('version-page');
    }

    // The web demo (#62), when the version has one: it runs from its
    // directory, and the root links it.
    if (fs.existsSync(path.join(siteDir, `v${b}`, 'demo.html'))) {
      await page.goto(`${origin}/`);
      await waitFor(page, 'rex86Versions');
      const linked = await page.evaluate(() => {
        const demo = document.getElementById('demo');
        return demo !== null && !demo.hidden ? demo.getAttribute('href') : null;
      });
      await page.goto(`${origin}/v${b}/demo.html?workload=alu&engine=wasm&budget=100000&frames=3&autostart=1`);
      const demo = await waitFor(page, 'rex86Demo');
      const demoOk = demo.ok === true && !demo.error && demo.frames === 3 && linked !== null;
      console.log(`${prefix} check=demo version=${b} linked=${linked} frames=${demo.frames} ` +
        `result=${demoOk ? 'ok' : 'fail'}${demo.error ? ` error="${demo.error}"` : ''}`);
      if (!demoOk) {
        failures.push('demo');
      }
    }

    await page.goto(`${origin}/compare.html?a=${a}&b=${b}&rounds=${rounds}` +
      (frames === 'default' ? '' : `&frames=${frames}`) +
      `&control=${control === 'on' ? 1 : 0}&engines=interpreter,wasm&settle=0&autostart=1`);
    const result = await waitFor(page, 'rex86Compare');
    for (const line of result.lines) {
      console.log(`${prefix} ${line}`);
    }
    const got = result.error ? 'error' : result.ok ? 'ok' : 'fail';
    console.log(`${prefix} check=compare a=${a} b=${b} expected=${expect} got=${got}` +
      `${result.error ? ` error="${result.error}"` : ''} result=${got === expect ? 'ok' : 'fail'}`);
    if (got !== expect) {
      failures.push('compare');
    }
    // The control's figures: every workload row of a passing comparison
    // carries A2/A when the control is on, and none when it is off.
    if (got === 'ok') {
      const rows = result.lines.filter((line) => / workload=\S+ a_mips=/.test(line));
      const carried = rows.filter((line) => / control_ratio=[0-9.]+ /.test(line)).length;
      const controlOk = rows.length > 0 && carried === (control === 'on' ? rows.length : 0);
      console.log(`${prefix} check=control control=${control} rows=${rows.length} with_ratio=${carried} ` +
        `result=${controlOk ? 'ok' : 'fail'}`);
      if (!controlOk) {
        failures.push('control');
      }
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
  console.error('usage: node drive_pages.mjs --site <dir> --compare A,B [--expect ok|fail] ' +
    '[--browsers chromium,firefox,webkit] [--rounds N] [--frames N|default] [--control on|off]');
  process.exit(2);
}
const listFile = path.join(options.site, 'versions.json');
if (!fs.existsSync(listFile)) {
  console.error(`${options.site} is not an assembled site (versions.json is missing)`);
  process.exit(2);
}
const versions = JSON.parse(fs.readFileSync(listFile, 'utf8')).map((entry) => entry.version);
if (!options.compare.every((version) => versions.includes(version))) {
  console.error(`--compare names a version the site lacks (it has ${versions.join(',')})`);
  process.exit(2);
}

const server = await startSiteServer(options.site);
const origin = `http://127.0.0.1:${server.address().port}`;
let failed = false;
for (const name of options.browsers) {
  try {
    const failures = await checkBrowser(name, origin, versions, options);
    console.log(`[rex86-pages] browser=${name} result=${failures.length ? `fail failed=${failures.join(',')}` : 'ok'}`);
    failed ||= failures.length > 0;
  } catch (error) {
    console.log(`[rex86-pages] browser=${name} result=fail runner_error="${String(error).split('\n')[0]}"`);
    failed = true;
  }
}
server.close();
console.log(`[rex86-pages] browsers=${options.browsers.join(',')} result=${failed ? 'fail' : 'ok'}`);
process.exit(failed ? 1 : 0);
