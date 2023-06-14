#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <ArduinoBLE.h>
#include <Arduino_LSM9DS1.h>
#include <TensorFlowLite.h>
#include "EMGFilters.h"

#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

#include "model_quant.h"

#define ANALOG_IN_PIN_DOWN A0
#define ANALOG_IN_PIN_UP A1

//BLE
BLEService gestureEstimateService("66df5109-edde-4f8a-a5e1-02e02a69cbd5");
BLEStringCharacteristic gesturePred("741c12b9-e13c-4992-8a5e-fce46dec0bff", BLERead | BLENotify, 15);
bool ifIdle = true;

// EMG
EMGFilters myFilter;
SAMPLE_FREQUENCY sampleRate = SAMPLE_FREQ_500HZ;  // or SAMPLE_FREQ_1000HZ
NOTCH_FREQUENCY humFreq = NOTCH_FREQ_50HZ;  // or NOTCH_FREQ_60HZ

const int emgCutThreshold = 0;  // not using this
const int emgThreshold = 2500;
const int numSamples = 150;

int samplesRead = numSamples;

// global variables used for TensorFlow Lite (Micro)
tflite::MicroErrorReporter tflErrorReporter;

// pull in all the TFLM ops, you can remove this line and
// only pull in the TFLM ops you need, if would like to reduce
// the compiled size of the sketch.
tflite::AllOpsResolver tflOpsResolver;

const tflite::Model* tflModel = nullptr;
tflite::MicroInterpreter* tflInterpreter = nullptr;
TfLiteTensor* tflInputTensor = nullptr;
TfLiteTensor* tflOutputTensor = nullptr;

// Create a static memory buffer for TFLM, the size may need to
// be adjusted based on the model you are using
constexpr int tensorArenaSize = 8 * 1024;
byte tensorArena[tensorArenaSize] __attribute__((aligned(16)));

// array to map gesture index to a name
const char* GESTURES[] = {
    "fist_left",
    "fist_right",
    "fist_up",
    "fist_down",
    "fist_center",
    "palm_left",
    "palm_right",
    "palm_up",
    "palm_down",
    "palm_center"
};

// #define NUM_GESTURES (sizeof(GESTURES) / sizeof(GESTURES[0]))
#define NUM_GESTURES 10

void setup() {
  Serial.begin(9600);
  while (!Serial);

  // initialize the IMU
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  if (!BLE.begin()) {
    Serial.println("starting BLE failed!");
    while (1);
  }

  BLE.setLocalName("GestureControlForRunners");
  BLE.setAdvertisedService(gestureEstimateService);
  gestureEstimateService.addCharacteristic(gesturePred);
  BLE.addService(gestureEstimateService);

  BLE.advertise();
  Serial.println("Bluetooth device active, waiting for connections...");
  
    /* add setup code here */
  myFilter.init(sampleRate, humFreq, true, true, true);

  // get the TFL representation of the model byte array
  tflModel = tflite::GetModel(model);
  if (tflModel->version() != TFLITE_SCHEMA_VERSION) {
    Serial.println("Model schema mismatch!");
    while (1);
  }
  
  // Create an interpreter to run the model
  tflInterpreter = new tflite::MicroInterpreter(tflModel, tflOpsResolver, tensorArena, tensorArenaSize);

  // Allocate memory for the model's input and output tensors
  tflInterpreter->AllocateTensors();
  
  Serial.print("Used bytes in tensor arena: ");
  Serial.println(tflInterpreter->arena_used_bytes());

  // Get pointers for the model's input and output tensors
  tflInputTensor = tflInterpreter->input(0);
  tflOutputTensor = tflInterpreter->output(0);

  Serial.println("TF Lite setup done");
}

void loop() {
  BLEDevice central = BLE.central();
  if (central) {
    Serial.print("Connected to central: ");
    Serial.println(central.address());
  }

  while (central.connected()) {
    float aX, aY, aZ, gX, gY, gZ;
    // wait for significant motion (Accelerometer)
    while (samplesRead == numSamples) {
      int eD_raw = analogRead(ANALOG_IN_PIN_DOWN);
      int eU_raw = analogRead(ANALOG_IN_PIN_UP);

      int eD_filtered = myFilter.update(eD_raw);
      int eU_filtered = myFilter.update(eU_raw);

      int eD = sq(eD_filtered);
      int eU = sq(eU_filtered);

      eD = (eD > 0) ? eD : 0;
      eU = (eU > 0) ? eU : 0;

      // check if it's above the threshold
      if ((eD >= emgThreshold) || (eU >= emgThreshold)) {
        samplesRead = 0;
        break;
      }

      if (!ifIdle) {
        gesturePred.writeValue("Idle");
        ifIdle = true;
      }
      delay(1);
    }

    // check if the all the required samples have been read since
    // the last time the significant motion was detected
    while (samplesRead < numSamples) {
      // check if new acceleration AND gyroscope data is available
      if (true) {
        // read the acceleration and gyroscope data
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

        // normalize the IMU data between 0 to 1 and store in the model's
        // input tensor
        tflInputTensor->data.f[samplesRead * 8 + 0] = eD / 500000;
        tflInputTensor->data.f[samplesRead * 8 + 1] = eU / 500000;
        tflInputTensor->data.f[samplesRead * 8 + 2] = (aX + 4.0) / 8.0;
        tflInputTensor->data.f[samplesRead * 8 + 3] = (aY + 4.0) / 8.0;
        tflInputTensor->data.f[samplesRead * 8 + 4] = (aZ + 4.0) / 8.0;
        tflInputTensor->data.f[samplesRead * 8 + 5] = (gX + 2000.0) / 4000.0;
        tflInputTensor->data.f[samplesRead * 8 + 6] = (gY + 2000.0) / 4000.0;
        tflInputTensor->data.f[samplesRead * 8 + 7] = (gZ + 2000.0) / 4000.0;

        samplesRead++;

        if (samplesRead == numSamples) {
          Serial.println("Enough data for inferencing");
          // Run inferencing
          TfLiteStatus invokeStatus = tflInterpreter->Invoke();
          if (invokeStatus != kTfLiteOk) {
            Serial.println("Invoke failed!");
            while (1);
            return;
          }

          // Loop through the output tensor values from the model
          bool SHOW_ALL_PROB = false;
          double max_prob = 0.0;
          int max_prob_gesture = 0;
          for (unsigned int i = 0; i < NUM_GESTURES; i++) {
            double pred_prob = tflOutputTensor->data.f[i];
            if (pred_prob >= max_prob) {
              max_prob = pred_prob;
              max_prob_gesture = i;
            }
            if (SHOW_ALL_PROB) {
              Serial.print(GESTURES[i]);
              Serial.print(": ");
              Serial.println(pred_prob, 6);
            }
          }
          Serial.print("Predicted gesture: ");
          Serial.print(GESTURES[max_prob_gesture]);
          Serial.print(" (");
          Serial.print(max_prob, 6);
          Serial.print(")");
          Serial.println();

          // send data to BLE
          ifIdle = false;
          gesturePred.writeValue(GESTURES[max_prob_gesture]);
        }
      }
    }
    delay(10);
  }
  delay(1000);
}
