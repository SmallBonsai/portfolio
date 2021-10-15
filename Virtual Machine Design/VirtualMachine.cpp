#include "VirtualMachine.h"
#include "Machine.h"
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <strings.h>

#define VM_FILE_SYSTEM_ATTR_LONG_NAME_MASK (VM_FILE_SYSTEM_ATTR_READ_ONLY | VM_FILE_SYSTEM_ATTR_HIDDEN | VM_FILE_SYSTEM_ATTR_SYSTEM | VM_FILE_SYSTEM_ATTR_VOLUME_ID | VM_FILE_SYSTEM_ATTR_DIRECTORY | VM_FILE_SYSTEM_ATTR_ARCHIVE)

#define VM_FILE_SYSTEM_ATTR_LONG_NAME (VM_FILE_SYSTEM_ATTR_READ_ONLY | VM_FILE_SYSTEM_ATTR_HIDDEN | VM_FILE_SYSTEM_ATTR_SYSTEM | VM_FILE_SYSTEM_ATTR_VOLUME_ID)

extern "C" {

	TVMMainEntry VMLoadModule(const char *module);
	void VMUnloadModule(void);
	TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
	uint32_t VMStringLength(const char *str);
	void VMStringCopy(char *dest, const char *src);
	void VMStringCopyN(char *dest, const char *src, int32_t n);
	TVMStatus VMDateTime(SVMDateTimeRef curdatetime);

	void alarmCallback(void *calldata);
	void fileOpenCallback(void *calldata, int result);
	void fileCloseCallback(void *calldata, int result);
	void fileWriteCallback(void *calldata, int result);
	void fileSeekCallback(void* calldata, int result);
	void fileReadCallback(void* calldata, int result);

	TVMStatus InternalFileOpen(const char *filename, int flags, int mode, int *filedescriptor);
	TVMStatus InternalFileClose(int filedescriptor);      
	TVMStatus InternalFileRead(int filedescriptor, void *data, int *length);
	TVMStatus InternalFileWrite(int filedescriptor, void *data, int *length);
	TVMStatus InternalFileSeek(int filedescriptor, int offset, int whence, int *newoffset);

	void scheduler();

	void skeleton(void *data);
	void idle(void *param);
	void snapshot();
	void displayBPB(struct BPB* bpb);
	void displayFatInfo(struct FatInfo* curInfo);

	TVMStatus ReadSector(int secNum, void* data);
	TVMStatus WriteSector(int secNum, void* data);
	TVMStatus ReadCluster(int clusterNum, void* data);
	TVMStatus WriteCluster(int clusterNum, void* data);
	int getImageLocation(int clusNum);
	void createFile(struct FileEntry* newFile, char* name, char* entryName);

	void setDateStruct(SVMDateTimeRef dateStruct, uint16_t dateBytes, uint16_t timeBytes);
	void setDateAccess(SVMDateTimeRef dateStruct, uint16_t dateBytes);
	void encodeDateStruct(SVMDateTimeRef dateStruct, uint16_t* dateBytes, uint16_t* timeBytes);

	void makeReady(struct Thread *thread);
	void makeWaiting(struct Thread *thread);
	void removeFromReady(struct Thread * thread);
	void removeFromWaiting(struct Thread * thread);
	void removeFromOwned(struct Thread* thread, TVMMutexID mutex);
	void removeFromWaitingOnMutex(struct Thread *thread);

	static const int NOT_SET = 0;	// constant for if file descriptor has not been set, might be problematic
	static const TVMMemorySize memSectionSize = 512;
	static const TVMMemorySize pageSize = 4096;

	struct fileOpenData {
		struct Thread *thread;
		int* filedescriptor;
	};

	struct fileWriteData {
		struct Thread *thread;
		int* numBytes;
		int sectionIndex;
		int* numCallbacksDone;
		int numCallbacksNeeded;
	};

	struct fileReadData {
		struct Thread *thread;
		int* numBytes;
		int sectionIndex;
		int* numCallbacksDone;
		int numCallbacksNeeded;
	};

	struct fileSeekData {
		struct Thread *thread;
		int* curOffset;
	};

	struct fileCloseData {
		struct Thread *thread;
		int* result;
	};

	struct Thread {
		TVMThreadEntry threadEntry;
		void* parameter;
		TVMMemorySize memsize;
		TVMThreadPriority priority;
		TVMThreadID tid;
		TVMThreadState state;
		SMachineContext context;
		void* stack;
		int sleepDuration;
		int timeoutDuration;
		std::queue<TVMMutexID> mutexesOwned;
	};

	struct Mutex {
		TVMThreadID owner;
		bool unlocked;
		TVMMutexID id;
		std::queue<struct Thread*> waitingMutex;
	};


	struct SharedMemorySection {
		bool unlocked;
		void *startOfSection;
		unsigned int sectionID;
		int bytesUsed;
	};

	struct FatInfo {
		unsigned int FirstRootSector;
		unsigned int RootDirectorySectors;
		unsigned int FirstDataSector;
		unsigned int ClusterCount;
	};

	#pragma pack(1)
	struct BPB {
		uint8_t jmpBoot[3]; //unused
		uint8_t OEMName[8]; //unused
		uint16_t BytesPerSec; //2 bytes
		uint8_t SecPerClus; //1 byte
		uint16_t ResvdSecCount;
		uint8_t NumFATs;
		uint16_t RootEntCnt;
		uint16_t TotSec16;
		uint8_t Media;
		uint16_t FATSz16;
		uint16_t SecPerTrk;
		uint16_t NumHeads;
		uint32_t HiddSec;	//unused
		uint32_t TotSec32;	//unused
		uint8_t DrvNum;
		uint8_t Reserved1;
		uint8_t BootSig;
		uint32_t VolID;		//unused
		uint8_t VolLab[11]; //unused
		uint8_t FilSysType[8];
	};
	#pragma pack()

	struct DirectoryEntry {
		SVMDirectoryEntryRef entry;
		uint16_t FstClusLO; // index for FATTable
		int entryNum;
	};

	struct FileEntry {
		int fileDescriptor;
		char path[100];	// will want to memCpy path and name 
		char name[20];
		//int curLocation;
		int curOffset;
		int curCluster;
		int flags;
		struct DirectoryEntry * rootEntry;
	};

	volatile static TVMTick curTicks;
	volatile static int interval;
	static unsigned char* sharedMemoryStart;
	static struct Thread *curThread = new struct Thread;
	static struct Thread *mainThread = new struct Thread;
	static TVMThreadID idleThread;

	static struct BPB *bpb;
	static struct FatInfo *fatInformation = new struct FatInfo;
	static std::vector<uint16_t> fatTable;
	static std::vector<struct DirectoryEntry*> rootDirectories;
	static std::vector<uint8_t> rootData;
	static std::vector<struct FileEntry*> openFiles;
 	static int curFD = 3;
 	static int fatFileDescriptor;
 	static bool rootOpen = false;
 	static int curDirectoryDescriptor = 0;

	std::queue<struct Thread*> readyIDLEThreads;
	std::queue<struct Thread*> readyLowThreads;
	std::queue<struct Thread*> readyNormalThreads;
	std::queue<struct Thread*> readyHighThreads;
	std::queue<struct Thread*> waitingThreads;
	std::queue<struct Thread*> waitingOnMutex;
	std::queue<struct Thread*> waitingOnMemory;

	std::map<TVMThreadID, struct Thread*> allThreads;
	std::map<TVMMutexID, struct Mutex*> allMutexes;

	std::vector<struct SharedMemorySection*> sharedMemory;
	std::queue<struct SharedMemorySection*> availableMemorySection;

	void snapshot() {
		std::cout << "CurThread ID" << std::endl;
		std::cout << curThread->tid << std::endl;

		std::cout << "" << std::endl;

		std::cout << "In Low prio: " << std::endl;
		int numLoops = readyLowThreads.size();
				for (int i = 0; i < numLoops; i++) {
					struct Thread *front = readyLowThreads.front();
					readyLowThreads.pop();
					std::cout << "Thread in Low" << std::endl;
					std::cout << front->tid << std::endl;
					std::cout << "Low Thread Status" << std::endl;
					std::cout << front->state << std::endl;
					readyLowThreads.push(front);
				}


		std::cout << "In Normal prio: " << std::endl;
		int numNormalLoops = readyNormalThreads.size();
				for (int i = 0; i < numNormalLoops; i++) {
					struct Thread *front = readyNormalThreads.front();
					readyNormalThreads.pop();
					std::cout << "Thread in Normal" << std::endl;
					std::cout << front->tid << std::endl;
					std::cout << "Normal Thread Status" << std::endl;
					std::cout << front->state << std::endl;
					readyNormalThreads.push(front);
				}

		std::cout << "In High Prio: " << std::endl;
		int numHighLoops = readyHighThreads.size();
		for (int i = 0; i < numHighLoops; i++) {
					struct Thread *front = readyHighThreads.front();
					readyHighThreads.pop();
					std::cout << "Thread in High" << std::endl;
					std::cout << front->tid << std::endl;
					std::cout << "High Thread Status" << std::endl;
					std::cout << front->state << std::endl;
					readyHighThreads.push(front);
				}

		std::cout << "In Waiting" << std::endl;
		int numWaitingLoops = waitingThreads.size();
		for(int i = 0; i < numWaitingLoops; i++) {
			struct Thread *front = waitingThreads.front();
					waitingThreads.pop();
					std::cout << "Thread in Waiting" << std::endl;
					std::cout << front->tid << std::endl;
					std::cout << "Waiting Thread Status" << std::endl;
					std::cout << front->state << std::endl;
					readyHighThreads.push(front);
		}

	}

	void scheduler () {

		struct Thread *nextThread = NULL;

		if(!waitingOnMemory.empty() && !availableMemorySection.empty()) {
			nextThread = waitingOnMemory.front();
			waitingOnMemory.pop();
			nextThread->state = VM_THREAD_STATE_READY;
		} else if (!readyHighThreads.empty()) {
			nextThread = readyHighThreads.front(); // get NextThread, will want to check priority in future
			readyHighThreads.pop();				
		} else if (!readyNormalThreads.empty()) {
			nextThread = readyNormalThreads.front(); // get NextThread, will want to check priority in future
			readyNormalThreads.pop();
		} else if (!readyLowThreads.empty()) {
			nextThread = readyLowThreads.front(); // get NextThread, will want to check priority in future
			readyLowThreads.pop();
		} else {
			if (curThread->state != VM_THREAD_STATE_RUNNING) {
				nextThread = readyIDLEThreads.front(); // get NextThread, will want to check priority in future
				readyIDLEThreads.pop();
			}
		}
		// if decide on a new thread, will change context
		if (nextThread) {

			if(curThread->priority > nextThread->priority && curThread->state == VM_THREAD_STATE_RUNNING) {
				makeReady(nextThread);
			} else {
				// need to check here if need to push the nextThread back on to its ready queue or not
				if (curThread->state == VM_THREAD_STATE_RUNNING) {
					makeReady(curThread);
				}

				//switch machine context
				SMachineContextRef prevContextRef = &(curThread->context);
				curThread = nextThread;
				nextThread->state = VM_THREAD_STATE_RUNNING;
				removeFromReady(nextThread);
				MachineContextSwitch(prevContextRef, &(nextThread->context));
			}

		}

	}

	void idle(void* param) {
		MachineEnableSignals();
		while(1){};
	}

	void makeReady(struct Thread *thread) {
		thread->state = VM_THREAD_STATE_READY;

		if (thread->priority == 0x00) {
			readyIDLEThreads.push(thread);
		}	else if (thread->priority == VM_THREAD_PRIORITY_LOW) {
			readyLowThreads.push(thread);
		} else if (thread->priority == VM_THREAD_PRIORITY_NORMAL) {
			readyNormalThreads.push(thread);
		} else if (thread->priority == VM_THREAD_PRIORITY_HIGH)	{
			readyHighThreads.push(thread);
		}

	}

	void makeWaiting(struct Thread *thread) {

		if (thread->state == VM_THREAD_STATE_READY) {
			removeFromReady(thread);
		}

		thread->state = VM_THREAD_STATE_WAITING;
		waitingThreads.push(thread);

	}

	void removeFromReady(struct Thread *thread) {
		 if (thread->priority == VM_THREAD_PRIORITY_LOW) {
				int numLoops = readyLowThreads.size();
				for (int i = 0; i < numLoops; i++) {
					struct Thread *front = readyLowThreads.front();
					readyLowThreads.pop();
					if(front->tid == thread->tid) {
						//do no push,
					} else {
						readyLowThreads.push(front);
					}
				}
			} else if (thread->priority == VM_THREAD_PRIORITY_NORMAL) {
					int numLoops = readyNormalThreads.size();
					for (int i = 0; i < numLoops; i++) {
						struct Thread *front = readyNormalThreads.front();
						readyNormalThreads.pop();
						if(front->tid == thread->tid) {
							//do no push,
						} else {
							readyNormalThreads.push(front);
						}
					}
			} else if (thread->priority == VM_THREAD_PRIORITY_HIGH) {
					int numLoops = readyHighThreads.size();
					for (int i = 0; i < numLoops; i++) {
						struct Thread *front = readyHighThreads.front();
						readyHighThreads.pop();
						if(front->tid == thread->tid) {
							//do no push,
						} else {
							readyHighThreads.push(front);
						}
					}
			}
	}

	void removeFromWaiting(struct Thread *thread) {
		int numLoops = waitingThreads.size();
		for (int i = 0; i < numLoops; i++) {
			struct Thread *front = waitingThreads.front();
			waitingThreads.pop();
			if(front->tid == thread->tid) {
				//do no push,
			} else {
				waitingThreads.push(front);
			}
		}
	}

	void removeFromOwned(struct Thread* thread, TVMMutexID mutex) {
		int numLoops = thread->mutexesOwned.size();
		for (int i = 0; i < numLoops; i++) {
			TVMMutexID front = thread->mutexesOwned.front();
			thread->mutexesOwned.pop();
			if(front == mutex) {
				//do no push,
			} else {
				thread->mutexesOwned.push(front);
			}
		}
	}

	void removeFromWaitingOnMutex(struct Thread *thread) {
		int numLoops = waitingOnMutex.size();
		for (int i = 0; i < numLoops; i++) {
			struct Thread *front = waitingOnMutex.front();
			waitingOnMutex.pop();
			if(front->tid == thread->tid) {
				//do no push,
			} else {
				waitingOnMutex.push(front);
			}
		}
	}

	void displayBPB(struct BPB *bpb) {
		std::ios  state(NULL);
		state.copyfmt(std::cout);
		std::cout << "In BPB" << std::endl;
		//std::cout << std::hex << *(bpb->jmpBoot) << std::endl;
		std::cout << bpb->OEMName << std::endl;
		std::cout << "BytesPerSec " << (int)bpb->BytesPerSec << std::endl;
		std::cout << "SecPerClus " << (int)bpb->SecPerClus << std::endl;
		std::cout << "ResvdSecCount " << (int)bpb->ResvdSecCount << std::endl;
		std::cout << "NumFATs " << (int)bpb->NumFATs << std::endl;
		std::cout << "RootEntCnt " << (int)bpb->RootEntCnt << std::endl;
		std::cout << "TotSec16 " << (int)bpb->TotSec16 << std::endl;
		std::cout << "Media " << (int)bpb->Media << std::endl;
		std::cout << "FATSz16 " << (int)bpb->FATSz16 << std::endl;
		std::cout << "SecPerTrk " << (int)bpb->SecPerTrk << std::endl;
		std::cout << "NumHeads " << (int)bpb->NumHeads << std::endl;
		std::cout << "HiddSec " << (int)bpb->HiddSec << std::endl;
		std::cout << "TotSec32 " << (int)bpb->TotSec32 << std::endl;
		std::cout << "DrvNum " << (int)bpb->DrvNum << std::endl;
		std::cout << "Reserved1 " << (int)bpb->Reserved1 << std::endl;
		std::cout << "BootSig " << (int)bpb->BootSig << std::endl;
		std::cout << "VolID " << std::hex << bpb->VolID << std::endl;
		std::cout << "VolLab " << bpb->VolLab << std::endl;
		std::cout << "FilSysType " << (char*)bpb->FilSysType << std::endl;
		std::cout.copyfmt(state);
	}

	void displayFatInfo(struct FatInfo *curInfo) {
		std::ios  state(NULL);
		state.copyfmt(std::cout);

		std::cout << "pointer to fatInformation " << curInfo << std::endl;
		std::cout << "RootDirectorySectors " << curInfo->RootDirectorySectors << std::endl;
		std::cout << "FirstRootSector " << curInfo->FirstRootSector << std::endl;
		std::cout << "FirstDataSector " << curInfo->FirstDataSector << std::endl;
		std::cout << "ClusterCount " << curInfo->ClusterCount << std::endl;
		std::cout.copyfmt(state);
	}

	void displayEntry(struct DirectoryEntry *dirEntry) {
		std::ios  state(NULL);
		state.copyfmt(std::cout);

		  //   char DLongFileName[VM_FILE_SYSTEM_MAX_PATH];
    // char DShortFileName[VM_FILE_SYSTEM_SFN_SIZE];
    // unsigned int DSize;
    // unsigned char DAttributes;
    // SVMDateTime DCreate;
    // SVMDateTime DAccess;
    // SVMDateTime DModify;

		std::cout << "ShortFileName: " << dirEntry->entry->DShortFileName << std::endl;
		std::cout << "DSize: " << dirEntry->entry->DSize << std::endl;
		std::cout << "DAttributes: " << dirEntry->entry->DAttributes << std::endl;
		std::cout << "Created Date: " << (int)dirEntry->entry->DCreate.DMonth << "/" << (int)dirEntry->entry->DCreate.DDay << "/" << (int)dirEntry->entry->DCreate.DYear << std::endl;
	 	std::cout << "Created Time: " << (int)dirEntry->entry->DCreate.DHour << ":" << (int)dirEntry->entry->DCreate.DMinute << ":" << (int)dirEntry->entry->DCreate.DSecond << std::endl;
	 	std::cout << "Modified Date: " << (int)dirEntry->entry->DModify.DMonth << "/" << (int)dirEntry->entry->DModify.DDay << "/" << (int)dirEntry->entry->DModify.DYear << std::endl;
	 	std::cout << "Modified Time: " << (int)dirEntry->entry->DModify.DHour << ":" << (int)dirEntry->entry->DModify.DMinute << ":" << (int)dirEntry->entry->DModify.DSecond << std::endl;
		std::cout << "Accessed Date: " << (int)dirEntry->entry->DAccess.DMonth << "/" << (int)dirEntry->entry->DAccess.DDay << "/" << (int)dirEntry->entry->DAccess.DYear << std::endl;

		std::cout.copyfmt(state);
	}
 
 	void displayFile(struct FileEntry *fileEntry) {
 		std::ios  state(NULL);
		state.copyfmt(std::cout);

		// int fileDescriptor;
		// char* path;	// will want to memCpy path and name 
		// char* name;
		// //int curLocation;
		// int curOffset;
		// int curCluster;
		// int flags;

		displayEntry(fileEntry->rootEntry);

		std::cout << "Filedescriptor: " << fileEntry->fileDescriptor << std::endl;
		std::cout << "Path: " << (char*)fileEntry->path << std::endl;
		std::cout << "Name: " << (char*)fileEntry->name << std::endl;
		std::cout << "curOffset: " << fileEntry->curOffset << std::endl;
		std::cout << "curCluster" << std::hex << fileEntry->curCluster << std::endl;
		std::cout.copyfmt(state);

		std::cout << "flags" << fileEntry->flags << std::endl;
 	}

	TVMStatus VMStart(int tickms, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]) {
		curTicks = 0;	// set curNum of Ticks
		interval = tickms;	// set curNum of Milliseconds between Ticks

		TVMMainEntry entryPoint = VMLoadModule(argv[0]);

		if (entryPoint) {
			// starting point = MachineIntialize(sharedsize);
				sharedMemoryStart = (unsigned char*)MachineInitialize(sharedsize);
				//calculate number of 512 shared memory sections
				int pages = sharedsize/pageSize;
				int leftover = sharedsize % pageSize;
				if(leftover != 0) {
					pages += 1;
				}

				int numSections = (pages * pageSize)/memSectionSize;

				//create shared memory sections and add to vector
				for (int i = 0; i < numSections; i++) {
					struct SharedMemorySection* newSection = new SharedMemorySection;
					newSection->unlocked = true;
					newSection->startOfSection = sharedMemoryStart + (memSectionSize * i);
					newSection->sectionID = i;
					sharedMemory.push_back(newSection);
					availableMemorySection.push(newSection);
				}

				MachineEnableSignals();

				// set Idle Thread and put onto ready
				// third argument is memsize
				VMThreadCreate(&idle, NULL, 0x100000, ((TVMThreadPriority)0x00), &idleThread);
				VMThreadActivate(idleThread);

				mainThread->priority = VM_THREAD_PRIORITY_NORMAL;	//for bookkeeping later
				mainThread->state = VM_THREAD_STATE_RUNNING;
				mainThread->tid = 1;
				allThreads[mainThread->tid] = mainThread;

				curThread = mainThread;

				//Request for alarm at millisecond duration (convert to useconds_t)
				useconds_t tickDurationUS = tickms * 1000;
				MachineRequestAlarm(tickDurationUS, &alarmCallback, NULL);

				// create vector with 512 uint_8 entries for each byte of the BPB, read in directly after into BPB
				InternalFileOpen(mount, O_RDWR, 0600, &fatFileDescriptor);
				uint8_t BPBbuffer[512];

				ReadSector(0, BPBbuffer);
				bpb = (struct BPB*)BPBbuffer; // put BPB info into struct

				fatInformation->FirstRootSector = bpb->ResvdSecCount + (bpb->NumFATs * bpb->FATSz16);
				fatInformation->RootDirectorySectors = (bpb->RootEntCnt * 32) / 512;
				fatInformation->FirstDataSector = fatInformation->FirstRootSector + fatInformation->RootDirectorySectors;
				fatInformation->ClusterCount = (bpb->TotSec32 - fatInformation->FirstDataSector) / bpb->SecPerClus;

				//displayBPB(bpb);
				//displayFatInfo(fatInformation);

				//read in FAT (will be number of sectors in Fat = BPB_FatSz16, copy into FAT)
				int numEntries = bpb->FATSz16 * 256; // had originally * 256
				//uint16_t fatTable[numEntries];
				fatTable.resize(numEntries);

				for(int i = 0; i < bpb->FATSz16; i++) {
					int secNum = 1 + i;
					uint8_t fatTableSector[512];
					ReadSector(secNum, fatTableSector);
					int index = 256 * i;
					memcpy(&fatTable[index], fatTableSector, 512);
				}

				// for (int i = 0; i < numEntries; i++) {
				// 	std::ios  state(NULL);
				//  	state.copyfmt(std::cout);
				//  	std::cout << std::hex << fatTable[i] << " " << std::endl;
				//  	std::cout.copyfmt(state);
				//  }

				// read in root directory, for each file (32 bytes), read in info, then store in rootDirectories
				int rootSize = fatInformation->RootDirectorySectors * 512;
				rootData.resize(rootSize);
				for(unsigned int i = 0; i < fatInformation->RootDirectorySectors; i++) {
					int secNum = fatInformation->FirstRootSector + i;
					uint8_t rootSector[512];
					ReadSector(secNum, rootSector);
					int index = 512 * i;
					memcpy(&rootData[index], rootSector, 512);
				}
				// read in info, store in rootDirectories
				for(int i = 0; i < rootSize; i += 32) {
					if (rootData[i] != 0x00) { //see if empty file !!!!! MAY NOT BE CORRECT MAY NEED TO BE MORE SPECIFIC!!!!!!
						//read into a Directory struct
						//check if LongEntry

						struct DirectoryEntry* fullEntry = new struct DirectoryEntry;
						SVMDirectoryEntryRef newEntry = new SVMDirectoryEntry;
						memcpy(&(newEntry->DAttributes), &rootData[i+11], 1);

						if ((newEntry->DAttributes & VM_FILE_SYSTEM_ATTR_LONG_NAME_MASK) == VM_FILE_SYSTEM_ATTR_LONG_NAME) {
							continue;
						} else {
							// need to put in bytes here... with bitwise operators
							// bitFunction(&(newEntry->DCreate), 2 bytes)
							// bitFunction(DVMDateREf, bytes)
							fullEntry->entryNum = (int)(i / 32);
							memcpy(&(newEntry->DShortFileName), &rootData[i], 11);
							// Not passing right thing
							uint16_t* dateCrtData = (uint16_t*)&(rootData[i+16]);
							uint16_t* timeCrtData = (uint16_t*)&(rootData[i+14]);
							setDateStruct(&(newEntry->DCreate), *dateCrtData, *timeCrtData);

							uint16_t* dateModData = (uint16_t*)&(rootData[i+24]);
							uint16_t* timeModData = (uint16_t*)&(rootData[i+22]);
							setDateStruct(&(newEntry->DModify), *dateModData, *timeModData);

							uint16_t* dateAccData = (uint16_t*)&(rootData[i+18]);
							setDateAccess(&(newEntry->DAccess), *dateAccData);
							//FstClusLO
							memcpy(&(newEntry->DSize), &rootData[i+28], 4);
							fullEntry->entry = newEntry;
							memcpy(&(fullEntry->FstClusLO), &rootData[i+26], 2);
							//add to list of directories
							rootDirectories.push_back(fullEntry);
						}

					} else {
						break;
					}
				}

				openFiles.push_back(NULL);	// set openFiles, 0, 1, 2 all to NULL (not applicable)
				openFiles.push_back(NULL);
				openFiles.push_back(NULL);

				// for (int i =0; i < rootDirectories.size(); i++) {
				// 	std::cout << "FileName: " << rootDirectories[i]->entry->DShortFileName << std::endl;
				// 	std::cout << "FileSize: " << rootDirectories[i]->entry->DSize << std::endl;
				// 	std::cout << "Created Date: " << (int)rootDirectories[i]->entry->DCreate.DMonth << "/" << (int)rootDirectories[i]->entry->DCreate.DDay << "/" << (int)rootDirectories[i]->entry->DCreate.DYear << std::endl;
				// 	std::cout << "Created Time: " << (int)rootDirectories[i]->entry->DCreate.DHour << ":" << (int)rootDirectories[i]->entry->DCreate.DMinute << ":" << (int)rootDirectories[i]->entry->DCreate.DSecond << std::endl;
				// 	std::cout << "FstClusLO: " << rootDirectories[i]->FstClusLO << std::endl;
				// }

				(*entryPoint)(argc, argv);

				InternalFileClose(fatFileDescriptor);
				MachineTerminate();
				VMUnloadModule();
		} else {
			return VM_STATUS_FAILURE;
		}

		return VM_STATUS_SUCCESS;

	}	

	TVMStatus VMTickMS(int *tickmsref) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (tickmsref) {	// location exists
			*tickmsref = interval;
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMTickCount(TVMTickRef tickref) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (tickref) {
			*tickref = curTicks;
			MachineResumeSignals(&sigstate);
			return VM_STATUS_SUCCESS;
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	void alarmCallback(void *calldata) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		curTicks += 1;	// add tick

		// those waiting on mutex, deduct from their timeout
		int numLoopsMutex = waitingOnMutex.size();
		for (int i = 0; i < numLoopsMutex; i++) {
			struct Thread *waiting = waitingOnMutex.front();
			waitingOnMutex.pop();

			if (waiting->timeoutDuration > 0) {
				waiting->timeoutDuration -= 1;
				// sleep for a tick
				if (waiting->timeoutDuration == 0) {
					// reached duration of timeout, make ready to try one last acquire
					removeFromWaitingOnMutex(waiting);
					// need to remove from the mutexes waiting list
					makeReady(waiting);
				} else {
					// hasn't been woken, still waiting on mutex
					waitingOnMutex.push(waiting);
				}

			} else {
				// if waiting indefinitely, just push back on
				waitingOnMutex.push(waiting);
			}

		}
		// if timeout gets reached, remove from waiting on mutex queue, check to see if can acquire again


		// deduct 1 to duration for those that are sleeping
		int numLoops = waitingThreads.size();
		for (int i = 0; i < numLoops; i++) {
			struct Thread *asleep = waitingThreads.front();
			waitingThreads.pop();


			if (asleep->sleepDuration > 0) {
				asleep->sleepDuration -= 1;
				// sleep for a tick
				if (asleep->sleepDuration == 0) {
					// is asleep and has awoken
					makeReady(asleep);
				} else {
					// is asleep but isn't done sleeping
					waitingThreads.push(asleep);
				}

			} else {
				// if not asleep but is waiting, push back onto waiting list
				waitingThreads.push(asleep);
			}

		}
		
		scheduler();

		MachineResumeSignals(&sigstate);

	
	}

	TVMStatus VMThreadSleep(TVMTick tick) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if (tick == VM_TIMEOUT_INFINITE) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		} else if (tick == VM_TIMEOUT_IMMEDIATE) {
			// yield rest of time to process of next highest priority
			MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;

		}
		else {
			makeWaiting(curThread);
			curThread->sleepDuration = tick;
			scheduler();	// Not sure if need to call here since scheduler is called every tick

			MachineResumeSignals(&sigstate);
			return VM_STATUS_SUCCESS;
		}	
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if(tid && entry) {
			// make new thread
			struct Thread *newThread = new struct Thread;
			newThread->threadEntry = entry;
			newThread->parameter = param;
			newThread->memsize = memsize;
			newThread->priority = prio;
			newThread->tid = allThreads.size();
			*tid = newThread->tid;
			newThread->state = VM_THREAD_STATE_DEAD;

			if (allThreads.find(*tid) == allThreads.end()) {
				allThreads[*tid] = newThread;	// add to all threads if not found already
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_PARAMETER;	// ID has already been used
			}

		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref) {
		TMachineSignalState sigstate;
		if (stateref) {	
			MachineSuspendSignals(&sigstate);		// suspend and resume here to limit scope
			if (allThreads.find(thread) == allThreads.end()) {
				MachineResumeSignals(&sigstate);	//stateref is valid but thread does not exist
				return VM_STATUS_ERROR_INVALID_ID;
			} else {
				struct Thread *foundThread = allThreads.at(thread);
				*stateref = foundThread->state;		//stateref is valid and thread exists
			}
			MachineResumeSignals(&sigstate);
		} else {
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadActivate(TVMThreadID thread) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (allThreads.find(thread) == allThreads.end()) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
			struct Thread *foundThread = allThreads.at(thread);
			if (foundThread->state == VM_THREAD_STATE_DEAD) {
				// put into ready state change curThread state to READY
				makeReady(foundThread);

				// activate thread (create machine context)
				if (foundThread->memsize  == 0) {
					foundThread->stack = NULL;
				} else {
					foundThread->stack = malloc(foundThread->memsize);
				}

				MachineContextCreate(&(foundThread->context), &skeleton, foundThread->parameter, foundThread->stack, foundThread->memsize);
				if(foundThread->priority > curThread->priority) {
					scheduler();
				}

			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadID(TVMThreadIDRef threadref) {
		TMachineSignalState sigstate;
		if (threadref) {
			MachineSuspendSignals(&sigstate);	// limit scope that needs to suspend
			*threadref = curThread->tid;
			MachineResumeSignals(&sigstate);
		} else {
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMThreadTerminate(TVMThreadID thread) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (allThreads.find(thread) == allThreads.end()) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
				struct Thread *foundThread = allThreads.at(thread);
				if (foundThread->state == VM_THREAD_STATE_DEAD) { //if thread is already dead
					MachineResumeSignals(&sigstate);
					return VM_STATUS_ERROR_INVALID_STATE;
				} else {
					if (foundThread->state == VM_THREAD_STATE_READY) {
						removeFromReady(foundThread);
					} else if (foundThread->state == VM_THREAD_STATE_WAITING) {
						removeFromWaiting(foundThread);
					}

					foundThread->state = VM_THREAD_STATE_DEAD;	// change state to dead, may not be applicable for IDLE

					// release any mutexes that are owned
					if(!foundThread->mutexesOwned.empty()) {
						int numLoops = foundThread->mutexesOwned.size();
						for (int i = 0; i < numLoops; i++) {
							//remove from owned list
							TVMMutexID ownedMutex = foundThread->mutexesOwned.front();
							foundThread->mutexesOwned.pop();


							struct Mutex *foundMutex = allMutexes.at(ownedMutex);

							//RELEASE
							if (foundMutex->waitingMutex.empty()) {
								foundMutex->owner = -1; // no owner, need placeholder
								foundMutex->unlocked = true;

							} else {
								struct Thread* nextOwner = foundMutex->waitingMutex.front();	// once released, get next owner
								foundMutex->waitingMutex.pop();
								removeFromWaitingOnMutex(nextOwner);
								nextOwner->mutexesOwned.push(foundMutex->id);
								foundMutex->owner = nextOwner->tid;
								makeReady(nextOwner);
							}
						}
					}

					if (foundThread->tid == curThread->tid) { // if thread that is being terminated is current thread
						scheduler();	// schedule new thread
					}
					MachineResumeSignals(&sigstate);
					return VM_STATUS_SUCCESS;
				}	
			}	
	}

	TVMStatus VMThreadDelete(TVMThreadID thread) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if (allThreads.find(thread) == allThreads.end()) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
			struct Thread *foundThread = allThreads.at(thread);
			if (foundThread->state == VM_THREAD_STATE_DEAD) {
				free(foundThread->stack);
				allThreads.erase(thread);
				MachineResumeSignals(&sigstate);
				return VM_STATUS_SUCCESS;
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_STATE;
			}
		}

	}

 void fileOpenCallback(void* calldata, int result) {
 		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		// put result in file descriptor
 		struct fileOpenData *data = (struct fileOpenData *)calldata;
		*(data->filedescriptor) = result;
		struct Thread *thread = data->thread;
		removeFromWaiting(thread);
		makeReady(thread);

		MachineResumeSignals(&sigstate);	
	}

	TVMStatus InternalFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (filename && filedescriptor) {
			*filedescriptor = NOT_SET;
			struct fileOpenData *fileData = new struct fileOpenData;
			fileData->thread = curThread;
			fileData->filedescriptor = filedescriptor;

			MachineFileOpen(filename, flags, mode, &fileOpenCallback, fileData);
			makeWaiting(curThread);
			curThread->sleepDuration = -1;
			scheduler();

			delete fileData;

			if (*filedescriptor < 0) {
				MachineResumeSignals(&sigstate);	
				return VM_STATUS_FAILURE;
			} else {
				MachineResumeSignals(&sigstate);	
				return VM_STATUS_SUCCESS;
			}
		} else {
			MachineResumeSignals(&sigstate);	
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	void fileCloseCallback(void* calldata, int result) {
 		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

 		struct fileCloseData *data = (struct fileCloseData *)calldata;
		*(data->result) = result;
		struct Thread *thread = data->thread;

		removeFromWaiting(thread);
		makeReady(thread);

		MachineResumeSignals(&sigstate);	
	}	

	TVMStatus InternalFileClose(int filedescriptor) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		int result = 10; // hasn't been closed 
		struct fileCloseData *fileData = new struct fileCloseData;
		fileData->thread = curThread;
		fileData->result = &result;


		MachineFileClose(filedescriptor, &fileCloseCallback, fileData);
		makeWaiting(curThread);
		curThread->sleepDuration = -1;
		scheduler();

		while(result == 10) {	// wait until fileDescriptor has been set
		}

		delete fileData;

		if (result < 0) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_FAILURE;
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	void fileWriteCallback(void* calldata, int result) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

 		struct fileWriteData *data = (struct fileWriteData *)calldata;
		*(data->numBytes) = result;

		struct Thread *thread = data->thread;
		*(data->numCallbacksDone) += 1;

		removeFromWaiting(thread);
		makeReady(thread);

		MachineResumeSignals(&sigstate);	
	}

	TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		
		if(filedescriptor < 3) {
			InternalFileWrite(filedescriptor, data, length);
		} else {
			if(openFiles[filedescriptor]) {
				struct FileEntry* curFile = openFiles[filedescriptor];
				//std::cout << "finds file is open" << std::endl;
				//std::cout << "flags " << curFile->flags << std::endl;
				if ((curFile->flags & O_ACCMODE) > 0) {

					uint8_t writeBuffer[bpb->SecPerClus * 512];
					memcpy(&(writeBuffer[(curFile->curOffset)]), data, *length);
					WriteCluster(curFile->curCluster, writeBuffer);

					curFile->rootEntry->entry->DSize += *length;
					curFile->curOffset += *length;

					//std::cout << "offset in file" << curFile->curOffset << std::endl;
					//std::cout << "Entry Num" << curFile->rootEntry->entryNum << std::endl;

					//change date modified in the entry
					VMDateTime(&(curFile->rootEntry->entry->DModify));

					uint16_t dateModified = 0;
					uint16_t timeModified = 0; //placeholder
					encodeDateStruct(&(curFile->rootEntry->entry->DModify), &dateModified, &timeModified);

					uint8_t sectorBuffer[512];
					uint8_t entryArray[32];
					int bufferIndex = (32 * curFile->rootEntry->entryNum);

					ReadSector(fatInformation->FirstRootSector, sectorBuffer);

					memcpy(entryArray, &(sectorBuffer[bufferIndex]), 32);
					memcpy(&(entryArray[22]), &timeModified, 2);
					memcpy(&(entryArray[24]), &dateModified, 2);
					memcpy(&(entryArray[28]), &(curFile->rootEntry->entry->DSize), 4);
					// for future may not want 
					
					memcpy(&(sectorBuffer[bufferIndex]), entryArray, 32);
					WriteSector(fatInformation->FirstRootSector, sectorBuffer);


				} else {
					MachineResumeSignals(&sigstate);
					return VM_STATUS_FAILURE;
				}

			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			}
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	void fileReadCallback(void* calldata, int result) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

 		struct fileReadData *data = (struct fileReadData *)calldata;
		*(data->numBytes) = result;
		struct Thread *thread = data->thread;

		*(data->numCallbacksDone) += 1;

		removeFromWaiting(thread);
		makeReady(thread);

		MachineResumeSignals(&sigstate);	
	}

	TVMStatus InternalFileRead(int filedescriptor, void *data, int *length) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (data && length) {
			int bytesRead = 0;
			int callbacksReturned = 0;
			//find out how many sections you need to acquire
			int numSectionsNeeded = (int)(*length / memSectionSize);

			int remainderBytes = *length % memSectionSize;
			if(remainderBytes != 0) {
				numSectionsNeeded += 1;
			}
			int numSectionsAcquired = 0;

			unsigned char *fileStart = (unsigned char*)data;

			int numAvailable = availableMemorySection.size();
			for(int i = 0; i < numSectionsNeeded; i++) {
				if(numSectionsAcquired >= numSectionsNeeded) {
					// done with all sections needed
					break;
				} else {
						if (numAvailable > 0) {
							struct SharedMemorySection *firstAvailable = availableMemorySection.front();
							availableMemorySection.pop();

							firstAvailable->unlocked = false;
							numSectionsAcquired += 1;

							struct fileReadData *fileData = new struct fileReadData;
							fileData->thread = curThread;
							fileData->numBytes = &bytesRead;
							fileData->numCallbacksDone = &callbacksReturned;
							fileData->numCallbacksNeeded = numSectionsNeeded;


							if (numSectionsAcquired == numSectionsNeeded && remainderBytes != 0) {	// last section, length to write is remainder (max of 512)
								firstAvailable->bytesUsed = remainderBytes;
							} else {
								firstAvailable->bytesUsed = memSectionSize; // full section, length to write is 512
							}

							MachineFileRead(filedescriptor, firstAvailable->startOfSection, firstAvailable->bytesUsed, &fileReadCallback, fileData);	

							makeWaiting(curThread);
							curThread->sleepDuration = -1;
							scheduler();

							memcpy(fileStart, firstAvailable->startOfSection, firstAvailable->bytesUsed);
							fileStart = fileStart + firstAvailable->bytesUsed;

							firstAvailable->unlocked = true;
							availableMemorySection.push(firstAvailable);

							delete fileData;

							if (bytesRead < 0) {
								MachineResumeSignals(&sigstate);
								return VM_STATUS_FAILURE;
							}
						} else {
							waitingOnMemory.push(curThread);
							curThread->sleepDuration = -1;
							curThread->state = VM_THREAD_STATE_WAITING;
							scheduler();
						}


					}
				}

			*length = bytesRead;
			
			if (bytesRead < 0) {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_SUCCESS;
			}
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	void fileSeekCallback(void* calldata, int result) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		struct fileSeekData *data = (struct fileSeekData *)calldata;
		*(data->curOffset) = result;
		struct Thread *thread = data->thread;
		removeFromWaiting(thread);
		makeReady(thread);

		MachineResumeSignals(&sigstate);	
	}	

	TVMStatus InternalFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		int result = 0;

		struct fileSeekData *fileData = new struct fileSeekData;
		fileData->thread = curThread;
		if (newoffset) {
			fileData->curOffset = newoffset;
		} else {
			fileData->curOffset = &result;
		}
		
		MachineFileSeek(filedescriptor, offset, whence, &fileSeekCallback, fileData);

		makeWaiting(curThread);
		curThread->sleepDuration = -1;
		scheduler();

		delete fileData;

		if (result < 0) {
			MachineResumeSignals(&sigstate);	
			return VM_STATUS_FAILURE;
		} else {
			MachineResumeSignals(&sigstate);	
			return VM_STATUS_SUCCESS;
		}
		MachineResumeSignals(&sigstate);	
		return VM_STATUS_SUCCESS;
	}

	void skeleton (void *data) {
		MachineEnableSignals();

		curThread->threadEntry(curThread->parameter);

		VMThreadTerminate(curThread->tid);
	}

	TVMStatus VMMutexCreate(TVMMutexIDRef mutexref) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if(mutexref){
			struct Mutex* newMutex = new struct Mutex;
			newMutex->unlocked = true;
			newMutex->id = allMutexes.size();

			TVMMutexID newId = newMutex->id;
			*mutexref = newId;

			if (allMutexes.find(newId) == allMutexes.end()) {
				allMutexes[newId] = newMutex;	// add to all mutexes if not found already
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_PARAMETER;	// ID has already been used (should never happen but just in case)
			}

		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER; // invalid mutex ref
		}

		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if(ownerref) {
			if (allMutexes.find(mutex) == allMutexes.end()) {	// mutex id does not exist
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_ID;
			} else {
				struct Mutex *foundMutex = allMutexes.at(mutex);
				if(foundMutex->unlocked == true) {	// thread is unlocked, cannot store owner
					*ownerref = VM_THREAD_ID_INVALID;
					MachineResumeSignals(&sigstate);
					return VM_STATUS_SUCCESS;
				} else {
					*ownerref = foundMutex->owner;	// store mutex owner in ownerref
					MachineResumeSignals(&sigstate);
					return VM_STATUS_SUCCESS;
				}
			}
		} else {	// ownerref does not exist
			MachineResumeSignals(&sigstate);	
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}

		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
		
	}

	TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if(allMutexes.find(mutex) == allMutexes.end()) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
			struct Mutex *foundMutex = allMutexes.at(mutex);

			if(timeout == VM_TIMEOUT_IMMEDIATE) {	// immediate, either get or fail to get mutex this moment
				if(foundMutex->unlocked == false) {
					MachineResumeSignals(&sigstate);
					return VM_STATUS_FAILURE;
				} else {
					if(foundMutex->waitingMutex.empty()) {
						foundMutex->unlocked = false;
						foundMutex->owner = curThread->tid;
						curThread->mutexesOwned.push(mutex);
						MachineResumeSignals(&sigstate);
						return VM_STATUS_SUCCESS;
					} else {
						MachineResumeSignals(&sigstate);
						return VM_STATUS_FAILURE;
					}
				}
			} else if (timeout == VM_TIMEOUT_INFINITE) {
				if(foundMutex->unlocked == false) {
					foundMutex->waitingMutex.push(curThread);	// add it to waiting, schedule new thread
					waitingOnMutex.push(curThread);
					curThread->timeoutDuration = timeout;
					curThread->state = VM_THREAD_STATE_WAITING;
					scheduler();
				} else {
					foundMutex->unlocked = false;
					foundMutex->owner = curThread->tid;
					curThread->mutexesOwned.push(mutex);
					MachineResumeSignals(&sigstate);
					return VM_STATUS_SUCCESS;
				}
			} else {
				if(foundMutex->unlocked == true) {	// is free to be acquired
						foundMutex->unlocked = false;
						foundMutex->owner = curThread->tid;
						curThread->mutexesOwned.push(mutex);
				} else {
					foundMutex->waitingMutex.push(curThread);	// add it to waiting, schedule new thread
					waitingOnMutex.push(curThread);
					curThread->timeoutDuration = timeout;
					curThread->state = VM_THREAD_STATE_WAITING;
					scheduler();
					// when wakes up after timeout
					if(foundMutex->unlocked == true) {	// is free to be acquired
						if(foundMutex->waitingMutex.empty()) {	//no others waiting, can become thread rn
							foundMutex->unlocked = false;
							foundMutex->owner = curThread->tid;
							curThread->mutexesOwned.push(mutex);
						} else {
							// remove from the waiting of the mutex
							int mutexWaiting = foundMutex->waitingMutex.size();
							for (int i = 0; i < mutexWaiting; i++) {
								struct Thread *front = foundMutex->waitingMutex.front();
								foundMutex->waitingMutex.pop();
								if(front->tid == curThread->tid){
									//don't push
								} else {
									foundMutex->waitingMutex.push(front);
								}
							}

							MachineResumeSignals(&sigstate);
							return VM_STATUS_FAILURE;	// was not able to acquire was unlocked but others are waiting (should never happen but could be outlier)
						} 
					} else {
							int mutexWaiting = foundMutex->waitingMutex.size();
							for (int i = 0; i < mutexWaiting; i++) {
								struct Thread *front = foundMutex->waitingMutex.front();
								foundMutex->waitingMutex.pop();
								if(front->tid == curThread->tid){
									// don't push
								} else {
									foundMutex->waitingMutex.push(front);
								}
							}

						MachineResumeSignals(&sigstate);	// mutex was not unlocked
						return VM_STATUS_FAILURE;
					}
				}
			}

			MachineResumeSignals(&sigstate);
			return VM_STATUS_SUCCESS;
		}
	}

	TVMStatus VMMutexRelease(TVMMutexID mutex) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if(allMutexes.find(mutex) == allMutexes.end()) {	//mutex not valid
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
			struct Mutex *foundMutex = allMutexes.at(mutex);	//finds mutex
			if (foundMutex->owner != curThread->tid) {	// if curThread is not the owner

				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_STATE;
			} else {
				struct Thread *oldOwner = allThreads.at(foundMutex->owner);
				removeFromOwned(oldOwner, mutex);

				if (foundMutex->waitingMutex.empty()) {
					foundMutex->owner = -1; // no owner, need placeholder
					foundMutex->unlocked = true;
				} else {
					struct Thread* nextOwner = foundMutex->waitingMutex.front();	// once released, get next owner
					foundMutex->waitingMutex.pop();
					removeFromWaitingOnMutex(nextOwner);
					nextOwner->mutexesOwned.push(foundMutex->id);
					foundMutex->owner = nextOwner->tid;
					makeReady(nextOwner);
					if(nextOwner->priority > curThread->priority) {	// need to schedule if higher priority
						scheduler();
					}
				}
			}
		}

		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexDelete(TVMMutexID mutex) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);

		if(allMutexes.find(mutex) == allMutexes.end()) {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_ID;
		} else {
			struct Mutex *foundMutex = allMutexes.at(mutex);
			if(foundMutex->unlocked == false) {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_ERROR_INVALID_STATE; 
			} else {
				allMutexes.erase(foundMutex->id);
				MachineResumeSignals(&sigstate);
				return VM_STATUS_SUCCESS;
			}
		}
	}

	TVMStatus InternalFileWrite(int filedescriptor, void *data, int *length) {
		TMachineSignalState sigstate;
		MachineSuspendSignals(&sigstate);
		if (data && length) {

			int bytesWritten = 0;
			
			int callbacksReturned = 0;
			//find out how many sections you need to acquire
			int numSectionsNeeded = (int)(*length / memSectionSize);

			int remainderBytes = *length % memSectionSize;
			if(remainderBytes != 0) {
				numSectionsNeeded += 1;
			}
			int numSectionsAcquired = 0;

			unsigned char *fileStart = (unsigned char*)data;

			//acquire next available memory section
			int numAvailable = availableMemorySection.size();
			for(int i = 0; i < numSectionsNeeded; i++) {
				if(numSectionsAcquired >= numSectionsNeeded) {
					// acquired all sections needed
					break;
				} else {
						if (numAvailable > 0) {
							struct SharedMemorySection *firstAvailable = availableMemorySection.front();
							availableMemorySection.pop();

							firstAvailable->unlocked = false;
							numSectionsAcquired += 1;

							struct fileWriteData *fileData = new struct fileWriteData;
							fileData->thread = curThread;
							fileData->numBytes = &bytesWritten;
							fileData->numCallbacksDone = &callbacksReturned;
							fileData->numCallbacksNeeded = numSectionsNeeded;


							if (numSectionsAcquired == numSectionsNeeded && remainderBytes != 0) {	// last section, length to write is remainder (max of 512)
								firstAvailable->bytesUsed = remainderBytes;
							} else {
								firstAvailable->bytesUsed = memSectionSize; // full section, length to write is 512
							}
							//memcopy to shared memory, by section
							memcpy(firstAvailable->startOfSection, fileStart, firstAvailable->bytesUsed);
							fileStart = fileStart + firstAvailable->bytesUsed;
							MachineFileWrite(filedescriptor, firstAvailable->startOfSection, firstAvailable->bytesUsed, &fileWriteCallback, fileData);

							makeWaiting(curThread);
							curThread->sleepDuration = -1;
							scheduler();

							firstAvailable->unlocked = true;
							availableMemorySection.push(firstAvailable);

							delete fileData;

							if (bytesWritten < 0) {
								MachineResumeSignals(&sigstate);
								return VM_STATUS_FAILURE;
							}
						} else {
							waitingOnMemory.push(curThread);
							curThread->sleepDuration = -1;
							curThread->state = VM_THREAD_STATE_WAITING;
							scheduler();
						}
					}
				}

			if (bytesWritten < *length) {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_SUCCESS;
			}
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		}
		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus ReadSector(int secNum, void* data) {
		//lock
		//Seek(sec*512)
		int offset = secNum * 512;
		InternalFileSeek(fatFileDescriptor, offset, 0, NULL);
		//Read 512 bytes
		int length = 512;
		InternalFileRead(fatFileDescriptor, data, &length);
		//unlock
		return VM_STATUS_SUCCESS;
	}

	TVMStatus WriteSector(int secNum, void* data) {
		//lock
		//Seek(sec*512)
		int offset = secNum * 512;
		InternalFileSeek(fatFileDescriptor, offset, 0, NULL);
		//Read 512 bytes
		int length = 512;
		InternalFileWrite(fatFileDescriptor, data, &length);
		//unlock
		return VM_STATUS_SUCCESS;
	}

	TVMStatus ReadCluster(int clusterNum, void* data) {


		if(clusterNum == 0 || clusterNum == 1) {

			return VM_STATUS_FAILURE;
		} else {
			for (int i = 0; i < bpb->SecPerClus; i++) {
				// calculate starting point
				int secNum = fatInformation->FirstDataSector + (clusterNum - 2) * bpb->SecPerClus + i;
				ReadSector(secNum, data);
			}
		}


		return VM_STATUS_SUCCESS;
	}

	TVMStatus WriteCluster(int clusterNum, void* data) {


		if(clusterNum == 0 || clusterNum == 1) {
			return VM_STATUS_FAILURE;
		} else {
			for (int i = 0; i < bpb->SecPerClus; i++) {
				// calculate starting point
				int secNum = fatInformation->FirstDataSector + (clusterNum - 2) * bpb->SecPerClus + i;
				WriteSector(secNum, data);
			}
		}

		return VM_STATUS_SUCCESS;
	}


	void setDateStruct(SVMDateTimeRef dateStruct, uint16_t dateBytes, uint16_t timeBytes) {
		//access bits 0-4 for day
		uint16_t dayCopy = dateBytes;
		uint16_t dayMask = 31;
		dateStruct->DDay = dayCopy & dayMask;
		//std::cout << "Day: " << (int)dateStruct->DDay << std::endl;

		//access bits 5-8 for month
		uint16_t monthCopy = dateBytes;
		monthCopy >>= 5;
		uint16_t monthMask = 15;
		dateStruct->DMonth = monthCopy & monthMask;
		//std::cout << "Month: " << (int)dateStruct->DMonth << std::endl;

		std::ios  state(NULL);
		state.copyfmt(std::cout);

		uint16_t yearCopy = dateBytes;
		//std::cout << std::hex << "Year Copy before shift " << yearCopy << std::endl;
		yearCopy >>= 9;

		//std::cout << std::hex << "Year Copy after shifted " << yearCopy << std::endl;

		uint16_t yearMask = 127;
		dateStruct->DYear = 1980 + (yearCopy & yearMask);

		//std::cout.copyfmt(state);

		//std::cout << "Year: " << (int)dateStruct->DYear << std::endl;

		//get time for DateStruct
		uint16_t secondCopy = timeBytes;
		uint16_t secondMask = 31;
		dateStruct->DSecond = 2 * (secondCopy & secondMask);
		//std::cout << "Second: " << (int)dateStruct->DSecond << std::endl;

		uint16_t minutesCopy = timeBytes;
		minutesCopy >>= 5;
		uint16_t minutesMask = 63;
		dateStruct->DMinute = minutesCopy & minutesMask;
		//std::cout << "Minutes: " << (int)dateStruct->DMinute << std::endl;

		uint16_t hourCopy = timeBytes;
		hourCopy >>= 11;
		uint16_t hourMask = 31;
		dateStruct->DHour = hourCopy & hourMask;
		//std::cout << "Hour: " << (int)dateStruct->DHour << std::endl;
	}

	void encodeDateStruct(SVMDateTimeRef dateStruct, uint16_t* dateBytes, uint16_t* timeBytes) {
		uint16_t yearMask = dateStruct->DYear - 1980;
		*dateBytes = *dateBytes | yearMask;
		*dateBytes <<= 4;	// shift for month value


		//set mask to value of Month
		uint16_t monthMask = dateStruct->DMonth;
		*dateBytes = (*dateBytes | monthMask);
		*dateBytes <<= 5;

		uint16_t dayMask = dateStruct->DDay / 2;
		*dateBytes = (*dateBytes | dayMask);


		uint16_t hourMask = dateStruct->DHour;
		*timeBytes = *timeBytes | hourMask;
		*timeBytes <<= 6;

		uint16_t minutesMask = dateStruct->DMinute;
		*timeBytes = *timeBytes | minutesMask;
		*timeBytes <<= 5;

		//get time for DateStruct
		uint16_t secondMask = dateStruct->DSecond / 2;
		*timeBytes = *timeBytes | secondMask;

	}

	void setDateAccess(SVMDateTimeRef dateStruct, uint16_t dateBytes) {
		uint16_t dayCopy = dateBytes;
		uint16_t dayMask = 31;
		dateStruct->DDay = dayCopy & dayMask;
		//access bits 5-8 for month
		uint16_t monthCopy = dateBytes;
		monthCopy >>= 5;
		uint16_t monthMask = 15;
		dateStruct->DMonth = monthCopy & monthMask;

		uint16_t yearCopy = dateBytes;
		yearCopy >>= 9;
		uint16_t yearMask = 127;
		dateStruct->DYear = yearCopy & yearMask;
	}


uint16_t findFirstFreeCluster() {
	//returns cluster number, will need to figure out address in fat.ima if use
	for (unsigned int i = 0; i < fatTable.size(); i++) {
		if(fatTable[i] == 0x00) {
			//std::cout << "First Free Cluster " << i << std::endl;
			return i;
		}
	}
	
	return -1;
}

uint16_t findFirstFreeEntry(){
	//returns entry number, will need to figure out address in fat.ima if use
	for(int i = 0; i < bpb->RootEntCnt; i++) {
		if(rootData[i*32] == 0x00) {
			//std::cout << "First Free Entry " << i << std::endl;
			return i;
		}
	}
	
	return -1;
}

TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	bool found = false;
	struct DirectoryEntry* foundDirectory = NULL;
	// see if filename is one of the entries
	//if yes, go about opening it
	//else will need to create one (but save for later)
	uint32_t userFileLength = VMStringLength(filename);
 
	char userCopy[20];
	int indexOfDot = -1;
	//find index of . if it's zero, throw

	if(userFileLength > 12){
		// not handling long file name
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
	} else {
		// check if proper format/format copy to ShortDirectoryName and 
		for(int i = 0; i < 8; i++) {
			if(filename[i] == '.'){
				if (i == 0) {
					MachineResumeSignals(&sigstate);
					return VM_STATUS_FAILURE;
				} else {
					// if hasnt been set, set index of Dot
					if (indexOfDot == -1) {
						indexOfDot = i;
						userCopy[i] = ' ';
					} else {
						MachineResumeSignals(&sigstate);
						return VM_STATUS_FAILURE;
					}
					
				}
				//pad with spaces or put in character
			} else {
				if(indexOfDot > 0){
					userCopy[i] = ' ';
				} else {
					userCopy[i] = filename[i];
					//toUpper(userCopy[i]);
				}
			}
		}

		if(indexOfDot < 0 && userFileLength > 8) {
			if(filename[8] == '.') {
				indexOfDot = 8;
			}
		}

		// deal with suffix
		for(int i = 8; i < 11; i++) {
			if (indexOfDot > 0) {
				unsigned int sourceIndex = indexOfDot + (i-8) + 1;
				if (sourceIndex >= userFileLength) {
					userCopy[i] = ' ';
				} else {
					userCopy[i] = filename[sourceIndex];	//get suffix at char after dot indexofDot+(8-8)+1, char + 1 after dot indexofDot+(9-8)+1, char + 2 after dot
				}
				//toUpper(userCopy[i]);
			} else {
				userCopy[i] = ' ';
			}
		}

		userCopy[11] = '\0'; //null terminate string

		for(unsigned int i = 0; i < rootDirectories.size(); i++) {
			char dirName[12];
			VMStringCopyN(dirName, rootDirectories[i]->entry->DShortFileName, 12); // MAY FAIL HERE
			int result = strcasecmp(userCopy, dirName);
			if (result == 0) {
				found = true;
				foundDirectory = rootDirectories[i];
				break;
			}
		}

		//std::cout << "made it to name comparison" << std::endl;


		if (found) {
			//actually open foundDirectory (may need this code after this block)
			// 			struct FileEntry {
			// 	int fileDescriptor;
			// 	char* path;	// will want to memCpy path and name 
			// 	char* name;
			//	int flags;
			//  int curOffset;
			//	int curCluster;
			// 	struct DirectoryEntry * rootEntry;
			// };
			// check if directory or normal file
			if((foundDirectory->entry->DAttributes & VM_FILE_SYSTEM_ATTR_DIRECTORY) == VM_FILE_SYSTEM_ATTR_DIRECTORY) {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			} else {
				struct FileEntry* openedFile = new struct FileEntry;

				//std::cout << "finds file that exists" << std::endl;

				//INITIALIZE FILE INFO
				openedFile->fileDescriptor = curFD;
				curFD++; // increment filedescriptor variable
				openedFile->rootEntry = foundDirectory; // set directory entry

				memcpy(openedFile->name, userCopy, 11); //put name here

				openedFile->flags = flags; // set flags
				SVMDateTime accDate;	// change access Dates
				VMDateTime(&accDate);
				openedFile->rootEntry->entry->DAccess = accDate;

				uint16_t date = 0;
				uint16_t time = 0;
				encodeDateStruct(&(openedFile->rootEntry->entry->DAccess), &date, &time);

				//change access date in root section

				uint8_t sectorBuffer[512];
				uint8_t entryArray[32];
				int bufferIndex = (32 * openedFile->rootEntry->entryNum);

				ReadSector(fatInformation->FirstRootSector, sectorBuffer);

				memcpy(entryArray, &(sectorBuffer[bufferIndex]), 32);
				memcpy(&(entryArray[18]), &date, 2);
				// for future may not want 
				
				memcpy(&(sectorBuffer[bufferIndex]), entryArray, 32);
				WriteSector(fatInformation->FirstRootSector, sectorBuffer);


				// write to rootEntry Sector
				//write the two bytes into the correct place in memory PROBABLY WILL NEED TO CHANGE MIGHT WANT TO JUST MEMCPY WHOLE ENTRY
				//memcpy(&(rootData[(fatInformation->FirstRootSector * 512) + (entryIndex + 18)]), &(rootData[entryIndex + 18]), 2);

				if((flags & O_APPEND) == O_APPEND) {
					if(openedFile->rootEntry->entry->DSize > 0) { //if is a file > 0 bytes, will need to find curCluster and new offset within that cluster
						// set offset to offset in last cluster
						openedFile->curOffset = (openedFile->rootEntry->entry->DSize % (bpb->SecPerClus * 512));
						// if the size of the file > the the size of a cluster, find what the current cluster is
						if (openedFile->rootEntry->entry->DSize > (bpb->SecPerClus * 512)) {
							int clusSkip = openedFile->rootEntry->entry->DSize / (bpb->SecPerClus * 512);
							for(int i = 0; i < clusSkip; i++) {
								int nextCluster = fatTable[openedFile->rootEntry->FstClusLO + i];
								openedFile->curCluster = nextCluster;
							}
						} else {
							openedFile->curCluster = foundDirectory->FstClusLO;
						}
					} else {
						// file size is 0 so just set offset to 0 and cluster to the first one
						openedFile->curOffset = 0;
						openedFile->curCluster = foundDirectory->FstClusLO;
					}

				} else {
					// assume starting at the beginning of the file (not appending)
					openedFile->curCluster = foundDirectory->FstClusLO;
					openedFile->curOffset = 0;
				}

				// memcpy(openedFile->path, VM_FILE_SYSTEM_DIRECTORY_DELIMETER, 1); // get path?


				//openedFile->curLocation = getImageLocation(foundDirectory->FstClusLO); // point of start of cluster
				// add to list of opened files
				openFiles.push_back(openedFile);
				*filedescriptor = openedFile->fileDescriptor;
			}

		} else if ((flags & O_CREAT) == O_CREAT) {

				// 	char              Name[11];
		  // uint8_t           Attr <- just put 0
		  // uint8_t           NTRes <- this is anyways 0
		  // uint8_t           CrtTimeTenth <- call that function he provided
		  // uint16_t          CrtTime; 
		  // uint16_t          CrtDate;
		  // uint16_t          LstAccDate;
		  // uint16_t          FstClusHI; <- its 0
		  // uint16_t          WrtTime;
		  // uint16_t          WrtDate;
		  // uint16_t          FstClusLO; <- well we need to do this properly
		  // uint32_t          FileSize; <- this is 0
			// if can create file, create it
			// else return Fail

			struct FileEntry* openedFile = new struct FileEntry;
			struct DirectoryEntry* newEntry = new struct DirectoryEntry;

			int freeEntryNum = findFirstFreeEntry();

			uint8_t entryArray[32];
			//std::cout << "File is being created" << std::endl;
			//INITIALIZE FILE INFO
			
			//memcpy(&(entryArray[11]), );
			openedFile->fileDescriptor = curFD; //set filedescriptor
			curFD++; // increment filedescriptor variable
			// change entry info and the write to the image
			openedFile->rootEntry = newEntry; // set directory entry NEED TO MAKE THIS
			
			openedFile->rootEntry->entryNum = freeEntryNum;

			openedFile->curOffset = 0;

			memcpy(openedFile->name, userCopy, 12); //put name here WILL WANT TO PUT IN ROOTENTRY->ENTRY AS NAME AS WELL
			// find next free entry and write to it, setFSClsLO as next free cluster
			uint16_t firstFreeCluster = findFirstFreeCluster();

			//std::cout << "Cluster was found" << std::endl;

			openedFile->curCluster = firstFreeCluster;

			openedFile->flags = flags; // set flags

			SVMDateTime accDate;	// change access Dates
			VMDateTime(&accDate);
			openedFile->rootEntry->entry->DAccess = accDate;
			VMDateTime(&(openedFile->rootEntry->entry->DCreate));

			uint16_t dateCreated = 0;
			uint16_t timeCreated = 0;
			encodeDateStruct(&(openedFile->rootEntry->entry->DCreate), &dateCreated, &timeCreated);
			uint16_t dateAccessed = 0;
			uint16_t timeAccessed = 0; //placeholder
			encodeDateStruct(&(openedFile->rootEntry->entry->DAccess), &dateAccessed, &timeAccessed);


			//Initial Values
			uint8_t Attr = 0;
			uint8_t NTRes = 0;
			uint8_t CrtTimeTenth = 0;
			uint16_t CrtTime = timeCreated;
			uint16_t CrtDate = dateCreated;
			uint16_t LastAccDate = dateAccessed;
			uint16_t FstClusHI = 0;
			uint16_t WrtTime = 0;
			uint16_t WrtDate = 0;
			uint16_t FstClusLO = firstFreeCluster; // need to reserve cluster as well
			uint32_t FileSize = 0;

			memcpy(&(openedFile->rootEntry->entry->DShortFileName), userCopy, 11);
			openedFile->rootEntry->entry->DAttributes = Attr;
			openedFile->rootEntry->entry->DSize = FileSize;

			memcpy(&(entryArray[0]),  userCopy, 11); //copy name
			memcpy(&(entryArray[11]), &Attr, 1);
			memcpy(&(entryArray[12]), &NTRes, 1);
			memcpy(&(entryArray[13]), &CrtTimeTenth, 1);
			memcpy(&(entryArray[14]), &CrtTime, 2);
			memcpy(&(entryArray[16]), &CrtDate, 2);
			memcpy(&(entryArray[18]), &LastAccDate, 2);
			memcpy(&(entryArray[20]), &FstClusHI, 2);
			memcpy(&(entryArray[22]), &WrtTime, 2);
			memcpy(&(entryArray[24]), &WrtDate, 2);
			memcpy(&(entryArray[26]), &FstClusLO, 2);
			memcpy(&(entryArray[28]), &FileSize, 4);

			fatTable[firstFreeCluster] = 0xFFFF;
			//std::cout << "created Entry Array" << std::endl;

			for(int i = 0; i < bpb->FATSz16; i++) {
				int secNum = i + 1;
				WriteSector(secNum, &(fatTable[i * 512]));
			}

			//std::cout << "edited Fat" << std::endl;

			// write to root data sector when done TURN INTO FUNCTION IF POSSIBLE
			uint8_t sectorBuffer[512];
			// for future may not want 
			ReadSector(fatInformation->FirstRootSector, sectorBuffer);
			int bufferIndex = (32 * freeEntryNum);
			memcpy(&(sectorBuffer[bufferIndex]), entryArray, 32);
			WriteSector(fatInformation->FirstRootSector, sectorBuffer);

			//std::cout << "Write to Image" << std::endl;

			openFiles.push_back(openedFile);

			//displayFile(openedFile);
			*filedescriptor = openedFile->fileDescriptor;
			//std::cout << "Opened" << std::endl;

			MachineResumeSignals(&sigstate);
			return VM_STATUS_SUCCESS;
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_FAILURE;
		}
		// add to list of openFiles
		

		MachineResumeSignals(&sigstate);
		return VM_STATUS_SUCCESS;
	}
	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

// may not use this function
int getImageLocation(int clusNum) {
	int fatLocation = (fatInformation->FirstDataSector + (clusNum - 2) * bpb->SecPerClus) * 512; //May be wrong
	return fatLocation;
}

TVMStatus VMFileClose(int filedescriptor) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	if(filedescriptor < 3) {
		InternalFileClose(filedescriptor);
	} else {
		if((unsigned int)filedescriptor < openFiles.size()) {
			if(openFiles[filedescriptor]) {
				openFiles[filedescriptor] = NULL;
				MachineResumeSignals(&sigstate);
				return VM_STATUS_SUCCESS;
			} else {
				//file already closed
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			}
		}
	}
	
	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	if(filedescriptor < 3) {
		InternalFileSeek(filedescriptor, offset, whence, newoffset);
	} else {
		if((unsigned int)filedescriptor < openFiles.size()){
		if(openFiles[filedescriptor]){
			//if openFiles[filedescriptor]->curLocation + offset (may want start of file info and see if total bytes trying to seek to(start-curLocation-offset) is <= filesize)
			//if go past bounds of a cluster, change curCluster based on FAT Table
			if(offset + openFiles[filedescriptor]->curOffset > (bpb->SecPerClus * 512)) {
				// see if there is next cluster
				int nextCluster = fatTable[(openFiles[filedescriptor]->curCluster)];
				if (nextCluster < 0xFFF8) { // if there is next cluster (not an end)
					openFiles[filedescriptor]->curCluster = nextCluster;

					openFiles[filedescriptor]->curOffset = whence + offset; //add to total and then subtract a cluster worth for offset within cluster
					openFiles[filedescriptor]->curOffset -= bpb->SecPerClus * 512; // offset within cluster
				} else {
					MachineResumeSignals(&sigstate);
					return VM_STATUS_FAILURE;
				}
			} else {
				// set new curLocation and offset (from beginning of clusters)
				//openFiles[filedescriptor]->curLocation = openFiles[filedescriptor]->curLocation + offset;
				openFiles[filedescriptor]->curOffset = whence + offset;


				if(newoffset){
				//putnewoffset here
					std::cout << "offset" << openFiles[filedescriptor]->curOffset << std::endl;
					*newoffset = openFiles[filedescriptor]->curOffset;
				} else {
					MachineResumeSignals(&sigstate);
					return VM_STATUS_ERROR_INVALID_PARAMETER;	
				}
				
			}
			

		} else { // file is closed
			MachineResumeSignals(&sigstate);
			return VM_STATUS_FAILURE;
		}
	} else {
		// file not one that has ever been opened
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
	}
	}


	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMFileRead(int filedescriptor, void *data, int *length) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);
	if(filedescriptor < 3) {
		InternalFileRead(filedescriptor, data, length);
	} else {
		if((unsigned int)filedescriptor < openFiles.size()){
			if(openFiles[filedescriptor]){ // file is open
				if((openFiles[filedescriptor]->flags & O_ACCMODE) != 1) { // can read
					char buffer[bpb->SecPerClus * 512];
					ReadCluster(openFiles[filedescriptor]->curCluster, buffer);
					//copy memory into data starting at offset within curCluster
					//only works if length is less than the size of a cluster
					if(openFiles[filedescriptor]->curOffset + *length < (bpb->SecPerClus * 512)){
						memcpy(data, &(buffer[(openFiles[filedescriptor]->curOffset)]), *length);

					} else {
						// read in and then go to next cluster
					}
					
				} else {
					MachineResumeSignals(&sigstate); // file cannot read
					return VM_STATUS_FAILURE;
				}
			} else { // file is closed
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			}
		} else {
			// file not one that has ever been opened
			MachineResumeSignals(&sigstate);
			return VM_STATUS_FAILURE;
		}
	}


	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);
	if(dirname && dirdescriptor) {
		if(dirname[0] == '/') {
			rootOpen = true;
			*dirdescriptor = 3;
		} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
		}
	} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}


	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryCurrent(char *abspath) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);
	if (abspath) {
		char slash = '/';
		memcpy(abspath, &slash, 1);
	} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);
	if(dirent) {
		if(dirdescriptor == 3) {
			if((unsigned int)curDirectoryDescriptor < rootDirectories.size()) {
				*dirent = *(rootDirectories[curDirectoryDescriptor]->entry);
				curDirectoryDescriptor++;
			} else {
				MachineResumeSignals(&sigstate);
				return VM_STATUS_FAILURE;
			}
		} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_FAILURE;
		}
	} else {
			MachineResumeSignals(&sigstate);
			return VM_STATUS_ERROR_INVALID_PARAMETER;
	}


	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryChange(const char *path) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	if(path) {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
	} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}

	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRewind(int dirdescriptor) {
	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	if(dirdescriptor == 3 && rootOpen) {
		curDirectoryDescriptor = 3;
	} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
	}

	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryClose(int dirdescriptor) {
 	TMachineSignalState sigstate;
	MachineSuspendSignals(&sigstate);

	if(dirdescriptor == 3 && rootOpen) {
		rootOpen = false;
		curDirectoryDescriptor = 3;
	} else {
		MachineResumeSignals(&sigstate);
		return VM_STATUS_FAILURE;
	}

	MachineResumeSignals(&sigstate);
	return VM_STATUS_SUCCESS;
}

/*	class Cache {
		unsigned int numSectors, numEntries;
		uint16_t table[];

		public:
			Cache(int numSectorsIn){
				numSectors = numSectorsIn;
				numEntries = bpb->FATSz16 * 256;
				table[numEntries];
			}
			void setEntries();
	};
*/

}
