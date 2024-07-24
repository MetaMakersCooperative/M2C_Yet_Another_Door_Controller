#include <Wiegand.h>
#include <LittleFS.h>
#include <LinkedList.h>
#include <errno.h>
// Dependencies: freertos/FreeRTOS.h and AsyncTCP.h
#include <espMqttClientAsync.h>

// Note: These need to be defined BEFORE including ETH.h or calling ETH.begin()
#ifndef ETH_PHY_TYPE
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 0
#define ETH_PHY_MDC 23
#define ETH_PHY_MDIO 18
#define ETH_PHY_POWER -1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN
#endif

#include <ETH.h>

#include "root_ca.h"

// The value that will be set as the device's host name
#ifndef DC_HOST_NAME
#define DC_HOST_NAME "m2cdoorone"
#endif

// Client ID is used by the MQTT broker to identify the client
// This value needs to be unique across all clients connected to the broker
#ifndef DC_CLIENT_ID
#define DC_CLIENT_ID "door_one"
#endif

// Host name for the MQTT Broker. The host name is used to get the server IP using DNS
#ifndef DC_MQTT_HOST
#define DC_MQTT_HOST "mqtt.metamakers.org"
#endif

// The user that the MQTT client will use to authenticate
#ifndef DC_MQTT_USER
#define DC_MQTT_USER "door_one"
#endif

// The password that the MQTT client will use to authenticate
#ifndef DC_MQTT_PW
#define DC_MQTT_PW "Door_One!1"
#endif

// The domain used to update the RTC using NTP
#ifndef DC_NTP_HOST
#define DC_NTP_HOST "pool.ntp.org"
#endif

// Enable more verbose debugging information via the serial port
// Do not enable this for production deployments
#ifndef DC_DEBUG
#define DC_DEBUG 0
#endif

// How long the client should wait before trying to reconnect to the MQTT Broker
#define DC_MQTT_RECONNECT_DELAY 20000

// How long to wait to refresh the RTC using NTP
// Default is set to 1 day
#define DC_NTP_REFRESH_TIME 86400000

// Publish MQTT topics with the client ID as the last level
#define TOPIC_LOG_INFO "door_controller/log_info/" DC_CLIENT_ID
#define TOPIC_LOG_WARN "door_controller/log_warn/" DC_CLIENT_ID
#define TOPIC_LOG_FATAL "door_controller/log_fatal/" DC_CLIENT_ID
#define TOPIC_UNLOCK "door_controller/unlock/" DC_CLIENT_ID
#define TOPIC_LOCK "door_controller/lock/" DC_CLIENT_ID
#define TOPIC_DENIED_ACCESS "door_controller/denied_access/" DC_CLIENT_ID
#define TOPIC_CHECK_IN "door_controller/check_in" DC_CLIENT_ID

// Subscribe MQTT topics
#define TOPIC_ACCESS_LIST "door_controller/access_list"
#define TOPIC_HEALTH_CHECK "door_controller/health_check"

// These are the pins connected to the Wiegand D0 and D1 signals.
// Ensure your board supports external Interruptions on these pins
#define PIN_D0 39
#define PIN_D1 36

// Change this value to increase/decrease the amount of time the
// door is unlocked
#define UNLOCK_DOOR_MS 8000

// Relay pins to (un)lock the door
#define PIN_IO14 15
#define PIN_IO15 14

// Possible door states
#define STATE_NORMAL 0
#define STATE_UNLOCKED 1
#define STATE_COMPARING 2
#define STATE_MATCH 3

struct DoorState {
    uint8_t state;
    uint32_t cardCode;
    long doorUnlockedAtMS;
    int cursor;
};

espMqttClientAsync mqttClient;
long lastReconnectAttempt = 0;

LinkedList<uint32_t> codeList = LinkedList<uint32_t>();

Wiegand wiegand;

DoorState doorState;

IPAddress mqttServerIp;
bool hasIp = false;

long lastNTPRefresh = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("Listening...");

    if (DC_DEBUG == 1) {
        Serial.println("Echo flags for debugging");
        Serial.print("DC_HOST_NAME: ");
        Serial.print(DC_HOST_NAME);
        Serial.print(", DC_CLIENT_ID: ");
        Serial.print(DC_CLIENT_ID);
        Serial.print(", DC_MQTT_HOST: ");
        Serial.print(DC_MQTT_HOST);
        Serial.print(", DC_MQTT_USER: ");
        Serial.print(DC_MQTT_USER);
        Serial.print(", DC_MQTT_PW: ");
        Serial.print(DC_MQTT_PW);
        Serial.println("");
    }

    // NOTE: mqttClient needs to be configured BEFORE the ETH.begin() call.
    // In the event that the board is restarted, the ETH connected event will
    // trigger almost immediately. When the event is triggered the mqttClient
    // is set to attempt to connect. If the below lines haven't been called yet
    // then the mqttClient will attempt to connect with the wrong config.
    mqttClient.setClientId(DC_CLIENT_ID);
    mqttClient.setCredentials(DC_MQTT_USER, DC_MQTT_PW);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setTimeout(2);
    mqttClient.setKeepAlive(15);

    // WiFi is not configured! However, the WiFi class is generic
    // which means it can be used to help manage the ETH connection
    // See the ETH example code:
    // https://github.com/espressif/arduino-esp32/blob/2.0.11/libraries/Ethernet/examples/ETH_LAN8720/ETH_LAN8720.ino
    WiFi.onEvent(WiFiEvent);
    ETH.begin();

    if (DC_DEBUG == 1) {
        Serial.print("DNS IP: ");
        Serial.print(ETH.dnsIP());
        Serial.println();
    }
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Starting setup");

    doorState = {STATE_NORMAL, 0, 0, 0};

    if (!LittleFS.begin()) {
        Serial.println("An Error has occurred while mounting LittleFS");
        mqttClient.publish(TOPIC_LOG_FATAL, 1, false, "Failed to mount file system.");
        return;
    }

    File file = LittleFS.open("/cards.txt", FILE_READ);
    if (!file) {
        Serial.println("Failed to open file for reading");
        mqttClient.publish(TOPIC_LOG_FATAL, 1, false, "Failed to open cards.txt");
        return;
    } else {
        buildCodeList(file);
        file.close();
    }

    char listSize[31];
    sprintf(listSize, "Code list size: %d", codeList.size());
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, listSize);
    Serial.println(listSize);
    if (codeList.size() == 0) {
        Serial.println("Code list is empty! cards.txt is likely malformed or corrupted!");
        mqttClient.publish(
            TOPIC_LOG_WARN,
            1,
            false,
            "Code list is empty! cards.txt is likely malformed or corrupted!"
        );
    }

    mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Configuring wiegand");
    wiegand.onReceive(receiveCardCode, &doorState);
    wiegand.onReceiveError(receivedDataError, "Card read error: ");
    wiegand.onStateChange(stateChanged, "State changed: ");
    wiegand.begin(Wiegand::LENGTH_ANY, true);
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, "wiegand configured");

    pinMode(PIN_D0, INPUT);
    pinMode(PIN_D1, INPUT);
    pinMode(PIN_IO14, OUTPUT);
    pinMode(PIN_IO15, OUTPUT);
    digitalWrite(PIN_IO14, LOW);
    digitalWrite(PIN_IO15, LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);

    pinStateChanged();
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Setup complete");
}

void loop() {
    int delayAmount = 100;
    char payload[31];
    switch (doorState.state) {
        case STATE_NORMAL: 
            break;
        case STATE_UNLOCKED:
            if ((millis() - doorState.doorUnlockedAtMS) >= UNLOCK_DOOR_MS) {
                digitalWrite(PIN_IO14, LOW);
                digitalWrite(PIN_IO15, LOW);
                createCardAndTimePayload(payload);
                Serial.println("Locking door.");
                mqttClient.publish(TOPIC_LOCK, 1, false, payload);

                doorState.doorUnlockedAtMS = 0;
                doorState.cardCode = 0;
                doorState.state = STATE_NORMAL;
            }
            break;
        case STATE_MATCH:
            digitalWrite(PIN_IO14, HIGH);
            digitalWrite(PIN_IO15, HIGH);

            createCardAndTimePayload(payload);
            Serial.print("Unlocking door for: ");
            Serial.println(payload);
            mqttClient.publish(TOPIC_UNLOCK, 1, false, payload);

            Serial.println("Unlocking door ...");
            doorState.doorUnlockedAtMS = millis();
            doorState.state = STATE_UNLOCKED;
            doorState.cursor = 0;
            break;
        case STATE_COMPARING:
            if (doorState.cursor >= codeList.size()) {
                createCardAndTimePayload(payload);
                Serial.print("Denied access: ");
                Serial.println(payload);
                mqttClient.publish(TOPIC_DENIED_ACCESS, 1, false, payload);
                doorState.state = STATE_NORMAL;
                doorState.cursor = 0;
                doorState.cardCode = 0;
                break;
            }
            uint32_t testCode = codeList.get(doorState.cursor);
            if (doorState.cardCode == testCode) {
                Serial.println("Match!!");
                doorState.state = STATE_MATCH;
                break;
            }
            doorState.cursor += 1;
            delayAmount = 0;
            break;
    }
    Serial.flush();
    noInterrupts();
    wiegand.flush();
    interrupts();

    if (attemptMqttConnection() && millis() - lastReconnectAttempt > DC_MQTT_RECONNECT_DELAY) {
        connectToMqtt();
    }

    if (ETH.linkUp() && hasIp) {
        if (millis() - lastNTPRefresh > DC_NTP_REFRESH_TIME) {
            xTaskCreate(setClock, "set_clock", 10000, NULL, 1, NULL);
            lastNTPRefresh = millis();
        }
    }

    delay(delayAmount);
}

void buildCodeList(File &file) {
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Building code list");
    Serial.println("Building List");

    char strCode[11];
    int bytesRead = 0;
    while (file.available()) {
        strCode[0] = '\0';
        bytesRead = 0;

        int character;
        while (bytesRead < 10) {
            character = file.read();
            if (character == '\n' || character == EOF) break;
            strCode[bytesRead] = character;
            bytesRead += 1;
        }

        strCode[bytesRead] = '\0';
        if (bytesRead == 0) {
            Serial.println("No data found between line \\n characters.");
            mqttClient.publish(TOPIC_LOG_WARN, 1, false, "No data found between line \\n characters.");
            continue;
        }

        if (DC_DEBUG == 1) {
            Serial.printf("last character: %#x", character);
            Serial.println();
            Serial.print("First 10 bytes: ");
            char *idx = strCode;
            while(idx < strCode + 10) {
                if (*idx == '\0' || *idx == EOF) break;
                Serial.printf(" %#x ", *idx);
                idx += 1;
            }
            Serial.println();
            Serial.flush();
        }

        // If there are more characters than the expected 10 then it's not
        // safe to assume that the characters read are a code that should
        // be able to unlock the door.
        if (character != '\n' && character != EOF) {
            Serial.println("Found more characters than expected. Ignoring.");
            character = file.read();
            while (character != '\n' && character != EOF) {
                character = file.read();
            }
            continue;
        }

        char *endPointer = 0;
        uint32_t code = strtoul(strCode, &endPointer, 10);
        if ((errno == ERANGE && code == UINT32_MAX) || (errno != 0 && code == 0)) {
            Serial.print("Conversion error occurred with: ");
            Serial.println(strCode);
            Serial.println("Skipping entry.");
            continue;
        }
        if (strcmp(endPointer, strCode) == 0) {
            Serial.println("No digits were found. Ignoring.");
            continue;
        }
        codeList.add(code);
    }
}

void createCardAndTimePayload(char *payload) {
    struct tm timeinfo;
    bool hasTime = true;

    if(!getLocalTime(&timeinfo)){
        Serial.println("Failed to obtain time");
        mqttClient.publish(TOPIC_LOG_WARN, 1, false, "Failed to get time from RTC");
        hasTime = false;
    }

    sprintf(payload, "%010d", doorState.cardCode);

    if (hasTime) {
        char time[21];
        strftime(time, 21, "|%F %T", &timeinfo);
        strcat(payload, time);
    }
}

bool attemptMqttConnection() {
    return mqttClient.disconnected() && ETH.linkUp() && hasIp;
}

void connectToMqtt() {
    Serial.println("MQTT connecting ...");
    if (ETH.linkUp() && hasIp) {
        lastReconnectAttempt = millis();
        mqttClient.connect();
    } else {
        Serial.println("Cannot create connection to MQTT. Ethernet is down.");
        Serial.print("ETH Link Status: ");
        Serial.println(ETH.linkUp());
        Serial.print("ETH IP: ");
        Serial.println(ETH.localIP());
        Serial.flush();
    }
}

void onMqttConnect(bool sessionPresent) {
    Serial.println("Connected to MQTT.");
    mqttClient.subscribe(TOPIC_ACCESS_LIST, 1);
    mqttClient.subscribe(TOPIC_HEALTH_CHECK, 1);
}

void onMqttDisconnect(espMqttClientTypes::DisconnectReason reason) {
    Serial.print("MQTT Disconnected - Code: ");
    Serial.print(static_cast<uint8_t>(reason));
    Serial.print(" - Reason: ");
    switch (reason) {
        case espMqttClientTypes::DisconnectReason::TCP_DISCONNECTED:
            Serial.println("TCP disconnected");
            break;
        case espMqttClientTypes::DisconnectReason::MQTT_NOT_AUTHORIZED:
            Serial.println("Not Authorized");
            break;
        case espMqttClientTypes::DisconnectReason::MQTT_SERVER_UNAVAILABLE:
            Serial.println("Server unavailabled");
            break;
        case espMqttClientTypes::DisconnectReason::MQTT_MALFORMED_CREDENTIALS:
            Serial.println("Malformed credentials");
            break;
        case espMqttClientTypes::DisconnectReason::MQTT_IDENTIFIER_REJECTED:
            Serial.println("Identifier rejected");
            break;
        case espMqttClientTypes::DisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
            Serial.println("Unacceptable protocol version");
            break;
        case espMqttClientTypes::DisconnectReason::TLS_BAD_FINGERPRINT:
            Serial.println("TLS bad fingerprint");
            break;
        case espMqttClientTypes::DisconnectReason::USER_OK:
            Serial.println("User ok");
            break;
        default:
            Serial.println("Unknown");
            break;
    }
}

void onMqttMessage(
    const espMqttClientTypes::MessageProperties& properties,
    const char* topic,
    const uint8_t* payload,
    size_t len,
    size_t index,
    size_t total
) {
    if (DC_DEBUG == 1) {
        Serial.println("Publish received.");
        Serial.print("  topic: ");
        Serial.println(topic);
        Serial.print("  qos: ");
        Serial.println(properties.qos);
        Serial.print("  dup: ");
        Serial.println(properties.dup);
        Serial.print("  retain: ");
        Serial.println(properties.retain);
        Serial.print("  len: ");
        Serial.println(len);
        Serial.print("  index: ");
        Serial.println(index);
        Serial.print("  total: ");
        Serial.println(total);
    }
    char infoPayload[400];
    snprintf(
        infoPayload,
        400,
        "P:%s|QOS:%s|DUP:%s|R:%s|L:%s|I:%s|T:%s",
        topic,
        properties.qos,
        properties.dup,
        properties.retain,
        len,
        index,
        total
    );
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, infoPayload);
    if (index != 0 || (len < total)) {
        mqttClient.publish(TOPIC_LOG_WARN, 1, false, "Split payload, ignoring");
        return;
    } else if (properties.dup) {
        mqttClient.publish(TOPIC_LOG_WARN, 1, false, "Received duplicate, ignoring");
        return;
    } else if (strcmp(topic, TOPIC_ACCESS_LIST) == 0) {
        File file = LittleFS.open("/cards.txt", FILE_WRITE);
        if (!file) {
            Serial.println("Failed to open cards.txt for writing");
            mqttClient.publish(TOPIC_LOG_FATAL, 1, false, "Failed to open cards.txt for writing");
            return;
        }

        Serial.println("Rebuilding cards.txt");
        mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Rebuilding cards.txt");

        const char *payloadPointer = reinterpret_cast<const char*>(payload);
        while (*(payloadPointer + strspn(payloadPointer, "\n")) != '\0') {
            file.write(*payloadPointer);
            payloadPointer += 1;
        }
        file.write(EOF);
        file.flush();
        file.close();

        file = LittleFS.open("/cards.txt", FILE_READ);
        if (!file) {
            Serial.println("Failed to read cards.txt");
            mqttClient.publish(TOPIC_LOG_FATAL, 1, false, "Failed to read cards.txt");
            return;
        }

        codeList.clear();
        buildCodeList(file);
        file.close();

        Serial.println("Completed rebuilding cards.txt");
        mqttClient.publish(TOPIC_LOG_INFO, 1, false, "Completed rebuilding cards.txt");

    } else if (strcmp(topic, TOPIC_HEALTH_CHECK) == 0) {
        Serial.println("Received health check message");
        mqttClient.publish(TOPIC_CHECK_IN, 1, false, DC_CLIENT_ID);
    } else {
        char payload[100];
        snprintf(payload, 100, "%s", "Recieved an unknown topic:  %s", topic);
        mqttClient.publish(TOPIC_LOG_INFO, 1, false, payload);
        Serial.println(payload);
    }
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
    wiegand.setPin0State(digitalRead(PIN_D0));
    wiegand.setPin1State(digitalRead(PIN_D1));
}

// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
    char payload[100];
    if (plugged) {
        snprintf(payload, 100, "%s", "Wiegand disconnected: %s", message);
        mqttClient.publish(TOPIC_LOG_INFO, 1, false, payload);
        Serial.println(payload);
    } else {
        snprintf(payload, 100, "%s", "Wiegand disconnected: %s", message);
        mqttClient.publish(TOPIC_LOG_INFO, 1, false, payload);
        Serial.println(payload);
    }
}

void receiveCardCode(uint8_t* data, uint8_t bits, DoorState* state) {
    if (DC_DEBUG == 1) {
        Serial.println("Receiving data ...");
        Serial.print("Current state: ");
        Serial.println(state->state);
    }
    if (state->state != STATE_NORMAL) {
        return;
    }
    uint32_t code = 0;
    uint8_t bytes = (bits + 7) / 8;
    for (int i = 0; i < bytes; i++) {
        code = code << 8;
        code = code | data[i];
    }
    state->cardCode = code;
    state->state = STATE_COMPARING;
    if (DC_DEBUG == 1) {
        Serial.print("Moving to state: ");
        Serial.println(state->state);
    }
}

void receivedDataError(Wiegand::DataError error, uint8_t* rawData, uint8_t rawBits, const char* message) {
    Serial.print(message);
    Serial.print(Wiegand::DataErrorStr(error));
    Serial.print(" - Raw data: ");
    Serial.print(rawBits);
    Serial.print("bits / ");

    uint8_t bytes = (rawBits + 7) / 8;
    for (int i = 0; i < bytes; i++) {
        Serial.print(rawData[i] >> 4, 16);
        Serial.print(rawData[i] & 0xF, 16);
    }
    Serial.println();
}

void WiFiEvent(WiFiEvent_t event) {
    int resultCode;
    switch(event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("ETH Started");
            ETH.setHostname(DC_HOST_NAME);
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("ETH connected");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            hasIp = true;
            Serial.print("ETH MAC: ");
            Serial.print(ETH.macAddress());
            Serial.print(", IPv4: ");
            Serial.print(ETH.localIP());
            if (ETH.fullDuplex()) {
                Serial.print(", FULL_DUPLEX");
            }
            Serial.print(", ");
            Serial.print(ETH.linkSpeed());
            Serial.println("Mbps");

            setClock(NULL);

            resultCode = WiFi.hostByName(DC_MQTT_HOST, mqttServerIp);
            if (resultCode == 1) {
                Serial.print("IP found for hostname: ");
                Serial.print(DC_MQTT_HOST);
                Serial.print(" - IP: ");
                Serial.print(mqttServerIp.toString());
                Serial.println();
                mqttClient.setServer(mqttServerIp, 1883);
            } else {
                Serial.print("Could not find hostname: ");
                Serial.print(DC_MQTT_HOST);
                Serial.println();
            }

            connectToMqtt();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH Disconnected");
            hasIp = false;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            Serial.println("ETH Stopped");
            hasIp = false;
            break;
        default:
            Serial.print("Got unhandled event");
            Serial.println(event);
            break;
    }
}

void setClock(void *_p) {
    configTime(0, 0, DC_NTP_HOST);

    Serial.print("Waiting for NTP time sync: ");
    time_t nowSecs = time(nullptr);
    while (nowSecs < 8 * 3600 * 2) {
        delay(100);
        yield();
        nowSecs = time(nullptr);
    }

    struct tm timeinfo;
    gmtime_r(&nowSecs, &timeinfo);
    Serial.print("Current time: ");
    Serial.println(asctime(&timeinfo));
    char payload[100];
    snprintf(payload, 100, "%s", asctime(&timeinfo));
    mqttClient.publish(TOPIC_LOG_INFO, 1, false, payload);
}
