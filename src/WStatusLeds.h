#ifndef W_STATUS_LEDS_H
#define W_STATUS_LEDS_H

#include <FastLED.h>
#include "WProperty.h"
#include "WOutput.h"

#ifdef ESP8266
#define DATA_PIN D4
#elif ESP32
#define DATA_PIN 32
#endif
#define LED_PM 0
#define LED_VOC 1
#define LED_AUTO 2
#define LED_FILTER 3
#define LED_WIFI 4
#define LED_STATUS 5

const float PI180 = 0.01745329;
const int NUM_LEDS = 6;
const CRGB DEFAULT_COLOR = CRGB(10, 30, 20);
const byte DEFAULT_BRIGHTNESS = 160;
const unsigned long BLINK_DURATION = 600;
const unsigned long CYCLE_DURATION = 20000;
const char* FAN_MODE_OFF = "off";
const char* FAN_MODE_LOW  = "low";
const char* FAN_MODE_MEDIUM  = "medium";
const char* FAN_MODE_HIGH = "high";
const char* MODE_MANUAL = "manual";
const char* MODE_AUTO = "auto";

struct WSLed {
	WSLed() : on(false), blinking(false), color(DEFAULT_COLOR) {}
	bool on;
	bool blinking;
	CRGB color;
};

class WStatusLeds: public WOutput {
public:
	WStatusLeds(WNetwork* network, WIOExpander* expander, WProperty* aqi, WProperty* outsideAqi, bool insideOutsideAqiStatus,
		          WProperty* onOffProperty, WProperty* fanMode, WProperty* mode, WProperty* co2, WProperty* tvoc) :
			WOutput(DATA_PIN) {
		this->network = network;
		this->expander = expander;
		this->aqi = aqi;
		this->outsideAqi = outsideAqi;
		this->insideOutsideAqiStatus = insideOutsideAqiStatus;
		this->onOffProperty = onOffProperty;
		this->fanMode = fanMode;
		this->mode = mode;
		this->co2 = co2;
		this->tvoc = tvoc;
		this->brightness = 160;
		this->touchPanelOn = false;
		this->statusLedOn = WProps::createBooleanProperty("statusLedOn", "Status LED");
		this->statusLedOn->asBool(true);
		this->network->settings()->add(this->statusLedOn);
		this->lastBlinkOn = 0;
		this->lastCycle = 0;
		this->cycleFactor = 0;
		this->blinkOn = false;
		this->lastPulse = nullptr;
		//initialize FastLED
		FastLED.addLeds<WS2812, DATA_PIN, GRB>(fastLeds, NUM_LEDS);
		FastLED.setBrightness(DEFAULT_BRIGHTNESS);
	}

	void setTouchPanelOn(bool touchPanelOn) {
		this->touchPanelOn = touchPanelOn;
	}

	void loop(unsigned long now) {

		if (insideOutsideAqiStatus) {
			//Calculate cycle factor 0.. 1.0
			if ((this->aqi != nullptr) && (!this->aqi->isNull()) && (this->outsideAqi != nullptr) && (!this->outsideAqi->isNull())) {
				if (lastCycle == 0) {
					lastCycle = now;
				}
				int f = now -lastCycle;
				if (f >= CYCLE_DURATION) {
					f = f - CYCLE_DURATION;
					lastCycle = now;
				}
				f = round(((float) f / (float) CYCLE_DURATION) * 360.0f);
				cycleFactor = sin((float)(f - 90) * PI180);
				cycleFactor = (cycleFactor + 1) / 2;
			} else {
				lastCycle = 0;
				cycleFactor = 0;
			}
		}

		updateLedStates();
		//Bar LEDs
		expander->digitalWrite(PIN_LED_LOW, ((barLeds[0].on && ((!barLeds[0].blinking) || (blinkOn))) ? LOW : HIGH));
		expander->digitalWrite(PIN_LED_MEDIUM, ((barLeds[1].on && ((!barLeds[1].blinking) || (blinkOn))) ? LOW : HIGH));
		expander->digitalWrite(PIN_LED_HIGH, ((barLeds[2].on && ((!barLeds[2].blinking) || (blinkOn))) ? LOW : HIGH));
		//RGB LEDs
		if ((lastBlinkOn == 0) || (now - lastBlinkOn > BLINK_DURATION)) {
			blinkOn = !blinkOn;
			lastBlinkOn = now;
		}
		bool changed = false;
		for (int i = 0; i < NUM_LEDS; i++) {
			CRGB pulseColor = ((leds[i].on && ((!leds[i].blinking) || (blinkOn))) ? leds[i].color : CRGB::Black);
			changed = changed || (pulseColor != fastLeds[i]);
			fastLeds[i] = pulseColor;
		}
		//update
		if (changed) {
			FastLED.show();
		}
	}

	WProperty* statusLedOn;

private:
	CRGB fastLeds[NUM_LEDS];
	WSLed leds[NUM_LEDS];
	WSLed barLeds[3];
	bool touchPanelOn;
	byte brightness;
	WNetwork* network;
	WIOExpander* expander;
	WProperty* aqi;
	WProperty* outsideAqi;
	WProperty* onOffProperty;
	WProperty* fanMode;
  WProperty* mode;
	WProperty* co2;
	WProperty* tvoc;
	bool blinkOn, insideOutsideAqiStatus;
	unsigned long lastBlinkOn, lastCycle;
	float cycleFactor;
	CRGB* lastPulse;

	void updateLedStates() {
		CRGB pmStatusColor = getPmStatusColor();
		if (this->touchPanelOn == true) {
    	//WIFI LED
    	if (network->isWifiConnected()) {
      	if ((network->isSupportingMqtt()) && (!network->isMqttConnected())) {
        	//orange blink
        	leds[LED_WIFI].color = CRGB::Yellow;
					leds[LED_WIFI].blinking = true;
      	} else {
        	//connection ok
        	leds[LED_WIFI].color = DEFAULT_COLOR;
					leds[LED_WIFI].blinking = false;
      	}
  		} else {
      	//red blink
				leds[LED_WIFI].color = CRGB::Red;
				leds[LED_WIFI].blinking = true;
    	}
			leds[LED_WIFI].on = true;
    	//Auto mode
    	if ((this->mode) && (this->mode->equalsString(MODE_AUTO))) {
				leds[LED_AUTO].color = CRGB::Red;
    	} else {
				leds[LED_AUTO].color = DEFAULT_COLOR;
    	}
			leds[LED_AUTO].blinking = (onOffProperty && !onOffProperty->asBool());
			leds[LED_AUTO].on = true;
    	//Filter
			leds[LED_FILTER].color = DEFAULT_COLOR;
			leds[LED_FILTER].on = true;
    	//VOC
			byte level = max(co2->enumIndex(), tvoc->enumIndex());
			if (level != 0xFF) {
				switch (level) {
					case 0:
					case 1:
						leds[LED_VOC].color = DEFAULT_COLOR;
						break;
					case 2:
						leds[LED_VOC].color = CRGB::Yellow;
						break;
					default :
						leds[LED_VOC].color = CRGB::Red;
				}
				leds[LED_VOC].blinking = false;
			} else {
				leds[LED_VOC].color = CRGB::Red;
				leds[LED_VOC].blinking = true;
			}
			leds[LED_VOC].on = true;
			//PM
			leds[LED_PM].color = pmStatusColor;
			leds[LED_PM].on = true;
			leds[LED_PM].blinking = ((this->aqi == nullptr) || (this->aqi->isNull()));
		} else {
			leds[LED_WIFI].on = false;
			leds[LED_AUTO].on = false;
			leds[LED_FILTER].on = false;
			leds[LED_VOC].on = false;
			leds[LED_PM].on = false;
		}
		//External Status LED
		if ((this->statusLedOn->asBool()) || (this->touchPanelOn)) {
			leds[LED_STATUS].color = pmStatusColor;
			leds[LED_STATUS].on = true;
			leds[LED_STATUS].blinking = ((this->aqi == nullptr) || (this->aqi->isNull()));
		} else {
			leds[LED_STATUS].on = false;
		}
		//Fan Bar LEDs
		bool b = (this->touchPanelOn) && (!this->fanMode->equalsString(FAN_MODE_OFF));
		barLeds[0].on = b;
		barLeds[0].blinking = (onOffProperty && !onOffProperty->asBool());
		barLeds[1].on = (b && ((this->fanMode->equalsString(FAN_MODE_MEDIUM)) || (this->fanMode->equalsString(FAN_MODE_HIGH))));
		barLeds[1].blinking = barLeds[0].blinking;
		barLeds[2].on = (b && (this->fanMode->equalsString(FAN_MODE_HIGH)));
		barLeds[2].blinking = barLeds[0].blinking;
  }

	CRGB getPmStatusColor() {
		if (lastPulse != nullptr) {
			delete lastPulse;
			lastPulse = nullptr;
		}
		if ((this->aqi != nullptr) && (!this->aqi->isNull())) {
      float aqi = this->aqi->asInt();
			if ((insideOutsideAqiStatus) && (this->outsideAqi != nullptr) && (!this->outsideAqi->isNull())) {
				aqi = aqi + ((float) (this->outsideAqi->asInt() - aqi) * cycleFactor);
			}
      byte r = 0;
      byte g = 0;
      byte b = 0;
      float m = 0.05;
      if (aqi < 20) {
        b = 255 - round(aqi * 255 * 0.05);
        g = round(aqi *255 * 0.05);
      }
      if ((aqi >= 20) && (aqi < 60)) {
        g = 255;
      } else if ((aqi >= 60) && (aqi < 80)) {
        g = 255 - round((aqi - 60) * 255 * 0.05);
      }
      if ((aqi >= 30) && (aqi < 50)) {
        r = round((aqi - 30) * 255 * 0.05);
      } else if ((aqi >= 50) && (aqi < 90)) {
        r = 255;
      } else if ((aqi >= 90) && (aqi < 110)) {
        r = 255 - round((aqi - 90) * 255 * 0.025);
      } else if (aqi >= 110) {
        r = 128;
      }


			lastPulse = new CRGB(r, g, b);
			return *lastPulse;

    } else {
      return CRGB::Red;
    }
	}

};

#endif
