#include "esp_camera.h"
#include "WiFi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"

// Configuration WiFi
const char* ssid = "Mbotte";
const char* password = "Bir@ne2002";

httpd_handle_t camera_httpd = NULL;

// Pins ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Handler streaming (identique à celui qui marchait)
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  char part_buf[128];

  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while(true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Capture failed");
      break;
    }

    httpd_resp_send_chunk(req, "\r\n--frame\r\n", 9);
    size_t hlen = snprintf(part_buf, 128, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    httpd_resp_send_chunk(req, part_buf, hlen);
    httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    
    // Framerate un peu amélioré : 150ms = ~6-7 FPS
    vTaskDelay(150 / portTICK_PERIOD_MS);
  }
  return ESP_OK;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== ESP32-CAM Surveillance Piscicole ===");

  // WiFi (même config qui marchait)
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✓ WiFi connecté: %s\n", WiFi.localIP().toString().c_str());

  // Configuration caméra IDENTIQUE à celle qui marchait
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Configuration EXACTEMENT comme celle qui marchait
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;      // On garde UXGA mais...
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    Serial.println("Mode PSRAM: Haute résolution");
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("Mode DRAM: Résolution standard");
  }

  Serial.println("Initialisation caméra...");
  esp_err_t err = esp_camera_init(&config);
  
  if (err != ESP_OK) {
    Serial.printf("✗ Erreur: 0x%x\n", err);
    return;
  }
  
  Serial.println("✓ Caméra initialisée!");

  // Configuration capteur EXACTEMENT comme celle qui marchait
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    Serial.printf("Capteur détecté - PID: 0x%02X\n", s->id.PID);
    
    if (s->id.PID == 0x26) {
      Serial.println("→ Capteur: OV2640");
      // Réduire à VGA pour éviter DMA overflow
      s->set_framesize(s, FRAMESIZE_VGA);  // 640x480 au lieu d'UXGA
    } else if (s->id.PID == 0x36) {
      Serial.println("→ Capteur: OV3660");
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, -2);
      s->set_framesize(s, FRAMESIZE_VGA);  // 640x480
    } else {
      Serial.printf("→ Capteur inconnu: 0x%02X\n", s->id.PID);
      s->set_framesize(s, FRAMESIZE_VGA);
    }
    
    // Réglages conservateurs pour éviter DMA overflow
    s->set_quality(s, 12);  // Qualité 12 au lieu de 4
  }

  // Test capture
  Serial.println("Test capture...");
  delay(2000);
  
  camera_fb_t * fb = esp_camera_fb_get();
  if (fb) {
    Serial.printf("✓ Capture OK: %dx%d, %u bytes\n", fb->width, fb->height, fb->len);
    esp_camera_fb_return(fb);
    
    // Démarrer serveur (config simple)
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = 80;

    httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL
    };

    if (httpd_start(&camera_httpd, &server_config) == ESP_OK) {
      httpd_register_uri_handler(camera_httpd, &stream_uri);
      
      Serial.println("\n=== STREAMING ACTIF ===");
      Serial.printf("URL: http://%s/stream\n", WiFi.localIP().toString().c_str());
      Serial.println("Résolution: 640x480 (VGA)");
      Serial.println("Framerate: ~7 FPS");
      Serial.println("Prêt pour surveillance!");
    }
    
  } else {
    Serial.println("✗ Capture échouée");
  }
}

void loop() {
  delay(30000);
  Serial.printf("[%lu] Surveillance active - Heap: %d\n", 
                millis()/1000, ESP.getFreeHeap());
}