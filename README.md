# M2C Yet Another Door Controller

Proof of concept for new door controller design using a `WT32-ETH01`.

## Development Requirements

Make sure version `v1.X` of the Arduino IDE installed. It is required to upload the cards file. See [Cards File](#cards_file) section.

`arduino-cli` also needs to be installed to compile and upload the sketch. It is possible to use the Arduino IDE `v1.X` or `v2` but they have not been tested/verified. Milage may vary.

## Config

Using the `arduino-cli`, make sure the config contains the `esp8266` index is listed under `additional_urls`.

> Run `arduino-cli config dump` to see the current config. If it doesn't exist run `arduino-cli config init`.
> Then edit the config file under ~/.arduino15/arduino-cli.yaml and make it look like this:

```yaml
board_manager:
  additional_urls:
    - https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

> Run `arduino-cli core update-index` to update the board index and `arduino-cli core install esp32:esp32` install the `esp32` libraries.

## Cards File

The sketch assumes a file named `cards.txt` exists within a `LitteFS` file system. The code will not function correctly without it.

Unfortantely, the only way to upload the file to the device is using `v1.X` Arduino IDE. This is because the upload using a plugin that only can be used with that version of the IDE. Setting up the upload plugin is only required if the `cards.txt` file needs to be updated or if the flash storage was wiped.

### Setup the Upload Plugin

To setup the plugin, follow the steps outlined below:

1. First download the following `zip` file: [https://github.com/lorol/arduino-esp32fs-plugin/releases/download/2.0.7/esp32fs.zip](https://github.com/lorol/arduino-esp32fs-plugin/releases/download/2.0.7/esp32fs.zip)
2. Unzip and place the `esp32fs.jar` file in the following directory: `~/Arduino/tools/ESP32FS/tool/esp32fs.jar`
3. Restart Arduino IDE if it's running

### Uploading

Before uploading, create the `data` directory in the project root if it does not already exist. Update/create the `card.txt` file with the codes that should unlock the door. Make sure each code is in it's own line, like below:

> Each code should be no more than 10 digits long and only container digits `0-9`. No leading zeros!

```text
1234567
8912345
6789123
```

Double check and make sure the project structure matches the following:

```text
~/Arduino/M2C_Yet_Another_Door_Controller/
├── data
│   └── cards.txt
├── README.md
├── sketch.yaml
└── M2CYetAnotherDoorController.ino
```

To upload the `card.txt` file, follow the steps outlined below:

> When referencing the Arduino IDE, it is implied that it's `v1.X` not `v2`

1. Open Arduino IDE and open the `M2CYetAnotherDoorController.ino` sketch.
2. Navigate and click on `Tools -> Board` and select `WT32-ETH01 Ethernet Module` if it is not already selected.
3. Navigate and click on `Tools -> ESP32 Sketch Date Upload`
4. Select `LittleFS` then click `OK`

## Libraries

To install the `esp32` libraries run the following command:

```bash
arduino-cli core install esp32:esp32
```

The following libraries can be installed using the Arduino IDE or by downloading the files directly from the source repositories.

When downloading the libraries manually, make sure the source is place them within the following directory: `~/Arduino/libraries`.

The `libraries` should look like this assuming the only installed libraries are the ones used by this project.

```
~/Arduino/libraries
├── LinkedList
│   ├── ...
├── LittleFS_esp32
│   ├── ...
└── Yet_Another_Arduino_Wiegand_Library
    ├── ...
```

> Make sure to download the source tagged with the versions listed below!

- [LittleFS_esp32](https://github.com/lorol/LITTLEFS) - by: lorol - version: 1.0.5
- [Yet Another Arduino Wiegand Library](https://github.com/paulo-raca/YetAnotherArduinoWiegandLibrary) - by: Paulo Costa - version: 2.0.0
- [LinkedList](https://github.com/ivanseidel/LinkedList) - version: v1.3.3

## Commands (using CLI)

> Note: `/dev/ttyUSB0` is the default device. Run the following command to see what device is being used: `arduino-cli board list`. Make sure to modify the commands below to match that device.

To compile the sketch run following command:

```bash
arduino-cli compile --fqbn esp32:esp32:wt32-eth01 ./
```

To Upload the compiled sketch:

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:wt32-eth01 ./
```

To monitor the serial output:

```bash
arduino-cli monitor --log-level debug --fqbn esp32:esp32:wt32-eth01 -p /dev/ttyUSB0 -c baudrate=115200
```
