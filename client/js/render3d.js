// SPDX-License-Identifier: AGPL-3.0-or-later
(function (global) {
  'use strict';
  var gl = null, canvas3d = null;

  function init(c) {
    canvas3d = c;
    try {
      gl = c.getContext('webgl') || c.getContext('experimental-webgl');
    } catch (e) { gl = null; }
    if (!gl) return false; // вызывающий код остаётся на 2D
    gl.clearColor(0.05, 0.05, 0.08, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    return true;
  }

  function isAvailable() { return !!gl; }
  global.MCGRender3D = { init: init, isAvailable: isAvailable };
})(window);