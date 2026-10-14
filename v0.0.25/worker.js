// Starts one Emscripten tool of a REX86_BROWSER build in a Worker (design
// #48, decision 3): worker.js?tool=rex86_irdiff&args=["--cases","2000"].
// Output lines and the exit code go to the page as messages.
//
// The Pages site's comparison (design #52, decision 4) starts this file from
// every deployed version's directory, so the query (tool=, args= as a JSON
// array) and the messages ({ kind: 'out' | 'err' | 'exit', text, code }) are
// a contract: a change has to keep the old form working.

const params = new URL(self.location.href).searchParams;
const tool = params.get('tool') ?? '';
if (!/^rex86_[a-z0-9_]+$/.test(tool)) {
  postMessage({ kind: 'exit', code: 2, text: `not a rex86 tool: ${tool}` });
} else {
  self.Module = {
    arguments: JSON.parse(params.get('args') ?? '[]'),
    print: (text) => postMessage({ kind: 'out', text }),
    printErr: (text) => postMessage({ kind: 'err', text }),
    onExit: (code) => postMessage({ kind: 'exit', code }),
    onAbort: (what) => postMessage({ kind: 'exit', code: 134, text: `abort: ${what}` }),
    locateFile: (file) => `bin/${file}`,
  };
  importScripts(`bin/${tool}.js`);
}
