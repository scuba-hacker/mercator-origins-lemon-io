#pragma once

// see https://github.com/lovyan03/LovyanGFX
#include <LovyanGFX.hpp>

#define OLED_RST_ADA_GREEN_LV 0         // copied from main.cpp for now

#define LGFX_0_BLACK             0x000000  // Level  0 - Black
#define LGFX_1_VERY_DARK_GRAY    0x111111  // Level  1 - Very dark gray
#define LGFX_2_DARK_GRAY         0x222222  // Level  2 - Dark gray
#define LGFX_3_CHARCOAL_GRAY     0x333333  // Level  3 - Charcoal gray
#define LGFX_4_SLATE_GRAY        0x444444  // Level  4 - Slate gray
#define LGFX_5_MEDIUM_DARK_GRAY  0x555555  // Level  5 - Medium dark gray
#define LGFX_6_DIM_GRAY          0x666666  // Level  6 - Dim gray
#define LGFX_7_STANDARD_GRAY     0x777777  // Level  7 - Standard gray
#define LGFX_8_MID_LIGHT_GRAY    0x888888  // Level  8 - Mid-light gray
#define LGFX_9_LIGHT_GRAY        0x999999  // Level  9 - Light gray
#define LGFX_10_PALE_GRAY        0xAAAAAA  // Level 10 - Pale gray
#define LGFX_11_SILVERY_GRAY     0xBBBBBB  // Level 11 - Silvery gray
#define LGFX_12_VERY_LIGHT_GRAY  0xCCCCCC  // Level 12 - Very light gray
#define LGFX_13_NEAR_WHITE       0xDDDDDD  // Level 13 - Near white
#define LGFX_14_ALMOST_WHITE     0xEEEEEE  // Level 14 - Almost white
#define LGFX_15_WHITE            0xFFFFFF  // Level 15 - White

class LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE : public lgfx::LGFX_Device
{
    lgfx::Panel_SSD1327  _panel_instance;
    lgfx::Bus_I2C        _bus_instance;

public:

  LGFX_I2C_Adafruit_SSD1327_128x128_Grey_OLE(void)
  {
    {
      auto cfg = _bus_instance.config();    // バス設定用の構造体を取得します。

      cfg.i2c_port    = 0;
      cfg.freq_write  = 400000;     // OR set both to 1000000 if 400000 works
      cfg.freq_read   = 400000;     
      cfg.pin_sda     = 8;          
      cfg.pin_scl     = 9;          
      cfg.i2c_addr    = 0x3D;       // alternative address is 0x3C if A0 jumper is closed.
      
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { 
      auto cfg = _panel_instance.config();

      cfg.pin_cs           =    -1; 
      cfg.pin_rst          =    OLED_RST_ADA_GREEN_LV; 
      cfg.pin_busy         =    -1; 

      cfg.panel_width      =   128;  // 実際に表示可能な幅
      cfg.panel_height     =   128;  // 実際に表示可能な高さ
      cfg.offset_x         =     0;  // パネルのX方向オフセット量
      cfg.offset_y         =     0;  // パネルのY方向オフセット量
      cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.readable         =  false;  // データ読出しが可能な場合 trueに設定
      cfg.invert           = false;  // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order        = false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit       = false;  // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared       = false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }

};

class LGFX_SPI_Adafruit_SSD1327_128x128_Grey_OLE : public lgfx::LGFX_Device
{
    lgfx::Panel_SSD1327  _panel_instance;
    lgfx::Bus_SPI        _bus_instance;   

public:

  LGFX_SPI_Adafruit_SSD1327_128x128_Grey_OLE(void)
  {
    // ENTIRE FUNCTION NEEDS FIXING UP WITH REAL VALUES
    {
      auto cfg = _bus_instance.config();    // バス設定用の構造体を取得します。

      cfg.spi_host = SPI2_HOST;     // 使用するSPIを選択  ESP32-S2,C3 : SPI2_HOST or SPI3_HOST / ESP32 : VSPI_HOST or HSPI_HOST
      // ※ ESP-IDFバージョンアップに伴い、VSPI_HOST , HSPI_HOSTの記述は非推奨になるため、エラーが出る場合は代わりにSPI2_HOST , SPI3_HOSTを使用してください。
      cfg.spi_mode = 0;             // SPI通信モードを設定 (0 ~ 3)
      cfg.freq_write = 40000000;    // 送信時のSPIクロック (最大80MHz, 80MHzを整数で割った値に丸められます)
      cfg.freq_read  = 16000000;    // 受信時のSPIクロック
      cfg.spi_3wire  = true;        // 受信をMOSIピンで行う場合はtrueを設定
      cfg.use_lock   = true;        // トランザクションロックを使用する場合はtrueを設定
      cfg.dma_channel = SPI_DMA_CH_AUTO; // 使用するDMAチャンネルを設定 (0=DMA不使用 / 1=1ch / 2=ch / SPI_DMA_CH_AUTO=自動設定)
      // ※ ESP-IDFバージョンアップに伴い、DMAチャンネルはSPI_DMA_CH_AUTO(自動設定)が推奨になりました。1ch,2chの指定は非推奨になります。
      cfg.pin_sclk = 18;            // SPIのSCLKピン番号を設定
      cfg.pin_mosi = 23;            // SPIのMOSIピン番号を設定
      cfg.pin_miso = 19;            // SPIのMISOピン番号を設定 (-1 = disable)
      cfg.pin_dc   = 27;            // SPIのD/Cピン番号を設定  (-1 = disable)
     // SDカードと共通のSPIバスを使う場合、MISOは省略せず必ず設定してください。

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { 
      auto cfg = _panel_instance.config();

      cfg.pin_cs           =    -1; 
      cfg.pin_rst          =    OLED_RST_ADA_GREEN_LV; 
      cfg.pin_busy         =    -1; 

      cfg.panel_width      =   128;  // 実際に表示可能な幅
      cfg.panel_height     =   128;  // 実際に表示可能な高さ
      cfg.offset_x         =     0;  // パネルのX方向オフセット量
      cfg.offset_y         =     0;  // パネルのY方向オフセット量
      cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.dummy_read_pixel =     8;  // ピクセル読出し前のダミーリードのビット数
      cfg.dummy_read_bits  =     1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
      cfg.readable         =  true;  // データ読出しが可能な場合 trueに設定
      cfg.invert           = false;  // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order        = false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit       = false;  // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared       = false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};
