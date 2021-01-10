#ifndef PURIFIER_DEVICE_MCU_H
#define	PURIFIER_DEVICE_MCU_H

#include "Arduino.h"
#include <EEPROM.h>
#include "Wire.h"
#include "WOutsideAqiDevice.h"
#include "WIOExpander.h"
#include "WStatusLeds.h"
#include "WPms7003.h"
#include "WIaqCore.h"
#include "WClock.h"
#include "WTemperatureSensor.h"


#ifdef ESP8266
#define PIN_PMS_SLEEP D5
#elif ESP32
#define PIN_PMS_SLEEP 33
#define PIN_SWITCH_COVER 13
#define PIN_EXPANDER_RESET 15
#endif
#define AQI_LIMIT_LOW 15
#define AQI_LIMIT_MEDIUM 40
#define AQI_LIMIT_HIGH 100

class WPurifierDevice: public WDevice {
public:
  typedef std::function<bool()> THandlerFunction;

  WPurifierDevice(WNetwork* network)
  	: WDevice(network, "airpurifier", "Air Purifier", DEVICE_TYPE_MULTI_LEVEL_SWITCH) {
    //outside AQI device
    this->outsideAqiDevice = new WOutsideAqiDevice(network);
    this->insideOutsideAqiStatus = network->getSettings()->setBoolean("insideOutsideAqiStatus", true);
    this->insideOutsideAqiStatus->setReadOnly(true);
		this->insideOutsideAqiStatus->setVisibility(NONE);
    this->addProperty(insideOutsideAqiStatus);
    //switchStatusLedOffAtNight
    this->switchStatusLedOffAtNight = network->getSettings()->setBoolean("switchStatusLedOffAtNight", true);
    this->switchStatusLedOffAtNight->setReadOnly(true);
		this->switchStatusLedOffAtNight->setVisibility(NONE);
    this->addProperty(switchStatusLedOffAtNight);
    //clock
    this->clock = new WClock(network, true);
    if (this->switchStatusLedOffAtNight->getBoolean()) {
      this->clock->nightMode->setOnChange([this](WProperty *prop){
        this->leds->statusLedOn->setBoolean(!prop->getBoolean());
      });
    }
    //IAQ Core
    Wire.begin();
    //Initialize expander
    resetExpander();

    this->iaqCore = new WIaqCore(this->network);
    this->addPin(this->iaqCore);
    //temperatureSensor
    this->temperatureSensor = new WTemperatureSensor(network);
    //pms7003
    this->pms = new WPms7003(this->network, this->clock, PIN_PMS_SLEEP);
    this->addPin(this->pms);

    //Pins
    pinMode(PIN_SWITCH_COVER, INPUT);
    pinMode(PIN_EXPANDER_RESET, OUTPUT);
    //IO expander
    this->expander = new WIOExpander(0x20);
    this->expander->setOnNotify(std::bind(&WPurifierDevice::onSwitchPressed, this, std::placeholders::_1, std::placeholders::_2));
    this->addPin(this->expander);

    //AQIs
    this->addProperty(this->pms->aqi);
    this->addProperty(this->pms->pm01);
    this->addProperty(this->pms->pm25);
    this->addProperty(this->pms->pm10);
    this->addProperty(this->pms->noOfSamples);
    this->addProperty(this->pms->lastUpdate);
    this->addProperty(this->iaqCore->co2);
    this->addProperty(this->iaqCore->co2Value);
    this->addProperty(this->iaqCore->tvoc);
    this->addProperty(this->iaqCore->tvocValue);
    //onOffProperty
    this->onOffProperty = WProperty::createOnOffProperty("on", "Switch");
    this->onOffProperty->setBoolean(true);
    this->onOffProperty->setOnChange(std::bind(&WPurifierDevice::onOnOffChanged, this, std::placeholders::_1));
    this->addProperty(this->onOffProperty);
    //fan mode
    this->fanMode = new WProperty("fanMode", "Fan", STRING, TYPE_FAN_MODE_PROPERTY);
    this->fanMode->addEnumString(FAN_MODE_OFF);
    this->fanMode->addEnumString(FAN_MODE_LOW);
    this->fanMode->addEnumString(FAN_MODE_MEDIUM);
    this->fanMode->addEnumString(FAN_MODE_HIGH);
    this->fanMode->setString(FAN_MODE_OFF);
    this->fanMode->setOnChange(std::bind(&WPurifierDevice::onFanModeChanged, this, std::placeholders::_1));
    this->addProperty(this->fanMode);
    //mode
    this->mode = new WProperty("mode", "Mode", STRING, TYPE_THERMOSTAT_MODE_PROPERTY);
    this->mode->addEnumString(MODE_MANUAL);
    this->mode->addEnumString(MODE_AUTO);
    //this->mode->setOnChange(std::bind(&WPurifierDevice::updateLeds, this));
    this->mode->setString(MODE_MANUAL);
    network->getSettings()->add(this->mode);
    this->addProperty(this->mode);
    //Initialize LEDs
    //StatusLEDs
    this->leds = new WStatusLeds(network, this->expander, this->pms->aqi, this->outsideAqiDevice->getAqi(), this->insideOutsideAqiStatus->getBoolean(),
                                 this->onOffProperty, this->fanMode, this->mode,
                                 this->iaqCore->co2, this->iaqCore->tvoc);
    this->addPin(this->leds);
    this->addProperty(this->leds->statusLedOn);

    //HtmlPages
    WPage* configPage = new WPage(this->getId(), "Configure air purifier");
    configPage->setPrintPage(std::bind(&WPurifierDevice::printConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    configPage->setSubmittedPage(std::bind(&WPurifierDevice::saveConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(configPage);
  }

  void resetExpander() {
    digitalWrite(PIN_EXPANDER_RESET, LOW);
    delay(100);
    digitalWrite(PIN_EXPANDER_RESET, HIGH);
    delay(100);
  }


  void loop(unsigned long now) {
    if ((this->mode->equalsString(MODE_AUTO)) && (!this->pms->aqi->isNull())) {
      int aqi = this->pms->aqi->getInteger();
      if (aqi < AQI_LIMIT_LOW) {
        this->fanMode->setString(FAN_MODE_OFF);
      } else if ((aqi >= AQI_LIMIT_LOW) && (aqi < AQI_LIMIT_MEDIUM)) {
        this->fanMode->setString(FAN_MODE_LOW);
      } else if ((aqi >= AQI_LIMIT_MEDIUM) && (aqi < AQI_LIMIT_HIGH)) {
        this->fanMode->setString(FAN_MODE_MEDIUM);
      } else if (aqi > AQI_LIMIT_HIGH) {
        this->fanMode->setString(FAN_MODE_HIGH);
      }
    }
    //PIN_SWITCH_FAN
    bool newCoverState = (digitalRead(PIN_SWITCH_COVER) == HIGH);
    if (newCoverState != expander->isCoverOpen()) {
      network->debug("cover open: %d", newCoverState);
      if (newCoverState) {
        /*network->debug("expander reset...");
        digitalWrite(PIN_EXPANDER_RESET, LOW);
        delay(100);
        digitalWrite(PIN_EXPANDER_RESET, HIGH);
        delay(100);
        expander->configureExpander();
        delay(200);*/
        digitalWrite(PIN_EXPANDER_RESET, HIGH);
      } else {
        digitalWrite(PIN_EXPANDER_RESET, HIGH);
      }
      expander->setCoverOpen(newCoverState);
      leds->setTouchPanelOn(expander->isCoverOpen());
    }

    WDevice::loop(now);
  }

  WOutsideAqiDevice* getOutsideAqiDevice() {
    return this->outsideAqiDevice;
  }

  WClock* getClock() {
    return this->clock;
  }

  WTemperatureSensor* getTemperatureSensor() {
    return this->temperatureSensor;
  }

  void printConfigPage(AsyncWebServerRequest* request, Print* page) {
    page->printf(HTTP_CONFIG_PAGE_BEGIN, getId());
    page->printf(HTTP_CHECKBOX_OPTION, "sa", "sa", (insideOutsideAqiStatus->getBoolean() ? HTTP_CHECKED : ""), "", "Show inside and outside AQI at status LED");
    page->printf(HTTP_CHECKBOX_OPTION, "ss", "ss", (switchStatusLedOffAtNight->getBoolean() ? HTTP_CHECKED : ""), "", "Switch status LED off during night");
    page->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
	}

	void saveConfigPage(AsyncWebServerRequest* request, Print* page) {
		network->notice(F("Save config page"));
		this->insideOutsideAqiStatus->setBoolean(request->arg("sa") == HTTP_TRUE);
    this->switchStatusLedOffAtNight->setBoolean(request->arg("ss") == HTTP_TRUE);
	}

protected:

  void onOnOffChanged(WProperty* property) {
    if ((this->onOffProperty) && (this->onOffProperty->getBoolean())) {
      resetExpander();
    }
    onFanModeChanged(property);
  }

  void onFanModeChanged(WProperty* property) {
    bool devOn = ((this->onOffProperty) && (this->onOffProperty->getBoolean()));
    expander->digitalWrite(PIN_Z, ((devOn) && (!this->fanMode->equalsString(FAN_MODE_OFF))));
    expander->digitalWrite(PIN_LOW, ((devOn) && ((this->fanMode->equalsString(FAN_MODE_LOW)) || (this->fanMode->equalsString(FAN_MODE_MEDIUM)))));
    expander->digitalWrite(PIN_MEDIUM, ((devOn) && this->fanMode->equalsString(FAN_MODE_MEDIUM)));
    expander->digitalWrite(PIN_HIGH, ((devOn) && this->fanMode->equalsString(FAN_MODE_HIGH)));
  }

  void onSwitchPressed(byte switchNo, bool pressed) {
    network->notice(F("Switch %d pressed."), switchNo);
    switch (switchNo) {
      case PIN_SWITCH_COVER:
        //updateLeds();
        break;
      case PIN_SWITCH_FAN:
        if (this->onOffProperty) {
          this->onOffProperty->setBoolean(true);
        }
        if (this->mode) {
          this->mode->setString(MODE_MANUAL);
        }
        if (fanMode->equalsString(FAN_MODE_OFF)) {
          this->fanMode->setString(FAN_MODE_LOW);
        } else if (fanMode->equalsString(FAN_MODE_LOW)) {
          this->fanMode->setString(FAN_MODE_MEDIUM);
        } else if (fanMode->equalsString(FAN_MODE_MEDIUM)) {
          this->fanMode->setString(FAN_MODE_HIGH);
        } else {
          this->fanMode->setString(FAN_MODE_OFF);
        }
        break;
      case PIN_SWITCH_AUTO:
        if (this->onOffProperty) {
          this->onOffProperty->setBoolean(true);
        }
        if (this->mode->equalsString(MODE_AUTO)) {
          this->mode->setString(MODE_MANUAL);
        } else {
          this->mode->setString(MODE_AUTO);
        }
        break;
      case PIN_SWITCH_WIFI:
        //not supported yet
        break;
    }
  }

private:
  WOutsideAqiDevice* outsideAqiDevice;
  WStatusLeds* leds;
  WIOExpander* expander;
  WPms7003* pms;
  WIaqCore* iaqCore;
  WClock* clock;
  WTemperatureSensor* temperatureSensor;
  WProperty* onOffProperty;
  WProperty* fanMode;
  WProperty* mode;
  WProperty* insideOutsideAqiStatus;
  WProperty* switchStatusLedOffAtNight;
};

#endif
