#include <WiFi.h>
#include <HTTPClient.h>
#include "EmonLib.h"

// --- 🔐 Credentials ---
const char* ssid = "XYZ"; 
const char* password = "12345678910";
String baseURL = "https://voltiq-f3cf8-default-rtdb.asia-southeast1.firebasedatabase.app/channels/";

// --- 📡 Pin Mapping ---
#define VOLT_PIN 34
int currentPins[4] = {35, 32, 33, 36}; // Pins for CT1, CT2, CT3, CT4
int relayPins[4] = {12, 14, 27, 26}; 
String chNames[4] = {"channel1", "channel2", "channel3", "channel4"};

// Create 4 separate monitor objects
EnergyMonitor emon[4]; 
unsigned long lastSync = 0;

void setup() {
  Serial.begin(115200);
  
  // 1. Initialize Hardware
  for(int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);

    // Setup each monitor with the shared Voltage pin but unique Current pin
    emon[i].voltage(VOLT_PIN, 234.26, 1.7); 
    emon[i].current(currentPins[i], 30.0); // Calibrated for SCT-013
  }

  // 2. Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n✅ System Online: 4 Channels Active");
}

void loop() {
  // Sync all 4 channels every 3 seconds
  if (millis() - lastSync > 3000) {
    Serial.println("\n--- [ VOLTIQ 4-CHANNEL DASHBOARD ] ---");
    
    for(int i = 0; i < 4; i++) {
      // 1. Calculate AC Data for this specific channel
      // We use 20 crossings (approx 200ms of sampling) per channel
      emon[i].calcVI(20, 2000); 
      
      float v = emon[i].Vrms;
      float a = emon[i].Irms;

      // 2. Professional Filtering & Noise Gates
      if (v < 50.0) v = 0;
      if (a < 0.15) a = 0; // Ignore ghost current (anything below ~35W)

      // 3. Command & Upload Logic
      processChannel(i, v, a);
    }
    lastSync = millis();
  }
}

void processChannel(int i, float v, float a) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  
  // STEP A: Fetch Relay Command from Website
  http.begin(baseURL + chNames[i] + "/relay.json");
  if (http.GET() == 200) {
    String cmd = http.getString();
    cmd.replace("\"", ""); cmd.trim();
    
    if (cmd == "ON") digitalWrite(relayPins[i], HIGH);
    else digitalWrite(relayPins[i], LOW);
  }
  http.end();

  // STEP B: Force Professional Zeroing
  // If the relay is physically OFF, we show 0V/0A on the website
  float displayV = 0, displayA = 0, displayW = 0;
  String statusText = "OFF";

  if (digitalRead(relayPins[i]) == HIGH) {
    displayV = v;
    displayA = a;
    displayW = v * a;
    statusText = "ON";
    
    // Safety check: Relay is ON but no AC detected
    if (v < 50.0) statusText = "FUSE BLOWN";
  }

  // STEP C: Push to Firebase
  http.begin(baseURL + chNames[i] + ".json");
  http.addHeader("Content-Type", "application/json");
  
  String json = "{\"voltage\":" + String(displayV, 1) + 
                ",\"current\":" + String(displayA, 3) + 
                ",\"power\":" + String(displayW, 1) + 
                ",\"status\":\"" + statusText + "\"}";

  int patchCode = http.sendRequest("PATCH", json);
  http.end();

  // STEP D: Serial Monitoring
  Serial.printf("CH%d | Status: %s | V: %.1fV | A: %.3fA | W: %.1fW\n", 
                i+1, statusText.c_str(), displayV, displayA, displayW);
}