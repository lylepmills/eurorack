// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
//
// AudioWorklet processor for `plaits-lab dev` live audition. Runs the package's
// engine compiled to WebAssembly (wasm_audition.cc) on the audio thread and
// renders continuously, so the four Plaits controls, pitch, and trigger respond
// in real time — no render-and-listen round trip.
//
// The compiled WebAssembly.Module is passed in via processorOptions.wasmModule
// (compiled on the main thread). The standalone build has NO imports and exports
// its own memory, so instantiation is synchronous and needs no import object.
// Parameters arrive over the message port; no SharedArrayBuffer, so the page is
// served without COOP/COEP.

class PlaitsAuditionProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.ready = false;
    this.exports = null;
    this.mainPtr = 0;
    this.auxPtr = 0;
    this.monitor = 0; // 0 main only (default), 1 aux only, 2 stereo (main L / aux R)

    const module = options && options.processorOptions && options.processorOptions.wasmModule;
    if (module) {
      try {
        const instance = new WebAssembly.Instance(module, {});
        const e = instance.exports;
        if (typeof e._initialize === 'function') e._initialize();
        e.init();
        this.exports = e;
        this.mainPtr = e.main_out();
        this.auxPtr = e.aux_out();
        const p = options.processorOptions.params;
        if (p) e.set_params(p.note, p.harmonics, p.timbre, p.morph, p.macro);
        const env = options.processorOptions.envMode | 0;
        if (typeof e.set_env_mode === 'function') e.set_env_mode(env);
        this.ready = true;
      } catch (err) {
        this.port.postMessage({ type: 'error', message: String(err && err.message || err) });
      }
    }

    this.port.onmessage = (event) => {
      const data = event.data || {};
      if (!this.ready) return;
      if (data.type === 'params') {
        this.exports.set_params(data.note, data.harmonics, data.timbre, data.morph, data.macro);
      } else if (data.type === 'trigger') {
        this.exports.trigger();
      } else if (data.type === 'env') {
        if (typeof this.exports.set_env_mode === 'function') this.exports.set_env_mode(data.value | 0);
      } else if (data.type === 'monitor') {
        this.monitor = data.value | 0;
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0];
    if (!out || out.length === 0) return true;
    const frames = out[0].length;
    if (!this.ready) {
      for (let c = 0; c < out.length; c++) out[c].fill(0);
      return true;
    }
    this.exports.render(frames);
    // Re-view each block: wasm memory could in principle detach on growth.
    const main = new Float32Array(this.exports.memory.buffer, this.mainPtr, frames);
    const aux = new Float32Array(this.exports.memory.buffer, this.auxPtr, frames);
    const left = out[0];
    const right = out.length > 1 ? out[1] : null;
    for (let i = 0; i < frames; i++) {
      let l, r;
      if (this.monitor === 0) { l = main[i]; r = main[i]; }
      else if (this.monitor === 1) { l = aux[i]; r = aux[i]; }
      else { l = main[i]; r = aux[i]; }
      left[i] = l;
      if (right) right[i] = r;
    }
    return true;
  }
}

registerProcessor('plaits-audition', PlaitsAuditionProcessor);
