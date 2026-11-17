// client/js/servers.js
(function (global) {
  'use strict';
  var STORAGE_KEY = 'mcg_servers';

  function getServers() {
    var raw = localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : [];
  }

  function saveServers(list) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(list));
  }

  function addServer(name, url) {
    var list = getServers();
    list.push({ name: name, url: url });
    saveServers(list);
  }

  function removeServer(index) {
    var list = getServers();
    list.splice(index, 1);
    saveServers(list);
  }

  global.MCGServerList = {
    get: getServers,
    add: addServer,
    remove: removeServer
  };
})(window);