# Phos

Phos is a living desktop assistant built around an LCDWiki E32R28T
(ESP32-32E) board. Its visual body is a small ILI9341 display with resistive
touch, while its future assistant capabilities will connect the device to a
Mac through a safe, explicitly allowlisted bridge.

## Hardware

- LCDWiki E32R28T (ESP32-32E)
- ESP32-WROOM-32E, 4 MB flash, no PSRAM
- 2.8-inch ILI9341V display, 240x320
- XPT2046 resistive touch controller
- CH340 USB serial interface

## Credits

Phos is based on
[ESP32-PC-Control-Deck](https://github.com/lepczynski-cloud/ESP32-PC-Control-Deck)
by lepczynski-cloud, released under the MIT License. The original license is
preserved with the reused project files.
