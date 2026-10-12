// ESP32-S3 硬件侦察固件
//
// 目的：不依赖任何资料，纯靠烧录程序把这块板上到底接了什么挖出来。
//
//   1) GPIO 电平体检 —— 接了外设的引脚会表现出「外部上拉/被驱动」，浮空的脚则是悬空
//   2) 尝试读常见音频/显示芯片的 ID 寄存器（ES8311 的 0xFD 应该返回 0x83）
//   3) 尝试 SPI 屏的 RDDID（ST7789/ILI9341 会返回自家 ID）
//
// 串口命令：
//   G  重扫 GPIO 状态
//   A  尝试读音频 codec 芯片 ID
//   L  尝试读 SPI 屏 ID
//   R  全部跑一遍
//   ?  帮助

#include <Arduino.h>
#include <SPI.h>

// ---------------------------------------------------------------- 软件 I2C
static inline void busLow(gpio_num_t p)
{
  gpio_set_direction(p, GPIO_MODE_OUTPUT);
  gpio_set_level(p, 0);
}
static inline void busRelease(gpio_num_t p)
{
  gpio_pullup_en(p);
  gpio_set_direction(p, GPIO_MODE_INPUT);
}
static inline bool busRead(gpio_num_t p) { return gpio_get_level(p) != 0; }

static void i2cStart(gpio_num_t sda, gpio_num_t scl)
{
  busRelease(sda); busRelease(scl); delayMicroseconds(10);
  busLow(sda); delayMicroseconds(5);
  busLow(scl); delayMicroseconds(5);
}
static void i2cStop(gpio_num_t sda, gpio_num_t scl)
{
  busLow(sda); delayMicroseconds(3);
  busRelease(scl); delayMicroseconds(3);
  busRelease(sda); delayMicroseconds(5);
}
static bool i2cWriteByte(gpio_num_t sda, gpio_num_t scl, uint8_t b)
{
  for (int i = 7; i >= 0; i--)
  {
    if ((b >> i) & 1) busRelease(sda); else busLow(sda);
    delayMicroseconds(3);
    busRelease(scl); delayMicroseconds(3);
    busLow(scl);     delayMicroseconds(3);
  }
  busRelease(sda); delayMicroseconds(3);
  busRelease(scl); delayMicroseconds(4);
  const bool ack = !busRead(sda);
  busLow(scl);     delayMicroseconds(3);
  return ack;
}
static uint8_t i2cReadByte(gpio_num_t sda, gpio_num_t scl, bool nack)
{
  uint8_t v = 0;
  busRelease(sda);
  for (int i = 7; i >= 0; i--)
  {
    delayMicroseconds(3);
    busRelease(scl); delayMicroseconds(3);
    if (busRead(sda)) v |= (1 << i);
    busLow(scl);     delayMicroseconds(3);
  }
  if (nack) busRelease(sda); else busLow(sda);
  delayMicroseconds(3);
  busRelease(scl); delayMicroseconds(3);
  busLow(scl);     delayMicroseconds(3);
  return v;
}

// 读一个 8 位寄存器：写 addr+W，写 reg，restart，写 addr+R，读 1 字节
static int i2cReadReg(uint8_t sdaPin, uint8_t sclPin, uint8_t dev, uint8_t reg)
{
  const gpio_num_t sda = (gpio_num_t)sdaPin, scl = (gpio_num_t)sclPin;
  gpio_reset_pin(sda); gpio_reset_pin(scl);
  busRelease(sda); busRelease(scl); delayMicroseconds(20);

  i2cStart(sda, scl);
  if (!i2cWriteByte(sda, scl, (dev << 1) | 0)) { i2cStop(sda, scl); return -1; }
  if (!i2cWriteByte(sda, scl, reg))            { i2cStop(sda, scl); return -1; }
  i2cStop(sda, scl);
  delayMicroseconds(10);
  i2cStart(sda, scl);
  if (!i2cWriteByte(sda, scl, (dev << 1) | 1)) { i2cStop(sda, scl); return -1; }
  uint8_t v = i2cReadByte(sda, scl, true);
  i2cStop(sda, scl);
  return v;
}

// ---------------------------------------------------------------- GPIO 体检
static void scanGPIO()
{
  Serial.println("\n===== GPIO STATE SCAN =====");
  Serial.println("FLOATING = 没接东西 / EXT-UP = 有上拉(常是 CS,RST,SDA,SCL) / LOW = 被拉低");

  const int pins[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,
                      21,35,36,37,38,39,40,41,42,45,46,47,48};
  for (int p : pins)
  {
    gpio_reset_pin((gpio_num_t)p);
    pinMode(p, INPUT_PULLDOWN); delay(2); const int pd = digitalRead(p);
    pinMode(p, INPUT_PULLUP);   delay(2); const int pu = digitalRead(p);

    const char* st;
    if (!pu && pd) st = "FLOATING";
    else if (pu && pd) st = "EXT PULL-UP  <-- something is here";
    else if (!pu && !pd) st = "HELD LOW     <-- driven or pull-down";
    else st = "odd";

    Serial.printf("GPIO%-3d  pulldown=%d  pullup=%d   %s\n", p, pd, pu, st);
  }
  Serial.println("[gpio] done\n");
}

// ---------------------------------------------------------------- 音频芯片 ID
static void scanAudioCodec()
{
  Serial.println("\n===== AUDIO CODEC ID PROBE =====");
  Serial.println("ES8311: reg 0xFD should read 0x83");

  const struct { uint8_t sda, scl; } pairs[] = {
    {8,9},{8,18},{18,8},{17,18},{18,17},{10,11},{11,10},{47,48},{48,47},
    {38,39},{41,40},{1,2},{4,5},{6,7},{12,13},{14,15},{15,16},{45,46},{39,38}
  };
  const uint8_t devs[] = {0x18, 0x10, 0x1A, 0x40, 0x41};

  bool hit = false;
  for (auto &p : pairs)
  {
    for (uint8_t d : devs)
    {
      for (uint8_t reg : {(uint8_t)0xFD, (uint8_t)0x00})
      {
        int v = i2cReadReg(p.sda, p.scl, d, reg);
        if (v >= 0 && v != 0x00 && v != 0xFF)
        {
          Serial.printf("[codec] SDA=%2d SCL=%2d dev=0x%02X reg=0x%02X -> 0x%02X  HIT\n",
                        p.sda, p.scl, d, reg, v);
          hit = true;
        }
      }
    }
  }
  if (!hit) Serial.println("[codec] no codec answered -> no ES8311/ES7210 on board");
  Serial.println("[codec] done\n");
}

// ---------------------------------------------------------------- SPI 屏 ID
static uint32_t readLcdId(int8_t cs, int8_t dc, int8_t rst, int8_t sck, int8_t mosi, int8_t miso)
{
  SPIClass spi(HSPI);
  spi.begin(sck, miso, mosi, cs);
  pinMode(cs, OUTPUT);  pinMode(dc, OUTPUT);
  if (rst >= 0) { pinMode(rst, OUTPUT); digitalWrite(rst, LOW); delay(20); digitalWrite(rst, HIGH); delay(120); }

  spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  digitalWrite(dc, LOW);  spi.transfer(0x04);          // RDDID
  digitalWrite(dc, HIGH);
  uint8_t a = spi.transfer(0x00), b = spi.transfer(0x00), c = spi.transfer(0x00);
  digitalWrite(cs, HIGH);
  spi.endTransaction();
  spi.end();
  return ((uint32_t)a << 16) | ((uint32_t)b << 8) | c;
}

static void scanLcd()
{
  Serial.println("\n===== SPI LCD ID PROBE =====");
  Serial.println("expect ST7789-ish 0x858552 / ILI9341 0x009341");

  const struct { int8_t cs, dc, rst, sck, mosi; } cfgs[] = {
    { 8, 9,10,12,11}, {15,18,17,12,11}, {13,18,17,12,11}, {46,11,12, 9,10},
    { 3, 7, 9, 4, 5}, {10,11,12,13,14}, { 5, 6, 7, 8, 9}, { 9,10,11,12,13},
    { 4, 5, 6, 7, 8}, {14,15,16,17,18}, {21,47,48,45,46}, { 6, 7, 8, 9,10},
  };

  bool hit = false;
  for (auto &c : cfgs)
  {
    uint32_t id = readLcdId(c.cs, c.dc, c.rst, c.sck, c.mosi, -1);
    if (id != 0 && id != 0xFFFFFF)
    {
      Serial.printf("[lcd] CS=%d DC=%d RST=%d SCK=%d MOSI=%d -> ID=0x%06X  HIT\n",
                    c.cs, c.dc, c.rst, c.sck, c.mosi, id);
      hit = true;
    }
  }
  if (!hit) Serial.println("[lcd] no LCD answered (pins may differ, or screen is I2C/RGB)");
  Serial.println("[lcd] done\n");
}

static void printHelp()
{
  Serial.println("\n----- PROBE commands -----");
  Serial.println("  G  scan GPIO state (find pins with something attached)");
  Serial.println("  A  probe audio codec chip ID (ES8311/ES7210)");
  Serial.println("  L  probe SPI LCD ID (ST7789/ILI9341)");
  Serial.println("  R  run all");
  Serial.println("  ?  help");
  Serial.println("--------------------------\n");
}

void setup()
{
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n\n===== S3 HARDWARE PROBE =====");
  Serial.printf("chip: %s rev%d | flash %uMB | psram %uMB\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getFlashChipSize()/(1024*1024), ESP.getPsramSize()/(1024*1024));
  printHelp();
  scanGPIO();
  scanAudioCodec();
  scanLcd();
  Serial.println("[sys] ready");
}

void loop()
{
  delay(200);
  if (!Serial.available()) return;
  String s = Serial.readStringUntil('\n');
  s.trim();
  if (!s.length()) return;
  Serial.printf("[echo] %s\n", s.c_str());
  if      (s.equalsIgnoreCase("G")) scanGPIO();
  else if (s.equalsIgnoreCase("A")) scanAudioCodec();
  else if (s.equalsIgnoreCase("L")) scanLcd();
  else if (s.equalsIgnoreCase("?") || s.equalsIgnoreCase("H")) printHelp();
  else if (s.equalsIgnoreCase("R")) { scanGPIO(); scanAudioCodec(); scanLcd(); }
}
