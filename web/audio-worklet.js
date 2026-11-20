/* A22 Browser Studio - AudioWorklet ring-buffer sink (zero-glitch stereo playback) */
class A22AudioSink extends AudioWorkletProcessor {
  constructor() {
    super();
    this.cap = 16384; /* stereo frames */
    this.bufL = new Float32Array(this.cap);
    this.bufR = new Float32Array(this.cap);
    this.r = 0;
    this.w = 0;
    this.count = 0;
    this.port.onmessage = (e) => {
      const { l, r } = e.data;
      const n = l.length;
      for (let i = 0; i < n; i++) {
        if (this.count >= this.cap) {
          this.r = (this.r + 1) & (this.cap - 1);
          this.count--;
        }
        this.bufL[this.w] = l[i];
        this.bufR[this.w] = r[i];
        this.w = (this.w + 1) & (this.cap - 1);
        this.count++;
      }
    };
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    const outL = out[0];
    const outR = out[1] || outL;
    const n = outL.length;
    for (let i = 0; i < n; i++) {
      if (this.count > 0) {
        outL[i] = this.bufL[this.r];
        outR[i] = this.bufR[this.r];
        this.r = (this.r + 1) & (this.cap - 1);
        this.count--;
      } else {
        outL[i] = 0;
        outR[i] = 0;
      }
    }
    if (this.count < 1024) {
      this.port.postMessage(1024 - this.count);
    }
    return true;
  }
}

registerProcessor("a22-audio-sink", A22AudioSink);
