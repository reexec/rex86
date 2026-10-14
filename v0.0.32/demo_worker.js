// The web demo's Worker (design #62, decision 2): loads the rex86_demo
// module from this directory's bin/, as worker.js does for the test tools,
// and runs it a frame at a time, posting the counts after each frame so the
// page only draws. A Worker may compile generated wasm modules synchronously,
// which the wasm engine's installer does.
//
// Messages in:
//   { type: 'start', run, workload, engine, budget }   frames until stopped
//   { type: 'stop' }
//   { type: 'measure', engine, workloads, frames, budget }   as rex86_bench
// Messages out:
//   { type: 'ready', info, wasmFileBytes } | { type: 'error', text }
//   { type: 'frame', run, ok, stats, failure, heapBytes }   (run echoes start's)
//   { type: 'measured', workload, engine, ok, failure, frameMs, firstFrameMs, stats, heapBytes }
//   { type: 'measure-done' } | { type: 'stopped' }

/* global createRex86Demo */
importScripts('bin/rex86_demo.js');

let api = null;
// Bumped by every command, so a frame loop from an earlier one ends.
let generation = 0;

function readStats() {
  const stats = {};
  for (const [index, name] of api.info.stats.entries()) {
    stats[name] = api.stat(index);
  }
  return stats;
}

function wasmFileBytes() {
  const url = new URL('bin/rex86_demo.wasm', self.location.href).href;
  const entry = self.performance?.getEntriesByName?.(url)?.[0];
  return entry?.decodedBodySize || entry?.encodedBodySize || null;
}

// The next frame after the event loop has had a turn, so commands get in.
const yieldThen = (callback) => setTimeout(callback, 0);

function runLoop(token, budget, run) {
  if (token !== generation) {
    return;
  }
  const ok = api.frame(budget) === 1;
  postMessage({
    type: 'frame', run, ok, stats: readStats(), failure: ok ? '' : api.failure(), heapBytes: api.module.HEAP8.length,
  });
  if (ok) {
    yieldThen(() => runLoop(token, budget, run));
  }
}

function measure(token, { engine, workloads, frames, budget }) {
  const queue = [...workloads];
  const next = () => {
    if (token !== generation) {
      return;
    }
    const workload = queue.shift();
    if (workload === undefined) {
      postMessage({ type: 'measure-done' });
      return;
    }
    // Like rex86_bench: the first frame's time counts from loading the
    // machine; the MIPS from the frames alone.
    const begin = performance.now();
    let ok = api.start(workload, engine) === 1;
    let firstFrameMs = 0;
    const frameMs = [];
    for (let i = 0; ok && i < frames; ++i) {
      ok = api.frame(budget) === 1;
      if (ok) {
        frameMs.push(api.stat(api.info.stats.indexOf('frame_ms')));
        if (i === 0) {
          firstFrameMs = performance.now() - begin;
        }
      }
    }
    postMessage({
      type: 'measured', workload, engine, ok, failure: ok ? '' : api.failure(), frameMs, firstFrameMs,
      stats: readStats(), heapBytes: api.module.HEAP8.length,
    });
    api.stop();
    yieldThen(next);
  };
  next();
}

const ready = createRex86Demo({ locateFile: (file) => `bin/${file}` }).then((module) => {
  const wrap = (name, returns, args) => module.cwrap(name, returns, args);
  api = {
    module,
    info: JSON.parse(wrap('rex86_demo_info', 'string', [])()),
    start: wrap('rex86_demo_start', 'number', ['number', 'number']),
    frame: wrap('rex86_demo_frame', 'number', ['number']),
    stat: wrap('rex86_demo_stat', 'number', ['number']),
    failure: wrap('rex86_demo_failure', 'string', []),
    stop: wrap('rex86_demo_stop', null, []),
  };
  postMessage({ type: 'ready', info: api.info, wasmFileBytes: wasmFileBytes() });
}, (error) => {
  postMessage({ type: 'error', text: `could not load the demo engine: ${error}` });
});

self.onmessage = async ({ data }) => {
  await ready;
  if (api === null) {
    return;
  }
  const token = ++generation;
  if (data.type === 'start') {
    if (api.start(data.workload, data.engine) !== 1) {
      postMessage({
        type: 'frame', run: data.run, ok: false, stats: readStats(), failure: api.failure(),
        heapBytes: api.module.HEAP8.length,
      });
      return;
    }
    yieldThen(() => runLoop(token, data.budget, data.run));
  } else if (data.type === 'measure') {
    measure(token, data);
  } else if (data.type === 'stop') {
    api.stop();
    postMessage({ type: 'stopped' });
  }
};
