// The JavaScript half of the reference WasmModuleServices (design #45):
// compiles and instantiates a module the core generated, against the
// core's own memory and indirect function table, and adds its function
// exports to that table. Link with --js-library and -sALLOW_TABLE_GROWTH.
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
  rex86_wasm_release__deps: ['$removeFunction'],
  rex86_wasm_release: (indices, count) => {
    for (let i = 0; i < count; ++i) {
      removeFunction(HEAPU32[(indices >> 2) + i]);
    }
  },
});
