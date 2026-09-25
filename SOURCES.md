# Source and artifact provenance

Checked on 2026-09-26.

## Hardware identity

- Lenovo user guide/model table: B8000-H = machine type 60047 / Z0AJ,
  WLAN + 3G data.
  <https://usermanual.wiki/Lenovo/B8000F.User-Guide-2056549.pdf>
- Linux DT binding discussion lists the blade family and identifies B8000-H as
  the 10-inch 3G variant using MT8389.
  <https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2603.2/10734.html>

## Stock firmware

- 4PDA discussion, post 9963, supplies a currently reachable MEGA copy of the
  B8000-H Android 4.4.2 package.
  <https://4pda.to/forum/index.php?showtopic=514193&view=findpost&p=141405713>
- Service-GSM independently lists the same package as 708.14 MB and exposes its
  archive member names. It is useful as corroborating metadata, but downloading
  from that mirror requires a paid account.
  <https://service-gsm.net/Lenovo/Yoga%20Tablet/YOGA%20Tablet%20-%2010inch/B8000/B8000_A442_001_020_140620_WW_3GDATA.zip/>
- Hovatek has an older B8000-H Android 4.2.2 image. Keep it as a fallback only;
  do not mix its scatter or preloader with the 4.4.2 package.
  <https://www.hovatek.com/forum/thread-10450.html>

The downloaded archive passed a full RAR integrity test. Its SHA-256 is
`BBDB37EFAF67451BA961EDA613A701BEA5DEC554A167EF56AEF4791E6F40C0BA`;
the detailed local image manifest is in `docs/firmware-manifest.md`. This hash
documents this exact 4PDA/MEGA artifact rather than assuming that two mirrors
contain byte-identical files.

## Windows preloader driver

- Microsoft Update Catalog entry for `USB\VID_0E8D&PID_2000`, Windows 10 and
  later: update ID `8aec9af1-27cc-4c97-8ed5-9db7afeb499a`.
  <https://www.catalog.update.microsoft.com/Search.aspx?q=VID_0E8D%26PID_2000>
- Downloaded CAB SHA-256, matching Microsoft's catalog metadata:
  `CE72E97C07582AD41DEBFAEEE6F6284ECD28285DF5F9CAA05193653FB236561F`.
- Extracted INF matches `USB\Vid_0e8d&Pid_2000`; catalog signature status is
  valid. The package driver version is `3.0.1504.0` (2015-01-22).

## Linux work

- Active near-mainline kernel fork for MT6589/MT8125/MT8389:
  <https://github.com/TeamYogaBlade2/linux/tree/blade/v7.1>
- Current postmarketOS packaging (marked broken and selecting B8000-F DT):
  <https://github.com/TeamYogaBlade2/lenovo-b8000_postmarketos_files>
- Historical vendor-kernel/device work:
  <https://github.com/TeamYogaBlade/lenovo_b6000-8000_kernel_source>
  <https://github.com/TeamYogaBlade/android_device_lenovo_b8000>
- Initial upstream B8000-F DTS patch uses bootloader-initialized
  simple-framebuffer only. It is evidence of early boot/display support, not a
  complete tablet port:
  <https://lkml.rescloud.iu.edu/hypermail/linux/kernel/2603.2/10736.html>
