// Test-only DOM substitute; browser runs always use the actual browser DOM.
if (typeof window === 'undefined' && typeof process === 'object') {
  class ProbeTarget {
    constructor() { this.listeners = new Map(); }
    addEventListener(name, cb) { if (!this.listeners.has(name)) this.listeners.set(name, new Set()); this.listeners.get(name).add(cb); }
    removeEventListener(name, cb) { if (this.listeners.has(name)) this.listeners.get(name).delete(cb); }
    dispatchEvent(event) { for (const cb of this.listeners.get(event.type) || []) cb(event); return !event.defaultPrevented; }
  }
  class ProbeEvent {
    constructor(type, values={}) { this.type=type; Object.assign(this, values); this.defaultPrevented=false; }
    preventDefault() { if (this.cancelable) this.defaultPrevented=true; }
  }
  class ProbeCanvas extends ProbeTarget {
    constructor() { super(); this.attrs=new Map(); this.style={}; this.isConnected=true; this.width=800; this.height=450; }
    get clientWidth() { return this.style.display==='none' || !this.isConnected ? 0 : parseInt(this.style.width || '800'); }
    get clientHeight() { return this.style.display==='none' || !this.isConnected ? 0 : parseInt(this.style.height || '450'); }
    getAttribute(name) { return this.attrs.has(name) ? this.attrs.get(name) : null; }
    setAttribute(name, value) { this.attrs.set(name, value); }
    removeAttribute(name) { this.attrs.delete(name); }
    focus() { const old=document.activeElement; document.activeElement=this; if (old && old!==this) old.dispatchEvent(new ProbeEvent('blur')); this.dispatchEvent(new ProbeEvent('focus')); }
    getBoundingClientRect() { return {left:0, top:0, width:this.clientWidth, height:this.clientHeight}; }
    setPointerCapture() {}
    remove() { this.isConnected=false; }
  }
  const canvas=new ProbeCanvas();
  const outside=new ProbeTarget();
  outside.focus=function () { const old=document.activeElement; document.activeElement=this; if (old) old.dispatchEvent(new ProbeEvent('blur')); };
  globalThis.window=new ProbeTarget();
  globalThis.document=new ProbeTarget();
  document.activeElement=null; document.hidden=false;
  document.querySelector=selector => selector==='#canvas' ? canvas : (selector==='#outside' ? outside : null);
  document.getElementById=()=>null;
  globalThis.HTMLCanvasElement=ProbeCanvas;
  globalThis.Event=ProbeEvent;
  globalThis.KeyboardEvent=ProbeEvent;
  globalThis.WheelEvent=ProbeEvent;
  globalThis.PointerEvent=ProbeEvent;
  globalThis.ResizeObserver=class { observe() {} disconnect() {} };
  globalThis.getComputedStyle=element => ({visibility:'visible', display:element.style.display || 'block'});
  globalThis.devicePixelRatio=2;
}
