# MiniLabo ESP8266 Firmware

This repository contains the firmware for the MiniLabo ESP8266 board. The
project is built with PlatformIO and targets the `nodemcuv2` board using the
Arduino framework.

## Serial console settings

The ESP8266 boot ROM prints diagnostics at **74880 baud**. The firmware keeps the
serial port at the same speed so that the boot loader messages and the
application logs stay aligned.

When using the PlatformIO device monitor (or any other serial terminal) make
sure it is configured for 74880 baud; otherwise the console will show garbled
characters during and after boot which can make debugging networking issues
impossible.

```
pio device monitor -b 74880
```

With the correct baud rate you will see the normal boot output and messages such
as the SoftAP SSID and IP address that confirm the web server and Wi-Fi
services are running.
