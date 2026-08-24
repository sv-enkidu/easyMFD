/* Phase 2: SignalK TFT instrument display -- live over MQTT
 * -----------------------------------------------------------------
 * Rendering is split into drawStaticLabels() (title + unit, drawn
 * once and left alone) and drawValue() (redrawn on every update).
 * drawValue() composes the new number into an off-screen sprite in
 * RAM, then pushes the finished result to the panel in one single
 * transfer -- this eliminates the visible flash from the old
 * clear-then-draw-directly-on-panel approach. We subscribe to an
 * MQTT topic and call renderFromJson() every time a new payload
 * arrives.
 *
 * Expected JSON payload (published by Node-RED):
 * {
 *   "title": "Outside Temperature",
 *   "value": "67.5",
 *   "unit": "F",
 *   "font_color": "yellow",   // optional, see parseFontColor()
 *   "brightness": 200,        // optional, 0-255
 *   "font_size": 4            // optional, 1-6
 * }
 *
 * Display-setting persistence model:
 *   - "title" changing = a new data source/session. Settings reset
 *     to firmware defaults, THEN any overrides in this same message
 *     are applied on top.
 *   - "title" unchanged = same session. Only fields actually present
 *     in the message update their setting; anything omitted keeps
 *     whatever is currently active. So Node-RED only needs to send
 *     font_color/brightness/font_size once per session, not on every
 *     value update.
 *   - font_color applies to the big value number only (title/unit
 *     stay a fixed neutral white for legibility).
 *
 * Topic convention: JSON lives in the PAYLOAD. Topic is a plain
 * routing string, e.g. boat/display/outside_temp -- set that same
 * string in MQTT_TOPIC below.
 * 
 * Send data from NodRed using MQTT out node to send to IP and port of Mosquitto server
 * Set path (topic) and payload (actual data displayed on tft) upstream of MQTT out
 * node (for example, with inject node for static content, or using 
 *
 * Libraries needed (Arduino IDE Library Manager):
 *   - LovyanGFX
 *   - ArduinoJson (v6.x)
 *   - PubSubClient (by Nick O'Leary)
 *
 * Pinout / panel config below is copied unchanged from your working
 * test sketch (Lonely Binary ESP32-S3 + ILI9486).
 */

#include <LovyanGFX.hpp>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------
// 0. WIFI / MQTT CONFIG -- wifi network + broker details
// ---------------------------------------------------------------
const char* WIFI_SSID     = "routerSSID";
const char* WIFI_PASSWORD = "routerPW";

const char* MQTT_BROKER   = "routerIPAddress";   // your Mosquitto host IP
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "helm"; // one topic per display
const char* MQTT_CLIENT_ID= "esp32-display-helm"; // must be unique per device on the broker

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
// 1. LGFX CONFIG CLASS -- reusable display config class code
// ---------------------------------------------------------------
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9486 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:
  LGFX(void)
  {
    { // SPI Bus Configuration
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 24000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 13;
      cfg.pin_mosi = 12;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 2;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel Configuration
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 10;
      cfg.pin_rst          = 4;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      _panel_instance.config(cfg);
    }

    { // Backlight Configuration
      auto cfg = _light_instance.config();
      cfg.pin_bl = 41;
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

  renderFromJson(buf);
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
      Serial.print("[MQTT] subscribed to ");
      Serial.println(MQTT_TOPIC);
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
}
