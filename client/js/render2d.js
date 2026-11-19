(function (global) {
  'use strict';
  var canvas, ctx;
  var TILE = 32;
  var CHUNK_SIZE = 32;
  var chunks = {}; // { "cx,cy": [[...]] }
  var players = {}; 
  var myUsername = null;
  var cameraX = 0, cameraY = 0;
  var cameraAngle = 0; 

  function init(c) {
    canvas = c;
    ctx = canvas.getContext('2d');
    if (window.ResizeObserver) new ResizeObserver(resize).observe(canvas.parentElement);
    window.addEventListener('resize', resize);
    resize();
  }

  function resize() {
    if (!canvas) return;
    var w = canvas.parentElement.clientWidth; var h = canvas.parentElement.clientHeight;
    if (w > 0 && h > 0) { canvas.width = w; canvas.height = h; draw(); }
  }

  function setChunk(cx, cy, data) {
    chunks[cx + ',' + cy] = data;
    draw();
  }

  function setPlayers(playerList) {
    players = {}; playerList.forEach(p => players[p.username] = p);
    if (myUsername && players[myUsername]) {
      cameraX = (players[myUsername].x * TILE); cameraY = (players[myUsername].y * TILE);
    }
    draw();
  }

  function updatePlayer(username, x, y) {
    if (!players[username]) players[username] = { username: username, x: x, y: y };
    else { players[username].x = x; players[username].y = y; }
    
    if (username === myUsername) { cameraX = (x * TILE); cameraY = (y * TILE); }
    draw();
  }

  function setMyUsername(name) { myUsername = name; }
  function getCameraAngle() { return cameraAngle; }
  function rotateCamera(angle) { cameraAngle += angle; draw(); }
  function setCameraAngle(angle) { cameraAngle = angle; draw(); }

  function draw() {
    if (!ctx) return;
    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    ctx.save();
    ctx.translate(canvas.width / 2, canvas.height / 2);
    ctx.rotate(cameraAngle * Math.PI / 180);
    
    var maxDim = Math.max(canvas.width, canvas.height) / TILE + 2;
    var startX = Math.floor((cameraX / TILE) - maxDim);
    var endX = Math.ceil((cameraX / TILE) + maxDim);
    var startY = Math.floor((cameraY / TILE) - maxDim);
    var endY = Math.ceil((cameraY / TILE) + maxDim);

    // Отрисовка тайлов из чанков
    for (var y = startY; y < endY; y++) {
      for (var x = startX; x < endX; x++) {
        var cx = Math.floor(x / CHUNK_SIZE);
        var cy = Math.floor(y / CHUNK_SIZE);
        var chunk = chunks[cx + ',' + cy];
        
        if (chunk) {
            var lx = x - (cx * CHUNK_SIZE);
            var ly = y - (cy * CHUNK_SIZE);
            var tile = chunk[ly][lx];
            
            var px = (x * TILE) - cameraX;
            var py = (y * TILE) - cameraY;

            if (tile === 1) ctx.fillStyle = '#1e3a5f';
            else if (tile === 2) ctx.fillStyle = '#555';
            else if (tile === 0) ctx.fillStyle = '#2d5a27';
            else ctx.fillStyle = '#000'; // Неизвестный чанк
            
            ctx.fillRect(px, py, TILE, TILE);
            if (tile === 0) { ctx.fillStyle = '#3e7a35'; ctx.fillRect(px + 8, py + 8, 4, 4); }
        }
      }
    }

    for (var uname in players) {
      var p = players[uname];
      var px = (p.x * TILE) - cameraX; var py = (p.y * TILE) - cameraY;

      ctx.beginPath();
      ctx.arc(px + TILE/2, py + TILE/2, TILE/2 - 2, 0, Math.PI * 2);
      ctx.fillStyle = (uname === myUsername) ? '#4caf50' : '#f44336';
      ctx.fill(); ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.stroke();

      ctx.save();
      ctx.translate(px + TILE/2, py + TILE/2); ctx.rotate(-cameraAngle * Math.PI / 180); 
      ctx.fillStyle = '#fff'; ctx.font = '12px sans-serif'; ctx.textAlign = 'center';
      ctx.fillText(uname, 0, -TILE/2 - 5);
      ctx.restore();
    }
    ctx.restore(); 
  }

  global.MCGRender2D = { 
      init: init, setChunk: setChunk, setPlayers: setPlayers, 
      updatePlayer: updatePlayer, setMyUsername: setMyUsername,
      resize: resize, rotateCamera: rotateCamera, setCameraAngle: setCameraAngle,
      getCameraAngle: getCameraAngle
  };
})(window);