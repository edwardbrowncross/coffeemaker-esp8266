# coffeemaker
An Arduino sketch for an ESP8266, which monitors the flashing LED on the office coffee maker, as well as its weight, and sends appropriate MQTT messages to AWS IOT.

## Flashing the ESP8266

1. Follow the [instructions here](https://github.com/esp8266/Arduino#installing-with-boards-manager) to install the Arduino IDE and ESP8266 libraries.
2. Clone this repository and also clone the libraries listed at the top of coffeemaker.ino to ~/Arduino/libraries.
3. To flash onto the ESP8266 D1 Mini board, connect it to your computer via Micro USB and set up the flashing options in the tools menu as follows:
![Flashing menu](https://i.imgur.com/NtFzFqS.png)
(The board and upload speed being most important)
4. Upload using the upload button (circle with arrow) in the Arduino IDE

## Configuration

When the device starts up, it checks its config and attempt to connect to the wifi. If it cannot connect or some config options are missing, it starts an access point. Connect to this access point and set the required values to proceed.

To force the config access point to start, connect pins labeled D0 and D5 and reset the device. Disconnect the jumpered pins before next restart.

## Debugging

The code outputs debug data via the serial console during operation. To see this, you can use tools > serial monitor in the Arduino IDE and set the speed to 115200.
You can also use any other serial port coms tool your OS supports.

If the MQTT client is connected to AWS IOT, MQTT messages can be seen arriving in the AWS console.

## Extending the hardware

The hardware currently runs on a [WeMos D1 Mini](https://www.banggood.com/WeMos-D1-mini-V2_2_0-WIFI-Internet-Development-Board-Based-ESP8266-4MB-FLASH-ESP-12S-Chip-p-1143874.html?rmmds=search&cur_warehouse=CN).
There are [many compatible expansion boards](https://www.banggood.com/search/d1-mini/2152-0-0-1-1-44-0-price-0-0_p-1.html?brand=623) that require minimal electronics knowledge to use.

## Logic within the software

The arduino code sets out a simple state machine. The logic of that state machine as well as other values tracked and sent to AWS can be seen in the following diagram:
![State machine](https://i.imgur.com/U75YlyI.png)
