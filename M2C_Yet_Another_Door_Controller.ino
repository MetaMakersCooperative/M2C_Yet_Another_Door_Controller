#include <Wiegand.h>
#include <LittleFS.h>
#include <LinkedList.h>
#include <errno.h>

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

#define STATE_NORMAL 0
#define STATE_UNLOCKED 1
#define STATE_COMPARING 2
#define STATE_MATCH 3

struct State {
    uint8_t state;
    uint32_t cardCode;
    long doorUnlockedAtMS;
    int cursor;
};

LinkedList<uint32_t> codeList = LinkedList<uint32_t>();

Wiegand wiegand;

State state;

void setup() {
    Serial.begin(115200);
    Serial.println("Listening...");

    state = {STATE_NORMAL, 0, 0, 0};

    if (!LittleFS.begin()) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    File file = LittleFS.open("/cards.txt");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.println("Building List");
    while (file.available()) {
        String strCode = file.readStringUntil('\n');
        Serial.println(strCode);
        char* endPtr = 0;
        uint32_t code = strtoul(strCode.c_str(), &endPtr, 10);
        if ((errno == ERANGE && code == UINT32_MAX) || (errno != 0 && code == 0)) {
            Serial.print("Conversion error occurred with: ");
            Serial.println(strCode);
            Serial.println("Skipping entry.");
            continue;
        }
        if (endPtr == strCode.c_str()) {
            Serial.print("No digits were found: ");
            Serial.println(strCode);
            Serial.println("Skipping entry.");
            continue;
        }
        codeList.add(code);
    }

    file.close();

    wiegand.onReceive(receiveCardCode, &state);
    wiegand.onReceiveError(receivedDataError, "Card read error: ");
    wiegand.onStateChange(stateChanged, "State changed: ");
    wiegand.begin(Wiegand::LENGTH_ANY, true);

    pinMode(PIN_D0, INPUT);
    pinMode(PIN_D1, INPUT);
    pinMode(PIN_IO14, OUTPUT);
    pinMode(PIN_IO15, OUTPUT);
    digitalWrite(PIN_IO14, LOW);
    digitalWrite(PIN_IO15, LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);

    pinStateChanged();
}

void loop() {
    int delayAmount = 100;

    switch (state.state) {
        case STATE_NORMAL: 
            break;
        case STATE_UNLOCKED:
            if ((millis() - state.doorUnlockedAtMS) >= UNLOCK_DOOR_MS) {
                digitalWrite(PIN_IO14, LOW);
                digitalWrite(PIN_IO15, LOW);
                Serial.println("Locking door.");

                state.doorUnlockedAtMS = 0;
                state.state = STATE_NORMAL;
            }
            break;
        case STATE_MATCH:
            Serial.println("Unlocking door ...");
            digitalWrite(PIN_IO14, HIGH);
            digitalWrite(PIN_IO15, HIGH);
            state.doorUnlockedAtMS = millis();
            state.cardCode = 0;
            state.state = STATE_UNLOCKED;
            state.cursor = 0;
            break;
        case STATE_COMPARING:
            if (state.cursor >= codeList.size()) {
                state.state = STATE_NORMAL;
                state.cursor = 0;
                break;
            }
            uint32_t testCode = codeList.get(state.cursor);
            if (state.cardCode == testCode) {
                Serial.println("Match!!");
                state.state = STATE_MATCH;
                break;
            }
            state.cursor += 1;
            delayAmount = 0;
            break;
    }
    noInterrupts();
    wiegand.flush();
    interrupts();
    delay(delayAmount);
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged() {
    wiegand.setPin0State(digitalRead(PIN_D0));
    wiegand.setPin1State(digitalRead(PIN_D1));
}

// Notifies when a reader has been connected or disconnected.
// Instead of a message, the seconds parameter can be anything you want -- Whatever you specify on `wiegand.onStateChange()`
void stateChanged(bool plugged, const char* message) {
    Serial.print(message);
    Serial.println(plugged ? "CONNECTED" : "DISCONNECTED");
}

void receiveCardCode(uint8_t* data, uint8_t bits, State* state) {
    Serial.println("Receiving data ...");
    Serial.print("Current state: ");
    Serial.println(state->state);
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
    Serial.print("Moving to state: ");
    Serial.println(state->state);
}

// Notifies when an invalid transmission is detected
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
