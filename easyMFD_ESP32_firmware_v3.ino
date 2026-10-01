/* *** Version 3 With Alerting ***
 * 
 * Signalk includes very powerful and flexible web-based dashboards and UI functionality,
 * but there are times when it may be convenient to display common signalk data on
 * low-cost tft displays that lack browser functionality. easyMFD is a package that includes a 
 * Node-RED FlowFuse GUI dashboard to select most common Signalk path data and display
 * these values on such tft displays dynamically (displays update when values change in 
 * SK, or user can build drag-and-drop Node Red logic to send such data to any ESP32-controlled
 * TFT display running this firmware). 
 * 
 * Additionally, the package includes the ability for users to create any number of audible alerts, 
 * which can be sent to any 3-5V piezoelectric buzzer, wired to the same ESP32 controlling the
 * display.  The GUI allows users to create and save any number of bespoke 
 * specifying alert tones, setting buzzer in frequency (in hertz), tone characteristics (on/off time, 
 * duration of alert, etc.) to the buzzer attached to any useable PWM pin on the same display ESP32. 
 * 
 * The package supports multiple displays (there is no real limit to the number of displays used, 
 * or whether the displays are wired with a buzzer or not.  Each display has a unique identifier 
 * (the MQTT topic, user definable) and the alerting and displaying functionality are intentionally
 * kept seperate (although they are controlled from the sameGUI) so that alerts can be sent with 
 * or without display changes.  
 * 
 * Typical (external) ESP32/TFT display wiring (where ESP32 is not integrated into the display PCB:
 * ________________
 * |              |
 * | GPIO PWM Pin |---Optional ~100ohm resister-----Pizeo Buzzer----
 * |              |                                                |
 * |      GND Pin |-------------------------------------------------
 * |              |     ____________
 * |           CS |-----|  DISPLAY |
 * |           DC |-----|          |
 * |          RST |-----|          |
 * |       SCL/SCK|-----|          |
 * |      SDA/MOSI|-----|          |
 * |          3-5v|-----|          |
 * |           GND| ----|          |
 * |     backlight|-----|          | 
 * L______________|     L__________|
 * 
 * 
 * See readme for details on architecture and use, but from a high level, the package is 
 * intentionally designed to separate the Node Red trigger logic (for both buzzer/alerts)
 * from the display/alert IO/signaling logic.  That is, any logic that can be created
 * in a flow in Node Red using Signalk or other, i.e., non-Signal K data (an MQTT-attached 
 * motion sensor, other sensors or boat systems that can integrate with Node Red via
 * MQTT). Thus to use the package practically, you must either be comfortable with 
 * creating Node Red logic flows, or you must integrate existing flows into the easyMFD
 * display/alerting nodes (see readme for details).  This allows users essentially 
 * limitless flexbility in displaying and alerting.  
 * 
 * Some simple examples to illustrate the concept:
 * 
 * 1. a display can be set to show the current time on the hour, every hour, with font 
 * brightness adjusted automatically per time of day, and sound a simple, low-volume 
 * alert -  à la a 'ships bell' - except when watches change, at which point (or X minutes
 * before a watch change), the time display can be made to change color (a signal for 
 * pending watch change, for example) and a louder, repeating buzzer tone can be sounded 
 * at, say, 2000hz tone for X seconds (or whatever you specify in the GUI for that Alert Profile).  
 * 
 * 2. A display can be set to show current autopilot maintainted COG, with the display autmatically 
 * changing its content for a user definted period to temporarily show windspeed if it suddenly increases
 * beyond a specific user-defined threshold, while a specific 'warning' alert tone, ie., a
 * 6000hz, full volume, oscillating on/off tone alert can be audibly sent to a display which 
 * can show current wind speed in a large, red 'alert like' font, versus whatever is currently 
 * displayed. Literally any boat-related event configurable in Node Red can be matched to a
 * display-specific output and 'alert' tone of the user's choosing, with all 'Alert.." and 
 * "Display Profiles" saved in non-volatile Node-RED memory for future use.
 * 
 * That's the gist of it.  There is probably a bit of alearning curve involved in using the 
 * package but for me and my boat, this sort of functionality is of value and I hope it is
 * of use to other boaters.
 * 
 * 
 * Note the current version cannot yet dynamically adjust display 
 * content for the size of the TFT screen (it must be set in the GUI by the user currently),
 * nor can it show graphical boat instrument widgets, although I hope to include both of those
 * features in a future release (which would allow, for example, a common Signalk Anchor 
 * Alarm plugin to be continuously or temporarily displayed on a TFT, based on user-defined 
 * logic (if anchor approaches parameter of anchor alarm geo-fence, for example, display 
 * anchor alarm image and sound custom alert tone).
 * 
 * 
 * ESP32 ibraries needed (Arduino IDE Library Manager):
 *   - LovyanGFX
 *   - ArduinoJson (v6.x)
 *   - PubSubClient (by Nick O'Leary)
 * 
 * Node Red Nodes/Palettes needed:
 *  - Signalk node package (ideally, use embedded version of Node Red that is
 *    included with signalk.
 *  - Flowfuse dashboard (for easyMFD GUI)
 *  - MQTT Node package (if not using embedded SK version of NR, which includes
 *    these nodes by default.
 * 
 * IMPORTANT: Because the package was written to display on essentially any size
 * TFT display, you must create (or have Clause create) a TFT-specific 'class' and replace
 * the class used in the code below (which is for 4" dislays in the code below with that code.  
 * 
 * 
 * To install, wire up the ESP32 either using the pinout below, or use your own
 * pinout (just be sure to change the pins below) then flash the MCU and the 
 * display should be good-to-go.  I will later post some simple 3D-printable STL
 * files for common display sizes.
 * 
 * Be sure to add your wifi credentials and Mosquitto/MQTT server and port
 * 
 */

#include <LovyanGFX.hpp>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------
// 0. WIFI / MQTT CONFIG -- wifi network + broker details
// ---------------------------------------------------------------
const char* WIFI_SSID     = "yourWifiSSID";
const char* WIFI_PASSWORD = "yourWifiPassword";

const char* MQTT_BROKER   = "192.168.1.x";   // your Mosquitto host IP
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "helm_display"; // one topic per display
const char* MQTT_CLIENT_ID= "tft1_helm_display"; // must be unique per device on the broker

// Buzzer alert topic is always "<MQTT_TOPIC>/buzzer" -- derived once
// at startup so it can never drift out of sync with MQTT_TOPIC above.  Topic determines
// which display the MQTT broker communicates with. Must be unique per display.

char mqttBuzzerTopic[64];

WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ---------------------------------------------------------------
// 0b. DISPLAY TUNING -- defaults + current (possibly overridden by
//     JSON) settings. See persistence model in the header comment.
// ---------------------------------------------------------------
const uint16_t DEFAULT_FONT_COLOR = TFT_WHITE;
const uint8_t  DEFAULT_BRIGHTNESS = 255;
const uint8_t  DEFAULT_FONT_SIZE  = 1; // multiplier on Font7's native size

uint16_t currentFontColor  = DEFAULT_FONT_COLOR;
uint8_t  currentBrightness = DEFAULT_BRIGHTNESS;
uint8_t  currentFontSize   = DEFAULT_FONT_SIZE;

// ---------------------------------------------------------------
// 0c. BUZZER CONFIG -- piezo buzzer on its own GPIO, driven via LEDC
//     (PWM) so both loudness (duty cycle) and tone (frequency) are
//     controllable from a single peripheral. See header comment for
//     the JSON schema this responds to.
// ---------------------------------------------------------------
const int      BUZZER_PIN            = 7;
const int      LEDC_RESOLUTION_BITS  = 10;                    // duty resolution: 0-1023
const uint32_t LEDC_MAX_DUTY         = (1 << LEDC_RESOLUTION_BITS) - 1; // 1023

// Buzzer state machine -- all non-blocking, driven off millis() and
// serviced every loop() iteration so it never blocks MQTT/display.
bool     buzzerActive        = false;
bool     buzzerSegmentIsOn   = false;
uint32_t buzzerDuty          = 0;     // computed from "loudness" 0-100
uint32_t buzzerSegmentMs     = 0;
uint32_t buzzerPauseMs       = 0;
unsigned long buzzerAlertEndAt   = 0; // millis() timestamp -- whole alert stops here
unsigned long buzzerPhaseEndAt   = 0; // millis() timestamp -- current on/off phase ends here

// Starts (or restarts) a buzzer alert. Called once per incoming
// buzzer MQTT message; all repetition afterward is handled by
// serviceBuzzer() below.
void startBuzzerAlert(int loudnessPct, int frequencyHz, uint32_t durationMs,
                       uint32_t segmentMs, uint32_t pauseMs)
{
  if (loudnessPct < 0)   loudnessPct = 0;
  if (loudnessPct > 100) loudnessPct = 100;
  if (frequencyHz < 20)  frequencyHz = 20; // LEDC floor guard; well below audible anyway

  ledcChangeFrequency(BUZZER_PIN, frequencyHz, LEDC_RESOLUTION_BITS);

  buzzerDuty      = (uint32_t)((loudnessPct / 100.0f) * LEDC_MAX_DUTY);
  buzzerSegmentMs = segmentMs;
  buzzerPauseMs   = pauseMs;

  unsigned long now = millis();
  buzzerAlertEndAt  = now + durationMs;

  // Start immediately in the "on" phase, capped to the overall
  // alert duration in case segmentMs alone would overrun it.
  ledcWrite(BUZZER_PIN, buzzerDuty);
  buzzerSegmentIsOn = true;
  buzzerPhaseEndAt  = min(now + buzzerSegmentMs, buzzerAlertEndAt);
  buzzerActive      = true;
}

// Call every loop() iteration. Advances the on/off segment pattern
// and stops the buzzer outright once the total duration has elapsed
// -- even mid-segment or mid-pause, so an alert never overruns.
void serviceBuzzer()
{
  if (!buzzerActive) return;

  unsigned long now = millis();

  if (now >= buzzerAlertEndAt)
  {
    ledcWrite(BUZZER_PIN, 0);
    buzzerActive = false;
    return;
  }

  if (now >= buzzerPhaseEndAt)
  {
    if (buzzerSegmentIsOn)
    {
      // Segment finished -- switch to pause (silence).
      ledcWrite(BUZZER_PIN, 0);
      buzzerSegmentIsOn = false;
      buzzerPhaseEndAt  = min(now + buzzerPauseMs, buzzerAlertEndAt);
    }
    else
    {
      // Pause finished -- switch back to an "on" segment.
      ledcWrite(BUZZER_PIN, buzzerDuty);
      buzzerSegmentIsOn = true;
      buzzerPhaseEndAt  = min(now + buzzerSegmentMs, buzzerAlertEndAt);
    }
  }
}

// Parses an incoming buzzer-topic MQTT payload and kicks off the
// alert. Missing fields fall back to conservative defaults rather
// than failing silently on a malformed/partial message.
void handleBuzzerJson(const char* jsonStr)
{
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);

  if (err)
  {
    Serial.print("[Buzzer] JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  int      loudnessPct = doc["loudness"]     | 100;
  int      frequencyHz = doc["frequency_hz"] | 2000;
  uint32_t durationMs  = doc["duration_ms"]  | 1000;
  uint32_t segmentMs   = doc["segment_ms"]   | durationMs; // default: one continuous segment
  uint32_t pauseMs     = doc["pause_ms"]     | 0;

  Serial.printf("[Buzzer] loudness=%d%% freq=%dHz duration=%lums segment=%lums pause=%lums\n",
                loudnessPct, frequencyHz, (unsigned long)durationMs,
                (unsigned long)segmentMs, (unsigned long)pauseMs);

  startBuzzerAlert(loudnessPct, frequencyHz, durationMs, segmentMs, pauseMs);
}

// ---------------------------------------------------------------
// 1. LGFX CONFIG CLASS -- reusable display config class code
// ---------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9486 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;


/* 
 *  
 * DISPLAY SPECIFIC CLASS CODE HERE. 
 * Replace class below (which is for a 4" ST7796-driven display) with display class specific to
 * your display and display driver.  IMPORTANT: be sure to define class as 'LGFX(void)' and
 * instantiate with 'static LGFX lcd;' in your class code.  The rest of the code is display 
 * agnostic so there is no need to change any other part of the firmware code.  
 * 
 */
  
public:
  LGFX(void)
  {
    { // SPI Bus Configuration
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 24000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 8;
      cfg.pin_mosi = 9;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 10;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel Configuration
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 13;
      cfg.pin_rst          = 11;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      _panel_instance.config(cfg);
    }

    { // Backlight Configuration
      auto cfg = _light_instance.config();
      cfg.pin_bl = 12;
      cfg.freq   = 12000;
      cfg.pwm_channel = 1;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};

static LGFX lcd;

// ---------------------------------------------------------------
// 1b. VALUE SPRITE -- off-screen buffer for the big number.
//     We compose the new value here (clear + draw text) entirely
//     in RAM, then push the finished result to the panel in one
//     single fast transfer. This is what eliminates the visible
//     "flash" of the old clear-then-draw-on-panel approach -- the
//     panel never sees a blank intermediate state.
// ---------------------------------------------------------------
static LGFX_Sprite valueSprite(&lcd);
const int VALUE_AREA_HEIGHT = 140; // must match old fillRect height
int valueAreaTop = 0;              // computed in setup() from lcd.height()

// ---------------------------------------------------------------
// 2. Kept only as a fallback so the screen shows something sane
//    before the first MQTT message arrives.
// ---------------------------------------------------------------
const char* fakeIncomingJson = R"({"title":"Waiting for data","value":"--","unit":""})";

// ---------------------------------------------------------------
// 3. DISPLAY LOGIC
//    title  -> small text, top      -- drawn once, kept static
//    value  -> big 7-segment digits -- redrawn on every update
//    unit   -> small text, below    -- drawn once, kept static
//
//    Split into two functions so a data update only repaints the
//    value's own area, instead of flashing the whole screen.
// ---------------------------------------------------------------
void drawStaticLabels(const char* title, const char* unit)
{
  lcd.startWrite();
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  // --- Title ---
  lcd.setFont(&fonts::Font4);
  lcd.setTextSize(1);
  lcd.setTextDatum(lgfx::textdatum_t::top_center);
  lcd.drawString(title, lcd.width() / 2, 30);

  // --- Unit ---
  lcd.drawString(unit, lcd.width() / 2, (lcd.height() / 2) + 90);

  lcd.endWrite();
}

// ---------------------------------------------------------------
// 3b. FONT COLOR PARSER -- accepts either a common color name or a
//     hex string like "#FF8800". Unrecognized input falls back to
//     the default color rather than silently drawing invisible
//     (e.g. black-on-black) text.
// ---------------------------------------------------------------
uint16_t parseFontColor(const char* name)
{
  if (strcmp(name, "white")   == 0) return TFT_WHITE;
  if (strcmp(name, "red")     == 0) return TFT_RED;
  if (strcmp(name, "green")   == 0) return TFT_GREEN;
  if (strcmp(name, "blue")    == 0) return TFT_BLUE;
  if (strcmp(name, "yellow")  == 0) return TFT_YELLOW;
  if (strcmp(name, "cyan")    == 0) return TFT_CYAN;
  if (strcmp(name, "magenta") == 0) return TFT_MAGENTA;
  if (strcmp(name, "orange")  == 0) return TFT_ORANGE;

  if (name[0] == '#' && strlen(name) == 7)
  {
    long hex = strtol(name + 1, NULL, 16);
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8)  & 0xFF;
    uint8_t b = hex & 0xFF;
    return lcd.color565(r, g, b);
  }

  return DEFAULT_FONT_COLOR;
}

void drawValue(const char* value)
{
  // Everything below happens in RAM -- fillScreen/drawString on a
  // sprite never touch the SPI bus. Only pushSprite() at the end
  // sends anything to the panel, as one single transfer.
  valueSprite.fillScreen(TFT_BLACK);

  valueSprite.setFont(&fonts::Font7);
  valueSprite.setTextSize(currentFontSize);
  valueSprite.setTextColor(currentFontColor, TFT_BLACK);
  valueSprite.setTextDatum(lgfx::textdatum_t::middle_center);
  valueSprite.drawString(value, valueSprite.width() / 2, valueSprite.height() / 2);

  valueSprite.pushSprite(0, valueAreaTop);
}

// ---------------------------------------------------------------
// 4b. STATUS MESSAGE HELPER -- simple full-screen text, used only
//     during WiFi/MQTT connection so you can SEE what stage it's
//     stuck on, instead of just staring at "Waiting for data".
// ---------------------------------------------------------------
void showStatus(const char* msg)
{
  lcd.startWrite();
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(&fonts::Font4);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.setTextDatum(lgfx::textdatum_t::middle_center);
  lcd.drawString(msg, lcd.width() / 2, lcd.height() / 2);
  lcd.endWrite();
}
void renderFromJson(const char* jsonStr)
{
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);

  if (err)
  {
    // Show the error on-screen rather than failing silently --
    // useful once real (possibly malformed) MQTT payloads show up.
    lcd.startWrite();
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.setCursor(10, 10);
    lcd.printf("JSON parse error:\n%s", err.c_str());
    lcd.endWrite();
    return;
  }

  const char* title = doc["title"] | "";
  const char* value = doc["value"] | "";
  const char* unit  = doc["unit"]  | "";

  static char lastTitle[64] = "";
  static char lastUnit[16]  = "";
  static bool staticLabelsDrawn = false;

  // "title" changing (or first message ever) = a new data source =
  // a new "session". Reset display settings to firmware defaults
  // BEFORE applying this message's overrides, so a custom color set
  // for the previous data source never bleeds into the next one.
  bool isNewSession = !staticLabelsDrawn || strcmp(title, lastTitle) != 0;

  if (isNewSession)
  {
    currentFontColor  = DEFAULT_FONT_COLOR;
    currentBrightness = DEFAULT_BRIGHTNESS;
    currentFontSize   = DEFAULT_FONT_SIZE;
  }

  // Apply only the display-setting fields actually present in this
  // message. Omitted fields simply keep whatever is currently set --
  // so within a session, Node-RED only needs to send a setting once.
  if (doc.containsKey("font_color"))
  {
    currentFontColor = parseFontColor(doc["font_color"]);
  }
  if (doc.containsKey("brightness"))
  {
    int b = doc["brightness"];
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    currentBrightness = (uint8_t)b;
  }
  if (doc.containsKey("font_size"))
  {
    int fs = doc["font_size"];
    if (fs < 1) fs = 1;
    if (fs > 6) fs = 6; // beyond this will likely clip in the fixed-height value sprite
    currentFontSize = (uint8_t)fs;
  }

  lcd.setBrightness(currentBrightness);

  // Only repaint title/unit on the first reading, or if they've
  // actually changed. Otherwise every update only touches the
  // value's own area via drawValue().
  if (isNewSession || strcmp(unit, lastUnit) != 0)
  {
    drawStaticLabels(title, unit);
    strncpy(lastTitle, title, sizeof(lastTitle) - 1);
    lastTitle[sizeof(lastTitle) - 1] = '\0';
    strncpy(lastUnit, unit, sizeof(lastUnit) - 1);
    lastUnit[sizeof(lastUnit) - 1] = '\0';
    staticLabelsDrawn = true;
  }

  drawValue(value);
}

// ---------------------------------------------------------------
// 5. MQTT CALLBACK -- fires whenever a message arrives on a
//    subscribed topic. payload is NOT null-terminated, so we copy
//    it into a local buffer before handing it to renderFromJson().
// ---------------------------------------------------------------
void onMqttMessage(char* topic, byte* payload, unsigned int length)
{
  if (length >= 512) length = 511; // guard against oversized payloads
  char buf[512];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  Serial.print("[MQTT] message on '");
  Serial.print(topic);
  Serial.print("': ");
  Serial.println(buf);

  if (strcmp(topic, mqttBuzzerTopic) == 0)
  {
    handleBuzzerJson(buf);
  }
  else
  {
    renderFromJson(buf);
  }
}

// ---------------------------------------------------------------
// 6. WIFI / MQTT CONNECTION HANDLING
// ---------------------------------------------------------------
void connectWifi()
{
  Serial.print("[WiFi] connecting to ");
  Serial.println(WIFI_SSID);
  showStatus("Connecting WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("[WiFi] connected, IP: ");
  Serial.println(WiFi.localIP());

  char msg[64];
  snprintf(msg, sizeof(msg), "WiFi OK\n%s", WiFi.localIP().toString().c_str());
  showStatus(msg);
  delay(1000);
}

void connectMqtt()
{
  while (!mqttClient.connected())
  {
    Serial.print("[MQTT] connecting to ");
    Serial.print(MQTT_BROKER);
    Serial.print(":");
    Serial.print(MQTT_PORT);
    Serial.print(" as ");
    Serial.print(MQTT_CLIENT_ID);
    Serial.print(" ... ");
    showStatus("Connecting MQTT...");

    if (mqttClient.connect(MQTT_CLIENT_ID))
    {
      Serial.println("connected");
      mqttClient.subscribe(MQTT_TOPIC);
      mqttClient.subscribe(mqttBuzzerTopic);
      Serial.print("[MQTT] subscribed to ");
      Serial.print(MQTT_TOPIC);
      Serial.print(" and ");
      Serial.println(mqttBuzzerTopic);
      showStatus("MQTT connected");
      delay(1000);
    }
    else
    {
      // mqttClient.state() codes: -4 timeout, -2 connect failed
      // (wrong IP/port/unreachable), -1 disconnected, etc.
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(2000); // retry
    }
  }
}

// ---------------------------------------------------------------
// 7. SETUP / LOOP
// ---------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(200); // give Serial Monitor a moment to catch the first lines

  // Build the buzzer topic once, from MQTT_TOPIC, so it can never
  // drift out of sync with the display topic above.
  snprintf(mqttBuzzerTopic, sizeof(mqttBuzzerTopic), "%s/buzzer", MQTT_TOPIC);

  // Attach the buzzer pin to LEDC now with a placeholder frequency --
  // startBuzzerAlert() will call ledcChangeFrequency() with the real
  // frequency every time an alert actually fires.
  pinMode(BUZZER_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, 2000, LEDC_RESOLUTION_BITS);
  ledcWrite(BUZZER_PIN, 0); // silent until the first alert arrives

  lcd.init();
  lcd.setBrightness(255);

  // NOTE: my low-cost tft panel's MADCTL wiring renders text mirrored at rotation 1.
  // Rotation 5 (= 1 + 4) cancels the panel's built-in mirroring --
  // If text mirrored or upside down, try setRotation(x), where x is 0-5 until correct
  lcd.setRotation(5);

  // --- Set up the off-screen value sprite ---
  valueAreaTop = (lcd.height() / 2) - (VALUE_AREA_HEIGHT / 2);

  valueSprite.setColorDepth(16);
  if (!valueSprite.createSprite(lcd.width(), VALUE_AREA_HEIGHT))
  {
    Serial.println("[Sprite] 16-bit allocation failed, trying 8-bit...");
    valueSprite.setColorDepth(8);
    if (!valueSprite.createSprite(lcd.width(), VALUE_AREA_HEIGHT))
    {
      // Extremely unlikely on this board's RAM, but if it happens,
      // drawValue()'s pushSprite() call will simply do nothing --
      // worth knowing if the value ever stops updating visually.
      Serial.println("[Sprite] 8-bit allocation ALSO failed -- value display will not update!");
    }
  }

  renderFromJson(fakeIncomingJson); // show placeholder until first MQTT message

  connectWifi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  connectMqtt();
}

void loop()
{
  if (!mqttClient.connected())
  {
    connectMqtt();
  }
  mqttClient.loop(); // processes incoming messages, triggers onMqttMessage()
  serviceBuzzer();   // non-blocking -- advances any in-progress alert pattern
}
