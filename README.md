# M2C Yet Another Door Controller

Proof of concept for new door controller design using a `WT32-ETH01`.

## Development Requirements

Make sure version `v1.X` of the Arduino IDE installed. It is required to upload the cards file. See [Cards File](#cards_file) section.

`arduino-cli` also needs to be installed to compile and upload the sketch. It is possible to use the Arduino IDE `v1.X` or `v2` but they have not been tested/verified. Milage may vary.

## Cards File

The sketch assumes a file named `cards.txt` exists within a `LitteFS` file system. The code will not function correctly without it.

Unfortunately, the only way to upload the file to the device is using `v1.X` Arduino IDE. This is because the upload using a plugin that only can be used with that version of the IDE. Setting up the upload plugin is only required if the `cards.txt` file needs to be updated or if the flash storage was wiped.

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
└── M2C_Yet_Another_Door_Controller.ino
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
arduino-cli core install esp32:esp32@2.0.11
```

> Might need to run `arduino-cli core update-index` to update the board index.

This will install [espressif/arduino-esp32 version `2.0.11`](https://github.com/espressif/arduino-esp32/tree/2.0.11).

The following libraries can be installed using the Arduino IDE or by downloading the files directly from the source repositories.

When downloading the libraries manually, make sure the source is place them within the following directory: `~/Arduino/libraries`.

The `libraries` should look like this assuming the only installed libraries are the ones used by this project.

```
~/Arduino/libraries
├── LinkedList
│   ├── ...
└── Yet_Another_Arduino_Wiegand_Library
    ├── ...
```

> Make sure to download the source tagged with the versions listed below!

The following dependencies need to be installed manually as the latest versions are required:

> The best way to get these libraries is to `git clone` them into the `~/Arduino/libraries` directory.

- [Yet Another Arduino Wiegand Library](https://github.com/paulo-raca/YetAnotherArduinoWiegandLibrary) commit: `556e7b8` tag: `2.0.1`
- [LinkedList](https://github.com/ivanseidel/LinkedList) commit: `0439a72` tag: `v1.3.3`
- [espMqttClient](https://github.com/bertmelis/espMqttClient) commit: `70d3113` tag: `v1.7.0`
- [AsyncTCP](https://github.com/mathieucarbou/AsyncTCP) commit: `b20e92e` tag: `v3.1.4`

## Commands (using CLI)

> Note: `/dev/ttyUSB0` is the default device. Run the following command to see what device is being used: `arduino-cli board list`. Make sure to modify the commands below to match that device.

To compile the sketch with defaults, run following command:

> WARNING: Defaults will apply not overridden which could cause a lot of problems on multi door deployments. See the [Override Defaults](#override-defaults) section below.

```bash
arduino-cli compile --fqbn esp32:esp32:wt32-eth01 ./
```

To enable more debugging messages, run the following command:

```bash
arduino-cli compile --build-property "build.extra_flags=\"-DDC_DEBUG=1\"" --fqbn esp32:esp32:wt32-eth01 ./
```

To Upload the compiled sketch:

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:wt32-eth01 ./
```

To monitor the serial output:

```bash
arduino-cli monitor --log-level debug --fqbn esp32:esp32:wt32-eth01 -p /dev/ttyUSB0 -c baudrate=115200
```

## Override Defaults

> Note: Make sure the latest Arduino CLI is installed

The table below shows the build flags that should be overridden for each controller being deployed.
Refer to the source files for a complete list of overridable flags.

| Flag Name    | Default             | Description                                                                                                                           | Build Argument                     |
|--------------|---------------------|---------------------------------------------------------------------------------------------------------------------------------------|------------------------------------|
| DC_HOST_NAME | m2cdoorone          | The value that will be set as the device's host name                                                                                  | -DDC_HOST_NAME=m2cdoorone          |
| DC_CLIENT_ID | door_one            | Client ID is used by the MQTT broker to identify the client. This value needs to be unique across all clients connected to the broker | -DDC_CLIENT_ID=door_one            |
| DC_MQTT_HOST | mqtt.metamakers.org | Host name for the MQTT Broker. The host name is used to get the server IP using DNS                                                   | -DDC_MQTT_HOST=mqtt.metamakers.org |
| DC_MQTT_USER | door_one            | The user that the MQTT client will use to authenticate                                                                                | -DDC_MQTT_USER=door_one            |
| DC_MQTT_PW   | Door_One!1          | The password that the MQTT client will use to authenticate                                                                            | -DDC_MQTT_PW=Door_One!1            |
| DC_NTP_HOST  | pool.ntp.org        | The domain used to update the RTC using NTP                                                                                           | -DDC_NTP_HOST=pool.ntp.org         |
| DC_DEBUG     | 0                   | Enable more verbose debugging information via the serial port. Do not enable this for production deployments                          | -DDC_DEBUG=0                       |

Below is an example of how to override all the flags in the table above:

> Documentation on the CLI compile command can be found [here](https://arduino.github.io/arduino-cli/0.36/commands/arduino-cli_compile/).

```bash
arduino-cli compile --build-property "build.extra_flags=\"-DDC_HOST_NAME=m2cdoorone -DDC_CLIENT_ID=door_one -DDC_MQTT_HOST=mqtt.metamakers.org -DDC_MQTT_USER=door_one -DDC_MQTT_PW=Door_One!1 -DDC_NTP_HOST=pool.ntp.org -DDC_DEBUG=0\"" --fqbn esp32:esp32:wt32-eth01 ./
```
