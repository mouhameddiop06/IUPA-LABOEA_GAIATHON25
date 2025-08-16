#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Configuration WiFi
const char* ssid = "Mbotte";
const char* password = "Bir@ne2002";

// Configuration MLX90614
#define SDA_PIN 21
#define SCL_PIN 22
#define MLX90614_ADDR 0x5A
#define MLX90614_TA 0x06      // Température ambiante
#define MLX90614_TOBJ1 0x07   // Température objet

// Configuration capteur de luminosité
#define LIGHT_SENSOR_PIN 34   // Pin analogique pour photorésistance
#define ADC_MAX 4095          // Résolution ADC 12-bit
#define VOLTAGE_REF 3.3       // Tension de référence

WebServer server(80);

// Variables globales
float ambientTemp = 0.0;
float objectTemp = 0.0;
float estimatedPH = 7.0;
float estimatedDO = 8.0;
float lightLevel = 0.0;        // Nouveau: niveau de luminosité en lux
unsigned long lastReading = 0;
const unsigned long READING_INTERVAL = 3000;

// Page HTML
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Station Monitoring Aquatique</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
            color: white;
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        
        .header {
            text-align: center;
            padding: 30px 0;
            border-bottom: 2px solid rgba(255,255,255,0.2);
            margin-bottom: 30px;
        }
        
        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
        }
        
        .status-bar {
            display: flex;
            justify-content: space-between;
            align-items: center;
            background: rgba(255,255,255,0.1);
            padding: 15px 20px;
            border-radius: 10px;
            margin-bottom: 30px;
            backdrop-filter: blur(10px);
            flex-wrap: wrap;
        }
        
        .status-item {
            display: flex;
            align-items: center;
            margin: 5px;
        }
        
        .status-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
            animation: pulse 2s infinite;
            background: #00ff88;
        }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.6; }
        }
        
        .warning {
            background: rgba(255, 170, 0, 0.2);
            border: 1px solid rgba(255, 170, 0, 0.5);
            border-radius: 10px;
            padding: 15px;
            margin-bottom: 20px;
            text-align: center;
        }
        
        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 25px;
            margin-bottom: 30px;
        }
        
        .metric-card {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(15px);
            border-radius: 15px;
            padding: 25px;
            border: 1px solid rgba(255,255,255,0.2);
            text-align: center;
            transition: transform 0.3s ease;
        }
        
        .metric-card:hover {
            transform: translateY(-5px);
        }
        
        .metric-icon {
            font-size: 3em;
            margin-bottom: 15px;
        }
        
        .metric-title {
            font-size: 1.2em;
            font-weight: 600;
            margin-bottom: 15px;
        }
        
        .metric-value {
            font-size: 2.8em;
            font-weight: 700;
            margin: 15px 0;
        }
        
        .metric-unit {
            font-size: 0.6em;
            opacity: 0.8;
        }
        
        .metric-subtitle {
            font-size: 0.9em;
            opacity: 0.7;
            margin-top: 10px;
        }
        
        .controls {
            display: flex;
            justify-content: center;
            gap: 15px;
            margin-top: 30px;
            flex-wrap: wrap;
        }
        
        .btn {
            background: linear-gradient(45deg, #1976d2, #42a5f5);
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 25px;
            cursor: pointer;
            font-weight: 600;
            transition: all 0.3s ease;
        }
        
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(25,118,210,0.4);
        }
        
        .sensor-info {
            background: rgba(255,255,255,0.1);
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 20px;
            text-align: center;
        }
        
        @media (max-width: 768px) {
            .metrics-grid {
                grid-template-columns: 1fr;
            }
            
            .status-bar {
                flex-direction: column;
                gap: 10px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🌊 Station Monitoring Aquatique</h1>
            <p>Capteurs MLX90614 + Luminosité - Surveillance complète de l'environnement aquatique</p>
        </div>
        
        <div class="warning">
            <strong>Information:</strong> Capteur infrarouge MLX90614 et photorésistance détectés. Les valeurs pH et oxygène sont des estimations 
            basées sur la température. La luminosité est mesurée via capteur analogique.
        </div>
        
        <div class="status-bar">
            <div class="status-item">
                <div class="status-dot"></div>
                <span>Système en ligne</span>
            </div>
            <div class="status-item">
                <div class="status-dot"></div>
                <span>MLX90614 opérationnel</span>
            </div>
            <div class="status-item">
                <div class="status-dot"></div>
                <span>Capteur luminosité actif</span>
            </div>
            <div class="status-item">
                <span>IP: %DEVICE_IP%</span>
            </div>
            <div class="status-item">
                <span id="lastUpdate">Dernière MAJ: --</span>
            </div>
        </div>
        
        <div class="sensor-info">
            <div><strong>Capteurs:</strong> MLX90614 (Température IR) + Photorésistance (Luminosité)</div>
            <div><strong>Précision Temp:</strong> ±0.5°C | <strong>Plage:</strong> -70°C à +380°C | <strong>Luminosité:</strong> 0-10000 lux (estimé)</div>
        </div>
        
        <div class="metrics-grid">
            <div class="metric-card">
                <div class="metric-icon">🌡️</div>
                <div class="metric-title">Température Ambiante</div>
                <div class="metric-value">
                    <span id="ambientTemp">--</span>
                    <span class="metric-unit">°C</span>
                </div>
                <div class="metric-subtitle">Capteur MLX90614 interne</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-icon">🎯</div>
                <div class="metric-title">Température Objet/Eau</div>
                <div class="metric-value">
                    <span id="objectTemp">--</span>
                    <span class="metric-unit">°C</span>
                </div>
                <div class="metric-subtitle">Mesure infrarouge sans contact</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-icon">💡</div>
                <div class="metric-title">Luminosité</div>
                <div class="metric-value">
                    <span id="lightLevel">--</span>
                    <span class="metric-unit">lux</span>
                </div>
                <div class="metric-subtitle">Capteur analogique (estimation)</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-icon">🧪</div>
                <div class="metric-title">pH Estimé</div>
                <div class="metric-value">
                    <span id="ph">--</span>
                    <span class="metric-unit">pH</span>
                </div>
                <div class="metric-subtitle">Basé sur température (approximatif)</div>
            </div>
            
            <div class="metric-card">
                <div class="metric-icon">💨</div>
                <div class="metric-title">Oxygène Dissous</div>
                <div class="metric-value">
                    <span id="oxygen">--</span>
                    <span class="metric-unit">mg/L</span>
                </div>
                <div class="metric-subtitle">Estimation théorique</div>
            </div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshData()">🔄 Actualiser</button>
            <button class="btn" onclick="captureData()">📸 Capturer</button>
        </div>
    </div>

    <script>
        function updateData() {
            fetch('/api/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('ambientTemp').textContent = data.ambientTemp.toFixed(1);
                    document.getElementById('objectTemp').textContent = data.objectTemp.toFixed(1);
                    document.getElementById('lightLevel').textContent = Math.round(data.lightLevel);
                    document.getElementById('ph').textContent = data.ph.toFixed(1);
                    document.getElementById('oxygen').textContent = data.dissolvedO2.toFixed(1);
                    
                    document.getElementById('lastUpdate').textContent = 
                        'Dernière MAJ: ' + new Date().toLocaleTimeString('fr-FR');
                })
                .catch(error => {
                    console.error('Erreur:', error);
                });
        }
        
        function refreshData() {
            updateData();
        }
        
        function captureData() {
            const data = {
                ambient: document.getElementById('ambientTemp').textContent,
                object: document.getElementById('objectTemp').textContent,
                light: document.getElementById('lightLevel').textContent,
                ph: document.getElementById('ph').textContent,
                oxygen: document.getElementById('oxygen').textContent,
                timestamp: new Date().toLocaleString('fr-FR')
            };
            
            console.log('Données capturées:', data);
            alert('Données capturées dans la console du navigateur');
        }
        
        // Mise à jour automatique toutes les 5 secondes
        setInterval(updateData, 5000);
        
        // Première mise à jour
        updateData();
    </script>
</body>
</html>
)rawliteral";

// Lecture MLX90614
uint16_t readMLX90614(uint8_t reg) {
  Wire.beginTransmission(MLX90614_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return 0xFFFF;
  }
  
  Wire.requestFrom(MLX90614_ADDR, (uint8_t)3);
  if (Wire.available() >= 3) {
    uint8_t lowByte = Wire.read();
    uint8_t highByte = Wire.read();
    uint8_t crc = Wire.read();
    return (highByte << 8) | lowByte;
  }
  
  return 0xFFFF;
}

// Conversion température MLX90614
float mlxTempConvert(uint16_t rawTemp) {
  if (rawTemp == 0xFFFF) return -999.0;
  float tempK = rawTemp * 0.02;
  return tempK - 273.15;
}

// Lecture et conversion luminosité
float readLightLevel() {
  int analogValue = analogRead(LIGHT_SENSOR_PIN);
  
  // Conversion ADC vers voltage
  float voltage = (analogValue * VOLTAGE_REF) / ADC_MAX;
  
  // Conversion approximative vers lux (formule empirique)
  // Cette formule peut être ajustée selon votre photorésistance
  float lux = 0.0;
  
  if (voltage > 0.1) {
    // Formule approximative pour conversion en lux
    // Plus la tension est élevée, plus il y a de lumière
    lux = (voltage / VOLTAGE_REF) * 10000.0; // Échelle 0-10000 lux
    
    // Courbe logarithmique pour une meilleure représentation
    lux = pow(lux / 100.0, 1.5) * 100.0;
  }
  
  return constrain(lux, 0.0, 10000.0);
}

// Estimation pH basée sur température
float estimatePH(float temp) {
  float basePH = 7.2;
  float tempCorrection = (temp - 25.0) * (-0.015);
  return constrain(basePH + tempCorrection, 6.0, 8.5);
}

// Estimation oxygène dissous
float estimateDissolvedO2(float temp) {
  float o2_sat = 14.652 - 0.41022 * temp + 0.007991 * temp * temp - 0.000077774 * temp * temp * temp;
  return constrain(o2_sat * 0.85, 0.0, 15.0);
}

void handleRoot() {
  String page = htmlPage;
  page.replace("%DEVICE_IP%", WiFi.localIP().toString());
  server.send(200, "text/html", page);
}

void handleApiData() {
  String json = "{";
  json += "\"ambientTemp\":" + String(ambientTemp, 2) + ",";
  json += "\"objectTemp\":" + String(objectTemp, 2) + ",";
  json += "\"lightLevel\":" + String(lightLevel, 1) + ",";
  json += "\"ph\":" + String(estimatedPH, 2) + ",";
  json += "\"dissolvedO2\":" + String(estimatedDO, 2) + ",";
  json += "\"timestamp\":" + String(millis());
  json += "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void updateSensorReadings() {
  // Lecture des capteurs de température
  uint16_t rawAmbient = readMLX90614(MLX90614_TA);
  uint16_t rawObject = readMLX90614(MLX90614_TOBJ1);
  
  ambientTemp = mlxTempConvert(rawAmbient);
  objectTemp = mlxTempConvert(rawObject);
  
  // Lecture du capteur de luminosité
  lightLevel = readLightLevel();
  
  // Utiliser la température objet pour les estimations (plus représentative de l'eau)
  float referenceTemp = (objectTemp > -50) ? objectTemp : ambientTemp;
  
  estimatedPH = estimatePH(referenceTemp);
  estimatedDO = estimateDissolvedO2(referenceTemp);
  
  Serial.printf("Ambiante: %.1f°C | Objet: %.1f°C | Luminosité: %.0f lux | pH: %.1f | O2: %.1f mg/L\n", 
                ambientTemp, objectTemp, lightLevel, estimatedPH, estimatedDO);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== Station Monitoring MLX90614 + Luminosité ===");
  
  // Configuration du pin analogique pour le capteur de luminosité
  pinMode(LIGHT_SENSOR_PIN, INPUT);
  
  // Initialisation I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  
  // Connexion WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connexion WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.printf("WiFi connecté: %s\n", WiFi.localIP().toString().c_str());
  
  // Configuration serveur web
  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  
  server.begin();
  Serial.println("Serveur web démarré");
  Serial.printf("Interface: http://%s\n", WiFi.localIP().toString().c_str());
  
  // Première lecture
  updateSensorReadings();
}

void loop() {
  server.handleClient();
  
  if (millis() - lastReading >= READING_INTERVAL) {
    updateSensorReadings();
    lastReading = millis();
  }
  
  delay(50);
}
