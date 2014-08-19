#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include "SPI.h" 
#include "LPD8806.h"
#include <Encoder.h>
#include <Bounce.h>

#define debug true

#define nLEDs 44
#define nBINS 22

#define encCHUNK 1
#define milBUTTON_HOLD 800

#define pinEncA 15
#define pinEncB 16
#define pinBtn 17

#define pinDATA 11
#define pinCLK 13

boolean modeMAX = true;
#define MAX_FADE 12

uint8_t brightness = 16;

Encoder enc(pinEncA, pinEncB);
int32_t encOld = -999;

Bounce button = Bounce( pinBtn, 10);
uint32_t milTimeout;

AudioInputAnalog        audioInput(A9);
AudioPeak               peak_M;
AudioAnalyzeFFT256	myFFT(11);
AudioMixer4			mix1;

AudioConnection c1(audioInput, 0, mix1, 0);
//AudioConnection c2(mix1, 0, peak_M, 0);
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
		scale = maxSum();
		
		for (int i=0; i<nBINS; i++) {
			val[i] = 384 * sum[i] / scale;
			if (val[i] > 384) val[i] = 384;

			if (val[i] >= maximum[i]) {
				maximum[i] = val[i];
			} else {
				if (maximum[i] > 0 && maximum[i] > MAX_FADE)
					maximum[i] = maximum[i] - MAX_FADE;
				else
					maximum[i] = 0;
     	 	}

			if (debug){
				Serial.print("scale="); Serial.print(scale); Serial.print(" ");
				Serial.print(sum[i]);
				Serial.print("=");
				Serial.print(val[i]);
				Serial.print(",");
			}
		}
		Serial.println();
		count = 0;
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
		dimPixel(i, brightness);
	}
	strip.show();
}

int maxVal(){
	int temp;
	for (int i = 0; i<nBINS; i++){
		if (val[i]  > temp) temp = val[i];
	}
	return temp;
}

int maxSum(){
	int temp;
	for (int i = 0; i<nBINS; i++){
		if (sum[i]  > temp) temp = sum[i];
		if(debug){
			Serial.print(" sum[");Serial.print(i);Serial.print("]");Serial.print(sum[i]);
			Serial.print(" temp=");Serial.print(temp);
		}
	}
	if(debug) {Serial.println();Serial.print("max=");Serial.println(temp);}
	return temp;
}

void inputEncoder() {
	int32_t encNew = enc.read();
	int32_t encChange = encNew - encOld;
	if ( abs(encChange) >= encCHUNK ) {
		encOld = encNew;
		//milTimeout = millis();
		encChange /= encCHUNK; // use chunks to ensure fine control (move by a single index)
		if ( encChange > 0) { // increased
			brightness++;
		}
		else { // decreased
			brightness--;
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

void dimPixel(uint16_t pos, uint8_t intensity){ // intensity = 0-127
	
	uint32_t colorOld = strip.getPixelColor(pos);
	
	strip.setPixelColor(pos, (
						 (((colorOld >> 16) & 0x7f) * intensity / 127) << 16 |
						 (((colorOld >>  8) & 0x7f) * intensity / 127) <<  8 |
						  ((colorOld 	   	& 0x7f) * intensity / 127) 		));	
}