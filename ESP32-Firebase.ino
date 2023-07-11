#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h"
#include "time.h"

//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Insert your network credentials
#define WIFI_SSID  "4G UFI_381"
#define WIFI_PASSWORD "1234567890"

// Insert Firebase project API Key
#define API_KEY "AIzaSyBPoTGsMXzC2vjrwBL1El_LgP2syLJak3U"

// Insert RTDB URLefine the RTDB URL */
#define DATABASE_URL "projek-ta-greenhouse-default-rtdb.asia-southeast1.firebasedatabase.app"

//Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

#define pinBUZZ  4
#define pinDHT   5
#define pinLED2  18
#define pinLED1  19  
#define pinPH    32
#define pinLDR   33
#define pinMoist 35
#define typeDHT  DHT11
#define MAX_DATA_SIZE 60
DHT dht(pinDHT, typeDHT);

int ldr, moist, pH = 0;
float LDR;
float SM;
float PH;
float Temp;
float Hum;
double dataLDR[MAX_DATA_SIZE];
double dataTemp[MAX_DATA_SIZE];
double dataHum[MAX_DATA_SIZE];

// Double exponential smoothing parameters
double alpha = 0.2;
double beta = 0.3; 
int dataSize = 60;
int forecastSize = 15;
  
bool signupOK = false;

// Database main path (to be updated in setup with the user UID)
String databasePath;
// Database child nodes
String tempPath = "/temperature";
String humPath = "/humidity";
String ldrPath = "/light";
String phPath = "/ph";
String soilPath = "/soil";
String timePath = "/timestamp";

String parentPath;

int timeEpoch;
char timestamp[20];
FirebaseJson json;

const char* ntpServer = "pool.ntp.org";

// Timer variables (send new readings every minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 60000;

float readLight(){
  ldr = analogRead(pinLDR);
  float ldrPercentage = map(ldr, 0.0, 4095.0, 100.0, 0.0);
  return ldrPercentage;
  
  /*float Vb, Rldr, lux;
  Vb   = (ldr/3800.0)*3.3;
  Rldr = (4.7*Vb)/(3.3-Vb);
  lux  = -90*((Rldr*3.3)/15.0)+100.0;

  if(lux<0.0){
    lux = 0.0;
  }

  return ldr;*/
}

float readPH(){
  pH = analogRead(pinPH);
  float outputValue = (-0.0139*pH)+7.7851;
  return pH;
}

float readMoist(){
  moist = analogRead(pinMoist);
  float moistPercentage = map(moist, 0.0, 4095.0, 100.0, 0.0);
  return moistPercentage;
}

// Initialize WiFi
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  //Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(pinBUZZ, HIGH);
    delay(1000);
  }
}

// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void monitoringValue();
void doubleExponentialSmoothing(double *data, int dataSize, double alpha, double beta, int forecastSize);

void setup(){
  Serial.begin(9600);
  pinMode(pinBUZZ, OUTPUT);
  pinMode(pinLED1, OUTPUT);
  pinMode(pinLED2, OUTPUT);

  initWiFi();
  configTime(6*3600, 3600, ntpServer);
  
  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")){
    signupOK = true;
  }
  else{
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  databasePath = "/readings";
}

void loop(){
  LDR  = readLight();
  SM   = readMoist();
  PH   = readPH();
  Temp = dht.readTemperature();
  Hum  = dht.readHumidity();
  
  if (isnan(Temp) || isnan(Hum)) {
    return;
  }
      
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
    sendDataPrevMillis = millis();

    for(int i = 0; i < MAX_DATA_SIZE; i++) {    
      dataLDR[i]  = LDR;
      dataTemp[i] = Temp;
      dataHum[i]  = Hum;

      doubleExponentialSmoothing(dataLDR, dataSize, alpha, beta, forecastSize);
      doubleExponentialSmoothing(dataTemp, dataSize, alpha, beta, forecastSize);
      doubleExponentialSmoothing(dataHum, dataSize, alpha, beta, forecastSize);
    }
    
    //Get current timestamp
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      //Serial.println("Failed to obtain time");
      return;
    }
    strftime(timestamp, 20, "%d-%m-%Y %H:%M:%S", &timeinfo);
    timeEpoch = getTime();

    //Send to database    
    parentPath = databasePath + "/" + String(timeEpoch);
    json.set(tempPath.c_str(), String(Temp));
    json.set(humPath.c_str(), String(Hum));
    json.set(ldrPath.c_str(), String(LDR));
    json.set(soilPath.c_str(), String(SM));
    json.set(phPath.c_str(), String(PH));
    json.set(timePath, String(timestamp));
    Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json);
    
    monitoringValue();
    
  }
}

void monitoringValue(){
  Firebase.RTDB.setFloat(&fbdo, "Node2/Sensor1/Nilai", Hum);
  Firebase.RTDB.setFloat(&fbdo, "Node2/Sensor2/Nilai", Temp);
  Firebase.RTDB.setFloat(&fbdo, "Node2/Sensor3/Nilai", LDR);
  Firebase.RTDB.setFloat(&fbdo, "Node2/Sensor4/Nilai", SM);
  Firebase.RTDB.setFloat(&fbdo, "Node2/Sensor5/Nilai", random(6.0,7.0));
}

void doubleExponentialSmoothing(double *dataSensor, int dataSize, double alpha, double beta, int forecastSize) {
    double level[MAX_DATA_SIZE];
    double trend[MAX_DATA_SIZE];

    // Initialize level and trend components
    level[0] = dataSensor[0];
    trend[0] = 0;

    // Double exponential smoothing
    for (int i = 1; i < dataSize; i++) {
        // Update level component
        level[i] = alpha * dataSensor[i] + (1 - alpha) * (level[i - 1] + trend[i - 1]);

        // Update trend component
        trend[i] = beta * (level[i] - level[i - 1]) + (1 - beta) * trend[i - 1];
    }

    // Forecast future values
    for (int i = 0; i < forecastSize; i++) {
        int dataIndex = dataSize + i;

        double forecast = level[dataSize - 1] + (i + 1) * trend[dataSize - 1];
    }
}
