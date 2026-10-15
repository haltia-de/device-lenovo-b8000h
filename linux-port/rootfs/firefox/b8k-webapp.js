// Opens the two allowed sites in a chromeless browser window.
// A title prefix lets the bar tell these windows from the normal browser.
(function () {
  if (globalThis.__b8kWebapp) {
    return;
  }
  globalThis.__b8kWebapp = true;

  var Cc = Components.classes;
  var Ci = Components.interfaces;
  var ALLOW = {
    "https://www.gismeteo.ru/": "\u2060Погода",
    "https://web.max.ru/": "\u2060MAX",
  };
  var DIR = "/tmp/b8k-webapp.d";
  var LOG = "/tmp/b8k-webapp.log";
  var arms = [];

  function log(msg) {
    try {
      var file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      file.initWithPath(LOG);
      var stream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
        Ci.nsIFileOutputStream
      );
      stream.init(file, 0x02 | 0x08 | 0x10, 420, 0);
      var text = String(msg) + "\n";
      var bytes = unescape(encodeURIComponent(text));
      stream.write(bytes, bytes.length);
      stream.close();
    } catch (e) {}
  }

  function readText(path) {
    var file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    file.initWithPath(path);
    if (!file.exists() || file.fileSize <= 0) {
      return "";
    }
    var input = Cc["@mozilla.org/network/file-input-stream;1"].createInstance(
      Ci.nsIFileInputStream
    );
    input.init(file, 0x01, 0, 0);
    var scriptable = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(
      Ci.nsIScriptableInputStream
    );
    scriptable.init(input);
    var data = scriptable.read(file.fileSize);
    scriptable.close();
    input.close();
    return String(data || "").replace(/^\s+|\s+$/g, "");
  }

  function writeText(path, text) {
    var file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    file.initWithPath(path);
    var stream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
      Ci.nsIFileOutputStream
    );
    stream.init(file, 0x02 | 0x08 | 0x20, 420, 0);
    var body = String(text) + "\n";
    var bytes = unescape(encodeURIComponent(body));
    stream.write(bytes, bytes.length);
    stream.close();
  }

  function takeJobs() {
    var dir = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    dir.initWithPath(DIR);
    if (!dir.exists() || !dir.isDirectory()) {
      return [];
    }
    var paths = [];
    var entries = dir.directoryEntries;
    while (entries.hasMoreElements()) {
      var item = entries.getNext().QueryInterface(Ci.nsIFile);
      if (item.isFile()) {
        paths.push(item.path);
      }
    }
    var jobs = [];
    for (var i = 0; i < paths.length; i++) {
      var url = "";
      try {
        url = readText(paths[i]);
      } catch (e) {
        url = "";
      }
      try {
        var gone = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
        gone.initWithPath(paths[i]);
        if (gone.exists()) {
          gone.remove(false);
        }
      } catch (e2) {}
      if (url) {
        jobs.push({ path: paths[i], url: url });
      }
    }
    return jobs;
  }

  function browserWindows() {
    var wm = Cc["@mozilla.org/appshell/window-mediator;1"].getService(
      Ci.nsIWindowMediator
    );
    var found = [];
    var en = wm.getEnumerator("navigator:browser");
    while (en.hasMoreElements()) {
      var win = en.getNext();
      try {
        if (!win.closed) {
          found.push(win);
        }
      } catch (e) {}
    }
    return found;
  }

  function openerWindow() {
    var wins = browserWindows();
    for (var i = 0; i < wins.length; i++) {
      try {
        if (wins[i].toolbar && wins[i].toolbar.visible) {
          return wins[i];
        }
      } catch (e) {}
    }
    return wins.length ? wins[0] : null;
  }

  function lockTitle(win, prefix) {
    try {
      if (win.__b8kTitle) {
        return;
      }
      win.__b8kTitle = prefix;
    } catch (e) {
      return;
    }
    var tick = function () {
      try {
        if (win.closed) {
          return;
        }
        if (String(win.document.title || "") !== prefix) {
          win.document.title = prefix;
        }
      } catch (e2) {}
    };
    tick();
    try {
      win.setInterval(tick, 700);
    } catch (e3) {}
  }

  function watchNew(prefix) {
    var tries = 0;
    var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    var callback = {
      notify: function () {
        tries += 1;
        var wins = browserWindows();
        for (var i = 0; i < wins.length; i++) {
          try {
            if (!wins[i].__b8kSeen) {
              wins[i].__b8kSeen = true;
              lockTitle(wins[i], prefix);
              log("window " + prefix);
              timer.cancel();
              return;
            }
          } catch (e) {}
        }
        if (tries > 40) {
          log("window-timeout " + prefix);
          timer.cancel();
        }
      },
    };
    arms.push(timer);
    arms.push(callback);
    timer.initWithCallback(callback, 300, Ci.nsITimer.TYPE_REPEATING_SLACK);
  }

  function openOne(url) {
    var prefix = ALLOW[url];
    if (!prefix) {
      log("reject " + url);
      return true;
    }
    var opener = openerWindow();
    if (!opener) {
      return false;
    }
    var wins = browserWindows();
    for (var i = 0; i < wins.length; i++) {
      try {
        wins[i].__b8kSeen = true;
      } catch (e) {}
    }
    var helper = ChromeUtils.importESModule(
      "resource:///modules/URILoadingHelper.sys.mjs"
    );
    helper.URILoadingHelper.openWebLinkIn(opener, url, "chromeless", {
      width: 1280,
      height: 704,
    });
    watchNew(prefix);
    log("open " + url);
    return true;
  }

  function chromeless(win) {
    try {
      return !(win.toolbar && win.toolbar.visible);
    } catch (e) {
      return false;
    }
  }

  function prefixFor(spec) {
    spec = String(spec || "");
    if (spec.indexOf("https://www.gismeteo.ru/") === 0) {
      return ALLOW["https://www.gismeteo.ru/"];
    }
    if (spec.indexOf("https://web.max.ru/") === 0) {
      return ALLOW["https://web.max.ru/"];
    }
    return "";
  }

  function retitle() {
    var wins = browserWindows();
    for (var i = 0; i < wins.length; i++) {
      if (!chromeless(wins[i])) {
        continue;
      }
      var spec = "";
      try {
        spec = wins[i].gBrowser.selectedBrowser.currentURI.spec;
      } catch (e) {}
      var prefix = prefixFor(spec);
      if (prefix) {
        lockTitle(wins[i], prefix);
      }
    }
  }

  function poll() {
    try {
      retitle();
    } catch (e) {}
    var jobs = [];
    try {
      jobs = takeJobs();
    } catch (e) {
      log("read " + e);
      return;
    }
    if (!jobs.length) {
      return;
    }
    if (!openerWindow()) {
      for (var i = 0; i < jobs.length; i++) {
        try {
          writeText(jobs[i].path, jobs[i].url);
        } catch (e2) {}
      }
      return;
    }
    for (var j = 0; j < jobs.length; j++) {
      try {
        if (!openOne(jobs[j].url)) {
          writeText(jobs[j].path, jobs[j].url);
        }
      } catch (e3) {
        log("err " + e3);
        try {
          writeText(jobs[j].path, jobs[j].url);
        } catch (e4) {}
      }
    }
  }

  var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  var callback = { notify: poll };
  arms.push(timer);
  arms.push(callback);
  globalThis.__b8kWebappTimer = timer;
  timer.initWithCallback(callback, 400, Ci.nsITimer.TYPE_REPEATING_SLACK);
  log("ready");
})();
