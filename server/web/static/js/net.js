// SPDX-License-Identifier: AGPL-3.0-or-later
(function (global) {
  'use strict';
  var ws = null;
  var handlers = [];

  function connect(url) {
    if (!url) url = location.host + '/ws';
    
    // Умная починка адреса
    if (!/^wss?:\/\//i.test(url)) {
      // Если мы на HTTPS (GitHub Pages), принудительно ставим wss://
      // Если на HTTP (локально), ставим ws://
      var protocol = location.protocol === 'https:' ? 'wss://' : 'ws://';
      url = protocol + url;
    }
    if (!/\/ws$/i.test(url) && !/\/ws\?/.test(url)) {
      url = url.replace(/\/$/, '') + '/ws';
    }

    if (ws) {
      ws.onclose = null;
      ws.close();
    }

    ws = new WebSocket(url);
    
    ws.onopen = function () { emit('open'); };
    ws.onclose = function () { emit('close'); };
    ws.onerror = function (e) { emit('error', e); };
    ws.onmessage = function (ev) {
      var msg;
      try { msg = JSON.parse(ev.data); } catch (e) { return; }
      
      // Обработка команды переноса на другой сервер
      if (msg.type === 'transfer' && msg.target) {
          emit('transfer_start', msg.target);
          return;
      }
      
      emit('message', msg);
    };
  }

  function send(obj) {
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify(obj));
    }
  }

  function on(fn) { handlers.push(fn); }
  function emit(type, data) {
    for (var i = 0; i < handlers.length; i++) handlers[i](type, data);
  }

  global.MCGNet = { connect: connect, send: send, on: on };
})(window);