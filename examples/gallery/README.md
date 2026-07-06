# examples/gallery — manual native-feature check

A small Engawa app that exercises each namespace from a button and logs the result. Its point is the
parts the automated conformance suite can only **substitute**: `dialog.open/save` really opens the
native file picker and returns the chosen path, `notification.show` really posts a system toast,
`shellOpen.openExternal` really launches the browser. Everything else (clipboard, fs, sqlite, window,
path, app, update) is exercised against the real host too.

Run it (needs an interactive desktop session — WebView2 draws a real window):

```
engawa dev --dir examples/gallery
# or, from the repo without linking the CLI:
node cli/src/main.ts dev --dir examples/gallery
```

Click a button, watch the log. The dialog/notification/shellOpen buttons are the interactive checks
that can't be automated (which is exactly why the conformance suite uses substitutes for them).
