# Display Configuration

Confirmed for the LCDWiki E32R28T (ESP32-32E) with the HSD028309 panel:

- Driver: ILI9341V, configured as `ILI9341`
- Resolution: 240x320 native, rendered at 320x240 landscape
- SPI bus: HSPI
- TFT pins: `CS=15`, `DC=2`, `SCK=14`, `MOSI=13`, `MISO=12`, `RST=-1`
- Backlight: `BL=21`, active high
- Rotation: `setRotation(1)`
- SPI frequency: 16 MHz
- Color order: RGB is correct; no BGR override
- Inversion: no inversion override

The color diagnostic confirmed the red, green, and blue channel order. The
dark slate appearance of black is normal for this TN panel at an angle.
