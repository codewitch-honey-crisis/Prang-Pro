#include <USBHost_t36.h>

// Setup USBHost_t36 and as many HUB ports as needed.
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
USBHub hub3(myusb);
USBHub hub4(myusb);

// Instances for the number of USB drives you are using.
USBDrive myDrive1(myusb);
USBDrive myDrive2(myusb);

// Instances for accessing the files on each drive
USBFilesystem myFiles1(myusb);
USBFilesystem myFiles2(myusb);


// Show USB drive information for the selected USB drive.
void printDriveInfo(USBDrive &drive) {
  // Print USB drive information.
  Serial.printf(F("       connected: %d\n"), drive.msDriveInfo.connected);
  Serial.printf(F("     initialized: %d\n"), drive.msDriveInfo.initialized);
  Serial.printf(F("   USB Vendor ID: %4.4x\n"), drive.msDriveInfo.idVendor);
  Serial.printf(F("  USB Product ID: %4.4x\n"), drive.msDriveInfo.idProduct);
  Serial.printf(F("      HUB Number: %d\n"), drive.msDriveInfo.hubNumber);
  Serial.printf(F("        HUB Port: %d\n"), drive.msDriveInfo.hubPort);
  Serial.printf(F("  Device Address: %d\n"), drive.msDriveInfo.deviceAddress);
  Serial.printf(F("Removable Device: "));
  if(drive.msDriveInfo.inquiry.Removable == 1) {
    Serial.printf(F("YES\n"));
  } else {
    Serial.printf(F("NO\n"));
  }
  Serial.printf(F("        VendorID: %8.8s\n"), drive.msDriveInfo.inquiry.VendorID);
  Serial.printf(F("       ProductID: %16.16s\n"), drive.msDriveInfo.inquiry.ProductID);
  Serial.printf(F("      RevisionID: %4.4s\n"), drive.msDriveInfo.inquiry.RevisionID);
  Serial.printf(F("         Version: %d\n"), drive.msDriveInfo.inquiry.Version);
  Serial.printf(F("    Sector Count: %ld\n"), drive.msDriveInfo.capacity.Blocks);
  Serial.printf(F("     Sector size: %ld\n"), drive.msDriveInfo.capacity.BlockSize);
  uint64_t drivesize = drive.msDriveInfo.capacity.Blocks;
  drivesize *= drive.msDriveInfo.capacity.BlockSize;
  Serial.print(F("   Disk Capacity: "));
  Serial.print(drivesize);
  Serial.println(" Bytes");
  drive.printPartionTable(Serial);
  Serial.println();
}

// Show USB filesystem information
void printFilesystemInfo(USBFilesystem &fs) {
  // print the type and size of the first FAT-type volume
  char volname[32];
  fs.mscfs.getVolumeLabel(volname, sizeof(volname));
  Serial.print("Volume name: ");
  Serial.println(volname);
  Serial.print("Volume type: FAT");
  Serial.println(fs.mscfs.fatType(), DEC);
  Serial.print("Cluster Size: ");
  Serial.print(fs.mscfs.bytesPerCluster());
  Serial.println(" bytes");
  Serial.print("Volume size: ");
  Serial.print(fs.totalSize());
  Serial.println(" bytes");
  Serial.print(" Space used: ");
  elapsedMillis ms = 0;
  Serial.print(fs.usedSize());
  Serial.print(" bytes  (");
  Serial.print(ms);
  Serial.println(" ms to compute)");
  Serial.println();
  Serial.println("Files:");
  fs.mscfs.ls(LS_R | LS_DATE | LS_SIZE);
  Serial.println();
}


void setup()
{
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for Arduino Serial Monitor to connect.
  }

  myusb.begin();
  Serial.println("\nInitializing USB drive 1...");
  // future USBFilesystem will begin automatically
  // begin(USBDrive) is a temporary feature
  if (!myFiles1.begin(&myDrive1)) {
    Serial.print("initialization failed with code: ");
    myFiles1.printError(Serial);
    return;
  }
  Serial.printf("Device 1 Info:\n");
  printDriveInfo(myDrive1);
  printFilesystemInfo(myFiles1);

  Serial.println("\nInitializing USB drive 2...");
  // future USBFilesystem will begin automatically
  // begin(USBDrive) is a temporary feature
  if (!myFiles2.begin(&myDrive2)) {
    Serial.print("initialization failed with code: ");
    myFiles2.printError(Serial);
    return;
  }
  Serial.printf("Device 2 Info:\n");
  printDriveInfo(myDrive2);
  printFilesystemInfo(myFiles2);
}


void loop(void) {

}
