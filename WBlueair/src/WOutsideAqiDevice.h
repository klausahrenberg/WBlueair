#ifndef OUTSIDE_AQI_DEVICE_MCU_H
#define	OUTSIDE_AQI_DEVICE_MCU_H

#include "Arduino.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#elif ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#endif
#include <EEPROM.h>
#include "WDevice.h"

// Web Server address to read/write from
// Go to https://aqicn.org/data-platform/token/#/ to get your personal token.
// The @XXXX number is the index of the station whose data you want to read.
// To find your station's index, go to your desired station on aqicn.org,
// then view source and search for "idx" until you see some JSON that lists the
// idx as a 4 digit number.
//const char* request = "http://api.waqi.info/feed/@4485/?token=3b67fe9c9b6d1ea7d885c5824af57e3a0aa2ae48";
//const char *request = "http://api.waqi.info/feed/@1679/?token=3b67fe9c9b6d1ea7d885c5824af57e3a0aa2ae48";

HTTPClient http;

class WOutsideAqiDevice: public WDevice {
public:
  WOutsideAqiDevice(WNetwork* network)
  	: WDevice(network, "outsideaqi", "Outside Air Quality", DEVICE_TYPE_MULTI_LEVEL_SWITCH) {
    this->mainDevice = false;
    this->lastMeasure = 0;
    this->measureInterval = 300000;
    //Settings
    this->showAsWebthingDevice = network->getSettings()->setBoolean("showAsWebthingDevice", true);
    this->showAsWebthingDevice->setReadOnly(true);
		this->showAsWebthingDevice->setVisibility(NONE);
		this->addProperty(showAsWebthingDevice);
    this->setVisibility(this->showAsWebthingDevice->getBoolean() ? ALL : MQTT);
    this->stationIndex = network->getSettings()->setString("stationIndex", 6, "1679");
    this->stationIndex->setReadOnly(true);
		this->stationIndex->setVisibility(NONE);
    this->addProperty(this->stationIndex);
    this->apiToken = network->getSettings()->setString("apiToken", 45, "3b67fe9c9b6d1ea7d885c5824af57e3a0aa2ae48");
    this->apiToken->setReadOnly(true);
		this->apiToken->setVisibility(NONE);
    this->addProperty(this->apiToken);
    //HtmlPages
    WPage* configPage = new WPage(this->getId(), "Configure Outside Air Quality");
    configPage->setPrintPage(std::bind(&WOutsideAqiDevice::printConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    configPage->setSubmittedPage(std::bind(&WOutsideAqiDevice::saveConfigPage, this, std::placeholders::_1, std::placeholders::_2));
    network->addCustomPage(configPage);
    //Properties
    this->aqi = new WLevelIntProperty("aqi", "AQI", 0, 200);
    this->aqi->setReadOnly(true);
    this->addProperty(this->aqi);
    this->locale = WProperty::createStringProperty("name", "Locale", 64);
		this->locale->setReadOnly(true);
    this->addProperty(this->locale);
    this->updateTime = WProperty::createStringProperty("s", "Update time", 19);
		this->updateTime->setReadOnly(true);
		this->addProperty(this->updateTime);
  }

  void loop(unsigned long now) {
    WDevice::loop(now);
    if ((!this->apiToken->isNull()) && (!this->apiToken->equalsString(""))
        && (!this->stationIndex->isNull()) && (!this->stationIndex->equalsString(""))
        && ((lastMeasure == 0) || (now - lastMeasure > measureInterval))
        && (WiFi.status() == WL_CONNECTED)) {
      WStringStream* request = new WStringStream(128);
      request->printAndReplace(F("http://api.waqi.info/feed/@%s/?token=%s"), this->stationIndex->c_str(), this->apiToken->c_str());
      network->notice(F("Outside AQI update via '%s'"), request->c_str());

      http.begin(request->c_str());
      int httpCode = http.GET();
      if (httpCode > 0) {
        WJsonParser parser;
        this->aqi->setReadOnly(false);
        this->locale->setReadOnly(false);
        this->updateTime->setReadOnly(false);
        WProperty* property = parser.parse(http.getString().c_str(), this);
        this->aqi->setReadOnly(true);
        this->locale->setReadOnly(true);
        this->updateTime->setReadOnly(true);
        if (property != nullptr) {
          lastMeasure = millis();
          network->notice(F("Outside AQI evaluated. Current value: %d"), this->aqi->getInteger());
        }
      } else {
        network->error(F("Outside AQI update failed: %s)"), httpCode);
      }
      http.end();
      delete request;
    }
  }

  void printConfigPage(AsyncWebServerRequest* request, Print* page) {
    page->printf(HTTP_CONFIG_PAGE_BEGIN, getId());
    page->printf(HTTP_CHECKBOX_OPTION, "sa", "sa", (showAsWebthingDevice->getBoolean() ? HTTP_CHECKED : ""), "", "Show as Mozilla Webthing device");
    page->printf(HTTP_TEXT_FIELD, "Station Index:", "si", "8", stationIndex->c_str());
    page->printf(HTTP_TEXT_FIELD, "waqi.info API token:", "at", "64", apiToken->c_str());
    page->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
	}

	void saveConfigPage(AsyncWebServerRequest* request, Print* page) {
		network->notice(F("Save config page"));
		this->showAsWebthingDevice->setBoolean(request->arg("sa") == HTTP_TRUE);
    this->stationIndex->setString(request->arg("si").c_str());
    this->apiToken->setString(request->arg("at").c_str());
	}

  WProperty* getAqi() {
    return this->aqi;
  }

private:
  WProperty* showAsWebthingDevice;
  WProperty* stationIndex;
  WProperty* apiToken;
  WProperty* aqi;
  WProperty* locale;
  WProperty* updateTime;
  unsigned long lastMeasure, measureInterval;
};

#endif
