// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "bitmap.h"
#include "machine.h"
#include "noff.h"

#define UserStackSize        1024    // increase this as necessary!
#define AvailablePages 4  // most pages a user can use

class AddrSpace {
public:
    AddrSpace(OpenFile *executable);    // Create an address space,
    // initializing it with the program
    // stored in the file "executable"
    ~AddrSpace();            // De-allocate an address space

    void InitRegisters();        // Initialize user-level CPU registers,
    // before jumping to user code

    void SaveState();            // Save/restore address space-specific
    void RestoreState();        // info on a context switch

    void Print();

    int getSpaceId() { return spaceId; }

    void setUpTranslation();
    void copy2Mem();
    void Swap(int oldPage,int newPage);
    void FIFO(int faultPageAddr);
    void SecondChance(int faultPageAddr);
    void WriteBack(int oldPage);
    void ReadIn(int newPage);

    NoffHeader noffH;
    OpenFile *executable;
    int virtualPages[AvailablePages];
    OpenFile *virtualSpaceFile;
    char* virtualName;
    BitMap *virtualSpaceMap;
    int firstInPage;

private:
    TranslationEntry *pageTable;    // Assume linear page table translation
    // for now!
    unsigned int numPages;        // Number of pages in the virtual
    // address space
    static BitMap *bitmap;
    static bool spaceIdMap[128];
    int spaceId;
};

#endif // ADDRSPACE_H
