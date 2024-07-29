#ifndef W_PMS_7003_H
#define W_PMS_7003_H

#include "WOutput.h"
#include "WClock.h"
#include "Plantower_PMS7003.h"

#define MEASUREMENTS_MAX 12
#define MEASUREMENTS_MIN 4
#define READ_TIMEOUT 14000
#define CORRECTION_PM_01 0.0
#define CORRECTION_PM_25 0.0
#define CORRECTION_PM_10 0.0

class WPms7003: public WOutput {
public:
	WPms7003(WNetwork* network, WClock* clock, int sleepPin) :
			WOutput(sleepPin) {
    this->network = network;
		this->clock = clock;
    this->lastMeasure = 0;
		this->lastSign = 0;
    this->measureValuePm01 = 0;
    this->measureValuePm25 = 0;
    this->measureValuePm10 = 0;
    this->measureCounts = 0;
    this->measuring = false;
    this->updateNotify = false;
    this->measureInterval = 300000;
		this->pms7003 = new Plantower_PMS7003();
    this->pms7003->init(&Serial);
		this->failStatusSent = false;
    _aqi = WProps::createLevelIntProperty("aqi", "AQI", 0, 200);
    _aqi->readOnly(true);
    _pm01 = WProps::createLevelIntProperty("pm01", "PM 1.0", 0, 200);
    _pm01->readOnly(true);
		_pm01->visibility(MQTT);
    _pm25 = WProps::createLevelIntProperty("pm25", "PM 2.5", 0, 200);
    _pm25->readOnly(true);
		_pm25->visibility(MQTT);
    _pm10 = WProps::createLevelIntProperty("pm10", "PM 10", 0, 200);
    _pm10->readOnly(true);
		_pm10->visibility(MQTT);
		_noOfSamples = WProps::createIntegerProperty("noOfSamples", "noOfSamples");
		_noOfSamples->readOnly(true);
		_noOfSamples->visibility(MQTT);
		_lastUpdate = WProps::createStringProperty("lastUpdate", "lastUpdate");
		_lastUpdate->readOnly(true);
		_lastUpdate->visibility(MQTT);
  }

  void loop(unsigned long now) {
		if ((!measuring) && ((lastMeasure == 0) || (now - lastMeasure > measureInterval))) {
			network->notice(F("Start measuring..."));
    	lastMeasure = now;
			digitalWrite(this->pin(), HIGH);
			pms7003->requestRead();
			lastSign = now;
			measuring = true;
		}
		if (measuring) {
			pms7003->updateFrame();
		}
		if (pms7003->hasNewData()) {
  		measureValuePm01 = max(measureValuePm01, (int) pms7003->getPM_1_0());
    	measureValuePm25 = max(measureValuePm25, (int) pms7003->getPM_2_5());
    	measureValuePm10 = max(measureValuePm10, (int) pms7003->getPM_10_0());
			measureCounts++;
			network->debug(F("Measure sample %d: PM1.0 %d, PM2.5 %d, PM10 %d [ug/m3]"), measureCounts, measureValuePm01, measureValuePm25, measureValuePm10);
			lastSign = now;
			measuring = true;
			//switch device off, however, there comes some more measurements afterwards
    	//digitalWrite(this->getPin(), LOW);
		} else if ((measuring) && ((now - lastSign > READ_TIMEOUT) || (measureCounts >= MEASUREMENTS_MAX))) {
			//Timeout, End Reading
			network->notice(F("Measurement finished. %d"), measureCounts);
			lastMeasure = now;
			if (measureCounts > 0) {
				if (measureCounts >= MEASUREMENTS_MIN) {
					_pm01->asInt(measureValuePm01);
    			_pm25->asInt(measureValuePm25);
    			_pm10->asInt(measureValuePm10);
    			_aqi->asInt(max(max(measureValuePm01, measureValuePm25), measureValuePm10));
					_noOfSamples->asInt(measureCounts);
					_lastUpdate->asString(clock->isValidTime() ? clock->epochTimeFormatted()->c_str() : "");
				}
			} else {
				network->error(F("Timeout reading AQI sensor"));
			}
    	measureValuePm01 = 0;
    	measureValuePm25 = 0;
    	measureValuePm10 = 0;
			measureCounts = 0;
	  	measuring = false;
			digitalWrite(this->pin(), LOW);
		}
  }

  WProperty* aqi() { return _aqi; }
	WProperty* pm01() { return _pm01; }
	WProperty* pm25() { return _pm25; }
	WProperty* pm10() { return _pm10; }
	WProperty* noOfSamples() { return _noOfSamples; }
	WProperty* lastUpdate() { return _lastUpdate; }
protected:

private:
  WNetwork* network;
	WClock* clock;
	Plantower_PMS7003* pms7003;
	bool failStatusSent;
  unsigned long lastMeasure, measureInterval, lastSign;
  bool measuring, updateNotify;
  int measureCounts;
  int measureValuePm01, measureValuePm25, measureValuePm10;
	WProperty* _aqi;
  WProperty* _pm01;
  WProperty* _pm25;
  WProperty* _pm10;
	WProperty* _noOfSamples;
	WProperty* _lastUpdate;
};

#endif
