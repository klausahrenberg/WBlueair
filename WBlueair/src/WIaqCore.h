#include "Arduino.h"
#include <ESP8266WiFi.h>
#include "Wire.h"
#include "iAQcore.h"

#define AVERAGE_COUNTS 2

const char* LEVEL_EXCELLENT = "Excellent";
const char* LEVEL_GOOD = "Good";
const char* LEVEL_MODERATE = "Moderate";
const char* LEVEL_POOR = "Poor";
const char* LEVEL_UNHEALTHY = "Unhealty";

class WIaqCore: public WPin {
public:
  WIaqCore(WNetwork* network) :
			WPin(NO_PIN, INPUT) {
    this->network = network;
    lastMeasure = 0;
		measureValueCo2 = 0;
		measureValueTvoc = 0;
		measureCounts = 0;
		measuring = false;
    measureInterval = 60000;
    this->co2Value = WProperty::createUnsignedLongProperty("co2Value", "co2Value");
    this->co2Value->setReadOnly(true);
    this->co2Value->setVisibility(MQTT);
    this->co2 = WProperty::createStringProperty("co2", "CO2", 9);
    this->co2->setReadOnly(true);
    this->co2->addEnumString(LEVEL_EXCELLENT);
    this->co2->addEnumString(LEVEL_GOOD);
    this->co2->addEnumString(LEVEL_MODERATE);
    this->co2->addEnumString(LEVEL_POOR);
    this->co2->addEnumString(LEVEL_UNHEALTHY);
    this->tvocValue = WProperty::createUnsignedLongProperty("tvocValue", "tvocValue");
    this->tvocValue->setReadOnly(true);
    this->tvocValue->setVisibility(MQTT);
    this->tvoc = WProperty::createStringProperty("tvoc", "TVOC", 9);
    this->tvoc->setReadOnly(true);
    this->tvoc->addEnumString(LEVEL_EXCELLENT);
    this->tvoc->addEnumString(LEVEL_GOOD);
    this->tvoc->addEnumString(LEVEL_MODERATE);
    this->tvoc->addEnumString(LEVEL_POOR);
    this->tvoc->addEnumString(LEVEL_UNHEALTHY);
    // Enable iAQ-Core
    iaq = new iAQcore();
    Wire.begin(D2, D1);
    Wire.setClockStretchLimit(1000);
    initialized = iaq->begin();
    if (!initialized) {
      this->network->error(F("Could not access iAQ-Core chip."));
    }
  }

  void loop(unsigned long now) {
    if ((initialized) && (((measuring) && (now - lastMeasure > 1000)) || (lastMeasure == 0)
				|| (now - lastMeasure > measureInterval))) {
			lastMeasure = now;
      uint16_t eco2;
      uint16_t stat;
      uint32_t resist;
      uint16_t etvoc;
      iaq->read(&eco2, &stat, &resist, &etvoc);
			if ((eco2 > 0) && (etvoc > 0)) {
				measureValueCo2 = measureValueCo2 + eco2;
				measureValueTvoc = measureValueTvoc + etvoc;
				measureCounts++;
				measuring = (measureCounts < AVERAGE_COUNTS);
				if (!measuring) {
          co2Value->setUnsignedLong((int) round((double) measureValueCo2 / (double) measureCounts));
          tvocValue->setUnsignedLong((int) round((double) measureValueTvoc / (double) measureCounts));
          updateCo2AndTvocRating();
					measureValueCo2 = 0;
					measureValueTvoc = 0;
					measureCounts = 0;
				}
			}
		}
  }

  WProperty* co2Value;
  WProperty* tvocValue;
  WProperty* co2;
  WProperty* tvoc;
private:
  WNetwork* network;
  iAQcore* iaq;
  unsigned long lastMeasure, measureInterval;
	bool measuring, initialized;
	int measureCounts;
	double measureValueCo2;
	double measureValueTvoc;

  void updateCo2AndTvocRating() {
    if (!co2Value->isNull()) {
      //1 Excellent 250-400ppm 	Normal background concentration in outdoor ambient air
      //2 Good 400-1,000ppm 	Concentrations typical of occupied indoor spaces with good air exchange
      //3 Moderate 1,000-2,000ppm 	Complaints of drowsiness and poor air.
      //4 Poor 2,000-5,000 ppm 	Headaches, sleepiness and stagnant, stale, stuffy air. Poor concentration, loss of attention, increased heart rate and slight nausea may also be present.
      //5 Unhealty 5,000 	Workplace exposure limit (as 8-hour TWA) in most jurisdictions.
      //6 >40,000 ppm 	Exposure may lead to serious oxygen deprivation resulting in permanent brain damage, coma, even death.
      if (co2Value->isUnsignedLongBetween(0, 400)) {
        co2->setString(LEVEL_EXCELLENT);
      } else if (co2Value->isUnsignedLongBetween(400, 1000)) {
        co2->setString(LEVEL_GOOD);
      } else if (co2Value->isUnsignedLongBetween(1000, 2000)) {
        co2->setString(LEVEL_MODERATE);
      } else if (co2Value->isUnsignedLongBetween(2000, 5000)) {
        co2->setString(LEVEL_POOR);
      } else {
        co2->setString(LEVEL_UNHEALTHY);
      }
    } else {
      co2->setString("");
    }
    if (!tvocValue->isNull()) {
      //1 Excellent No objectionsTarget value no limit 0 – 65
      //2 Good No Ventilation recommended 65 – 220
      //3 Moderate Intensified ventilation recommended 220 – 660
      //4 Poor Intensified ventilation necessary 660 – 2200
      //5 Unhealty Situation not acceptable 2200 – 5500
      if (tvocValue->isUnsignedLongBetween(0, 65)) {
        tvoc->setString(LEVEL_EXCELLENT);
      } else if (tvocValue->isUnsignedLongBetween(65, 220)) {
        tvoc->setString(LEVEL_GOOD);
      } else if (tvocValue->isUnsignedLongBetween(220, 660)) {
        tvoc->setString(LEVEL_MODERATE);
      } else if (tvocValue->isUnsignedLongBetween(660, 2200)) {
        tvoc->setString(LEVEL_POOR);
      } else {
        tvoc->setString(LEVEL_UNHEALTHY);
      }
    } else {
      tvoc->setString("");
    }
  }
};
