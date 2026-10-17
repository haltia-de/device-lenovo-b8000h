#!/usr/bin/env python3
"""Write Firefox's policies for the tablet: mobile-config-firefox's, changed.

    b8k-policies.py SRC DST [XPI [LANGPACK]]

SRC is the policies.json of mobile-config-firefox
(/usr/lib/firefox/distribution/policies.json), DST is
/etc/firefox/policies/policies.json, which Firefox reads in its place and
which a package upgrade leaves alone.

The main change: uBlock Origin Lite instead of uBlock Origin. uBlock Origin
compiles its filter lists in its own process now and then (after a list or
Firefox update, or when it was stopped before saving them): 270-340 MB on
this 1 GB tablet, which once filled the swap and froze the session.
uBlock Origin Lite ships its rules precompiled for the browser to apply,
and its process stays near 37 MB. Its filtering mode is set to complete
(cosmetic filtering included) through its managed settings.

XPI is the path of a signed uBlock Origin Lite on the tablet. Firefox
installs it from there: addons.mozilla.org answers 406 from some networks,
and then the extension never arrived. Without XPI (or with an empty one),
Firefox downloads it.

Firefox asks for Russian menus, then English. Alpine packages no language
packs, so LANGPACK is the path of Mozilla's signed Russian one on the
tablet; without it the menus stay English.
"""
import json
import sys

UBO = "uBlock0@raymondhill.net"
UBOL = "uBOLiteRedux@raymondhill.net"

src, dst = sys.argv[1], sys.argv[2]
xpi = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
langpack = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else None
with open(src) as fh:
    data = json.load(fh)
policies = data["policies"]
settings = policies.setdefault("ExtensionSettings", {})
settings[UBO] = {"installation_mode": "blocked"}
settings[UBOL] = {
    "installation_mode": "normal_installed",
    "install_url": "file://" + xpi if xpi else
    "https://addons.mozilla.org/firefox/downloads/latest/ublock-origin-lite/latest.xpi",
    "default_area": "menupanel",
}
if langpack:
    settings["langpack-ru@firefox.mozilla.org"] = {
        "installation_mode": "force_installed",
        "install_url": "file://" + langpack,
    }
policies["RequestedLocales"] = ["ru", "en-US"]
policies.setdefault("3rdparty", {}).setdefault("Extensions", {})[UBOL] = {
    "defaultFiltering": "complete",
}
with open(dst, "w") as fh:
    json.dump(data, fh, indent=4, ensure_ascii=False)
    fh.write("\n")
