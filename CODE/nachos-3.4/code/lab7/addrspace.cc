// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void
SwapHeader(NoffHeader *noffH) {
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

BitMap *AddrSpace::bitmap = new BitMap(NumPhysPages);
bool AddrSpace::spaceIdMap[128] = { 0 };

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

AddrSpace::AddrSpace(OpenFile *executable) {
    this->executable = executable;
    unsigned int i, size;

//    Init spaceId for current space
    bool flag = false;
    for (int i = 0; i < 128; i++) {
        if (!spaceIdMap[i]) {
            spaceIdMap[i] = true;
            flag = true;
            spaceId = i;
            break;
        }
    }
    ASSERT(flag);

    executable->ReadAt((char *) &noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) &&
        (WordToHost(noffH.noffMagic) == NOFFMAGIC))
        SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size
           + UserStackSize;    // we need to increase the size
    // to leave room for the stack
    numPages = divRoundUp(size, PageSize);
//    size = (numPages + StackPages) * PageSize;
    size = numPages * PageSize;

    char *threadName = currentThread->getName();
    char fileName[50];
    strcpy(fileName, threadName);
    char *fileSuffix = ".va";
    strcat(fileName, fileSuffix);
    virtualName = fileName;
    if (fileSystem->Create(fileName, size)) {
        virtualSpaceFile = fileSystem->Open(fileName);
    } else {
        printf("FileSystem error, check it!\n");
        ASSERT(FALSE);
    }

    ASSERT(numPages <= NumPhysPages);        // check we're not trying
    // to run anything too big --
    // at least until we have
    // virtual memory

    DEBUG('a', "Initializing address space, num pages %d, size %d\n",
          numPages, size);
// first, set up the translation
    setUpTranslation();

// zero out the entire address space, to zero the unitialized data segment 
// and the stack segment
// not zero out the entire address space for multi program
//    bzero(machine->mainMemory, size);

// then, copy in the code and data segments into memory
    copy2Mem();

    Print();
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace() {
    spaceIdMap[spaceId] = false;

    for (int i = 0; i < numPages; ++i) {
        bitmap->Clear(pageTable[i].physicalPage);
    }
    delete[] pageTable;
}

void AddrSpace::setUpTranslation() {
    firstInPage = 0;
    pageTable = new TranslationEntry[numPages];
    for (int i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;    // for now, virtual page # = phys page #
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;  // if the code segment was entirely on
        // a separate page, we could set its
        // pages to be read-only
        pageTable[i].inFileAddr = -1;

        if (i >= numPages - StackPages) {
            pageTable[i].type = userStack;
        }

        // prepare virtual pages the user can use
        if (i < AvailablePages) {
            virtualPages[firstInPage] = pageTable[i].virtualPage;
            firstInPage = (firstInPage + 1) % AvailablePages;
            pageTable[i].physicalPage = bitmap->Find();
            pageTable[i].valid = TRUE;
            pageTable[i].use = TRUE;
        } else {
            pageTable[i].physicalPage = -1;
            pageTable[i].valid = FALSE;
        }
    }
}

void AddrSpace::copy2Mem() {
    if (noffH.code.size > 0) {
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n",
              noffH.code.virtualAddr, noffH.code.size);

        unsigned int numPage = divRoundUp(noffH.code.size,PageSize);
        for (int i = 0; i < numPage; ++i) {
            pageTable[i].inFileAddr = noffH.code.inFileAddr + i * PageSize;
            pageTable[i].type = code;
            if (pageTable[i].valid) {
                executable->ReadAt(&(machine->mainMemory[pageTable[i].physicalPage*PageSize]),
                                   PageSize,
                                   pageTable[i].inFileAddr);
            }
        }
    }

    if (noffH.initData.size > 0) {
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n",
              noffH.initData.virtualAddr, noffH.initData.size);

        unsigned int numPage, starPage;
        numPage = divRoundUp(noffH.initData.size, PageSize);
        starPage = divRoundUp(noffH.code.size,PageSize);
        for (int i = starPage; i < numPage + starPage; ++i) {
            pageTable[i].inFileAddr = noffH.initData.inFileAddr + (i-starPage) * PageSize;
            pageTable[i].type = initData;
            if (pageTable[i].valid) {
                executable->ReadAt(&(machine->mainMemory[pageTable[i].physicalPage*PageSize]),
                                   PageSize,
                                   pageTable[i].inFileAddr);
            }
        }
    }

    if (noffH.uninitData.size > 0) {
        DEBUG('a', "Initializing unInitData segment, at 0x%x, size %d\n",
              noffH.uninitData.virtualAddr, noffH.uninitData.size);

        unsigned int numPage, startPage;
        numPage = divRoundUp(noffH.uninitData.size, PageSize);
        startPage = divRoundUp(noffH.code.size, PageSize) + divRoundUp(noffH.initData.size, PageSize);

        for (int i = startPage; i < numPage + startPage; ++i) {
            pageTable[i].type = uninitData;
        }
    }
}

void AddrSpace::Translate(int addr, unsigned int* vpn, unsigned int* offset){
    int page = -1;
    int off = 0;
    if (addr >= numPages * PageSize - UserStackSize) {
        int userPages = numPages - StackPages;
        page = userPages + (addr - userPages*PageSize) / PageSize;
        off = (addr - userPages * PageSize) % PageSize;
    } else if(noffH.uninitData.size > 0 && addr>=noffH.uninitData.virtualAddr){
        page = divRoundUp(noffH.code.size, PageSize)
               + divRoundUp(noffH.initData.size, PageSize)
               + (addr - noffH.uninitData.virtualAddr) / PageSize;
        off = (addr - noffH.uninitData.virtualAddr) % PageSize;
    } else if(noffH.initData.size > 0 && addr>=noffH.initData.virtualAddr){
        page = divRoundUp(noffH.code.size, PageSize)
               + (addr - noffH.initData.virtualAddr) / PageSize;
        off = (addr - noffH.initData.virtualAddr) % PageSize;
    } else {
        page = addr / PageSize;
        off = addr % PageSize;
    }

    *vpn = page;
    *offset = off;
}

void AddrSpace::FIFO(int faultPageAddr){
    unsigned int oldPage = virtualPages[firstInPage];
    unsigned int newPage, offset;
    Translate(faultPageAddr,&newPage,&offset);
//    newPage = (unsigned) faultPageAddr / PageSize;
//    offset = (unsigned) faultPageAddr % PageSize;
    virtualPages[firstInPage] = newPage;
    firstInPage = (firstInPage + 1) % AvailablePages;

    printf("swap vm page %d:%d==>%d\n",
           pageTable[oldPage].physicalPage,
           pageTable[oldPage].virtualPage,
           pageTable[newPage].virtualPage);
    Swap(oldPage, newPage);
}

void AddrSpace::SecondChance(int faultPageAddr) {
    int oldPageIndex = 0;
    while (pageTable[oldPageIndex].use == TRUE) {
        pageTable[oldPageIndex].use == FALSE;
        oldPageIndex = (oldPageIndex + 1) % AvailablePages;
    }
    unsigned int oldPage = virtualPages[oldPageIndex];
    unsigned int newPage, offset;
    Translate(faultPageAddr,&newPage,&offset);
//    newPage = (unsigned) faultPageAddr / PageSize;
//    offset = (unsigned) faultPageAddr % PageSize;
    virtualPages[firstInPage] = newPage;
    firstInPage = (firstInPage + 1) % AvailablePages;

    printf("swap vm page %d:%d==>%d\n",
           pageTable[oldPage].physicalPage,
           pageTable[oldPage].virtualPage,
           pageTable[newPage].virtualPage);
    Swap(oldPage, newPage);
}

void AddrSpace::Swap(int oldPage,int newPage){
    WriteBack(oldPage);

    pageTable[newPage].physicalPage = pageTable[oldPage].physicalPage;
    pageTable[oldPage].physicalPage = -1;
    pageTable[oldPage].valid = FALSE;
    pageTable[newPage].valid = TRUE;
    pageTable[newPage].use = TRUE;
    pageTable[newPage].dirty = FALSE;

    ReadIn(newPage);
    Print();
}

void AddrSpace::WriteBack(int oldPage){
    if (pageTable[oldPage].dirty) {
        switch (pageTable[oldPage].type) {
            case code:
            case initData:
                executable->WriteAt(&(machine->mainMemory[pageTable[oldPage].physicalPage * PageSize]), PageSize, pageTable[oldPage].inFileAddr);
                break;
            case uninitData:
            case userStack:
                pageTable[oldPage].inFileAddr = (virtualSpaceMap->Find()) * PageSize;
                virtualSpaceFile->WriteAt(&(machine->mainMemory[pageTable[oldPage].physicalPage * PageSize]), PageSize, pageTable[oldPage].inFileAddr);
                break;
            default:
                break;
        }
        pageTable[oldPage].dirty = FALSE;
    }
}

void AddrSpace::ReadIn(int newPage){
    switch(pageTable[newPage].type){
        case code:
        case initData:
            printf("copy from source file pageTable[newPage].inFileAddr:%d===>mainMemory[%d]\n",
                   pageTable[newPage].inFileAddr,
                   pageTable[newPage].physicalPage*PageSize);
            executable->ReadAt(&(machine->mainMemory[pageTable[newPage].physicalPage * PageSize]),
                               PageSize,
                               pageTable[newPage].inFileAddr);
            break;
        case uninitData:
        case userStack:
            if(pageTable[newPage].inFileAddr >= 0){
                printf("copy from swap file pageTable[newPage].inFileAddr:%d===>mainMemory[%d]\n",
                       pageTable[newPage].inFileAddr,
                       pageTable[newPage].physicalPage * PageSize);
                virtualSpaceFile->ReadAt(&(machine->mainMemory[pageTable[newPage].physicalPage * PageSize]),
                                 PageSize,
                                 pageTable[newPage].inFileAddr);
                virtualSpaceMap->Clear(pageTable[newPage].inFileAddr / PageSize);
                pageTable[newPage].inFileAddr = -1;
            } else {
                bzero(machine->mainMemory + pageTable[newPage].physicalPage * PageSize,PageSize);
            }
            break;
    }
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters() {
    int i;

    for (i = 0; i < NumTotalRegs; i++)
        machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

    // Set the stack register to the end of the address space, where we
    // allocated the stack; but subtract off a bit, to make sure we don't
    // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() {}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() {
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}

void AddrSpace::Print() {
    printf("page table dump: %d pages in total\n", numPages);
    printf("============================================\n");
    printf("\tVirtPage, \tPhysPage\n");
    for (int i = 0; i < numPages; i++) {
        printf("\t%d, \t\t%d\n", pageTable[i].virtualPage,
               pageTable[i].physicalPage);
    }
    printf("============================================\n\n"
    );
}
