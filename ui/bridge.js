'use strict';
window.clickpulseLocale = window.clickpulseLocale || 'en';
window.clickpulsePlatform = window.clickpulsePlatform || 'windows';
window.clickpulseTheme = window.clickpulseTheme || 'blue';
if (!window.ipc) {
  window.ipc = {
    postMessage(msg) {
      if (window.chrome && window.chrome.webview && typeof window.chrome.webview.postMessage === 'function') {
        window.chrome.webview.postMessage(typeof msg === 'string' ? msg : JSON.stringify(msg));
      }
    }
  };
}
