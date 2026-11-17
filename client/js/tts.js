// SPDX-License-Identifier: AGPL-3.0-or-later
(function (global) {
  'use strict';
  var enabled = true;
  var settings = { rate: 1, pitch: 1, volume: 1 };

  function speak(text, opts) {
    if (!enabled || !text || !('speechSynthesis' in window)) return;
    var u = new SpeechSynthesisUtterance(text);
    u.lang = 'ru-RU';
    u.rate = (opts && opts.rate) || settings.rate;
    u.pitch = (opts && opts.pitch) || settings.pitch;
    u.volume = (opts && opts.volume) || settings.volume;
    window.speechSynthesis.speak(u);
  }

  function configure(s) {
    if (s.rate != null) settings.rate = Math.max(0.1, Math.min(10, s.rate));
    if (s.pitch != null) settings.pitch = Math.max(0, Math.min(2, s.pitch));
    if (s.volume != null) settings.volume = Math.max(0, Math.min(1, s.volume));
  }

  global.MCGTTS = { speak: speak, configure: configure, setEnabled: function (v) { enabled = !!v; } };
})(window);