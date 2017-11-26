# nodemcu-suart-module

This nodemcu-firmware module adds software uart support on any pin with esp8266.

Installation:
1) Download and install docker (https://www.docker.com -> Get Docker)
2) Do following commands to build firmware with built-in software UART module:
```bash
pip install esptool # if you already have esptool, just skip this step
git clone git@github.com:nodemcu/nodemcu-firmware.git # clone nodemcu-firmware repository
git clone git@github.com:kozharskyad/nodemcu-suart-module.git # clone this repository
cp nodemcu-suart-module/app/modules/suart.c nodemcu-firmware/app/modules/suart.c # copy module source code
cd nodemcu-firmware # change directory to nodemcu firmware src
docker run --rm -ti -v `pwd`:/opt/nodemcu-firmware marcelstoer/nodemcu-build # run firmware build with docker
ls bin # list binary files for flashing on esp8266
export FRM_FILE="bin/nodemcu_integer_master_20171125-2047.bin" # choose integer build
export ESP_DEV="/dev/tty.wchusbserial1d170" # choose esp8266 device for flashing
esptool.py --port $ESP_DEV erase_flash # erase flash. NOTE: previous UART connection must be closed! All esp8266 data will be erased!
esptool.py --port $ESP_DEV write_flash -fm dio 0x00000 $FRM_FILE # flash new firmware
```
3) Connect to your esp8266 with esplorer and follow software UART module instructions:
In esplorer console:
```lua
local RX_PIN=1
local TX_PIN=2
local BAUD_RATE=9600
local STR_TERMINATOR="$"
suart.setup(RX_PIN, TX_PIN, BAUD_RATE)
suart.on("data", STR_TERMINATOR, function(data)
  print("Received data: "..data)
end)
```
4) Connect another esp8266 or arduino: TX pin to D1, RX pin to D2
5) Send string data terminated by dollar sign ("$")
6) You must see "Received data: Hello, World!" in your esplorer lua console

Contributors are welcome!
