#include "Arduino.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif
#include "Wire.h"

#define IAQ_ADDR	0x5A
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
  }

  void loop(unsigned long now) {
    if ((initialized) && (((measuring) && (now - lastMeasure > 1000)) || (lastMeasure == 0)
				|| (now - lastMeasure > measureInterval))) {
			lastMeasure = now;

      readRegisters();
      uint16_t eco2 = getPrediction();
      uint16_t etvoc = getTVOC();

			if ((eco2 > 0) && (etvoc > 0)) {
        network->debug(F("IAQ measure sample %d: CO2 %d, TVOC %d"), measureCounts, eco2, etvoc);
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
  //iAQcore* iaq;
  unsigned long lastMeasure, measureInterval;
	bool measuring, initialized;
	int measureCounts;
	double measureValueCo2;
	double measureValueTvoc;
  uint8_t data[9];

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

  void readRegisters() {
  	int i = 0;
  	Wire.requestFrom(IAQ_ADDR, 9);
  	while (Wire.available()) {
  		data[i] = Wire.read();
  		i++;
  	}
  }

  /* Calculate CO2 prediction*/
  uint16_t getPrediction() {
  	return (uint16_t)((data[0] << 8) + data[1]);
  }

  /* Calculate sensor resistance in ohms*/
  int32_t getResistance() {
  	return (int32_t)(data[3] << 24) + (int32_t)(data[4] << 16) + (int32_t)(data[5] << 8) + (int32_t)data[6];
  }

  /* Calculate TVOC prediction*/
  uint16_t getTVOC() {
  	return (uint16_t)(data[7] << 8) + (uint16_t)(data[8]);
  }

  /* Get status of data
  OK - data is valid
  RUNIN - warm-up phase
  BUSY - data integrity >8 bits not guaranteed
  ERROR - sensor possibly defective
  UNRECOGNIZED DATA - physical connection error*/
  char* getStatus() {
  	if (data[2] == 0x00)
  		return "OK";
  	else if (data[2] == 0x10)
  		return "RUNIN";
  	else if (data[2] == 0x01)
  		return "BUSY";
  	else if (data[2] == 0x80)
  		return "ERROR";
  	else
  		return "UNRECOGNIZED DATA";
  }

  uint8_t getStatusByte() {
  	return data[2];
  }

};
