/**The MIT License (MIT)

Copyright (c) 2018 by Daniel Eichhorn - ThingPulse

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See more at https://thingpulse.com
*/

#include <Arduino.h>

// time
#include <time.h>     // time() ctime()
#include <sys/time.h> // struct timeval
// #include <coredecls.h>                  // settimeofday_cb()

// sensors / displays
#include <SH1106.h>
#include <OLEDDisplayUi.h>
#include <Adafruit_AHTX0.h>

// networking
#include <WiFiManager.h>
#include <AsyncMqttClient.h>

// local libs
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

// declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state);
void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
void setReadyForWeatherUpdate();
void setReadyForTempHumidUpdate();
void setUpWiFi();
void disableWiFi();
void enableWiFi();
void configModeCallback(WiFiManager *myWiFiManager);
void connectToMqtt();
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(uint16_t packetId, uint8_t qos);
void onMqttUnsubscribe(uint16_t packetId);
void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void onMqttPublish(uint16_t packetId);
void mqttPublishTempHumid(void);

/***************************
 * Begin Settings
 **************************/
#define TZ 1      // (utc+) TZ in hours
#define DST_MN 60 // use 60mn for summer time in some countries

// Setup
const int UPDATE_INTERVAL_SECS = 15 * 60; // Update every 15 minutes

// Display Settings
const int I2C_DISPLAY_ADDRESS = 0x3c;
const int SDA_PIN = D4;
const int SDC_PIN = D5;

const int TEMP_HUMID_UPDATE_INTERVAL_SECS = 20; // Update every 30 secs
char formattedTemperature[10];
char formattedHumidity[10];

// OpenWeatherMap Settings
// Sign up here to get an API key:
// https://docs.thingpulse.com/how-tos/openweathermap-key/
String OPEN_WEATHER_MAP_APP_ID = "";
/*
Go to https://openweathermap.org/find?q= and search for a location. Go through the
result set and select the entry closest to the actual location you want to display
data for. It'll be a URL like https://openweathermap.org/city/2657896. The number
at the end is what you assign to the constant below.
 */
String OPEN_WEATHER_MAP_LOCATION_ID = "";

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.
const String OPEN_WEATHER_MAP_LANGUAGE = F("de");
const uint8_t MAX_FORECASTS = 4;

const boolean IS_METRIC = true;

// Adjust according to your language
const String WDAY_NAMES[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OKT", "NOV", "DEZ"};

/***************************
 * End Settings
 **************************/
// Initialize the oled display for address 0x3c
// sda-pin=14 and sdc-pin=12
SH1106 display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi ui(&display);

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

#define TZ_MN ((TZ) * 60)
#define TZ_SEC ((TZ) * 3600)
#define DST_SEC ((DST_MN) * 60)
time_t now;

bool readyForWeatherUpdate = false;
bool readyForTempHumidUpdate = false;

String lastUpdate = "--";
long timeSinceLastWUpdate = 0;
long timeSinceLastTempHumidUpdate = 0;

// TempHumid Settings
// #define TempHumidPIN D2 // NodeMCU
// #define TempHumidPIN 3 // Wemos D1R2 Mini
// #define TempHumidTYPE TempHumidesp::AM2302   // TempHumid22  (AM2302), AM2321
Adafruit_AHTX0 aht;
// Initialize the temperature/ humidity sensor
// TempHumidesp dht;
float humidity = 0.0;
float temperature = 0.0;

// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
FrameCallback frames[] = {drawDateTime, drawCurrentWeather, drawForecast, drawIndoor};
int numberOfFrames = 4;

OverlayCallback overlays[] = {drawHeaderOverlay};
int numberOfOverlays = 1;

#define MQTT_HOST IPAddress(192, 67, 202, 206)
// #define MQTT_HOST "a1datapi.local"
#define MQTT_PORT 1883

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

void setup()
{
  Serial.begin(115200);
  Serial.println();

  // initialize dispaly
  display.init();
  display.clear();
  display.display();
  // display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  display.drawString(64, 10, F("Suche nach Netzwerken"));
  display.drawXbm(46, 30, 8, 8, inactiveSymbole);
  display.drawXbm(60, 30, 8, 8, inactiveSymbole);
  display.drawXbm(74, 30, 8, 8, inactiveSymbole);
  display.display();

  // WiFi
  // WiFiManager
  // wifiManager.resetSettings();
  setUpWiFi();

  // Get time from network time service
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  ui.setTargetFPS(30);

  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);

  // You can change this to
  // TOP, LEFT, BOTTOM, RIGHT
  ui.setIndicatorPosition(BOTTOM);

  // Defines where the first frame is located in the bar.
  ui.setIndicatorDirection(LEFT_RIGHT);

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

  // Inital UI takes care of initalising the display too.
  ui.init();

  // initialize dht
  // dht.setup(TempHumidPIN, TempHumidTYPE); // Connect TempHumid sensor to GPIO 17
  if (!aht.begin())
  {
    Serial.println(F("Could not find AHT? Check wiring"));
    while (1)
      delay(10);
  }
  Serial.println(F("AHT10 or AHT20 found"));

  updateData(&display);

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
}

void loop()
{

  if (millis() - timeSinceLastWUpdate > (1000L * UPDATE_INTERVAL_SECS))
  {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (millis() - timeSinceLastTempHumidUpdate > (1000L * TEMP_HUMID_UPDATE_INTERVAL_SECS))
  {
    setReadyForTempHumidUpdate();
    timeSinceLastTempHumidUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED)
  {
    updateData(&display);
  }

  if (readyForTempHumidUpdate && ui.getUiState()->frameState == FIXED)
  {
    sensors_event_t aht_humidity, aht_temp;
    aht.getEvent(&aht_humidity, &aht_temp);
    humidity = aht_humidity.relative_humidity;
    temperature = aht_temp.temperature;
    readyForTempHumidUpdate = false;
    mqttPublishTempHumid();
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0)
  {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
    // disableWiFi();
    // esp_sleep_enable_timer_wakeup(remainingTimeBudget * 1000);
    // int ret = esp_light_sleep_start();
    // Serial.printf("light_sleep: %d\n", ret);
    // enableWiFi();
  }
}

void drawProgress(OLEDDisplay *display, int percentage, String label)
{
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display)
{
  drawProgress(display, 10, F("Aktualisiere Zeit..."));
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  drawProgress(display, 20, F("Aktualisiere Zeit..."));
  now = time(nullptr);
  struct tm *timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));

  drawProgress(display, 30, F("Aktualisiere Wetter..."));
  currentWeatherClient.setMetric(IS_METRIC);
  drawProgress(display, 40, F("Aktualisiere Wetter..."));
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  drawProgress(display, 50, F("Aktualisiere Wetter..."));
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);

  drawProgress(display, 60, F("Aktualisiere Vorhersage..."));
  forecastClient.setMetric(IS_METRIC);
  drawProgress(display, 70, F("Aktualisiere Vorhersage..."));
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  drawProgress(display, 80, F("Aktualisiere Vorhersage..."));
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  drawProgress(display, 90, F("Aktualisiere Vorhersage..."));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  drawProgress(display, 100, F("Fertig!"));
  readyForWeatherUpdate = false;
  delay(300);
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  now = time(nullptr);
  struct tm *timeInfo;
  timeInfo = localtime(&now);
  char buff[16];

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon + 1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}

void drawForecast(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex)
{
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm *timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState *state)
{
  now = time(nullptr);
  struct tm *timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
}

void drawIndoor(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16);
  display->drawString(64 + x, 0, F("Innen"));
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  dtostrf(temperature, 4, 1, formattedTemperature);
  display->drawString(x, 20, "Temperatur:  " + String(formattedTemperature) + (IS_METRIC ? "°C" : "°F"));
  dtostrf(humidity, 4, 1, formattedHumidity);
  display->drawString(x, 34, "Luftfeuchte:  " + String(formattedHumidity) + "%");
}

void setReadyForWeatherUpdate()
{
  Serial.println(F("Setting readyForUpdate to true"));
  readyForWeatherUpdate = true;
}

void setReadyForTempHumidUpdate()
{
  Serial.println(F("Setting readyForTempHumidUpdate to true"));
  readyForTempHumidUpdate = true;
}

void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println(F("Entered config mode"));
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(38, 0, F("WIFI SETUP"));
  display.drawXbm(17, 2, 8, 8, inactiveSymbole);
  display.drawXbm(26, 2, 8, 8, activeSymbole);
  display.drawXbm(99, 2, 8, 8, activeSymbole);
  display.drawXbm(108, 2, 8, 8, inactiveSymbole);
  display.drawString(0, 8, F("1. Mit 'WeatherStation'"));
  display.drawString(0, 18, F("    verbinden."));
  display.drawString(0, 29, F("2. '192.168.4.1' öffnen."));
  display.drawString(0, 40, F("3. WiFi einstellen."));
  display.drawString(0, 51, F("4. Speichern und fertig :)"));
  display.display();
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void setUpWiFi()
{
  WiFiManager wifiManager;
  wifiManager.setCountry("DE");
  wifiManager.setDarkMode(true);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeoutCallback(onWifiConfigPortalTimeout);
  wifiManager.setConfigPortalTimeout(5 * 60);
  wifiManager.setConnectTimeout(45);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.autoConnect("WeatherStation");
  WiFi.onEvent(onWiFiEvent);
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));
}

void onWifiConfigPortalTimeout()
{
  Serial.println(F("WiFi Config Portal has timed out..."));
}

void disableWiFi()
{
  WiFi.mode(WIFI_OFF); // Switch WiFi off
  Serial.println(F("WiFi disconnected!"));
}

void enableWiFi()
{
  WiFi.mode(WIFI_STA); // Switch WiFi off
  Serial.print(F("WiFi connected. IP: "));
  Serial.println(WiFi.localIP());
}

void connectToWifi()
{
  if (!WiFi.isConnected())
  {
    Serial.println("Attempting to reconnect to Wi-Fi...");
    WiFi.begin();
  }
}

void onWiFiEvent(WiFiEvent_t event)
{
  Serial.printf("[WiFi Event] event: %d\n", event);
  switch (event)
  {
  case SYSTEM_EVENT_STA_GOT_IP:
    Serial.println(F("WiFi connected!"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
    connectToMqtt();
    xTimerStop(wifiReconnectTimer, 0);
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    Serial.println(F("WiFi lost connection. Attempting to reconnect..."));
    xTimerStop(mqttReconnectTimer, 0);
    xTimerStart(wifiReconnectTimer, 0);
    break;
  }
}

void connectToMqtt()
{
  if (WiFi.isConnected())
    if (!mqttClient.connected())
    {
      Serial.println(F("Attempting to connect MQTT..."));
      mqttClient.connect();
    }
    else
    {
      xTimerStop(mqttReconnectTimer, 0);
    }
}

void onMqttConnect(bool sessionPresent)
{
  Serial.println(F("Connected to MQTT."));
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{

  if (WiFi.isConnected() && mqttClient.connected())
  {
    Serial.println(F("Disconnected from MQTT."));
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
  Serial.println(F("Subscribe acknowledged."));
  Serial.print(F("  packetId: "));
  Serial.println(packetId);
  Serial.print(F("  qos: "));
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId)
{
  Serial.println(F("Unsubscribe acknowledged."));
  Serial.print(F("  packetId: "));
  Serial.println(packetId);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  Serial.println(F("Publish received."));
  Serial.print(F("  topic: "));
  Serial.println(topic);
  Serial.print(F("  qos: "));
  Serial.println(properties.qos);
  Serial.print(F("  dup: "));
  Serial.println(properties.dup);
  Serial.print(F("  retain: "));
  Serial.println(properties.retain);
  Serial.print(F("  len: "));
  Serial.println(len);
  Serial.print(F("  index: "));
  Serial.println(index);
  Serial.print(F("  total: "));
  Serial.println(total);
}

void onMqttPublish(uint16_t packetId)
{
  Serial.println(F("Publish acknowledged."));
  Serial.print(F("  packetId: "));
  Serial.println(packetId);
}

void mqttPublishTempHumid(void)
{
  if (mqttClient.connected())
  {
    Serial.print(F("MQTT: Publishing temp and humidity... "));
    dtostrf(temperature, 4, 1, formattedTemperature);
    dtostrf(humidity, 4, 1, formattedHumidity);
    uint16_t packetIdPub1 = mqttClient.publish("test/temperature", 2, true, formattedTemperature);
    uint16_t packetIdPub2 = mqttClient.publish("test/humidity", 2, true, formattedHumidity);
    Serial.println(F("Done!"));
  }
  else
    Serial.println(F("MQTT: Can not publish data - not connected... "));
  connectToMqtt();
}
