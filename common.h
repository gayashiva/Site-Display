//

#include <Arduino.h>
#include <HTTPClient.h>

// Site Data API reading structure
typedef struct {
  int      Dt;           // Unix timestamp
  String   Timestamp;    // Human readable timestamp
  float    Temperature;  // Ambient temperature
  float    WaterTemp;    // Water temperature
  float    Pressure;     // Pressure reading
  float    Voltage;      // Battery/supply voltage
  int      Counter;      // Reading counter
} SiteReading_type;

// Site metadata
typedef struct {
  String   SiteName;
  String   SiteType;     // "air" or "drip"
  bool     Active;
  int      TimezoneOffset;
  int      QueryTime;
} SiteInfo_type;

SiteInfo_type     SiteInfo;
SiteReading_type  CurrentReading;
SiteReading_type  SiteReadings[max_readings];
int               NumReadings = 0;

// Function declarations
bool ReceiveSiteData(WiFiClient& client, bool print);
bool DecodeSiteData(JsonDocument& doc, bool print);
String ConvertUnixTime(int unix_time);
int JulianDate(int d, int m, int y);
String TitleCase(String text);
double NormalizedMoonPhase(int d, int m, int y);

//#########################################################################################
String ConvertUnixTime(int unix_time) {
  // Returns either '21:12  ' or ' 09:12pm' depending on Units mode
  time_t tm = unix_time;
  struct tm *now_tm = gmtime(&tm);
  char output[40];
  if (Units == "M") {
    strftime(output, sizeof(output), "%H:%M %d/%m/%y", now_tm);
  }
  else {
    strftime(output, sizeof(output), "%I:%M%P %m/%d/%y", now_tm);
  }
  return output;
}
//#########################################################################################
// Fetch data from Site Data API (HTTPS)
bool ReceiveSiteData(WiFiClient& client, bool print) {
  Serial.println("Fetching site data...");
  client.stop(); // close connection before sending a new request
  HTTPClient http;

  // Build the API URL - use HTTPS (port 443)
  String url = "https://" + String(server) + "/prod/site?site_name=" + SiteName + "&count=" + String(ReadingCount);
  Serial.println("URL: " + url);

  http.begin(url);
  int httpCode = http.GET();

  if(httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    if (print) Serial.println("Response: " + payload);

    // Parse JSON from string
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      http.end();
      return false;
    }

    if (!DecodeSiteData(doc, print)) {
      http.end();
      return false;
    }
    http.end();
    return true;
  }
  else {
    Serial.printf("Connection failed, HTTP code: %d, error: %s\n", httpCode, http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }
}
//#######################################################################################
bool DecodeSiteData(JsonDocument& doc, bool print) {
  if (print) Serial.println("Decoding Site Data...");

  // Parse site metadata
  SiteInfo.SiteName       = doc["site_name"].as<String>();
  SiteInfo.SiteType       = doc["site_type"].as<String>();
  SiteInfo.Active         = doc["active"];
  SiteInfo.TimezoneOffset = doc["timezone_offset"];
  SiteInfo.QueryTime      = doc["query_time"];

  if (print) {
    Serial.println("Site: " + SiteInfo.SiteName);
    Serial.println("Type: " + SiteInfo.SiteType);
    Serial.println("Active: " + String(SiteInfo.Active));
    Serial.println("TZ Offset: " + String(SiteInfo.TimezoneOffset));
  }

  // Parse current reading
  JsonObject current = doc["current"];
  CurrentReading.Dt          = current["dt"];
  CurrentReading.Timestamp   = current["timestamp"].as<String>();
  CurrentReading.Temperature = current["temperature"];
  CurrentReading.WaterTemp   = current["water_temp"];
  CurrentReading.Pressure    = current["pressure"];
  CurrentReading.Voltage     = current["voltage"];
  CurrentReading.Counter     = current["counter"];

  if (print) {
    Serial.println("\nCurrent Reading:");
    Serial.println("  Time: " + CurrentReading.Timestamp);
    Serial.println("  Temp: " + String(CurrentReading.Temperature) + "C");
    Serial.println("  Water: " + String(CurrentReading.WaterTemp) + "C");
    Serial.println("  Pressure: " + String(CurrentReading.Pressure));
    Serial.println("  Voltage: " + String(CurrentReading.Voltage) + "V");
  }

  // Parse historical readings array
  JsonArray readings = doc["readings"];
  NumReadings = min((int)readings.size(), max_readings);

  if (print) Serial.println("\nHistorical Readings (" + String(NumReadings) + "):");

  for (int r = 0; r < NumReadings; r++) {
    JsonObject reading = readings[r];
    SiteReadings[r].Dt          = reading["dt"];
    SiteReadings[r].Timestamp   = reading["timestamp"].as<String>();
    SiteReadings[r].Temperature = reading["temperature"];
    SiteReadings[r].WaterTemp   = reading["water_temp"];
    SiteReadings[r].Pressure    = reading["pressure"];
    SiteReadings[r].Voltage     = reading["voltage"];
    SiteReadings[r].Counter     = reading["counter"];

    if (print) {
      Serial.println("  [" + String(r) + "] " + SiteReadings[r].Timestamp +
                     " T:" + String(SiteReadings[r].Temperature) +
                     " W:" + String(SiteReadings[r].WaterTemp));
    }
  }

  return true;
}

int JulianDate(int d, int m, int y) {
  int mm, yy, k1, k2, k3, j;
  yy = y - (int)((12 - m) / 10);
  mm = m + 9;
  if (mm >= 12) mm = mm - 12;
  k1 = (int)(365.25 * (yy + 4712));
  k2 = (int)(30.6001 * mm + 0.5);
  k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
  // 'j' for dates in Julian calendar:
  j = k1 + k2 + d + 59 + 1;
  if (j > 2299160) j = j - k3; // 'j' is the Julian date at 12h UT (Universal Time) For Gregorian calendar:
  return j;
}

float SumOfPrecip(float DataArray[], int readings) {
  float sum = 0;
  for (int i = 0; i < readings; i++) {
    sum += DataArray[i];
  }
  return sum;
}

String TitleCase(String text){
  if (text.length() > 0) {
    String temp_text = text.substring(0,1);
    temp_text.toUpperCase();
    return temp_text + text.substring(1); // Title-case the string
  }
  else return text;
}

double NormalizedMoonPhase(int d, int m, int y) {
  int j = JulianDate(d, m, y);
  //Calculate the approximate phase of the moon
  double Phase = (j + 4.867) / 29.53059;
  return (Phase - (int) Phase);
}


