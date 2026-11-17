// SPDX-License-Identifier: AGPL-3.0-or-later
(function (global) {
  'use strict';
  var ws = null;
  var handlers = [];

  function connect(url) {
    if (!url) url = 'ws://' + location.host + '/ws';
    
    // Автоматическая починка адреса (чтобы не было ошибок ввода)
    if (!/^wss?:\/\//i.test(url)) {
      url = 'ws://' + url;
    }
    // Если нет в конце /ws, добавляем его
    if (!/\/ws$/i.test(url) && !/\/ws\?/.test(url)) {
      url = url.replace(/\/$/, '') + '/ws';
    }

    // Закрываем старое соединение, если оно было
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
      emit('message', msg);
    };
  }

  function send(obj) {
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify(obj));
    } else {
      console.warn("Нет соединения с сервером");
    }
  }

  function on(fn) { handlers.push(fn); }
  function emit(type, data) {
    for (var i = 0; i < handlers.length; i++) handlers[i](type, data);
  }

  global.MCGNet = { connect: connect, send: send, on: on };
})(window);