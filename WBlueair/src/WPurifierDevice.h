#ifndef PURIFIER_DEVICE_MCU_H
#define	PURIFIER_DEVICE_MCU_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include "WOutsideAqiDevice.h"
#include "WStatusLeds.h"
#include "WPms7003.h"
#include "WIaqCore.h"
#include "WClock.h"
#include "WTemperatureSensor.h"

#define PIN_PMS_SLEEP 14 //D5
#define AQI_LIMIT_LOW 10
#define AQI_LIMIT_MEDIUM 30
#define AQI_LIMIT_HIGH 60

class WPurifierDevice: public WDevice {
public:
  typedef std::function<bool()> THandlerFunction;

  WPurifierDevice(WNetwork* network)
  	: WDevice(network, "airpurifier", "Air Purifier", DEVICE_TYPE_MULTI_LEVEL_SWITCH) {
    this->setFromAutoMode = false;
    //outside AQI device
    //network->setOnNotify(std::bind(&WPurifierDevice::updateLeds, this));
    this->outsideAqiDevice = new WOutsideAqiDevice(network);
    this->insideOutsideAqiStatus = network->getSettings()->setBoolean("insideOutsideAqiStatus", true);
    this->insideOutsideAqiStatus->setReadOnly(true);
		this->insideOutsideAqiStatus->setVisibility(NONE);
    this->addProperty(insideOutsideAqiStatus);
    //clock
    this->clock = new WClock(network);
    //IAQ Core
    this->iaqCore = new WIaqCore(this->network);
    this->addPin(this->iaqCore);
    //temperatureSensor
    this->temperatureSensor = new WTemperatureSensor(network);
    //pms7003
    this->pms = new WPms7003(this->network, this->clock, PIN_PMS_SLEEP);
    this->addPin(this->pms);

    //IO expander
    this->expander = new WMCP23017Expander(0x20);
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
    //fan mode
    this->fanMode = new WProperty("fanMode", "Fan", STRING, TYPE_FAN_MODE_PROPERTY);
    this->fanMode->addEnumString(FAN_MODE_OFF);
    this->fanMode->addEnumString(FAN_MODE_LOW);
    this->fanMode->addEnumString(FAN_MODE_MEDIUM);
    this->fanMode->addEnumString(FAN_MODE_HIGH);
    this->fanMode->setOnChange(std::bind(&WPurifierDevice::onFanModeChanged, this, std::placeholders::_1));
    this->fanMode->setString(FAN_MODE_OFF);
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
    this->leds = new WStatusLeds(network, this->expander, this->pms->aqi, this->outsideAqiDevice->getAqi(), this->insideOutsideAqiStatus->getBoolean(), this->fanMode, this->mode,
                                 this->iaqCore->co2, this->iaqCore->tvoc);
    this->addPin(this->leds);
    this->addProperty(this->leds->statusLedOn);
    //HtmlPages
    WPage* configPage = new WPage(this->getId(), "Configure air purifier");
    configPage->setPrintPage(std::bind(&WPurifierDevice::printConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    configPage->setSubmittedPage(std::bind(&WPurifierDevice::saveConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(configPage);
  }


  void loop(unsigned long now) {
    if ((this->mode->equalsString(MODE_AUTO)) && (!this->pms->aqi->isNull())) {
      int aqi = this->pms->aqi->getInteger();
      this->setFromAutoMode = true;
      if (aqi < AQI_LIMIT_LOW) {
        this->fanMode->setString(FAN_MODE_OFF);
      } else if ((aqi >= AQI_LIMIT_LOW) && (aqi < AQI_LIMIT_MEDIUM)) {
        this->fanMode->setString(FAN_MODE_LOW);
      } else if ((aqi >= AQI_LIMIT_MEDIUM) && (aqi < AQI_LIMIT_HIGH)) {
        this->fanMode->setString(FAN_MODE_MEDIUM);
      } else if (aqi > AQI_LIMIT_HIGH) {
        this->fanMode->setString(FAN_MODE_HIGH);
      }
      this->setFromAutoMode = false;
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

  void printConfigPage(ESP8266WebServer* webServer, WStringStream* page) {
    page->printAndReplace(FPSTR(HTTP_CONFIG_PAGE_BEGIN), getId());
    page->printAndReplace(FPSTR(HTTP_CHECKBOX_OPTION), "sa", "sa", (insideOutsideAqiStatus->getBoolean() ? HTTP_CHECKED : ""), "", "Show inside and outside AQI at status LED");
    page->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
	}

	void saveConfigPage(ESP8266WebServer* webServer, WStringStream* page) {
		network->notice(F("Save config page"));
		this->insideOutsideAqiStatus->setBoolean(webServer->arg("sa") == HTTP_TRUE);
	}

protected:

  void onFanModeChanged(WProperty* property) {
    if ((this->mode) && (!this->setFromAutoMode)) {
      this->mode->setString(MODE_MANUAL);
    }
    expander->digitalWrite(PIN_Z, (!this->fanMode->equalsString(FAN_MODE_OFF)));
    expander->digitalWrite(PIN_LOW, (this->fanMode->equalsString(FAN_MODE_LOW)) || (this->fanMode->equalsString(FAN_MODE_MEDIUM)));
    expander->digitalWrite(PIN_MEDIUM, this->fanMode->equalsString(FAN_MODE_MEDIUM));
    expander->digitalWrite(PIN_HIGH, this->fanMode->equalsString(FAN_MODE_HIGH));
  }

  void onSwitchPressed(byte switchNo, bool pressed) {
    leds->setTouchPanelOn(expander->isCoverOpen());
    switch (switchNo) {
      case PIN_SWITCH_COVER:
        //updateLeds();
        break;
      case PIN_SWITCH_FAN:
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
  WMCP23017Expander* expander;
  WPms7003* pms;
  WIaqCore* iaqCore;
  WClock* clock;
  WTemperatureSensor* temperatureSensor;
  WProperty* fanMode;
  WProperty* mode;
  WProperty* insideOutsideAqiStatus;
  bool setFromAutoMode;
};

#endif
