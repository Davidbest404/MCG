// SPDX-License-Identifier: AGPL-3.0-or-later
(function () {
  'use strict';
  var canvas, log, input, status, toggleBtn;
  var myName = 'Игрок';
  var isAdmin = false;
  var isAuthenticated = false;
  var uiLocked = false;
  var serverButtons = [];
  var coordinateType = 'float';

  function init() {
    canvas = document.getElementById('world');
    log = document.getElementById('chat-log');
    input = document.getElementById('chat-input');
    status = document.getElementById('status');
    toggleBtn = document.getElementById('toggle-render');
    toggleBtn.textContent = '2D';

    MCGRender2D.init(canvas);

    window.addEventListener('keydown', function(e) {
      if (e.target.tagName === 'INPUT' || !isAuthenticated) return;

      var customBtns = JSON.parse(localStorage.getItem('custom_hud_buttons') || '[]');
      var allBtns = serverButtons.concat(customBtns);
      
      for (var i = 0; i < allBtns.length; i++) {
        var b = allBtns[i];
        var keys = JSON.parse(localStorage.getItem('keys_' + b.id) || '[]');
        if (keys.length === 0 && b.key) keys = [b.key]; // Совместимость со старым форматом
        
        if (keys.indexOf(e.code) !== -1) {
            e.preventDefault();
            MCGNet.send({ type: 'chat', text: b.command, from: myName }); 
            appendLog('Вы', b.command);
            return; 
        }
      }

      // Отправляем сырое нажатие клавиши в Lua
      MCGNet.send({ type: 'input', key: e.code });
    });

    MCGNet.on(function (type, data) {
      if (type === 'open') { status.textContent = 'подключено'; status.style.color = '#4caf50'; }
      if (type === 'close') { status.textContent = 'отключено'; status.style.color = '#f44336'; appendLog('Система', 'Соединение потеряно.'); }
      
      if (type === 'message') {
        if (data.type === 'require_auth') { document.getElementById('auth-modal').classList.remove('hidden'); } 
        else if (data.type === 'auth_error') { document.getElementById('auth-error').textContent = data.text; } 
        else if (data.type === 'auth_success') {
          isAuthenticated = true;
          document.getElementById('auth-modal').classList.add('hidden');
          myName = data.user.username;
          isAdmin = data.user.is_admin;
          coordinateType = data.server.coordinate_type || 'float'; 
          MCGRender2D.setMyUsername(myName);
          
          document.getElementById('player-name').textContent = myName + (isAdmin ? " (Админ)" : "");
          document.getElementById('server-ip').textContent = "IP: " + data.server.ip;
          document.getElementById('server-title').textContent = data.server.name;
          document.getElementById('app').style.display = 'flex';
          
          requestAnimationFrame(function() { MCGRender2D.resize(); });
        } 
        else if (data.type === 'chunk') { MCGRender2D.setChunk(data.cx, data.cy, data.data); }
        else if (data.type === 'players_update') { MCGRender2D.setPlayers(data.players); }
        else if (data.type === 'player_moved') { MCGRender2D.updatePlayer(data.username, data.x, data.y); }
        else if (data.type === 'camera_rotate') { 
            if (data.current_angle !== undefined) MCGRender2D.setCameraAngle(data.current_angle);
            else MCGRender2D.rotateCamera(data.angle); 
        }
        else if (data.type === 'ui_buttons') { serverButtons = data.buttons; rebuildAllButtons(); } 
        else { handleMessage(data); }
      }
    });

    document.getElementById('server-ip').onclick = function() { this.classList.toggle('show'); };
    document.getElementById('auth-form').onsubmit = function(e) {
      e.preventDefault();
      MCGNet.send({ type: 'auth', username: document.getElementById('auth-username').value.trim(), password: document.getElementById('auth-password').value });
    };

    document.getElementById('chat-form').addEventListener('submit', function (e) {
      e.preventDefault();
      var text = input.value.trim();
      if (!text) return;
      MCGNet.send({ type: 'chat', text: text, from: myName });
      appendLog('Вы', text);
      input.value = '';
    });

    document.getElementById('toggle-hotbar').onclick = function() { document.getElementById('app').classList.toggle('hidden-all-ui'); };
    document.getElementById('toggle-hud').onclick = function() { 
        uiLocked = !uiLocked; 
        this.textContent = uiLocked ? "🔒 Замок" : "👁️ Кнопки";
        this.style.backgroundColor = uiLocked ? "#555" : "";
    };
    
    document.getElementById('toggle-editor').onclick = function() { renderEditorList(); document.getElementById('editor-modal').classList.remove('hidden'); };
    document.getElementById('close-editor').onclick = function() { document.getElementById('editor-modal').classList.add('hidden'); };
    document.getElementById('add-btn-form').onsubmit = function(e) {
      e.preventDefault();
      var label = document.getElementById('btn-label').value.trim();
      var command = document.getElementById('btn-command').value.trim();
      var key = document.getElementById('btn-key').value.trim();
      if (!label || !command) return;
      
      var customBtns = JSON.parse(localStorage.getItem('custom_hud_buttons') || '[]');
      customBtns.push({ id: 'custom_' + Date.now(), label: label, command: command, key: "" });
      localStorage.setItem('custom_hud_buttons', JSON.stringify(customBtns));
      
      if (key) {
          var newBtnId = customBtns[customBtns.length - 1].id;
          localStorage.setItem('keys_' + newBtnId, JSON.stringify([key]));
      }
      this.reset(); renderEditorList(); rebuildAllButtons();
    };

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
      btn.onclick = function () { container.style.display = 'none'; MCGNet.connect(srv.url); };
      var del = document.createElement('button');
      del.textContent = 'Удалить';
      del.onclick = function () { MCGServerList.remove(i); showServerList(); };
      li.appendChild(btn); li.appendChild(del); listEl.appendChild(li);
    });

    document.getElementById('add-server-form').onsubmit = function (e) {
      e.preventDefault();
      var name = document.getElementById('server-name').value.trim();
      var url = document.getElementById('server-url').value.trim();
      if (!name || !url) return;
      MCGServerList.add(name, url); showServerList(); this.reset();
    };

    document.getElementById('connect-default').onclick = function () { container.style.display = 'none'; MCGNet.connect('ws://' + location.host + '/ws'); };
    container.style.display = 'flex';
  }

  function handleMessage(msg) {
    if (msg.type === 'welcome' || msg.type === 'chat' || msg.type === 'echo') {
      var from = msg.from || 'Сервер';
      appendLog(from, msg.text || '');
      if (from !== myName) { MCGTTS.speak(msg.text, { pitch: msg.pitch || 1, rate: msg.rate || 1 }); }
    }
  }

  function appendLog(from, text) {
    var div = document.createElement('div'); div.className = 'line';
    var b = document.createElement('b'); b.textContent = from + ': ';
    div.appendChild(b); div.appendChild(document.createTextNode(text));
    log.appendChild(div); log.scrollTop = log.scrollHeight;
  }

  function rebuildAllButtons() {
    var customBtns = JSON.parse(localStorage.getItem('custom_hud_buttons') || '[]');
    renderActionButtons(serverButtons.concat(customBtns));
  }

  function renderEditorList() {
    var list = document.getElementById('custom-btn-list');
    var customBtns = JSON.parse(localStorage.getItem('custom_hud_buttons') || '[]');
    var allBtns = serverButtons.concat(customBtns); 
    
    list.innerHTML = '';
    if (allBtns.length === 0) { list.innerHTML = '<li style="color:#888;">Нет доступных кнопок</li>'; return; }
    
    allBtns.forEach(function(btn) {
      var keys = JSON.parse(localStorage.getItem('keys_' + btn.id) || '[]');
      if (keys.length === 0 && btn.key) keys = [btn.key]; // Legacy
      
      var li = document.createElement('li');
      li.innerHTML = `<span><b>${btn.label}</b> (${btn.command})</span>`;
      
      // Список назначенных клавиш
      var keysContainer = document.createElement('div');
      keysContainer.style.display = 'flex';
      keysContainer.style.gap = '5px';
      keysContainer.style.flexWrap = 'wrap';
      keysContainer.style.marginTop = '5px';
      
      keys.forEach(function(k, idx) {
          var kSpan = document.createElement('span');
          kSpan.style.background = '#444';
          kSpan.style.padding = '2px 5px';
          kSpan.style.borderRadius = '3px';
          kSpan.style.cursor = 'pointer';
          kSpan.textContent = k + ' ✖';
          kSpan.onclick = function() {
              keys.splice(idx, 1);
              localStorage.setItem('keys_' + btn.id, JSON.stringify(keys));
              renderEditorList();
          };
          keysContainer.appendChild(kSpan);
      });
      
      var addKeyBtn = document.createElement('button');
      addKeyBtn.textContent = '+ Клавиша';
      addKeyBtn.style.background = '#2196f3';
      addKeyBtn.style.padding = '4px 8px';
      addKeyBtn.style.fontSize = '12px';
      addKeyBtn.onclick = function() {
        addKeyBtn.textContent = 'Жми...';
        addKeyBtn.style.background = '#ff9800';
        function captureKey(e) {
            e.preventDefault();
            var newKey = e.code; 
            if (keys.indexOf(newKey) === -1) keys.push(newKey);
            localStorage.setItem('keys_' + btn.id, JSON.stringify(keys));
            window.removeEventListener('keydown', captureKey, true);
            renderEditorList(); 
        }
        window.addEventListener('keydown', captureKey, true);
      };
      keysContainer.appendChild(addKeyBtn);
      li.appendChild(keysContainer);

      // Удаление кастомной кнопки
      var isCustom = customBtns.find(c => c.id === btn.id);
      if (isCustom) {
        var delBtn = document.createElement('button');
        delBtn.textContent = 'Удалить'; 
        delBtn.style.background = '#f44336'; 
        delBtn.style.padding = '4px 8px';
        delBtn.style.marginTop = '5px';
        delBtn.onclick = function() {
          customBtns.splice(customBtns.indexOf(isCustom), 1);
          localStorage.setItem('custom_hud_buttons', JSON.stringify(customBtns));
          renderEditorList(); rebuildAllButtons();
        };
        li.appendChild(delBtn);
      }
      
      list.appendChild(li);
    });
  }

  function renderActionButtons(buttons) {
    var hud = document.getElementById('hud'); var hotbar = document.getElementById('hotbar');
    hud.innerHTML = ''; hotbar.innerHTML = '';
    
    buttons.forEach(function(btnData) {
      var btn = document.createElement('div'); btn.className = 'hud-btn'; btn.textContent = btnData.label;
      var savedState = localStorage.getItem('hud_state_' + btnData.id);
      
      if (savedState === 'in_hotbar' || !savedState) { hotbar.appendChild(btn); } 
      else {
        var pos = JSON.parse(savedState); btn.style.position = 'absolute'; btn.style.left = pos.left + 'px'; btn.style.top = pos.top + 'px'; hud.appendChild(btn);
      }
      
      btn.onclick = function(e) {
        if (btn.dataset.dragged === "true") return;
        MCGNet.send({ type: 'chat', text: btnData.command, from: myName }); appendLog('Вы', btnData.command);
      };
      
      var isDragging = false, startX, startY, initialLeft, initialTop;
      function dragStart(e) {
        if (uiLocked) return; var ev = e.touches ? e.touches[0] : e; isDragging = true; btn.dataset.dragged = "false";
        if (btn.parentNode === hotbar) {
          var rect = btn.getBoundingClientRect(); var parentRect = hud.getBoundingClientRect();
          btn.style.position = 'absolute'; btn.style.left = (rect.left - parentRect.left) + 'px'; btn.style.top = (rect.top - parentRect.top) + 'px'; hud.appendChild(btn);
        }
        var rect = btn.getBoundingClientRect(); var parentRect = hud.getBoundingClientRect();
        initialLeft = rect.left - parentRect.left; initialTop = rect.top - parentRect.top; startX = ev.clientX; startY = ev.clientY;
      }
      function dragMove(e) {
        if (!isDragging) return; var ev = e.touches ? e.touches[0] : e; if (e.cancelable) e.preventDefault(); 
        var dx = ev.clientX - startX; var dy = ev.clientY - startY; if (Math.abs(dx) > 5 || Math.abs(dy) > 5) btn.dataset.dragged = "true";
        btn.style.left = (initialLeft + dx) + 'px'; btn.style.top = (initialTop + dy) + 'px';
      }
      function dragEnd(e) {
        if (!isDragging) return; isDragging = false;
        if (btn.dataset.dragged === "true") {
          var hotbarRect = hotbar.getBoundingClientRect(); var ev = e.changedTouches ? e.changedTouches[0] : e;
          if (ev.clientY >= hotbarRect.top && ev.clientY <= hotbarRect.bottom) {
            btn.style.position = 'static'; btn.style.left = 'auto'; btn.style.top = 'auto'; hotbar.appendChild(btn); localStorage.setItem('hud_state_' + btnData.id, 'in_hotbar');
          } else {
            var rect = btn.getBoundingClientRect(); var parentRect = hud.getBoundingClientRect();
            var maxX = parentRect.width - rect.width; var maxY = parentRect.height - rect.height;
            var newLeft = Math.max(0, Math.min(rect.left - parentRect.left, maxX)); var newTop = Math.max(0, Math.min(rect.top - parentRect.top, maxY));
            btn.style.left = newLeft + 'px'; btn.style.top = newTop + 'px';
            localStorage.setItem('hud_state_' + btnData.id, JSON.stringify({ left: newLeft, top: newTop }));
          }
        }
      }
      btn.addEventListener('mousedown', dragStart); window.addEventListener('mousemove', dragMove); window.addEventListener('mouseup', dragEnd);
      btn.addEventListener('touchstart', dragStart, {passive: false}); window.addEventListener('touchmove', dragMove, {passive: false}); window.addEventListener('touchend', dragEnd);
    });
  }

  if (document.readyState === 'loading') { document.addEventListener('DOMContentLoaded', init); } else { init(); }
})();