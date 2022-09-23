#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include "secrets.h"

//60 seconds WDT
#define WDT_TIMEOUT 60

#define DATAREQ 22
#define DSMR_IN 16

// Replace the next variables with your SSID/Password combination
const char* ssid = WIFI_SSID;
const char* password = PASSWORD;

// Add your MQTT Broker IP address, example:
const char* mqtt_server = MQTT_SERVER_IP;

uint64_t chipid;
char chipID[13];
char topic[30];

char* values_of_interest[]={
  "1.7.0",
  "1.8.1",
  "1.8.2",
  "2.7.0",
  "2.8.1",
  "2.8.2",
  "21.7.0",
  "41.7.0",
  "61.7.0",
  "24.2.1"
};

char* parameters_of_interest[]={
  "actual_power_p_plus",
  "delivered_to_tariff_1",
  "delivered_to_tariff_2",
  "actual_power_p_minus",
  "delivered_by_tariff_1",
  "delivered_by_tariff_2",
  "active_power_l1",
  "active_power_l2",
  "active_power_l3",
  "gas_delivered_m3"
};

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setBufferSize(1000);

  // Setup serial for DSMR
  Serial2.begin(115200, SERIAL_8N1, 16, 17, true);

  pinMode(DATAREQ, OUTPUT);
  digitalWrite(DATAREQ, LOW);
  pinMode(DSMR_IN, INPUT_PULLUP);
  
  esp_task_wdt_reset();

  // Get chip ID
  chipid = ESP.getEfuseMac();//The chip ID is essentially its MAC address(length: 6 bytes).
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* message, unsigned int length) {
  
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    sprintf(chipID, "%04X%08X", (uint16_t)(chipid>>32), (uint32_t)chipid);
    sprintf(topic, "energy/%s", chipID);
    
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(chipID)) {
      Serial.println("connected");
      Serial.println("Topic:");
      Serial.println(topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

int Messagereceived = 0;
int Bufferpointer = 0;
char messagebuffer[2000];

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  while(Serial2.available() && Bufferpointer < 1999){    
    messagebuffer[Bufferpointer++] = Serial2.read();
    if(messagebuffer[Bufferpointer - 1] == '!' | Messagereceived){
      Messagereceived++;
      break;
    }
  }

  if(Messagereceived > 4){
    messagebuffer[Bufferpointer++] = 0;
    Serial.printf("Got message\n");
    Serial.flush();

    String json = String("{");
    // Match values
    String DSMR_string = String(messagebuffer);
    Serial.print(DSMR_string);
    int hits = 0;
    for(int i = 0; i < sizeof(values_of_interest)/sizeof(values_of_interest[0]); i++){
      int index = DSMR_string.indexOf(values_of_interest[i]);
      if(index > 0){
        if (hits++ > 0){
          json += ',';
        }
        if(i == 9){
          while(DSMR_string[index] != '('){
            index++;
          }
          index++;
          while(DSMR_string[index] != '('){
            index++;
          }
          index++;
          int index2 = index;
          while(DSMR_string[index2] != '*'){
            index2++;
          }
          DSMR_string[index2] = 0;
          json += '"';
          json += parameters_of_interest[i];
          json += "\":";
          json += String(&DSMR_string[index]).toFloat();
          DSMR_string[index2] = '*';
          
        } else {
          while(DSMR_string[index] != '('){
            index++;
          }
          index++;
          int index2 = index;
          while(DSMR_string[index2] != '*'){
            index2++;
          }
          DSMR_string[index2] = 0;
          json += '"';
          json += parameters_of_interest[i];
          json += "\":";
          json += String(&DSMR_string[index]).toFloat();
          DSMR_string[index2] = '*';
        }
      }
    }
    json += '}';
    Serial.print(json);
    char json_buf[json.length() + 2];
    json.toCharArray(json_buf, json.length() +1);
    bool res = client.publish(topic, json_buf);
    if (res){
      esp_task_wdt_reset();
      Serial.println(" Sent!");
    } else {
      Serial.println(" Error!");
    }
    
    delay(10000);
    while(Serial2.available()){
      Serial2.read();
    }
    while(Serial2.read() != '/'){            
    }
    Bufferpointer = 0;
    for(int i = 0; i < sizeof(messagebuffer); i++){
      messagebuffer[i] = 0;
    }
  
    Bufferpointer = 0;
    Messagereceived = 0;
  }
}
