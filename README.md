WiFi.MSC – A USB device to securely access storage over SSH
===========================================================

Summary
-------
This is firmware that can be configured and loaded onto an Espressif
ESP32-S2 or ESP32-S3 to create a USB mass storage device, whose data
is accessed over WiFi via SSH using key authentication.
It is based on the LibSSH-ESP32 library.

Author
------
Created by Ewan Parker https://www.ewan.cc/ on 16th September 2023,
and initially released on 19th September 2023.

Use Cases
---------
A small USB bus powered device that offers up to 8 TB of storage.
Simple alternative to iSCSI over USB and WiFi.
Shared access to USB storage (read-only or filesystem dependent).
Remote adhoc access to administered storage.
Flexible boot media.
NAS on-a-stick.
Cloud storage accessed from anywhere over a cellular WiFi hotspot.

Limitations
-----------
Slower than local storage.  Cached data transfer speed reaches 980 kB/s
on the ESP32-S3 and 580 kB/s and higher for the ESP32-S2.
Network speed measured to be about 61 kB/s on an ESP32-S3 and slightly
slower on an ESP32-S2.
No error checking implemented (yet).  Needs WiFi and SSH access when
started and without interruption.
Single storage profile hard-coded into device firmware.  To change the
profile the SPIFFS (or entire flash) must be wiped and the firmware
rebuilt and re-flashed.
No status/activity feedback or user interface.
No Ethernet support.
Memory is tight on the ESP32-S2.

Configuration
-------------
Run the ```config/create_config.sh``` script.  Leave the passphrase blank.
Edit the files now created in the ```config/data/0``` directory
Re-run the ```config/create_config.sh``` script to fix-up any changes
you made to the configuration.
Two files are created: ```wifimsc_disk_config.h``` and
```wifimsc_ssh_config.h```.
If not already present, copy your SSH key to the remote SSH server, e.g.
using ```ssh-copy-id -i```.
Build and uploaded the firmware using ```arduino-cli``` or
```arduino-ide```.
Enable PSRAM first if you want caching.

Usage
-----
Plug the device into any USB host (e.g. PC).
It will take a few seconds to connect to WiFi and create a session
over SSH.  Then the storage will become available.
The first time the device is started it will create the backing-file
using the SSH credentials on the remove host (defaults to ```/var/tmp```).
The backing-file can also be accessed and administered locally using all
standard tools such as ```mkfs```, ```mount```, ```cp```, etc.
