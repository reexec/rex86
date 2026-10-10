// The JavaScript half of the reference WasmModuleServices (designs #45 and
// #48): compiles and instantiates a module the core generated, against the
// core's own memory and indirect function table, and adds its function
// exports to that table, at once (rex86_wasm_install) or through Promises
// whose results wait in a queue (rex86_wasm_install_async, rex86_wasm_take).
// Link with --js-library and -sALLOW_TABLE_GROWTH.
addToLibrary({
  rex86_wasm_install__deps: ['$addFunction'],
  rex86_wasm_install: (bytes, size, exportCount, indices) => {
    try {
      const module = new WebAssembly.Module(HEAPU8.slice(bytes, bytes + size));
      const instance = new WebAssembly.Instance(module, {
        env: { memory: wasmMemory, table: wasmTable },
      });
      const names = WebAssembly.Module.exports(module)
        .filter((e) => e.kind === 'function')
        .map((e) => e.name);
      if (names.length < exportCount) {
        return 0;
      }
      for (let i = 0; i < exportCount; ++i) {
        HEAPU32[(indices >> 2) + i] = addFunction(instance.exports[names[i]], 'iiii');
      }
      return 1;
    } catch (error) {
      err(`rex86: a generated module failed to install: ${error}`);
      return 0;
    }
  },
  // Each asynchronous adapter's queue of finished installations, by owner:
  // { ticket, indices } or { ticket, indices: null } on failure. A result
  // whose owner was forgotten meanwhile gives its table slots back.
  $rex86WasmQueues: 'new Map()',
  rex86_wasm_install_async__deps: ['$addFunction', '$removeFunction', '$rex86WasmQueues'],
  rex86_wasm_install_async: (owner, ticket, bytes, size, exportCount) => {
    let queue = rex86WasmQueues.get(owner);
    if (queue === undefined) {
      queue = [];
      rex86WasmQueues.set(owner, queue);
    }
    // The bytes are copied now; the core may reuse them once Install returns.
    const copy = HEAPU8.slice(bytes, bytes + size);
    const indices = [];
    WebAssembly.compile(copy)
      .then((module) =>
        WebAssembly.instantiate(module, { env: { memory: wasmMemory, table: wasmTable } }).then(
          (instance) => ({ module, instance })))
      .then(({ module, instance }) => {
        if (rex86WasmQueues.get(owner) !== queue) {
          return;
        }
        const names = WebAssembly.Module.exports(module)
          .filter((e) => e.kind === 'function')
          .map((e) => e.name);
        if (names.length < exportCount) {
          throw new Error('missing exports');
        }
        for (let i = 0; i < exportCount; ++i) {
          indices.push(addFunction(instance.exports[names[i]], 'iiii'));
        }
        queue.push({ ticket, indices });
      })
      .catch((error) => {
        indices.forEach((index) => removeFunction(index));
        err(`rex86: a generated module failed to install: ${error}`);
        queue.push({ ticket, indices: null });
      });
  },
  rex86_wasm_take__deps: ['$removeFunction', '$rex86WasmQueues'],
  rex86_wasm_take: (owner, ticketOut, indicesOut, capacity) => {
    const queue = rex86WasmQueues.get(owner);
    const result = queue === undefined ? undefined : queue.shift();
    if (result === undefined) {
      return -1;
    }
    HEAPU32[ticketOut >> 2] = result.ticket;
    if (result.indices === null) {
      return 0;
    }
    if (result.indices.length > capacity) {
      result.indices.forEach((index) => removeFunction(index));
      return 0;
    }
    for (let i = 0; i < result.indices.length; ++i) {
      HEAPU32[(indicesOut >> 2) + i] = result.indices[i];
    }
    return result.indices.length;
  },
  rex86_wasm_forget__deps: ['$removeFunction', '$rex86WasmQueues'],
  rex86_wasm_forget: (owner) => {
    const queue = rex86WasmQueues.get(owner);
    if (queue === undefined) {
      return;
    }
    rex86WasmQueues.delete(owner);
    for (const result of queue) {
      if (result.indices !== null) {
        result.indices.forEach((index) => removeFunction(index));
      }
    }
  },
  rex86_wasm_release__deps: ['$removeFunction'],
  rex86_wasm_release: (indices, count) => {
    for (let i = 0; i < count; ++i) {
      removeFunction(HEAPU32[(indices >> 2) + i]);
    }
  },
});
