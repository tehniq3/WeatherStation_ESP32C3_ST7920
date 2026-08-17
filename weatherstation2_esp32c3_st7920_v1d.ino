/*
 * ==============================================================================
 * PROIECT: Stație Meteo Bilingvă (RO/EN) cu Ceas Analog și Digital
 * ==============================================================================
 * DESCRIERE:
 * Afișaj complet pe ecran LCD ST7920 128x64 controlat de ESP32.
 * Preia date meteo și calitatea aerului prin WiFi de la API-ul Open-Meteo.
 * 
 * HARDWARE ȘI CONEXIUNI:
 * - Microcontroler: ESP32 (ex: ESP32-C3 Mini)
 * - Ecran: ST7920 128x64 (Comunicare Software SPI)
 * - Pin SCLK: GPIO 2
 * - Pin MOSI: GPIO 3
 * - Pin CS:   GPIO 4
 * 
 * DISPUNEREA PE ECRAN (LAYOUT):
 * - Stânga-Sus:  Ceas digital mare (Font helvB24), cu ":" care clipește.
 * - Dreapta-Sus: Ceas analogic (raza 28px), cu cifre mici poziționate elegant în interior.
 * - Stânga-Jos:  Informații meteo pe 2 rânduri (Font helvB10):
 *               > Rândul 1: Maxim 8 caractere (Etichete / Primele cuvinte)
 *               > Rândul 2: Maxim 9 caractere (Valori / Descrieri)
 * 
 * FUNCȚIONALITĂȚI:
 * - Conectare WiFi securizată prin WiFiManager (Captive Portal "MeteoAP").
 * - Sincronizare oră prin NTP (Fus orar România, trecere automată la oră de vară/iarnă).
 * - Afișare ciclică (la fiecare 3 secunde) a datelor în limba Română și Engleză.
 * - Format dată: ZZ.L.AAAA (ex: 17.8.2026).
 * - Format temperatură: +XX.X°C (inclusiv simbolul de grad desenat manual).
 * - Descrierea vremii (când are 2 cuvinte) este împărțită inteligent pe cele 2 rânduri.
 * - Vântul afișează direcția pe rândul 1 și viteza pe rândul 2.
 * - UV și Calitatea Aerului afișează valoarea pe rândul 1 și calificativul pe rândul 2.
 * 
 * SURSE DE DATE (API):
 * - Vreme: Open-Meteo (Temperatură, Umiditate, Cod vreme, Vânt, Presiune, UV).
 * - Aer:   Open-Meteo Air Quality (European AQI).
 * - Locație implicită: Craiova (44.3167°N, 23.8000°E).
 * 
 * TIMPING DE ACTUALIZARE:
 * - Date meteo: La fiecare 10 minute (600000 ms).
 * - Verificare WiFi: La fiecare 30 de secunde.
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
    TegnViser(ora % 12, 12.0, 14, CX, CY);   
    TegnViser(minut, 60.0, 22, CX, CY);     
    TegnViser(secunda, 60.0, 25, CX, CY);   
  
    for(int i = 0; i < 12; i++) { 
      Vinkel = i / 12.0 * 2 * 3.1415;
      int r1 = 28; int r2 = 26; 
      if (i % 3 == 0) { r1 = 28; r2 = 25; } 
      
      X2 = CX + r1 * cos(Vinkel);
      Y2 = CY + r1 * sin(Vinkel);
      X3 = CX + r2 * cos(Vinkel);
      Y3 = CY + r2 * sin(Vinkel);
      u8g2.drawLine(X2, Y2, X3, Y3);
    }

    u8g2.setFont(u8g2_font_5x8_tr); 
    u8g2.setFontPosTop();
    u8g2.drawStr(CX - 4, CY - 22, "12"); 
    u8g2.drawStr(CX + 22, CY - 4, "3");  
    u8g2.drawStr(CX - 2, CY + 14, "6");  
    u8g2.drawStr(CX - 22, CY - 4, "9");  
}

// ================= FUNCȚII AJUTĂTOARE =================
String formatLine1(const char* text) {
  String s = String(text);
  while(s.length() < 8) s += " ";
  return s.substring(0, 8);
}

String getDirectieVantShort(int grade) {
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
  
  // 1. ORA DIGITALA (MĂRIT și RIDICAT SUS)
   u8g2.setFont(u8g2_font_helvB24_tr);
  int textY = 0; // Y=0 este limita maximă sus fără a tăia literele
  u8g2.setCursor(0, textY);
  
  if (ora < 10) u8g2.print("0");
  u8g2.print(ora);    

  int hourEndX = u8g2.getCursorX();
  int colonWidth = u8g2.getStrWidth(":");
  
  u8g2.setCursor(hourEndX-4, textY);
  if (secunda % 2 == 0) u8g2.print(":"); else u8g2.print(" ");
  
  // Ancorare perfectă a minutelor
  u8g2.setCursor(hourEndX + colonWidth-8, textY);
  if (minut < 10) u8g2.print("0");
  u8g2.print(minut);

  // 2. CEAS ANALOGIC
  DeseneazaCeasMare(100, 32); 

  // ==========================================
  // 3. ZONA DE JOS 
  // ==========================================
  u8g2.setFont(u8g2_font_helvB10_tr); 
  
  String line1 = ""; 
  String line2 = ""; 
  char buf[12];
  bool drawTempGrad = false; // Variabilă pentru a ști când să desenăm gradul

  switch(displayState) {
    // ================= ROMANA =================
    case 0: { 
      line1 = formatLine1("Data:"); 
      snprintf(buf, sizeof(buf), "%02d.%02d.%04d ", zi, luna, an); 
      line2 = String(buf); 
      break; 
    }
    case 1: { 
      line1 = formatLine1("Ziua:"); 
      line2 = weekDays2[zi2]; 
      while(line2.length() < 9) line2 += " "; 
      line2 = line2.substring(0, 9); 
      break; 
    }
    case 2: { 
      line1 = formatLine1("Temper."); 
      // Lăsăm spațiu la final pentru a desena manual simbolul °C
      snprintf(buf, sizeof(buf), "%+5.1f", temperatura); 
      line2 = String(buf); 
      drawTempGrad = true; // Activăm desenarea
      break; 
    }
    case 3: { 
      line1 = formatLine1("Umidit."); 
      snprintf(buf, sizeof(buf), "%3d%%     ", umiditate); 
      line2 = String(buf); 
      break; 
    }
    case 4: { 
      switch(codVreme) {
        case 0: line1 = "        "; line2 = "Senin   "; break;
        case 1: line1 = "Predom. "; line2 = "senin   "; break;
        case 2: line1 = "Partial "; line2 = "noros   "; break;
        case 3: line1 = "        "; line2 = "Noros   "; break;
        case 45: case 48: line1 = "        "; line2 = "Ceata   "; break;
        case 51: case 53: case 55: line1 = "        "; line2 = "Burnita "; break;
        case 56: case 57: line1 = "Burnita "; line2 = "inghet. "; break;
        case 61: case 63: case 65: line1 = "        "; line2 = "Ploaie  "; break;
        case 66: case 67: line1 = "Ploaie  "; line2 = "inghet. "; break;
        case 71: case 73: case 75: case 77: line1 = "        "; line2 = "Ninsoare"; break;
        case 80: case 81: case 82: line1 = "        "; line2 = "Averse  "; break;
        case 85: case 86: line1 = "Ninsoare"; line2 = "trec.   "; break;
        case 95: line1 = "        "; line2 = "Furtuna "; break;
        case 96: case 99: line1 = "Furtuna "; line2 = "Grind.  "; break;
        default: line1 = "        "; line2 = "N/A     "; break;
      }
      break; 
    }
    case 5: { 
      line1 = "Vant: " + getDirectieVantShort(directieVant);
      while(line1.length() < 8) line1 += " ";
      line1 = line1.substring(0, 8);
      snprintf(buf, sizeof(buf), "%3dkm/h ", (int)vitezaVant); 
      line2 = String(buf); 
      break; 
    }
    case 6: { 
      line1 = formatLine1("Presiune"); 
      snprintf(buf, sizeof(buf), "%dmmHg   ", (int)(presiuneHpa * 0.750062)); 
      line2 = String(buf); 
      break; 
    }
    case 7: { 
      snprintf(buf, sizeof(buf), "UV: %4.1f", indiceUV);
      line1 = String(buf); while(line1.length() < 8) line1 += " "; line1 = line1.substring(0, 8);
      if (indiceUV <= 2) line2 = "Scazut  ";
      else if (indiceUV <= 5) line2 = "Moderat ";
      else if (indiceUV <= 7) line2 = "Ridicat ";
      else if (indiceUV <= 10) line2 = "F.ridicat";
      else line2 = "Extrem  ";
      break; 
    }
    case 8: { 
      snprintf(buf, sizeof(buf), "Aer: %3d", calitateAer);
      line1 = String(buf); while(line1.length() < 8) line1 += " "; line1 = line1.substring(0, 8);
      if (calitateAer <= 20) line2 = "Bun     ";
      else if (calitateAer <= 40) line2 = "Bunicel "; 
      else if (calitateAer <= 60) line2 = "Moderat ";
      else if (calitateAer <= 80) line2 = "Slab    ";
      else if (calitateAer <= 100) line2 = "F.slab  ";
      else line2 = "Periculos";
      break; 
    }

    // ================= ENGLEZA =================
    case 9: { 
      line1 = formatLine1("Date:"); 
      snprintf(buf, sizeof(buf), "%02d.%02d.%04d ", zi, luna, an); 
      line2 = String(buf); 
      break; 
    }
    case 10: { 
      line1 = formatLine1("Day:"); 
      line2 = weekDays1[zi2]; 
      while(line2.length() < 9) line2 += " "; 
      line2 = line2.substring(0, 9); 
      break; 
    }
    case 11: { 
      line1 = formatLine1("Temper.:"); 
      // Lăsăm spațiu la final pentru a desena manual simbolul °C
      snprintf(buf, sizeof(buf), "%+5.1f", temperatura); 
      line2 = String(buf); 
      drawTempGrad = true; // Activăm desenarea
      break; 
    }
    case 12: { 
      line1 = formatLine1("Humidity"); 
      snprintf(buf, sizeof(buf), "%3d%%     ", umiditate); 
      line2 = String(buf); 
      break; 
    }
    case 13: { 
      switch(codVreme) {
        case 0: line1 = "Clear   "; line2 = "sky     "; break;
        case 1: line1 = "Mainly  "; line2 = "clear   "; break;
        case 2: line1 = "Partly  "; line2 = "cloudy  "; break;
        case 3: line1 = "        "; line2 = "Overcast"; break;
        case 45: case 48: line1 = "        "; line2 = "Fog     "; break;
        case 51: case 53: case 55: line1 = "        "; line2 = "Drizzle "; break;
        case 56: case 57: line1 = "Frz.    "; line2 = "drizzle "; break;
        case 61: case 63: case 65: line1 = "        "; line2 = "Rain    "; break;
        case 66: case 67: line1 = "Frz.    "; line2 = "rain    "; break;
        case 71: case 73: case 75: case 77: line1 = "        "; line2 = "Snow    "; break;
        case 80: case 81: case 82: line1 = "Rain    "; line2 = "showers "; break;
        case 85: case 86: line1 = "Snow    "; line2 = "showers "; break;
        case 95: line1 = "Thunder "; line2 = "storm   "; break;
        case 96: case 99: line1 = "T-Storm/"; line2 = "Hail    "; break;
        default: line1 = "        "; line2 = "N/A     "; break;
      }
      break; 
    }
    case 14: { 
      line1 = "Wind: " + getDirectieVantShort(directieVant);
      while(line1.length() < 8) line1 += " ";
      line1 = line1.substring(0, 8);
      snprintf(buf, sizeof(buf), "%3dkm/h ", (int)vitezaVant); 
      line2 = String(buf); 
      break; 
    }
    case 15: { 
      line1 = formatLine1("Pressure"); 
      snprintf(buf, sizeof(buf), "%dmmHg   ", (int)(presiuneHpa * 0.750062)); 
      line2 = String(buf); 
      break; 
    }
    case 16: { 
      snprintf(buf, sizeof(buf), "UV: %4.1f", indiceUV);
      line1 = String(buf); while(line1.length() < 8) line1 += " "; line1 = line1.substring(0, 8);
      if (indiceUV <= 2) line2 = "Low     ";
      else if (indiceUV <= 5) line2 = "Moderate";
      else if (indiceUV <= 7) line2 = "High    ";
      else if (indiceUV <= 10) line2 = "Very High";
      else line2 = "Extreme ";
      break; 
    }
    case 17: { 
      snprintf(buf, sizeof(buf), "Air: %3d", calitateAer);
      line1 = String(buf); while(line1.length() < 8) line1 += " "; line1 = line1.substring(0, 8);
      if (calitateAer <= 20) line2 = "Good    ";
      else if (calitateAer <= 40) line2 = "Fair    ";
      else if (calitateAer <= 60) line2 = "Moderate";
      else if (calitateAer <= 80) line2 = "Poor    ";
      else if (calitateAer <= 100) line2 = "Very Poor";
      else line2 = "Ext.Poor";
      break; 
    }
  }

  // Desenare efectivă pe ecran
  u8g2.setCursor(0, 34); // Primul rând
  u8g2.print(line1);
  
  u8g2.setCursor(0, 48); // Al doilea rând
  u8g2.print(line2);

  // DESENEAZĂ SIMBOLUL DE GRADE MANUAL (Cercul mic și litera C)
  if (drawTempGrad) {
    int gradX = u8g2.getCursorX() + 4; 
    int gradY = 50; // Puțin mai sus, să arate ca un superscript față de rândul de jos
    u8g2.drawCircle(gradX, gradY, 2);
    u8g2.setCursor(gradX + 4, 48); // Revenim la linia de bază pentru "C"
    u8g2.print("C");
  }

  u8g2.sendBuffer();
}
