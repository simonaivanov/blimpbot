#include <LiquidCrystal.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define uS_TO_S_FACTOR 1000000ULL // conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  30         // time esp32 will go to sleep (in seconds)
unsigned long wakeStartTime = 0;
unsigned long timeAwake = 15000;

const char* ssid = "Simona’s iPhone";
const char* password = "bojko&gizmo";

const char* laptopIP = "172.20.10.9";
 
LiquidCrystal lcd(19, 23, 18, 17, 16, 15);
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool lowPowerFlag = false;

// input logic
// buttons
const int numIn = 2; // number of buttons
const int inPins[numIn] = {32, 33};
int back_button = 32;
int forward_button = 33;
int inputState[numIn];
int lastInputState[numIn] = {0, 0};
bool inputFlags[numIn] = {0, 0};
long lastDebounceTime[numIn] = {0, 0};
long debounceDelay = 50;
// sensors
float turbidity_voltage = 0;
int turbidityPin = 36;
String lastTurbidity = "";
int lastScreen = 0;
String lastScreenValue = "";


int tempPin = 21;
OneWire oneWire(tempPin);
DallasTemperature DS18B20(&oneWire);
float tempC;
String lastTemp;
unsigned long lastTempRequestTime = 0;

//low power mode
int powerPin = 25;
int greenLEDPin = 2;
int blueLEDPin = 4;
int lcdBackLightPin = 27;
int turbPowerPin = 13;
int tempPowerPin = 26;

// LCD scrolling menu
const int numOfScreens = 3;
int currentScreen = 0;
String screens [numOfScreens][2]  = {
  {"Temperature", "Degrees"}, 
  {"Turbidity", "% Clear"}, 
  {"Fish", ""}
};
struct fish_specs {
  String name;
  float temp_low;
  float temp_high;
  float turbidity_low;
  float turbidity_high;
};
fish_specs fish_lookup[] = {
  {"Catfish", 27, 37, 10, 40}, // temperatures indicate ideal spawning range and ability to hunt
  {"Largemouth Bass", 27, 37, 60, 85},
  {"Carp", 20, 28, 25, 50},
  {"Koi", 18, 24, 70, 95},
  {"Bluegill", 27, 37, 80, 100},
  {"Green Sunfish", 27, 37, 0, 30}, 
  {"White Crappie", 27, 37, 50, 100}, 
  {"Betafish", 24, 28, 90, 100},
  {"Salmon", 8, 18, 90, 100}, 
  {"Smelt", 10, 15, 30, 75}, 
  {"Trout", 12, 18, 80, 95}, 
  {"Walleye", 15, 21, 50, 80}, 
  {"Muskellunge", 15, 21, 80, 100},
  {"Northern Pike", 15, 21, 50, 90},
  {"Yellow Perch", 15, 21, 90, 100}
};
boolean fish_found = false;
String sensor_readings[numOfScreens];

void setup() {
  Serial.begin(115200);
  delay(1000); 

  //increment boot number and print it every reboot
  ++bootCount;
  Serial.println("boot number: " + String(bootCount));

  lcd.begin(16, 2);
  pinMode(forward_button, INPUT_PULLUP);
  pinMode(back_button, INPUT_PULLUP);
  pinMode(turbidityPin, INPUT);
  pinMode(powerPin, INPUT_PULLUP);
  pinMode(blueLEDPin, OUTPUT);
  pinMode(greenLEDPin, OUTPUT);
  pinMode(lcdBackLightPin,OUTPUT);
  pinMode(turbPowerPin, OUTPUT);
  pinMode(tempPowerPin, OUTPUT);

  DS18B20.begin();
  DS18B20.setWaitForConversion(false);
  digitalWrite(blueLEDPin, HIGH);

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  if (!lowPowerFlag || wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) { // go into normal mode
    lowPowerFlag = false;
    digitalWrite(lcdBackLightPin, 1);
    digitalWrite(turbPowerPin, 1);
    digitalWrite(tempPowerPin, 1);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Waking Up!");
    lcd.setCursor(0,1);
    lcd.print("Normal Mode");
    digitalWrite(greenLEDPin, LOW);
    digitalWrite(blueLEDPin, HIGH);
    delay(1000);
    updateScreen();
    return;
  }
  if (lowPowerFlag) {
    unsigned long startTime = millis();
    
    bool dataSent = false;
    DS18B20.requestTemperatures();
    if (!dataSent) {
      digitalWrite(turbPowerPin, 1);
      digitalWrite(tempPowerPin, 1);
      digitalWrite(blueLEDPin, LOW);
      digitalWrite(greenLEDPin, HIGH);
      WiFi.begin(ssid, password);
      Serial.println("wifi connection started");
      unsigned long wifiStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < timeAwake) {
        delay(200);
        Serial.print(".");
      }
      while (millis() - startTime < timeAwake) {
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("wifi connected");
          float temp = DS18B20.getTempCByIndex(0);
          float turbidity = analogRead(turbidityPin);
          float turb_perc = (turbidity / 4095.0) * 100;
          sendData(temp, turb_perc);
        } else {
          Serial.println("wifi not connected");
        }
      }
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      dataSent = true;
    }
    delay(3000);
    // delay(timeAwake);
    // delay(3000);
    digitalWrite(greenLEDPin, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)powerPin, 0); //enables with same button as wakeup trigger
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR); // otherwise wake up every 10 seconds
    digitalWrite(turbPowerPin, 0);
    digitalWrite(tempPowerPin, 0);
    digitalWrite(lcdBackLightPin, 0);
    esp_deep_sleep_start();
  }
}
 
void loop() {
  if (digitalRead(powerPin) == LOW) {
    delay(debounceDelay);
    if (digitalRead(powerPin) == LOW) {
      lowPowerFlag = !lowPowerFlag;
      lcd.clear();
      lcd.setCursor(0,0);
      if (lowPowerFlag) {
        lcd.print("Low Power Mode");
        digitalWrite(blueLEDPin, LOW);
        digitalWrite(greenLEDPin, HIGH);
        delay(1000);
        startLowPower();
      } else {
        lcd.print("Normal Mode");
        digitalWrite(greenLEDPin, LOW);
        digitalWrite(blueLEDPin, HIGH);
        delay(1000);
      }
    }
  }
  

  setInputFlags();
  resolveInputFlags();

  digitalWrite(lcdBackLightPin, 1);
  digitalWrite(turbPowerPin, 1);
  digitalWrite(tempPowerPin, 1);

  float turbidity = 0;
  turbidity_voltage = 0;
  for(int i = 0; i < 2000; i++) {
    turbidity = analogRead(turbidityPin);
    turbidity_voltage += (turbidity / 4095.0) * 3.3;
  }
  turbidity_voltage = turbidity_voltage / 2000;

  unsigned long current_time = millis();
  if (current_time - lastTempRequestTime >= 1000) {
    lastTempRequestTime = current_time;
    tempC = DS18B20.getTempCByIndex(0);
    DS18B20.requestTemperatures();
  }
  float turb_perc = turbidity_voltage / 3.3 * 100;
  sensor_readings[0] = String(tempC, 0);
  sensor_readings[1] = String(turb_perc, 0);
  int fish_lookup_size = sizeof(fish_lookup) / sizeof(fish_lookup[0]);
  Serial.println(fish_lookup_size);
  Serial.println(tempC);
  Serial.println(turbidity_voltage);
  for (int i = 0; i < fish_lookup_size; i++) {
    if (tempC >= fish_lookup[i].temp_low && tempC <= fish_lookup[i].temp_high && turb_perc >= fish_lookup[i].turbidity_low && turb_perc <= fish_lookup[i].turbidity_high) {
      sensor_readings[2] = fish_lookup[i].name;
      Serial.println(fish_lookup[i].name);
      fish_found = true;
      break;
    }
    fish_found = false;
  }
  if (!fish_found) {
    sensor_readings[2] = "None";
  };

  if (currentScreen != lastScreen || sensor_readings[currentScreen] != lastScreenValue) {
    updateSensorValue();
    lastScreen = currentScreen;
    lastScreenValue = sensor_readings[currentScreen];
  }
}

void setInputFlags() {
  for (int i = 0; i < numIn; i++) {
    int reading = digitalRead(inPins[i]);
    if (reading != lastInputState[i]) {
      lastDebounceTime[i] = millis();
    }
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (reading != inputState[i]) {
        inputState[i] = reading;
        if (inputState[i] == LOW) {
          inputFlags[i] = HIGH;
        }
      }
    }
    lastInputState[i] = reading;
  }
}
void scrollingAction(int input) {
  if (input == 0) {
    if (currentScreen == 0) {
      currentScreen = numOfScreens - 1;
    } else {
      currentScreen--;
    }
  } else if (input == 1) {
    if (currentScreen == numOfScreens - 1) {
      currentScreen = 0;
    } else {
      currentScreen++;
    }
  }
}
void resolveInputFlags() {
  for (int i = 0; i < numIn; i++) {
    if (inputFlags[i] == HIGH) {
      scrollingAction(i);
      inputFlags[i] = 0;
      updateScreen();
    }
  }
}
void updateScreen() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(screens[currentScreen][0]);
  if (currentScreen != 2) {
    lcd.setCursor(0, 1);
    lcd.print("     ");
    lcd.print(screens[currentScreen][1]);
  }
}
void updateSensorValue() {
  lcd.setCursor(0, 1);
  if (currentScreen == 0 || currentScreen == 1) {
    lcd.print("     ");
    lcd.setCursor(0, 1);
    lcd.print(sensor_readings[currentScreen]);
  } else {
    lcd.print("                ");
    lcd.setCursor(0,1);
    lcd.print(sensor_readings[currentScreen]);
  }
}
void startLowPower(){
  lowPowerFlag = true;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("I'm sleepy...");
  lcd.setCursor(0,1);
  delay(1000);
  lcd.print("Goodbye...");
  digitalWrite(blueLEDPin, LOW);
  digitalWrite(greenLEDPin, HIGH);
  delay(3000);
  digitalWrite(greenLEDPin, LOW);
  
  lcd.noDisplay();
  digitalWrite(lcdBackLightPin, 0);
  digitalWrite(turbPowerPin, 0);
  digitalWrite(tempPowerPin, 0);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)powerPin, 0); //enables with same button as wakeup trigger
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR); // otherwise wake up every 30 seconds
  esp_deep_sleep_start();
}
void sendData(float temp, float turb) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("connected to wifi, ready to send data");
    HTTPClient http;
    String url = "http://" + String(laptopIP) + ":5000/log?";
    url += "temperature=" + String(temp);
    url += "&turbidity=" + String(turb);
    Serial.println("Sending request to:");
    Serial.println(url);
    http.begin(url);
    int httpResponseCode = http.GET();
    Serial.print("http response: ");
    Serial.println(httpResponseCode);
    http.end();
  } else {
    Serial.println("can't connect to wifi");
  }
}
