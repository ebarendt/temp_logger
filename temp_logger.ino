#include <Wire.h>
#include "RTClib.h"

#include <Adafruit_MCP23008.h>
#include <LiquidCrystal.h>

#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <string.h>
#include "utility/debug.h"

#define SUCCESS_LED 9
#define ERROR_LED 8
#define aref_voltage 3.3

// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!
// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

// Use hardware SPI for the remaining pins
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
                                         SPI_CLOCK_DIVIDER); // you can change this clock speed

#define WLAN_SSID       "ssid"        // cannot be longer than 32 characters!
#define WLAN_PASS       "password"
// Security can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
#define WLAN_SECURITY   WLAN_SEC_WPA2

#define IDLE_TIMEOUT_MS  3000      // Amount of time to wait (in milliseconds) with no data 
                                   // received before closing the connection.  If you know the server
                                   // you're accessing is quick to respond, you can reduce this value.

// What page to grab!
#define WEBSITE      "servername"
#define WEBPAGE      "/readings"

int temperaturePin = 0;
LiquidCrystal lcd(0);
RTC_DS1307 rtc;

/**************************************************************************/
/*!
    @brief  Sets up the HW and the CC3000 module (called automatically
            on startup)
*/
/**************************************************************************/

void setup(void)
{
  Serial.begin(115200);
  lcd.begin(16, 2);
  Wire.begin();
  rtc.begin();
  
  pinMode(ERROR_LED, OUTPUT);
  pinMode(SUCCESS_LED, OUTPUT);
  analogReference(EXTERNAL);
  Serial.println(F("Hello, CC3000!\n")); 

  Serial.print("Free RAM: "); Serial.println(getFreeRam(), DEC);
  
  /* Initialise the module */
  Serial.println(F("\nInitializing..."));
  if (!cc3000.begin()) {
    Serial.println(F("Couldn't begin()! Check your wiring?"));
    error();
    while(1);
  }

  connectToWifi();
}

void loop(void) {
//  Serial.print("Free RAM: "); Serial.println(getFreeRam(), DEC);
  checkDHCP();
  success();
  
  float temperature = getTemperature();
  logTemperature(temperature);

//  disconnectWifi();
  delay(5000);
}

void checkDHCP() {
  /* Wait for DHCP to complete */
//  Serial.println(F("Request DHCP"));
  int dhcpTimeout = 0;
  while (!cc3000.checkDHCP()) {
    if (dhcpTimeout >= 20000) {
      Serial.println(F("DHCP Request Failed"));
      error();
      while(1);
    }
    dhcpTimeout += 100;
    delay(100);
  }  

  /* Display the IP address DNS, Gateway, etc. */  
  while (! displayConnectionDetails()) {
    error();
    delay(1000);
    digitalWrite(ERROR_LED, LOW);
  }
}

void connectToWifi() {
  Serial.print(F("\nAttempting to connect to ")); Serial.println(WLAN_SSID);
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(F("Failed!"));
    error();
    while(1);
  }
   
  Serial.println(F("Connected!"));
}

void disconnectWifi() {
  /* You need to make sure to clean up after yourself or the CC3000 can freak out */
  /* the next time your try to connect ... */
  Serial.println(F("\n\nDisconnecting"));
  cc3000.disconnect();  
}

float getTemperature() {
  float tempReading = analogRead(temperaturePin);
  float voltage = tempReading * aref_voltage;
  voltage /= 1024.0;
  
  float temperatureC = (voltage - 0.5) * 100;
  float temperatureF = temperatureC * 1.8 + 32;
  return temperatureF;
}

void logTemperature(float temperature) {
  lcd.setCursor(0, 1);
  lcd.print(temperature);
  
  DateTime now = rtc.now();
  lcd.print(" - ");
  lcd.print(now.hour(), DEC);
  lcd.print(":");
  lcd.print(now.minute(), DEC);
  lcd.print(":");
  lcd.print(now.second(), DEC);
  
  uint32_t ip = cc3000.IP2U32(192,168,1,99);
  /* Try connecting to the website.
     Note: HTTP/1.1 protocol is used to keep the server from closing the connection before all data is read.
  */
  Adafruit_CC3000_Client www = cc3000.connectTCP(ip, 3000);
  if (www.connected()) {
    www.fastrprint(F("POST /readings.json HTTP/1.0\r\n"));
    www.fastrprint(F("Host: ")); www.fastrprint(WEBSITE); www.fastrprint(F("\r\n"));
      
    char strTmp[6];
    dtostrf(temperature,5,2,strTmp);
    strTmp[5] = 0;
  
    www.println(F("Content-type: application/json"));
    www.println(F("Content-Length: 33"));
    www.println(F("Connection: close"));
    www.println();
    www.print(F("{\"reading\":{\"temperature\":"));
    www.print(strTmp);
    www.print(F("}}"));    
    www.println();
  } else {
    Serial.println(F("Connection failed"));    
    return;
  }

//  Serial.println(F("-------------------------------------"));
  
  /* Read data until either the connection is closed, or the idle timeout is reached. */ 
  unsigned long lastRead = millis();
  while (www.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
    while (www.available()) {
      char c = www.read();
//      Serial.print(c);
      lastRead = millis();
    }
  }
  www.close();
//  Serial.println(F("-------------------------------------"));
}

/**************************************************************************/
/*!
    @brief  Tries to read the IP address and other connection details
*/
/**************************************************************************/
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;
  
  if(!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println(F("Unable to retrieve the IP Address!\r\n"));
    return false;
  }
  else
  {
//    Serial.print(F("\nIP Addr: ")); cc3000.printIPdotsRev(ipAddress);
//    Serial.print(F("\nNetmask: ")); cc3000.printIPdotsRev(netmask);
//    Serial.print(F("\nGateway: ")); cc3000.printIPdotsRev(gateway);
//    Serial.print(F("\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
//    Serial.print(F("\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
//    Serial.println();
    return true;
  }
}

void error() {
  lcd.setCursor(0, 0);
  lcd.print("Error");
}

void success() {
  lcd.setCursor(0, 0);
  lcd.print("Success");
}
