/*
 * This program attempts to initialize an SD card and analyze its structure.
 */
#include "SdFat.h"
#include "sdios.h"
/*
  Set DISABLE_CS_PIN to disable a second SPI device.
  For example, with the Ethernet shield, set DISABLE_CS_PIN
  to 10 to disable the Ethernet controller.
*/
const int8_t DISABLE_CS_PIN = -1;
/*
  Change the value of SD_CS_PIN if you are using SPI
  and your hardware does not use the default value, SS.
  Common values are:
  Arduino Ethernet shield: pin 4
  Sparkfun SD shield: pin 8
  Adafruit SD shields and modules: pin 10
*/
// SDCARD_SS_PIN is defined for the built-in SD on some boards.
#ifndef SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SS;
#else  // SDCARD_SS_PIN
const uint8_t SD_CS_PIN = SDCARD_SS_PIN;
#endif  // SDCARD_SS_PIN

// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(16))
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(16))
#endif  // HAS_SDIO_CLASS

//------------------------------------------------------------------------------
SdFs sd;
cid_t cid;
csd_t csd;
scr_t scr;
uint8_t cmd6Data[64];
uint32_t eraseSize;
uint32_t ocr;
static ArduinoOutStream cout(Serial);
//------------------------------------------------------------------------------
void cidDmp() {
  cout << "\nManufacturer ID: ";
  cout << uppercase << showbase << hex << int(cid.mid) << dec << endl;
  cout << "OEM ID: " << cid.oid[0] << cid.oid[1] << endl;
  cout << "Product: ";
  for (uint8_t i = 0; i < 5; i++) {
    cout << cid.pnm[i];
  }
  cout << "\nRevision: " << cid.prvN() << '.' << cid.prvM() << endl;
  cout << "Serial number: " << hex << cid.psn() << dec << endl;
  cout << "Manufacturing date: ";
  cout << cid.mdtMonth() << '/' << cid.mdtYear() << endl;
  cout << endl;
}
//------------------------------------------------------------------------------
void clearSerialInput() {
  uint32_t m = micros();
  do {
    if (Serial.read() >= 0) {
      m = micros();
    }
  } while (micros() - m < 10000);
}
//------------------------------------------------------------------------------
void csdDmp() {
  eraseSize = csd.eraseSize();
  cout << "cardSize: " << 0.000512 * csd.capacity();
  cout << " MB (MB = 1,000,000 bytes\n");

  cout << "flashEraseSize: " << int(eraseSize) << " blocks\n";
  cout << "eraseSingleBlock: ";
  if (csd.eraseSingleBlock()) {
    cout << "true\n";
  } else {
    cout << "false\n";
  }
  cout << "dataAfterErase: ";
  if (scr.dataAfterErase()) {
    cout << "ones\n";
  } else {
    cout << "zeros\n";
  }
}
//------------------------------------------------------------------------------
void errorPrint() {
  if (sd.sdErrorCode()) {
    cout << "SD errorCode: " << hex << showbase;
    printSdErrorSymbol(&Serial, sd.sdErrorCode());
    cout << " = " << int(sd.sdErrorCode()) << endl;
    cout << "SD errorData = " << int(sd.sdErrorData()) << dec << endl;
  }
}
//------------------------------------------------------------------------------
bool mbrDmp() {
  MbrSector_t mbr;
  bool valid = true;
  if (!sd.card()->readSector(0, (uint8_t*)&mbr)) {
    cout << "\nread MBR failed.\n";
    errorPrint();
    return false;
  }
  cout << "\nSD Partition Table\n";
  cout << "part,boot,bgnCHS[3],type,endCHS[3],start,length\n";
  for (uint8_t ip = 1; ip < 5; ip++) {
    MbrPart_t *pt = &mbr.part[ip - 1];
    if ((pt->boot != 0 && pt->boot != 0X80) ||
        getLe32(pt->relativeSectors) > csd.capacity()) {
      valid = false;
    }
    cout << int(ip) << ',' << uppercase << showbase << hex;
    cout << int(pt->boot) << ',';
    for (int i = 0; i < 3; i++ ) {
      cout << int(pt->beginCHS[i]) << ',';
    }
    cout << int(pt->type) << ',';
    for (int i = 0; i < 3; i++ ) {
      cout << int(pt->endCHS[i]) << ',';
    }
    cout << dec << getLe32(pt->relativeSectors) << ',';
    cout << getLe32(pt->totalSectors) << endl;
  }
  if (!valid) {
    cout << "\nMBR not valid, assuming Super Floppy format.\n";
  }
  return true;
}
//------------------------------------------------------------------------------
void dmpVol() {
  cout << "\nScanning FAT, please wait.\n";
  int32_t freeClusterCount = sd.freeClusterCount();
  if (sd.fatType() <= 32) {
    cout << "\nVolume is FAT" << int(sd.fatType()) << endl;
  } else {
    cout << "\nVolume is exFAT\n";
  }
  cout << "sectorsPerCluster: " << sd.sectorsPerCluster() << endl;
  cout << "fatStartSector:    " << sd.fatStartSector() << endl;
  cout << "dataStartSector:   " << sd.dataStartSector() << endl;
  cout << "clusterCount:      " << sd.clusterCount() << endl;
  cout << "freeClusterCount:  ";
  if (freeClusterCount >= 0) {
    cout << freeClusterCount << endl;
  } else {
    cout << "failed\n";
    errorPrint();    
  }
}
//------------------------------------------------------------------------------
void printCardType() {

  cout << "\nCard type: ";

  switch (sd.card()->type()) {
    case SD_CARD_TYPE_SD1:
      cout << "SD1\n";
      break;

    case SD_CARD_TYPE_SD2:
      cout << "SD2\n";
      break;

    case SD_CARD_TYPE_SDHC:
      if (csd.capacity() < 70000000) {
        cout << "SDHC\n";
      } else {
        cout << "SDXC\n";
      }
      break;

    default:
      cout << "Unknown\n";
  }
}
//------------------------------------------------------------------------------
void printConfig(SdSpiConfig config) {
  if (DISABLE_CS_PIN < 0) {
    cout <<
           "\nAssuming the SD is the only SPI device.\n"
           "Edit DISABLE_CS_PIN to disable an SPI device.\n";
  } else {
    cout << "\nDisabling SPI device on pin ";
    cout << int(DISABLE_CS_PIN) << endl;
    pinMode(DISABLE_CS_PIN, OUTPUT);
    digitalWrite(DISABLE_CS_PIN, HIGH);
  }
  cout << "\nAssuming the SD chip select pin is: " << int(config.csPin);
  cout << "\nEdit SD_CS_PIN to change the SD chip select pin.\n";
}
//------------------------------------------------------------------------------
void printConfig(SdioConfig config) {
  (void)config;
  cout << "Assuming an SDIO interface.\n";
}
//-----------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  cout << "SdFat version: " << SD_FAT_VERSION_STR << endl;
  printConfig(SD_CONFIG);

}
//------------------------------------------------------------------------------
void loop() {
  // Read any existing Serial data.
  clearSerialInput();

  // F stores strings in flash to save RAM
  cout << "\ntype any character to start\n";
  while (!Serial.available()) {
    yield();
  }
  uint32_t t = millis();
  if (!sd.cardBegin(SD_CONFIG)) {
    cout <<
           "\nSD initialization failed.\n"
           "Do not reformat the card!\n"
           "Is the card correctly inserted?\n"
           "Is there a wiring/soldering problem?\n";
    if (isSpi(SD_CONFIG)) {
      cout <<
           "Is SD_CS_PIN set to the correct value?\n"
           "Does another SPI device need to be disabled?\n"
           ;
    }
    errorPrint();
    return;
  }
  t = millis() - t;
  cout << "init time: " << dec << t << " ms" << endl;

  if (!sd.card()->readCID(&cid) ||
      !sd.card()->readCSD(&csd) ||
      !sd.card()->readOCR(&ocr) ||
      !sd.card()->readSCR(&scr)) {
    cout << "readInfo failed\n";
    errorPrint();
    return;
  }
  printCardType();
  cout << "sdSpecVer: " << 0.01*scr.sdSpecVer() << endl;
  cout << "HighSpeedMode: ";
  if (scr.sdSpecVer() &&
    sd.card()->cardCMD6(0X00FFFFFF, cmd6Data) && (2 & cmd6Data[13])) {
    cout << "true\n";
  } else {
    cout << "false\n";
  }      
  cidDmp();
  csdDmp();
  cout << "\nOCR: " << uppercase << showbase;
  cout << hex << ocr << dec << endl;
  if (!mbrDmp()) {
    return;
  }
  if (!sd.volumeBegin()) {
    cout << "\nvolumeBegin failed. Is the card formatted?\n";
    errorPrint();
    return;
  }
  dmpVol();
}