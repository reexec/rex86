// The module limit probe's Worker (design #49, decision 5): creates small
// modules with new WebAssembly.Module and new WebAssembly.Instance, keeping
// every instance, until creation fails or the cap is reached, and posts
// { modules, functions, bytes, ms, error }. Each module exports `funcs`
// functions; Firefox's limit counts modules whatever that number.

function leb(value) {
  const out = [];
  do {
    let byte = value & 0x7f;
    value >>>= 7;
    if (value) {
      byte |= 0x80;
    }
    out.push(byte);
  } while (value);
  return out;
}

function section(id, body) {
  return [id, ...leb(body.length), ...body];
}

// funcs functions of type (i32) -> i32, each adding salted constants to its
// argument, so that no two modules are the same.
function makeModule(funcs, salt) {
  const types = section(1, [1, 0x60, 1, 0x7f, 1, 0x7f]);
  const functions = section(3, [...leb(funcs), ...Array(funcs).fill(0)]);
  const exports = [];
  const bodies = [];
  for (let f = 0; f < funcs; ++f) {
    const name = [...`b${f}`].map((c) => c.charCodeAt(0));
    exports.push(name.length, ...name, 0, ...leb(f));
    const code = [];
    for (let k = 0; k < 20; ++k) {
      code.push(0x20, 0, 0x41, ...leb((salt + k * 7 + f) & 0x3fff), 0x6a, 0x21, 0);
    }
    code.push(0x20, 0, 0x0b);
    const body = [0, ...code];
    bodies.push(...leb(body.length), ...body);
  }
  return new Uint8Array([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0, ...types, ...functions,
    ...section(7, [...leb(funcs), ...exports]), ...section(10, [...leb(funcs), ...bodies])]);
}

self.onmessage = ({ data: { funcs, cap } }) => {
  const kept = [];
  const start = performance.now();
  let modules = 0;
  let error = '';
  try {
    for (; modules < cap; ++modules) {
      kept.push(new WebAssembly.Instance(new WebAssembly.Module(makeModule(funcs, modules))));
    }
  } catch (e) {
    error = String(e);
  }
  self.postMessage({
    modules,
    functions: modules * funcs,
    bytes: makeModule(funcs, 0).length,
    ms: Math.round(performance.now() - start),
    error,
  });
};
