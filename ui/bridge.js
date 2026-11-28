'use strict';
window.clickpulseLocale = window.clickpulseLocale || 'en';
window.clickpulsePlatform = window.clickpulsePlatform || 'windows';
window.clickpulseTheme = window.clickpulseTheme || 'blue';
window.clickpulseColorMode = window.clickpulseColorMode || 'system';
window.clickpulseSystemDark = !!window.clickpulseSystemDark;

if (typeof document !== 'undefined' && document.documentElement) {
  document.documentElement.dataset.platform = window.clickpulsePlatform;
}

if (!window.ipc) {
  window.ipc = {
    postMessage(msg) {
      const payload = typeof msg === 'string' ? msg : JSON.stringify(msg);
      if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === 'function') {
        window.chrome.webview.postMessage(payload);
      } else if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.ipc && typeof window.webkit.messageHandlers.ipc.postMessage === 'function') {
        window.webkit.messageHandlers.ipc.postMessage(payload);
      }
    }
  };
}
