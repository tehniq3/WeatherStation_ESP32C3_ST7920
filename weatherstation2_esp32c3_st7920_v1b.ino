/*
 * ==============================================================================
 * PROIECT: Stație Meteo Bilingvă cu Ceas Analog și Digital
 * ==============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>         
#include <U8g2lib.h>
#include <time.h>                
#include <HTTPClient.h>           
#include <ArduinoJson.h>          

#define SCLK_PIN 2   
#define MOSI_PIN 3   
#define CS_PIN   4   

U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R2, SCLK_PIN, MOSI_PIN, CS_PIN, U8X8_PIN_NONE);

const char* ntpServer = "pool.ntp.org"; 

const unsigned long wifiCheckInterval = 30000;
const unsigned long weatherInterval = 600000;

unsigned long lastWifiCheckTime = 0;
unsigned long lastWeatherUpdate = 0;

int ora = 0;
int minut = 0;
int secunda = 0;
int zi, zi2, luna, an;
String weekDays1[7]={"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String weekDays2[7]={"Duminica", "Luni", "Marti", "Miercuri", "Joi", "Vineri", "Sambata"};

// Variabile meteo
float temperatura = 0.0;
int umiditate = 0;
int codVreme = 0;
float vitezaVant = 0.0;
int directieVant = 0;
float presiuneHpa = 0.0;
float indiceUV = 0.0;
int calitateAer = 0;

// Coordonate GPS (Craiova)
const char* lat = "44.3167";  
const char* lon = "23.8000";  

DynamicJsonDocument docMeteo(2048);
DynamicJsonDocument docAer(512);

// Variabile pentru desenul ceasului
float Vinkel = 0;
int X2 = 0, Y2 = 0, X3 = 0, Y3 = 0;

// ================= FUNCȚII DE TRADUCERE VREME =================
String getVremeRO(int code) {
  switch(code) {
    case 0: return "Senin"; case 1: return "Predom. senin"; case 2: return "Partial noros";
    case 3: return "Noros"; case 45: case 48: return "Ceata";
    case 51: case 53: case 55: return "Burnita"; case 56: case 57: return "Burnita inghet.";
    case 61: case 63: case 65: return "Ploaie"; case 66: case 67: return "Ploaie inghet.";
    case 71: case 73: case 75: case 77: return "Ninsoare";
    case 80: case 81: case 82: return "Averse"; case 85: case 86: return "Ninsoare trec.";
    case 95: return "Furtuna"; case 96: case 99: return "Furtuna+Grind.";
    default: return "N/A";
  }
}

String getVremeEN(int code) {
  switch(code) {
    case 0: return "Clear sky"; case 1: return "Mainly clear"; case 2: return "Partly cloudy";
    case 3: return "Overcast"; case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle"; case 56: case 57: return "Frz. drizzle";
    case 61: case 63: case 65: return "Rain"; case 66: case 67: return "Frz. rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Rain showers"; case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm"; case 96: case 99: return "T-Storm/Hail";
    default: return "N/A";
  }
}

String getDirectieVantRO(int grade) {
  if (grade >= 337.5 || grade < 22.5) return "N"; if (grade >= 22.5 && grade < 67.5) return "NE";
  if (grade >= 67.5 && grade < 112.5) return "E"; if (grade >= 112.5 && grade < 157.5) return "SE";
  if (grade >= 157.5 && grade < 202.5) return "S"; if (grade >= 202.5 && grade < 247.5) return "SV";
  if (grade >= 247.5 && grade < 292.5) return "V"; if (grade >= 292.5 && grade < 337.5) return "NV";
  return "N";
}

String getDirectieVantEN(int grade) {
  if (grade >= 337.5 || grade < 22.5) return "N"; if (grade >= 22.5 && grade < 67.5) return "NE";
  if (grade >= 67.5 && grade < 112.5) return "E"; if (grade >= 112.5 && grade < 157.5) return "SE";
  if (grade >= 157.5 && grade < 202.5) return "S"; if (grade >= 202.5 && grade < 247.5) return "SW";
  if (grade >= 247.5 && grade < 292.5) return "W"; if (grade >= 292.5 && grade < 337.5) return "NW";
  return "N";
}

String getCalificativUV_RO(float uv) {
  if (uv <= 2) return "Scazut"; if (uv <= 5) return "Moderat";
  if (uv <= 7) return "Ridicat"; if (uv <= 10) return "Foarte ridicat"; return "Extrem";
}

String getCalificativUV_EN(float uv) {
  if (uv <= 2) return "Low"; if (uv <= 5) return "Moderate";
  if (uv <= 7) return "High"; if (uv <= 10) return "Very High"; return "Extreme";
}

String getCalificativAQI_RO(int aqi) {
  if (aqi <= 20) return "Bun"; if (aqi <= 40) return "Acceptabil"; if (aqi <= 60) return "Moderat";
  if (aqi <= 80) return "Slab"; if (aqi <= 100) return "Foarte slab"; return "Extrem de slab";
}

String getCalificativAQI_EN(int aqi) {
  if (aqi <= 20) return "Good"; if (aqi <= 40) return "Fair"; if (aqi <= 60) return "Moderate";
  if (aqi <= 80) return "Poor"; if (aqi <= 100) return "Very Poor"; return "Extremely Poor";
}

// ================= FUNCȚII METEO API =================
void updateWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat) + 
                 "&longitude=" + String(lon) + 
                 "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl,uv_index";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      deserializeJson(docMeteo, payload);
      temperatura = docMeteo["current"]["temperature_2m"].as<float>();
      umiditate = docMeteo["current"]["relative_humidity_2m"].as<int>();
      codVreme = docMeteo["current"]["weather_code"].as<int>();
      vitezaVant = docMeteo["current"]["wind_speed_10m"].as<float>();
      directieVant = docMeteo["current"]["wind_direction_10m"].as<int>();
      presiuneHpa = docMeteo["current"]["pressure_msl"].as<float>();
      indiceUV = docMeteo["current"]["uv_index"].as<float>();
    }
    http.end();
  }
}

void updateAirQuality() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(lat) + 
                 "&longitude=" + String(lon) + "&current=european_aqi";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      deserializeJson(docAer, payload);
      calitateAer = docAer["current"]["european_aqi"].as<int>();
    }
    http.end();
  }
}

// ================= FUNCȚII CEAS ANALOGIC =================
void TegnViser(float Omdreining, float forhold, int Radius, int CX, int CY) {
    Vinkel = Omdreining * 2.0 * 3.1415 / forhold - 1.5707;  
    X2 = CX + Radius * cos(Vinkel);
    Y2 = CY + Radius * sin(Vinkel);
    u8g2.drawLine(CX, CY, X2, Y2);
}

void DeseneazaCeasMare(int CX, int CY) { 
    u8g2.drawCircle(CX, CY, 1); 
    
    TegnViser(ora % 12, 12.0, 11, CX, CY);   
    TegnViser(minut, 60.0, 17, CX, CY);     
    TegnViser(secunda, 60.0, 22, CX, CY);   
  
    for(int i = 0; i < 12; i++) { 
      Vinkel = i / 12.0 * 2 * 3.1415;
      int r1 = 24; int r2 = 22; 
      if (i % 3 == 0) { r1 = 24; r2 = 21; } 
      
      X2 = CX + r1 * cos(Vinkel);
      Y2 = CY + r1 * sin(Vinkel);
      X3 = CX + r2 * cos(Vinkel);
      Y3 = CY + r2 * sin(Vinkel);
      u8g2.drawLine(X2, Y2, X3, Y3);
    }

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setFontPosTop();
    u8g2.drawStr(CX - 5, CY - 20, "12"); 
    u8g2.drawStr(CX + 15, CY - 4, "3");  
    u8g2.drawStr(CX - 2, CY + 15, "6");  
    u8g2.drawStr(CX - 20, CY - 4, "9");  
}

// ================= SETUP & LOOP =================
void setup() {
    Serial.begin(115200);
    
    u8g2.begin();
    u8g2.setContrast(255);
    u8g2.clearBuffer(); 
    u8g2.setFont(u8g2_font_profont12_mr); 
    u8g2.drawStr(0, 20, "Statie Meteo + Ceas");  
    u8g2.drawStr(0, 35, "Conectare WiFi...");
    u8g2.sendBuffer();
    delay(2000);
   
    WiFiManager wifiManager;
    wifiManager.autoConnect("MeteoAP");

    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    Serial.println("Astept sincronizarea NTP...");
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nNTP Sincronizat!");

    updateWeather();
    updateAirQuality();
    lastWeatherUpdate = millis();
}

void loop() {
  if (millis() - lastWifiCheckTime >= wifiCheckInterval) {
    lastWifiCheckTime = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect(); 
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastWeatherUpdate >= weatherInterval) {
    updateWeather();
    updateAirQuality();
    lastWeatherUpdate = millis();
  }
 
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 100)) { 
    ora = timeinfo.tm_hour;
    minut = timeinfo.tm_min;
    secunda = timeinfo.tm_sec;
    zi = timeinfo.tm_mday;
    luna = timeinfo.tm_mon + 1;
    an = timeinfo.tm_year + 1900;
    zi2 = timeinfo.tm_wday; 
  }

  deseneazaEcran();
  delay(250); 
}  

void deseneazaEcran() {
  int rawState = (millis() / 3000);
  int displayState;

  if (indiceUV < 0.1) {
    int adjState = rawState % 16; 
    if (adjState < 7) displayState = adjState;            
    else if (adjState < 15) displayState = adjState + 1;        
    else displayState = adjState + 2;        
  } else {
    displayState = rawState % 18;         
  }
  
  u8g2.clearBuffer();
  
  // ==========================================
  // 1. ORA DIGITALA (URCATĂ LA LIMITA MAXIMĂ SUS)
  // ==========================================
  u8g2.setFont(u8g2_font_helvB24_tr);
  int textY = 14; // Y=14 este limita maximă sus fără a tăia literele
  u8g2.setCursor(0, textY);
  
  if (ora < 10) u8g2.print("0");
  u8g2.print(ora);    

  int hourEndX = u8g2.getCursorX();
  int colonWidth = u8g2.getStrWidth(":");
  
  u8g2.setCursor(hourEndX-2, textY);
  if (secunda % 2 == 0) u8g2.print(":"); else u8g2.print(" ");
  
  // Ancorare perfectă a minutelor
  u8g2.setCursor(hourEndX + colonWidth-4, textY);
  if (minut < 10) u8g2.print("0");
  u8g2.print(minut);

  // 2. CEAS ANALOGIC
  DeseneazaCeasMare(102, 25);

  // ==========================================
  // 3. ZONA DE JOS 
  // ==========================================
  u8g2.setCursor(0, 50); 

  switch(displayState) {
    // ================= ROMANA =================
    case 0: { 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi); u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna); u8g2.print("."); u8g2.print(an);
      break;
    }
    case 1: { 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(weekDays2[zi2]);
      break;
    }
    case 2: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Temperatura: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      int gradX = u8g2.getCursorX() + 3; 
      int gradY = 50; // CORECTAT: Lipit fix de baza literelor de 10px
      u8g2.drawCircle(gradX, gradY, 2);
      u8g2.setCursor(gradX + 5, 50); u8g2.print("C");
      break;
    }
    case 3: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Umiditatea: ");
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(umiditate); u8g2.print("%");
      break;
    }
    case 4: { 
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(getVremeRO(codVreme));
      break;
    }
    case 5: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Vant: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(getDirectieVantRO(directieVant));
      u8g2.print(" "); u8g2.print((int)vitezaVant); u8g2.print("km/h");
      break;
    }
    case 6: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Presiune: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print((int)(presiuneHpa * 0.750062)); u8g2.print(" mmHg");
      break;
    }
    case 7: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("UV: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(indiceUV, 1); u8g2.print(" "); u8g2.print(getCalificativUV_RO(indiceUV));
      break;
    }
    case 8: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Aer: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(calitateAer); u8g2.print(" "); u8g2.print(getCalificativAQI_RO(calitateAer));
      break;
    }

    // ================= ENGLEZA =================
    case 9: { 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi); u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna); u8g2.print("."); u8g2.print(an);
      break;
    }
    case 10: { 
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(weekDays1[zi2]);
      break;
    }
    case 11: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Temperature: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      int gradX = u8g2.getCursorX() + 3; 
      int gradY = 50; // CORECTAT: Lipit fix de baza literelor
      u8g2.drawCircle(gradX, gradY, 2);
      u8g2.setCursor(gradX + 5, 50); u8g2.print("C");
      break;
    }
    case 12: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Humidity: ");
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(umiditate); u8g2.print("%");
      break;
    }
    case 13: { 
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(getVremeEN(codVreme));
      break;
    }
    case 14: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Wind: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(getDirectieVantEN(directieVant));
      u8g2.print(" "); u8g2.print((int)vitezaVant); u8g2.print("km/h");
      break;
    }
    case 15: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Pressure: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print((int)(presiuneHpa * 0.750062)); u8g2.print(" mmHg");
      break;
    }
    case 16: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("UV: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(indiceUV, 1); u8g2.print(" "); u8g2.print(getCalificativUV_EN(indiceUV));
      break;
    }
    case 17: { 
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Air: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(calitateAer); u8g2.print(" "); u8g2.print(getCalificativAQI_EN(calitateAer));
      break;
    }
  }

  u8g2.sendBuffer();
}
