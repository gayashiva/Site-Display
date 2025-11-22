/* ESP32 Weather Display using an EPD 4.2" Display, obtains data from Open Weather Map, decodes it and then displays it.
  ####################################################################################################################################
  This software, the ideas and concepts is Copyright (c) David Bird 2018. All rights to this software are reserved.

  Any redistribution or reproduction of any part or all of the contents in any form is prohibited other than the following:
  1. You may print or download to a local hard disk extracts for your personal and non-commercial use only.
  2. You may copy the content to individual third parties for their personal use, but only if you acknowledge the author David Bird as the source of the material.
  3. You may not, except with my express written permission, distribute or commercially exploit the content.
  4. You may not transmit it or store it in any other website or other form of electronic retrieval system for commercial purposes.

  The above copyright ('as annotated') notice and this permission notice shall be included in all copies or substantial portions of the Software and where the
  software use is visible to an end-user.

  THE SOFTWARE IS PROVIDED "AS IS" FOR PRIVATE USE ONLY, IT IS NOT FOR COMMERCIAL USE IN WHOLE OR PART OR CONCEPT. FOR PERSONAL USE IT IS SUPPLIED WITHOUT WARRANTY
  OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHOR OR COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  See more at http://www.dsbird.org.uk
*/
#include "credentials.h"  // WiFi credentials and Site API configuration
#include <ArduinoJson.h>       // https://github.com/bblanchon/ArduinoJson
#include <WiFi.h>              // Built-in
#include <WiFiClientSecure.h>  // For HTTPS connections
#include "time.h"              // Built-in
#include <SPI.h>               // Built-in
#define  ENABLE_GxEPD2_display 0
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>

// Native Adafruit GFX bold fonts for darker text
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "lang.h"
//#include "lang_cz.h"                // Localisation (Czech)
//#include "lang_fr.h"                // Localisation (French)
//#include "lang_gr.h"                // Localisation (German)
//#include "lang_it.h"                // Localisation (Italian)
//#include "lang_nl.h"                // Localisation (Dutch)
//#include "lang_pl.h"                // Localisation (Polish)

#define SCREEN_WIDTH  400.0    // Set for landscape mode, don't remove the decimal place!
#define SCREEN_HEIGHT 300.0

enum alignment {LEFT, RIGHT, CENTER};

// Connections for CrowPanel
static const uint8_t EPD_BUSY = 48;  // to EPD BUSY
static const uint8_t EPD_CS   = 45;  // to EPD CS
static const uint8_t EPD_RST  = 47; // to EPD RST
static const uint8_t EPD_DC   = 46; // to EPD DC
static const uint8_t EPD_SCK  = 12; // to EPD CLK
static const uint8_t EPD_MISO = -1; // Master-In Slave-Out not used, as no data from display
static const uint8_t EPD_MOSI = 11; // to EPD DIN

// GxEPD2_BW<GxEPD2_420, GxEPD2_420::HEIGHT> display(GxEPD2_420(/*CS=D8*/ EPD_CS, /*DC=D3*/ EPD_DC, /*RST=D4*/ EPD_RST, /*BUSY=D2*/ EPD_BUSY));
//GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(/*CS=D8*/ EPD_CS, /*DC=D3*/ EPD_DC, /*RST=D4*/ EPD_RST, /*BUSY=D2*/ EPD_BUSY));
//CrowPanel
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(GxEPD2_420_GDEY042T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

//################  VERSION  ##########################
String version = "12.5";     // Version of this program
//################ VARIABLES ###########################

String  time_str, date_str; // strings to hold time and date
int     wifi_signal, CurrentHour = 0, CurrentMin = 0, CurrentSec = 0;
long    StartTime = 0;

//################ PROGRAM VARIABLES and OBJECTS ################

#define max_readings 50

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

//#########################################################################################
void setup() {
  StartTime = millis();
  Serial.begin(115200);
  if (StartWiFi() == WL_CONNECTED && SetupTime() == true) {
    if (CurrentHour >= WakeupTime && CurrentHour <= SleepTime ) {
      InitialiseDisplay(); // Give screen time to initialise
      byte Attempts = 1;
      bool RxData = false;
      WiFiClient client;   // wifi client object
      while (RxData == false && Attempts <= 2) { // Try up-to 2 times for site data
        if (RxData == false) RxData = ReceiveSiteData(client, true);
        Attempts++;
      }
      if (RxData) { // Only if received site data proceed
        StopWiFi(); // Reduces power consumption
        DisplaySiteData();
        // Note: display.display() is NOT needed after paged drawing -
        // the display is already updated when nextPage() returns false
      }
    }
  }
  BeginSleep();
}
//#########################################################################################
void loop() { // this will never run!
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
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    DrawHeadingSection();
    DrawMainDataSection(0, 17);
    DrawGraphSection(0, 132);
  } while (display.nextPage());
}
//#########################################################################################
void DrawHeadingSection() {
  // Site name as title - larger and bolder font
  display.setFont(&FreeSansBold12pt7b);
  drawString(SCREEN_WIDTH / 2, 0, SiteName, CENTER);

  // Date and time with smaller font
  display.setFont(&FreeSansBold9pt7b);
  drawString(SCREEN_WIDTH - 5, 0, date_str, RIGHT);
  drawString(4, 0, time_str, LEFT);
  DrawBattery(65, 12);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, GxEPD_BLACK);
}
//#########################################################################################
void DrawMainDataSection(int x, int y) {
  // Left panel - Air Temperature with large display
  display.drawRect(x, y, 130, 110, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 65, y + 5, "AIR TEMP", CENTER);
  display.drawLine(x, y + 18, x + 130, y + 18, GxEPD_BLACK);

  display.setFont(&FreeSansBold24pt7b);
  drawString(x + 65, y + 35, String(CurrentReading.Temperature, 1) + "C", CENTER);

  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 65, y + 75, "Water: " + String(CurrentReading.WaterTemp, 1) + "C", CENTER);

  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 65, y + 95, CurrentReading.Timestamp, CENTER);

  // Center panel - Pressure and Voltage
  display.drawRect(x + 135, y, 130, 110, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 200, y + 5, "PRESSURE", CENTER);
  display.drawLine(x + 135, y + 18, x + 265, y + 18, GxEPD_BLACK);

  display.setFont(&FreeSansBold18pt7b);
  drawString(x + 200, y + 35, String(CurrentReading.Pressure, 2), CENTER);

  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 200, y + 65, "Voltage: " + String(CurrentReading.Voltage, 2) + "V", CENTER);

  display.setFont(&FreeSansBold9pt7b);
  String statusStr = SiteInfo.Active ? "ACTIVE" : "OFFLINE";
  drawString(x + 200, y + 85, "Status: " + statusStr, CENTER);
  drawString(x + 200, y + 98, "Counter: " + String(CurrentReading.Counter), CENTER);

  // Right panel - Site info
  display.drawRect(x + 270, y, 130, 110, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 335, y + 5, "SITE INFO", CENTER);
  display.drawLine(x + 270, y + 18, x + 400, y + 18, GxEPD_BLACK);

  display.setFont(&FreeSansBold12pt7b);
  drawString(x + 335, y + 30, SiteInfo.SiteName, CENTER);

  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 335, y + 50, "Type: " + SiteInfo.SiteType, CENTER);

  display.setFont(&FreeSansBold9pt7b);
  drawString(x + 335, y + 70, "Readings: " + String(NumReadings), CENTER);
  drawString(x + 335, y + 85, "TZ: IST (+5:30)", CENTER);
  drawString(x + 335, y + 98, "Ladakh, India", CENTER);
}
//#########################################################################################
void DrawGraphSection(int x, int y) {
  // Title for graph section
  display.drawLine(0, y - 5, SCREEN_WIDTH, y - 5, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  drawString(SCREEN_WIDTH / 2, y, "Historical Data (last " + String(NumReadings * 5) + " min)", CENTER);

  // Prepare data arrays (reverse order - oldest first for proper graph display)
  float water_temp_readings[max_readings];
  for (int r = 0; r < NumReadings; r++) {
    temperature_readings[r] = SiteReadings[NumReadings - 1 - r].Temperature;
    water_temp_readings[r] = SiteReadings[NumReadings - 1 - r].WaterTemp;
    pressure_readings[r] = SiteReadings[NumReadings - 1 - r].Pressure;
  }

  // Draw 3 graphs side by side
  int graph_y = y + 15;
  int graph_h = 70;
  int graph_w = 100;

  DrawGraph(x + 30, graph_y, graph_w, graph_h, -10, 10, "Air Temp (C)", temperature_readings, NumReadings, autoscale_on, barchart_off);
  DrawGraph(x + 165, graph_y, graph_w, graph_h, 0, 10, "Water Temp (C)", water_temp_readings, NumReadings, autoscale_on, barchart_off);
  DrawGraph(x + 300, graph_y, graph_w, graph_h, 0, 2, "Pressure", pressure_readings, NumReadings, autoscale_on, barchart_off);
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
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.nist.gov"); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
  setenv("TZ", Timezone, 1);  //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
  tzset(); // Set the TZ environment variable
  delay(100);
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
  if (Units == "M") {
    if ((Language == "CZ") || (Language == "DE") || (Language == "NL") || (Language == "PL")) {
      sprintf(day_output, "%s, %02u. %s %04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900); // day_output >> So., 23. Juni 2019 <<
    }
    else
    {
      sprintf(day_output, "%s  %02u-%s-%04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900);
    }
    strftime(update_time, sizeof(update_time), "%H:%M:%S", &timeinfo);  // Creates: '@ 14:05:49'   and change from 30 to 8 <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    sprintf(time_output, "%s", update_time);
  }
  else
  {
    strftime(day_output, sizeof(day_output), "%a  %b-%d-%Y", &timeinfo); // Creates  'Sat May-31-2019'
    strftime(update_time, sizeof(update_time), "%r", &timeinfo);         // Creates: '@ 02:05:49pm'
    sprintf(time_output, "%s", update_time);
  }
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
    drawString(x + 65, y - 11, String(percentage) + "%", RIGHT);
    //drawString(x + 13, y + 5,  String(voltage, 2) + "v", CENTER);
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
  float x1, y1, x2, y2;
  if (auto_scale == true) {
    for (int i = 1; i < readings; i++ ) {
      if (DataArray[i] >= maxYscale) maxYscale = DataArray[i];
      if (DataArray[i] <= minYscale) minYscale = DataArray[i];
    }
    maxYscale = round(maxYscale + auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Max
    Y1Max = round(maxYscale + 0.5);
    if (minYscale != 0) minYscale = round(minYscale - auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Min
    Y1Min = round(minYscale);
  }
  // Draw the graph
  last_x = x_pos + 1;
  last_y = y_pos + (Y1Max - constrain(DataArray[1], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;
  display.drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  drawString(x_pos + gwidth / 2, y_pos - 12, title, CENTER);
  // Draw the graph
  last_x = x_pos;
  last_y = y_pos + (Y1Max - constrain(DataArray[1], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;
  display.drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, GxEPD_BLACK);
  drawString(x_pos + gwidth / 2, y_pos - 13, title, CENTER);
  // Draw the data
  for (int gx = 0; gx < readings; gx++) {
    y2 = y_pos + (Y1Max - constrain(DataArray[gx], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight + 1;
    if (barchart_mode) {
      x2 = x_pos + gx * (gwidth / readings) + 2;
      display.fillRect(x2, y2, (gwidth / readings) - 2, y_pos + gheight - y2 + 2, GxEPD_BLACK);
    } 
    else
    {
      x2 = x_pos + gx * gwidth / (readings - 1) + 1; // max_readings is the global variable that sets the maximum data that can be plotted
      display.drawLine(last_x, last_y, x2, y2, GxEPD_BLACK);
    }
    last_x = x2;
    last_y = y2;
  }
  //Draw the Y-axis scale
#define number_of_dashes 15
  for (int spacing = 0; spacing <= y_minor_axis; spacing++) {
    for (int j = 0; j < number_of_dashes; j++) { // Draw dashed graph grid lines
      if (spacing < y_minor_axis) display.drawFastHLine((x_pos + 3 + j * gwidth / number_of_dashes), y_pos + (gheight * spacing / y_minor_axis), gwidth / (2 * number_of_dashes), GxEPD_BLACK);
    }
    if ((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing) < 5) {
      drawString(x_pos, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
    }
    else
    {
      if (Y1Min < 1 && Y1Max < 10)
        drawString(x_pos - 3, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
      else
        drawString(x_pos - 3, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 0), RIGHT);
    }
  }
}
//#########################################################################################
void drawString(int x, int y, String text, alignment align) {
  int16_t  x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  display.setTextWrap(false);
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  display.setCursor(x, y + h);
  display.print(text);
}
//#########################################################################################
void drawStringMaxWidth(int x, int y, unsigned int text_width, String text, alignment align) {
  int16_t  x1, y1; //the bounds of x,y and w and h of the variable 'text' in pixels.
  uint16_t w, h;
  display.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  display.setCursor(x, y);
  if (text.length() > text_width * 2) {
    display.setFont(&FreeSansBold9pt7b);
    text_width = 42;
    y = y - 3;
  }
  display.println(text.substring(0, text_width));
  if (text.length() > text_width) {
    display.setCursor(x, y + h + 15);
    String secondLine = text.substring(text_width);
    secondLine.trim(); // Remove any leading spaces
    display.println(secondLine);
  }
}
//#########################################################################################
void InitialiseDisplay() {
  display.init(115200, true, 50, false);
  // display.init(); for older Waveshare HAT's
  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.display(true);
  delay(200);
}
/*
  Version 12.0 reformatted to use u8g2 fonts
  1.  Screen layout revised
  2.  Made consitent with other versions specifically 7x5 variant
  3.  Introduced Visibility in Metres, Cloud cover in % and RH in %
  4.  Correct sunrise/sunset time when in imperial mode.

  Version 12.1 Clarified Waveshare ESP32 driver board connections

  Version 12.2 Changed GxEPD2 initialisation from 115200 to 0
  1.  display.init(115200); becomes display.init(0); to stop blank screen following update to GxEPD2

  Version 12.3
  1. Added 20-secs to allow for slow ESP32 RTC timers
  
  Version 12.4
  1. Improved graph drawing function for negative numbers Line 808
  
  Version 12.5
  1. Modified for GxEPD2 changes
*/
