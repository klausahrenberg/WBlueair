#ifndef WTEMPERATURESENSOR_H_
#define WTEMPERATURESENSOR_H_

#include "Arduino.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#elif ESP32
#include <WiFi.h>
#endif
#include "WDevice.h"
#include "HTU21D.h"

#define AVERAGE_COUNTS 2
#define CORRECTION_TEMPERATURE 0.0
#define CORRECTION_HUMIDITY 0.0

class WTemperatureSensor: public WDevice {
public:
	WTemperatureSensor(WNetwork* network)
		: WDevice(network, "temperature", "Temperature Sensor", DEVICE_TYPE_TEMPERATURE_SENSOR) {
		this->setMainDevice(false);
		lastMeasure = 0;
		measureValueTemperature = 0;
		measureValueHumidity = 0;
		measureCounts = 0;
		measuring = false;
		this->temperature = WProps::createTemperatureProperty("temperature", "Actual");
		this->temperature->readOnly(true);
		this->addProperty(temperature);
		this->humidity = WProps::createLevelProperty("humidity", "Humidity", 0.0, 140.0);
		this->humidity->readOnly(true);
		this->humidity->multipleOf(0.1);
		this->humidity->unit("%");
		this->addProperty(humidity);
		measureInterval = 60000;
		dht = new HTU21D();
		dht->begin();
	}

	void loop(unsigned long now) {
		//Measure temperature
		if (((measuring) && (now - lastMeasure > 1000)) || (lastMeasure == 0)
				|| (now - lastMeasure > measureInterval)) {
			lastMeasure = now;
			double t = dht->readTemperature();
			double h = dht->readHumidity();
			if ((!isnan(t)) && (t > -50) && (t < 120) && (!isnan(h))
					&& (h > 0.0) && (h < 200)) {
				measureValueTemperature = measureValueTemperature + t;
				measureValueHumidity = measureValueHumidity + h;
				measureCounts++;
				measuring = (measureCounts < AVERAGE_COUNTS);
				if (!measuring) {
					setActualValues(
							(double) (round(
									measureValueTemperature * 10.0
											/ (double) measureCounts)
									/ 10.0) + CORRECTION_TEMPERATURE,
							(double) (round(
									measureValueHumidity * 10.0
											/ (double) measureCounts)
									/ 10.0) + CORRECTION_HUMIDITY);
					measureValueTemperature = 0;
					measureValueHumidity = 0;
					measureCounts = 0;
				}
			}
		}
	}

	double getTemperature() {
		return temperature->asDouble();
	}

	double getHumidity() {
		return humidity->asDouble();
	}

	int getMeasureInterval() {
		return measureInterval;
	}

	/*void getMqttState(JsonObject& json) {
		json["temperature"] = getTemperature();
		json["humidity"] = getHumidity();
		//json["measureInterval"] = getMeasureInterval();
	}*/

	bool isDeviceStateComplete() {
		return ((!this->temperature->isNull()) && (!this->humidity->isNull()));
	}

private:
	HTU21D *dht;
	unsigned long lastMeasure, measureInterval;
	bool measuring;
	int measureCounts;
	double measureValueTemperature;
	double measureValueHumidity;
	WProperty* temperature;
	WProperty* humidity;

	void setActualValues(double temperature, double humidity) {
		if ((this->getTemperature() != temperature) || (this->getHumidity() != humidity)) {
			this->temperature->asDouble(temperature);
			this->humidity->asDouble(humidity);

		}
	}

};

#endif /* WTEMPERATURESENSOR_H_ */
