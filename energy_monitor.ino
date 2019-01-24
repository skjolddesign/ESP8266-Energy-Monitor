/* About:
 *  Wifi sensor to put on the flasher in your electricity meter. 
 *  Detects flashes with LDR, and displays power on OLED display.
 *  Get updated code at https://github.com/skjolddesign/ESP8266-Energy-Monitor
 *  Sends data to Thingsboard Dashboard https://thingsboard.io.
 *  Sign up for a Free Live Demo there.
 *    
 * Energy monitor parts:
 * -ESP8266 with OLED.
 * -LDR
 * -10K resistor 
 *  
 * Wiring: 
 * VCC to LDR to A0 to 10K Resistor to GND.
 * 
 * Formula:
 * 2000 pulses(on my instrument) = 1KWh
 * 1 pulse = 0,5Wh
 * This is calculated automatically by the pulsesPerKWh variable.
 * 
 * Details:
 * sends 3 variables for each 30s period to Thingsboard:
 * pulse: pulses for the period
 * wPeriod: watt for the period
 * wh: calculated watt per hour.
 * v2 use ttgo oled 
 * 
 */

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>      // make sure to add U8g2 library and restart Arduino IDE  

//+------------------------------------------------------------------+
//| insert your values here
//+------------------------------------------------------------------+
#define ssid                ""
#define WIFI_PASSWORD       ""
#define THINGSBOARD_TOKEN   "" 
char thingsboardServer[] =  "demo.thingsboard.io"; // demo server

const int pulsesPerKWh =          2000; // see your value on electricity meter
const int analogTresholdLight =   300;  // set to 75% of analog light range(good light value).  
const int analogTresholdDark =    100;  // set to 25% of analog light range(good dark value).
const int VccGPIO =               16;   // this output is used as VCC for the LDR 

#define OLED_SDA            2     // Black TTGO=2,  White TTGO=4.
#define OLED_SCL            14    // Black TTGO=14, White TTGO=5.
#define OLED_RST            4     // Black TTGO=4,  White TTGO=16 evt. 2.



//+------------------------------------------------------------------+
//| dont touch below
//+------------------------------------------------------------------+
WiFiClient wifiClient;
PubSubClient client(wifiClient);
int status = WL_IDLE_STATUS;
unsigned long lastSend;
unsigned long lastTest;


//+------------------------------------------------------------------+
//| pulse 
//+------------------------------------------------------------------+
unsigned int pulses = 0;
unsigned int wh = 0; // calculated watt per hour
unsigned int wPeriod = 0; // watt pr update period
const int periodSeconds = 30;
bool blockPulse=false;
bool flip = false;
float WattPerPulse = 1.0; //default

//+------------------------------------------------------------------+
//| oled
//+------------------------------------------------------------------+
// see other oled modules in U8G2 library
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0,  OLED_RST, OLED_SCL, OLED_SDA); // HW pin remapping with ESP8266 HW I2C

void setup(){
  WattPerPulse = 1000.0 / pulsesPerKWh;
  pinMode(VccGPIO, OUTPUT); // LDR VCC, near A0.
  digitalWrite(VccGPIO, HIGH);
  Serial.begin(115200);
  initOled(); 
  Serial.println();
  Serial.println(F("Thingsboard energy monitor"));
  Serial.print(F("Watt per pulse "));
  Serial.println((WattPerPulse));
  delay(10);
  InitWiFi();
  client.setServer( thingsboardServer, 1883 );
  lastSend = 0;
}

void loop(){
  if ( !client.connected() ) {
    reconnect();
  }
  client.loop();
  
 testAnalogValue();
 countPulses();
 getValAndSend();

}

void testAnalogValue(){
if ( millis() - lastTest < 200 ) return; // return if not the time
 
 lastTest = millis();
 int aVal;
 aVal =analogRead(A0);
 Serial.print(F("Analog: "));
 Serial.println(aVal);
 
}


void countPulses(){
  delay(10); // prevent new pulse at trigg time
  int aVal;
  aVal =analogRead(A0);
 
 // count flashes on Energy Monitor
 if(aVal>analogTresholdLight && blockPulse==false){ //signal detected
      blockPulse=true;
      pulses++;
      flip=!flip; //flip
      Serial.print("Analog: ");
      Serial.print(aVal);
      Serial.print("  Pulse: ");
      Serial.println(pulses);
      updateOLED();
 
  }

 if(aVal<analogTresholdDark && blockPulse==true){ //signal detected
   unsigned long currentMillis = millis();
   blockPulse=false;
   }
}


void getValAndSend(){
  if ( millis() - lastSend < 1000 * periodSeconds ) return; // return if not the time
  lastSend = millis();
  
  //convert pulse to Wh. 2000 pulses = 1KWh. 1 pulse = 0,5Wh = 0,5wh pr pulse
  //multiply with 60 to get hours, cause this update is in minutes.
  //multiply with 2, cause we update two times each minute.
  wh = WattPerPulse * pulses * 60 * 2 ; // calculated watt hour for oled. 
  wPeriod = WattPerPulse * pulses; // watt minute for accumulation in Thingsboard
  //wh = (0.5) * pulses * 60 * 2 ; // calculated watt hour for oled. 
  //wPeriod = (0.5) * pulses; // watt minute for accumulation in Thingsboard
 
  updateOLED();
  
  String s1 = String(pulses);
  String s2 = String(wh);
  String s3 = String(wPeriod);
 
  // Just debug messages
  Serial.print( "Sending variables : [" );
  Serial.print( s1 ); Serial.print( "," );
  Serial.print( s3 ); Serial.print( "," );
  Serial.print( s2 );
  Serial.print( "]   -> " );

  // Prepare a JSON payload string
  String payload = "{";
  payload += "\"pulse\":"; payload += s1; payload += ",";
  payload += "\"wPeriod\":"; payload += s3; payload += ",";
  payload += "\"wh\":"; payload += s2;
  payload += "}";
 
  // Send payload
  char attributes[100];
  payload.toCharArray( attributes, 100 );
  client.publish( "v1/devices/me/telemetry", attributes );
  Serial.println( attributes );
  pulses=0; //reset pulse counter

}


void InitWiFi(){
  Serial.println("Connecting to AP ...");
  // attempt to connect to WiFi network
  WiFi.mode(WIFI_STA); // turn off access point mode
  delay(2000);

//check if sdk has autoconnected already
if (WiFi.status() == WL_CONNECTED) {
   Serial.println("Connected to AP");
   updateOLED();
   return;
}

  
  WiFi.begin(ssid, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    u8g2.clearBuffer();      // clear the internal memory
    u8g2.drawStr(0,12,"Connecting"); // write something to the internal memory
    u8g2.sendBuffer();        // transfer internal memory to the display
  }
  Serial.println("Connected to AP");
  updateOLED();
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    delay( 2000 ); //safe wait before reconnect
    status = WiFi.status();
    if ( status != WL_CONNECTED) {
       WiFi.mode(WIFI_STA); // turn off access point mode
      WiFi.begin(ssid, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
          u8g2.clearBuffer();      // clear the internal memory
          u8g2.drawStr(0,12,"Connecting"); // write something to the internal memory
          u8g2.sendBuffer();        // transfer internal memory to the display
      }
      Serial.println("Connected to AP");
      updateOLED();
    }
    Serial.print("Connecting to ThingsBoard node ...");
    // Attempt to connect (clientId, username, password)
    if ( client.connect("ESP8266 Device", THINGSBOARD_TOKEN, NULL) ) { //connect with username only(token)
      Serial.println( "[DONE]" );
    } else {
      Serial.print( "[FAILED] [ rc = " );
      Serial.print( client.state() );
      Serial.println( " : retrying in 5 seconds]" );
      // Wait 5 seconds before retrying
      delay( 5000 );
    }
  }
}


void updateOLED(){
  char buf[12];
  dtostrf(wh, 8, 0, buf); // verdi int/desimal, lengde, desimaler, char array buffer
  u8g2.clearBuffer();      // clear the internal memory
  u8g2.drawStr(0,12,"Wh"); // write something to the internal memory
  u8g2.drawStr(25,12,buf);  // write something to the internal memory
  if(flip) u8g2.drawStr(0,30,"|"); // write something to the internal memory
  else u8g2.drawStr(0,30,"--"); // write something to the internal memory
  u8g2.sendBuffer();        // transfer internal memory to the display
}

void initOled(){
  u8g2.begin();
  u8g2.clearBuffer();          // clear the internal memory
  u8g2.setFont(u8g2_font_ncenB12_tr);  // choose a suitable font
  u8g2.drawStr(0,12,"eNERGY!");  // write something to the internal memory
  u8g2.sendBuffer();          // transfer internal memory to the display
  delay(2000);
}
