# Visual Research & Implementation Plan

## Candidate Evaluation & Comparison

| Candidate | Strategy | Memory Cost | Estimated FPS | Pros/Cons |
|-----------|----------|-------------|---------------|-----------|
| **Digital Rain Animation (0015)** | Direct `drawChar` & custom `fillRect` partial updates | Very low (~2 KB state) | Variable (15-30+) | **Pros**: Off-the-shelf logic. **Cons**: Direct drawing over SPI can cause flicker; tight loop makes it hard to run concurrent complex logic without rewriting. |
| **Full Framebuffer (8-bit sprite)** | Allocate `320x240` 8bpp `TFT_eSprite`, draw all, push once | ~77 KB | ~25 FPS (at 40 MHz SPI) | **Pros**: Zero flicker/tearing. Smooth updates. **Cons**: Memory fragmentation risk on standard ESP32, high SPI bus time cost per frame. |
| **Full Framebuffer (16-bit sprite)** | Allocate `320x240` 16bpp `TFT_eSprite` | ~154 KB | ~12 FPS | **Pros**: True color, zero tearing. **Cons**: Impossible on standard ESP32 w/o PSRAM when paired with Wi-Fi/JSON. |
| **Grid-based Opaque `drawChar`** | Subdivide screen into cells (e.g. 6x8 or 12x16). Track state per cell. Only `drawChar(fg, bg)` when cell changes. | ~1-2 KB (just cell state array) | 30+ FPS | **Pros**: No flicker because we overwrite foreground and background pixels simultaneously. Minimal SPI traffic. **Cons**: Requires building the cell management engine ourselves. |

## First Step Decisions: "The Being" v0.1

1. **Digital Rain Background**: Deferred. `phos_rain_candidate` stays untouched as reference.
2. **Architecture**: We will build the Grid-based Opaque `drawChar` system.
   * **Why**: It avoids the memory risks of full sprites while providing tear-free updates (since `drawChar` with a background color acts as an opaque block).
   * **Sizes**: We will test standard `6x8` pixels (Font 1) leading to a `40x24` grid, and `12x16` (Font 2 or doubled Font 1) leading to a `26x15` grid.
3. **Black Level Check**: Many cheap TFTs suffer from "IPS glow" or TN panel washout. We will test vendor-specific ILI9341 initialization registers (`0xC5`, `0xC7`, `0xB1` and custom gamma) vs standard `TFT_eSPI` registers, alongside PWM backlight stepping (100% / 60% / 30%) to calibrate the hardware before tuning the palette.

## Diff: ILI9341V Vendor Init vs TFT_eSPI
* `0xC5` (VCOM Control 1): Vendor uses `0x44, 0x30`. TFT_eSPI uses `0x3E, 0x28`.
* `0xC7` (VCOM Control 2): Vendor uses `0xB6`. TFT_eSPI uses `0x86`.
* `0xB1` (Frame Rate Control): Vendor uses `0x00, 0x1A` (70Hz). TFT_eSPI uses `0x00, 0x13` (100Hz default) or `0x1B`.
* `0xE0/0xE1` (Gamma): Vendor uses completely custom curves.

## Implementation Order
1. **Black Level Check** (In Progress)
2. **Cell Grid & Opaque Rendering Engine**
3. **Organic Math & Mutation Logic**
4. **Visual Tuning (Size & Color)**
