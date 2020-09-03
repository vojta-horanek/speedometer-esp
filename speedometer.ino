#include <ESP8266WiFi.h>
#include <uMQTTBroker.h>

const int WHEEL_DIAMETER_EEPROM_ADDR = 0;
const int PIN_MAGNET_DETECTOR = 0;
const unsigned long UPDATE_INTERVAL = 1000;

struct Speedometer {
  float speed; // in m/s
  float distance; // in m
  float wheel_diameter; // in cm
};

Speedometer spdm = { 0.0, 0.0, 215.5 };
bool measuring = true;

/*

   Channels:
   client device -> esp => config/{control, wheel_diameter}
   eps -> client device => speedometer/{speed, distance}
   esp -> client device => status/{error}
*/

class myMQTTBroker: public uMQTTBroker
{
  public:
    virtual void onData(String topic, const char *data, uint32_t length) {
      if (topic == "config/wheel_diameter" && length == sizeof(float)) {

        char diameter[length + 1];
        os_memcpy(diameter, data, length);
        diameter[length] = '\0';

        spdm.wheel_diameter = atof(diameter);

      } else if (topic == "config/control") {

        char control[length + 1];
        os_memcpy(control, data, length);
        control[length] = '\0';

        if (strcmp(control, "start") == 0) {
          if (spdm.wheel_diameter == 0.0 || isnan(spdm.wheel_diameter)) {
            this->publish("status/error", "wheel_diameter");
          } else {
            measuring = true;
          }
        }
        else if (strcmp(control, "stop") == 0) {
          measuring = false;
        }
      }

    }
};

myMQTTBroker mqtt;

unsigned long last_rotation_time = 0;
unsigned long last_sent_info_time = 0;
int magnet_pin_last_state = 1;
int last_client_count = 0;

void setup()
{
  // TODO Remove for release
  Serial.begin(9600);

  pinMode(PIN_MAGNET_DETECTOR, INPUT);

  if (isnan(spdm.wheel_diameter)) {
    spdm.wheel_diameter = 0.0;
  }

  WiFi.softAPConfig(
    IPAddress(192, 168, 1, 1),
    IPAddress(192, 168, 1, 1),
    IPAddress(255, 255, 255, 0)
  );

  WiFi.softAP("Speedometer FF-00", "", 1, false, 1);

  mqtt.init();
  mqtt.subscribe("#");


}

void loop()
{
  int current_client_count = WiFi.softAPgetStationNum();
  if (measuring) {
    int magnet_pin_state = digitalRead(PIN_MAGNET_DETECTOR);
    // If changed and if currently near magnet (on)

    if (magnet_pin_state != magnet_pin_last_state && !magnet_pin_state) {
      unsigned long current_time = millis();
      unsigned long cycle_duration = current_time - last_rotation_time;
      last_rotation_time = current_time;
      spdm.distance += spdm.wheel_diameter / 100.0f;

      float rpm = 60.0f / ((float)cycle_duration / 1000.0f);
      spdm.speed = (spdm.wheel_diameter / 100.0f) * (PI / 60.0f) * rpm * 3.6;

    } else if (millis() - last_rotation_time > 2000) {
      spdm.speed = 0.0f;
    }
    magnet_pin_last_state = magnet_pin_state;


    if (millis() - last_sent_info_time >= UPDATE_INTERVAL && current_client_count > 0) {
      mqtt.publish("speedometer/update",
                   "{"
                   "\"speed\":" + String(spdm.speed) + ","
                   "\"distance\":" + String(spdm.distance) +
                   "}");

      last_sent_info_time = millis();
    }
  }

  // If client has disconnected
  if (last_client_count > current_client_count) {
    mqtt.cleanupClientConnections();
  }
  last_client_count = current_client_count;
}
