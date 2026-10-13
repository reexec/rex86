// Assembles the GitHub Pages site of versioned browser tests (design #52,
// decisions 1 and 2): one version directory per release built from its own
// tag, a versions.json listing them highest first, and the root (the version
// list and the comparison) taken only from a tag at least as high as every
// deployed version. Used by .github/workflows/pages.yml and by local and CI
// verification.
//
//   node scripts/pages_site.mjs --site <dir> --version X.Y.Z --bin <build>/bin
//        --runner <tests/host/web of the same commit> [--commit <sha>] [--date <ISO 8601>]
//        [--root <tests/host/web/pages> --root-version X.Y.Z] [--budget-mb 500]
//
// The site directory may hold an earlier deployment (the gh-pages tree); the
// version's directory is replaced whole, the rest kept. Past the size budget
// the oldest versions are removed, never the one being deployed.

import fs from 'node:fs';
import path from 'node:path';

// module_limit.{html,js}: the module limit probe (design #49), for real devices;
// demo.{html,js} and demo_worker.js: the web demo (design #62).
const kRunnerFiles = ['index.html', 'suite.js', 'worker.js', 'main.html', 'report.mjs', 'module_limit.html',
  'module_limit.js', 'demo.html', 'demo.js', 'demo_worker.js'];
const kRequiredRunnerFiles = ['index.html', 'suite.js', 'worker.js'];
const kRootFiles = ['index.html', 'compare.html', 'compare.js'];
const kBinExtensions = new Set(['.js', '.wasm', '.data', '.json']);
const kVersion = /^[0-9]+\.[0-9]+\.[0-9]+$/;

function fail(message) {
  console.error(`pages_site: ${message}`);
  process.exit(2);
}

function parseArguments(argv) {
  const options = { budgetMb: 500, commit: '', date: new Date().toISOString() };
  const names = {
    '--site': 'site', '--version': 'version', '--bin': 'bin', '--runner': 'runner',
    '--commit': 'commit', '--date': 'date', '--root': 'root', '--root-version': 'rootVersion',
    '--budget-mb': 'budgetMb',
  };
  for (let i = 0; i < argv.length; i += 2) {
    const name = names[argv[i]];
    if (!name || argv[i + 1] === undefined) {
      fail(`unknown or incomplete argument ${argv[i]}`);
    }
    options[name] = argv[i + 1];
  }
  for (const name of ['site', 'version', 'bin', 'runner']) {
    if (!options[name]) {
      fail(`--${name} is required`);
    }
  }
  if (!kVersion.test(options.version)) {
    fail(`--version ${options.version} is not X.Y.Z`);
  }
  if (Boolean(options.root) !== Boolean(options.rootVersion)) {
    fail('--root and --root-version go together');
  }
  if (options.rootVersion && !kVersion.test(options.rootVersion)) {
    fail(`--root-version ${options.rootVersion} is not X.Y.Z`);
  }
  options.budgetMb = Number(options.budgetMb);
  if (!(options.budgetMb > 0)) {
    fail('--budget-mb must be positive');
  }
  return options;
}

// Orders X.Y.Z strings; negative when a < b.
function compareVersions(a, b) {
  const [x, y] = [a, b].map((v) => v.split('.').map(Number));
  return x[0] - y[0] || x[1] - y[1] || x[2] - y[2];
}

function directorySize(dir) {
  let total = 0;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const file = path.join(dir, entry.name);
    total += entry.isDirectory() ? directorySize(file) : fs.statSync(file).size;
  }
  return total;
}

const megabytes = (bytes) => (bytes / (1024 * 1024)).toFixed(1);

const options = parseArguments(process.argv.slice(2));
for (const file of kRequiredRunnerFiles) {
  if (!fs.existsSync(path.join(options.runner, file))) {
    fail(`${options.runner} has no ${file}; versions before 0.0.23 have no browser runner`);
  }
}
if (!fs.existsSync(path.join(options.bin, 'rex86_browser.json'))) {
  fail(`${options.bin} holds no REX86_BROWSER build (rex86_browser.json is missing)`);
}

fs.mkdirSync(options.site, { recursive: true });
const versionDir = path.join(options.site, `v${options.version}`);
fs.rmSync(versionDir, { recursive: true, force: true });
fs.mkdirSync(path.join(versionDir, 'bin'), { recursive: true });
for (const file of kRunnerFiles) {
  const from = path.join(options.runner, file);
  if (fs.existsSync(from)) {
    fs.copyFileSync(from, path.join(versionDir, file));
  }
}
for (const entry of fs.readdirSync(options.bin, { withFileTypes: true })) {
  if (entry.isFile() && kBinExtensions.has(path.extname(entry.name))) {
    fs.copyFileSync(path.join(options.bin, entry.name), path.join(versionDir, 'bin', entry.name));
  }
}
const versionBytes = directorySize(versionDir);

const listFile = path.join(options.site, 'versions.json');
let versions = fs.existsSync(listFile) ? JSON.parse(fs.readFileSync(listFile, 'utf8')) : [];
versions = versions.filter((entry) => entry.version !== options.version &&
  fs.existsSync(path.join(options.site, `v${entry.version}`)));
// demo marks a version directory holding the web demo (#62), which the root
// links; earlier versions have none.
versions.push({
  version: options.version, commit: options.commit, date: options.date,
  demo: fs.existsSync(path.join(versionDir, 'demo.html')),
});
versions.sort((a, b) => compareVersions(b.version, a.version));

let rootUpdated = false;
if (options.root && compareVersions(options.rootVersion, versions[0].version) >= 0) {
  for (const file of kRootFiles) {
    const from = path.join(options.root, file);
    if (!fs.existsSync(from)) {
      fail(`${options.root} has no ${file}`);
    }
    fs.copyFileSync(from, path.join(options.site, file));
  }
  rootUpdated = true;
}
fs.writeFileSync(path.join(options.site, '.nojekyll'), '');

const budgetBytes = options.budgetMb * 1024 * 1024;
const removed = [];
const siteSize = () => directorySize(options.site) - (fs.existsSync(path.join(options.site, '.git'))
  ? directorySize(path.join(options.site, '.git')) : 0);
while (siteSize() > budgetBytes) {
  const oldest = versions.findLast((entry) => entry.version !== options.version);
  if (!oldest) {
    break;
  }
  fs.rmSync(path.join(options.site, `v${oldest.version}`), { recursive: true, force: true });
  versions = versions.filter((entry) => entry !== oldest);
  removed.push(oldest.version);
}
fs.writeFileSync(listFile, `${JSON.stringify(versions, null, 2)}\n`);

console.log(`[rex86-pages] version=${options.version} version_mb=${megabytes(versionBytes)} ` +
  `root_updated=${rootUpdated} versions=${versions.map((entry) => entry.version).join(',')} ` +
  `removed=${removed.join(',') || 'none'} site_mb=${megabytes(siteSize())} budget_mb=${options.budgetMb}`);
