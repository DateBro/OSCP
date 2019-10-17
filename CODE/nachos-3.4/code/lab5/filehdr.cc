// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize) {
    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);

    int directSectors = numSectors;
    if (numSectors > NumDirect - 1) directSectors = NumDirect - 1;

    if (freeMap->NumClear() < directSectors)
        return FALSE;        // not enough space
    for (int i = 0; i < directSectors; i++)
        dataSectors[i] = freeMap->Find();

    if (numSectors > NumDirect - 1) {
        int indirectSector = freeMap->Find();
        if (indirectSector == -1) {
            return FALSE;
        } else {
            indirect = new FileHeader;
            int remainFileSize = fileSize - (NumDirect - 1) * SectorSize;
            if (!indirect->Allocate(freeMap, remainFileSize))
                return FALSE;
            dataSectors[NumDirect - 1] = indirectSector;
        }
    } else {
        indirect = NULL;
        dataSectors[NumDirect - 1] = -1;
    }

    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void
FileHeader::Deallocate(BitMap *freeMap) {
    if (indirect != NULL) indirect->Deallocate(freeMap);
    int directSectors = numSectors;
    if (numSectors > NumDirect - 1) directSectors = NumDirect - 1;
    for (int i = 0; i < directSectors; i++) {
        ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
        freeMap->Clear((int) dataSectors[i]);
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector) {
	// print the sector to fetch from for debug easily
	printf("Fetch from sector %d.\n", sector);
    synchDisk->ReadSector(sector, (char *) this);
    // read from indirect fileheader
    if (dataSectors[NumDirect-1] != -1) {
        indirect = new FileHeader;
        indirect->FetchFrom(dataSectors[NumDirect-1]);
    }
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector) {
    // write back to the indirect fileheader
    if (indirect != NULL) {
        // print the sector to write back for debug easily
        printf("writing back indirect sector %d. \n", dataSectors[NumDirect-1]);
        indirect->WriteBack(dataSectors[NumDirect-1]);
    }
    synchDisk->WriteSector(sector, (char *) this);
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset) {
    int directSector = offset / SectorSize;
    if (directSector >= NumDirect-1) return indirect->ByteToSector(offset - (NumDirect-1)*SectorSize);
	// print this for debuging easily, but there are too much these infos, so I note it
//	printf("ByteToSector is %d, \n", dataSectors[offset / SectorSize]);
    return(dataSectors[directSector]);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength() {
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print() {
    int directSector, directBytes;
    if (numSectors > NumDirect-1) {
        directSector = NumDirect-1;
        directBytes = directSector * SectorSize;
    } else {
        directSector = numSectors;
        directBytes = numBytes;
    }
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < directSector; i++)
        printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");
    for (i = k = 0; i < directSector; i++) {
        synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < directBytes); j++, k++) {
            if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
                printf("%c", data[j]);
            else
                printf("\\%x", (unsigned char)data[j]);
        }
        printf("\n");
    }
    delete [] data;
    if (indirect != NULL) {
        printf("Now print indirect: %d.\n", indirect->numSectors);
        indirect->Print();
    }
}

int FileHeader::Extend(int newFileSize) {
    // nothing needs to change
    if (newFileSize <= numBytes)
        return 0;
    // change fileSize but don't change numSectors
    int newNumSectors = divRoundUp(newFileSize, SectorSize);
    if (newNumSectors == numSectors) {
        numBytes = newFileSize;
        return 1;
    }

    // change fileSize and numSectors
    int appendSectorsNum = newNumSectors - numSectors;
    // bitmap is located in 0 sector
    OpenFile *bitmapFile = new OpenFile(0);
    BitMap *freeMap = new BitMap(NumSectors);
    freeMap->FetchFrom(bitmapFile);
    // similar to Allocate() function
    // if no more space to allocate new sectors, just return -1
    if (freeMap->NumClear() < appendSectorsNum)
        return -1;
    // if indirect is not null, we should just extend indirect fileheader
    if (dataSectors[NumDirect - 1] != -1 && indirect != NULL) {
        int remainFileSize = newFileSize - (NumDirect - 1) * SectorSize;
        indirect->FetchFrom(dataSectors[NumDirect - 1]);
        indirect->Extend(remainFileSize);
    } else {
        int directSectors = newNumSectors;
        if(newNumSectors > NumDirect - 1) directSectors = NumDirect - 1;
        for (int i = numSectors; i < directSectors; i++)
            dataSectors[i] = freeMap->Find();
        if (newNumSectors > NumDirect - 1) {
            int indirectSector = freeMap->Find();
            if (indirectSector == -1) {
                return -1;
            } else {
                indirect = new FileHeader;
                int remainFileSize = newFileSize - directSectors * SectorSize;
                if (!indirect->Allocate(freeMap, remainFileSize))
                    return -1;
                dataSectors[NumDirect - 1] = indirectSector;
                indirect->Extend(remainFileSize);
            }
        } else {
            indirect = NULL;
            dataSectors[NumDirect - 1] = -1;
        }
    }
    numBytes = newFileSize;
    numSectors = newNumSectors;
    return 2;
}
