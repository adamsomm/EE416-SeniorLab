/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Complete project details at https://RandomNerdTutorials.com/esp-now-esp32-arduino-ide/  
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*/

#include <esp_now.h>
#include <WiFi.h>
#define MAX_DEVICES 5


typedef struct struct_device_data {
  char mac_addr[18];
  float average_rssi;
} struct_device_data;

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
    int device_count;
    struct_device_data devices[MAX_DEVICES];
} struct_message;

// Create a struct_message called myData
struct_message myData;

// callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.printf("Bytes received: %d\n", len);
  Serial.printf("Device Count: %d\n", myData.device_count);
  Serial.println("--------------------");

  for (int i = 0; i < myData.device_count; i++) {
    Serial.printf("Device %d:\n", i);
    Serial.printf("  MAC Address: %s\n", myData.devices[i].mac_addr);
    Serial.printf("  Average RSSI: %.2f\n", myData.devices[i].average_rssi);
  }
  Serial.println("--------------------");
}

 
void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}
 
void loop() {

}
