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
