#ifdef RUN_BEING
#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

constexpr int BACKLIGHT_PIN = 21;
constexpr int PWM_CHANNEL = 0;
constexpr int PWM_FREQ = 5000;
constexpr int PWM_RES = 8;

void applyVendorInit() {
  tft.writecommand(0xEF);
  tft.writedata(0x03);
  tft.writedata(0x80);
  tft.writedata(0x02);

  tft.writecommand(0xCF);
  tft.writedata(0x00);
  tft.writedata(0xC1);
  tft.writedata(0x30);

  tft.writecommand(0xED);
  tft.writedata(0x64);
  tft.writedata(0x03);
  tft.writedata(0x12);
  tft.writedata(0x81);

  tft.writecommand(0xE8);
  tft.writedata(0x85);
  tft.writedata(0x00);
  tft.writedata(0x78);

  tft.writecommand(0xCB);
  tft.writedata(0x39);
  tft.writedata(0x2C);
  tft.writedata(0x00);
  tft.writedata(0x34);
  tft.writedata(0x02);

  tft.writecommand(0xF7);
  tft.writedata(0x20);

  tft.writecommand(0xEA);
  tft.writedata(0x00);
  tft.writedata(0x00);

  tft.writecommand(ILI9341_PWCTR1);    //Power control
  tft.writedata(0x23);   //VRH[5:0]

  tft.writecommand(ILI9341_PWCTR2);    //Power control
  tft.writedata(0x10);   //SAP[2:0];BT[3:0]

  // VENDOR DIFF: 0xC5
  tft.writecommand(ILI9341_VMCTR1);    //VCM control
  tft.writedata(0x44); // TFT_eSPI is 0x3E
  tft.writedata(0x30); // TFT_eSPI is 0x28

  // VENDOR DIFF: 0xC7
  tft.writecommand(ILI9341_VMCTR2);    //VCM control2
  tft.writedata(0xB6); // TFT_eSPI is 0x86

  tft.writecommand(ILI9341_MADCTL);    // Memory Access Control
  tft.writedata(TFT_MAD_MX | TFT_MAD_COLOR_ORDER); // Rotation 0 (portrait mode)

  tft.writecommand(ILI9341_PIXFMT);
  tft.writedata(0x55);

  // VENDOR DIFF: 0xB1
  tft.writecommand(ILI9341_FRMCTR1);
  tft.writedata(0x00);
  tft.writedata(0x1A); // TFT_eSPI is 0x13

  tft.writecommand(ILI9341_DFUNCTR);    // Display Function Control
  tft.writedata(0x08);
  tft.writedata(0x82);
  tft.writedata(0x27);

  tft.writecommand(0xF2);    // 3Gamma Function Disable
  tft.writedata(0x00);

  tft.writecommand(ILI9341_GAMMASET);    //Gamma curve selected
  tft.writedata(0x01);

  // VENDOR GAMMA
  tft.writecommand(ILI9341_GMCTRP1);    //Set Gamma
  tft.writedata(0x0F);
  tft.writedata(0x31);
  tft.writedata(0x2B);
  tft.writedata(0x0C);
  tft.writedata(0x0E);
  tft.writedata(0x08);
  tft.writedata(0x4E);
  tft.writedata(0xF1);
  tft.writedata(0x37);
  tft.writedata(0x07);
  tft.writedata(0x10);
  tft.writedata(0x03);
  tft.writedata(0x0E);
  tft.writedata(0x09);
  tft.writedata(0x00);

  tft.writecommand(ILI9341_GMCTRN1);    //Set Gamma
  tft.writedata(0x00);
  tft.writedata(0x0E);
  tft.writedata(0x14);
  tft.writedata(0x03);
  tft.writedata(0x11);
  tft.writedata(0x07);
  tft.writedata(0x31);
  tft.writedata(0xC1);
  tft.writedata(0x48);
  tft.writedata(0x08);
  tft.writedata(0x0F);
  tft.writedata(0x0C);
  tft.writedata(0x31);
  tft.writedata(0x36);
  tft.writedata(0x0F);

  tft.writecommand(ILI9341_SLPOUT);    //Exit Sleep
  delay(120);
  tft.writecommand(ILI9341_DISPON);    //Display on
}

void setup() {
  Serial.begin(115200);

  // Use LEDC for PWM backlight on GPIO21
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
  
  tft.init();

#ifdef USE_VENDOR_INIT
  applyVendorInit();
#endif

  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("BLACK LEVEL TEST", 10, 10, 2);
#ifdef USE_VENDOR_INIT
  tft.drawString("Vendor Init ON", 10, 50, 2);
#else
  tft.drawString("TFT_eSPI Init", 10, 50, 2);
#endif
}

void loop() {
  static uint32_t lastChange = 0;
  static int state = 0;
  uint32_t now = millis();

  if (now - lastChange > 3000) {
    lastChange = now;
    state = (state + 1) % 3;
    if (state == 0) {
      ledcWrite(PWM_CHANNEL, 255); // 100%
      tft.fillRect(10, 30, 200, 20, TFT_BLACK);
      tft.drawString("100% Backlight", 10, 30, 2);
    } else if (state == 1) {
      ledcWrite(PWM_CHANNEL, 153); // 60%
      tft.fillRect(10, 30, 200, 20, TFT_BLACK);
      tft.drawString("60% Backlight", 10, 30, 2);
    } else {
      ledcWrite(PWM_CHANNEL, 76);  // 30%
      tft.fillRect(10, 30, 200, 20, TFT_BLACK);
      tft.drawString("30% Backlight", 10, 30, 2);
    }
  }
}
#endif
