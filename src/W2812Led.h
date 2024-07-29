#ifndef W_2812_LED_H
#define W_2812_LED_H

#include "Adafruit_NeoPixel.h"
#include "WPin.h"

const int COUNT_LED_PROGRAMS = 3;
const float PI180 = 0.01745329;

class W2812Led: public WPin {
public:
	W2812Led(WNetwork* network, int ledPin, byte numberOfLeds) :
			WPin(ledPin, OUTPUT) {
		this->numberOfLeds = numberOfLeds;
		this->ledProgram = 0;
		this->programStatusCounter = 0;
		this->lastUpdate = 0;
		this->color = new WColorProperty("color", "Color", 255, 255, 255);
		network->getSettings()->add(this->color);
		this->brightness = new WLevelIntProperty("brightness", "Brightness", 10, 255);
		this->brightness->setInteger(160);
		network->getSettings()->add(this->brightness);
		this->brightness->setOnChange([this](WProperty* property) {strip->setBrightness(property->getInteger());});
		this->strip = new Adafruit_NeoPixel(numberOfLeds, ledPin, NEO_GRB + NEO_KHZ800);
		this->strip->begin();     // INITIALIZE NeoPixel strip object (REQUIRED)
		this->strip->show();            // Turn OFF all pixels ASAP
		this->strip->setBrightness(brightness->getInteger()); // Set BRIGHTNESS to about 1/5 (max = 255)

	}

	bool isOn() {
		return stripOn;
	}

	void setOn(bool stripOn) {
		this->stripOn = stripOn;
		if (!stripOn) {
			for (int i = 0; i < strip->numPixels(); i++) {
				strip->setPixelColor(i, strip->Color(0, 0, 0));
			}
			strip->show();
		}
	}

	byte getLedProgram() {
		return ledProgram;
	}

	void setLedProgram(byte ledProgram) {
		if (ledProgram >= COUNT_LED_PROGRAMS) {
			ledProgram = 0;
		}
		if (this->ledProgram != ledProgram) {
			this->ledProgram = ledProgram;
			this->programStatusCounter = 0;
			//this->saveSettings();
			//this->notify();
		}
	}

	WColorProperty* getColor() {
		return this->color;
	}

	WLevelIntProperty* getBrightness() {
		return this->brightness;
	}

	void loop(unsigned long now) {
		if ((isOn()) && (now - lastUpdate > 200)) {
			switch (ledProgram) {
			case 0: {
				//Pulsing RGB color
				float t = sin((programStatusCounter - 90) * PI180);
				t = (t + 1) / 2;
				uint32_t pulseColor = strip->Color(
						round(color->getRed() * (85 + round(170.0 * t)) / 255),
						round(color->getGreen() * (85 + round(170.0 * t)) / 255),
						round(color->getBlue() * (85 + round(170.0 * t)) / 255));
				for (uint16_t rainbowI = 0; rainbowI < strip->numPixels();	rainbowI++) {
					strip->setPixelColor(rainbowI, pulseColor);
				}
				strip->show();
				programStatusCounter++;
				if (programStatusCounter >= 360) {
					programStatusCounter = 0;
				}
				break;
			}
			case 1: {
				//Rainbow program, all pixel the same color
				uint32_t wC = wheelColor((programStatusCounter) & 255);
				for (uint16_t rainbowI = 0; rainbowI < strip->numPixels();
						rainbowI++) {
					strip->setPixelColor(rainbowI, wC);
				}
				strip->show();
				programStatusCounter++;
				if (programStatusCounter == 256 * 5) {
					programStatusCounter = 0;
				}
				break;
			}
			case 2: {
				//Rainbow program, different colors
				for (uint16_t rainbowI = 0; rainbowI < strip->numPixels();
						rainbowI++) {
					strip->setPixelColor(rainbowI,
							wheelColor(
									((rainbowI * 256 / strip->numPixels())
											+ programStatusCounter) & 255));
				}
				strip->show();
				programStatusCounter++;
				if (programStatusCounter == 256 * 5) {
					programStatusCounter = 0;
				}
				break;
			}
			}
			lastUpdate = now;
		}

	}

protected:
private:
	bool stripOn;
	byte numberOfLeds;
	byte ledProgram;
	int programStatusCounter;
	WColorProperty* color;
	WLevelIntProperty* brightness;
	unsigned long lastUpdate;
	Adafruit_NeoPixel *strip;

	uint32_t wheelColor(byte wheelPos) {
		byte c;
		if (wheelPos < 85) {
			c = wheelPos * 3;
			return strip->Color(c, 255 - c, 0);
		} else if (wheelPos < 170) {
			//wheelPos -= 85;
			c = (wheelPos - 85) * 3;
			return strip->Color(255 - c, 0, c);
		} else {
			//wheelPos -= 170;
			c = (wheelPos - 170) * 3;
			return strip->Color(0, c, 255 - c);
		}
	}


	void whiteOverRainbow(int whiteSpeed, int whiteLength) {

		if (whiteLength >= strip->numPixels())
			whiteLength = strip->numPixels() - 1;

		int head = whiteLength - 1;
		int tail = 0;
		int loops = 3;
		int loopNum = 0;
		uint32_t lastTime = millis();
		uint32_t firstPixelHue = 0;

		for (;;) { // Repeat forever (or until a 'break' or 'return')
			for (int i = 0; i < strip->numPixels(); i++) { // For each pixel in strip...
				if (((i >= tail) && (i <= head)) || //  If between head & tail...
						((tail > head) && ((i >= tail) || (i <= head)))) {
					strip->setPixelColor(i, strip->Color(0, 0, 0, 255)); // Set white
				} else {                                     // else set rainbow
					int pixelHue = firstPixelHue
							+ (i * 65536L / strip->numPixels());
					strip->setPixelColor(i,
							strip->gamma32(strip->ColorHSV(pixelHue)));
				}
			}

			strip->show(); // Update strip with new contents
			// There's no delay here, it just runs full-tilt until the timer and
			// counter combination below runs out.

			firstPixelHue += 40; // Advance just a little along the color wheel

			if ((millis() - lastTime) > whiteSpeed) { // Time to update head/tail?
				if (++head >= strip->numPixels()) { // Advance head, wrap around
					head = 0;
					if (++loopNum >= loops)
						return;
				}
				if (++tail >= strip->numPixels()) { // Advance tail, wrap around
					tail = 0;
				}
				lastTime = millis();               // Save time of last movement
			}
		}
	}

};

#endif
