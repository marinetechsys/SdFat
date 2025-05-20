/*
 * This program demonstrates the freeClusterCount() call.
 */
#include <SPI.h>
#include "SdFat.h"
#include "sdios.h"
/*
 * SD chip select pin.  Common values are:
 *
 * Arduino Ethernet shield, pin 4.
 * SparkFun SD shield, pin 8.
 * Adafruit Datalogging shield, pin 10.
 * Default SD chip select is the SPI SS pin.
 */
const uint8_t chipSelect = SS;

#define TEST_FILE "Cluster.test"
// file system
SdFat sd;

// test file
SdFile file;

// Serial output stream
ArduinoOutStream cout(Serial);
//------------------------------------------------------------------------------
void printFreeSpace() {
  cout << "freeClusterCount( call time: ");
  uint32_t m = micros();
  uint32_t volFree = sd.vol()->freeClusterCount();
  cout << micros() - m << " micros\n";
  cout << "freeClusters: " <<  volFree << setprecision(3) << endl;
  float fs = 0.000512*volFree*sd.vol()->blocksPerCluster();
  cout << "freeSpace: " << fs << " MB (MB = 1,000,000 bytes\n\n");
}
//------------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  // Wait for USB Serial
  while (!Serial) {
    yield();
  }
  if (!MAINTAIN_FREE_CLUSTER_COUNT) {
    cout << "Please edit SdFatConfig.h and set\n";
    cout << "MAINTAIN_FREE_CLUSTER_COUNT nonzero for\n";
    cout << "maximum freeClusterCount( performance.\n\n");
  }
  // F stores strings in flash to save RAM
  cout << "Type any character to start\n";
  while (!Serial.available()) {
    yield();
  }
  // Initialize at the highest speed supported by the board that is
  // not over 50 MHz. Try a lower speed if SPI errors occur.
  if (!sd.begin(chipSelect, SD_SCK_MHZ(50))) {
    sd.initErrorHalt();
  }
  // Insure no TEST_FILE.
  sd.remove(TEST_FILE);

  cout << "\nFirst call to freeClusterCount scans the FAT.\n\n";
  printFreeSpace();

  cout << "Create and write to " << TEST_FILE << endl;
  if (!file.open(TEST_FILE, O_WRONLY | O_CREAT)) {
    sd.errorHalt("Create failed");
  }
  file.print("Cause a cluster to be allocated");
  file.close();

  cout << "\nSecond freeClusterCount call is faster if\n";
  cout << "MAINTAIN_FREE_CLUSTER_COUNT is nonzero.\n\n";

  printFreeSpace();

  cout << "Remove " << TEST_FILE << endl << endl;
  sd.remove(TEST_FILE);
  printFreeSpace();
  cout << "Done" << endl;
}
//------------------------------------------------------------------------------
void loop() {}