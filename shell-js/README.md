# shell-js

The shared JS runtime — **identical bytes on every host**. Injected immediately after `window.__shell` (contract §1). It implements `invoke()`, promise/`id` correlation, event subscription, and `__shell._deliver` on top of the host's two primitives (receive a string, evaluate a string). Populated in bootstrap stage 2.
