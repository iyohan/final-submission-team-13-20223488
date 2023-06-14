/*
* Copyright 2017, OYMotion Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in
*    the documentation and/or other materials provided with the
*    distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
* THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
* DAMAGE.
*
*/

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <Arduino_LSM9DS1.h>
#include "EMGFilters.h"

#define TIMING_DEBUG 1

#define ANALOG_IN_PIN_DOWN A0
#define ANALOG_IN_PIN_UP A1

// IMU
const float accelerationThreshold = 2.5; // threshold of significant in G's

// EMG
EMGFilters myFilter;
SAMPLE_FREQUENCY sampleRate = SAMPLE_FREQ_500HZ;  // or SAMPLE_FREQ_1000HZ
NOTCH_FREQUENCY humFreq = NOTCH_FREQ_50HZ;  // or NOTCH_FREQ_60HZ

// Calibration:
// put on the sensors, and release your muscles;
// wait a few seconds, and select the max value as the throhold;
// any value under throhold will be set to zero
static int emgCutThreshold = 0;  // not using this
const int emgThreshold = 2500;

unsigned long timeStamp;
unsigned long timeBudget;

const int numSamples = 150;

int samplesRead = numSamples;

void setup() {
  // open serial
  Serial.begin(115200);
  while (!Serial);

  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  /* add setup code here */
  myFilter.init(sampleRate, humFreq, true, true, true);

  // setup for time cost measure
  // using micros()
  timeBudget = 1e6 / sampleRate;
  // micros will overflow and auto return to zero every 70 minutes

  Serial.println("timestamp,eD,eU,aX,aY,aZ,gX,gY,gZ");
}

void loop() {
  /* add main program code here */
  // In order to make sure the ADC sample frequence on arduino,
  // the time cost should be measured each loop
  /*------------start here-------------------*/
  // timeStamp = micros();

  float aX, aY, aZ, gX, gY, gZ;

  // wait for significant motion (Accelerometer)
  while (samplesRead == numSamples) {
    // float aSum = 0;
    // if (IMU.accelerationAvailable()) {
    //   IMU.readAcceleration(aX, aY, aZ);
    //   aSum = fabs(aX) + fabs(aY) + fabs(aZ);
    // }
    
    int eD_raw = analogRead(ANALOG_IN_PIN_DOWN);
    int eU_raw = analogRead(ANALOG_IN_PIN_UP);

    // filter processing; the filter cost average around 520 us
    int eD_filtered = myFilter.update(eD_raw);
    int eU_filtered = myFilter.update(eU_raw);

    int eD = sq(eD_filtered);
    int eU = sq(eU_filtered);

    eD = (eD > emgCutThreshold) ? eD : 0;
    eU = (eU > emgCutThreshold) ? eU : 0;

    // check if it's above the threshold
    if ((eD >= emgThreshold) || (eU >= emgThreshold)) {
      samplesRead = 0;
      break;
    }

    delay(1);
  }

  // check if the all the required samples have been read since
  // the last time the significant motion was detected
  while (samplesRead < numSamples) {
    // if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
    if (1) {
      // timeStamp = micros() - timeStamp;
      timeStamp = micros();
      IMU.readAcceleration(aX, aY, aZ);
      IMU.readGyroscope(gX, gY, gZ);

    int eD_raw = analogRead(ANALOG_IN_PIN_DOWN);
    int eU_raw = analogRead(ANALOG_IN_PIN_UP);

    // filter processing; the filter cost average around 520 us
    int eD_filtered = myFilter.update(eD_raw);
    int eU_filtered = myFilter.update(eU_raw);

    int eD = sq(eD_filtered);
    int eU = sq(eU_filtered);

      // any value under threshold will be set to zero
      eD = (eD > emgCutThreshold) ? eD : 0;
      eU = (eU > emgCutThreshold) ? eU : 0;

      samplesRead++;

      Serial.print(timeStamp);
      Serial.print(',');
      Serial.print(eD);
      Serial.print(',');
      Serial.print(eU);
      Serial.print(',');
      Serial.print(aX, 3);
      Serial.print(',');
      Serial.print(aY, 3);
      Serial.print(',');
      Serial.print(aZ, 3);
      Serial.print(',');
      Serial.print(gX, 3);
      Serial.print(',');
      Serial.print(gY, 3);
      Serial.print(',');
      Serial.print(gZ, 3);
      Serial.println();

      if (samplesRead == numSamples) {
        // add an empty line if it's the last sample
        Serial.println();
      }
    }
  }

//////
    /*------------end here---------------------*/
    // if less than timeBudget, then you still have (timeBudget - timeStamp) to
    // do your work
    // delayMicroseconds(500);
  delayMicroseconds(500);
}
