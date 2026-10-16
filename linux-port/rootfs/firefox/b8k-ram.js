// One content process. This tablet has 1 GB of RAM.
pref("dom.ipc.processCount", 1);
pref("dom.ipc.processCount.webIsolated", 1);
pref("fission.autostart", false);
pref("dom.ipc.keepProcessesAlive.web", 0);
// The default Firefox home page. Old tabs are not restored.
pref("browser.sessionstore.resume_from_crash", false);
pref("browser.startup.page", 1);
// Unload background tabs when memory runs low instead of letting the
// system run out. Linux defaults: off, at 5% free, tabs idle 10 minutes.
pref("browser.tabs.unloadOnLowMemory", true);
pref("browser.low_commit_space_threshold_percent", 10);
pref("browser.tabs.min_inactive_duration_before_unload", 60000);
// Battery: no video starts by itself (sites such as the weather page play
// clips), and pages are asked for less motion, which also stops many
// CSS animations from redrawing the window all the time.
pref("media.autoplay.default", 5);
pref("ui.prefersReducedMotion", 1);
// No screen reader here; otherwise Firefox starts the at-spi bus itself.
pref("accessibility.force_disabled", 1);
// Light pages, as the rest of the content: the home page and new tabs
// followed a dark system theme and came out dark.
pref("layout.css.prefers-color-scheme.content-override", 1);
pref("browser.theme.content-theme", 1);
pref("browser.theme.toolbar-theme", 1);
