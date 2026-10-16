// Opens the web apps of apps.conf in chromeless browser windows.
// A title prefix lets the bar tell these windows from the normal browser.
(function () {
  if (globalThis.__b8kWebapp) {
    return;
  }
  globalThis.__b8kWebapp = true;

  var Cc = Components.classes;
  var Ci = Components.interfaces;
  // The web apps are the apps.conf sections with a url and a host, read
  // once when Firefox starts. The window title is U+2060 + label, which is
  // how b8k-focus tells the window apart.
  var APPS = "/usr/local/share/b8k/apps.conf";
  try {
    // The session's copy written by b8k-apps, with one's own sites.
    var run = Cc["@mozilla.org/process/environment;1"]
      .getService(Ci.nsIEnvironment)
      .get("XDG_RUNTIME_DIR");
    if (run) {
      var f = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
      f.initWithPath(run + "/b8k/apps.conf");
      if (f.exists()) {
        APPS = f.path;
      }
    }
  } catch (e) {}
  var ALLOW = {}; // page -> title prefix
  // Each web app may show its own site and its subdomains. A page on any
  // other host moves to a normal window with an address bar, so it cannot
  // pose under the app's name in a window that hides the address.
  var HOSTS = {}; // title prefix -> site
  var ORIGINS = {}; // "https://host/" of the page -> title prefix
  // Per-user runtime directory, the same one b8k-launch writes to. /tmp is
  // shared, so another user could plant a request there.
  var env = Cc["@mozilla.org/process/environment;1"].getService(Ci.nsIEnvironment);
  var RUN = env.get("XDG_RUNTIME_DIR") || env.get("HOME") + "/.cache";
  var DIR = RUN + "/b8k-webapp.d";
  var LOG = RUN + "/b8k-webapp.log";
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

  // A window that has finished starting. Right after a cold start the
  // first window exists before it can open a chromeless one: asked too
  // early, Firefox put the page into that normal window, address bar,
  // tabs and an on-screen keyboard for the focused address included.
  function ready(win) {
    try {
      return !!(win.gBrowserInit && win.gBrowserInit.delayedStartupFinished);
    } catch (e) {
      return false;
    }
  }

  function openerWindow() {
    var wins = browserWindows().filter(ready);
    for (var i = 0; i < wins.length; i++) {
      try {
        if (wins[i].toolbar && wins[i].toolbar.visible) {
          return wins[i];
        }
      } catch (e) {}
    }
    return wins.length ? wins[0] : null;
  }

  function currentSpec(win) {
    try {
      return String(win.gBrowser.selectedBrowser.currentURI.spec || "");
    } catch (e) {
      return "";
    }
  }

  function onSite(spec, prefix) {
    var base = HOSTS[prefix];
    if (!base) {
      return false;
    }
    var host = "";
    try {
      host = Cc["@mozilla.org/network/io-service;1"]
        .getService(Ci.nsIIOService)
        .newURI(spec).host;
    } catch (e) {
      return false;
    }
    host = String(host || "").toLowerCase();
    return host === base || host.slice(-(base.length + 1)) === "." + base;
  }

  // Move a foreign page out of the app window: open it where the address
  // is visible, then step the app window back, or close it.
  function moveOut(win, spec) {
    if (win.__b8kMoved === spec) {
      return;
    }
    win.__b8kMoved = spec;
    log("move-out " + spec);
    var helper = ChromeUtils.importESModule(
      "resource:///modules/URILoadingHelper.sys.mjs"
    );
    var target = null;
    var wins = browserWindows();
    for (var i = 0; i < wins.length; i++) {
      if (wins[i] !== win && !chromeless(wins[i])) {
        target = wins[i];
        break;
      }
    }
    if (target) {
      helper.URILoadingHelper.openWebLinkIn(target, spec, "tab");
    } else {
      helper.URILoadingHelper.openWebLinkIn(win, spec, "window");
    }
    try {
      if (win.gBrowser.canGoBack) {
        win.gBrowser.goBack();
        return;
      }
    } catch (e) {}
    try {
      win.close();
    } catch (e2) {}
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
        // The app's name only while the window shows the app's own site.
        if (!onSite(currentSpec(win), prefix)) {
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
    // A fresh window has its address bar focused, which keeps the
    // on-screen keyboard up: Firefox does not drop that text field when
    // another of its windows comes to the front, so the keyboard stayed over
    // the web app. Move the focus into the page first.
    try {
      opener.gURLBar.blur();
      opener.gBrowser.selectedBrowser.focus();
    } catch (e) {}
    var helper = ChromeUtils.importESModule(
      "resource:///modules/URILoadingHelper.sys.mjs"
    );
    helper.URILoadingHelper.openWebLinkIn(opener, url, "chromeless", {
      width: 1160,
      height: 800,
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
    for (var origin in ORIGINS) {
      if (spec.indexOf(origin) === 0) {
        return ORIGINS[origin];
      }
    }
    return "";
  }

  function loadApps() {
    var text = "";
    try {
      // readText returns the raw bytes; the labels are UTF-8.
      text = decodeURIComponent(escape(readText(APPS)));
    } catch (e) {
      log("apps " + e);
      return;
    }
    var apps = [];
    var cur = null;
    var lines = text.split("\n");
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].replace(/^\s+|\s+$/g, "");
      if (!line || line[0] === "#" || line[0] === ";") {
        continue;
      }
      var head = /^\[(.+)\]$/.exec(line);
      if (head) {
        cur = { id: head[1].replace(/^\s+|\s+$/g, "") };
        apps.push(cur);
        continue;
      }
      var eq = line.indexOf("=");
      if (cur && eq > 0) {
        var key = line.slice(0, eq).replace(/\s+$/, "");
        cur[key] = line.slice(eq + 1).replace(/^\s+/, "");
      }
    }
    for (var j = 0; j < apps.length; j++) {
      var app = apps[j];
      var origin = /^https:\/\/[^\/]+\//.exec(app.url || "");
      if (!origin || !app.host) {
        continue;
      }
      var prefix = "\u2060" + (app.label || app.id);
      ALLOW[app.url] = prefix;
      HOSTS[prefix] = app.host.toLowerCase();
      ORIGINS[origin[0]] = prefix;
      log("app " + app.id + " " + app.url);
    }
  }

  function retitle() {
    var wins = browserWindows();
    for (var i = 0; i < wins.length; i++) {
      if (!chromeless(wins[i])) {
        continue;
      }
      var spec = currentSpec(wins[i]);
      var prefix = "";
      try {
        prefix = wins[i].__b8kTitle || "";
      } catch (e) {}
      prefix = prefix || prefixFor(spec);
      if (!prefix) {
        continue;
      }
      if (/^https?:/.test(spec) && !onSite(spec, prefix)) {
        moveOut(wins[i], spec);
        continue;
      }
      lockTitle(wins[i], prefix);
    }
  }

  // A window that opens on the home page puts the cursor in the address
  // bar, and the on-screen keyboard came up over half the screen though
  // nobody had touched a text field. Once per window, take the focus off
  // it; a tap on the address bar brings the keyboard as usual.
  function calmStart() {
    var wins = browserWindows().filter(ready);
    for (var i = 0; i < wins.length; i++) {
      try {
        if (wins[i].__b8kCalm || chromeless(wins[i])) {
          continue;
        }
        wins[i].__b8kCalm = true;
        if (/^about:(home|newtab|blank)/.test(currentSpec(wins[i]))) {
          wins[i].gURLBar.blur();
          wins[i].gBrowser.selectedBrowser.focus();
        }
      } catch (e) {}
    }
  }

  // b8k-apps rewrites the session's apps.conf when one adds a site; read
  // it again then, so a running Firefox knows the new site.
  var appsStamp = 0;
  function rereadApps() {
    var f = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
    f.initWithPath(APPS);
    var stamp = f.exists() ? f.lastModifiedTime : 0;
    if (stamp && stamp !== appsStamp) {
      appsStamp = stamp;
      loadApps();
    }
  }

  function poll() {
    try {
      retitle();
      calmStart();
      rereadApps();
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

  rereadApps();
  var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  var callback = { notify: poll };
  arms.push(timer);
  arms.push(callback);
  globalThis.__b8kWebappTimer = timer;
  timer.initWithCallback(callback, 400, Ci.nsITimer.TYPE_REPEATING_SLACK);
  log("ready");
})();
