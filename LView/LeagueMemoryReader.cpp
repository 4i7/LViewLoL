#include "LeagueMemoryReader.h"
#include "windows.h"
#include "Utils.h"
#include "Structs.h"
#include "psapi.h"
#include <stdexcept>

bool LeagueMemoryReader::IsLeagueWindowActive() {
	HWND handle = GetForegroundWindow();

	DWORD h;
	GetWindowThreadProcessId(handle, &h);
	return pid == h;
}

bool LeagueMemoryReader::IsHookedToProcess() {
	return Process::IsProcessRunning(pid);
}

void LeagueMemoryReader::HookToProcess() {

	// Find the window
	hWindow = FindWindowA("RiotWindowClass", NULL);
	if (hWindow == NULL) {
		throw WinApiException("League window not found");
	}

	// Get the process ID
	GetWindowThreadProcessId(hWindow, &pid);
	if (pid == NULL) {
		throw WinApiException("Couldn't retrieve league process id");
	}

	// Open the process
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (hProcess == NULL) {
		throw WinApiException("Couldn't open league process");
	}

	// Check architecture
	if (0 == IsWow64Process(hProcess, &is64Bit)) {
		throw WinApiException("Failed to identify if process has 32 or 64 bit architecture");
	}

	HMODULE hMods[1024];
	DWORD cbNeeded;
	if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
		moduleBaseAddr = (DWORD_PTR)hMods[0];
	}
	else {
		throw WinApiException("Couldn't retrieve league base address");
	}
}

void LeagueMemoryReader::ReadRenderer() {
	high_resolution_clock::time_point readTimeBegin;
	duration<float, std::milli> readDuration;
	readTimeBegin = high_resolution_clock::now();
	
	DWORD rendererAddr = Mem::ReadPointer(hProcess, moduleBaseAddr + oRenderer);
	renderer.LoadFromMem(rendererAddr, moduleBaseAddr, hProcess);

	readDuration = high_resolution_clock::now() - readTimeBegin;
	benchmark.readRendererMs = readDuration.count();
}

void LeagueMemoryReader::ReadChampions() {
	high_resolution_clock::time_point readTimeBegin;
	duration<float, std::milli> readDuration;
	readTimeBegin = high_resolution_clock::now();

	DWORD heroManagerPtr = Mem::ReadPointer(hProcess, moduleBaseAddr + oHeroList);
	DWORD champListPtr = Mem::ReadPointer(hProcess, heroManagerPtr + oHeroListHeroArray);
	Mem::Read(hProcess, heroManagerPtr + oHeroListNumChampions, &numChampions, 4);

	if (champListPtr != 0 && numChampions > 0 && numChampions <= numMaxChamps) {

		for (size_t i = 0; i < numChampions; ++i) {
			DWORD heroPtr = Mem::ReadPointer(hProcess, champListPtr + i * 4);

			if (heroPtr == 0)
				break;
			champions[i]->LoadFromMem(heroPtr, hProcess);
		}
	}
	else
		numChampions = 0;

	readDuration = high_resolution_clock::now() - readTimeBegin;
	benchmark.readChampsMs = readDuration.count();
}

void LeagueMemoryReader::ReadMinions() {
	high_resolution_clock::time_point readTimeBegin;
	duration<float, std::milli> readDuration;
	readTimeBegin = high_resolution_clock::now();

	DWORD minionManager = Mem::ReadPointer(hProcess, moduleBaseAddr + oMinionList);
	DWORD minionList = Mem::ReadPointer(hProcess, minionManager + oMinionListArray);
	Mem::Read(hProcess, minionManager + oMinionNumMinions, &numMinions, 4);

	if (minionList != 0 && numMinions > 0 & numMinions < numMaxMinions) {

		wards.clear();
		others.clear();

		DWORD pointers[numMaxMinions];
		Mem::Read(hProcess, minionList, pointers, sizeof(DWORD) * numMinions);
		for (size_t i = 0; i < numMinions; ++i) {
			if (pointers[i] == 0)
				break;
			minions[i]->LoadFromMem(pointers[i], hProcess);

			if (wardNames.find(minions[i]->name) != wardNames.end()) {
				wards.push_back(minions[i]);
				minions[i]->expiryAt += gameTime;
			}
			else
				others.push_back(minions[i]);
		}
	}
	else
		numMinions = 0;

	readDuration = high_resolution_clock::now() - readTimeBegin;
	benchmark.readMinionsMs = readDuration.count();
}

void LeagueMemoryReader::ReadStructs() {
	
	static int calls = 0;

	//Read game time
	gameTime = Mem::ReadFloat(hProcess, moduleBaseAddr + oGameTime);

	if (gameTime > 1) {

		ReadChampions();
		ReadRenderer();
		ReadMinions();
	}
}