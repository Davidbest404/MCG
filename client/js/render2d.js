// SPDX-License-Identifier: AGPL-3.0-or-later
(function (global) {
  'use strict';
  var canvas, ctx, tiles = [];
  var TILE = 32;

  function init(c) {
    canvas = c;
    ctx = canvas.getContext('2d');
    resize();
    window.addEventListener('resize', resize);
    generateDemoMap();
    draw();
  }

  function resize() {
    if (!canvas) return;
    canvas.width = canvas.clientWidth || 640;
    canvas.height = canvas.clientHeight || 360;
    draw();
  }

  function generateDemoMap() {
    tiles = [];
    for (var y = 0; y < 20; y++) {
      for (var x = 0; x < 40; x++) {
        // Псевдо-шум: трава/вода/камень по синусу — без «швов», т.к. считаем по мировым координатам
        var n = Math.sin(x * 0.3) + Math.cos(y * 0.4);
        tiles.push({ x: x, y: y, t: n > 0.8 ? 'water' : (n < -0.8 ? 'stone' : 'grass') });
      }
    }
  }

  function draw() {
    if (!ctx) return;
    ctx.fillStyle = '#111';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    for (var i = 0; i < tiles.length; i++) {
      var t = tiles[i];
      var px = t.x * TILE, py = t.y * TILE;
      if (px > canvas.width || py > canvas.height) continue;
      ctx.fillStyle = t.t === 'water' ? '#1e3a5f' : (t.t === 'stone' ? '#555' : '#2d5a27');
      ctx.fillRect(px, py, TILE - 1, TILE - 1);
      if (t.t === 'grass') {
        ctx.fillStyle = '#3e7a35';
        ctx.fillRect(px + 6, py + 6, 4, 4);
      }
    }
  }

  global.MCGRender2D = { init: init, draw: draw };
})(window);