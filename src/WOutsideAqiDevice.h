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
    this->setMainDevice(false);
    this->lastMeasure = 0;
    this->measureInterval = 300000;
    //Settings
    this->showAsWebthingDevice = network->settings()->setBoolean("showAsWebthingDevice", true);
    this->showAsWebthingDevice->readOnly(true);
		this->showAsWebthingDevice->visibility(NONE);
		this->addProperty(showAsWebthingDevice);
    this->setVisibility(this->showAsWebthingDevice->asBool() ? ALL : MQTT);
    this->stationIndex = network->settings()->setString("stationIndex", "1679");
    this->stationIndex->readOnly(true);
		this->stationIndex->visibility(NONE);
    this->addProperty(this->stationIndex);
    this->apiToken = network->settings()->setString("apiToken", "3b67fe9c9b6d1ea7d885c5824af57e3a0aa2ae48");
    this->apiToken->readOnly(true);
		this->apiToken->visibility(NONE);
    this->addProperty(this->apiToken);
    //HtmlPages
    WPage* configPage = new WPage(network, this->id(), "Configure Outside Air Quality");
    configPage->onPrintPage(std::bind(&WOutsideAqiDevice::printConfigPage, this, std::placeholders::_1));
    configPage->onSubmitPage(std::bind(&WOutsideAqiDevice::submitConfigPage, this, std::placeholders::_1));
    network->addCustomPage(configPage);
    //Properties
    _aqi = WProps::createLevelIntProperty("aqi", "AQI", 0, 200);
    _aqi->readOnly(true);
    this->addProperty(_aqi);
    _locale = WProps::createStringProperty("name", "Locale");
		_locale->readOnly(true);
    this->addProperty(_locale);
    _updateTime = WProps::createStringProperty("s", "Update time");
		_updateTime->readOnly(true);
		this->addProperty(_updateTime);
  }

  void loop(unsigned long now) {
    WDevice::loop(now);
    if ((!this->apiToken->isNull()) && (!this->apiToken->equalsString(""))
        && (!this->stationIndex->isNull()) && (!this->stationIndex->equalsString(""))
        && ((lastMeasure == 0) || (now - lastMeasure > measureInterval))
        && (WiFi.status() == WL_CONNECTED)) {
      WStringStream* request = new WStringStream(128);
      request->printAndReplace(F("http://api.waqi.info/feed/@%s/?token=%s"), this->stationIndex->c_str(), this->apiToken->c_str());
      network()->notice(F("Outside AQI update via '%s'"), request->c_str());

      http.begin(request->c_str());
      int httpCode = http.GET();
      if (httpCode > 0) {
        WJsonParser parser;
        _aqi->readOnly(false);
        _locale->readOnly(false);
        _updateTime->readOnly(false);
        WProperty* property = parser.parse(http.getString().c_str(), this);
        _aqi->readOnly(true);
        _locale->readOnly(true);
        _updateTime->readOnly(true);
        if (property != nullptr) {
          lastMeasure = millis();
          network()->notice(F("Outside AQI evaluated. Current value: %d"), _aqi->asInt());
        }
      } else {
        network()->error(F("Outside AQI update failed: %s)"), httpCode);
      }
      http.end();
      delete request;
    }
  }

  void printConfigPage(WPage* page) {
    HTTP_CONFIG_PAGE_BEGIN(page->stream(), id());
    page->stream()->printf(HTTP_CHECKBOX_OPTION, "sa", "sa", (showAsWebthingDevice->asBool() ? HTTP_CHECKED : ""), "", "Show as Mozilla Webthing device");
    page->stream()->printf(HTTP_TEXT_FIELD, "Station Index:", "si", "8", stationIndex->c_str());
    page->stream()->printf(HTTP_TEXT_FIELD, "waqi.info API token:", "at", "64", apiToken->c_str());
    page->stream()->print(FPSTR(HTTP_CONFIG_SAVE_BUTTON));
	}

	void submitConfigPage(AsyncWebServerRequest* request) {
		network()->notice(F("Save config page"));
		this->showAsWebthingDevice->asBool(request->arg("sa") == HTTP_TRUE);
    this->stationIndex->asString(request->arg("si").c_str());
    this->apiToken->asString(request->arg("at").c_str());
	}

  WProperty* aqi() { return _aqi; }

  WProperty* locale() { return _locale; }

  WProperty* updateTime() { return _updateTime; }

private:
  WProperty* showAsWebthingDevice;
  WProperty* stationIndex;
  WProperty* apiToken;
  WProperty* _aqi;
  WProperty* _locale;
  WProperty* _updateTime;
  unsigned long lastMeasure, measureInterval;
};

#endif
