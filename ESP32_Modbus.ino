
#include "License.h"    //Open Source License File

//https://dl.espressif.com/dl/package_esp32_index.json files
#include <WiFi.h>                                                      
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
//end https://dl.espressif.com/dl/package_esp32_index.json


#include <U8g2lib.h>

#define   FONT_ONE_HEIGHT               8                              // font one height in pixels
#define   FONT_TWO_HEIGHT               20                             // font two height in pixels

#define ReWePin                         33                             // Read Enable, Write Enable PIN. I'm not sure these should be bonded.
                                                                       // It probably needs to be 2 pins where we set pin1 and then set pin2 = !pin1

char      chBuffer[128];                                               // general purpose character buffer
char      chPassword[] =                  "KAFcdc2014";                // your network password
char      chSSID[] =                      "To Infinity!";              // your network SSID
//char      chPassword[] =                  "WIFISec16";               // your network password
//char      chSSID[] =                      "DSCARWASH";               // your network SSID
U8G2_SSD1306_128X64_NONAME_F_HW_I2C       u8g2(U8G2_R0, 16, 15, 4);    // OLED graphics
int       nWifiStatus =                   WL_IDLE_STATUS;              // wifi status

//VFD Function Codes
#define NONE        0x00
#define READ_HOLDING_REGS 0x03
#define READ_INPUT_REG    0x04
#define WRITE_SINGLE_REG  0x06
#define WRITE_MULTIPLE_REGS 0x10

//baud rate
int baud;
//VFD Station Address
unsigned short VFD_ID;
//Time in uS to keep ReWePin high so that we don't trim the end of our packet.
int postWriteDelay;

//HTML webpage contents in program memory
const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<body>
<center>
<h1>ESP32 Yaskawa V1000 demo: 1</h1><br>
Click to Run Forward<a href="RunForward">RUN FOR</a><br>
Click to Run Reverse<a href="RunReverse">RUN REV</a><br>
Click to Stop <a href="Stop">STOP</a><br>
<hr>
</center>
 
</body>
</html>
)=====";

//The webserver on port 80 that will respond to client requests
WebServer server(80);



struct PacketData
{
    volatile unsigned char VFDAddress;
    volatile unsigned char FunctionCode;    //
    volatile unsigned char ByteLength;  //Number of words
    volatile unsigned short StartingRegister;
    unsigned short ByteBuffer[50];
    volatile unsigned char ErrorCode;
};

//Comuptes cyclic redundancy check
unsigned short ComputeCrc(unsigned char *pucBuffer, int nSize)
    {
        int nIndex1;
        int nIndex2;

        unsigned short crcHash;

        //Init crc reg to all ones
        crcHash = 0xffff;

        for (nIndex1 = 0; nIndex1 < nSize; nIndex1++)
        {  //Xor packet unsigned char with crc
            crcHash ^= ((unsigned short)pucBuffer[nIndex1] & 0x00ff);

            //Shift right 8 times
            for (nIndex2 = 0; nIndex2 < 8; nIndex2++)
            {
                //Check shifted out bit
                if ((crcHash & 0x0001) == 0x0001)
                { //The shifted out bit will be 1, shift then use the polynomial
                    crcHash >>= 1;
                    crcHash ^= 0xA001;  //Then xor the crc reg with the
                }
                else
                {   //The shifted out bit will be 0, do nothing but shift
                    crcHash >>= 1;
                }
            }
        }
        return crcHash;
    }

//Since we'll be working with multiple data types and sending a serial packet our buffer data type is 8 bits.
//as such we'll need to divide up the data of larger data types into 8 bit incriments before adding them to the buffer.
//IE a 16 bit 
bool Write(PacketData *psrData)
{
    int nSize;//this should just be an unsigned char
    int nIndex;
    unsigned short crcHash;
    unsigned char aucBuffer[255];
    unsigned long dwBytesWritten;

    //starting at 0 index load data to buffer and increase buffer index counter
    nSize = 0; //buffer count
    aucBuffer[nSize++] = psrData->VFDAddress;//add vfd address data to packet
    aucBuffer[nSize++] = psrData->FunctionCode;//add modbus function code data to packet

    //Check function type, different functions send different data
    switch (psrData->FunctionCode)
    {
        case READ_HOLDING_REGS:
        case READ_INPUT_REG:
            aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 8) & 0xff);
            aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 0) & 0xff);
            aucBuffer[nSize++] = 0;
            aucBuffer[nSize++] = psrData->ByteLength;
            break;

        case WRITE_SINGLE_REG:
            aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 8) & 0xff);
            aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 0) & 0xff);
            aucBuffer[nSize++] = (unsigned char)((psrData->ByteBuffer[0] >> 8) & 0xff);
            aucBuffer[nSize++] = (unsigned char)((psrData->ByteBuffer[0] >> 0) & 0xff);
            break;

        case WRITE_MULTIPLE_REGS:
      aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 8) & 0xff);
            aucBuffer[nSize++] = (unsigned char)((psrData->StartingRegister >> 0) & 0xff);
            aucBuffer[nSize++] = 0;
            aucBuffer[nSize++] = psrData->ByteLength;
            aucBuffer[nSize++] = (unsigned char)((psrData->ByteLength * 2) & 0xff);
            for (nIndex = 0; nIndex < (psrData->ByteLength * 2); nIndex += 2)
            {
                aucBuffer[nSize++] = (unsigned char)((psrData->ByteBuffer[nIndex / 2] >> 8) & 0xff);
                aucBuffer[nSize++] = (unsigned char)((psrData->ByteBuffer[nIndex / 2] >> 0) & 0xff);
            }
            break;

        default:
            break;
    }
    crcHash = ComputeCrc(aucBuffer, nSize);
    aucBuffer[nSize++] = (unsigned char)((crcHash >> 0) & 0xff);
    aucBuffer[nSize++] = (unsigned char)((crcHash >> 8) & 0xff);
    
  digitalWrite(ReWePin, HIGH);
  //m_pclPort->fnWrite(aucBuffer, nSize, &dwBytesWritten);
   for(int i = 0; i < nSize; i++)
   {
    Serial.write(aucBuffer[i]);
    }

    Serial.flush();
    delayMicroseconds(postWriteDelay);
    digitalWrite(ReWePin, LOW);
    return true;
}



volatile bool setFrequency(unsigned short frequency)
{
  PacketData srModbusPacket;
  srModbusPacket.VFDAddress = VFD_ID;
  srModbusPacket.FunctionCode = WRITE_MULTIPLE_REGS;
  srModbusPacket.StartingRegister = 0x0002;
  srModbusPacket.ByteLength = 1;
  srModbusPacket.ByteBuffer[0] = frequency;
  Write(&srModbusPacket);
  return true;//will edit later
}

volatile bool fnStart(bool bDirection, unsigned short frequency) //Run
{
  PacketData srModbusPacket;
  srModbusPacket.VFDAddress = VFD_ID;
  srModbusPacket.FunctionCode = WRITE_MULTIPLE_REGS;
  srModbusPacket.StartingRegister = 0x0001;
  srModbusPacket.ByteLength = 2;
  
  if (bDirection)
  {
    srModbusPacket.ByteBuffer[0] = 0x0001; //run forward
  }
  else
  {
    srModbusPacket.ByteBuffer[0] = 0x0003; //run reverse
  }
  srModbusPacket.ByteBuffer[1] = frequency;
  Write(&srModbusPacket);
  return true;
}


volatile bool fnStop(void)
{
  PacketData srModbusPacket;
  srModbusPacket.VFDAddress = VFD_ID;
  srModbusPacket.FunctionCode = WRITE_MULTIPLE_REGS;
  srModbusPacket.StartingRegister = 0x0001;
  srModbusPacket.ByteLength = 1;
    
  srModbusPacket.ByteBuffer[0] = 0;
  Write(&srModbusPacket);
  return true;
}

void handleRoot() {
 //Serial.println("You called root page");
 String s = MAIN_page; //Read HTML contents
 server.send(200, "text/html", s); //Send web page


 u8g2.clearBuffer();
  
      // Display the title.

      sprintf(chBuffer, "%s", "WiFi Stats:");
      u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
  
      // Display the ip address assigned by the wifi router.
      
      char  chIp[81];
      WiFi.localIP().toString().toCharArray(chIp, sizeof(chIp) - 1);
      sprintf(chBuffer, "IP  : %s", chIp);
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 2, chBuffer);
  
      // Display the ssid of the wifi router.
      
      sprintf(chBuffer, "SSID: %s", chSSID);
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 3, chBuffer);
  
      // Display the rssi.
      
      sprintf(chBuffer, "RSSI: %d", WiFi.RSSI());
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 4, chBuffer);
      
      u8g2.sendBuffer();
}

void handleRunFor() { 
  fnStart(true, 6000);
 server.send(200, "text/html", "Running Forward");
 u8g2.clearBuffer();
      sprintf(chBuffer, "%s", "Run Forward");
      u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
      u8g2.sendBuffer();
}

void handleRunRev() { 
  fnStart(false, 6000);
 server.send(200, "text/html", "Running Reverse");
 u8g2.clearBuffer();
      sprintf(chBuffer, "%s", "Run Reverse");
      u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
      u8g2.sendBuffer();
}

void handleStop() { 
  fnStop();
 server.send(200, "text/html", "Stopping"); //Send ADC value only to client ajax request
 u8g2.clearBuffer();
      sprintf(chBuffer, "%s", "Stop");
      u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
      u8g2.sendBuffer();
}

void displayConnecting(bool bblink)
{
  if(bblink)
  {
  u8g2.clearBuffer();
    sprintf(chBuffer, "%s", "Connecting to:");
    u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
    sprintf(chBuffer, "%s", chSSID);
    u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 31 - (FONT_ONE_HEIGHT / 2), chBuffer);
    u8g2.sendBuffer();
  }
  else
  {
    u8g2.clearBuffer();
    sprintf(chBuffer, "%s", " ");
    u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
    u8g2.sendBuffer();
    }
  }

void setup() {
  pinMode(ReWePin,OUTPUT);
  
baud = 57600;
postWriteDelay = 120;
VFD_ID = 6;
Serial.begin(baud);
delay(5);

u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);

  

    int count = 2;
    bool disp = true;
    displayConnecting(disp);
    WiFi.begin(chSSID, chPassword);
    while(WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      count --;
      if(!count)
      {
        count = 2;
        disp = !disp;
        displayConnecting(disp);
        }
    }

    //Serial.println();
    //sprintf(chBuffer, "NTP clock: WiFi connected to %s.", chSSID);
    //Serial.println(chBuffer);
    
    // Display connection stats.

      // Clean the display buffer.
      
      u8g2.clearBuffer();
  
      // Display the title.

      sprintf(chBuffer, "%s", "WiFi Stats:");
      u8g2.drawStr(64 - (u8g2.getStrWidth(chBuffer) / 2), 0, chBuffer);
  
      // Display the ip address assigned by the wifi router.
      
      char  chIp[81];
      WiFi.localIP().toString().toCharArray(chIp, sizeof(chIp) - 1);
      sprintf(chBuffer, "IP  : %s", chIp);
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 2, chBuffer);
  
      // Display the ssid of the wifi router.
      
      sprintf(chBuffer, "SSID: %s", chSSID);
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 3, chBuffer);
  
      // Display the rssi.
      
      sprintf(chBuffer, "RSSI: %d", WiFi.RSSI());
      u8g2.drawStr(0, FONT_ONE_HEIGHT * 4, chBuffer);

      // Display waiting for ntp message.
      
      //u8g2.drawStr(0, FONT_ONE_HEIGHT * 6, "Awaiting NTP time...");

      // Now send the display buffer to the OLED.
      
      u8g2.sendBuffer();

server.on("/", handleRoot);      //Which routine to handle at root location. This is display page
server.on("/RunForward", handleRunFor); //as Per  <a href="ledOn">, Subroutine to be called
server.on("/RunReverse", handleRunRev);
server.on("/Stop", handleStop);

server.begin(); 
}

void loop() {
  // put your main code here, to run repeatedly:
server.handleClient();
//fnStop();
}
