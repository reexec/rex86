// The browser runner's report (design #48, decision 3), shared by the page
// (suite.js) and the Playwright driver (drive.mjs): one [rex86-browser] line
// per test and per benchmark workload, then a total.

const quote = (text) => `"${String(text).replaceAll('"', "'")}"`;

// The benchmark's figures in a tool's output: { workload: mips } and the
// wasm installation line's per-module microseconds.
export function parseBench(lines) {
  const mips = {};
  let installUs = null;
  for (const line of lines) {
    const workload = /^\[rex86-bench\] workload=(\S+) frames=.* mips=([0-9.]+)/.exec(line);
    if (workload) {
      mips[workload[1]] = Number(workload[2]);
    }
    const install = /^\[rex86-bench\] wasm .*install_us_per_module=([0-9.]+)/.exec(line);
    if (install) {
      installUs = Number(install[1]);
    }
  }
  return { mips, installUs };
}

// The report's lines; prefix names the browser (drive.mjs) or is empty.
export function reportLines(results, prefix = '') {
  const head = `[rex86-browser] ${prefix}`;
  const lines = [`${head}user_agent=${quote(results.userAgent)}`];
  for (const test of results.tests) {
    lines.push(`${head}test=${test.name} thread=${test.thread} result=${test.ok ? 'ok' : 'fail'} ` +
      `exit=${test.code} ms=${Math.round(test.ms)} retries=${test.retries ?? 0} summary=${quote(test.summary)}`);
    if (test.bench) {
      for (const [workload, mips] of Object.entries(test.bench.mips)) {
        lines.push(`${head}bench test=${test.name} workload=${workload} mips=${mips}`);
      }
      if (test.bench.installUs !== null) {
        lines.push(`${head}bench test=${test.name} install_us_per_module=${test.bench.installUs}`);
      }
    }
  }
  const failed = results.tests.filter((test) => !test.ok).length;
  lines.push(`${head}tests=${results.tests.length} failed=${failed} result=${failed === 0 ? 'ok' : 'fail'}`);
  return lines;
}
