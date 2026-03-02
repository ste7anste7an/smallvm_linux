#if defined(LMS_ESP32) && defined(ILI9341)
#define ILI9341_DRIVER
//#define ILI9342_DRIVER

// SPI pin configuration

//#define TFT_WIDTH  320
//#define TFT_HEIGHT 240
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS 15
#define TFT_DC 27
#define TFT_RST 32

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

#define TFT_SPI_PORT HSPI 
// Optional: set SPI clock speed
#define CONFIG_TFT_HSPI_PORT
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY 20000000

#define SUPPORT_TRANSACTIONS
#define USE_HSPI_PORT         // Use HSPI (VSPI also works, but this matches the custom pins above)
#define ENABLE_8_BIT_PALETTES
#define TFT_DMA_BUFFER_SIZE  132768
#define TFT_DMA_MODE         1   

#elif defined(LMS_ESP32) && defined(ST7789)
#define ST7789_DRIVER

#define TFT_MOSI 26
#define TFT_MISO -1
#define TFT_SCLK 15
#define TFT_CS 14

#define TFT_DC 27
#define TFT_RST 13

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
//#define TFT_ROTATION 2
#define TFT_SPI_PORT HSPI 
// Optional: set SPI clock speed
#define CONFIG_TFT_HSPI_PORT
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY 20000000

#define TFT_WIDTH 240
#define TFT_HEIGHT 280

// Optional: enable touch or fonts
//#define SUPPORT_TOUCH
//#define LOAD_GLCD
//#define LOAD_FONT2
// swap R and B
#define TFT_RGB_ORDER TFT_RGB

#define SUPPORT_TRANSACTIONS
#define USE_HSPI_PORT         // Use HSPI (VSPI also works, but this matches the custom pins above)
#define ENABLE_8_BIT_PALETTES
#define TFT_DMA_BUFFER_SIZE  132768
#define TFT_DMA_MODE  
#endif