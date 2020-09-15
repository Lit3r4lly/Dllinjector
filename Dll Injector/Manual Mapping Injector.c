#include "Includes.h"

int manualMappingInjectionMethod(int processId, char* dllPath) {
	BYTE					*pSrcDllData			= NULL;
	IMAGE_DOS_HEADER		*pDosHeader				= NULL;
	IMAGE_NT_HEADERS		*pOldNtHeader			= NULL;
	IMAGE_OPTIONAL_HEADER	*pOldOptHeader			= NULL;
	IMAGE_FILE_HEADER		*pOldFileHeader			= NULL;
	IMAGE_SECTION_HEADER	*pSectionHeader			= NULL;
	BYTE					*pTargetAddr			= NULL;

	FILE				*pFile			= NULL;
	unsigned int		fileSize		= 0;

	unsigned int i = 0;
	HANDLE hProcess = 0;
	LPTHREAD_START_ROUTINE entryPoint = 0;

	loaderData loaderParams = { 0 };
	PVOID loaderMemory = 0;

	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
	if (hProcess == NULL) {
		// failed to create handle with the process
		return FALSE;
	}

	pFile = fopen(dllPath, "wb");
	if (pFile == NULL) {
		printf("[!] Failed to open the dll file \n");

		return FALSE;
	}

	// getting the file size (classic c moment)
	fseek(pFile, 0L, SEEK_END);
	fileSize = ftell(pFile);
	rewind(pFile);

	// if there is nothing except PE headers
	if (fileSize < 0x1000) {
		printf("[!] File size is invalid \nNote: there is nothin else except PE headers \n");

		fclose(pFile);
		return FALSE;
	}

	// reading the dll file into memory for analysis
	pSrcDllData = (BYTE*)malloc(fileSize);
	if (pSrcDllData == NULL) {
		printf("[!] Failed to allocate memory for the dll data\n");
		
		fclose(pFile);
		return FALSE;

	}
	fread(pSrcDllData, sizeof(BYTE*), fileSize, pFile);
	fclose(pFile);

	// starting analysis the dll

	pDosHeader = (PIMAGE_DOS_HEADER)pSrcDllData;
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
		printf("[!] Invalid executable(dll) file \nNote: there is no MZ signature at the start of the file\n");
		
		free(pSrcDllData);
		return FALSE;
	}

	pOldNtHeader = (PIMAGE_NT_HEADERS)(pSrcDllData + pDosHeader->e_lfanew);
	pOldOptHeader = &pOldNtHeader->OptionalHeader;
	pOldFileHeader = &pOldNtHeader->FileHeader;

	if (pOldFileHeader->Machine != IMAGE_FILE_MACHINE_AMD64) {
		printf("[!] Dll didnt compiled to 64 bit \nNote: compile it again to 64 bit (injector support only 64 bit)\n");

		free(pSrcDllData);
		return FALSE;
	}
	
	// allocating memory in the target process for writing the dll
	// first trying to allocate in the image base of the process for unusing rva and some extra offsets
	pTargetAddr = (BYTE*)VirtualAllocEx(hProcess, (void*)pOldOptHeader->ImageBase, pOldOptHeader->SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (pTargetAddr == NULL) {
		// trying to allocate in other address (not directly to the image base)
		
		pTargetAddr = (BYTE*)VirtualAllocEx(hProcess, NULL, pOldOptHeader->SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (pTargetAddr == NULL) {
			printf("[!] Failed to allocate memory in the target process \n");

			free(pSrcDllData);
			return FALSE;
		}
	}

	//entryPoint = (LPTHREAD_START_ROUTINE)(pTargetAddr + pOldOptHeader->AddressOfEntryPoint);

	// writing sections to memory (target process)
	pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
	for (i = 0; i != pOldFileHeader->NumberOfSections; i++, pSectionHeader++) {
		if (!WriteProcessMemory(hProcess, pTargetAddr + pSectionHeader->VirtualAddress, pSrcDllData + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, NULL)) {
			printf("[!] Failed to write the sections into the process\n");

			VirtualFreeEx(hProcess, pTargetAddr, 0, MEM_RELEASE);
			free(pSrcDllData);
			return FALSE;
		}
	}

	// allocate memory for the loader (params + code)
	loaderMemory = VirtualAllocEx(hProcess, NULL, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

	// setting loader params for starting relocating and resolving iat
	loaderParams.ImageBase = pTargetAddr;
	loaderParams.NtHeaders = (PIMAGE_NT_HEADERS)(pTargetAddr + pDosHeader->e_lfanew);

	loaderParams.BaseReloc = (PIMAGE_BASE_RELOCATION)(pTargetAddr + pOldNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
	loaderParams.ImportDirectory = (PIMAGE_BASE_RELOCATION)(pTargetAddr + pOldNtHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	loaderParams.fnLoadLibraryA = LoadLibraryA;
	loaderParams.fnGetProcAddress = GetProcAddress;


	VirtualFreeEx(hProcess, loaderMemory, 0, MEM_RELEASE);
	VirtualFreeEx(hProcess, pTargetAddr, 0, MEM_RELEASE);
	free(pSrcDllData);
	return TRUE;
}

void loaderShellcode(loaderData loaderParams) {

}