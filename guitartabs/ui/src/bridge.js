import { demoDispatch, startDemo } from "./demo.js";

export function installBridge(handlers) {
  window.gtApplyState = handlers.onState;
  window.gtApplyMap = handlers.onMap;
  window.gtApplyTransport = handlers.onTransport;
  window.gtApplyStatus = handlers.onStatus;
  window.gtConfirmOverwrite = handlers.onConfirm;
  if (window.webkit?.messageHandlers?.reaper) post({ type: "ready" });
  else startDemo(handlers);
}

export function post(message) {
  if (window.webkit?.messageHandlers?.reaper) {
    window.webkit.messageHandlers.reaper.postMessage(JSON.stringify(message));
    return;
  }
  demoDispatch(message);
}
