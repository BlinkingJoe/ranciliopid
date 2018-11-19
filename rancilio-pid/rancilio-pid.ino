/********************************************************
   Version 1.1.0
******************************************************/
#include "Arduino.h"
unsigned long previousMillisColdstart = 0;
unsigned long previousMillisColdstartPause = 0;
unsigned long ColdstartPause = 0;
unsigned long KaltstartPause = 0;
unsigned long bruehvorganggestartet = 0;
unsigned long warmstart = 0;
unsigned long previousMillisSwing = 0;
double Onoff = 0 ;

/********************************************************
   Vorab-Konfig
******************************************************/
int Display = 1;    // 1=U8x8libm, 0=Deaktiviert, 2=Externes 128x64 Display
int OnlyPID = 1;    // 1=Nur PID ohne Preinfussion, 0=PID + Preinfussion
int TempSensor = 2; // 1=DS19B20; 2=TSIC306

char auth[] = "";
char ssid[] = "";
char pass[] = "";
/********************************************************
   BLYNK
******************************************************/
#define BLYNK_PRINT Serial
#include <BlynkSimpleEsp8266.h>

//Zeitserver
#include <TimeLib.h>
#include <WidgetRTC.h>
BlynkTimer timer;
WidgetRTC rtc;
WidgetBridge bridge1(V1);

//Update Intervall zur App
unsigned long previousMillis = 0;
const long interval = 5000;

/********************************************************
   Analog Schalter Read
******************************************************/
int analogPin = 0;
int brewcounter = 0;
int brewswitch = 0;


int brewtime = 25;
long aktuelleZeit = 0;
int totalbrewtime = 0;
int preinfusion = 2;
int preinfusionpause = 5;

#define pinRelayVentil    12
#define pinRelayPumpe     13

/********************************************************
   DISPLAY
******************************************************/

#include <U8x8lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
U8X8_SSD1306_128X32_UNIVISION_SW_I2C u8x8(/* clock=*/ 5, /* data=*/ 4, /* reset=*/ 16);   //Display initalisieren

// Display 128x64
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 16
Adafruit_SSD1306 display(OLED_RESET);
#define XPOS 0
#define YPOS 1
#define DELTAY 2


#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif


/********************************************************
   PID
******************************************************/
#include "PID_v1.h"
#define pinRelayHeater    14

int boilerPower = 1000; // Watts
float boilerVolume = 300; // Grams

unsigned int windowSize = 1000;
unsigned long windowStartTime;
double acceleration = 1;
double setPoint, Input, Output, Input2, setPointTemp, Coldstart;

double aggKp = 17.5 / acceleration;
double aggKi = 0.14 / acceleration;
double aggKd = 10 / acceleration;
double startKp = 0;
double starttemp = 90;

PID bPID(&Input, &Output, &setPoint, aggKp, aggKi, aggKd, DIRECT);

/********************************************************
   DALLAS TEMP
******************************************************/
// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>
// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 2
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

/********************************************************
   B+B Sensors TSIC 306
******************************************************/
#include "TSIC.h"       // include the library
TSIC Sensor1(2);    // only Signalpin, VCCpin unused by default
uint16_t temperature = 0;
float Temperatur_C = 0;

/********************************************************
   BLYNK WERTE EINLESEN
******************************************************/

//beim starten soll einmalig der gespeicherte Wert aus dem EEPROM 0 in die virtelle
//Variable geschrieben werden
BLYNK_CONNECTED() {
  Blynk.syncAll();
  rtc.begin();
}


BLYNK_WRITE(V4) {
  aggKp = param.asDouble();
}

BLYNK_WRITE(V5) {
  aggKi = param.asDouble();
}
BLYNK_WRITE(V6) {
  aggKd =  param.asDouble();
}

BLYNK_WRITE(V7) {
  setPoint = param.asDouble();
}

BLYNK_WRITE(V8) {
  brewtime = param.asDouble() * 1000;
}

BLYNK_WRITE(V9) {
  preinfusion = param.asDouble() * 1000;
}

BLYNK_WRITE(V10) {
  preinfusionpause = param.asDouble() * 1000;
}
BLYNK_WRITE(V11) {
  startKp = param.asDouble();
}
BLYNK_WRITE(V12) {
  starttemp = param.asDouble();
}
BLYNK_WRITE(V13)
{
  Onoff = param.asInt();
}



void setup() {

  Serial.begin(115200);

  if (Display == 1) {
    /********************************************************
      DISPLAY Intern
    ******************************************************/
    u8x8.begin();
    u8x8.setPowerSave(0);
  }
  if (Display == 2) {
    /********************************************************
      DISPLAY 128x64
    ******************************************************/
    display.begin(SSD1306_SWITCHCAPVCC, 0x3D);  // initialize with the I2C addr 0x3D (for the 128x64)
    display.clearDisplay();
  }

  /********************************************************
    BrewKnopf SSR Relais
  ******************************************************/
  pinMode(pinRelayVentil, OUTPUT);
  pinMode(pinRelayPumpe, OUTPUT);

  /********************************************************
     BLYNK
  ******************************************************/

  Blynk.begin(auth, ssid, pass, "blynk.remoteapp.de", 8080);

  pinMode(pinRelayHeater, OUTPUT);

  windowStartTime = millis();
  Coldstart = 1;

  setPointTemp = setPoint;
  bPID.SetSampleTime(windowSize);
  bPID.SetOutputLimits(0, windowSize);
  bPID.SetMode(AUTOMATIC);

  /********************************************************
     TEMP SENSOR
  ******************************************************/
  if (TempSensor == 1) {
    sensors.begin();
    sensors.requestTemperatures();
  }
}

void loop() {

  /********************************************************
    Temp. Request
  ******************************************************/
  if (TempSensor == 1) {
    sensors.requestTemperatures();
    Input = sensors.getTempCByIndex(0);
  }
  if (TempSensor == 2) {
    temperature = 0;
    Sensor1.getTemperature(&temperature);
    Temperatur_C = Sensor1.calc_Celsius(&temperature);
    Input = Temperatur_C;
    //Serial.print(Temperatur_C);
  }
  //Serial.println(Input);



  /********************************************************
    PreInfusion
  ******************************************************/

  brewswitch = analogRead(analogPin);

  unsigned long startZeit = millis();
  if (OnlyPID == 0) {
    if (brewswitch > 1000 && startZeit - aktuelleZeit > totalbrewtime && brewcounter == 0) {
      aktuelleZeit = millis();
      brewcounter = brewcounter + 1;
    }

    totalbrewtime = preinfusion + preinfusionpause + brewtime;
    //Serial.println(brewcounter);
    if (brewswitch > 1000 && startZeit - aktuelleZeit < totalbrewtime && brewcounter >= 1) {
      if (startZeit - aktuelleZeit < preinfusion) {
        // Serial.println("preinfusion");
        digitalWrite(pinRelayVentil, LOW);
        digitalWrite(pinRelayPumpe, LOW);
        //digitalWrite(pinRelayHeater, HIGH);
      }
      if (startZeit - aktuelleZeit > preinfusion && startZeit - aktuelleZeit < preinfusion + preinfusionpause) {
        //Serial.println("Pause");
        digitalWrite(pinRelayVentil, LOW);
        digitalWrite(pinRelayPumpe, HIGH);
        //digitalWrite(pinRelayHeater, HIGH);
      }
      if (startZeit - aktuelleZeit > preinfusion + preinfusionpause) {
        // Serial.println("Brew");
        digitalWrite(pinRelayVentil, LOW);
        digitalWrite(pinRelayPumpe, LOW);
        digitalWrite(pinRelayHeater, LOW);
      }


    } else {
      digitalWrite(pinRelayVentil, HIGH);
      digitalWrite(pinRelayPumpe, HIGH);
      //Serial.println("aus");
    }

    if (brewswitch < 1000 && brewcounter >= 1) {
      brewcounter = 0;
      aktuelleZeit = 0;
    }
  }

  //Sicherheitsabfrage
  if (Input >= 0) {
    if (Onoff == 1) {

      /********************************************************
        PID
      ******************************************************/
      bPID.Compute();
      if (millis() - windowStartTime > windowSize) {

        if (Input < starttemp) {
          bPID.SetTunings(aggKp, startKp, aggKd);
        } else {
          bPID.SetTunings(aggKp, aggKi, aggKd);
        }
        windowStartTime += windowSize;
      }
      if (Output < millis() - windowStartTime) {
        digitalWrite(pinRelayHeater, LOW);
        //Serial.println("Power off!");
      }
    } else {
      digitalWrite(pinRelayHeater, HIGH);
      //Serial.println("Power on!");
    }



    /********************************************************
      Sendet Daten zur App
    ******************************************************/
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {

      Blynk.run();

      previousMillis = currentMillis;
      Blynk.virtualWrite(V2, Input);
      Blynk.syncVirtual(V2);
      // Send date to the App
      Blynk.virtualWrite(V3, setPoint);
      Blynk.syncVirtual(V3);

      Serial.print(bPID.GetKp());
      Blynk.virtualWrite(V20, bPID.GetKp());
      Blynk.syncVirtual(V20);
      Serial.print(",");
      Serial.print(bPID.GetKi());
      Blynk.virtualWrite(V21, bPID.GetKi());
      Blynk.syncVirtual(V21);
      Serial.print(",");
      Serial.print(bPID.GetKd());
      Blynk.virtualWrite(V22, bPID.GetKd());
      Blynk.syncVirtual(V22);
      Serial.print(",");
      Serial.print(Output);
      Blynk.virtualWrite(V23, Output);
      Blynk.syncVirtual(V23);
      Serial.print(",");
      Serial.print(setPoint);
      Serial.print(",");
      Serial.println(Input);

      if (Display == 1) {

        /********************************************************
           DISPLAY AUSGABE
        ******************************************************/
        u8x8.setFont(u8x8_font_chroma48medium8_r);  //Ausgabe vom aktuellen Wert im Display

        u8x8.setCursor(0, 0);
        u8x8.print("               ");
        u8x8.setCursor(0, 1);
        u8x8.print("               ");
        u8x8.setCursor(0, 2);
        u8x8.print("               ");

        u8x8.setCursor(0, 0);
        u8x8.print(".");
        u8x8.setCursor(1, 0);
        u8x8.print(bPID.GetKp());
        u8x8.setCursor(6, 0);
        u8x8.print(",");
        u8x8.setCursor(7, 0);
        u8x8.print(bPID.GetKi());
        u8x8.setCursor(11, 0);
        u8x8.print(",");
        u8x8.setCursor(12, 0);
        u8x8.print(bPID.GetKd());

        u8x8.setCursor(0, 1);
        u8x8.print("Input:");
        u8x8.setCursor(9, 1);
        u8x8.print("   ");
        u8x8.setCursor(9, 1);
        u8x8.print(Input);

        u8x8.setCursor(0, 2);
        u8x8.print("SetPoint:");
        u8x8.setCursor(10, 2);
        u8x8.print("   ");
        u8x8.setCursor(10, 2);
        u8x8.print(setPoint);

        u8x8.setCursor(0, 3);
        u8x8.print(round((Input * 100) / setPoint));
        u8x8.setCursor(4, 3);
        u8x8.print("%");
        u8x8.setCursor(6, 3);
        u8x8.print(Output);


      }
      if (Display == 2) {
        /********************************************************
           DISPLAY AUSGABE
        ******************************************************/
        display.setTextSize(1);
        display.setTextColor(WHITE);

        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("Ist-Temp:");
        display.print("  ");
        display.println(Input);
        display.print("Soll-Temp:");
        display.print(" ");
        display.println(setPoint);
        display.print("PID:");
        display.print(" ");
        display.print(bPID.GetKp());
        display.print(",");
        display.print(bPID.GetKi());
        display.print(",");
        display.println(bPID.GetKd());
        display.println(" ");
        display.setTextSize(3);
        display.setTextColor(WHITE);

        display.print(round((Input * 100) / setPoint));
        display.println("%");
        display.display();
      }

    }

  } else {
    if (Display == 2) {
      /********************************************************
         DISPLAY AUSGABE
      ******************************************************/
      display.setTextSize(1);
      display.setTextColor(WHITE);

      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("Error:");
      display.print("  ");
      display.println(Input);
      display.print("Check Temp. Sensor!");
      display.display();
    }
    if (Display == 1) {
      /********************************************************
         DISPLAY AUSGABE
      ******************************************************/
      u8x8.setFont(u8x8_font_chroma48medium8_r);  //Ausgabe vom aktuellen Wert im Display
      u8x8.setCursor(0, 0);
      u8x8.print("               ");
      u8x8.setCursor(0, 1);
      u8x8.print("               ");
      u8x8.setCursor(0, 2);
      u8x8.print("               ");


      u8x8.setCursor(0, 1);
      u8x8.print("Error: Temp.");
      u8x8.setCursor(0, 2);
      u8x8.print(Input);
    }
  }

}
