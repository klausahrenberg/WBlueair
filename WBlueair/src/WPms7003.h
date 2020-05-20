#ifndef W_PMS_7003_H
#define W_PMS_7003_H

#include "WPin.h"
#include "WClock.h"
#include "Plantower_PMS7003.h"

#define MEASUREMENTS_MAX 12
#define MEASUREMENTS_MIN 4
#define READ_TIMEOUT 14000
#define CORRECTION_PM_01 0.0
#define CORRECTION_PM_25 0.0
#define CORRECTION_PM_10 0.0

class WPms7003: public WPin {
public:
	WPms7003(WNetwork* network, WClock* clock, int sleepPin) :
			WPin(sleepPin, OUTPUT) {
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
    this->aqi = new WLevelIntProperty("aqi", "AQI", 0, 200);
    this->aqi->setReadOnly(true);
    this->pm01 = new WLevelIntProperty("pm01", "PM 1.0", 0, 200);
    this->pm01->setReadOnly(true);
		this->pm01->setVisibility(MQTT);
    this->pm25 = new WLevelIntProperty("pm25", "PM 2.5", 0, 200);
    this->pm25->setReadOnly(true);
		this->pm25->setVisibility(MQTT);
    this->pm10 = new WLevelIntProperty("pm10", "PM 10", 0, 200);
    this->pm10->setReadOnly(true);
		this->pm10->setVisibility(MQTT);
		this->noOfSamples = WProperty::createIntegerProperty("noOfSamples", "noOfSamples");
		this->noOfSamples->setReadOnly(true);
		this->noOfSamples->setVisibility(MQTT);
		this->lastUpdate = WProperty::createStringProperty("lastUpdate", "lastUpdate", 19);
		this->lastUpdate->setReadOnly(true);
		this->lastUpdate->setVisibility(MQTT);
  }

  void loop(unsigned long now) {
		pms7003->updateFrame();
		if ((!measuring) && ((lastMeasure == 0) || (now - lastMeasure > measureInterval))) {
			//network->notice(F("Start measuring..."));
    	lastMeasure = now;
			digitalWrite(this->getPin(), HIGH);
			pms7003->requestRead();
			lastSign = now;
			measuring = true;
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
					this->pm01->setInteger(measureValuePm01);
    			this->pm25->setInteger(measureValuePm25);
    			this->pm10->setInteger(measureValuePm10);
    			this->aqi->setInteger(max(max(this->pm01->getInteger(), this->pm25->getInteger()), this->pm10->getInteger()));
					this->noOfSamples->setInteger(measureCounts);
					this->lastUpdate->setString(clock->isValidTime() ? clock->getEpochTimeFormatted()->c_str() : "");					
				}
			} else {
				network->error(F("Timeout reading AQI sensor"));
			}
    	measureValuePm01 = 0;
    	measureValuePm25 = 0;
    	measureValuePm10 = 0;
			measureCounts = 0;
	  	measuring = false;
			digitalWrite(this->getPin(), LOW);
		}
  }

  WProperty* aqi;
  WProperty* pm01;
  WProperty* pm25;
  WProperty* pm10;
	WProperty* noOfSamples;
	WProperty* lastUpdate;
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
};

#endif
