#include <Arduino.h>
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "button.hpp"
#include "mt8901.hpp"
#include "ui.h"
#include <WiFi.h>
#include <HTTPClient.h>

#define GFX_BL 38
#define BUTTON_PIN 3
#define MOTOR_PIN 7
#define LED_PIN 4
#define SCREEN_TIMEOUT 60000
#define TEMP_FETCH_INTERVAL 5000       // 5 seconds
#define BOILER_STATUS_FETCH_INTERVAL 2000  // 2 seconds

void connectWiFi(void);
void checkWiFi(void);
void initScreen(void);
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
void init_lv_group(void);
void postSetTemp(float temp);
void fetchCurrentTemp(void);
void fetchBoilerStatus(void);
void updateActivityTime(void);
void checkScreenTimeout(void);
void setScreenState(bool state);
void buttonLoop(button_t * btn);

Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, /* DC */
  21,              /* CS */
  47,              /* SCK */
  41,              /* MOSI */
  GFX_NOT_DEFINED  /* MISO */
);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  39, /* DE */
  48, /* VSYNC */ 
  40, /* HSYNC */ 
  45, /* PCLK */
  10, /* R0 */
  16, /* R1 */ 
  9,  /* R2 */ 
  15, /* R3 */ 
  46, /* R4 */
  8,  /* G0 */ 
  13, /* G1 */ 
  18, /* G2 */ 
  12, /* G3 */ 
  11, /* G4 */ 
  17, /* G5 */
  47, /* B0 */ 
  41, /* B1 */ 
  0,  /* B2 */
  42, /* B3 */ 
  14, /* B4 */
  1,  /* hsync_polarity */
  10, /* hsync_front_porch */
  10, /* hsync_pulse_width */
  10, /* hsync_back_porch */
  1,  /* vsync_polarity */
  14, /* vsync_front_porch */
  2,  /* vsync_pulse_width */
  12  /* vsync_back_porch */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, /* width */
  480, /* height */
  rgbpanel, 
  0, /* rotation */
  true, /* auto_flush */
  bus, 
  GFX_NOT_DEFINED, /* RST */
  st7701_type7_init_operations, 
  sizeof(st7701_type7_init_operations)
);

enum WiFiState {
  WIFI_DISCONNECTED,
  WIFI_CONNECTING,
  WIFI_CONNECTED
};

WiFiState wifiState = WIFI_DISCONNECTED;
unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 10000; 

const char* ssid = "CHANGE";
const char* password = "CHANGE";
const char* serverIP = "192.168.4.1";


static button_t *g_btn;
static lv_color_t *disp_draw_buf;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_group_t *lv_group;

// Screen timeout in milliseconds (1 minute)
unsigned long lastActivityTime = 0;
bool screenOn = true;
bool boilerStatus = false;

void setup(void)
{
  Serial.begin(115200);
  Serial.println("Starting system");
  connectWiFi();
  initScreen();
  ui_init();

  // Initial data fetch
  fetchCurrentTemp();
  fetchBoilerStatus();
  
  // Initialize activity timer
  updateActivityTime();
}

void loop(void)
{
  static unsigned long lastTempFetchTime = 0, lastBoilerStatusFetchTime = 0;
  
  // Handle LVGL tasks
  lv_timer_handler();
  
  // Check WiFi connection
  checkWiFi();
  
  // Check for screen timeout
  checkScreenTimeout();
  
  buttonLoop(g_btn);
  // Periodically fetch temperature
  unsigned long currentMillis = millis();
  if (currentMillis - lastTempFetchTime >= TEMP_FETCH_INTERVAL) {
    lastTempFetchTime = currentMillis;
    fetchCurrentTemp();
  }
  
  // Periodically fetch boiler status
  if (currentMillis - lastBoilerStatusFetchTime >= BOILER_STATUS_FETCH_INTERVAL) {
    lastBoilerStatusFetchTime = currentMillis;
    fetchBoilerStatus();
  }
}

void checkWiFi(void)
{
  unsigned long currentMillis = millis();
  
  switch (wifiState) {
    case WIFI_DISCONNECTED:
      // Try to connect if enough time has passed since last attempt
      if (currentMillis - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
        connectWiFi();
      }
      break;
      
    case WIFI_CONNECTING:
      // Check if connected
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        wifiState = WIFI_CONNECTED;
      } 
      // Check for timeout (5 seconds)
      else if (currentMillis - lastWiFiAttempt >= 5000) {
        Serial.println("WiFi connection attempt timed out");
        wifiState = WIFI_DISCONNECTED;
        WiFi.disconnect();
      }
      break;
      
    case WIFI_CONNECTED:
      // Check if we've lost connection
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost");
        wifiState = WIFI_DISCONNECTED;
        WiFi.disconnect();
      }
      break;
  }
}

void connectWiFi(void)
{
  if (wifiState == WIFI_CONNECTING) {
    // Already trying to connect
    return;
  }

  Serial.println("Starting WiFi connection...");
  WiFi.begin(ssid, password);
  wifiState = WIFI_CONNECTING;
  lastWiFiAttempt = millis();
}

void initScreen(void)
{
  gfx->begin();
  gfx->fillScreen(BLACK);

  pinMode(GFX_BL, OUTPUT);// turns on the screen
  digitalWrite(GFX_BL, HIGH);
  pinMode(MOTOR_PIN, OUTPUT);// setup motor's pin
  pinMode(LED_PIN, OUTPUT);// turns on the screen
  digitalWrite(LED_PIN, LOW);
  // Hardware Button
  g_btn = button_attch(BUTTON_PIN, 0, 10);
  // Magnetic Encoder
  mt8901_init(5, 6);

  lv_init();
  // Must to use PSRAM
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * gfx->width() * 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if(!disp_draw_buf) 
  {
        Serial.println("LVGL disp_draw_buf allocation failed!");
  } 
  else 
  {
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, gfx->width() * 32);
    /* Initialize the display */
    lv_disp_drv_init(&disp_drv);
    
    disp_drv.hor_res = gfx->width();
    disp_drv.ver_res = gfx->height();
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
   
    /* Initialize the input device driver */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = encoder_read;
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    lv_indev_drv_register(&indev_drv);
    init_lv_group();
  }
  gfx->fillScreen(BLACK);
  Serial.println("Screen started");
}

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) 
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp);
}

// read encoder
void encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
  int16_t count_temp;
  static int16_t count_temp_last = 0;
  static int16_t temp_lvgl = DEFAULT_TEMP;

  count_temp = mt8901_get_count();
  
  if (count_temp != count_temp_last)
  {
    // Update activity time when encoder is rotated
    updateActivityTime();
    
    // Turn on screen if it was off
    if (!screenOn)
    {
      setScreenState(true);
    }
    
    int16_t dif_temp = count_temp_last - count_temp;
    //Serial.printf( "\nTemp lvgl : %02d\n" , temp_lvgl);
    //Serial.printf("dif temp : %02d. count temp : %02d\n", dif_temp, count_temp);
    temp_lvgl = min<int>(max<int>(temp_lvgl + dif_temp, MIN_TEMP), MAX_TEMP);//
    //Serial.printf( "Temp lvgl : %02d\n" , temp_lvgl);
    lv_arc_set_value(ui_ArcSetTemp , temp_lvgl);
    lv_label_set_text_fmt (ui_LabelSetTemp, "Set: %02d", temp_lvgl);

    //Set shorter arc to the front
    if (lv_arc_get_value(ui_ArcSetTemp) >= lv_arc_get_value(ui_ArcTemp))
    {
      lv_obj_move_foreground(ui_ArcTemp);
    }
    else
    {
      lv_obj_move_foreground(ui_ArcSetTemp);
    }
    count_temp_last = count_temp;
    // Send new temperature to server
    postSetTemp((float)temp_lvgl);
  }
}

// Button callback function
void buttonLoop(button_t * btn)
{
  if(button_wasPressed(btn))
  {
    // Update activity time when button is pressed
    updateActivityTime();
    
    // Turn on screen if it was off
    if (!screenOn)
    {
      setScreenState(true);
    }
  }
}

// Starts LVGL group
void init_lv_group(void)
{
  lv_group = lv_group_create();
  lv_group_set_default(lv_group);
  lv_indev_t *cur_drv = NULL;
  while ((cur_drv = lv_indev_get_next(cur_drv)))
  {
    if (cur_drv->driver->type == LV_INDEV_TYPE_ENCODER)
    {
      lv_indev_set_group(cur_drv, lv_group);
    }
  }
}

// Send temperature setting to server
void postSetTemp(float temp) 
{
  if (wifiState != WIFI_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot post temperature.");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  char serverName[50];
  snprintf(serverName, sizeof(serverName), "http://%s/setTemp", serverIP);
  http.begin(client, serverName);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "value=" + String(temp);
  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) 
  {
    Serial.print("POST response: ");
    Serial.println(http.getString());
  }
  else
  {
    Serial.print("POST error code: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
}

// Fetch current temperature from server
void fetchCurrentTemp(void)
{
  static float lastFetchTemp = DEFAULT_TEMP;
  float temp = lastFetchTemp;
  if (wifiState  != WIFI_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot fetch temperature.");
  }
  else
  {
    WiFiClient client;
    HTTPClient http;
    char serverName[50];
    // Add "?plain" to the URL to get plain text response
    snprintf(serverName, sizeof(serverName), "http://%s/Temp?plain", serverIP);
    http.begin(client, serverName);
  
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) 
    {
      String response = http.getString();
      Serial.print("Temperature from server: ");
      Serial.println(response);
      
      // Parse the temperature value
      temp = response.toFloat();
      
    }
    else
    {
      Serial.print("GET temperature error code: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  }
  // Update UI with current temperature
  lv_label_set_text_fmt(ui_LabelTemp, "Temp: %02d", (int)temp);
  lv_arc_set_value(ui_ArcTemp, (int)temp);
  
  // Set shorter arc to the front
  if (lv_arc_get_value(ui_ArcSetTemp) >= lv_arc_get_value(ui_ArcTemp))
  {
    lv_obj_move_foreground(ui_ArcTemp);
  }
  else
  {
    lv_obj_move_foreground(ui_ArcSetTemp);
  }
  lastFetchTemp = temp;
}

// Fetch boiler status from server
void fetchBoilerStatus(void)
{
  if (wifiState != WIFI_CONNECTED)
  {
    Serial.println("WiFi not connected. Cannot fetch boiler status.");
    return;
  }

  WiFiClient client;
  HTTPClient http;
  char serverName[50];
  snprintf(serverName, sizeof(serverName), "http://%s/boilerStatus?plain", serverIP);
  http.begin(client, serverName);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) 
  {
    String response = http.getString();
    Serial.print("Boiler status from server: ");
    Serial.println(response);
    
    // Parse the boiler status (true/false)
    response.toLowerCase();
    bool newBoilerStatus = (response == "true" || response == "1");
    
    // Only process changes in status
    if (boilerStatus != newBoilerStatus)
    {
      boilerStatus = newBoilerStatus;
      
      // Turn on screen if boiler is active
      if (boilerStatus && !screenOn)
      {
        setScreenState(true);
        updateActivityTime(); // Reset the screen timeout
      }
    }
  }
  else
  {
    Serial.print("GET boiler status error code: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
}

// Update the last activity timestamp
void updateActivityTime(void)
{
  lastActivityTime = millis();
}

// Check if screen should time out
void checkScreenTimeout(void)
{
  if (screenOn && !boilerStatus)
  {
    // Only check timeout if screen is on and boiler is not active
    if ((millis() - lastActivityTime) > SCREEN_TIMEOUT)
    {
      setScreenState(false);
    }
  }
}

// Control screen power state
void setScreenState(bool state)
{
  screenOn = state;
  digitalWrite(GFX_BL, state ? HIGH : LOW);
  Serial.print("Screen turned ");
  Serial.println(state ? "ON" : "OFF");
}
