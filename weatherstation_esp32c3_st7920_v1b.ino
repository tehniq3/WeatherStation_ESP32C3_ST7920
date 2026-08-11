/*
 * ==============================================================================
 * PROIECT: Ceas NTP cu Stație Meteo Completă pe ESP32-C3 Mini
 * AUTOR: Asistent AI
 * 
 * DESCRIERE:
 * Acest program transformă un ESP32-C3 Mini și un ecran LCD ST7920 (128x64) 
 * într-un ceas de perete inteligent, care își actualizează ora prin internet 
 * (NTP) și afișează date meteo complete preluațe de la Open-Meteo.
 * 
 * FUNCTII:
 * - Sincronizare oră exactă via NTP (cu suport automat pentru ora de vară/iarnă).
 * - Afișare alternantă (bilingvă) în Română și Engleză.
 * - Temperatură curentă (grade Celsius).
 * - Starea vremii (Senin, Noros, Ploaie, etc.).
 * - Umiditate relativă (%).
 * - Vânt (direcție cardinală: N, NE, SV, V + viteză în km/h).
 * - Presiune atmosferică la nivelul mării (convertită din hPa în mmHg).
 * - Indice UV (afișat DOAR ziua, când valoarea > 0.1, însoțit de calificativ).
 * - Calitatea aerului (European AQI, însoțit de calificativ).
 * 
 * HARDWARE NECESAR:
 * - Placă de dezvoltare: ESP32-C3 Mini (sau orice ESP32 compatibil).
 * - Ecran: LCD ST7920 128x64 (pinii de mai jos sunt pentru conexiune software SPI).
 * 
 * CONEXIUNI (Software SPI):
 * - Pinul 2 (SCLK_PIN) -> CLK la ecran
 * - Pinul 3 (MOSI_PIN) -> SID/MOSI la ecran
 * - Pinul 4 (CS_PIN)   -> CS la ecran
 * - VCC -> 3.3V, GND -> GND
 * - Contrast ecran: se ajustează printr-o rezistență variabilă (potentiometru) 
 *   conectată la pinul V0 al ecranului, codul setând contrastul la maxim.
 * 
 * LIBRARII ARDUINO NECESARE (se instalează din Library Manager):
 * - WiFi (preinstalată cu ESP32)
 * - WebServer (preinstalată cu ESP32)
 * - WiFiManager (de tzapu / wifimanager)
 * - NTPClient (de Fabrice Weinberg)
 * - U8g2 (de oliver)
 * - ArduinoJson (de Benoit Blanchon)
 * - HTTPClient (preinstalată cu ESP32)
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
const unsigned long updateDelay = 900000;         // Actualizare NTP la 15 minute
const unsigned long retryDelay  = 5000;           // Retry la 5 secunde dacă pică NTP-ul
const unsigned long wifiCheckInterval = 30000;    // Verificare WiFi la 30 secunde
const unsigned long weatherInterval = 600000;     // Actualizare Meteo la 10 minute

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

// Variabile meteo
float temperatura = 0.0;
int umiditate = 0;
int codVreme = 0;
float vitezaVant = 0.0;
int directieVant = 0;
float presiuneHpa = 0.0;
float indiceUV = 0.0;
int calitateAer = 0;

// Coordonate GPS (trebuie modificate cu locația ta exactă pentru date precise)
const char* lat = "44.3167";  
const char* lon = "23.8000";  

// ================= FUNCȚII DE TRADUCERE VREME =================
String getVremeRO(int code) {
  switch(code) {
    case 0: return "Senin";
    case 1: return "Predom. senin";
    case 2: return "Partial noros";
    case 3: return "Noros";
    case 45: case 48: return "Ceata";
    case 51: case 53: case 55: return "Burnita";
    case 56: case 57: return "Burnita inghet.";
    case 61: case 63: case 65: return "Ploaie";
    case 66: case 67: return "Ploaie inghet.";
    case 71: case 73: case 75: case 77: return "Ninsoare";
    case 80: case 81: case 82: return "Averse";
    case 85: case 86: return "Ninsoare trec.";
    case 95: return "Furtuna";
    case 96: case 99: return "Furtuna+Grind.";
    default: return "N/A";
  }
}

String getVremeEN(int code) {
  switch(code) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Frz. drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Frz. rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "T-Storm/Hail";
    default: return "N/A";
  }
}

// ================= FUNCȚII VÂNT =================
String getDirectieVantRO(int grade) {
  // În limba română folosim V pentru Vest, nu W
  if (grade >= 337.5 || grade < 22.5) return "N";
  if (grade >= 22.5 && grade < 67.5) return "NE";
  if (grade >= 67.5 && grade < 112.5) return "E";
  if (grade >= 112.5 && grade < 157.5) return "SE";
  if (grade >= 157.5 && grade < 202.5) return "S";
  if (grade >= 202.5 && grade < 247.5) return "SV";
  if (grade >= 247.5 && grade < 292.5) return "V";
  if (grade >= 292.5 && grade < 337.5) return "NV";
  return "N";
}

String getDirectieVantEN(int grade) {
  if (grade >= 337.5 || grade < 22.5) return "N";
  if (grade >= 22.5 && grade < 67.5) return "NE";
  if (grade >= 67.5 && grade < 112.5) return "E";
  if (grade >= 112.5 && grade < 157.5) return "SE";
  if (grade >= 157.5 && grade < 202.5) return "S";
  if (grade >= 202.5 && grade < 247.5) return "SW";
  if (grade >= 247.5 && grade < 292.5) return "W";
  if (grade >= 292.5 && grade < 337.5) return "NW";
  return "N";
}

// ================= FUNCȚII UV & CALITATE AER =================
String getCalificativUV_RO(float uv) {
  if (uv <= 2) return "Scăzut";
  if (uv <= 5) return "Moderat";
  if (uv <= 7) return "Ridicat";
  if (uv <= 10) return "Foarte ridicat";
  return "Extrem";
}

String getCalificativUV_EN(float uv) {
  if (uv <= 2) return "Low";
  if (uv <= 5) return "Moderate";
  if (uv <= 7) return "High";
  if (uv <= 10) return "Very High";
  return "Extreme";
}

String getCalificativAQI_RO(int aqi) {
  if (aqi <= 20) return "Bun";
  if (aqi <= 40) return "Acceptabil";
  if (aqi <= 60) return "Moderat";
  if (aqi <= 80) return "Slab";
  if (aqi <= 100) return "Foarte slab";
  return "Extrem de slab";
}

String getCalificativAQI_EN(int aqi) {
  if (aqi <= 20) return "Good";
  if (aqi <= 40) return "Fair";
  if (aqi <= 60) return "Moderate";
  if (aqi <= 80) return "Poor";
  if (aqi <= 100) return "Very Poor";
  return "Extremely Poor";
}
// =================================================================

void getDate() {
  time_t rawtime = timeClient.getEpochTime();
  time_t localTime = rawtime + currentOffset; 
  struct tm * ti;
  ti = gmtime(&localTime); 
  
  zi = ti->tm_mday;
  luna = ti->tm_mon + 1;
  an = ti->tm_year + 1900;
  zi2 = ti->tm_wday; 
}

void updateWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Cerere API completă: temperatură, umiditate, cod vreme, vânt, presiune mări, UV
    String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat) + 
                 "&longitude=" + String(lon) + 
                 "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl,uv_index";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(2048);
      deserializeJson(doc, payload);
      
      temperatura = doc["current"]["temperature_2m"].as<float>();
      umiditate = doc["current"]["relative_humidity_2m"].as<int>();
      codVreme = doc["current"]["weather_code"].as<int>();
      vitezaVant = doc["current"]["wind_speed_10m"].as<float>();
      directieVant = doc["current"]["wind_direction_10m"].as<int>();
      presiuneHpa = doc["current"]["pressure_msl"].as<float>();
      indiceUV = doc["current"]["uv_index"].as<float>();
    }
    http.end();
  }
}

void updateAirQuality() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    // Cerere API separată pentru calitatea aerului (Indicele European AQI)
    String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + String(lat) + 
                 "&longitude=" + String(lon) + "&current=european_aqi";
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(512);
      deserializeJson(doc, payload);
      calitateAer = doc["current"]["european_aqi"].as<int>();
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

    // Prima interogare meteo și aer la pornire
    updateWeather();
    updateAirQuality();
    lastWeatherUpdate = millis();
}

void loop() {
  // Reconectare WiFi automată
  if (millis() - lastWifiCheckTime >= wifiCheckInterval) {
    lastWifiCheckTime = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect(); 
    }
  }

  // Actualizare oră NTP
  if (WiFi.status() == WL_CONNECTED && millis() - lastUpdatedTime >= updateDelay) {
    updated = timeClient.update();
    if (updated) {
      getDate();
      lastUpdatedTime = millis();
    } else {
      lastUpdatedTime = millis() - updateDelay + retryDelay;
    }
  }

  // Actualizare date Meteo și Aer
  if (WiFi.status() == WL_CONNECTED && millis() - lastWeatherUpdate >= weatherInterval) {
    updateWeather();
    updateAirQuality();
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
  
  int rawState = (millis() / 3000);
  int displayState;

  // LOGICĂ DE SĂRIRE ECRAN UV:
  // Dacă UV-ul este sub 0.1 (noaptea), scădem 2 din totalul de ecrane și recalculăm "displayState"
  if (indiceUV < 0.1) {
    int adjState = rawState % 16; // Trecem de la 18 ecrane la 16 valide
    if (adjState < 7) {
      displayState = adjState;            // Ecranele 0-6 rămân la fel
    } else if (adjState < 15) {
      displayState = adjState + 1;        // Ecranele 7-14 devin 8-15 (sare peste vechiul 7)
    } else {
      displayState = adjState + 2;        // Ecranul 15 devine 17 (sare peste vechiul 16)
    }
  } else {
    displayState = rawState % 18;         // Ziua rămânem cu toate cele 18 ecrane
  }
  
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

    // ================= BLOCUL ÎN ROMÂNĂ (0 - 8) =================
    if (displayState == 0) { // DATA
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi); u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna); u8g2.print("."); u8g2.print(an);
      
    } else if (displayState == 1) { // ZI SĂPTĂMÂNĂ
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(weekDays2[zi2]);
        
    } else if (displayState == 2) { // TEMPERATURĂ
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Temperatura: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      int gradX = u8g2.getCursorX() + 3; int gradY = 51; 
      u8g2.drawCircle(gradX, gradY, 2);
      u8g2.setCursor(gradX + 5, 61); u8g2.print("C");
      
    } else if (displayState == 3) { // UMIDITATE
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Umiditate: ");
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(umiditate); u8g2.print("%");
      
    } else if (displayState == 4) { // STARE VREME
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Vreme: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(getVremeRO(codVreme));

    } else if (displayState == 5) { // VÂNT
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Vant: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(getDirectieVantRO(directieVant));
      u8g2.print(" ");
      u8g2.print((int)vitezaVant);
      u8g2.print("km/h");

    } else if (displayState == 6) { // PRESIUNE
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Presiune: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      // Conversie matematică hPa în mmHg
      u8g2.print((int)(presiuneHpa * 0.750062)); 
      u8g2.print(" mmHg");

    } else if (displayState == 7) { // INDICE UV (Afisat doar daca UV > 0.1)
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("UV: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(indiceUV, 1);
      u8g2.print(" ");
      u8g2.print(getCalificativUV_RO(indiceUV));

    } else if (displayState == 8) { // CALITATE AER
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Aer: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(calitateAer);
      u8g2.print(" ");
      u8g2.print(getCalificativAQI_RO(calitateAer));
    } 

    // ================= BLOCUL ÎN ENGLEZĂ (9 - 17) =================
    else if (displayState == 9) { // DATE
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (zi < 10) u8g2.print("0");
      u8g2.print(zi); u8g2.print(".");
      if (luna < 10) u8g2.print("0");
      u8g2.print(luna); u8g2.print("."); u8g2.print(an);
      
    } else if (displayState == 10) { // WEEKDAY
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(weekDays1[zi2]);
        
    } else if (displayState == 11) { // TEMPERATURE
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Temperature: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      if (temperatura >= 0) u8g2.print("+");
      u8g2.print(temperatura, 1); 
      int gradX = u8g2.getCursorX() + 3; int gradY = 51; 
      u8g2.drawCircle(gradX, gradY, 2);
      u8g2.setCursor(gradX + 5, 61); u8g2.print("C");
      
    } else if (displayState == 12) { // HUMIDITY
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Humidity: ");
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(umiditate); u8g2.print("%");
      
    } else if (displayState == 13) { // WEATHER
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Weather: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); u8g2.print(getVremeEN(codVreme));

    } else if (displayState == 14) { // WIND
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Wind: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(getDirectieVantEN(directieVant));
      u8g2.print(" ");
      u8g2.print((int)vitezaVant);
      u8g2.print("km/h");

    } else if (displayState == 15) { // PRESSURE
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Pressure: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print((int)(presiuneHpa * 0.750062)); 
      u8g2.print(" mmHg");

    } else if (displayState == 16) { // UV INDEX (Afisat doar daca UV > 0.1)
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("UV: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(indiceUV, 1);
      u8g2.print(" ");
      u8g2.print(getCalificativUV_EN(indiceUV));

    } else if (displayState == 17) { // AIR QUALITY
      u8g2.setFont(u8g2_font_helvB08_tr); u8g2.print("Air: "); 
      u8g2.setFont(u8g2_font_helvB10_tr); 
      u8g2.print(calitateAer);
      u8g2.print(" ");
      u8g2.print(getCalificativAQI_EN(calitateAer));
    }
    
  } while ( u8g2.nextPage() );
}
