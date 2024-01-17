#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#include <ESP8266WiFi.h>
#include <espnow.h>

#define MY_NAME "CONTROLLER_NODE"
#define MY_ROLE ESP_NOW_ROLE_CONTROLLER   // set the role of this device: CONTROLLER, SLAVE, COMBO
#define RECEIVER_ROLE ESP_NOW_ROLE_SLAVE  // set the role of the receiver
#define WIFI_CHANNEL 1

//uint8_t receiverAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // please update this with the MAC address of the receiver
uint8_t receiverAddress[] = { 0x44, 0x17, 0x93, 0x11, 0x21, 0x0D };

const unsigned int MAX_MESSAGE_LENGTH = 10;
const unsigned int MAX_SERIAL_BUFFER_BYTES = 512;
const char STARTING_BYTE = 255;

struct __attribute__((packed)) dataPacket {
  int unsigned Right_Score;
  int unsigned Left_Score;
  int unsigned Seconds_Remaining;
  int unsigned Minutes_Remaining;
  bool Green_Light;
  bool Red_Light;
  bool White_Green_Light;
  bool White_Red_Light;
  bool Yellow_Green_Light;
  bool Yellow_Red_Light;
  bool Yellow_Card_Green;
  bool Yellow_Card_Red;
  bool Red_Card_Green;
  bool Red_Card_Red;
  bool Priority_Left;
  bool Priority_Right;
};

// Initializes the packet
dataPacket packet;

// Initializes the previous data packet
dataPacket packet_prev;

// Shows if new data is available for display
bool new_data = false;

// Initializes Message_Position
unsigned int message_pos = 0;

// Network settings
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
unsigned int localPort = 50101;

unsigned long previousMillis = 0;
unsigned long previousMillis_timer = 0;
unsigned long previousMillis_wdt = 0;

EthernetUDP udp;

int minutes, seconds;
bool running, redLight, greenLight, redWhiteLight, greenWhiteLight;
int rightScore, leftScore;
bool yellowCardRight, redCardRight, yellowCardLeft, redCardLeft;
int priority;

void parseTimePacket(char* packetBuffer) {
  // Time Packet
  // minutes = (packetBuffer[4] - '0') * 10 + (packetBuffer[5] - '0');
  packet.Minutes_Remaining = (packetBuffer[5] - '0');
  packet.Seconds_Remaining = (packetBuffer[7] - '0') * 10 + (packetBuffer[8] - '0');
}

bool areDataPacketsIdentical(const dataPacket& packet1, const dataPacket& packet2) {
  return memcmp(&packet1, &packet2, sizeof(dataPacket)) == 0;
}

void parseLightsPacket(char* packetBuffer) {
  // Lights Packet
  running = (packetBuffer[2] == 'N') ? false : true;
  packet.Red_Light = (packetBuffer[3] - '0') == 1;
  packet.Green_Light = (packetBuffer[5] - '0') == 1;
  packet.White_Red_Light = (packetBuffer[7] - '0') == 1;
  packet.White_Green_Light = (packetBuffer[9] - '0') == 1;
}

void parseScorePacket(char* packetBuffer) {
  // Score Packet
  packet.Right_Score = (packetBuffer[4] - '0') * 10 + (packetBuffer[5] - '0');
  packet.Left_Score = (packetBuffer[7] - '0') * 10 + (packetBuffer[8] - '0');

  packet.Yellow_Card_Green = (packetBuffer[11] - '0') == 1;
  packet.Red_Card_Green = (packetBuffer[13] - '0') == 1;

  packet.Yellow_Card_Red = (packetBuffer[17] - '0') == 1;
  packet.Red_Card_Red = (packetBuffer[19] - '0') == 1;

  priority = atoi(&packetBuffer[25]);

  if (priority == 0) {
    packet.Priority_Left = 0;
    packet.Priority_Right = 0;
  } else if (priority == 1) {
    packet.Priority_Left = 0;
    packet.Priority_Right = 1;
  } else if (priority == 2) {
    packet.Priority_Left = 1;
    packet.Priority_Right = 0;
  }
}

void transmissionComplete(uint8_t* receiver_mac, uint8_t transmissionStatus) {
  if (transmissionStatus == 0) {
    Serial.println("Data sent successfully");
  } else {
    Serial.print("Error code: ");
    Serial.println(transmissionStatus);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Connecting to Ethernet...");

  // Serial.setRxBufferSize(1024);
  // Serial.begin(2400);  // initialize serial port
  delay(10);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.print("Initializing...");
  Serial.println(MY_NAME);
  Serial.print("My MAC address is: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // we do not want to connect to a WiFi network

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }

  // Initializes the Wifi Connection
  esp_now_set_self_role(MY_ROLE);
  esp_now_register_send_cb(transmissionComplete);  // this function will get called once all data is sent
  esp_now_add_peer(receiverAddress, RECEIVER_ROLE, WIFI_CHANNEL, NULL, 0);
  Serial.println("Wifi Initialized.");

  // Initializes the Ethernet Connection
  Ethernet.begin(mac);
  Serial.println("Ethernet connected");
  Serial.print("IP address: ");
  Serial.println(Ethernet.localIP());
  udp.begin(localPort);
  Serial.println("UDP initialized");
}

void loop() {

  if (millis() - previousMillis_wdt >= 200) {
    wdt_reset();
    previousMillis_wdt = millis();
  }

  if (millis() - previousMillis_timer >= 1000) {
    Serial.print(".");
    previousMillis_timer = millis();
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int bytesRead = udp.read(packetBuffer, sizeof(packetBuffer));
    packetBuffer[bytesRead] = '\0';  // Null-terminate the received data

    // Identify packet type based on packet size
    if (packetSize == 10) {
      parseTimePacket(packetBuffer);
    } else if (packetSize == 11) {
      parseLightsPacket(packetBuffer);
    } else if (packetSize == 29) {
      parseScorePacket(packetBuffer);
    }
  }
  
  // Checks if the data has been updated
  if (areDataPacketsIdentical(packet, packet_prev) == 0) {
    new_data = true;
  }

  // Sets Previous Packet to Current Packet
  packet_prev = packet;

  if (new_data == true) {
    esp_now_send(receiverAddress, (uint8_t*)&packet, sizeof(packet));
    Serial.println("Sending New Data.");
    // Sets New Data to False
    new_data = false;
  }
}
