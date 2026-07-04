// Engawa shared runtime — identical bytes on every host (contract §1, §2).
//
// The host injects `globalThis.__shell` (the two primitives + handshake fields) at
// document start, then evaluates this file. shell.js builds invoke/promise-correlation
// and event subscription on top, and exposes the public runtime as `globalThis.engawa`.
//
// Hosts implement exactly two things: receive a string (`__shell.postMessage`) and
// evaluate a string (`__shell._deliver`, defined here). Everything protocol-shaped is here.

;(function (global) {
  'use strict';

  var shell = global.__shell;
  if (!shell || typeof shell.postMessage !== 'function') {
    throw new Error('engawa: __shell host primitives missing; the host must inject __shell before shell.js');
  }
  if (shell.__engawaLoaded) return;   // injection is idempotent per document (contract §6)
  shell.__engawaLoaded = true;

  var nextId = 1;
  var pending = new Map();             // id -> { resolve, reject }
  var listeners = new Map();           // topic -> Set<handler>

  // Host → JS delivery. The host evaluates `__shell._deliver(<json array of frames>)`.
  // The array is drained in one call; the host batches frames into one eval per tick
  // (contract §2.1). This function must never throw back into the host's eval.
  shell._deliver = function (jsonArrayString) {
    var frames;
    try {
      frames = JSON.parse(jsonArrayString);
    } catch (e) {
      return;                         // malformed batch: drop it, do not disturb the host
    }
    if (!Array.isArray(frames)) return;
    for (var i = 0; i < frames.length; i++) {
      dispatch(frames[i]);
    }
  };

  function dispatch(frame) {
    if (!frame || typeof frame !== 'object') return;
    if (frame.t === 'res') {
      var p = pending.get(frame.id);
      if (!p) return;                 // unknown or already-settled id: ignore
      pending.delete(frame.id);
      if (frame.ok) {
        p.resolve(frame.value);
      } else {
        var err = frame.err || {};
        var e = new Error(err.message || 'engawa command failed');
        e.code = err.code || 'EUNKNOWN';
        p.reject(e);
      }
    } else if (frame.t === 'evt') {
      var set = listeners.get(frame.topic);
      if (!set) return;
      set.forEach(function (h) {
        try { h(frame.payload); } catch (e) { /* a listener throwing must not break delivery */ }
      });
    }
  }

  // JS → host request. Returns a promise settled when the matching `res` frame arrives.
  function invoke(cmd, args) {
    var id = nextId++;
    return new Promise(function (resolve, reject) {
      pending.set(id, { resolve: resolve, reject: reject });
      shell.postMessage(JSON.stringify({
        t: 'req', id: id, cmd: cmd, args: args === undefined ? null : args
      }));
    });
  }

  // Event subscription. Returns an unsubscribe function.
  function on(topic, handler) {
    var set = listeners.get(topic);
    if (!set) { set = new Set(); listeners.set(topic, set); }
    set.add(handler);
    return function off() { set.delete(handler); };
  }

  function off(topic, handler) {
    var set = listeners.get(topic);
    if (set) set.delete(handler);
  }

  global.engawa = {
    contractVersion: shell.contractVersion,
    platform: shell.platform,
    capabilities: (shell.capabilities || []).slice(),
    invoke: invoke,
    on: on,
    off: off
  };
})(typeof globalThis !== 'undefined' ? globalThis : this);
