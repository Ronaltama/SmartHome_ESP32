#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Wire.h> // Required for I2C communication
#include <LiquidCrystal_I2C.h> // For LCD 16x2 with I2C
#include <SPI.h> // Required for MFRC522 RFID
#include <MFRC522.h> // Pustaka RFID MFRC522 standar

// Library for JSON parsing
#include <ArduinoJson.h>

// Wi-Fi credentials
const char* ssid = "my_technology";
const char* password = "35k4nu54marina";

// Node-RED server URLs
// PENTING: Ganti dengan alamat IP Node-RED Anda yang sebenarnya
// Contoh: const char* serverUrl = "http://192.168.1.100:1880/esp32_data";
const char* serverUrl = "http://127.0.0.1:1880/esp32_data"; // Endpoint untuk mengirim data sensor
const char* controlUrl = "http://127.0.0.1:1880/esp32_control"; // Endpoint untuk menerima perintah kontrol

// DHT22 sensor definition
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// RFID MFRC522 pin definitions
#define RST_PIN 17 // RFID RST pin
#define SS_PIN 5   // RFID SDA (SS/CS) pin
MFRC522 rfid(SS_PIN, RST_PIN); // Membuat instance MFRC522

// LCD 16x2 I2C definition
// Atur alamat LCD ke 0x27 atau 0x3F (periksa modul LCD Anda)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Servo (Door) definition
Servo doorServo;
const int servoOpenAngle = 90; // Sudut untuk pintu terbuka
const int servoCloseAngle = 0; // Sudut untuk pintu tertutup

// Pin assignments for sensors, actuators, and relays
const int pinPIRTerrace = 34;
const int pinPIRBathroom = 32;
const int intGasSensor = 35; // MQ-X sensor (analog input)
const int pinBuzzer = 27; // Alarm buzzer

const int relayTerraceLight = 25;
const int relayLRLight = 26;
const int relayLRFan = 14;
const int relayBathLight = 16;
const int relayBR1Light = 2;
const int relayBR2Light = 15;
const int relayKitchenLight = 13;
const int servoPin = 12;

// Global variables for state management and timing
unsigned long lastDHTReadMillis = 0;
const long DHT_READ_INTERVAL = 5000; // Baca DHT dan kirim data setiap 5 detik

// RFID access control state
// PENTING: UID kartu master Anda yang sebenarnya (D7 7C 37 03)
byte masterCardUID[] = {0xD7, 0x7C, 0x37, 0x03};
int rfidFailedAttempts = 0;
const int MAX_RFID_FAILED_ATTEMPTS = 3;
unsigned long alarmStartTime = 0;
const long ALARM_DURATION = 5000; // Durasi alarm dalam milidetik (5 detik)

// PIR Terrace Alarm state, dikontrol oleh Node-RED
bool pirTerraceAlarmArmed = false;

// Function prototypes
void setup_wifi();
void sendSensorData();
void handleRFID();
void handlePIRSensors();
void handleGasSensor();
void activateAlarm(long duration);
void updateRelayStatus(String relayName, bool state);
void sendControlRequest(); // Fungsi untuk meminta status kontrol dari Node-RED

void setup() {
  Serial.begin(115200);

  // Initialize Wi-Fi connection
  setup_wifi();

  // Initialize DHT sensor
  dht.begin();

  // Initialize LCD display
  Wire.begin(21, 22); // Initialize I2C communication for LCD (SDA, SCL)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SmartHome Init...");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi...");

  // Initialize RFID reader
  SPI.begin(18, 19, 23, 5); // Initialize SPI for RFID (SCK, MISO, MOSI, SS)
  rfid.PCD_Init();
  Serial.println("RFID initialized.");

  // Initialize Servo motor
  doorServo.attach(servoPin);
  doorServo.write(servoCloseAngle); // Pastikan pintu tertutup saat startup

  // Configure pin modes
  pinMode(pinPIRTerrace, INPUT);
  pinMode(pinPIRBathroom, INPUT);
  pinMode(intGasSensor, INPUT); // Analog input for gas sensor
  pinMode(pinBuzzer, OUTPUT);

  pinMode(relayTerraceLight, OUTPUT);
  pinMode(relayLRLight, OUTPUT);
  pinMode(relayLRFan, OUTPUT);
  pinMode(relayBathLight, OUTPUT);
  pinMode(relayBR1Light, OUTPUT);
  pinMode(relayBR2Light, OUTPUT);
  pinMode(relayKitchenLight, OUTPUT);

  // Set all relays OFF at startup (assuming active LOW relays: HIGH = OFF, LOW = ON)
  digitalWrite(relayTerraceLight, HIGH);
  digitalWrite(relayLRLight, HIGH);
  digitalWrite(relayLRFan, HIGH);
  digitalWrite(relayBathLight, HIGH);
  digitalWrite(relayBR1Light, HIGH);
  digitalWrite(relayBR2Light, HIGH);
  digitalWrite(relayKitchenLight, HIGH);
  digitalWrite(pinBuzzer, LOW); // Pastikan alarm buzzer mati

  // Initial LCD display after setup
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SmartHome Ready!");
  delay(2000);
  lcd.clear();
}

void loop() {
  // Handle RFID access control logic
  handleRFID();

  // Handle PIR motion sensors logic
  handlePIRSensors();

  // Handle Gas sensor logic
  handleGasSensor();

  // Periodically send sensor data to Node-RED and request control states
  if (millis() - lastDHTReadMillis > DHT_READ_INTERVAL) {
    sendSensorData();
    sendControlRequest(); // Meminta status kontrol terbaru dari Node-RED
    lastDHTReadMillis = millis();
  }

  // Manage alarm activation and duration
  if (alarmStartTime > 0 && millis() - alarmStartTime < ALARM_DURATION) {
    digitalWrite(pinBuzzer, HIGH); // Nyalakan buzzer
    lcd.setCursor(0, 0);
    lcd.print("!!! ALARM ON !!!");
  } else if (alarmStartTime > 0 && millis() - alarmStartTime >= ALARM_DURATION) {
    digitalWrite(pinBuzzer, LOW); // Matikan buzzer
    alarmStartTime = 0; // Reset timer alarm
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SmartHome Ready!"); // Kembali ke tampilan normal
  }
}

// Function to connect ESP32 to Wi-Fi network
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    lcd.setCursor(0, 1);
    lcd.print("IP: ");
    lcd.print(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("WiFi connection FAILED. Retrying...");
    lcd.setCursor(0, 1);
    lcd.print("WiFi FAILED!");
    delay(5000); // Wait before retrying
    ESP.restart(); // Restart ESP32 to attempt reconnection
  }
}

// Function to send sensor data to Node-RED via HTTP POST
void sendSensorData() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi(); // Attempt to reconnect if WiFi is disconnected
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  // Read sensor values
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int gasValue = analogRead(intGasSensor); // Menggunakan intGasSensor
  bool pirTerraceDetected = digitalRead(pinPIRTerrace);
  bool pirBathroomDetected = digitalRead(pinPIRBathroom);

  // Handle invalid DHT sensor readings
  if (isnan(t) || isnan(h)) {
    Serial.println("Failed to read from DHT sensor!");
    t = 0; // Default to 0 or last known good value if reading fails
    h = 0;
  }

  // Create JSON payload for sensor data
  StaticJsonDocument<512> doc; // Menentukan ukuran untuk StaticJsonDocument
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["gas_value"] = gasValue;
  doc["pir_terrace_detected"] = pirTerraceDetected;
  doc["pir_bathroom_detected"] = pirBathroomDetected;
  doc["rfid_failed_attempts"] = rfidFailedAttempts; // Include RFID failed attempts count
  doc["alarm_active"] = (alarmStartTime > 0); // Include current alarm status

  String payload;
  serializeJson(doc, payload); // Serialize JSON document to a String

  Serial.print("Sending sensor data: ");
  Serial.println(payload);

  int httpCode = http.POST(payload); // Send HTTP POST request

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.println("Response from Node-RED (data send): " + response);
    // No specific response parsing needed here, control commands are handled by sendControlRequest
  } else {
    Serial.printf("HTTP POST error (sendSensorData): %s\n", http.errorToString(httpCode).c_str());
  }

  http.end(); // Close HTTP connection
}

// Function to request and receive control commands from Node-RED via HTTP POST
void sendControlRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi(); // Attempt to reconnect if WiFi is disconnected
    return;
  }

  HTTPClient http;
  http.begin(controlUrl); // Use the dedicated control URL
  http.addHeader("Content-Type", "application/json");

  // Send a dummy payload or an empty JSON object to trigger Node-RED to send control states
  String payload = "{}";

  Serial.print("Requesting control data: ");
  Serial.println(payload);

  int httpCode = http.POST(payload); // Send HTTP POST request (or GET if Node-RED is configured for it)

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.println("Response from Node-RED (control): " + response);

    // Parse JSON response for control commands
    StaticJsonDocument<1024> doc; // Menentukan ukuran untuk StaticJsonDocument (mungkin butuh lebih besar untuk kontrol)
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    // Update relay states based on Node-RED commands
    if (doc.containsKey("terrace_light")) {
      updateRelayStatus("Terrace Light", doc["terrace_light"]);
    }
    if (doc.containsKey("living_light")) {
      updateRelayStatus("Living Light", doc["living_light"]);
    }
    if (doc.containsKey("living_fan")) {
      updateRelayStatus("Living Fan", doc["living_fan"]);
    }
    if (doc.containsKey("bathroom_light")) {
      updateRelayStatus("Bathroom Light", doc["bathroom_light"]);
    }
    if (doc.containsKey("bedroom1_light")) {
      updateRelayStatus("Bedroom 1 Light", doc["bedroom1_light"]);
    }
    if (doc.containsKey("bedroom2_light")) {
      updateRelayStatus("Bedroom 2 Light", doc["bedroom2_light"]);
    }
    if (doc.containsKey("kitchen_light")) {
      updateRelayStatus("Kitchen Light", doc["kitchen_light"]);
    }

    // Update PIR Terrace Alarm arming status based on Node-RED command
    if (doc.containsKey("pir_terrace_alarm_armed")) {
      pirTerraceAlarmArmed = doc["pir_terrace_alarm_armed"];
      Serial.print("PIR Terrace Alarm Armed: ");
      Serial.println(pirTerraceAlarmArmed ? "TRUE" : "FALSE");
    }

  } else {
    Serial.printf("HTTP POST error (sendControlRequest): %s\n", http.errorToString(httpCode).c_str());
  }

  http.end(); // Close HTTP connection
}

// Helper function to update relay status
void updateRelayStatus(String relayName, bool state) {
  int pin = -1;
  if (relayName == "Terrace Light") pin = relayTerraceLight;
  else if (relayName == "Living Light") pin = relayLRLight;
  else if (relayName == "Living Fan") pin = relayLRFan;
  else if (relayName == "Bathroom Light") pin = relayBathLight;
  else if (relayName == "Bedroom 1 Light") pin = relayBR1Light;
  else if (relayName == "Bedroom 2 Light") pin = relayBR2Light;
  else if (relayName == "Kitchen Light") pin = relayKitchenLight;

  if (pin != -1) {
    // Assuming active LOW relays: LOW = ON, HIGH = OFF
    digitalWrite(pin, state ? LOW : HIGH);
    Serial.print(relayName);
    Serial.print(" set to: ");
    Serial.println(state ? "ON" : "OFF");
  }
}

// Function to handle RFID access control
void handleRFID() {
  // Check if a new card is present
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Select the card
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Print PICC type for debugging (tanpa PICC_Type_str)
  Serial.print("PICC type SAK: 0x"); // Menambahkan cetak SAK
  Serial.println(rfid.uid.sak, HEX); // Mencetak SAK dalam heksadesimal

  // Construct UID string from the scanned card
  Serial.print("UID Tag :");
  String currentCardUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
    currentCardUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    currentCardUID += String(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
  currentCardUID.toUpperCase(); // Convert to uppercase for consistent comparison

  // Compare scanned UID with the master card UID
  bool uidMatch = true;
  if (rfid.uid.size != sizeof(masterCardUID)) {
    uidMatch = false;
  } else {
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] != masterCardUID[i]) {
        uidMatch = false;
        break;
      }
    }
  }

  if (uidMatch) {
    Serial.println("Access Granted!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SELAMAT DATANG!");
    lcd.setCursor(0, 1);
    lcd.print("Pintu Terbuka");
    doorServo.write(servoOpenAngle); // Open door
    delay(3000); // Keep door open for 3 seconds
    doorServo.write(servoCloseAngle); // Close door
    rfidFailedAttempts = 0; // Reset failed attempts on successful access
    lcd.clear();
  } else {
    Serial.println("Access Denied!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("COBA LAGI!");
    rfidFailedAttempts++;
    if (rfidFailedAttempts >= MAX_RFID_FAILED_ATTEMPTS) {
      Serial.println("Too many failed attempts! Activating alarm.");
      lcd.setCursor(0, 1);
      lcd.print("ALARM AKTIF!");
      activateAlarm(ALARM_DURATION);
      rfidFailedAttempts = 0; // Reset after alarm activation
    }
    delay(2000); // Display "COBA LAGI" for 2 seconds
    lcd.clear();
  }

  rfid.PICC_HaltA(); // Halt PICC (Personal Identification Card)
  rfid.PCD_StopCrypto1(); // Stop encryption on PCD (Proximity Coupling Device)
}

// Function to handle PIR motion sensors
void handlePIRSensors() {
  // PIR Kamar Mandi (Automatic Light)
  int pirBathroomState = digitalRead(pinPIRBathroom);
  if (pirBathroomState == HIGH) {
    // Motion detected in bathroom, turn on light
    digitalWrite(relayBathLight, LOW); // ON (assuming active LOW relay)
    Serial.println("Motion detected in Bathroom. Light ON.");
  } else {
    // No motion, turn off light
    digitalWrite(relayBathLight, HIGH); // OFF (assuming active LOW relay)
  }

  // PIR Teras (Home Alarm System)
  int pirTerraceState = digitalRead(pinPIRTerrace);
  if (pirTerraceAlarmArmed && pirTerraceState == HIGH) {
    // Alarm is armed (from Node-RED) and motion detected on terrace
    Serial.println("PIR Terrace ALARM TRIGGERED!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PIR Teras ALARM!");
    activateAlarm(ALARM_DURATION);
  }
}

// Function to handle Gas sensor
void handleGasSensor() {
  int gasValue = analogRead(intGasSensor); // Menggunakan intGasSensor
  Serial.print("Gas Sensor Value: ");
  Serial.println(gasValue);

  if (gasValue > 3000) { // Threshold for gas detection (adjust as per sensor calibration)
    Serial.println("Gas Leak Detected! Activating alarm.");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("GAS BOCOR!");
    lcd.setCursor(0, 1);
    lcd.print("ALARM AKTIF!");
    activateAlarm(ALARM_DURATION);
  }
}

// Function to activate the alarm (buzzer) for a specified duration
void activateAlarm(long duration) {
  if (alarmStartTime == 0) { // Only activate if the alarm is not already active
    alarmStartTime = millis();
    digitalWrite(pinBuzzer, HIGH); // Turn on buzzer
    Serial.println("Alarm activated.");
  }
}