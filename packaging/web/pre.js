// Keep user files in IndexedDB; run even when browser storage is unavailable.
Module.preRun = Module.preRun || [];
Module.arguments = Module.arguments || [];
Module.preRun.push(function () {
  addRunDependency('user-storage');
  FS.mkdirTree('/user');
  FS.mount(IDBFS, {}, '/user');
  FS.syncfs(true, function (error) {
    if (error) console.warn('Persistent storage unavailable:', error);
    FS.mkdirTree('/user/roms');
    if (FS.analyzePath('/gamefile.uze').exists) {
      FS.writeFile('/user/roms/gamefile.uze', FS.readFile('/gamefile.uze'));
      Module.arguments.push('/user/roms/gamefile.uze');
    }
    FS.chdir('/user');
    removeRunDependency('user-storage');
  });
});
Module.flushStorage = function () {
  // A save requested during another flush must include the newer file writes.
  var previous = Module.storageFlush || Promise.resolve();
  var current = previous.catch(function () {}).then(function () {
    return new Promise(function (resolve, reject) {
      FS.syncfs(false, function (error) {
        if (error) { console.warn('Could not save browser storage:', error); reject(error); }
        else resolve();
      });
    });
  });
  Module.storageFlush = current;
  return current.finally(function () {
    if (Module.storageFlush === current) Module.storageFlush = null;
  });
};

// SDL2 caches the last mouse-move position for button events. A page layout
// change can move the canvas beneath a stationary pointer between events.
(function () {
  var dragging = false;
  function refreshPointer(event) {
    var canvas = Module.canvas;
    if (!canvas || document.pointerLockElement === canvas) return;
    if (event.type === 'mousedown') dragging = event.target === canvas;
    if (!dragging && event.target !== canvas) return;
    canvas.dispatchEvent(new MouseEvent('mousemove', {
      clientX: event.clientX, clientY: event.clientY,
      screenX: event.screenX, screenY: event.screenY,
      button: event.button, buttons: event.buttons,
      ctrlKey: event.ctrlKey, shiftKey: event.shiftKey,
      altKey: event.altKey, metaKey: event.metaKey
    }));
    if (event.type === 'mouseup') dragging = false;
  }
  document.addEventListener('mousedown', refreshPointer, true);
  document.addEventListener('mouseup', refreshPointer, true);
})();
