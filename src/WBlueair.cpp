#include <Arduino.h>
#include "WNetwork.h"
#include "WPurifierDevice.h"
#include "WHtmlStatePage.h"

#define APPLICATION "Air Purifier"
#define VERSION "1.20"
#define FLAG_SETTINGS 0x20
#define DEBUG false


WNetwork* network;
WPurifierDevice* baDevice;
WHtmlStatePage* statePage;

void setup() {
  Serial.begin(9600);
	network = new WNetwork(DEBUG, APPLICATION, VERSION, NO_LED, FLAG_SETTINGS);

	baDevice = new WPurifierDevice(network);

  network->addDevice(baDevice);
  network->addDevice(baDevice->getClock());
  network->addDevice(baDevice->getTemperatureSensor());
  network->addDevice(baDevice->outsideAqi());

  statePage = new WHtmlStatePage(network, baDevice);
}

void loop() {
  network->loop(millis());
	delay(100);
}
