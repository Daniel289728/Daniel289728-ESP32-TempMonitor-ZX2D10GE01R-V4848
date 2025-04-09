#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include "button.hpp"
#include "mt8901.hpp"
#include "ui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>
#include "time.h"
#include <Preferences.h>
#include "ESPAsyncWebServer.h"

const char* ssid = "SSID";
const char* password = "PASS";

AsyncWebServer server(80);

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
#define GFX_BL 38

#define BUTTON_PIN 3
#define MOTOR_PIN 7
#define LED_PIN 4
#define NUM_LEDS 13


//CRGB leds[NUM_LEDS];

static button_t *g_btn;
static lv_color_t *disp_draw_buf;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_group_t *lv_group;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data);
void initScreen(void);
void init_lv_group();
void handleSetTemp(AsyncWebServerRequest *request);
void handleTemperature(AsyncWebServerRequest *request);
void postToSetTemp(const String& value);
void configureOTA(void);

void setup(void)
{
  Serial.begin(115200);
  Serial.println("System start");
  configureOTA();
  Serial.println("OTA configured");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  server.on("/Temp", HTTP_ANY, handleTemperature);
  server.on("/setTemp", HTTP_ANY, handleSetTemp);
  server.begin();
  Serial.println("Server started");

  initScreen();
  ui_init();
}

void loop(void)
{
  lv_timer_handler();
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
    // Enviar nueva temperatura al servidor
    postToSetTemp(String(temp_lvgl));
  }
  
}

// starts lvgls group
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

void handleTemperature(AsyncWebServerRequest *request) 
{
  // For GET requests (browser access)
  if (request->method() == HTTP_GET) {
    int currentTemp = lv_arc_get_value(ui_ArcTemp);
    
    // Create a simple HTML page that displays the current temperature
    String html = "<html><body>";
    html += "<h1>Current Temperature: " + String(currentTemp) + "</h1>";
    html += "</body></html>";
    
    request->send(200, "text/html", html);
  }
  // For POST requests (from clients)
  else if (request->method() == HTTP_POST) {
    if (request->hasParam("value", true)) {
      String tempValue = request->getParam("value", true)->value();
      Serial.println("Received Temperature: " + tempValue);
      
      // Update the temperature display
      int temp = tempValue.toInt();
      lv_arc_set_value(ui_ArcTemp, temp);
      lv_label_set_text_fmt(ui_LabelTemp, "Temp: %02d", temp);
      
      // Update display foreground/background based on temps
      if (lv_arc_get_value(ui_ArcSetTemp) >= lv_arc_get_value(ui_ArcTemp)) {
        lv_obj_move_foreground(ui_ArcTemp);
      } else {
        lv_obj_move_foreground(ui_ArcSetTemp);
      }
      
      request->send(200, "text/plain", "Temperature received");
    } else {
      request->send(400, "text/plain", "Bad Request: No temperature value found");
    }
  }
  // For other HTTP methods
  else {
    request->send(405, "Method Not Allowed");
  }
}


void handleSetTemp(AsyncWebServerRequest *request) {
  // For GET requests (browser access)
  if (request->method() == HTTP_GET) {
    int setTemp = lv_arc_get_value(ui_ArcSetTemp);
    
    // Create a simple HTML page that displays the current set temperature
    String html = "<html><body>";
    html += "<h1>Current Set Temperature: " + String(setTemp) + "</h1>";
    html += "</body></html>";
    
    request->send(200, "text/html", html);
  }
  // For POST requests (from encoder updates)
  else if (request->method() == HTTP_POST) {
    if (request->hasParam("value", true)) {
      String tempValue = request->getParam("value", true)->value();
      Serial.println("Received Set Temperature: " + tempValue);
      // Process the set temperature here
      request->send(200, "text/plain", "Set temperature received");
    } else {
      request->send(400, "text/plain", "Bad Request: No temperature value found");
    }
  }
  // For other HTTP methods
  else {
    request->send(405, "Method Not Allowed");
  }
}

void postToSetTemp(const String& value) 
{
  HTTPClient http;
  http.begin("http://192.168.4.1/setTemp"); // Cambia IP o dominio si es necesario
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "value=" + value;
  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) {
    Serial.printf("[POST /setTemp] Código de respuesta: %d\n", httpResponseCode);
    String response = http.getString();
    Serial.println("Respuesta: " + response);
  } else {
    Serial.printf("[POST /setTemp] Error en la petición: %s\n", http.errorToString(httpResponseCode).c_str());
  }

  http.end();
}

void configureOTA(void)
{
  //Access point and station
  WiFi.mode(WIFI_AP_STA);

}