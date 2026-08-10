/*
 * ==============================================================================
 * CEAS NTP PE ESP32-C3 MINI CU ECRAN ST7920 (128x64) + VREME OPEN-METEO
 * ==============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>         
#include <U8g2lib.h>
#include <Wire.h>
#include <time.h>                
#include <HTTPClient.h>           
#include <ArduinoJson.h>          

#define SCLK_PIN 2   
#define MOSI_PIN 3   
#define CS_PIN   4   

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R2, SCLK_PIN, MOSI_PIN, CS_PIN, U8X8_PIN_NONE);

const long timezoneOffset = 2; 
const char          *ntpServer  = "pool.ntp.org"; 
const unsigned long updateDelay = 900000;         
const unsigned long retryDelay  = 5000;           
const unsigned long wifiCheckInterval = 30000;    
const unsigned long weatherInterval = 600000;     

unsigned long lastUpdatedTime = updateDelay * -1;
unsigned long lastWifiCheckTime = 0;
unsigned long lastWeatherUpdate = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer);

int currentOffset = timezoneOffset * 3600; 

int ora = 0;
int minut = 0;
int secunda = 0;
int zi, zi2, luna, an;
String weekDays1[7]={"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String weekDays2[7]={"Duminica", "Luni", "Marti", "Miercuri", "Joi", "Vineri", "Sambata"};

bool updated;

float temperatura = 0.0;
int umiditate = 0;

const char* lat = "44.3167";  // "44.4268"; // Schimbă cu latitudinea ta
const char* lon = "23.8000";  //"26.1025"; // Schimbă cu longitudinea ta

void getDate() {
  time_t rawtime = timeClient.getEpochTime();
  struct tm * ti;
  ti = localtime (&rawtime);
  zi = ti->tm_mday;
  luna = ti->tm_mon + 1;
  an = ti->tm_year + 1900;
  zi2 = timeClient.getDay();
}

void updateWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat) + 
                 "&longitude=" + String(lon) + 
                 "&current=temperature_2m,relative_humidity_2m";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      temperatura = doc["current"]["temperature_2m"].as<float>();
      umiditate = doc["current"]["relative_humidity_2m"].as<int>();
    }
    http.end();
  }
}

void updateDSTOffset() {
  time_t rawtime = timeClient.getEpochTime();
  if (rawtime < 100000) return; 

  time_t approxUtc = rawtime - currentOffset;
  struct tm * utcTi = gmtime(&approxUtc); 
  
  int utcDay = utcTi->tm_mday;
  int utcMonth = utcTi->tm_mon + 1; 
  int utcDow = utcTi->tm_wday;      
  int utcHour = utcTi->tm_hour;

  bool dstActive = false;

  if (utcMonth > 3 && utcMonth < 10) {
    dstActive = true;
  } else if (utcMonth == 3) {
    int daysToEnd = 31 - utcDay;
    int dowOf31 = (utcDow + daysToEnd) % 7;
    int lastSundayMarch = 31 - dowOf31;
    if (utcDay > lastSundayMarch || (utcDay == lastSundayMarch && utcHour >= 1)) dstActive = true;
  } else if (utcMonth == 10) {
    int daysToEnd = 31 - utcDay;
    int dowOf31 = (utcDow + daysToEnd) % 7;
    int lastSundayOctober = 31 - dowOf31;
    if (utcDay < lastSundayOctober || (utcDay == lastSundayOctober && utcHour < 1)) dstActive = true;
  }

  int newOffset = (timezoneOffset + (dstActive ? 1 : 0)) * 3600;
  if (currentOffset != newOffset) {
     timeClient.setTimeOffset(newOffset);
     currentOffset = newOffset; 
  }
}

void setup()
{
    Serial.begin(115200);
    
    u8g2.begin();
    u8g2.setContrast(255);
    u8g2.clearBuffer(); 
    u8g2.firstPage();
    do {
        u8g2.setFont(u8g2_font_profont12_mr); 
        u8g2.drawStr(0, 20, "Ceas NTP + Vreme");  
        u8g2.drawStr(0, 35, "Se conecteaza...");
    } while ( u8g2.nextPage() );  
    delay(2000);
   
    WiFiManager wifiManager;
    wifiManager.autoConnect("AutoConnectAP");

    timeClient.setTimeOffset(currentOffset);
    timeClient.begin();

    updateWeather();
    lastWeatherUpdate = millis();
}

void loop() {
  if (millis() - lastWifiCheckTime >= wifiCheckInterval) {
    lastWifiCheckTime = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect(); 
    }
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastUpdatedTime >= updateDelay) {
    updated = timeClient.update();
    if (updated) {
      getDate();
      lastUpdatedTime = millis();
    } else {
      lastUpdatedTime = millis() - updateDelay + retryDelay;
    }
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastWeatherUpdate >= weatherInterval) {
    updateWeather();
    lastWeatherUpdate = millis();
  }
 
  updateDSTOffset(); 

  ora = timeClient.getHours();
  minut = timeClient.getMinutes();
  secunda = timeClient.getSeconds();

  ceas();
  delay(1000); 
}  

void ceas()
{
  char m_str[3];
  strcpy(m_str, u8x8_u8toa(minut, 2));   
  
  int displayState = (millis() / 3000) % 8;
  
  u8g2.firstPage();
  do {
    // --- ORA MARE (Y=42 exact) ---
    u8g2.setFont(u8g2_font_7Segments_26x42_mn);
    if (ora < 10) {
      u8g2.setCursor(32, 42); 
    } else {
      u8g2.setCursor(0, 42);
    }    
    u8g2.print(ora);    
    
    if (secunda % 2 == 0)
      u8g2.drawStr(58, 42, ":"); 
      
    u8g2.drawStr(69, 42, m_str);

    // --- ZONA DE JOS ---
    u8g2.setCursor(0, 61); 

    // ================= BLOCUL ÎN ROMÂNĂ (0 - 3) =================
    if (displayState == 0) {
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi);
      u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna);
      u8g2.print(".");
      u8g2.print(an);
      
    } else if (displayState == 1) {
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(weekDays2[zi2]);
        
    } else if (displayState == 2) {
      // TEXT MIC PENTRU ETICHETĂ
      u8g2.setFont(u8g2_font_helvB08_tr); 
      u8g2.print("Temperatura: "); 
      
      // VALOARE MARE (helvB10 nu are simbolul °, îl desenăm manual)
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      
      int gradX = u8g2.getCursorX() + 3; 
      int gradY = 51; 
      u8g2.drawCircle(gradX, gradY, 2);
      
      u8g2.setCursor(gradX + 5, 61);
      u8g2.print("C");
      
    } else if (displayState == 3) {
      u8g2.setFont(u8g2_font_helvB08_tr); 
      u8g2.print("Umiditatea: ");
      
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(umiditate);
      u8g2.print("%");
    } 

    // ================= BLOCUL ÎN ENGLEZĂ (4 - 7) =================
    else if (displayState == 4) {
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi);
      u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna);
      u8g2.print(".");
      u8g2.print(an);
      
    } else if (displayState == 5) {
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(weekDays1[zi2]);
        
    } else if (displayState == 6) {
      // TEXT MIC PENTRU ETICHETĂ
      u8g2.setFont(u8g2_font_helvB08_tr); 
      u8g2.print("Temperature: "); 
      
      // VALOARE MARE (helvB10 nu are simbolul °, îl desenăm manual)
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      
      int gradX = u8g2.getCursorX() + 3; 
      int gradY = 51; 
      u8g2.drawCircle(gradX, gradY, 2);
      
      u8g2.setCursor(gradX + 5, 61);
      u8g2.print("C");
      
    } else if (displayState == 7) {
      u8g2.setFont(u8g2_font_helvB08_tr); 
      u8g2.print("Humidity: ");
      
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(umiditate);
      u8g2.print("%");
    }
    
  } while ( u8g2.nextPage() );
}
