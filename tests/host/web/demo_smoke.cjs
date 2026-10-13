// The web demo engine's Node smoke (design #62, decision 4): loads the
// rex86_demo module factory as the page's Worker does, runs every workload on
// every engine until a lap ends and checks that it ends verified and the
// counts move. Prints [rex86-demo] lines; exits 1 on any failure.
//
//   node tests/host/web/demo_smoke.cjs <build>/bin/rex86_demo.js

'use strict';

const path = require('node:path');

const kMaxFrames = 30;
const kBudget = 100000;

async function main() {
  const file = process.argv[2];
  if (!file) {
    console.error('usage: node demo_smoke.cjs <path to rex86_demo.js>');
    return 2;
  }
  const createRex86Demo = require(path.resolve(file));
  const demo = await createRex86Demo();
  const call = (name, returns, args) => demo.cwrap(name, returns, args);
  const info = JSON.parse(call('rex86_demo_info', 'string', [])());
  const start = call('rex86_demo_start', 'number', ['number', 'number']);
  const frame = call('rex86_demo_frame', 'number', ['number']);
  const stat = call('rex86_demo_stat', 'number', ['number']);
  const failure = call('rex86_demo_failure', 'string', []);
  const index = (name) => info.stats.indexOf(name);

  let failed = info.workloads.length === 0 || info.image_error !== '';
  console.log(`[rex86-demo] version=${info.version} workloads=${info.workloads.join(',')} ` +
    `engines=${info.engines.join(',')} boards=${info.boards.length} stats=${info.stats.length}`);
  for (const [engine, engineName] of info.engines.entries()) {
    for (const [workload, name] of info.workloads.entries()) {
      // Frames until a lap has ended verified (mixed takes several).
      let ok = start(workload, engine) === 1;
      let frames = 0;
      while (ok && frames < kMaxFrames && stat(index('laps')) < 1) {
        ok = frame(kBudget) === 1;
        ++frames;
      }
      const retired = stat(index('retired'));
      const laps = stat(index('laps'));
      ok = ok && retired === frames * kBudget && laps >= 1 && stat(index('frame_ms')) >= 0;
      console.log(`[rex86-demo] engine=${engineName} workload=${name} laps=${laps} retired=${retired} ` +
        `blocks_translated=${stat(index('blocks_translated'))} result=${ok ? 'ok' : 'fail'}` +
        (ok ? '' : ` reason="${failure()}"`));
      failed ||= !ok;
    }
  }
  // An unknown workload is refused with a message.
  const refused = start(info.workloads.length, 0) === 0 && failure() !== '';
  console.log(`[rex86-demo] check=refuse result=${refused ? 'ok' : 'fail'}`);
  failed ||= !refused;
  console.log(`[rex86-demo] result=${failed ? 'fail' : 'ok'}`);
  return failed ? 1 : 0;
}

main().then((code) => process.exit(code), (error) => {
  console.error(error);
  process.exit(1);
});
