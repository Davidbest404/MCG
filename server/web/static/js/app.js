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
      if (type === 'open') { 
          status.textContent = 'подключено'; 
          status.style.color = '#4caf50'; // Зеленый
      }
      if (type === 'close') { 
          status.textContent = 'отключено'; 
          status.style.color = '#f44336'; // Красный
          appendLog('Система', 'Соединение с сервером потеряно.');
      }
      if (type === 'error') { 
          appendLog('Система', 'Ошибка подключения. Проверьте адрес сервера.');
      }
      if (type === 'message') {
          if (data.type === 'ui_buttons') {
              renderActionButtons(data.buttons);
          } else {
              handleMessage(data);
          }
      }
      if (type === 'transfer_start') {
          // Показываем экран загрузки (вместо этого можно сделать красивый оверлей)
          document.getElementById('status').textContent = 'Переход на другой сервер...';
          MCGNet.connect(data); // data - это URL нового сервера
      }
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
	
    document.getElementById('toggle-hotbar').onclick = function() {
        document.getElementById('hotbar').classList.toggle('hidden');
    };
    document.getElementById('toggle-hud').onclick = function() {
        document.getElementById('hud').classList.toggle('hidden-buttons');
    };

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

// --- СИСТЕМА HUD КНОПОК И ПЕРЕТАСКИВАНИЯ ---
function renderActionButtons(buttons) {
    var hud = document.getElementById('hud');
    var hotbar = document.getElementById('hotbar');
    hud.innerHTML = ''; hotbar.innerHTML = '';

    buttons.forEach(function(btnData) {
        var btn = document.createElement('div');
        btn.className = 'hud-btn';
        btn.textContent = btnData.label;
        
        var savedState = localStorage.getItem('hud_state_' + btnData.id);
        
        if (savedState === 'in_hotbar' || !savedState) {
            hotbar.appendChild(btn);
        } else {
            var pos = JSON.parse(savedState);
            btn.style.position = 'absolute';
            btn.style.left = pos.left + 'px';
            btn.style.top = pos.top + 'px';
            hud.appendChild(btn);
        }

        btn.onclick = function(e) {
            if (btn.dataset.dragged === "true") return;
            MCGNet.send({ type: 'chat', text: btnData.command, from: myName });
            appendLog('Вы', btnData.command);
        };

        var isDragging = false;
        var startX, startY, initialLeft, initialTop;

        function dragStart(e) {
            var ev = e.touches ? e.touches[0] : e;
            isDragging = true;
            btn.dataset.dragged = "false";
            
            if (btn.parentNode === hotbar) {
                var rect = btn.getBoundingClientRect();
                var parentRect = hud.getBoundingClientRect();
                btn.style.position = 'absolute';
                btn.style.left = (rect.left - parentRect.left) + 'px';
                btn.style.top = (rect.top - parentRect.top) + 'px';
                hud.appendChild(btn);
            }
            
            var rect = btn.getBoundingClientRect();
            var parentRect = hud.getBoundingClientRect();
            initialLeft = rect.left - parentRect.left;
            initialTop = rect.top - parentRect.top;
            startX = ev.clientX;
            startY = ev.clientY;
        }

        function dragMove(e) {
            if (!isDragging) return;
            var ev = e.touches ? e.touches[0] : e;
            
            // Защита от ошибок браузера
            if (e.cancelable) e.preventDefault(); 
            
            var dx = ev.clientX - startX;
            var dy = ev.clientY - startY;
            if (Math.abs(dx) > 5 || Math.abs(dy) > 5) btn.dataset.dragged = "true";
            
            btn.style.left = (initialLeft + dx) + 'px';
            btn.style.top = (initialTop + dy) + 'px';
        }

        function dragEnd(e) {
            if (!isDragging) return;
            isDragging = false;
            
            if (btn.dataset.dragged === "true") {
                var hotbarRect = hotbar.getBoundingClientRect();
                var ev = e.changedTouches ? e.changedTouches[0] : e;
                
                // Если отпустили над панелью — прячем обратно
                if (ev.clientY >= hotbarRect.top && ev.clientY <= hotbarRect.bottom) {
                    btn.style.position = 'static';
                    btn.style.left = 'auto';
                    btn.style.top = 'auto';
                    hotbar.appendChild(btn);
                    localStorage.setItem('hud_state_' + btnData.id, 'in_hotbar');
                } else {
                    var rect = btn.getBoundingClientRect();
                    var parentRect = hud.getBoundingClientRect();
                    localStorage.setItem('hud_state_' + btnData.id, JSON.stringify({
                        left: rect.left - parentRect.left,
                        top: rect.top - parentRect.top
                    }));
                }
            }
        }

        btn.addEventListener('mousedown', dragStart);
        window.addEventListener('mousemove', dragMove);
        window.addEventListener('mouseup', dragEnd);
        btn.addEventListener('touchstart', dragStart, {passive: false});
        window.addEventListener('touchmove', dragMove, {passive: false});
        window.addEventListener('touchend', dragEnd);
    });
}

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else { init(); }
})();