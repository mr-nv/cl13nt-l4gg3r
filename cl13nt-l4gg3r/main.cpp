#include "main.h"

CClientState* g_pClientState = nullptr;
IBaseClientDLL* g_pClient = nullptr;
CInput* g_pInput = nullptr;

std::uint8_t* PatternScan(void* module, const char* signature)
{
	static auto pattern_to_byte = [](const char* pattern) {
		auto bytes = std::vector<int>{};
		auto start = const_cast<char*>(pattern);
		auto end = const_cast<char*>(pattern) + strlen(pattern);

		for (auto current = start; current < end; ++current) {
			if (*current == '?') {
				++current;
				if (*current == '?')
					++current;
				bytes.push_back(-1);
			}
			else {
				bytes.push_back(strtoul(current, &current, 16));
			}
		}
		return bytes;
	};

	auto dosHeader = (PIMAGE_DOS_HEADER)module;
	auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)module + dosHeader->e_lfanew);

	auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
	auto patternBytes = pattern_to_byte(signature);
	auto scanBytes = reinterpret_cast<std::uint8_t*>(module);

	auto s = patternBytes.size();
	auto d = patternBytes.data();

	for (auto i = 0ul; i < sizeOfImage - s; ++i) {
		bool found = true;
		for (auto j = 0ul; j < s; ++j) {
			if (scanBytes[i + j] != d[j] && d[j] != -1) {
				found = false;
				break;
			}
		}
		if (found) {
			return &scanBytes[i];
		}
	}
	return nullptr;
}

using CreateMove = void(__thiscall*)(IBaseClientDLL*, int, float, bool);
CreateMove oCreateMove;

typedef void* (*CreateInterfaceFn)(const char *pName, int *pReturnCode);
CreateInterfaceFn GetModuleFactory(HMODULE module)
{
	return reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
}
template<typename T>
T* GetInterface(CreateInterfaceFn f, const char* szInterfaceVersion)
{
	auto result = reinterpret_cast<T*>(f(szInterfaceVersion, nullptr));

	return result;
}

void __stdcall hkdCreateMove(int sequence_number, float input_sample_frametime, bool active, bool& bSendPacket)
{
	oCreateMove(g_pClient, sequence_number, input_sample_frametime, active);

	auto pUserCmd = g_pInput->GetUserCmd(sequence_number);
	auto pVerifiedUserCmd = g_pInput->GetVerifiedCmd(sequence_number);

	if (!pUserCmd || !pUserCmd->command_number)
		return;

	static auto NewSignonMsg = reinterpret_cast<void(__thiscall*)(void*, int, int)>(PatternScan(GetModuleHandleA("engine.dll"), "55 8B EC 56 57 8B F9 8D 4F 04 C7 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B ?? ?? C6 ?? ?? ?? C7"));
	static auto ClearSignonMsg = reinterpret_cast<void(__thiscall*)(void*)>(PatternScan(GetModuleHandleA("engine.dll"), "53 56 57 8B F9 8D ?? 38 C7 ?? ?? ?? ?? ?? C7 ?? ?? ?? ?? ?? ?? 8B"));

	void* pSignonMsg = malloc(76);

	int nTotalSentSignon = 0;

	// random
	int AMOUNT_PER_TICK = 2000;
	int TOTAL_AMOUNT = 20000;

	INetChannel *NetChannel = *reinterpret_cast<INetChannel**>(reinterpret_cast<uintptr_t>(g_pClientState) + 0x9C);
	if (/*g_MenuOptions->misc_client_lagger or some shit like that && */GetAsyncKeyState(0x56) && pSignonMsg && NetChannel)
	{
		if (nTotalSentSignon < TOTAL_AMOUNT)
		{
			NewSignonMsg(pSignonMsg, 6, g_pClientState->m_nServerCount);

			for (int i = 0; i < AMOUNT_PER_TICK; i++)
			{
				NetChannel->SendNetMsg((INetMessage*)pSignonMsg, false, false);
			}

			ClearSignonMsg(pSignonMsg);
			nTotalSentSignon += AMOUNT_PER_TICK;
		}
	}

	pVerifiedUserCmd->m_cmd = *pUserCmd;
	pVerifiedUserCmd->m_crc = pUserCmd->GetChecksum();
}

__declspec(naked) void __stdcall hkdCreateMoveProxy(int sequence_number, float input_sample_frametime, bool active)
{
	__asm
	{
		push ebp
		mov  ebp, esp
		push ebx
		lea  ecx, [esp]
		push ecx
		push dword ptr[active]
		push dword ptr[input_sample_frametime]
		push dword ptr[sequence_number]
		call hkdCreateMove
		pop  ebx
		pop  ebp
		retn 0Ch
	}
}

bool GetInterfaces()
{
	g_pClient = GetInterface<IBaseClientDLL>(GetModuleFactory(GetModuleHandleW(L"client.dll")), "VClient018");
	if (!g_pClient) return false;

	g_pClientState = **(CClientState***)(PatternScan(GetModuleHandleA("engine.dll"), "A1 ? ? ? ? 8B 80 ? ? ? ? C3") + 1);
	if (!g_pClientState) return false;

	g_pInput = *(CInput**)(PatternScan(GetModuleHandleA("client.dll"), "B9 ? ? ? ? 8B 40 38 FF D0 84 C0 0F 85") + 1);
	if (!g_pInput) return false;
	
	/*if (g_pClientState)
	{
		char t[64];
		sprintf_s(t, "g_pClientState -> 0x%X", (DWORD)g_pClientState);
		MessageBoxA(NULL, t, t, MB_OK);
	}*/

	return true;
}

bool Hook()
{
	CVMTHookManager* clienthook = new CVMTHookManager((PDWORD*)g_pClient);

	oCreateMove = (CreateMove)clienthook->HookMethod((DWORD)hkdCreateMoveProxy, 21);

	return true;
}

bool Start()
{
	if (!GetInterfaces()) return EXIT_FAILURE;
	if (!Hook()) return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

DWORD WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		Start();
	}

	return true;
}
