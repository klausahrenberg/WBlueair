#include "Arduino.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif
#include "WI2C.h"

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

class WIOExpander : public WI2C {
public:
  typedef std::function<void(byte pin, bool isRising)> THandlerFunction;


  WIOExpander(byte i2cAddress) :
			WI2C(i2cAddress, 21, 22, NO_PIN) {
    this->coverOpen = false;
    statesA = 0b00000111;
    statesB = 0b00000000;
    changed = true;
    Wire.beginTransmission(this->getAddress());
    // Select bandwidth rate register
    Wire.write(0x2C);
    // Normal mode, Output data rate = 100 Hz
    Wire.write(0x0A);
    Wire.endTransmission();
    configureExpander();
  }

  void configureExpander() {
    // set entire PORT A to output
    Wire.beginTransmission(this->getAddress());
    Wire.write(0x00); // IODIRA register
    Wire.write(0b00111000);
    Wire.endTransmission();
    // set entire PORT B to output
    Wire.beginTransmission(this->getAddress());
    Wire.write(0x01); // IODIRB register
    Wire.write(0b00000000);
    Wire.endTransmission();
  }

  void loop(unsigned long now) {
    //Read inputs
    Wire.beginTransmission(this->getAddress());
    Wire.write(0x12); // address port A
    Wire.endTransmission();
    Wire.requestFrom(this->getAddress(), 1);
    inputA = Wire.read();


    /*bool newB = bitRead(inputA, PIN_SWITCH_COVER);
    if (newB != getBit(PIN_SWITCH_COVER)) {
      setBit(PIN_SWITCH_COVER, newB);
      if (!isCoverOpen()) {
        setBit(PIN_SWITCH_WIFI, false);
        setBit(PIN_SWITCH_FAN, false);
        setBit(PIN_SWITCH_AUTO, false);
      }
      notify(PIN_SWITCH_COVER, isCoverOpen());
    }*/
    if (isCoverOpen()) {
      //WIFI
      bool newB = bitRead(inputA, PIN_SWITCH_WIFI);
      if (newB != getBit(PIN_SWITCH_WIFI)) {
        setBit(PIN_SWITCH_WIFI, newB);
        if (newB) notify(PIN_SWITCH_WIFI, true);
      }
      //FAN
      newB = bitRead(inputA, PIN_SWITCH_FAN);
      if (newB != getBit(PIN_SWITCH_FAN)) {
        setBit(PIN_SWITCH_FAN, newB);
        if (newB) notify(PIN_SWITCH_FAN, true);
      }
      //AUTO
      newB = bitRead(inputA, PIN_SWITCH_AUTO);
      if (newB != getBit(PIN_SWITCH_AUTO)) {
        setBit(PIN_SWITCH_AUTO, newB);
        if (newB) notify(PIN_SWITCH_AUTO, true);
      }
    } else {
      setBit(PIN_SWITCH_WIFI, false);
      setBit(PIN_SWITCH_FAN, false);
      setBit(PIN_SWITCH_AUTO, false);
    }

    if (changed) {
      Serial.println("Expander state changed. Write to expander");
      configureExpander();
      //Set states A
      Wire.beginTransmission(this->getAddress());
      Wire.write(0x12); // address port A
      Wire.write(statesA);  // value to send
      Wire.endTransmission();
      //Set states B
      Wire.beginTransmission(this->getAddress());
      Wire.write(0x13); // address port B
      Wire.write(statesB);  // value to send
      Wire.endTransmission();
      changed = false;
    }
  }

  void setOnNotify(THandlerFunction fn) {
    _callback = fn;
  }

  void digitalWrite(int pinNo, int value) {
    setBit(pinNo, value);
  }

  bool getBit(byte pinNo) {
    if (pinNo < 8) {
      return bitRead(statesA, pinNo);
    } else {
      return bitRead(statesB, pinNo -8);
    }
  }

  void setBit(byte pinNo, bool value) {
    bool oldValue = getBit(pinNo);
    if (oldValue != value) {
      changed = true;
      if (pinNo < 8) {
        bitWrite(statesA, pinNo, value);
      } else {
        bitWrite(statesB, pinNo - 8, value);
      }
    }
  }

  bool isCoverOpen() {
    return coverOpen;
  }

  void setCoverOpen(bool coverOpen) {
    this->coverOpen = coverOpen;
  }

private:
  THandlerFunction _callback;
  byte statesA;
  byte inputA;
  byte statesB;
  byte resetPin;
  bool coverOpen;
  bool changed;

  void notify(byte pin, bool isRising) {
    if (_callback) {
      _callback(pin, isRising);
    }
  }



};
