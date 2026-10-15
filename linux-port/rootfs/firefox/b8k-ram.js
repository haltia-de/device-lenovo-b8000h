// One content process. This tablet has 1 GB of RAM.
pref("dom.ipc.processCount", 1);
pref("dom.ipc.processCount.webIsolated", 1);
pref("fission.autostart", false);
pref("dom.ipc.keepProcessesAlive.web", 0);
// The default Firefox home page. Old tabs are not restored.
pref("browser.sessionstore.resume_from_crash", false);
pref("browser.startup.page", 1);
