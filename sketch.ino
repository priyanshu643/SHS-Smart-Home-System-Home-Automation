#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ONE_WIRE_BUS 4
const int buzzer = 14;
const int gassensor = 34;
const int heater = 16;
const int cooler = 0;
const int aripurifier = 15;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const char* ssid = "Wokwi-GUEST";
const char* password = "";
WebServer server(80);

float currentTemperatureC = DEVICE_DISCONNECTED_C;
int currentGasRaw = 0;
float currentGasPercent = 0;

float heatBelowTemp = 20.0;
float coolAboveTemp = 25.0;

void showTemporaryMessage(String msg, int duration);
void updateNormalDisplay();
void tempControlLogic();
void airPurifierControl();
void handleSetTemp();
void connectToWiFi();

void setup() {
  Serial.begin(115200);
  
  sensors.begin();
  Serial.println("DS18B20 sensor initialized.");
  if (sensors.getDeviceCount() == 0) {
    Serial.println("CRITICAL: No DS18B20 sensors found!");
  }

  pinMode(buzzer, OUTPUT);
  digitalWrite(buzzer, LOW);
  pinMode(gassensor, INPUT);
  pinMode(heater, OUTPUT);
  digitalWrite(heater, HIGH);
  pinMode(cooler, OUTPUT);
  digitalWrite(cooler, HIGH);
  pinMode(aripurifier, OUTPUT);
  digitalWrite(aripurifier, HIGH);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("System Booting...");
  display.display();
  delay(1000);

  connectToWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    server.on("/settemp", HTTP_GET, handleSetTemp);
    server.begin();
    Serial.println("HTTP server started. API available at /settemp");
    showTemporaryMessage("API Ready\nIP: " + WiFi.localIP().toString(), 5000);
  } else {
    showTemporaryMessage("WiFi Failed\nAPI Offline", 5000);
  }
  updateNormalDisplay();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  sensors.requestTemperatures(); 
  currentTemperatureC = sensors.getTempCByIndex(0);
  currentGasRaw = analogRead(gassensor);
  currentGasPercent = map(currentGasRaw, 0, 4095, 0, 100);

  tempControlLogic();    
  airPurifierControl(); 

  updateNormalDisplay();

  delay(1000);
}

void connectToWiFi() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting to WiFi...");
  display.display();
  Serial.print("Connecting to WiFi ");
  Serial.print(ssid);
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Connected!");
    display.print("IP: ");
    display.println(WiFi.localIP().toString());
    display.display();
    delay(2000);
  } else {
    Serial.println("\nFailed to connect to WiFi.");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("WiFi Failed!");
    display.display();
    delay(2000);
  }
}

void showTemporaryMessage(String msg, int duration) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  int y = 0;
  int currentLineStart = 0;
  for (int i = 0; i < msg.length(); i++) {
    if (msg.charAt(i) == '\n') {
      display.setCursor(0, y);
      display.print(msg.substring(currentLineStart, i));
      y += 10;
      if (y >= SCREEN_HEIGHT - 8) break;
      currentLineStart = i + 1;
    }
  }
  if (y < SCREEN_HEIGHT - 8) {
    display.setCursor(0, y); 
    display.print(msg.substring(currentLineStart));
  }
  
  display.display();
  delay(duration);
}

void updateNormalDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.print("Gas: ");
  display.print(currentGasPercent, 0);
  display.println("%");

  display.print("Temp: ");
  if (currentTemperatureC == DEVICE_DISCONNECTED_C || isnan(currentTemperatureC)) {
    display.println("Err C");
    Serial.println("Error: Failed to read temperature from DS18B20 sensor!");
  } else {
    display.print(currentTemperatureC, 1);
    display.println(" C");
  }

  display.setCursor(0, 20);
  display.print("Heat <"); display.print(heatBelowTemp, 0);
  display.print("C Cool >"); display.print(coolAboveTemp, 0); display.println("C");

  display.setCursor(0, 32);
  display.print("H:"); display.print(digitalRead(heater) == LOW ? "ON " : "OFF");
  display.print(" C:"); display.print(digitalRead(cooler) == LOW ? "ON " : "OFF");
  display.print(" P:"); display.print(digitalRead(aripurifier) == LOW ? "ON" : "OFF");
  
  display.display();
}

void tempControlLogic() {
  static bool prevHeaterState = (digitalRead(heater) == LOW);
  static bool prevCoolerState = (digitalRead(cooler) == LOW);

  if (currentTemperatureC == DEVICE_DISCONNECTED_C || isnan(currentTemperatureC)) {
    digitalWrite(heater, HIGH);
    digitalWrite(cooler, HIGH);
    return;
  }

  bool desiredHeaterStateON = false;
  bool desiredCoolerStateON = false;

  if (currentTemperatureC <= heatBelowTemp) {
    desiredHeaterStateON = true;
    desiredCoolerStateON = false;
  } else if (currentTemperatureC >= coolAboveTemp) {
    desiredHeaterStateON = false;
    desiredCoolerStateON = true;
  } else {
    desiredHeaterStateON = false;
    desiredCoolerStateON = false;
  }

  digitalWrite(heater, desiredHeaterStateON ? LOW : HIGH);
  digitalWrite(cooler, desiredCoolerStateON ? LOW : HIGH);

  bool actualHeaterIsON = (digitalRead(heater) == LOW);
  bool actualCoolerIsON = (digitalRead(cooler) == LOW);

  if (actualHeaterIsON && !prevHeaterState) {
    showTemporaryMessage("Heater Turning ON", 5000);
    digitalWrite(buzzer, HIGH); delay(100); digitalWrite(buzzer, LOW);
  } else if (!actualHeaterIsON && prevHeaterState) {
  }

  if (actualCoolerIsON && !prevCoolerState) {
    showTemporaryMessage("Cooler Turning ON", 5000);
    digitalWrite(buzzer, HIGH); delay(100); digitalWrite(buzzer, LOW);
  } else if (!actualCoolerIsON && prevCoolerState) {
  }

  prevHeaterState = actualHeaterIsON;
  prevCoolerState = actualCoolerIsON;
}

void airPurifierControl() {
  static bool prevPurifierState = (digitalRead(aripurifier) == LOW);

  bool desiredPurifierStateON = false;

  if (currentGasRaw > 2500) {
    desiredPurifierStateON = true;
  } else {
    desiredPurifierStateON = false;
  }

  digitalWrite(aripurifier, desiredPurifierStateON ? LOW : HIGH);

  bool actualPurifierIsON = (digitalRead(aripurifier) == LOW);

  if (actualPurifierIsON && !prevPurifierState) {
    showTemporaryMessage("Air Purifier ON", 5000);
    digitalWrite(buzzer, HIGH); delay(200); digitalWrite(buzzer, LOW);
  } else if (!actualPurifierIsON && prevPurifierState) {
  }
  prevPurifierState = actualPurifierIsON;
}

void handleSetTemp() {
  String heatStr = server.arg("heat_below");
  String coolStr = server.arg("cool_above");
  String message = "API: Thresholds Set\n";
  bool updated = false;

  if (heatStr != "") {
    float newHeat = heatStr.toFloat();
    if (newHeat > 0 && newHeat < 50) {
        heatBelowTemp = newHeat;
        message += "Heat < " + String(heatBelowTemp, 1) + "C\n";
        updated = true;
    } else {
        message += "Invalid Heat Val\n";
    }
  }
  if (coolStr != "") {
    float newCool = coolStr.toFloat();
     if (newCool > 0 && newCool < 60 && newCool > heatBelowTemp) {
        coolAboveTemp = coolStr.toFloat();
        message += "Cool > " + String(coolAboveTemp, 1) + "C";
        updated = true;
    } else {
        message += "Invalid Cool Val";
         if (!(newCool > heatBelowTemp)) message += "\n(Cool > Heat Fail)";
    }
  }

  if (updated) {
    server.send(200, "text/plain", "Thresholds updated. Heat Below: " + String(heatBelowTemp) + ", Cool Above: " + String(coolAboveTemp));
    showTemporaryMessage(message, 5000);
  } else {
    server.send(400, "text/plain", "No valid parameters or values provided. Use e.g. /settemp?heat_below=19.5&cool_above=26.0");
    showTemporaryMessage("API: Invalid Data", 3000);
  }
}