#include "Arduino.h"
#include <ESP8266WiFi.h>
#include "Wire.h"
#include "Adafruit_MCP23017.h"

const int PIN_Z = 15;
const int PIN_LOW = 14;
const int PIN_HIGH = 13;
const int PIN_MEDIUM = 12;

const int PIN_LED_LOW = 2;
const int PIN_LED_MEDIUM = 1;
const int PIN_LED_HIGH = 0;
const int PIN_SWITCH_WIFI = 4;
const int PIN_SWITCH_FAN = 3;
const int PIN_SWITCH_AUTO = 5;
const int PIN_SWITCH_COVER = 6;

class WMCP23017Expander : public WPin {
public:
  typedef std::function<void(byte pin, bool isRising)> THandlerFunction;


  WMCP23017Expander(byte i2cAddress) :
			WPin(NO_PIN, INPUT) {
    this->i2cAddress = i2cAddress;
    lastUpdate = 0;
    lastRead = 0;
    for (int i = 0; i < 4; i++) {
      oldStates[i] = false;
    }


    Wire.begin();
    expander = new Adafruit_MCP23017();
    expander->begin();
    expander->pinMode(PIN_LED_LOW, OUTPUT);
    expander->pinMode(PIN_LED_MEDIUM, OUTPUT);
    expander->pinMode(PIN_LED_HIGH, OUTPUT);
    expander->pinMode(PIN_SWITCH_WIFI, INPUT);
    expander->pinMode(PIN_SWITCH_FAN, INPUT);
    expander->pinMode(PIN_SWITCH_AUTO, INPUT);
    expander->pinMode(PIN_SWITCH_COVER, INPUT);
    expander->pinMode(PIN_Z, OUTPUT);
    expander->pinMode(PIN_LOW, OUTPUT);
    expander->pinMode(PIN_MEDIUM, OUTPUT);
    expander->pinMode(PIN_HIGH, OUTPUT);
    //LEDs off
    expander->digitalWrite(PIN_LED_LOW, HIGH);
    expander->digitalWrite(PIN_LED_LOW, HIGH);
    expander->digitalWrite(PIN_LED_LOW, HIGH);
  }

  virtual ~WMCP23017Expander() {

  }

  void loop(unsigned long now) {
    if ((lastUpdate == 0) || (now - lastUpdate > 50)) {
      lastUpdate = now;

      bool newB = expander->digitalRead(PIN_SWITCH_COVER);
      if (newB != oldStates[0]) {
        oldStates[0] = newB;
        if (!isCoverOpen()) {
          oldStates[1] = false;
          oldStates[2] = false;
          oldStates[3] = false;
        }
        notify(PIN_SWITCH_COVER, isCoverOpen());
      }
      if (isCoverOpen()) {
        //WIFI
        newB = expander->digitalRead(PIN_SWITCH_WIFI);
        if (newB != oldStates[1]) {
          oldStates[1] = newB;
          if (oldStates[1]) notify(PIN_SWITCH_WIFI, true);
        }
        //FAN
        newB = expander->digitalRead(PIN_SWITCH_FAN);
        if (newB != oldStates[2]) {
          oldStates[2] = newB;
          if (oldStates[2]) notify(PIN_SWITCH_FAN, true);
        }
        //AUTO
        newB = expander->digitalRead(PIN_SWITCH_AUTO);
        if (newB != oldStates[3]) {
          oldStates[3] = newB;
          if (oldStates[3]) notify(PIN_SWITCH_AUTO, true);
        }
      }
    }
  }

  void setOnNotify(THandlerFunction fn) {
    _callback = fn;
  }

  void digitalWrite(int pinNo, int value) {
    expander->digitalWrite(pinNo, value);
  }

  bool isCoverOpen() {
    return oldStates[0];
  }

private:
  byte i2cAddress, lastRead;
  THandlerFunction _callback;
  unsigned long lastUpdate;
  Adafruit_MCP23017* expander;
  bool oldStates[4];

  void notify(byte pin, bool isRising) {
    if (_callback) {
      _callback(pin, isRising);
    }
  }



};
