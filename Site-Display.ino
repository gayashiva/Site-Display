/* ESP32 Site Display using an EPD 4.2" Display
  ####################################################################################################################################
  Based on code by David Bird and Mirko Pavleski
*/
#include "credentials.h"  // WiFi credentials and Site API configuration
#include <ArduinoJson.h>       // https://github.com/bblanchon/ArduinoJson
#include <WiFi.h>              // Built-in
#include <WiFiClientSecure.h>  // For HTTPS connections
#include "time.h"              // Built-in
#include <SPI.h>               // Built-in
#include <Preferences.h>       // For saving site selection
#include <GxEPD2_BW.h>

Preferences prefs;
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include "lang.h"

#define SCREEN_WIDTH  400
#define SCREEN_HEIGHT 300

// Pin definitions for CrowPanel
#define PWR  7
#define BUSY 48
#define RES  47
#define DC   46
#define CS   45

// Rotary switch pins
#define ROT_UP   6
#define ROT_DOWN 4
#define BTN_FETCH 2  // Button to fetch data

// Site list for rotation
const char* siteList[] = {"Sakti", "Likir", "Baroo", "Tuna", "Ayee", "Chanigund", "Stakmo", "Igoo"};
const int numSites = 8;
int currentSiteIndex = 0;  // Will be loaded from preferences
bool dataLoaded = false;   // Track if data has been fetched for current site

// CrowPanel EPD display
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(CS, DC, RES, BUSY));

//################  VERSION  ##########################
String version = "12.5";     // Version of this program
//################ VARIABLES ###########################

String  time_str, date_str; // strings to hold time and date
int     wifi_signal, CurrentHour = 0, CurrentMin = 0, CurrentSec = 0;
long    StartTime = 0;

//################ PROGRAM VARIABLES and OBJECTS ################

#define max_readings 72

#include <common.h>

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};

long SleepDuration = 1; // Sleep time in minutes, aligned to the nearest minute boundary, so if 30 will always update at 00 or 30 past the hour
int  WakeupTime    = 6;  // Don't wakeup until after 07:00 to save battery power
int  SleepTime     = 22; // Sleep after (23+1) 00:00 to save battery power

// Forward declarations
uint8_t StartWiFi();
boolean SetupTime();
void DisplaySiteData();
void DisplayNoData();
void DisplayWiFiError();

//#########################################################################################
void fetchAndDisplay() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, reconnecting...");
    if (StartWiFi() != WL_CONNECTED) {
      Serial.println("WiFi reconnect failed");
      DisplayWiFiError();
      return;
    }
    Serial.println("WiFi reconnected");
  }

  if (SetupTime()) {
    Serial.println("Time: " + time_str);
    WiFiClient client;
    bool RxData = false;
    for (int i = 1; i <= 2 && !RxData; i++) {
      RxData = ReceiveSiteData(client, true);
    }
    if (RxData) {
      dataLoaded = true;
      DisplaySiteData();
    }
  } else {
    Serial.println("NTP failed");
  }
}

//#########################################################################################
void setup() {
  StartTime = millis();
  Serial.begin(115200);
  Serial.println("\n== Site Display ==");

  // Setup input pins
  pinMode(ROT_UP, INPUT_PULLUP);
  pinMode(ROT_DOWN, INPUT_PULLUP);
  pinMode(BTN_FETCH, INPUT_PULLUP);

  // Load saved site index
  prefs.begin("site", false);
  currentSiteIndex = prefs.getInt("index", 0);
  prefs.end();

  // Update SiteName from list
  SiteName = siteList[currentSiteIndex];
  Serial.println("Site: " + SiteName);

  // Connect to WiFi at startup
  if (StartWiFi() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    DisplayNoData();
  } else {
    DisplayWiFiError();
  }
}
//#########################################################################################
void loop() {
  // Check rotary switch for site change
  bool siteChanged = false;

  if (digitalRead(ROT_UP) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(ROT_UP) == LOW) {
      currentSiteIndex = (currentSiteIndex + 1) % numSites;
      siteChanged = true;
      while (digitalRead(ROT_UP) == LOW) delay(10);  // Wait for release
    }
  }

  if (digitalRead(ROT_DOWN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(ROT_DOWN) == LOW) {
      currentSiteIndex = (currentSiteIndex - 1 + numSites) % numSites;
      siteChanged = true;
      while (digitalRead(ROT_DOWN) == LOW) delay(10);  // Wait for release
    }
  }

  // If site changed, save and update display
  if (siteChanged) {
    SiteName = siteList[currentSiteIndex];
    dataLoaded = false;  // Reset data loaded flag
    prefs.begin("site", false);
    prefs.putInt("index", currentSiteIndex);
    prefs.end();
    Serial.println("Site: " + SiteName);
    DisplayNoData();  // Show placeholder screen
  }

  // Check fetch button
  if (digitalRead(BTN_FETCH) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(BTN_FETCH) == LOW) {
      Serial.println("Fetching data...");
      while (digitalRead(BTN_FETCH) == LOW) delay(10);  // Wait for release
      fetchAndDisplay();
    }
  }

  delay(100);  // Small delay to prevent CPU hogging
}
//#########################################################################################
void BeginSleep() {
  display.powerOff();
  long SleepTimer = SleepDuration * 60; // theoretical sleep duration
  long offset = (CurrentMin % SleepDuration) * 60 + CurrentSec; // number of seconds elapsed after last theoretical wake-up time point
  if (offset > SleepDuration/2 * 60){ // waking up too early will cause <offset> too large
    offset -= SleepDuration * 60; // then we should make it negative, so as to extend this coming sleep duration
  }
  esp_sleep_enable_timer_wakeup((SleepTimer - offset) * 1000000LL); // do compensation to cover ESP32 RTC timer source inaccuracies
#ifdef BUILTIN_LED
  pinMode(BUILTIN_LED, INPUT); // If it's On, turn it off and some boards use GPIO-5 for SPI-SS, which remains low after screen use
  digitalWrite(BUILTIN_LED, HIGH);
#endif
  Serial.println("Entering " + String(SleepTimer) + "-secs of sleep time");
  Serial.println("Awake for : " + String((millis() - StartTime) / 1000.0, 3) + "-secs");
  Serial.println("Starting deep-sleep period...");
  esp_deep_sleep_start();      // Sleep for e.g. 30 minutes
}
//#########################################################################################
void DisplaySiteData() {                // 4.2" e-paper display is 400x300 resolution
  epdPower(HIGH);
  display.init(115200, true, 50, false);
  display.setRotation(0);

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // Draw all sections
  DrawHeadingSection();
  DrawMainDataSection(0, 32);
  DrawGraphSection(0, 150);

  display.display();
  display.hibernate();
  epdPower(LOW);
}
//#########################################################################################
void DisplayNoData() {                  // Show placeholder when no data loaded
  epdPower(HIGH);
  display.init(115200, true, 50, false);
  display.setRotation(0);

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // Draw header with site name
  DrawHeadingSection();

  // Show "Press button to fetch data" message
  display.setFont(&FreeMonoBold12pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  String msg = "Press button to";
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 130);
  display.print(msg);

  msg = "fetch data";
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 160);
  display.print(msg);

  display.display();
  display.hibernate();
  epdPower(LOW);
}
//#########################################################################################
void DisplayWiFiError() {              // Show WiFi connection error
  epdPower(HIGH);
  display.init(115200, true, 50, false);
  display.setRotation(0);

  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // Draw header with site name
  DrawHeadingSection();

  // Show WiFi error message
  display.setFont(&FreeMonoBold12pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  String msg = "WiFi Error";
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 120);
  display.print(msg);

  msg = "Connect to:";
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 150);
  display.print(msg);

  display.setFont(&FreeMonoBold18pt7b);
  msg = String(ssid);
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 190);
  display.print(msg);

  display.display();
  display.hibernate();
  epdPower(LOW);
}
//#########################################################################################
void DrawHeadingSection() {
  // Time on left (HH:MM format)
  display.setFont(&FreeMonoBold9pt7b);
  String shortTime = time_str.substring(0, 5);  // Just HH:MM
  display.setCursor(5, 18);
  display.print(shortTime);

  // Site name centered (Sakti, Ladakh)
  display.setFont(&FreeMonoBold12pt7b);
  String title = SiteName + ", Ladakh";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 18);
  display.print(title);

  // Date on right (compact: 23-Nov)
  display.setFont(&FreeMonoBold9pt7b);
  // date_str format: "Sun, 23. Nov 2025" - extract day (5-6) and month (9-11)
  String day = String(date_str).substring(5, 7);   // "23"
  String mon = String(date_str).substring(9, 12); // "Nov"
  String shortDate = day + "-" + mon;              // "23-Nov"
  display.getTextBounds(shortDate, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w - 5, 18);
  display.print(shortDate);

  display.drawLine(0, 26, SCREEN_WIDTH, 26, GxEPD_BLACK);
}
//#########################################################################################
void DrawMainDataSection(int x, int y) {
  int panelW = 133;
  int panelH = 110;

  // LEFT PANEL - Site Info
  display.drawRect(x, y, panelW, panelH, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(x + 20, y + 15);
  display.print("SITE INFO");
  display.drawLine(x, y + 20, x + panelW, y + 20, GxEPD_BLACK);

  display.setFont(&FreeMonoBold12pt7b);
  display.setCursor(x + 10, y + 50);
  display.print(SiteInfo.SiteName);

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(x + 10, y + 80);
  display.print("Readings:" + String(NumReadings));

  // CENTER PANEL - Air Temperature
  int cx = x + panelW + 1;
  display.drawRect(cx, y, panelW, panelH, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(cx + 25, y + 15);
  display.print("AIR TEMP");
  display.drawLine(cx, y + 20, cx + panelW, y + 20, GxEPD_BLACK);

  display.setFont(&FreeMonoBold18pt7b);
  display.setCursor(cx + 10, y + 55);
  display.print(String(CurrentReading.Temperature, 1) + "C");

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(cx + 10, y + 85);
  display.print("Water:" + String(CurrentReading.WaterTemp, 1) + "C");

  // RIGHT PANEL - Pressure
  int rx = cx + panelW + 1;
  display.drawRect(rx, y, panelW, panelH, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(rx + 25, y + 15);
  display.print("PRESSURE");
  display.drawLine(rx, y + 20, rx + panelW, y + 20, GxEPD_BLACK);

  display.setFont(&FreeMonoBold18pt7b);
  display.setCursor(rx + 10, y + 55);
  display.print(String(CurrentReading.Pressure, 2));

  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(rx + 10, y + 85);
  display.print("Count:" + String(CurrentReading.Counter));
}
//#########################################################################################
void DrawGraphSection(int x, int y) {
  // Section title
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(120, y + 12);
  display.print("HISTORY (6 hrs)");
  display.drawLine(0, y + 18, SCREEN_WIDTH, y + 18, GxEPD_BLACK);

  // Prepare data arrays (reverse order - oldest first for proper graph display)
  float water_temp_readings[max_readings];
  for (int r = 0; r < NumReadings; r++) {
    temperature_readings[r] = SiteReadings[NumReadings - 1 - r].Temperature;
    water_temp_readings[r] = SiteReadings[NumReadings - 1 - r].WaterTemp;
    pressure_readings[r] = SiteReadings[NumReadings - 1 - r].Pressure;
  }

  // Draw 3 graphs side by side
  int graph_y = y + 35;
  int graph_h = 110;
  int graph_w = 120;

  DrawGraph(x + 10, graph_y, graph_w, graph_h, -10, 10, "Air", temperature_readings, NumReadings, autoscale_on, barchart_off);
  DrawGraph(x + 140, graph_y, graph_w, graph_h, 0, 10, "Water", water_temp_readings, NumReadings, autoscale_on, barchart_off);
  DrawGraph(x + 270, graph_y, graph_w, graph_h, 0, 2, "Pressure", pressure_readings, NumReadings, autoscale_on, barchart_off);
}
//#########################################################################################
uint8_t StartWiFi() {
  Serial.print("\r\nConnecting to: "); Serial.println(String(ssid));
  IPAddress dns(8, 8, 8, 8); // Google DNS
  WiFi.disconnect();
  WiFi.mode(WIFI_STA); // switch off AP
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  uint8_t connectionStatus;
  bool AttemptConnection = true;
  while (AttemptConnection) {
    connectionStatus = WiFi.status();
    if (millis() > start + 15000) { // Wait 15-secs maximum
      AttemptConnection = false;
    }
    if (connectionStatus == WL_CONNECTED || connectionStatus == WL_CONNECT_FAILED) {
      AttemptConnection = false;
    }
    delay(50);
  }
  if (connectionStatus == WL_CONNECTED) {
    wifi_signal = WiFi.RSSI(); // Get Wifi Signal strength now, because the WiFi will be turned off to save power!
    Serial.println("WiFi connected at: " + WiFi.localIP().toString());
  }
  else Serial.println("WiFi connection *** FAILED ***");
  return connectionStatus;
}
//#########################################################################################
void StopWiFi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}
//#########################################################################################
boolean SetupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.cloudflare.com");
  setenv("TZ", Timezone, 1);
  tzset();
  delay(1000);  // Give NTP time to sync after WiFi connection
  bool TimeStatus = UpdateLocalTime();
  return TimeStatus;
}
//#########################################################################################
boolean UpdateLocalTime() {
  struct tm timeinfo;
  char   time_output[30], day_output[30], update_time[30];
  while (!getLocalTime(&timeinfo, 10000)) { // Wait for 5-sec for time to synchronise
    Serial.println("Failed to obtain time");
    return false;
  }
  CurrentHour = timeinfo.tm_hour;
  CurrentMin  = timeinfo.tm_min;
  CurrentSec  = timeinfo.tm_sec;
  //See http://www.cplusplus.com/reference/ctime/strftime/
  //Serial.println(&timeinfo, "%a %b %d %Y   %H:%M:%S"); // Displays: Saturday, June 24 2017 14:05:49
    sprintf(day_output, "%s, %02u. %s %04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900); // day_output >> So., 23. Juni 2019 <<
    strftime(update_time, sizeof(update_time), "%H:%M:%S", &timeinfo);  // Creates: '@ 14:05:49'   and change from 30 to 8 <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    sprintf(time_output, "%s", update_time);
  date_str = day_output;
  time_str = time_output;
  return true;
}
//#########################################################################################
void DrawBattery(int x, int y) {
  uint8_t percentage = 100;
  float voltage = analogRead(35) / 4096.0 * 7.46;
  if (voltage > 1 ) { // Only display if there is a valid reading
    Serial.println("Voltage = " + String(voltage));
    percentage = 2836.9625 * pow(voltage, 4) - 43987.4889 * pow(voltage, 3) + 255233.8134 * pow(voltage, 2) - 656689.7123 * voltage + 632041.7303;
    if (voltage >= 4.20) percentage = 100;
    if (voltage <= 3.50) percentage = 0;
    display.drawRect(x + 15, y - 12, 19, 10, GxEPD_BLACK);
    display.fillRect(x + 34, y - 10, 2, 5, GxEPD_BLACK);
    display.fillRect(x + 17, y - 10, 15 * percentage / 100.0, 6, GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(x + 40, y);
    display.print(String(percentage) + "%");
  }
}
//#########################################################################################
/* (C) D L BIRD
    This function will draw a graph on a ePaper/TFT/LCD display using data from an array containing data to be graphed.
    The variable 'max_readings' determines the maximum number of data elements for each array. Call it with the following parametric data:
    x_pos - the x axis top-left position of the graph
    y_pos - the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
    width - the width of the graph in pixels
    height - height of the graph in pixels
    Y1_Max - sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
    data_array1 is parsed by value, externally they can be called anything else, e.g. within the routine it is called data_array1, but externally could be temperature_readings
    auto_scale - a logical value (TRUE or FALSE) that switches the Y-axis autoscale On or Off
    barchart_on - a logical value (TRUE or FALSE) that switches the drawing mode between bar and line graphs
    barchart_colour - a sets the title and graph plotting colour
    If called with Y!_Max value of 500 and the data never goes above 500, then autoscale will retain a 0-500 Y scale, if on, the scale increases/decreases to match the data.
    auto_scale_margin, e.g. if set to 1000 then autoscale increments the scale by 1000 steps.
*/
void DrawGraph(int x_pos, int y_pos, int gwidth, int gheight, float Y1Min, float Y1Max, String title, float DataArray[], int readings, boolean auto_scale, boolean barchart_mode) {
#define auto_scale_margin 0 // Sets the autoscale increment, so axis steps up in units of e.g. 3
#define y_minor_axis 5      // 5 y-axis division markers
  float maxYscale = -10000;
  float minYscale =  10000;
  int last_x, last_y;
  float x2, y2;
  if (auto_scale == true) {
    for (int i = 1; i < readings; i++ ) {
      if (DataArray[i] >= maxYscale) maxYscale = DataArray[i];
      if (DataArray[i] <= minYscale) minYscale = DataArray[i];
    }
    maxYscale = round(maxYscale + auto_scale_margin);
    Y1Max = round(maxYscale + 0.5);
    if (minYscale != 0) minYscale = round(minYscale - auto_scale_margin);
    Y1Min = round(minYscale);
  }
  // Draw the graph frame and title
  display.drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, GxEPD_BLACK);
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(x_pos + 30, y_pos - 5);
  display.print(title);

  // Initialize starting point
  last_x = x_pos;
  last_y = y_pos + (Y1Max - constrain(DataArray[0], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;

  // Draw the data
  for (int gx = 0; gx < readings; gx++) {
    y2 = y_pos + (Y1Max - constrain(DataArray[gx], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight + 1;
    if (barchart_mode) {
      x2 = x_pos + gx * (gwidth / readings) + 2;
      display.fillRect(x2, y2, (gwidth / readings) - 2, y_pos + gheight - y2 + 2, GxEPD_BLACK);
    }
    else {
      x2 = x_pos + gx * gwidth / (readings - 1) + 1;
      display.drawLine(last_x, last_y, x2, y2, GxEPD_BLACK);
    }
    last_x = x2;
    last_y = y2;
  }

  // Draw dashed grid lines
#define number_of_dashes 15
  for (int spacing = 0; spacing <= y_minor_axis; spacing++) {
    for (int j = 0; j < number_of_dashes; j++) {
      if (spacing < y_minor_axis)
        display.drawFastHLine((x_pos + 3 + j * gwidth / number_of_dashes), y_pos + (gheight * spacing / y_minor_axis), gwidth / (2 * number_of_dashes), GxEPD_BLACK);
    }
  }

  // Display min and max values
  display.setFont(&FreeSans9pt7b);
  display.setCursor(x_pos + 2, y_pos + 12);
  display.print(String(Y1Max, 1));
  display.setCursor(x_pos + 2, y_pos + gheight - 2);
  display.print(String(Y1Min, 1));
}
//#########################################################################################
void epdPower(int state) {
  pinMode(PWR, OUTPUT);
  digitalWrite(PWR, state);
}
//#########################################################################################
void InitialiseDisplay() {
  epdPower(HIGH);
  display.init(115200, true, 50, false);
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
}
