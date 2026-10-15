#!/usr/bin/env python3
"""Write Firefox's policies for the tablet: mobile-config-firefox's, changed.

    b8k-policies.py SRC DST

SRC is the policies.json of mobile-config-firefox
(/usr/lib/firefox/distribution/policies.json), DST is
/etc/firefox/policies/policies.json, which Firefox reads in its place and
which a package upgrade leaves alone.

The one change: uBlock Origin Lite instead of uBlock Origin. uBlock Origin
compiles its filter lists in its own process now and then (after a list or
Firefox update, or when it was stopped before saving them): 270-340 MB on
this 1 GB tablet, which once filled the swap and froze the session.
uBlock Origin Lite ships its rules precompiled for the browser to apply,
and its process stays near 37 MB. Its filtering mode is set to complete
(cosmetic filtering included) through its managed settings.
"""
import json
import sys

UBO = "uBlock0@raymondhill.net"
UBOL = "uBOLiteRedux@raymondhill.net"

src, dst = sys.argv[1], sys.argv[2]
with open(src) as fh:
    data = json.load(fh)
policies = data["policies"]
settings = policies.setdefault("ExtensionSettings", {})
settings[UBO] = {"installation_mode": "blocked"}
settings[UBOL] = {
    "installation_mode": "normal_installed",
    "install_url": "https://addons.mozilla.org/firefox/downloads/latest/ublock-origin-lite/latest.xpi",
    "default_area": "menupanel",
}
policies.setdefault("3rdparty", {}).setdefault("Extensions", {})[UBOL] = {
    "defaultFiltering": "complete",
}
with open(dst, "w") as fh:
    json.dump(data, fh, indent=4, ensure_ascii=False)
    fh.write("\n")
