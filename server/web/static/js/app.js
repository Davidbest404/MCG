// SPDX-License-Identifier: AGPL-3.0-or-later
(function () {
  'use strict';
  var canvas, log, input, status, toggleBtn;
  var renderMode = '2d';
  var myName = 'Игрок';

  function init() {
    canvas = document.getElementById('world');
    log = document.getElementById('chat-log');
    input = document.getElementById('chat-input');
    status = document.getElementById('status');
    toggleBtn = document.getElementById('toggle-render');

    // Пробуем 3D, при неудаче — 2D
    if (!MCGRender3D.init(canvas)) {
      MCGRender2D.init(canvas);
    } else {
      renderMode = '3d';
      toggleBtn.textContent = '3D';
    }

    toggleBtn.addEventListener('click', function () {
      if (renderMode === '2d' && MCGRender3D.isAvailable()) {
        renderMode = '3d'; toggleBtn.textContent = '3D';
        MCGRender3D.init(canvas);
      } else {
        renderMode = '2d'; toggleBtn.textContent = '2D';
        MCGRender2D.init(canvas);
      }
    });

    // Обработка сетевых событий
    MCGNet.on(function (type, data) {
      if (type === 'open') { status.textContent = 'подключено'; }
      if (type === 'close') { status.textContent = 'отключено'; }
      if (type === 'message') handleMessage(data);
    });

    // Отправка сообщений в чат
    document.getElementById('chat-form').addEventListener('submit', function (e) {
      e.preventDefault();
      var text = input.value.trim();
      if (!text) return;
      MCGNet.send({ type: 'chat', text: text, from: myName });
      appendLog('Вы', text);
      input.value = '';
    });

    // Показываем экран выбора сервера при запуске
    showServerList();
  }

  function showServerList() {
    var container = document.getElementById('server-list-screen');
    var listEl = document.getElementById('saved-servers');
    var servers = MCGServerList.get();

    listEl.innerHTML = '';
    servers.forEach(function (srv, i) {
      var li = document.createElement('li');
      li.textContent = srv.name + ' (' + srv.url + ') ';
      
    var btn = document.createElement('button');
    btn.textContent = 'Войти';
    btn.onclick = function () {
      container.style.display = 'none';
      document.getElementById('app').style.display = 'flex'; // <--- ДОБАВИТЬ ЭТУ СТРОКУ
      MCGNet.connect(srv.url);
    };
      
      var del = document.createElement('button');
      del.textContent = 'Удалить';
      del.onclick = function () {
        MCGServerList.remove(i);
        showServerList();
      };
      
      li.appendChild(btn);
      li.appendChild(del);
      listEl.appendChild(li);
    });

    // Форма добавления нового сервера
    var form = document.getElementById('add-server-form');
    form.onsubmit = function (e) {
      e.preventDefault();
      var name = document.getElementById('server-name').value.trim();
      var url = document.getElementById('server-url').value.trim();
      if (!name || !url) return;
      MCGServerList.add(name, url);
      showServerList();
      form.reset();
    };

    // Кнопка быстрого входа на локальный сервер
    document.getElementById('connect-default').onclick = function () {
      container.style.display = 'none';
      document.getElementById('app').style.display = 'flex'; // <--- ДОБАВИТЬ ЭТУ СТРОКУ
      MCGNet.connect('ws://' + location.host + '/ws');
    };

    container.style.display = 'block';
  }

  function handleMessage(msg) {
    if (msg.type === 'welcome' || msg.type === 'chat' || msg.type === 'echo') {
      var from = msg.from || 'Сервер';
      appendLog(from, msg.text || '');
      if (from !== myName) {
        MCGTTS.speak(msg.text, { pitch: msg.pitch || 1, rate: msg.rate || 1 });
      }
    }
  }

  function appendLog(from, text) {
    var div = document.createElement('div');
    div.className = 'line';
    var b = document.createElement('b');
    b.textContent = from + ': ';
    div.appendChild(b);
    div.appendChild(document.createTextNode(text));
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else { init(); }
})();