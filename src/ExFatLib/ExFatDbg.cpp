/**
 * Copyright (c) 2011-2022 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "ExFatVolume.h"
#include "../common/upcase.h"
#include "ExFatLib.h"
#ifndef DOXYGEN_SHOULD_SKIP_THIS
//------------------------------------------------------------------------------
static void printHex(print_t* pr, uint8_t h);
static void printHex(print_t* pr, uint16_t val);
static void printHex(print_t* pr, uint32_t val);
static void printHex64(print_t* pr, uint64_t n);
static void println64(print_t* pr, uint64_t n);
//------------------------------------------------------------------------------
static void dmpDirData(print_t* pr, DirGeneric_t* dir) {
  for (uint8_t k = 0; k < 31; k++) {
    if (k) {
      pr->write(' ');
    }
    printHex(pr, dir->data[k]);
  }
  pr->println();
}
//------------------------------------------------------------------------------
static uint16_t exFatDirChecksum(const void* dir, uint16_t checksum) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(dir);
  bool skip = data[0] == EXFAT_TYPE_FILE;
  for (size_t i = 0; i < 32; i += (i == 1 && skip ? 3 : 1)) {
    checksum = ((checksum << 15) | (checksum >> 1)) + data[i];
  }
  return checksum;
}

//------------------------------------------------------------------------------
static uint16_t hashDir(DirName_t* dir, uint16_t hash) {
  for (uint8_t i = 0; i < 30; i += 2) {
    uint16_t u = getLe16(dir->unicode + i);
    if (!u) {
      break;
    }
  uint16_t c = toUpcase(u);
  hash = ((hash << 15) | (hash >> 1)) + (c & 0XFF);
  hash = ((hash << 15) | (hash >> 1)) + (c >> 8);
  }
  return hash;
}
//------------------------------------------------------------------------------
static void printDateTime(print_t* pr,
                          uint32_t timeDate, uint8_t ms, int8_t tz) {
  fsPrintDateTime(pr, timeDate, ms, tz);
  pr->println();
}
//------------------------------------------------------------------------------
static void printDirBitmap(print_t* pr, DirBitmap_t* dir) {
  pr->print("dirBitmap: 0x");
  pr->println(dir->type, HEX);
  pr->print("flags: 0x");
  pr->println(dir->flags, HEX);
  pr->print("firstCluster: ");
  pr->println(getLe32(dir->firstCluster));
  pr->print("size: ");
  println64(pr, getLe64(dir->size));
}
//------------------------------------------------------------------------------
static void printDirFile(print_t* pr, DirFile_t* dir) {
  pr->print("dirFile: 0x");
  pr->println(dir->type, HEX);
  pr->print("setCount: ");
  pr->println(dir->setCount);
  pr->print("setChecksum: 0x");
  pr->println(getLe16(dir->setChecksum), HEX);
  pr->print("attributes: 0x");
  pr->println(getLe16(dir->attributes), HEX);
  pr->print("createTime: ");
  printDateTime(pr, getLe32(dir->createTime),
                dir->createTimeMs, dir->createTimezone);
  pr->print("modifyTime: ");
  printDateTime(pr, getLe32(dir->modifyTime),
                dir->modifyTimeMs, dir->modifyTimezone);
  pr->print("accessTime: ");
  printDateTime(pr, getLe32(dir->accessTime), 0, dir->accessTimezone);
}
//------------------------------------------------------------------------------
static void printDirLabel(print_t* pr, DirLabel_t* dir) {
  pr->print("dirLabel: 0x");
  pr->println(dir->type, HEX);
  pr->print("labelLength: ");
  pr->println(dir->labelLength);
  pr->print("unicode: ");
  for (size_t i = 0; i < dir->labelLength; i++) {
    pr->write(dir->unicode[2*i]);
  }
  pr->println();
}
//------------------------------------------------------------------------------
static void printDirName(print_t* pr, DirName_t* dir) {
  pr->print("dirName: 0x");
  pr->println(dir->type, HEX);
  pr->print("unicode: ");
  for (size_t i = 0; i < 30; i += 2) {
    uint16_t c = getLe16(dir->unicode + i);
    if (c == 0) break;
    if (c < 128) {
      pr->print(static_cast<char>(c));
    } else {
      pr->print("0x");
      pr->print(c, HEX);
    }
    pr->print(' ');
  }
  pr->println();
}
//------------------------------------------------------------------------------
static void printDirStream(print_t* pr, DirStream_t* dir) {
  pr->print("dirStream: 0x");
  pr->println(dir->type, HEX);
  pr->print("flags: 0x");
  pr->println(dir->flags, HEX);
  pr->print("nameLength: ");
  pr->println(dir->nameLength);
  pr->print("nameHash: 0x");
  pr->println(getLe16(dir->nameHash), HEX);
  pr->print("validLength: ");
  println64(pr, getLe64(dir->validLength));
  pr->print("firstCluster: ");
  pr->println(getLe32(dir->firstCluster));
  pr->print("dataLength: ");
  println64(pr, getLe64(dir->dataLength));
}
//------------------------------------------------------------------------------
static void printDirUpcase(print_t* pr, DirUpcase_t* dir) {
  pr->print("dirUpcase: 0x");
  pr->println(dir->type, HEX);
    pr->print("checksum: 0x");
  pr->println(getLe32(dir->checksum), HEX);
  pr->print("firstCluster: ");
  pr->println(getLe32(dir->firstCluster));
  pr->print("size: ");
  println64(pr, getLe64(dir->size));
}
//------------------------------------------------------------------------------
static void printExFatBoot(print_t* pr, pbs_t* pbs) {
  BpbExFat_t* ebs = reinterpret_cast<BpbExFat_t*>(pbs->bpb);
  pr->print("bpbSig: 0x");
  pr->println(getLe16(pbs->signature), HEX);
  pr->print("FileSystemName: ");
  pr->write(reinterpret_cast<uint8_t*>(pbs->oemName), 8);
  pr->println();
  for (size_t i = 0; i < sizeof(ebs->mustBeZero); i++) {
    if (ebs->mustBeZero[i]) {
      pr->println("mustBeZero error");
      break;
    }
  }
  pr->print("PartitionOffset: 0x");
  printHex64(pr, getLe64(ebs->partitionOffset));
  pr->print("VolumeLength: ");
  println64(pr, getLe64(ebs->volumeLength));
  pr->print("FatOffset: 0x");
  pr->println(getLe32(ebs->fatOffset), HEX);
  pr->print("FatLength: ");
  pr->println(getLe32(ebs->fatLength));
  pr->print("ClusterHeapOffset: 0x");
  pr->println(getLe32(ebs->clusterHeapOffset), HEX);
  pr->print("ClusterCount: ");
  pr->println(getLe32(ebs->clusterCount));
  pr->print("RootDirectoryCluster: ");
  pr->println(getLe32(ebs->rootDirectoryCluster));
  pr->print("VolumeSerialNumber: 0x");
  pr->println(getLe32(ebs->volumeSerialNumber), HEX);
  pr->print("FileSystemRevision: 0x");
  pr->println(getLe32(ebs->fileSystemRevision), HEX);
  pr->print("VolumeFlags: 0x");
  pr->println(getLe16(ebs->volumeFlags) , HEX);
  pr->print("BytesPerSectorShift: ");
  pr->println(ebs->bytesPerSectorShift);
  pr->print("SectorsPerClusterShift: ");
  pr->println(ebs->sectorsPerClusterShift);
  pr->print("NumberOfFats: ");
  pr->println(ebs->numberOfFats);
  pr->print("DriveSelect: 0x");
  pr->println(ebs->driveSelect, HEX);
  pr->print("PercentInUse: ");
  pr->println(ebs->percentInUse);
}
//------------------------------------------------------------------------------
static void printHex(print_t* pr, uint8_t h) {
  if (h < 16) {
    pr->write('0');
  }
  pr->print(h, HEX);
}
//------------------------------------------------------------------------------
static void printHex(print_t* pr, uint16_t val) {
  bool space = true;
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t h = (val >> (12 - 4*i)) & 15;
    if (h || i == 3) {
      space = false;
    }
    if (space) {
      pr->write(' ');
    } else {
      pr->print(h, HEX);
    }
  }
}
//------------------------------------------------------------------------------
static void printHex(print_t* pr, uint32_t val) {
  bool space = true;
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t h = (val >> (28 - 4*i)) & 15;
    if (h || i == 7) {
      space = false;
    }
    if (space) {
      pr->write(' ');
    } else {
      pr->print(h, HEX);
    }
  }
}
//------------------------------------------------------------------------------
static void printHex64(print_t* pr, uint64_t n) {
  char buf[17];
  char *str = &buf[sizeof(buf) - 1];
  *str = '\0';
  do {
    uint8_t h = n & 15;
    *--str = h < 10 ? h + '0' : h + 'A' - 10;
    n >>= 4;
  } while (n);
  pr->println(str);
}
//------------------------------------------------------------------------------
static void println64(print_t* pr, uint64_t n) {
  char buf[21];
  char *str = &buf[sizeof(buf) - 1];
  *str = '\0';
  do {
    uint64_t m = n;
    n /= 10;
    *--str = m - 10*n + '0';
  } while (n);
  pr->println(str);
}
//------------------------------------------------------------------------------
static void printMbr(print_t* pr, MbrSector_t* mbr) {
  pr->print("mbrSig: 0x");
  pr->println(getLe16(mbr->signature), HEX);
  for (int i = 0; i < 4; i++) {
    printHex(pr, mbr->part[i].boot);
    pr->write(' ');
    for (int k = 0; k < 3; k++) {
      printHex(pr, mbr->part[i].beginCHS[k]);
      pr->write(' ');
    }
    printHex(pr, mbr->part[i].type);
    pr->write(' ');
    for (int k = 0; k < 3; k++) {
      printHex(pr, mbr->part[i].endCHS[k]);
      pr->write(' ');
    }
    pr->print(getLe32(mbr->part[i].relativeSectors), HEX);
    pr->print(' ');
    pr->println(getLe32(mbr->part[i].totalSectors), HEX);
  }
}
//==============================================================================
void ExFatPartition::checkUpcase(print_t* pr) {
  bool skip = false;
  uint16_t u = 0;
  uint8_t* upcase = nullptr;
  uint32_t size = 0;
  uint32_t sector = clusterStartSector(m_rootDirectoryCluster);
  uint8_t* cache = dataCachePrepare(sector, FsCache::CACHE_FOR_READ);
  if (!cache) {
    pr->println("read root failed");
    return;
  }
  DirUpcase_t* dir = reinterpret_cast<DirUpcase_t*>(cache);

  pr->println("\nChecking upcase table");
  for (size_t i = 0; i < 16; i++) {
    if (dir[i].type == EXFAT_TYPE_UPCASE) {
      sector = clusterStartSector(getLe32(dir[i].firstCluster));
      size = getLe64(dir[i].size);
      break;
    }
  }
  if (!size) {
    pr->println("upcase not found");
    return;
  }
  for (size_t i = 0; i < size/2; i++) {
    if ((i%256) == 0) {
      upcase = dataCachePrepare(sector++, FsCache::CACHE_FOR_READ);
      if (!upcase) {
        pr->println("read upcase failed");
        return;
      }
    }
    uint16_t v = getLe16(&upcase[2*(i & 0XFF)]);
    if (skip) {
      pr->print("skip ");
      pr->print(u);
      pr->write(' ');
      pr->println(v);
    }
    if (v == 0XFFFF) {
      skip = true;
    } else if (skip) {
      for (uint16_t k = 0; k < v; k++) {
        uint16_t x = toUpcase(u + k);
        if (x != (u + k)) {
          printHex(pr, (uint16_t)(u+k));
          pr->write(',');
          printHex(pr, x);
          pr->println("<<<<<<<<<<<<<<<<<<<<");
        }
      }
      u += v;
      skip = false;
    } else {
      uint16_t x = toUpcase(u);
      if (v != x) {
        printHex(pr, u);
        pr->write(',');
        printHex(pr, x);
        pr->write(',');
        printHex(pr, v);
        pr->println();
      }
      u++;
    }
  }
  pr->println("Done checkUpcase");
}
//------------------------------------------------------------------------------
void ExFatPartition::dmpBitmap(print_t* pr) {
  pr->println("bitmap:");
  dmpSector(pr, m_clusterHeapStartSector);
}
//------------------------------------------------------------------------------
void ExFatPartition::dmpCluster(print_t* pr, uint32_t cluster,
                                uint32_t offset, uint32_t count) {
  uint32_t sector = clusterStartSector(cluster) + offset;
  for (uint32_t i = 0; i < count; i++) {
    pr->print("\nSector: ");
    pr->println(sector + i, HEX);
    dmpSector(pr, sector + i);
  }
}
//------------------------------------------------------------------------------
void ExFatPartition::dmpFat(print_t* pr, uint32_t start, uint32_t count) {
  uint32_t sector = m_fatStartSector + start;
  uint32_t cluster = 128*start;
  pr->println("FAT:");
  for (uint32_t i = 0; i < count; i++) {
    uint8_t* cache = dataCachePrepare(sector + i, FsCache::CACHE_FOR_READ);
    if (!cache) {
      pr->println("cache read failed");
      return;
    }
    uint32_t* fat = reinterpret_cast<uint32_t*>(cache);
    for (size_t k = 0; k < 128; k++) {
      if (0 == cluster%8) {
        if (k) {
          pr->println();
        }
        printHex(pr, cluster);
      }
      cluster++;
      pr->write(' ');
      printHex(pr, fat[k]);
    }
    pr->println();
  }
}
//------------------------------------------------------------------------------
void ExFatPartition::dmpSector(print_t* pr, uint32_t sector) {
  uint8_t* cache = dataCachePrepare(sector, FsCache::CACHE_FOR_READ);
  if (!cache) {
    pr->println("dmpSector failed");
    return;
  }
  for (uint16_t i = 0; i < m_bytesPerSector; i++) {
    if (i%32 == 0) {
      if (i) {
        pr->println();
      }
      printHex(pr, i);
    }
    pr->write(' ');
    printHex(pr, cache[i]);
  }
  pr->println();
}
//------------------------------------------------------------------------------
bool ExFatPartition::printDir(print_t* pr, ExFatFile* file) {
  DirGeneric_t* dir = nullptr;
  DirFile_t* dirFile;
  DirStream_t* dirStream;
  DirName_t* dirName;
  uint16_t calcHash = 0;
  uint16_t nameHash = 0;
  uint16_t setChecksum = 0;
  uint16_t calcChecksum = 0;
  uint8_t  nameLength = 0;
  uint8_t  setCount = 0;
  uint8_t  nUnicode;

#define RAW_ROOT
#ifndef RAW_ROOT
  while (1) {
    uint8_t buf[FS_DIR_SIZE];
    if (file->read(buf, FS_DIR_SIZE) != FS_DIR_SIZE) {
      break;
    }
    dir = reinterpret_cast<DirGeneric_t*>(buf);
#else  // RAW_ROOT
  (void)file;
  uint32_t nDir = 1UL << (m_sectorsPerClusterShift + 4);
  uint32_t sector = clusterStartSector(m_rootDirectoryCluster);
  for (uint32_t iDir = 0; iDir < nDir; iDir++) {
    size_t i = iDir%16;
    if (i == 0) {
      uint8_t* cache = dataCachePrepare(sector++, FsCache::CACHE_FOR_READ);
      if (!cache) {
        return false;
      }
      dir = reinterpret_cast<DirGeneric_t*>(cache);
    } else {
      dir++;
    }
#endif  // RAW_ROOT
    if (dir->type == EXFAT_TYPE_END_DIR) {
      break;
    }
    pr->println();

    switch (dir->type) {
      case EXFAT_TYPE_BITMAP:
        printDirBitmap(pr, reinterpret_cast<DirBitmap_t*>(dir));
        break;

      case EXFAT_TYPE_UPCASE:
        printDirUpcase(pr, reinterpret_cast<DirUpcase_t*>(dir));
        break;

      case EXFAT_TYPE_LABEL:
        printDirLabel(pr, reinterpret_cast<DirLabel_t*>(dir));
        break;

      case EXFAT_TYPE_FILE:
        dirFile = reinterpret_cast<DirFile_t*>(dir);
        printDirFile(pr, dirFile);
        setCount = dirFile->setCount;
        setChecksum = getLe16(dirFile->setChecksum);
        calcChecksum = exFatDirChecksum(dir, 0);
        break;

      case EXFAT_TYPE_STREAM:
        dirStream = reinterpret_cast<DirStream_t*>(dir);
        printDirStream(pr, dirStream);
        nameLength = dirStream->nameLength;
        nameHash = getLe16(dirStream->nameHash);
        calcChecksum = exFatDirChecksum(dir, calcChecksum);
        setCount--;
        calcHash = 0;
        break;

       case EXFAT_TYPE_NAME:
        dirName = reinterpret_cast<DirName_t*>(dir);
        printDirName(pr, dirName);
        calcChecksum = exFatDirChecksum(dir, calcChecksum);
        nUnicode = nameLength > 15 ? 15 : nameLength;
        calcHash = hashDir(dirName, calcHash);
        nameLength -= nUnicode;
        setCount--;
        if (nameLength == 0  || setCount == 0) {
          pr->print("setChecksum: 0x");
          pr->print(setChecksum, HEX);
          if (setChecksum != calcChecksum) {
            pr->print(" != calcChecksum: 0x");
          } else {
            pr->print(" == calcChecksum: 0x");
          }
          pr->println(calcChecksum, HEX);
          pr->print("nameHash: 0x");
          pr->print(nameHash, HEX);
          if (nameHash != calcHash) {
            pr->print(" != calcHash: 0x");
          } else {
            pr->print(" == calcHash: 0x");
          }
          pr->println(calcHash, HEX);
        }
        break;

      default:
        if (dir->type & EXFAT_TYPE_USED) {
          pr->print("Unknown dirType: 0x");
        } else {
          pr->print("Unused dirType: 0x");
        }
        pr->println(dir->type, HEX);
        dmpDirData(pr, dir);
        break;
    }
  }
  pr->println("Done");
  return true;
}
//------------------------------------------------------------------------------
void ExFatPartition::printFat(print_t* pr) {
  uint32_t next;
  int8_t status;
  pr->println("FAT:");
  for (uint32_t cluster = 0; cluster < 16; cluster++) {
    status = fatGet(cluster, &next);
    pr->print(cluster, HEX);
    pr->write(' ');
    if (status == 0) {
      next = EXFAT_EOC;
    }
    pr->println(next, HEX);
  }
}
//------------------------------------------------------------------------------
void ExFatPartition::printUpcase(print_t* pr) {
  uint8_t* upcase = nullptr;
  uint32_t sector;
  uint32_t size = 0;
  uint32_t checksum = 0;
  DirUpcase_t* dir;
  sector = clusterStartSector(m_rootDirectoryCluster);
  upcase = dataCachePrepare(sector, FsCache::CACHE_FOR_READ);
  dir = reinterpret_cast<DirUpcase_t*>(upcase);
  if (!dir) {
    pr->println("read root dir failed");
    return;
  }
  for (size_t i = 0; i < 16; i++) {
    if (dir[i].type == EXFAT_TYPE_UPCASE) {
      sector = clusterStartSector(getLe32(dir[i].firstCluster));
      size = getLe64(dir[i].size);
      break;
    }
  }
  if (!size) {
    pr->println("upcase not found");
    return;
  }
  for (uint16_t i = 0; i < size/2; i++) {
    if ((i%256) == 0) {
      upcase = dataCachePrepare(sector++, FsCache::CACHE_FOR_READ);
      if (!upcase) {
        pr->println("read upcase failed");
        return;
      }
    }
    if (i%16 == 0) {
      pr->println();
      printHex(pr, i);
    }
    pr->write(' ');
    uint16_t uc = getLe16(&upcase[2*(i & 0XFF)]);
    printHex(pr, uc);
    checksum = upcaseChecksum(uc, checksum);
  }
  pr->println();
  pr->print("checksum: ");
  printHex(pr, checksum);
  pr->println();
}
//------------------------------------------------------------------------------
bool ExFatPartition::printVolInfo(print_t* pr) {
  uint8_t* cache = dataCachePrepare(0, FsCache::CACHE_FOR_READ);
  if (!cache) {
    pr->println("read mbr failed");
    return false;
  }
  MbrSector_t* mbr = reinterpret_cast<MbrSector_t*>(cache);
  printMbr(pr, mbr);
  uint32_t volStart = getLe32(mbr->part->relativeSectors);
  uint32_t volSize = getLe32(mbr->part->totalSectors);
  if (volSize == 0) {
    pr->print("bad partition size");
    return false;
  }
  cache = dataCachePrepare(volStart, FsCache::CACHE_FOR_READ);
  if (!cache) {
    pr->println("read pbs failed");
    return false;
  }
  printExFatBoot(pr, reinterpret_cast<pbs_t*>(cache));
  return true;
}
#endif  // DOXYGEN_SHOULD_SKIP_THIS
