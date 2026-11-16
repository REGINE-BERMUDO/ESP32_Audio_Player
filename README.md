# Description

# Pin(s) Diagram

<div align="center">

#### SD-CARD
| Pin Name           | Pin Number |
| :----------------: | :--------: |
| PIN_NUM_MOSI       | 23         |
| PIN_NUM_MISO       | 19         |
| PIN_NUM_CLK or SCK | 18         |
| PIN_NUM_CS         | 5          |

#### AMPLIFIER
| Pin Name         | Pin Number        |
| :--------------: | :---------------: |
| LRC(LRCLK or WS) | 25                |
| BCLK             | 27                |
| DIN              | 26                |
| GND              | GND (ESP32)       |
| VIN              | 5V or Vin (ESP32) |

</div>

# References
#### SD-Card SPI Implementation
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/fatfs.html

#### MP3 Decoder
- https://github.com/lieff/minimp3
