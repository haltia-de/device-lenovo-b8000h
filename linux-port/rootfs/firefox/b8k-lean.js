// Lighter Firefox for 1 GB of RAM: no spare process, no rendered Back
// pages, no speculative connections, none of the background features
// (AI, translations, Pocket, studies, sponsored tiles). Measured on
// fontanka.ru: within the run-to-run spread, kept because it costs nothing.
// (Running extensions in the main process, extensions.webextensions.remote,
// is no longer honoured, so it is not set.)
// No spare content process kept ready.
pref("dom.ipc.processPrelaunch.enabled", false);
// No pages kept rendered for Back.
pref("browser.sessionhistory.max_total_viewers", 0);
// No guessing ahead.
pref("network.prefetch-next", false);
pref("network.dns.disablePrefetch", true);
pref("network.http.speculative-parallel-limit", 0);
pref("browser.urlbar.speculativeConnect.enabled", false);
pref("browser.places.speculativeConnect.enabled", false);
// Background features this tablet does not use.
pref("browser.ml.enable", false);
pref("browser.ml.chat.enabled", false);
pref("browser.translations.enable", false);
pref("extensions.pocket.enabled", false);
pref("browser.newtabpage.activity-stream.feeds.topsites", false);
pref("browser.newtabpage.activity-stream.feeds.section.topstories", false);
pref("browser.newtabpage.activity-stream.feeds.system.topstories", false);
pref("browser.newtabpage.activity-stream.feeds.snippets", false);
pref("browser.newtabpage.activity-stream.showSponsored", false);
pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false);
pref("browser.newtabpage.activity-stream.feeds.weatherfeed", false);
pref("browser.contentblocking.report.lockwise.enabled", false);
pref("app.normandy.enabled", false);
pref("app.shield.optoutstudies.enabled", false);
pref("browser.discovery.enabled", false);
pref("extensions.getAddons.cache.enabled", false);
pref("extensions.htmlaboutaddons.recommendations.enabled", false);
