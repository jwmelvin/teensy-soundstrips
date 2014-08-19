#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include "SPI.h" 
#include "LPD8806.h"
#include <Encoder.h>
#include <Bounce.h>

#define debug false

#define nLEDs 44
#define nBINS 22

#define encCHUNK 2
#define milBUTTON_HOLD 800

#define pinEncA 15
#define pinEncB 16
#define pinBtn 17

#define pinDATA 11
#define pinCLK 13

boolean modeMAX = true;
#define FADE_RATE 12

#define PEAK_ALL false
#define PEAK_BASS true
int scalePeak = 0;
#define MIN_BRIGHT 10
#define MAX_BRIGHT 255

uint8_t brightness = 32;

Encoder enc(pinEncA, pinEncB);
int32_t encOld = -999;

Bounce button = Bounce( pinBtn, 10);
uint32_t milTimeout;

AudioInputAnalog        audioInput(A9);
AudioPeak               peak_M;
float monoPeak;
AudioAnalyzeFFT256	myFFT(11);
AudioMixer4			mix1;

AudioConnection c1(audioInput, 0, mix1, 0);
AudioConnection c2(mix1, 0, peak_M, 0);
AudioConnection c3(mix1, 0, myFFT, 0);


LPD8806 strip = LPD8806(nLEDs);

int scale;
int count=0;
const int nsum[nBINS] = {1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16};
//const int nsum[nBINS] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3, 4, 5, 7, 9, 13, 17, 23, 31};

int sum[nBINS];	// raw sums
int val[nBINS];	// scaled sums
int maximum[nBINS];	// fading maxima

void setup() {
	AudioMemory(12);
	if(debug) Serial.begin(Serial.baud());
	mix1.gain(0,1.0);
	pinMode(pinBtn, INPUT);
	digitalWrite(pinBtn, HIGH); // enable pullup resistor
	
	strip.begin();
	strip.show();
}

void loop() {
	inputButton();  // check for button presses
	inputEncoder(); // check for encoder motion

	if (myFFT.available()) {
		// convert the 128 FFT frequency bins to only nBINS sums
		for (int i=0; i<nBINS; i++) {
			sum[i] = 0;
		}
		int n=0;
		int count=0;
		for (int i=0; i<128; i++) {
			sum[n] = sum[n] + myFFT.output[i];
			count++;
			if (count >= nsum[n]) {
				n++;
				if (n >= nBINS) break;
				count = 0;
			}
		}
		// determine scale based on largest sum
		scale = 0;
		for (int i = 0; i<nBINS; i++){
			if (sum[i]  > scale) scale = sum[i];
		}
		
		for (int i=0; i<nBINS; i++) {
			val[i] = 384 * sum[i] / scale;
			if (val[i] > 384) val[i] = 384;

			if (val[i] >= maximum[i]) {
				maximum[i] = val[i];
			} else {
				if (maximum[i] > 0 && maximum[i] > FADE_RATE)
					maximum[i] = maximum[i] - FADE_RATE;
				else
					maximum[i] = 0;
     	 	}
			
			if (debug){
				Serial.print("sum:val[");Serial.print(i);Serial.print("]=");
				Serial.print(sum[i]);
				Serial.print(":");
				Serial.print(val[i]);
				Serial.print(",");
			}
		}
		if(debug) Serial.println();
		count = 0;
		// measure peak
		if (PEAK_ALL) {
			if (peak_M.Dpp() > scalePeak) scalePeak = peak_M.Dpp();
			monoPeak=float(peak_M.Dpp())/float(scalePeak);
			peak_M.begin();
			if(debug){
				Serial.print("peak/scale:"); 
				Serial.print(peak_M.Dpp());Serial.print("/");Serial.print(scalePeak);
				Serial.print("=");Serial.println(monoPeak);
			}
		} else if (PEAK_BASS) {
			if (sum[1] > scalePeak) scalePeak = sum[1];
			else scalePeak--;
			monoPeak = float(sum[1])/float(scalePeak);
			if(debug){
				Serial.print("peak/scale:"); 
				Serial.print(sum[1]);Serial.print("/");Serial.print(scalePeak);
				Serial.print("=");Serial.println(monoPeak);
			}
		}
		updateStrip();
	}
}

void updateStrip(){
	for (int i=0; i<nBINS; i++){
		if (modeMAX) 
			strip.setPixelColor(i,Wheel(maximum[i]));
		else 
			strip.setPixelColor(i,Wheel(val[i]));
	}
	for (int i=nBINS; i<nBINS*2; i++){
		if (modeMAX) 
			strip.setPixelColor(i,Wheel(maximum[nBINS*2 - i - 1]));
		else 
			strip.setPixelColor(i,Wheel(val[nBINS*2 - i - 1]));
	}
	for (int i=0; i<nLEDs; i++){
		dimPixel(i, constrain(monoPeak * brightness, MIN_BRIGHT, monoPeak * brightness));
	}
	strip.show();
}

void inputEncoder() {
	int32_t encNew = enc.read();
	int32_t encChange = encNew - encOld;
	if ( abs(encChange) >= encCHUNK ) {
		encOld = encNew;
		//milTimeout = millis();
		encChange /= encCHUNK; // use chunks to ensure fine control 
		if ( encChange > 0) { // increased
			brightness = constrain(brightness+1, MIN_BRIGHT, MAX_BRIGHT);
		}
		else { // decreased
			brightness = constrain(brightness-1, MIN_BRIGHT, MAX_BRIGHT);
		}
	}
}

void inputButton(){
	static uint8_t flagBtnEnable;
	
	if (button.update ()) {
		// milTimeout = millis(); // check for button state and update timeout if action
	}
	// action when button held down
	if ( !button.read() && button.duration() > milBUTTON_HOLD && flagBtnEnable){
		flagBtnEnable = false; // disable btn so release has no action
		modeMAX = modeMAX ? false : true;
	}
	// button depressed
	else if ( button.fallingEdge() ) { 
		flagBtnEnable = true; // enable btn 
	}
	// action when button was tapped (held for less than milBUTTON_HOLD)
	else if ( button.risingEdge() && flagBtnEnable) {
		//
	}
}


/* Helper functions */

//Input a value 0 to 384 to get a color value.
//The colours are a transition r - g -b - back to r
uint32_t Wheel(uint16_t WheelPos)
{
  byte r, g, b;
  switch(WheelPos / 128)
  {
    case 0:
      r = 127 - WheelPos % 128;   //Red down
      g = WheelPos % 128;      // Green up
      b = 0;                  //blue off
      break; 
    case 1:
      g = 127 - WheelPos % 128;  //green down
      b = WheelPos % 128;      //blue up
      r = 0;                  //red off
      break; 
    case 2:
      b = 127 - WheelPos % 128;  //blue down 
      r = WheelPos % 128;      //red up
      g = 0;                  //green off
      break; 
  }
  return(strip.Color(r,g,b));
}

// dimming function for LPD8806 LED strips
void dimPixel(uint16_t pos, uint8_t intensity){ // intensity = 0-127
	uint32_t colorOld = strip.getPixelColor(pos);
	strip.setPixelColor(pos, (
						 (((colorOld >> 16) & 0x7f) * intensity / 127) << 16 |
						 (((colorOld >>  8) & 0x7f) * intensity / 127) <<  8 |
						  ((colorOld 	   	& 0x7f) * intensity / 127) 		));	
}