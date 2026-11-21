// ChloeSteamNet.asi - Chloe's multiplayer (FlatOutUCCustomMP_gcp.asi) over Steam instead of plain UDP.
//
// Chloe's multiplayer uses ENet over UDP (WS2_32: WSASendTo / WSARecvFrom / select). This plugin patches those
// imports of FlatOutUCCustomMP_gcp.asi only:
//   * Steam players get placeholder IPv4 addresses 240.0.0.x (class E, never routable). Datagrams to such an
//     address are sent with SteamNetworkingMessages (Steam relay / NAT traversal, no port forwarding), datagrams
//     from Steam are handed to ENet as if they came from that address. Real IP addresses still use real UDP.
//   * When Chloe's server socket is bound (ChloeNet_CreateGame -> port 23756) a public Steam lobby is created,
//     it is left again when the socket is closed. Lobby data: protocol version, host name, host SteamID.
//   * Lua functions ChloeSteam_* (lobby list, join) for the in-game game browser (chloemultiplayer.bed).
// Only for the current Steam version of FOUC (ZOOM's xlive.dll initialises Steam and runs the callbacks each frame).
// Build: pwsh -File custom_src/ChloeSteamNet/build.ps1  (Visual Studio 2022 Build Tools, x86)

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <xinput.h>
#include "StuntVideo.h"
#include "StuntAudio.h"

// ------------------------------------------------------------------------------------------------------------
// constants

static const char*    kProtocol      = "chloecustom-183-steam-4"; // both players need the same set of arenas
static const char*    kKeyProtocol   = "chloecustommp";
static const char*    kKeyName       = "name";
static const char*    kKeyHost       = "host";
static const uint16_t kServerPort    = 23756;                     // ChloeNet_CreateGame(23756) in chloemultiplayer.bed
static const int      kChannel       = 23756;                     // SteamNetworkingMessages channel (ZOOM's xlive uses its own)
static const int      kStuntVideoChannel = 23757;
static const uint8_t  kTypeData      = 0x43;
static const uint8_t  kTypeHello     = 0x48;   // host -> lobby members, client -> host while connecting
static const uint8_t  kTypeHelloAck  = 0x41;   // host -> client (answer to a client hello)
static const uint8_t  kTypeAIDrivers = 0x44;   // host -> clients: chosen AI drivers (profile ids in order)
static const uint8_t  kTypeAIDamage  = 0x47;   // client -> host: damage the client dealt to an AI car (srcPort = player index, float)
static const uint8_t  kTypeName      = 0x4E;   // profile name of the sender (wchar[16]), for the stunt score entries
static const uint8_t  kTypeStunt     = 0x54;   // stunt round record of the sender (srcPort = level, dstPort = round)
static const uint8_t  kTypeRaceStatus = 0x53;  // racing PC -> lobby members: progress for the lobby (5 x int32)
static const uint8_t  kTypeStuntResult = 0x52; // client -> host: result of a multiplayer stunt (narrow string, tournament)
static const DWORD    kConnectTimeout = 20000; // ms for the Steam connection to the host before joining with ENet
static const size_t   kMaxQueue      = 2048;                      // packets per local port

// ------------------------------------------------------------------------------------------------------------
// log (ChloeSteamNet.log next to Fouc.exe)

static CRITICAL_SECTION gLogLock;
static char gLogPath[MAX_PATH];

static void Log(const char* fmt, ...) {
	char line[1024];
	SYSTEMTIME t; GetLocalTime(&t);
	int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list a; va_start(a, fmt);
	_vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, fmt, a);
	va_end(a);
	EnterCriticalSection(&gLogLock);
	FILE* f = nullptr;
	if (fopen_s(&f, gLogPath, "a") == 0 && f) { fprintf(f, "%s\n", line); fclose(f); }
	LeaveCriticalSection(&gLogLock);
}

// ------------------------------------------------------------------------------------------------------------
// Steam flat API (steam_api.dll of the Steam version, SDK 1.5x), loaded dynamically

#pragma pack(push, 8)
struct SteamNetworkingIdentity { int eType; int cbSize; union { uint64_t steamID64; uint8_t raw[128]; }; };
struct LobbyCreated_t   { int eResult; uint64_t lobby; };                                     // 513
struct LobbyEnter_t     { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; }; // 504
struct LobbyMatchList_t { uint32_t count; };                                                   // 510
#pragma pack(pop)
static_assert(sizeof(SteamNetworkingIdentity) == 136, "identity size");
static_assert(sizeof(LobbyCreated_t) == 16 && sizeof(LobbyEnter_t) == 24, "callback size");

typedef uint64_t SteamAPICall_t;
static const int kIdentitySteamID = 16;

static struct {
	void* (*Matchmaking)();
	void* (*Messages)();
	void* (*Utils)();
	void* (*User)();
	void* (*Friends)();
	void* (*NetUtils)();
	SteamAPICall_t (*CreateLobby)(void*, int, int);
	SteamAPICall_t (*RequestLobbyList)(void*);
	void (*AddStringFilter)(void*, const char*, const char*, int);
	void (*AddDistanceFilter)(void*, int);
	void (*AddResultCountFilter)(void*, int);
	uint64_t (*GetLobbyByIndex)(void*, int);
	SteamAPICall_t (*JoinLobby)(void*, uint64_t);
	void (*LeaveLobby)(void*, uint64_t);
	bool (*SetLobbyData)(void*, uint64_t, const char*, const char*);
	const char* (*GetLobbyData)(void*, uint64_t, const char*);
	int (*GetNumLobbyMembers)(void*, uint64_t);
	uint64_t (*GetLobbyMemberByIndex)(void*, uint64_t, int);
	uint64_t (*GetLobbyOwner)(void*, uint64_t);
	bool (*SetLobbyJoinable)(void*, uint64_t, bool);
	bool (*IsAPICallCompleted)(void*, SteamAPICall_t, bool*);
	bool (*GetAPICallResult)(void*, SteamAPICall_t, void*, int, int, bool*);
	uint64_t (*GetSteamID)(void*);
	const char* (*GetPersonaName)(void*);
	const char* (*GetFriendPersonaName)(void*, uint64_t);
	int (*SendMessageToUser)(void*, const SteamNetworkingIdentity*, const void*, uint32_t, int, int);
	int (*ReceiveMessagesOnChannel)(void*, int, void**, int);
	bool (*AcceptSessionWithUser)(void*, const SteamNetworkingIdentity*);
	void (*ReleaseMessage)(void*);
	void (*InitRelayNetworkAccess)(void*);
	void (*RegisterCallback)(void*, int);
} S;
static bool gSteamLoaded = false;

static bool LoadSteam() {
	if (gSteamLoaded) return true;
	HMODULE h = GetModuleHandleA("steam_api.dll");
	if (!h) return false;
	bool ok = true;
	auto get = [&](void* dst, const char* name) {
		void* p = (void*)GetProcAddress(h, name);
		if (!p) { Log("steam_api.dll: missing export %s", name); ok = false; }
		*(void**)dst = p;
	};
	get(&S.Matchmaking, "SteamAPI_SteamMatchmaking_v009");
	get(&S.Messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
	get(&S.Utils, "SteamAPI_SteamUtils_v010");
	get(&S.User, "SteamAPI_SteamUser_v023");
	get(&S.Friends, "SteamAPI_SteamFriends_v017");
	get(&S.NetUtils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
	get(&S.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby");
	get(&S.RequestLobbyList, "SteamAPI_ISteamMatchmaking_RequestLobbyList");
	get(&S.AddStringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter");
	get(&S.AddDistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter");
	get(&S.AddResultCountFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter");
	get(&S.GetLobbyByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
	get(&S.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby");
	get(&S.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
	get(&S.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData");
	get(&S.GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData");
	get(&S.GetNumLobbyMembers, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
	get(&S.GetLobbyMemberByIndex, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
	get(&S.GetLobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner");
	get(&S.SetLobbyJoinable, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
	get(&S.IsAPICallCompleted, "SteamAPI_ISteamUtils_IsAPICallCompleted");
	get(&S.GetAPICallResult, "SteamAPI_ISteamUtils_GetAPICallResult");
	get(&S.GetSteamID, "SteamAPI_ISteamUser_GetSteamID");
	get(&S.GetPersonaName, "SteamAPI_ISteamFriends_GetPersonaName");
	get(&S.GetFriendPersonaName, "SteamAPI_ISteamFriends_GetFriendPersonaName");
	get(&S.SendMessageToUser, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
	get(&S.ReceiveMessagesOnChannel, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
	get(&S.AcceptSessionWithUser, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
	get(&S.ReleaseMessage, "SteamAPI_SteamNetworkingMessage_t_Release");
	get(&S.InitRelayNetworkAccess, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
	get(&S.RegisterCallback, "SteamAPI_RegisterCallback");
	gSteamLoaded = ok;
	return ok;
}

// SteamAPI_RegisterCallback needs an object with the vtable layout of Valve's CCallbackBase (same compiler: MSVC).
class CCallbackBase {
public:
	virtual void Run(void* pvParam) = 0;
	virtual void Run(void* pvParam, bool bIOFailure, SteamAPICall_t hSteamAPICall) = 0;
	virtual int GetCallbackSizeBytes() = 0;
	uint8_t m_nCallbackFlags = 0;
	int m_iCallback = 0;
};

// ------------------------------------------------------------------------------------------------------------
// state

static CRITICAL_SECTION gLock;
static void* gMM = nullptr;   // ISteamMatchmaking
static void* gMsg = nullptr;  // ISteamNetworkingMessages
static void* gUtils = nullptr;
static uint64_t gSelf = 0;

struct Packet { uint64_t from; uint16_t srcPort; std::vector<uint8_t> data; };
static std::map<uint16_t, std::deque<Packet>> gQueues;   // by local (destination) port
static std::map<SOCKET, uint16_t> gSocketPort;          // known local ports of Chloe's sockets
static std::map<uint64_t, uint32_t> gFakeBySteam;       // SteamID -> index (address 240.0.x.y)
static std::map<uint32_t, uint64_t> gSteamByFake;
static int gIdentityOffset = -1;                        // offset of m_identityPeer in SteamNetworkingMessage_t

// host side
static SOCKET gServerSocket = INVALID_SOCKET;
static SteamAPICall_t gCreateCall = 0;
static uint64_t gHostLobby = 0;
static DWORD gLastHello = 0;

// client side
static SteamAPICall_t gListCall = 0;
static std::vector<uint64_t> gLobbies;
static SteamAPICall_t gJoinCall = 0;
static uint64_t gJoinedLobby = 0;
static uint64_t gJoinHost = 0;
static int gJoinState = 0;   // 0 idle, 1 joining, 2 joined (address ready), 3 failed
static bool gJoinConnecting = false;   // lobby joined, waiting for the first message of the host over Steam
static DWORD gJoinConnectStart = 0, gJoinLastHello = 0;
static std::map<SOCKET, bool> gSteamSockets;   // client sockets that sent to Steam addresses

static uint32_t FakeAddrFor(uint64_t steamId);
// ZOOM's xlive.dll calls SteamAPI_Shutdown when the game exits while Chloe's network thread still runs. After that
// no Steam interface may be used (crash on Alt+F4: jump into freed memory), see hkSteamShutdown.
static volatile bool gSteamShutdown = false;

static bool SteamReady() {
	if (gSteamShutdown) return false;
	if (gMM && gMsg && gUtils && gSelf) return true;
	if (!LoadSteam()) return false;
	gMM = S.Matchmaking(); gMsg = S.Messages(); gUtils = S.Utils();
	void* user = S.User();
	if (!gMM || !gMsg || !gUtils || !user) { gMM = gMsg = gUtils = nullptr; return false; }
	gSelf = S.GetSteamID(user);
	if (void* nu = S.NetUtils()) S.InitRelayNetworkAccess(nu);
	Log("Steam ready, own SteamID %llu", gSelf);
	char env[8];
	if (GetEnvironmentVariableA("CHLOESTEAMNET_SELFMAP", env, sizeof(env)) && env[0] == '1') {
		FakeAddrFor(gSelf);
		Log("self test: own SteamID mapped");
	}
	return gSelf != 0;
}

static uint32_t FakeAddrFor(uint64_t steamId) {   // network byte order
	auto it = gFakeBySteam.find(steamId);
	uint32_t idx;
	if (it != gFakeBySteam.end()) idx = it->second;
	else {
		idx = (uint32_t)gFakeBySteam.size() + 1;
		gFakeBySteam[steamId] = idx;
		gSteamByFake[idx] = steamId;
		Log("SteamID %llu = 240.0.%u.%u", steamId, (idx >> 8) & 0xFF, idx & 0xFF);
	}
	return htonl(0xF0000000u | idx);
}
static bool IsFakeAddr(uint32_t addrNet) { return (ntohl(addrNet) & 0xFF000000u) == 0xF0000000u; }
static uint64_t SteamForFake(uint32_t addrNet) {
	auto it = gSteamByFake.find(ntohl(addrNet) & 0x00FFFFFFu);
	return it == gSteamByFake.end() ? 0 : it->second;
}

static SteamNetworkingIdentity Identity(uint64_t steamId) {
	SteamNetworkingIdentity id; memset(&id, 0, sizeof(id));
	id.eType = kIdentitySteamID; id.cbSize = 8; id.steamID64 = steamId;
	return id;
}

// ------------------------------------------------------------------------------------------------------------
// original WS2_32 functions (from the import table of FlatOutUCCustomMP_gcp.asi)

typedef int (WSAAPI* tWSASendTo)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WSAAPI* tWSARecvFrom)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, sockaddr*, LPINT, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
typedef int (WSAAPI* tselect)(int, fd_set*, fd_set*, fd_set*, const timeval*);
typedef int (WSAAPI* tbind)(SOCKET, const sockaddr*, int);
typedef int (WSAAPI* tclosesocket)(SOCKET);
static tWSASendTo oWSASendTo; static tWSARecvFrom oWSARecvFrom; static tselect oselect; static tbind obind; static tclosesocket oclosesocket;

static uint16_t LocalPort(SOCKET s, bool bindIfNeeded) {   // caller holds gLock
	auto it = gSocketPort.find(s);
	if (it != gSocketPort.end()) return it->second;
	sockaddr_in a; int len = sizeof(a);
	if (getsockname(s, (sockaddr*)&a, &len) != 0) {
		if (!bindIfNeeded) return 0;
		sockaddr_in any; memset(&any, 0, sizeof(any)); any.sin_family = AF_INET;
		if (obind(s, (sockaddr*)&any, sizeof(any)) != 0) return 0;
		len = sizeof(a);
		if (getsockname(s, (sockaddr*)&a, &len) != 0) return 0;
	}
	if (a.sin_family != AF_INET) return 0;
	gSocketPort[s] = ntohs(a.sin_port);
	return ntohs(a.sin_port);
}

// ------------------------------------------------------------------------------------------------------------
// pump: Steam -> queues, lobby call results, hello messages (caller holds gLock)

static void SendRaw(uint64_t to, uint8_t type, uint16_t srcPort, uint16_t dstPort, const uint8_t* data, size_t len) {
	if (gSteamShutdown || !gMsg) return;
	std::vector<uint8_t> buf(5 + len);
	buf[0] = type; memcpy(&buf[1], &srcPort, 2); memcpy(&buf[3], &dstPort, 2);
	if (len) memcpy(&buf[5], data, len);
	SteamNetworkingIdentity id = Identity(to);
	// k_nSteamNetworkingSend_UnreliableNoNagle (1) | k_nSteamNetworkingSend_AutoRestartBrokenSession (32)
	int r = S.SendMessageToUser(gMsg, &id, buf.data(), (uint32_t)buf.size(), 1 | 32, kChannel);
	if (r != 1) {
		static DWORD lastLog = 0;
		if (GetTickCount() - lastLog > 2000) { Log("SendMessageToUser(%llu) failed, EResult %d", to, r); lastLog = GetTickCount(); }
	}
}

static void PollCalls() {
	bool failed = false;
	if (gCreateCall && S.IsAPICallCompleted(gUtils, gCreateCall, &failed)) {
		LobbyCreated_t r; memset(&r, 0, sizeof(r));
		bool ok = S.GetAPICallResult(gUtils, gCreateCall, &r, sizeof(r), 513, &failed) && !failed && r.eResult == 1;
		gCreateCall = 0;
		if (!ok) Log("CreateLobby failed (EResult %d)", r.eResult);
		else if (gServerSocket == INVALID_SOCKET) { S.LeaveLobby(gMM, r.lobby); }   // server already closed
		else {
			gHostLobby = r.lobby;
			char host[32]; _snprintf_s(host, sizeof(host), _TRUNCATE, "%llu", gSelf);
			const char* name = "";
			if (void* fr = S.Friends()) name = S.GetPersonaName(fr);
			S.SetLobbyData(gMM, gHostLobby, kKeyProtocol, kProtocol);
			S.SetLobbyData(gMM, gHostLobby, kKeyName, name ? name : "");
			S.SetLobbyData(gMM, gHostLobby, kKeyHost, host);
			S.SetLobbyJoinable(gMM, gHostLobby, true);
			Log("Steam lobby %llu created", gHostLobby);
		}
	}
	if (gListCall && S.IsAPICallCompleted(gUtils, gListCall, &failed)) {
		LobbyMatchList_t r; memset(&r, 0, sizeof(r));
		bool ok = S.GetAPICallResult(gUtils, gListCall, &r, sizeof(r), 510, &failed) && !failed;
		gListCall = 0;
		gLobbies.clear();
		if (ok) {
			for (uint32_t i = 0; i < r.count; i++) {
				uint64_t lobby = S.GetLobbyByIndex(gMM, (int)i);
				const char* host = S.GetLobbyData(gMM, lobby, kKeyHost);
				uint64_t hostId = host ? _strtoui64(host, nullptr, 10) : 0;
				if (!hostId || hostId == gSelf) continue;   // own lobby or broken
				gLobbies.push_back(lobby);
			}
		}
		Log("lobby search: %u found, %u listed", r.count, (unsigned)gLobbies.size());
	}
	if (gJoinCall && S.IsAPICallCompleted(gUtils, gJoinCall, &failed)) {
		LobbyEnter_t r; memset(&r, 0, sizeof(r));
		bool ok = S.GetAPICallResult(gUtils, gJoinCall, &r, sizeof(r), 504, &failed) && !failed && r.response == 1;
		gJoinCall = 0;
		if (ok) {
			const char* host = S.GetLobbyData(gMM, r.lobby, kKeyHost);
			gJoinHost = host ? _strtoui64(host, nullptr, 10) : 0;
			gJoinedLobby = r.lobby;
			if (gJoinHost && S.GetLobbyOwner(gMM, r.lobby) == gJoinHost) {
				FakeAddrFor(gJoinHost);
				// the first Steam connection (NAT traversal / relay) can take several seconds, longer than ENet's
				// 5 s connect timeout: connect to the host first, ENet joins after the host has answered
				gJoinConnecting = true;
				gJoinConnectStart = GetTickCount();
				gJoinLastHello = 0;
				Log("joined Steam lobby %llu, host %llu, connecting over Steam", r.lobby, gJoinHost);
			} else {
				Log("joined Steam lobby %llu, but its host has left", r.lobby);
				S.LeaveLobby(gMM, r.lobby); gJoinedLobby = 0; gJoinState = 3;
			}
		} else {
			Log("JoinLobby failed (response %u)", r.response);
			gJoinState = 3;
		}
	}
}

static void LeaveJoinedLobby();
static void StoreStuntRecord(uint64_t from, const uint8_t* data, int len);
static void StoreStuntName(uint64_t from, const uint8_t* data, int len);
static void StoreHostAIDrivers(uint64_t from, const uint8_t* data, int len);
static void StoreAIDamage(uint64_t from, const uint8_t* data, int len);
static void StoreRaceStatus(uint64_t from, const uint8_t* data, int len);
static void StoreStuntResult(uint64_t from, const uint8_t* data, int len);
static void ResendStuntResult();
static void SendAIDriversToLobby();
static void Pump() {
	if (!SteamReady()) return;
	PollCalls();

	void* msgs[64];
	for (int round = 0; round < 8; round++) {
		int n = S.ReceiveMessagesOnChannel(gMsg, kChannel, msgs, 64);
		if (n <= 0) break;
		for (int i = 0; i < n; i++) {
			uint8_t* m = (uint8_t*)msgs[i];
			const uint8_t* data = *(const uint8_t**)m;
			int len = *(int*)(m + 4);
			if (gIdentityOffset < 0) {   // 12 (pack 4) or 16 (pack 8), detected from the identity type
				gIdentityOffset = (*(int*)(m + 16) == kIdentitySteamID && *(int*)(m + 20) == 8) ? 16 : 12;
				Log("message identity offset %d", gIdentityOffset);
			}
			const SteamNetworkingIdentity* from = (const SteamNetworkingIdentity*)(m + gIdentityOffset);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeHello && gHostLobby)
				SendRaw(from->steamID64, kTypeHelloAck, kServerPort, 0, nullptr, 0);
			if (from->eType == kIdentitySteamID && len >= 5 && (data[0] == kTypeHello || data[0] == kTypeHelloAck)
				&& gJoinConnecting && from->steamID64 == gJoinHost) {
				gJoinConnecting = false;
				gJoinState = 2;
				Log("Steam connection to host %llu ready after %lu ms", gJoinHost, GetTickCount() - gJoinConnectStart);
			}
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeStunt) StoreStuntRecord(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeName) StoreStuntName(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeAIDrivers) StoreHostAIDrivers(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeAIDamage) StoreAIDamage(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeRaceStatus) StoreRaceStatus(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeStuntResult) StoreStuntResult(from->steamID64, data, len);
			if (from->eType == kIdentitySteamID && len >= 5 && data[0] == kTypeData) {
				Packet p; p.from = from->steamID64;
				uint16_t dst; memcpy(&p.srcPort, data + 1, 2); memcpy(&dst, data + 3, 2);
				p.data.assign(data + 5, data + len);
				FakeAddrFor(p.from);
				auto& q = gQueues[dst];
				if (q.size() >= kMaxQueue) q.pop_front();
				q.push_back(std::move(p));
			}
			S.ReleaseMessage(m);
		}
		if (n < 64) break;
	}

	// client: say hello to the host until it answers
	if (gJoinConnecting) {
		if (GetTickCount() - gJoinConnectStart > kConnectTimeout) {
			gJoinConnecting = false;
			gJoinState = 3;
			Log("ERROR: no answer from host %llu over Steam within %lu ms", gJoinHost, kConnectTimeout);
			if (gSteamSockets.empty()) LeaveJoinedLobby();
		} else if (GetTickCount() - gJoinLastHello > 250) {
			gJoinLastHello = GetTickCount();
			SendRaw(gJoinHost, kTypeHello, 0, kServerPort, nullptr, 0);
		}
	}

	// host: open the sessions to everyone in the Steam lobby (no session request needed on our side)
	if (gHostLobby && GetTickCount() - gLastHello > 1000) {
		gLastHello = GetTickCount();
		int n = S.GetNumLobbyMembers(gMM, gHostLobby);
		for (int i = 0; i < n; i++) {
			uint64_t member = S.GetLobbyMemberByIndex(gMM, gHostLobby, i);
			if (member && member != gSelf) SendRaw(member, kTypeHello, kServerPort, 0, nullptr, 0);
		}
		SendAIDriversToLobby();
	}
	ResendStuntResult();
}

// accepts SteamNetworkingMessages sessions (callback 1251, run by SteamAPI_RunCallbacks in ZOOM's xlive.dll)
class SessionRequestCallback : public CCallbackBase {
public:
	void Run(void* p) override {
		const SteamNetworkingIdentity* id = (const SteamNetworkingIdentity*)p;
		if (gMsg && S.AcceptSessionWithUser(gMsg, id))
			Log("accepted Steam session from %llu", id->eType == kIdentitySteamID ? id->steamID64 : 0);
	}
	void Run(void* p, bool, SteamAPICall_t) override { Run(p); }
	int GetCallbackSizeBytes() override { return sizeof(SteamNetworkingIdentity); }
};
static SessionRequestCallback gSessionCallback;
static bool gCallbackRegistered = false;

static void RegisterCallbacksOnce() {
	if (gCallbackRegistered || !SteamReady()) return;
	gSessionCallback.m_iCallback = 1251;   // SteamNetworkingMessagesSessionRequest_t
	S.RegisterCallback(&gSessionCallback, 1251);
	gCallbackRegistered = true;
}

static void LeaveJoinedLobby() {
	if (gJoinedLobby && gMM) { S.LeaveLobby(gMM, gJoinedLobby); Log("left Steam lobby %llu", gJoinedLobby); }
	gJoinedLobby = 0;
}

// ------------------------------------------------------------------------------------------------------------
// WS2_32 hooks

static int WSAAPI hkWSASendTo(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD sent, DWORD flags, const sockaddr* to, int tolen,
                              LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
	static int logged = 0;
	if (logged < 20 && to && tolen >= (int)sizeof(sockaddr_in)) {   // diagnostics: first datagrams of the session
		logged++;
		const sockaddr_in* a = (const sockaddr_in*)to;
		uint32_t ip = ntohl(a->sin_addr.s_addr);
		Log("WSASendTo socket %u -> family %d %u.%u.%u.%u:%u (%lu buffers, overlapped %d)", (unsigned)s, a->sin_family,
			ip >> 24, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, ntohs(a->sin_port), n, ov != nullptr);
	}
	if (ov || !to || tolen < (int)sizeof(sockaddr_in) || to->sa_family != AF_INET || !IsFakeAddr(((const sockaddr_in*)to)->sin_addr.s_addr))
		return oWSASendTo(s, bufs, n, sent, flags, to, tolen, ov, cr);
	EnterCriticalSection(&gLock);
	uint64_t steamId = SteamReady() ? SteamForFake(((const sockaddr_in*)to)->sin_addr.s_addr) : 0;
	uint16_t src = steamId ? LocalPort(s, true) : 0;
	if (!steamId || !src) {
		static DWORD lastErrLog = 0;
		if (GetTickCount() - lastErrLog > 1000) {
			lastErrLog = GetTickCount();
			Log("ERROR: cannot send to placeholder address (SteamID %llu, local port %u, WSA error %d)", steamId, src, WSAGetLastError());
		}
		LeaveCriticalSection(&gLock);
		WSASetLastError(WSAEHOSTUNREACH);
		return SOCKET_ERROR;
	}
	std::vector<uint8_t> data;
	for (DWORD i = 0; i < n; i++) data.insert(data.end(), (uint8_t*)bufs[i].buf, (uint8_t*)bufs[i].buf + bufs[i].len);
	if (s != gServerSocket && !gSteamSockets.count(s)) {
		gSteamSockets[s] = true;
		Log("socket %u (port %u) talks to Steam player %llu", (unsigned)s, src, steamId);
	}
	SendRaw(steamId, kTypeData, src, ntohs(((const sockaddr_in*)to)->sin_port), data.data(), data.size());
	LeaveCriticalSection(&gLock);
	if (sent) *sent = (DWORD)data.size();
	return 0;
}

static int WSAAPI hkWSARecvFrom(SOCKET s, LPWSABUF bufs, DWORD n, LPDWORD recvd, LPDWORD flags, sockaddr* from, LPINT fromlen,
                                LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
	if (ov) return oWSARecvFrom(s, bufs, n, recvd, flags, from, fromlen, ov, cr);
	EnterCriticalSection(&gLock);
	Pump();
	uint16_t port = LocalPort(s, false);
	auto it = port ? gQueues.find(port) : gQueues.end();
	if (it != gQueues.end() && !it->second.empty()) {
		Packet p = std::move(it->second.front());
		it->second.pop_front();
		uint32_t fake = FakeAddrFor(p.from);
		LeaveCriticalSection(&gLock);
		size_t done = 0;
		for (DWORD i = 0; i < n && done < p.data.size(); i++) {
			size_t c = min((size_t)bufs[i].len, p.data.size() - done);
			memcpy(bufs[i].buf, p.data.data() + done, c);
			done += c;
		}
		if (recvd) *recvd = (DWORD)done;
		if (flags) *flags = done < p.data.size() ? MSG_PARTIAL : 0;
		if (from && fromlen && *fromlen >= (int)sizeof(sockaddr_in)) {
			sockaddr_in* a = (sockaddr_in*)from;
			memset(a, 0, sizeof(*a));
			a->sin_family = AF_INET; a->sin_addr.s_addr = fake; a->sin_port = htons(p.srcPort);
			*fromlen = sizeof(sockaddr_in);
		}
		return 0;
	}
	LeaveCriticalSection(&gLock);
	return oWSARecvFrom(s, bufs, n, recvd, flags, from, fromlen, ov, cr);
}

static bool HasQueued(SOCKET s) {   // caller holds gLock
	uint16_t port = LocalPort(s, false);
	if (!port) return false;
	auto it = gQueues.find(port);
	return it != gQueues.end() && !it->second.empty();
}

static int WSAAPI hkselect(int nfds, fd_set* rd, fd_set* wr, fd_set* ex, const timeval* tv) {
	if (!rd || rd->fd_count == 0) return oselect(nfds, rd, wr, ex, tv);
	fd_set r0 = *rd, w0 = {}, e0 = {};
	if (wr) w0 = *wr;
	if (ex) e0 = *ex;
	EnterCriticalSection(&gLock);
	for (u_int i = 0; i < r0.fd_count; i++) LocalPort(r0.fd_array[i], true);   // ENet clients are unbound until the first send
	LeaveCriticalSection(&gLock);
	DWORD timeout = tv ? (DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000) : INFINITE;
	DWORD start = GetTickCount();
	for (;;) {
		fd_set rr = r0, ww, ee;
		if (wr) ww = w0;
		if (ex) ee = e0;
		timeval zero = { 0, 0 };
		int count = oselect(nfds, &rr, wr ? &ww : nullptr, ex ? &ee : nullptr, &zero);
		if (count == SOCKET_ERROR) return SOCKET_ERROR;
		EnterCriticalSection(&gLock);
		Pump();
		for (u_int i = 0; i < r0.fd_count; i++) {
			SOCKET s = r0.fd_array[i];
			if (!HasQueued(s)) continue;
			bool already = false;
			for (u_int j = 0; j < rr.fd_count; j++) if (rr.fd_array[j] == s) already = true;
			if (!already && rr.fd_count < FD_SETSIZE) { rr.fd_array[rr.fd_count++] = s; count++; }
		}
		LeaveCriticalSection(&gLock);
		if (count > 0 || GetTickCount() - start >= timeout) {
			*rd = rr;
			if (wr) *wr = ww;
			if (ex) *ex = ee;
			return count;
		}
		Sleep(1);
	}
}

static int WSAAPI hkbind(SOCKET s, const sockaddr* a, int len) {
	int r = obind(s, a, len);
	if (r == 0 && a && a->sa_family == AF_INET && ntohs(((const sockaddr_in*)a)->sin_port) == kServerPort) {
		EnterCriticalSection(&gLock);
		gServerSocket = s;
		gSocketPort[s] = kServerPort;
		if (SteamReady()) {
			RegisterCallbacksOnce();
			gCreateCall = S.CreateLobby(gMM, 2 /* k_ELobbyTypePublic */, 32);
			Log("server socket bound on port %u, creating Steam lobby", kServerPort);
		} else Log("server socket bound, but Steam is not available");
		LeaveCriticalSection(&gLock);
	}
	return r;
}

static int WSAAPI hkclosesocket(SOCKET s) {
	EnterCriticalSection(&gLock);
	if (s == gServerSocket) {
		gServerSocket = INVALID_SOCKET;
		if (gHostLobby && gMM) { S.LeaveLobby(gMM, gHostLobby); Log("server closed, Steam lobby %llu left", gHostLobby); }
		gHostLobby = 0;
	}
	if (gSteamSockets.erase(s) && gSteamSockets.empty()) LeaveJoinedLobby();
	auto it = gSocketPort.find(s);
	if (it != gSocketPort.end()) { gQueues.erase(it->second); gSocketPort.erase(it); }
	LeaveCriticalSection(&gLock);
	return oclosesocket(s);
}

static void(__cdecl* oSteamShutdown)() = nullptr;
static void __cdecl hkSteamShutdown() {
	EnterCriticalSection(&gLock);
	gSteamShutdown = true;
	gMM = gMsg = gUtils = nullptr;
	LeaveCriticalSection(&gLock);
	Log("SteamAPI_Shutdown: Steam networking stopped");
	if (oSteamShutdown) oSteamShutdown();
}

static bool PatchImport(HMODULE mod, const char* dll, const char* name, void* hook, void** orig) {
	auto base = (uint8_t*)mod;
	auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
	auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return false;
	for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
		if (_stricmp((char*)(base + d->Name), dll) != 0 || !d->OriginalFirstThunk) continue;
		auto names = (IMAGE_THUNK_DATA*)(base + d->OriginalFirstThunk);
		auto iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, iat++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			if (strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, name) != 0) continue;
			DWORD old;
			VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			*orig = (void*)iat->u1.Function;
			iat->u1.Function = (uintptr_t)hook;
			VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
			return true;
		}
	}
	return false;
}

static bool PatchImports(HMODULE mod) {
	auto base = (uint8_t*)mod;
	auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
	auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return false;
	int patched = 0;
	for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
		if (_stricmp((char*)(base + d->Name), "WS2_32.dll") != 0 || !d->OriginalFirstThunk) continue;
		auto names = (IMAGE_THUNK_DATA*)(base + d->OriginalFirstThunk);
		auto iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
		for (; names->u1.AddressOfData; names++, iat++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			const char* name = (char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
			void* hook = nullptr; void** orig = nullptr;
			if (!strcmp(name, "WSASendTo")) { hook = (void*)hkWSASendTo; orig = (void**)&oWSASendTo; }
			else if (!strcmp(name, "WSARecvFrom")) { hook = (void*)hkWSARecvFrom; orig = (void**)&oWSARecvFrom; }
			else if (!strcmp(name, "select")) { hook = (void*)hkselect; orig = (void**)&oselect; }
			else if (!strcmp(name, "bind")) { hook = (void*)hkbind; orig = (void**)&obind; }
			else if (!strcmp(name, "closesocket")) { hook = (void*)hkclosesocket; orig = (void**)&oclosesocket; }
			if (!hook) continue;
			DWORD old;
			VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
			*orig = (void*)iat->u1.Function;
			iat->u1.Function = (uintptr_t)hook;
			VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
			patched++;
		}
	}
	return patched == 5;
}

static bool ReplaceCall(uint8_t* site, uintptr_t original, uintptr_t replacement) {
	int32_t oldDisplacement;
	memcpy(&oldDisplacement, site + 1, sizeof(oldDisplacement));
	DWORD protection;
	if (site[0] != 0xE8 || uintptr_t(site + 5) + oldDisplacement != original ||
			!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &protection)) return false;
	const int32_t displacement = int32_t(replacement - uintptr_t(site + 5));
	memcpy(site + 1, &displacement, sizeof(displacement));
	FlushInstructionCache(GetCurrentProcess(), site, 5);
	VirtualProtect(site, 5, protection, &protection);
	return true;
}

static uint8_t* gChloeMP;
static constexpr uintptr_t kChloeMPBase = 0x62840000;
static constexpr int kDerbyGridSize = 8;
static const auto gNativeGrid = reinterpret_cast<void(__stdcall*)(uint8_t*)>(0x469F50);

static bool IsDerbyArena(int level) { return level >= 31 && level <= 36; }

// The MP packet already transmits one dense slot per human. Fill the packet in roster order for Derby.
// Chloe takes the return value modulo count and marks the chosen slot as used.
static int __stdcall ChooseDerbyRosterSlot(const uint8_t* used, int count) {
	const int rules = *reinterpret_cast<const int*>(gChloeMP + (0x62D5D0C4 - kChloeMPBase));
	const int level = *reinterpret_cast<const int*>(gChloeMP + (0x62D5D0CC - kChloeMPBase));
	if (rules == 3 && IsDerbyArena(level)) {
		for (int slot = 0; slot < count; ++slot) if (!used[slot]) return slot;
	}
	return reinterpret_cast<int(*)()>(gChloeMP + (0x629259D8 - kChloeMPBase))();
}

static __declspec(naked) void DerbyRosterStub() {
	__asm {
		push esi  // number of drivers in Chloe's roster
		push ebp  // occupied flags for this start packet
		call ChooseDerbyRosterSlot
		ret
	}
}

// Called after Player objects exist but before the vanilla placement routine reads nStartPosition.
static void __stdcall AssignDerbyGrid(uint8_t* flow) {
	const int level = *reinterpret_cast<int*>(flow + 0x4CC);
	if (gChloeMP && *reinterpret_cast<int*>(gChloeMP + (0x62D58CF8 - kChloeMPBase)) &&
			*reinterpret_cast<int*>(flow + 0x514) == 3 && IsDerbyArena(level)) {
		auto begin = *reinterpret_cast<uint8_t***>(flow + 0x2824);
		auto end = *reinterpret_cast<uint8_t***>(flow + 0x2828);
		const int count = int(end - begin);
		const int humans = reinterpret_cast<int(*)()>(gChloeMP + (0x628D3BB0 - kChloeMPBase))();
		const int local = *reinterpret_cast<int*>(gChloeMP + (0x62D58E98 - kChloeMPBase));
		if (humans > 0 && humans <= count && count <= 32 && local >= 0 && local < humans) {
			for (int i = 0; i < count; ++i) {
				// Index 0 is local. The following human proxies are in roster order excluding local.
				const int slot = i == 0 ? local : i <= local ? i - 1 : i;
				*reinterpret_cast<int*>(begin[i] + 0x2CC) = slot % kDerbyGridSize + 1;
			}
			Log("derby grid: level %d, cars %d, human drivers %d, local grid slot %d", level, count, humans, local + 1);
			return;
		}
		Log("derby grid: inconsistent roster, using native grid (cars %d, humans %d, local %d)", count, humans, local);
	}
	gNativeGrid(flow);
}

// Derby uses the vanilla straight nitro bar in both HUDs. Use its matching flame parameters only for that draw call.
static uintptr_t gDrawNitro = 0x4E5810;
static __declspec(naked) void DerbyNitroStub() {
	__asm {
		push ebp
		mov ebp, esp
		push dword ptr ds:[0x84ABDC]
		push dword ptr ds:[0x84ABE0]
		push dword ptr ds:[0x84ABE4]
		push dword ptr ds:[0x84ABEC]
		mov dword ptr ds:[0x84ABDC], 0x3F333333  // x scale 0.7
		mov dword ptr ds:[0x84ABE0], 0x3F800000  // y scale 1.0
		mov dword ptr ds:[0x84ABE4], 0x42180000  // start offset 38
		mov dword ptr ds:[0x84ABEC], 0x41B00000  // y offset 22
		push dword ptr [ebp + 20]
		push dword ptr [ebp + 16]
		push dword ptr [ebp + 12]
		push dword ptr [ebp + 8]
		call dword ptr [gDrawNitro]
		pop dword ptr ds:[0x84ABEC]
		pop dword ptr ds:[0x84ABE4]
		pop dword ptr ds:[0x84ABE0]
		pop dword ptr ds:[0x84ABDC]
		pop ebp
		ret 16
	}
}

// Chloe's multiplayer gives every player its own grid slot each frame:
//   FlatOutUCCustomMP_gcp.asi 0x628F94AA: mov [eax+0x2CC], edx   (Player.nStartPosition = sessionSlot % numStartPoints + 1)
// In stunts all players must start at the stunt's start point (several stunts share one map), so this store is
// replaced by a call that writes the stunt start point on the stunt levels 47..58 and Chloe's value otherwise.
static int StuntStartForLevel(uint32_t levelId);
static uint32_t __stdcall ChloeStartPositionValue(uint32_t chloeValue) {
	auto gf = *(uint8_t**)0x9298FAC;
	int pos = gf ? StuntStartForLevel((uint32_t)*(int*)(gf + 0x4CC)) : 0;   // PreRace.nLevel
	return pos ? (uint32_t)pos : chloeValue;
}
static __declspec(naked) void ChloeStartPositionStub() {
	__asm {
		push ecx
		push edx
		push eax                      // player
		push edx                      // argument: Chloe's value
		call ChloeStartPositionValue  // stdcall, result in eax
		mov ecx, eax
		pop eax
		mov dword ptr [eax + 0x2CC], ecx
		pop edx                       // all registers as after the original store
		pop ecx
		ret
	}
}

static void PatchChloeStartPosition(HMODULE mp) {
	// The plugin is usually relocated and Windows writes the actual base into the loaded header, so the address is
	// converted with the preferred base of the 1.83 file (0x62840000).
	auto base = (uint8_t*)mp;
	uintptr_t rva = 0x628F94AA - 0x62840000;
	uint8_t* p = base + rva;
	static const uint8_t expected[6] = { 0x89, 0x90, 0xCC, 0x02, 0x00, 0x00 };
	if (memcmp(p, expected, 6) != 0) { Log("start position patch skipped: unexpected code in FlatOutUCCustomMP_gcp.asi"); return; }
	DWORD old;
	if (!VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old)) return;
	p[0] = 0xE8;
	int32_t rel = (int32_t)((uintptr_t)&ChloeStartPositionStub - ((uintptr_t)p + 5));
	memcpy(p + 1, &rel, 4);
	p[5] = 0x90;
	VirtualProtect(p, 6, old, &old);
	Log("start position of FlatOutUCCustomMP_gcp.asi patched for stunts");
}

static bool gImportsPatched = false;
static void TryPatchChloeMP() {
	if (gImportsPatched) return;
	HMODULE mp = GetModuleHandleA("FlatOutUCCustomMP_gcp.asi");
	if (!mp) return;
	gImportsPatched = PatchImports(mp);
	if (gImportsPatched) {
		PatchChloeStartPosition(mp);
		auto base = reinterpret_cast<uint8_t*>(mp);
		if (ReplaceCall(base + (0x628DC8F0 - kChloeMPBase), uintptr_t(base) + (0x629259D8 - kChloeMPBase),
				uintptr_t(&DerbyRosterStub))) gChloeMP = base;
		else Log("Derby roster patch unavailable: unexpected code or memory protection");
	}
	Log(gImportsPatched ? "network functions of FlatOutUCCustomMP_gcp.asi redirected" : "ERROR: could not patch FlatOutUCCustomMP_gcp.asi imports");
}

// ------------------------------------------------------------------------------------------------------------
// Lua (game's Lua 5.0 with wide strings, addresses from source/nya-common-fouc/types/LUA.h)

static auto lua_pushnumber   = (int(*)(void*, float))0x633550;
static auto lua_tolstring    = (const wchar_t*(*)(void*, int, void*))0x6332B0;
static auto luaL_checknumber = (float(*)(void*, int))0x634DD0;
static auto lua_setfield     = (void(*)(void*, int, const char*))0x633D20;
static auto lua_pushcfunction= (void(*)(void*, void*, int))0x633750;
static auto lua_pushboolean  = (int(*)(void*, int))0x633870;
static auto lua_pushlstring  = (int(*)(void*, const wchar_t*, size_t))0x6335D0;

// Keep the game's random car/class/cup selection, but limit the garage candidates for the Steam menu action.
static std::bitset<1024> gInstantActionCars;
static bool gInstantActionFilterInstalled = false;
static bool gInstantActionFilterActive = false;
static uint8_t* GameFlowPtr();

static int __stdcall InstantActionCarAllowed(unsigned id) {
	return !gInstantActionFilterActive || (id < gInstantActionCars.size() && gInstantActionCars[id]);
}

static uintptr_t gInstantActionInclude = 0x467E92;
static uintptr_t gInstantActionSkip = 0x467EB4;
static __declspec(naked) void InstantActionFilterStub() {
	__asm {
		cmp eax, 2                // original check: garage classes 1..3 (eax contains class minus one)
		ja skipCar
		pushad
		push ebx                  // database ID for the native garage candidate
		call InstantActionCarAllowed
		test eax, eax
		popad
		jz skipCar
		jmp dword ptr [gInstantActionInclude]
	skipCar:
		jmp dword ptr [gInstantActionSkip]
	}
}

static void PlaceInstantActionFilter() {
	auto site = reinterpret_cast<uint8_t*>(0x467E8D);
	static const uint8_t expected[] = { 0x83, 0xF8, 0x02, 0x77, 0x22 };
	DWORD old;
	if (memcmp(site, expected, sizeof(expected)) != 0 ||
			!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &old)) {
		Log("instant action filter unavailable: unexpected code or memory protection");
		return;
	}
	const int32_t relative = (int32_t)((uintptr_t)&InstantActionFilterStub - ((uintptr_t)site + 5));
	site[0] = 0xE9;
	memcpy(site + 1, &relative, sizeof(relative));
	FlushInstructionCache(GetCurrentProcess(), site, sizeof(expected));
	VirtualProtect(site, sizeof(expected), old, &old);
	gInstantActionFilterInstalled = true;
}

static int L_PrepareInstantAction(void* L) {
	gInstantActionFilterActive = false;
	gInstantActionCars.reset();
	auto gf = GameFlowPtr();
	auto input = reinterpret_cast<const char*>(lua_tolstring(L, 1, nullptr));
	if (gInstantActionFilterInstalled && gf && input) {
		const int count = *reinterpret_cast<const int*>(gf + 0x325C);
		const auto classes = *reinterpret_cast<const uint8_t* const*>(gf + 0x3264);
		const auto entries = *reinterpret_cast<const uint8_t* const*>(gf + 0x32AC);
		// Intersect with the exact native list criteria so the game's random modulo cannot see an empty list.
		while (*input && classes && entries) {
			char* end;
			const long id = std::strtol(input, &end, 10);
			if (input == end || (*end != ',' && *end != '\0')) break;
			if (id >= 0 && id < count && (unsigned)id < gInstantActionCars.size() &&
					classes[id] >= 1 && classes[id] <= 3 && *reinterpret_cast<const int*>(entries + id * 0x20) == 0) {
				gInstantActionCars.set(id);
			}
			input = *end ? end + 1 : end;
		}
		gInstantActionFilterActive = gInstantActionCars.any();
		Log("instant action: %u enabled cars with installed skins", (unsigned)gInstantActionCars.count());
	}
	lua_pushboolean(L, gInstantActionFilterActive);
	return 1;
}

static void PushString(void* L, const std::wstring& s) { lua_pushlstring(L, s.c_str(), (s.length() + 1) * 2); }

static std::wstring Widen(const char* utf8) {
	if (!utf8 || !*utf8) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
	std::wstring w(n > 0 ? n - 1 : 0, L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
	return w;
}

static int L_IsAvailable(void* L) {
	EnterCriticalSection(&gLock);
	bool ok = gImportsPatched && SteamReady();
	LeaveCriticalSection(&gLock);
	lua_pushboolean(L, ok);
	return 1;
}

static bool VideoPreviewEnabled();
static int L_RefreshLobbies(void*) {
	if (VideoPreviewEnabled()) {
		const auto* hash = *reinterpret_cast<const uint8_t* const*>(0x825520);
		if (hash) {
			char text[41]{};
			for (int i = 0; i < 20; ++i)
				sprintf_s(text + i * 2, sizeof(text) - i * 2, "%02x", static_cast<unsigned>(hash[i] ^ 0x53));
			Log("stunt video: engine script checksum %s", text);
		}
	}
	EnterCriticalSection(&gLock);
	TryPatchChloeMP();
	if (SteamReady() && !gListCall) {
		RegisterCallbacksOnce();
		S.AddStringFilter(gMM, kKeyProtocol, kProtocol, 0 /* k_ELobbyComparisonEqual */);
		S.AddDistanceFilter(gMM, 3 /* k_ELobbyDistanceFilterWorldwide */);
		S.AddResultCountFilter(gMM, 50);
		gListCall = S.RequestLobbyList(gMM);
		gLobbies.clear();
	}
	LeaveCriticalSection(&gLock);
	return 0;
}

static int L_IsRefreshing(void* L) {
	EnterCriticalSection(&gLock);
	Pump();
	bool busy = gListCall != 0;
	LeaveCriticalSection(&gLock);
	lua_pushboolean(L, busy);
	return 1;
}

static int L_GetNumLobbies(void* L) {
	EnterCriticalSection(&gLock);
	int n = (int)gLobbies.size();
	LeaveCriticalSection(&gLock);
	lua_pushnumber(L, (float)n);
	return 1;
}

static int L_GetLobbyLabel(void* L) {   // "Name  (3 players)"
	int i = (int)luaL_checknumber(L, 1) - 1;
	std::wstring label = L"---";
	EnterCriticalSection(&gLock);
	if (i >= 0 && i < (int)gLobbies.size() && SteamReady()) {
		std::wstring name = Widen(S.GetLobbyData(gMM, gLobbies[i], kKeyName));
		if (name.empty()) name = L"?";
		if (name.length() > 24) name = name.substr(0, 24);
		int members = S.GetNumLobbyMembers(gMM, gLobbies[i]);
		wchar_t buf[96];
		swprintf_s(buf, L"%s  (%d %s)", name.c_str(), members, members == 1 ? L"PLAYER" : L"PLAYERS");
		label = buf;
	}
	LeaveCriticalSection(&gLock);
	PushString(L, label);
	return 1;
}

static int L_JoinLobby(void* L) {
	int i = (int)luaL_checknumber(L, 1) - 1;
	bool started = false;
	EnterCriticalSection(&gLock);
	TryPatchChloeMP();
	if (i >= 0 && i < (int)gLobbies.size() && SteamReady() && !gJoinCall) {
		RegisterCallbacksOnce();
		LeaveJoinedLobby();
		gJoinHost = 0;
		gJoinConnecting = false;
		gJoinState = 1;
		gJoinCall = S.JoinLobby(gMM, gLobbies[i]);
		started = true;
		Log("joining Steam lobby %llu", gLobbies[i]);
	}
	LeaveCriticalSection(&gLock);
	lua_pushboolean(L, started);
	return 1;
}

static int L_GetJoinState(void* L) {
	EnterCriticalSection(&gLock);
	Pump();
	int st = gJoinState;
	LeaveCriticalSection(&gLock);
	lua_pushnumber(L, (float)st);
	return 1;
}

static int L_GetJoinAddress(void* L) {   // placeholder address of the lobby host for ChloeNet_JoinGame
	std::wstring addr = L"";
	EnterCriticalSection(&gLock);
	if (gJoinHost) {   // independent of gJoinState (the script may reset the state first)
		uint32_t a = ntohl(FakeAddrFor(gJoinHost));
		wchar_t buf[32];
		swprintf_s(buf, L"%u.%u.%u.%u", (a >> 24) & 0xFF, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
		addr = buf;
	}
	LeaveCriticalSection(&gLock);
	PushString(L, addr);
	return 1;
}

static int L_ResetJoin(void*) {   // after ChloeNet_JoinGame was called (lobby stays joined while connected)
	EnterCriticalSection(&gLock);
	if (gJoinState != 1) gJoinState = 0;
	LeaveCriticalSection(&gLock);
	return 0;
}

static int L_LeaveLobby(void*) {   // cancel: leave a joined lobby that is not used by a connection
	EnterCriticalSection(&gLock);
	if (gSteamSockets.empty()) LeaveJoinedLobby();
	gJoinConnecting = false;
	gJoinState = 0;
	LeaveCriticalSection(&gLock);
	return 0;
}

// Lobby values for the tournament mode (chloecustom_tournament.bed): the host writes a plain ASCII string into the
// Steam lobby data (key "cc_<key>", max. 8 KB), every member of the lobby reads it (also players joining later).
// Values are narrow Lua strings (bytes), not the game's wide strings; the script hex-encodes names itself.
static auto lua_pushnil_cc = (int(*)(void*))0x633520;
static int L_PrepareStuntVideo(void*);

static int L_SetLobbyValue(void* L) {   // host only; returns true if written
	size_t klen = 0, vlen = 0;
	auto key = (const char*)lua_tolstring(L, 1, &klen);
	auto val = (const char*)lua_tolstring(L, 2, &vlen);
	bool ok = false;
	EnterCriticalSection(&gLock);
	if (key && val && klen > 0 && klen < 200 && vlen < 8000 && gHostLobby && SteamReady()) {
		std::string k = "cc_" + std::string(key, klen);
		std::string v(val, vlen);
		ok = S.SetLobbyData(gMM, gHostLobby, k.c_str(), v.c_str());
	}
	LeaveCriticalSection(&gLock);
	lua_pushboolean(L, ok);
	return 1;
}

static int L_GetLobbyValue(void* L) {   // nil if not in a lobby or the value is not set
	size_t klen = 0;
	auto key = (const char*)lua_tolstring(L, 1, &klen);
	std::string v;
	bool found = false;
	EnterCriticalSection(&gLock);
	uint64_t lobby = gHostLobby ? gHostLobby : gJoinedLobby;
	if (key && klen > 0 && klen < 200 && lobby && SteamReady()) {
		std::string k = "cc_" + std::string(key, klen);
		const char* s = S.GetLobbyData(gMM, lobby, k.c_str());
		if (s && *s) { v = s; found = true; }
	}
	LeaveCriticalSection(&gLock);
	if (found) lua_pushlstring(L, (const wchar_t*)v.c_str(), v.length());
	else lua_pushnil_cc(L);
	return 1;
}

static int L_Log(void* L) {   // narrow Lua string into ChloeSteamNet.log (tournament diagnostics)
	size_t len = 0;
	auto s = (const char*)lua_tolstring(L, 1, &len);
	if (s && len > 0) {
		std::string line(s, len < 1000 ? len : 1000);
		Log("lua: %s", line.c_str());
	}
	return 0;
}

// Values that survive the reload of the menu scripts: Fouc.exe runs data/scripts/main.bed again every time the
// menus start (after each race), so all Lua globals are lost. Used by the tournament and the lobby defaults.
static auto lua_type_cc = (int(*)(void*, int))0x632F20;
static std::map<std::string, std::string> gLuaStore;

static int L_StoreSet(void* L) {   // ChloeSteam_StoreSet(key, value); value nil removes the key
	size_t klen = 0, vlen = 0;
	auto key = (const char*)lua_tolstring(L, 1, &klen);
	if (!key || klen == 0) return 0;
	std::string k(key, klen);
	if (lua_type_cc(L, 2) == 0 /* LUA_TNIL */) { gLuaStore.erase(k); return 0; }
	auto val = (const char*)lua_tolstring(L, 2, &vlen);
	if (val) gLuaStore[k] = std::string(val, vlen);
	return 0;
}

static int L_StoreGet(void* L) {   // nil if not set
	size_t klen = 0;
	auto key = (const char*)lua_tolstring(L, 1, &klen);
	auto it = key ? gLuaStore.find(std::string(key, klen)) : gLuaStore.end();
	if (it == gLuaStore.end()) lua_pushnil_cc(L);
	else lua_pushlstring(L, (const wchar_t*)it->second.data(), it->second.size());
	return 1;
}

// ------------------------------------------------------------------------------------------------------------
// AI drivers chosen by the host in Chloe's lobby (rows "AI SLOT" / "AI DRIVER", multiplayer_overrides).
// The game gives the k-th AI slot the profile (k mod NumAIProfiles)+1 (FUN_004698B0, Chloe patches the modulo at
// 0x4698CA) and the name with the same index. The call of the AIPlayer constructor at 0x46991D
// (call 0x4091C0: ecx = PlayerInfo index + 1, stack: memory, profile id, class; ebp = PlayerInfo index) is redirected:
// in Chloe's multiplayer the j-th real AI slot (after the slots of the other players) gets the j-th chosen driver and
// its name from Config/AIConfig.toml. Saved in Savegame/ChloeCustom_AIDrivers.txt, sent by the host to all clients.

static std::vector<uint64_t> LobbyPeers();
static const int kMaxAIDrivers = 12;
static int gAIDrivers[kMaxAIDrivers] = {};             // host settings: 0 = none, 1..11 = profile
static std::vector<int> gHostAIDrivers;                 // client: list received from the host
static bool gAIDriversDirty = false;
static std::vector<std::wstring> gAINames;              // AI1.. names from Config/AIConfig.toml

static std::string GameFile(const char* rel) {
	char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (char* s = strrchr(path, '\\')) s[1] = 0;
	return std::string(path) + rel;
}

static void LoadAINames() {
	static const wchar_t* defaults[] = { L"JACK BENTON", L"KATIE JACKSON", L"SOFIA MARTINEZ", L"SALLY TAYLOR", L"JASON WALKER",
		L"RAY CARTER", L"FRANK MALCOV", L"CURTIS WOLFE", L"LEWIS DURAN", L"LEI BING", L"JILL RICHARDS" };
	gAINames.assign(std::begin(defaults), std::end(defaults));
	FILE* f = nullptr;
	if (fopen_s(&f, GameFile("Config\\AIConfig.toml").c_str(), "r") != 0 || !f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		int n = 0; char name[128] = {};
		if (sscanf_s(line, " AI%d = \"%127[^\"]\"", &n, name, (unsigned)sizeof(name)) == 2 && n >= 1 && n <= 31) {
			if ((int)gAINames.size() < n) gAINames.resize(n);
			gAINames[n - 1] = Widen(name);
		}
	}
	fclose(f);
}

static void LoadAIDrivers() {
	FILE* f = nullptr;
	if (fopen_s(&f, GameFile("Savegame\\ChloeCustom_AIDrivers.txt").c_str(), "r") != 0 || !f) return;
	for (int i = 0; i < kMaxAIDrivers; i++) if (fscanf_s(f, "%d", &gAIDrivers[i]) != 1) break;
	fclose(f);
}

static void SaveAIDrivers() {
	FILE* f = nullptr;
	if (fopen_s(&f, GameFile("Savegame\\ChloeCustom_AIDrivers.txt").c_str(), "w") != 0 || !f) return;
	for (int i = 0; i < kMaxAIDrivers; i++) fprintf(f, "%d ", gAIDrivers[i]);
	fprintf(f, "\n");
	fclose(f);
}

static std::vector<int> CompactAIDrivers() {
	std::vector<int> list;
	for (int i = 0; i < kMaxAIDrivers; i++) if (gAIDrivers[i] > 0) list.push_back(gAIDrivers[i]);
	return list;
}

static void StoreHostAIDrivers(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	if (from != gJoinHost || len < 5) return;
	std::vector<int> list(data + 5, data + len);
	if (list != gHostAIDrivers) { gHostAIDrivers = list; Log("AI drivers received from the host: %u", (unsigned)list.size()); }
}

static void SendAIDriversToLobby() {   // caller holds gLock; host only
	if (!gHostLobby) return;
	auto list = CompactAIDrivers();
	std::vector<uint8_t> data;
	for (int v : list) data.push_back((uint8_t)v);
	int n = S.GetNumLobbyMembers(gMM, gHostLobby);
	for (int i = 0; i < n; i++) {
		uint64_t m = S.GetLobbyMemberByIndex(gMM, gHostLobby, i);
		if (m && m != gSelf) SendRaw(m, kTypeAIDrivers, 0, 0, data.data(), data.size());
	}
	gAIDriversDirty = false;
}

static int __stdcall MapAIProfile(int playerInfoIndex, int profile) {
	auto gf = GameFlowPtr();
	if (!gf) return profile;
	EnterCriticalSection(&gLock);
	bool mp = !gSocketPort.empty();
	std::vector<int> list = gJoinedLobby ? gHostAIDrivers : (gHostLobby ? CompactAIDrivers() : std::vector<int>());
	int others = mp ? (int)LobbyPeers().size() : 0;
	LeaveCriticalSection(&gLock);
	if (!mp || list.empty()) return profile;
	int locals = 0;
	for (int i = 0; i < 32; i++) if (*(int*)(gf + 0xAE8 + i * 0x50 + 0x2C) == 1) locals++;
	int j = playerInfoIndex - locals - others;                // index among the real AI slots
	if (j < 0 || j >= (int)list.size()) return profile;
	int p = list[j];
	if (p < 1 || p > (int)gAINames.size()) return profile;
	wcsncpy_s((wchar_t*)(gf + 0xAE8 + playerInfoIndex * 0x50), 16, gAINames[p - 1].c_str(), _TRUNCATE);
	Log("AI slot %d (real AI %d): profile %d -> %d", playerInfoIndex, j + 1, profile, p);
	return p;
}

static uintptr_t gAICreateTarget = 0x4091C0;
static __declspec(naked) void AICreateStub() {
	__asm {
		pushad
		push dword ptr [esp + 0x28]   // profile id (return address + memory before it, 32 bytes of pushad)
		push ebp                      // PlayerInfo index
		call MapAIProfile             // stdcall, result in eax
		mov dword ptr [esp + 0x28], eax
		popad
		jmp dword ptr [gAICreateTarget]
	}
}

// ------------------------------------------------------------------------------------------------------------
// Damage that a client deals to an AI car. The AI runs on the host. On the client the AI cars are copies whose
// state (also the damage) comes from the host, and on the host the client's car is only a copy, so the collision
// usually causes no damage there (test 19.11.2026: AI rammed by the client got no damage, by the host yes).
// Fouc.exe adds collision damage in FUN_00426d20 (Car +0x7AB8, old value at [esp+0x28]). At 0x428041, after the
// write, a hook reports increases on real AI cars; the client sends them to the host (kTypeAIDamage), which adds them.

static int gRealAIFirst = -1;          // PlayerInfo index of the first real AI in the current MP race, -1 = none
static float gAIDamageOut[32] = {};    // client: damage to send (game thread writes, RaceTick sends under gLock)
static float gAIDamageIn[32] = {};     // host: damage received (network thread, gLock)

static void StoreAIDamage(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	if (!gHostLobby || len != 5 + (int)sizeof(float)) return;
	int idx = data[1] | (data[2] << 8);
	float d; memcpy(&d, data + 5, sizeof(d));
	if (idx < 0 || idx >= 32 || !(d > 0.f) || d > 10.f) return;
	(void)from;
	gAIDamageIn[idx] += d;
}

static void __stdcall OnCarDamage(uint8_t* car, float oldDamage) {
	if (gRealAIFirst < 0 || !car) return;
	float d = *(float*)(car + 0x7AB8) - oldDamage;
	if (!(d > 0.f)) return;
	auto ply = *(uint8_t**)(car + 0x469C);
	if (!ply) return;
	int idx = *(int*)(ply + 0x2C4) - 1;
	if (idx < gRealAIFirst || idx >= 32) return;
	if (!gJoinedLobby) return;   // host: own collisions count already
	gAIDamageOut[idx] += d;
}

static uintptr_t gDamageHookReturn = 0x428047;
static __declspec(naked) void DamageHookStub() {
	__asm {
		pushad
		push dword ptr [esp + 0x48]   // old damage ([esp+0x28] before pushad)
		push esi                      // Car
		call OnCarDamage
		popad
		lea edx, [esi + 0x192C]       // original instruction
		jmp dword ptr [gDamageHookReturn]
	}
}

static void PlaceDamageHook() {
	auto p = (uint8_t*)0x428041;
	static const uint8_t expected[6] = { 0x8D, 0x96, 0x2C, 0x19, 0x00, 0x00 };   // lea edx, [esi+0x192C]
	if (memcmp(p, expected, 6) != 0) { Log("damage hook skipped: unexpected code at 0x428041"); return; }
	DWORD old;
	VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
	p[0] = 0xE9;
	int32_t rel = (int32_t)((uintptr_t)&DamageHookStub - ((uintptr_t)p + 5));
	memcpy(p + 1, &rel, 4);
	p[5] = 0x90;
	VirtualProtect(p, 6, old, &old);
}

static void PlaceAIDriverHook() {
	auto p = (uint8_t*)0x46991D;
	static const uint8_t expected[5] = { 0xE8, 0x9E, 0xF8, 0xF9, 0xFF };   // call 0x4091C0
	if (memcmp(p, expected, 5) != 0) { Log("AI driver hook skipped: unexpected code at 0x46991D"); return; }
	DWORD old;
	VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
	int32_t rel = (int32_t)((uintptr_t)&AICreateStub - ((uintptr_t)p + 5));
	memcpy(p + 1, &rel, 4);
	VirtualProtect(p, 5, old, &old);
}

static int L_SetAIDriver(void* L) {   // (slot 1..12, profile 0..11)
	int slot = (int)luaL_checknumber(L, 1), profile = (int)luaL_checknumber(L, 2);
	if (slot >= 1 && slot <= kMaxAIDrivers && profile >= 0 && profile <= (int)gAINames.size()) {
		EnterCriticalSection(&gLock);
		if (gAIDrivers[slot - 1] != profile) { gAIDrivers[slot - 1] = profile; gAIDriversDirty = true; SaveAIDrivers(); }
		if (gAIDriversDirty && SteamReady()) SendAIDriversToLobby();
		LeaveCriticalSection(&gLock);
	}
	return 0;
}

static int L_GetAIDriver(void* L) {
	int slot = (int)luaL_checknumber(L, 1);
	lua_pushnumber(L, (float)(slot >= 1 && slot <= kMaxAIDrivers ? gAIDrivers[slot - 1] : 0));
	return 1;
}

static int L_GetAIDriverCount(void* L) {
	lua_pushnumber(L, (float)CompactAIDrivers().size());
	return 1;
}

static int L_GetAIDriverText(void* L) {   // description line: the whole list
	std::wstring s = L"AI drivers: ";
	auto list = CompactAIDrivers();
	if (list.empty()) s += L"none";
	for (size_t i = 0; i < list.size(); i++) {
		if (i) s += L", ";
		s += list[i] >= 1 && list[i] <= (int)gAINames.size() ? gAINames[list[i] - 1] : L"?";
	}
	PushString(L, s);
	return 1;
}

// ------------------------------------------------------------------------------------------------------------
// Progress of a running game for the players in the lobby (chloemultiplayer_lobby.bed, UpdateGameInfo). Chloe's
// multiplayer only shares laps per player. Every racing PC sends once per second (kTypeRaceStatus, 5 x int32):
//   kind 1 = stunt: a = current attempt, b = attempts
//   kind 2 = derby: type = derby type (1 LMS, 2 wrecking, 3 frag), a = cars left (-1 unknown), b = cars,
//            c = seconds left (-1 = no time limit)
// Kept per sender; the lobby shows one of the fresh ones (ChloeSteam_GetRaceStatus, nil if all older than 3 s):
// stunt = the lowest current attempt (players are at different attempts), derby = the host's, else the lowest SteamID
// (the times differ by the countdown of each PC). So the display does not jump between players.

struct RaceStatus { int32_t kind, type, a, b, c; };
struct RaceStatusIn { RaceStatus st; DWORD tick; uint64_t videoEvent; };
static std::map<uint64_t, RaceStatusIn> gRaceStatusIn;
static uint64_t gVideoStatusEvent = 0; // game thread, written by StuntVideoTick
static bool gVideoStatusDriving = false;

static void StoreRaceStatus(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	const int normal = 5 + sizeof(RaceStatus);
	if (len != normal && len != normal + sizeof(uint64_t)) return;
	RaceStatusIn& in = gRaceStatusIn[from];
	memcpy(&in.st, data + 5, sizeof(RaceStatus));
	in.videoEvent = 0;
	if (len != normal) memcpy(&in.videoEvent, data + normal, sizeof(in.videoEvent));
	in.tick = GetTickCount();
}

static void SendRaceStatus(const RaceStatus& st) {   // game thread, at most once per second
	static DWORD last = 0;
	if (GetTickCount() - last < 1000) return;
	if (!TryEnterCriticalSection(&gLock)) return;
	last = GetTickCount();
	uint8_t payload[sizeof(st) + sizeof(uint64_t)];
	size_t length = sizeof(st);
	memcpy(payload, &st, sizeof(st));
	if (st.kind == 1 && gVideoStatusEvent) {
		const uint64_t event = gVideoStatusDriving ? gVideoStatusEvent : 0;
		memcpy(payload + length, &event, sizeof(event));
		length += sizeof(event);
	}
	for (auto peer : LobbyPeers()) SendRaw(peer, kTypeRaceStatus, 0, 0, payload, length);
	LeaveCriticalSection(&gLock);
}

static int L_GetRaceStatus(void* L) {   // kind, type, a, b, c or nil
	RaceStatus st = {};
	bool found = false;
	EnterCriticalSection(&gLock);
	DWORD now = GetTickCount();
	for (auto& it : gRaceStatusIn) {   // map order = ascending SteamID
		const RaceStatus& s = it.second.st;
		if (now - it.second.tick >= 3000 || s.kind == 0) continue;
		if (!found) { st = s; found = true; continue; }
		if (s.kind == 1 && st.kind == 1) { if (s.a < st.a) st = s; }          // stunt: lowest attempt
		else if (s.kind == 2 && it.first == gJoinHost) st = s;              // derby: the host's if it is racing
	}
	LeaveCriticalSection(&gLock);
	if (!found) { lua_pushnil_cc(L); return 1; }
	lua_pushnumber(L, (float)st.kind);
	lua_pushnumber(L, (float)st.type);
	lua_pushnumber(L, (float)st.a);
	lua_pushnumber(L, (float)st.b);
	lua_pushnumber(L, (float)st.c);
	return 5;
}

// ------------------------------------------------------------------------------------------------------------
// Result of a multiplayer stunt for the tournament. Every player does the attempts at its own pace, so the results
// of a PC are complete only if the records of all other players had arrived when its game built the final ranking
// (ScoreManagerStunt [9], see StuntResultsHook). After the race every PC hands its ranking to the host
// (ChloeSteam_SendStuntResult: "tag|missing records|names", repeated for 60 s); the host takes the most complete one.

static int gLastStuntMissing = -1;              // records missing when the ranking was built, -1 = no MP stunt
static std::map<uint64_t, std::string> gStuntResults;   // host: SteamID -> result string
static std::string gStuntResultOut;              // client: own result, resent to the host
static DWORD gStuntResultOutStart = 0, gStuntResultOutLast = 0;

static void StoreStuntResult(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	if (!gHostLobby || len > 5 + 4000) return;
	std::string s((const char*)data + 5, (size_t)(len - 5));
	if (gStuntResults[from] != s) { gStuntResults[from] = s; Log("stunt result of %llu received: %s", from, s.c_str()); }
}

static void ResendStuntResult() {   // caller holds gLock (network thread)
	if (gStuntResultOut.empty() || !gJoinHost) return;
	if (GetTickCount() - gStuntResultOutStart > 60000) { gStuntResultOut.clear(); return; }
	if (GetTickCount() - gStuntResultOutLast < 1000) return;
	gStuntResultOutLast = GetTickCount();
	SendRaw(gJoinHost, kTypeStuntResult, 0, 0, (const uint8_t*)gStuntResultOut.data(), gStuntResultOut.size());
}

static int L_GetLastStuntMissing(void* L) {   // missing records of the last MP stunt, nil if none (reset at stunt start)
	int v = gLastStuntMissing;
	if (v < 0) lua_pushnil_cc(L);
	else lua_pushnumber(L, (float)v);
	return 1;
}

static int L_SendStuntResult(void* L) {   // host: own result, client: sent to the host
	size_t len = 0;
	auto s = (const char*)lua_tolstring(L, 1, &len);
	if (!s || len == 0 || len > 4000) return 0;
	EnterCriticalSection(&gLock);
	if (gHostLobby) gStuntResults[gSelf] = std::string(s, len);
	else { gStuntResultOut.assign(s, len); gStuntResultOutStart = GetTickCount(); gStuntResultOutLast = 0; }
	Log("own stunt result: %s", std::string(s, len).c_str());
	LeaveCriticalSection(&gLock);
	return 0;
}

static int L_GetStuntResults(void* L) {   // host: all results, one per line
	std::string all;
	EnterCriticalSection(&gLock);
	for (auto& r : gStuntResults) { all += r.second; all += "\n"; }
	LeaveCriticalSection(&gLock);
	lua_pushlstring(L, (const wchar_t*)all.c_str(), all.length());
	return 1;
}

static int L_ClearStuntResults(void*) {
	EnterCriticalSection(&gLock);
	gStuntResults.clear();
	LeaveCriticalSection(&gLock);
	return 0;
}

static void RegisterLua(void* L) {
	TryPatchChloeMP();
	struct { const char* name; void* fn; } funcs[] = {
		{ "ChloeSteam_IsAvailable", (void*)L_IsAvailable },
		{ "ChloeSteam_RefreshLobbies", (void*)L_RefreshLobbies },
		{ "ChloeSteam_IsRefreshing", (void*)L_IsRefreshing },
		{ "ChloeSteam_GetNumLobbies", (void*)L_GetNumLobbies },
		{ "ChloeSteam_GetLobbyLabel", (void*)L_GetLobbyLabel },
		{ "ChloeSteam_JoinLobby", (void*)L_JoinLobby },
		{ "ChloeSteam_GetJoinState", (void*)L_GetJoinState },
		{ "ChloeSteam_GetJoinAddress", (void*)L_GetJoinAddress },
		{ "ChloeSteam_ResetJoin", (void*)L_ResetJoin },
		{ "ChloeSteam_LeaveLobby", (void*)L_LeaveLobby },
		{ "ChloeSteam_SetAIDriver", (void*)L_SetAIDriver },
		{ "ChloeSteam_GetAIDriver", (void*)L_GetAIDriver },
		{ "ChloeSteam_GetAIDriverCount", (void*)L_GetAIDriverCount },
		{ "ChloeSteam_GetAIDriverText", (void*)L_GetAIDriverText },
		{ "ChloeSteam_SetLobbyValue", (void*)L_SetLobbyValue },
		{ "ChloeSteam_PrepareStuntVideo", (void*)L_PrepareStuntVideo },
		{ "ChloeSteam_GetLobbyValue", (void*)L_GetLobbyValue },
		{ "ChloeSteam_Log", (void*)L_Log },
		{ "ChloeSteam_StoreSet", (void*)L_StoreSet },
		{ "ChloeSteam_StoreGet", (void*)L_StoreGet },
		{ "ChloeSteam_GetRaceStatus", (void*)L_GetRaceStatus },
		{ "ChloeSteam_GetLastStuntMissing", (void*)L_GetLastStuntMissing },
		{ "ChloeSteam_SendStuntResult", (void*)L_SendStuntResult },
		{ "ChloeSteam_GetStuntResults", (void*)L_GetStuntResults },
		{ "ChloeSteam_ClearStuntResults", (void*)L_ClearStuntResults },
		{ "ChloeSteam_PrepareInstantAction", (void*)L_PrepareInstantAction },
	};
	for (auto& f : funcs) {
		lua_pushcfunction(L, f.fn, 0);
		lua_setfield(L, -10002 /* LUA_GLOBALSINDEX */, f.name);
	}
}

// Fouc.exe 0x46256C: CALL lua_pushcfunction while the script host registers its functions (same hook point as
// NyaFO2Hooks::PlaceScriptHook in nya-common-fouc/fo2hooks.h). Other plugins chain the same call, so the previous
// target is called afterwards.
static const uintptr_t kScriptHookAddr = 0x46256C;
static void(*gScriptHookPrev)(void*, void*, int) = nullptr;
static void ScriptHook(void* L, void* fn, int n) {
	RegisterLua(L);
	gScriptHookPrev(L, fn, n);
}

// ------------------------------------------------------------------------------------------------------------
// Stunts in multiplayer: several stunts share one map (Stunt1 / Stunt2) and differ only in the start point
// (Levels[].StartPosition). In multiplayer every player got the grid slot of its player index, so e.g. the host
// started at the curling start while High Jump was selected. During a stunt in Chloe's multiplayer every player's
// start position is set to the stunt's start point each frame (like Chloe's time trial does for player 0).
// Use GHOST MODE so that the players do not block each other.

static const uintptr_t kEndSceneSlot = 0x677448;   // same hook point as NyaFO2Hooks::PlaceD3DHooks
static HRESULT(__thiscall* gEndSceneOrig)(void*) = nullptr;
static bool gStuntLogged = false;

static int StuntStartForLevel(uint32_t levelId) {   // Levels[47..58].StartPosition (levels.bed)
	static const int table[] = { 2, 3, 1, 1, 3, 4, 2, 6, 4, 5, 5, 6 };
	return (levelId >= 47 && levelId <= 58) ? table[levelId - 47] : 0;
}

static void FixStuntStartPositions() {
	static bool hookLogged = false;
	if (!hookLogged) { hookLogged = true; Log("EndScene hook active"); }
	auto gf = *(uint8_t**)0x9298FAC;               // GameFlow
	if (!gf) return;
	int gameState = *(int*)(gf + 0x4A4), rules = *(int*)(gf + 0x514), level = *(int*)(gf + 0x4CC);   // PreRace.nLevel
	auto host = *(uint8_t**)(gf + 0x2820);
	static int lastState = -1, lastLevel = -1;
	if (gameState != lastState || level != lastLevel) {   // diagnostics, once per change
		lastState = gameState; lastLevel = level;
		Log("game state %d, rules %d, level %d, stunt start (GameFlow) %d, player host %s", gameState, rules, level,
			*(int*)(gf + 0x520), host ? "yes" : "no");
		gStuntLogged = false;
		if (gameState == 2 && StuntStartForLevel((uint32_t)level)) {   // stunt diagnostics: PlayerInfo + score entries
			for (int i = 0; i < 8; i++) {
				uint8_t* pi = gf + 0xAE8 + i * 0x50;               // GameFlow.aPlayerInfos[i]
				int type = *(int*)(pi + 0x2C);
				if (!type) continue;
				char name[17] = {};
				for (int c = 0; c < 16; c++) name[c] = (char)((wchar_t*)pi)[c];
				Log("  PlayerInfo[%d] type %d name '%s'", i, type, name);
			}
			auto sm = *(uint8_t**)0x846514;                         // ScoreManager
			if (sm) Log("  ScoreManager entries %d, current round %d", (int)((*(uint8_t**)(sm + 0xC) - *(uint8_t**)(sm + 0x8)) / 4), *(int*)(sm + 0x34));
		}
	}
	int pos = StuntStartForLevel((uint32_t)level);   // only the stunt levels 47..58
	if (!pos || !host) return;
	if (!TryEnterCriticalSection(&gLock)) return;    // network thread busy, next frame
	bool inMultiplayer = !gSocketPort.empty();       // Chloe's multiplayer sockets open
	LeaveCriticalSection(&gLock);
	if (!inMultiplayer) return;
	auto begin = *(uint8_t***)(host + 0x14), end = *(uint8_t***)(host + 0x18);
	if (!begin || end < begin || end - begin > 32) return;
	bool logged = gStuntLogged;
	for (auto it = begin; it != end; ++it) {
		uint8_t* ply = *it;
		if (!ply) continue;
		if (!logged) { logged = true; }
		uint32_t& start = *(uint32_t*)(ply + 0x2CC);   // Player.nStartPosition
		if (start != (uint32_t)pos) {
			if (!gStuntLogged) Log("stunt level %d: player %d start point %u -> %d (game state %d, %d players)", level, (int)(it - begin), start, pos, gameState, (int)(end - begin));
			start = (uint32_t)pos;
		}
	}
	gStuntLogged = true;
}

// ------------------------------------------------------------------------------------------------------------
// Races in Chloe's multiplayer (EndScene, after the original):
// - Diagnostics: lap setting, laps and finish flag of every score entry (ScoreManager 0x846514, entries at +8/+C:
//   +4 player index, +0x38 bHasFinished, +0x39 second finish flag, +0x44 completed laps = value that Chloe compares
//   with her lap setting, FUN_628e52e0) and the car damage (Car +0x7AB8) of every player.
// - The game's AI keeps driving after the finish. Stopping it (braking in this tick) was tried on 19.11.2026: the AI
//   then resets itself to the track again and again, so it is left as it is.
// - Host: adds the damage that clients dealt to AI cars (see OnCarDamage); client: sends it.

static void RaceTick() {
	auto gf = GameFlowPtr();
	if (!gf) return;
	static bool active = false;
	struct State { int lap = -99, fin = -1, plap = -99; float dmg = 0.f; bool flagLogged = false; };
	static State st[32];
	int gameState = *(int*)(gf + 0x4A4), level = *(int*)(gf + 0x4CC);
	if (gameState != 2 || StuntStartForLevel((uint32_t)level)) { active = false; gRealAIFirst = -1; return; }
	if (!TryEnterCriticalSection(&gLock)) return;
	bool mp = !gSocketPort.empty(), isHost = gJoinedLobby == 0;
	int others = mp ? (int)LobbyPeers().size() : 0;
	if (mp && !isHost) {   // client: forward damage dealt to AI cars
		for (int i = 0; i < 32; i++) {
			if (gAIDamageOut[i] <= 0.f) continue;
			float d = gAIDamageOut[i];
			gAIDamageOut[i] = 0.f;
			if (gJoinHost) SendRaw(gJoinHost, kTypeAIDamage, (uint16_t)i, 0, (const uint8_t*)&d, sizeof(d));
			Log("race: damage to AI player %d +%.3f on this client, sent to the host", i, d);
		}
	}
	float incoming[32];
	memcpy(incoming, gAIDamageIn, sizeof(incoming));
	memset(gAIDamageIn, 0, sizeof(gAIDamageIn));
	LeaveCriticalSection(&gLock);
	if (!mp) { active = false; gRealAIFirst = -1; return; }
	auto sm = *(uint8_t**)0x846514, host = *(uint8_t**)(gf + 0x2820);
	if (!sm || !host) return;
	int derbyType = *(int*)(gf + 0x51C);   // GameFlow.nDerbyType: 0 none, 1 LMS, 2 wrecking, 3 frag
	if (derbyType >= 1 && derbyType <= 3 && *(uintptr_t*)sm == 0x6E2BEC) {   // ScoreManagerDerby (vtable)
		// Derby time limit: the lobby's minutes are in PreRace.Laps (+0x4D4). ScoreManagerDerby only turns them into
		// its time limit (+0x38, ms, -1 = none) in PreRace.Mode 5 (vanilla online, FUN_00490b60 / FUN_00490c10);
		// Chloe's multiplayer races run as Mode 2, so there was no limit (frag: fixed 5 minutes).
		int minutes = *(int*)(gf + 0x4D4);
		if (minutes >= 1 && minutes <= 60 && *(int*)(sm + 0x38) != minutes * 60000) {
			Log("derby: time limit %d -> %d ms (%d minutes, derby type %d)", *(int*)(sm + 0x38), minutes * 60000, minutes, derbyType);
			*(int*)(sm + 0x38) = minutes * 60000;
		}
		RaceStatus rs = { 2, derbyType, -1, 0, -1 };
		int limit = *(int*)(sm + 0x38), raceTime = *(int*)(host + 0x2099C);   // PlayerHost.nRaceTime (ms)
		if (raceTime < 0) raceTime = 0;   // countdown: the race time starts at -3000
		if (limit > 0) rs.c = (std::max)(0, (limit - raceTime + 999) / 1000);
		auto b = *(uint8_t***)(sm + 8), e = *(uint8_t***)(sm + 0xC);
		if (b && e >= b && e - b <= 32) {
			rs.b = (int)(e - b);
			if (derbyType == 2) {                             // cars left, kept by the game for wrecking derbies (from the start)
				int left = *(int*)(sm + 0x3C);
				if (left >= 1 && left <= rs.b) rs.a = left;
			}
			else if (derbyType == 1) {                        // survivor: entries not out (+0x39, events 0x1790/0x1792);
				rs.a = 0;                                     // +0x38 is also set for the winner / at the time limit
				for (auto p = b; p != e; ++p) if (*p && !(*p)[0x39]) rs.a++;
			}
		}
		SendRaceStatus(rs);
	}
	if (!active) {
		active = true;
		int locals = 0;
		for (int i = 0; i < 32; i++) if (*(int*)(gf + 0xAE8 + i * 0x50 + 0x2C) == 1) locals++;
		gRealAIFirst = locals + others;   // PlayerInfo index of the first real AI (same on all PCs)
		for (auto& s : st) s = State();
		int chloeLaps = -1;
		if (auto mpMod = (uint8_t*)GetModuleHandleA("FlatOutUCCustomMP_gcp.asi")) chloeLaps = mpMod[0x62D5D0DC - 0x62840000];
		Log("race: level %d, rules %d, laps PreRace %d, ScoreManager %d, Chloe setting %d, %s", level, *(int*)(gf + 0x514),
			*(int*)(gf + 0x4D4), *(int*)(sm + 4), chloeLaps, isHost ? "host" : "client");
	}
	auto eb = *(uint8_t***)(sm + 8), ee = *(uint8_t***)(sm + 0xC);
	auto pb = *(uint8_t***)(host + 0x14), pe = *(uint8_t***)(host + 0x18);
	if (!eb || ee < eb || ee - eb > 32 || !pb || pe < pb || pe - pb > 32) return;
	for (auto e = eb; e != ee; ++e) {
		uint8_t* entry = *e;
		if (!entry) continue;
		int idx = *(int*)(entry + 4);
		if (idx < 0 || idx >= 32) continue;
		uint8_t* ply = nullptr;
		for (auto p = pb; p != pe; ++p) if (*p && *(int*)(*p + 0x2C4) == idx + 1) { ply = *p; break; }
		if (!ply) continue;
		int type = *(int*)(ply + 0x2C8);
		if (type == 5) continue;
		auto car = *(uint8_t**)(ply + 0x294);
		State& s = st[idx];
		int lap = *(int*)(entry + 0x44), fin = entry[0x38] | (entry[0x39] << 1);
		float dmg = car ? *(float*)(car + 0x7AB8) : 0.f;
		int plap = *(int*)(ply + 0x3E8), split = *(int*)(ply + 0x3E4);   // Player.nCurrentLap / nCurrentSplit
		if (plap != s.plap) { Log("race: player %d (type %d) Player.nCurrentLap %d, split %d, score laps %d", idx, type, plap, split, *(int*)(entry + 0x44)); s.plap = plap; }
		if (!s.flagLogged && car) { s.flagLogged = true; Log("race: player %d (type %d) car no-damage flag %d", idx, type, car[0x7AE8]); }
		if (isHost && type == 4 && car && incoming[idx] > 0.f) {   // damage a client dealt to this AI car
			float maxDmg = *(float*)0x6DA518;
			float nd = *(float*)(car + 0x7AB8) + incoming[idx];
			*(float*)(car + 0x7AB8) = nd > maxDmg ? maxDmg : nd;
			Log("race: AI player %d damage +%.3f from a client", idx, incoming[idx]);
		}
		if (lap != s.lap || fin != s.fin) {
			Log("race: player %d (type %d) laps %d, finished %d, damage %.2f", idx, type, lap, fin, dmg);
			s.lap = lap; s.fin = fin;
		}
		if (car && (dmg > s.dmg + 0.05f || dmg < s.dmg - 0.05f)) {
			Log("race: player %d (type %d) damage %.2f -> %.2f%s", idx, type, s.dmg, dmg, fin ? " (finished)" : "");
			s.dmg = dmg;
		}
	}
}

// The original runs first: Chloe's time trial code (careertimetrial.h) writes nStartPosition = 1 every frame while
// her time trial flag is set (it is reset before other races, ChloeCollection_ReinitHooks). Our value is written
// last to be safe; the actual cause of wrong stunt start points was Chloe's MP plugin (see PatchChloeStartPosition).
static void StuntTick();
static void StuntVideoTick(void*);
static HRESULT __fastcall EndSceneHook(void* self, void*) {
	HRESULT r = gEndSceneOrig(self);
	FixStuntStartPositions();
	StuntTick();
	RaceTick();
	if (SUCCEEDED(r)) StuntVideoTick(self);
	return r;
}

// ------------------------------------------------------------------------------------------------------------
// Stunts in Chloe's multiplayer: everybody does the attempt at the same time, the points of all players are shown.
// Built on the single player stunt of the game (Documentation/Reverse-Engineering.md):
//   * ScoreManagerStunt::Init (vtable 0x6E3E4C [5], slot 0x6E3E60) creates one score entry per player object plus one
//     per PlayerInfo with type 5 ("simulated", name only, no car). Before it runs, one type 5 entry is written per
//     other player of the Steam lobby (sorted by SteamID, the same order on every PC) with the Steam name.
//   * ScoreManagerStunt [22] (slot 0x6E3EA4) runs when the local attempt has ended and lets every simulated player
//     "roll" its points (FUN_00497E80). Replaced: the local round record (0x114 bytes at entry+0x38+(round-1)*0x114)
//     is sent to all players, the simulated entries of the other players get their real records instead of rolled
//     ones. Records that arrive later are filled in by the EndScene tick.
// Waiting for all players before the next attempt is not done yet (needs the start of the next attempt).

static const uintptr_t kStuntInitSlot = 0x6E3E60, kStuntSimSlot = 0x6E3EA4;
static const size_t kStuntRecordSize = 0x114;
static void(__thiscall* gStuntInitOrig)(void*, int, int) = nullptr;
static void(__thiscall* gStuntSimOrig)(void*) = nullptr;
static auto StuntRollSimulated = (void(__stdcall*)(void*, int))0x497E80;

static std::vector<uint64_t> gStuntPeers;                           // simulated entry id k (1..n) = gStuntPeers[k-1]
static std::map<uint64_t, std::map<int, std::vector<uint8_t>>> gStuntRecords;   // SteamID -> level*100+round -> record
struct StuntPending { uint8_t* entry; uint64_t peer; int round; };
static std::vector<StuntPending> gStuntPending;                    // game thread only
static bool gStuntActive = false;
static int gStuntOwnRounds = 0;                                    // own attempts done (records sent)
static std::map<uint64_t, std::wstring> gStuntNames;              // SteamID -> in-game profile name (kept)

static uint8_t* GameFlowPtr() { return *(uint8_t**)0x9298FAC; }
static int CurrentLevel() { auto gf = GameFlowPtr(); return gf ? *(int*)(gf + 0x4CC) : 0; }

static bool IsMultiplayerStunt() {
	if (!StuntStartForLevel((uint32_t)CurrentLevel())) return false;
	EnterCriticalSection(&gLock);
	bool mp = !gSocketPort.empty() && (gHostLobby || gJoinedLobby);
	LeaveCriticalSection(&gLock);
	return mp;
}

static void StoreStuntRecord(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	if (len != (int)(5 + kStuntRecordSize)) return;
	uint16_t level, round;
	memcpy(&level, data + 1, 2); memcpy(&round, data + 3, 2);
	auto& rec = gStuntRecords[from][level * 100 + round];
	bool changed = rec.size() != kStuntRecordSize || memcmp(rec.data(), data + 5, kStuntRecordSize) != 0;
	if (!changed) return;   // records are sent again every 2 s
	rec.assign(data + 5, data + len);
	Log("stunt: record of %llu for level %u round %u received", from, level, round);
}

static void StoreStuntName(uint64_t from, const uint8_t* data, int len) {   // caller holds gLock
	if (len != 5 + 32) return;
	wchar_t name[17] = {};
	memcpy(name, data + 5, 32);
	if (gStuntNames[from] != name) { gStuntNames[from] = name; Log("stunt: profile name of %llu received", from); }
}

static void SendOwnStuntName() {   // caller holds gLock; own profile name = PlayerInfo[0]
	auto gf = GameFlowPtr();
	if (!gf) return;
	wchar_t name[16] = {};
	memcpy(name, gf + 0xAE8, 30);
	for (auto peer : gStuntPeers) SendRaw(peer, kTypeName, 0, 0, (const uint8_t*)name, 32);
}

static std::vector<uint64_t> LobbyPeers() {   // caller holds gLock
	std::vector<uint64_t> peers;
	uint64_t lobby = gHostLobby ? gHostLobby : gJoinedLobby;
	if (!lobby || !SteamReady()) return peers;
	int n = S.GetNumLobbyMembers(gMM, lobby);
	for (int i = 0; i < n; i++) {
		uint64_t m = S.GetLobbyMemberByIndex(gMM, lobby, i);
		if (m && m != gSelf) peers.push_back(m);
	}
	std::sort(peers.begin(), peers.end());
	return peers;
}

static void PrepareStuntPlayers() {
	auto gf = GameFlowPtr();
	EnterCriticalSection(&gLock);
	// records of an earlier stunt (e.g. the same stunt again in a tournament) must not be taken over; records of this
	// stunt that arrived before (other players faster) are sent again by their PCs every 2 s
	gStuntRecords.clear();
	gStuntPeers = LobbyPeers();
	std::vector<std::wstring> names;
	void* friends = S.Friends ? S.Friends() : nullptr;
	for (auto id : gStuntPeers) {
		auto known = gStuntNames.find(id);   // profile name if already received, else the Steam name for now
		names.push_back(known != gStuntNames.end() && !known->second.empty() ? known->second : Widen(friends ? S.GetFriendPersonaName(friends, id) : ""));
	}
	SendOwnStuntName();
	LeaveCriticalSection(&gLock);
	if (gStuntPeers.size() > 7) gStuntPeers.resize(7);
	gStuntPending.clear();
	for (size_t k = 0; k < 32; k++) {
		uint8_t* pi = gf + 0xAE8 + k * 0x50;                  // PlayerInfo[k]: name wchar[16] +0, type +0x2C
		int& type = *(int*)(pi + 0x2C);
		if (k >= 1 && k <= gStuntPeers.size()) {
			if (type == 1) { Log("stunt: PlayerInfo[%u] is a local player, other players not added", (unsigned)k); break; }
			std::wstring name = names[k - 1].empty() ? L"PLAYER" : names[k - 1];
			memset(pi, 0, 32);
			wcsncpy_s((wchar_t*)pi, 16, name.c_str(), _TRUNCATE);
			type = 5;
			*(int*)(pi + 0x20) = (int)((k - 1) % 5) + 0x22;    // like InitializeAI does for simulated stunt players
		} else if (k > gStuntPeers.size() && (type == 5 || type == 4 || type == 2)) {
			type = 0;                                          // no AI / leftovers in multiplayer stunts
		}
	}
	gStuntActive = true;
	gStuntOwnRounds = 0;
	gLastStuntMissing = -1;
	Log("stunt: %u other players added as simulated entries", (unsigned)gStuntPeers.size());
}

static void __fastcall StuntInitHook(void* self, void*, int a, int b) {
	if (IsMultiplayerStunt()) PrepareStuntPlayers();
	else gStuntActive = false;
	gStuntInitOrig(self, a, b);
	if (gStuntActive) {
		auto sm = (uint8_t*)self;
		Log("stunt: score entries %d, simulated %d, attempts %d", (int)((*(uint8_t**)(sm + 0xC) - *(uint8_t**)(sm + 0x8)) / 4),
			*(int*)(sm + 0x44), *(int*)(sm + 0x3C));
	}
}

static uint8_t* StuntEntry(uint8_t* sm, int playerId) {
	auto begin = *(uint8_t***)(sm + 0x8), end = *(uint8_t***)(sm + 0xC);
	for (auto it = begin; it != end; ++it) if (*it && *(int*)(*it + 4) == playerId) return *it;
	return nullptr;
}

static bool FillStuntRecord(uint8_t* entry, uint64_t peer, int round) {   // caller holds gLock
	auto p = gStuntRecords.find(peer);
	if (p == gStuntRecords.end()) return false;
	auto r = p->second.find(CurrentLevel() * 100 + round);
	if (r == p->second.end()) return false;
	memcpy(entry + 0x38 + (round - 1) * kStuntRecordSize, r->second.data(), kStuntRecordSize);
	return true;
}

static void __fastcall StuntSimHook(void* self, void*) {
	if (!gStuntActive) { gStuntSimOrig(self); return; }
	auto sm = (uint8_t*)self;
	auto vt = *(void***)self;
	int notLastAttempt = ((int(__thiscall*)(void*))vt[0x30 / 4])(self);
	int round = *(int*)(sm + 0x34);
	int total = (int)((*(uint8_t**)(sm + 0xC) - *(uint8_t**)(sm + 0x8)) / 4), sims = *(int*)(sm + 0x44);
	if (round < 1 || round > 10) { Log("stunt: unexpected round %d, rolling as usual", round); gStuntSimOrig(self); return; }

	EnterCriticalSection(&gLock);
	if (uint8_t* local = StuntEntry(sm, 0)) {               // own record of this attempt to everybody
		uint8_t* rec = local + 0x38 + (round - 1) * kStuntRecordSize;
		for (auto peer : gStuntPeers) SendRaw(peer, kTypeStunt, (uint16_t)CurrentLevel(), (uint16_t)round, rec, kStuntRecordSize);
		Log("stunt: own record of round %d sent to %u players", round, (unsigned)gStuntPeers.size());
		SendOwnStuntName();
		if (round > gStuntOwnRounds) gStuntOwnRounds = round;
	}
	for (int id = total - sims; id < total; id++) {
		uint8_t* entry = StuntEntry(sm, id);
		if (!entry) continue;
		if (id >= 1 && id <= (int)gStuntPeers.size()) {
			uint64_t peer = gStuntPeers[id - 1];
			if (!FillStuntRecord(entry, peer, round)) gStuntPending.push_back({ entry, peer, round });
		} else {
			LeaveCriticalSection(&gLock);
			StuntRollSimulated(entry, id + 1);                  // AI entries keep the rolled points
			EnterCriticalSection(&gLock);
		}
		if (!notLastAttempt) *(int*)(entry + 0xCA8) = 1;       // finished, like the original
	}
	LeaveCriticalSection(&gLock);
}

static void StuntTick() {   // EndScene: fill in records that arrived after the local attempt
	auto gf = GameFlowPtr();
	if (!gf || *(int*)(gf + 0x4A4) != 2) {                     // left the race: forget everything
		if (gStuntActive || !gStuntPending.empty()) {
			gStuntActive = false; gStuntPending.clear();
			EnterCriticalSection(&gLock); gStuntRecords.clear(); LeaveCriticalSection(&gLock);
		}
		return;
	}
	if (!gStuntActive) return;
	auto sm = *(uint8_t**)0x846514;
	if (sm && *(uintptr_t*)sm != 0x6E3E4C) sm = nullptr;   // only ScoreManagerStunt (vtable)
	if (sm) {
		RaceStatus rs = { 1, 0, *(int*)(sm + 0x34), *(int*)(sm + 0x3C), -1 };   // current attempt / attempts
		if (rs.a >= 1 && rs.a <= 10 && rs.b >= 1 && rs.b <= 10) SendRaceStatus(rs);
	}
	if (!TryEnterCriticalSection(&gLock)) return;
	// own records again every 2 s while in the race (messages are unreliable; duplicates just overwrite)
	static DWORD lastResend = 0;
	if (sm && gStuntOwnRounds > 0 && GetTickCount() - lastResend > 2000) {
		lastResend = GetTickCount();
		if (uint8_t* local = StuntEntry(sm, 0)) {
			for (int r = 1; r <= gStuntOwnRounds && r <= 10; r++)
				for (auto peer : gStuntPeers)
					SendRaw(peer, kTypeStunt, (uint16_t)CurrentLevel(), (uint16_t)r, local + 0x38 + (r - 1) * kStuntRecordSize, kStuntRecordSize);
			SendOwnStuntName();
		}
	}
	for (size_t k = 0; k < gStuntPeers.size() && k < 7; k++) {   // profile names of the other players
		auto n = gStuntNames.find(gStuntPeers[k]);
		if (n == gStuntNames.end() || n->second.empty()) continue;
		auto pi = (wchar_t*)(gf + 0xAE8 + (k + 1) * 0x50);
		if (*(int*)((uint8_t*)pi + 0x2C) == 5 && wcsncmp(pi, n->second.c_str(), 15) != 0) wcsncpy_s(pi, 16, n->second.c_str(), _TRUNCATE);
	}
	for (size_t i = 0; i < gStuntPending.size();) {
		auto& p = gStuntPending[i];
		if (FillStuntRecord(p.entry, p.peer, p.round)) {
			Log("stunt: record of %llu for round %d filled in late", p.peer, p.round);
			gStuntPending.erase(gStuntPending.begin() + i);
		} else i++;
	}
	LeaveCriticalSection(&gLock);
}

// ScoreManagerStunt [9] (FUN_0049aee0, slot 0x6E3E70) builds the final ranking of the stunt (GameFlow results that the
// lobby reads). Records that arrived late are filled in first; the number still missing is kept for the tournament.
static const uintptr_t kStuntResultsSlot = 0x6E3E70;
static void(__thiscall* gStuntResultsOrig)(void*, void*) = nullptr;
static void __fastcall StuntResultsHook(void* self, void*, void* a) {
	if (gStuntActive) {
		EnterCriticalSection(&gLock);
		for (size_t i = 0; i < gStuntPending.size();) {
			auto& p = gStuntPending[i];
			if (FillStuntRecord(p.entry, p.peer, p.round)) gStuntPending.erase(gStuntPending.begin() + i);
			else i++;
		}
		auto sm = (uint8_t*)self;
		int attempts = *(int*)(sm + 0x3C);
		int missing = (int)gStuntPending.size() + (gStuntOwnRounds < attempts ? attempts - gStuntOwnRounds : 0);
		LeaveCriticalSection(&gLock);
		if (missing != gLastStuntMissing) Log("stunt: final ranking built, %d records missing", missing);
		gLastStuntMissing = missing;
	}
	gStuntResultsOrig(self, a);
}

// Results between the attempts: the single player tab (IngameMenuTabSingleStuntResults, vtable 0x6E786C) draws only
// the attempts of the local player. In a multiplayer stunt its draw method [3] (slot 0x6E7878) runs the one of
// IngameMenuTabMultiplayerStuntResults (0x4B7CC0), which lists every score entry with the PlayerInfo names.
static const uintptr_t kSingleStuntTabDrawSlot = 0x6E7878;
static void(__thiscall* gSingleStuntTabDrawOrig)(void*, int) = nullptr;
static auto MultiplayerStuntTabDraw = (void(__thiscall*)(void*, int))0x4B7CC0;
static bool gStuntResultsDrawn = false;
static void __fastcall SingleStuntTabDrawHook(void* self, void*, int time) {
	if (gStuntActive) {
		gStuntResultsDrawn = true;
		MultiplayerStuntTabDraw(self, time);
	}
	else gSingleStuntTabDrawOrig(self, time);
}

// The feature remains a local opt-in for the peer test until the two-PC acceptance run.
static bool VideoPreviewEnabled() {
	static const bool enabled = [] {
		const auto attr = GetFileAttributesW(L"Config\\StuntVideoPreview.enabled");
		return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
	}();
	return enabled;
}

static constexpr const char* kVideoEventKey = "cc_stunt_video_event";
static uint64_t gPreparedVideoEvent = 0;
static int L_PrepareStuntVideo(void* L) {
	if (!VideoPreviewEnabled()) return 0;
	const auto level = static_cast<unsigned>(luaL_checknumber(L, 1));
	EnterCriticalSection(&gLock);
	if (gHostLobby && SteamReady()) {
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		gPreparedVideoEvent = (std::max)(gPreparedVideoEvent + 1, static_cast<uint64_t>(counter.QuadPart));
		char value[48];
		sprintf_s(value, "%llu:%u", gPreparedVideoEvent, level);
		if (S.SetLobbyData(gMM, gHostLobby, kVideoEventKey, value))
			Log("stunt video: host published event %llu level %u", gPreparedVideoEvent, level);
	}
	LeaveCriticalSection(&gLock);
	return 0;
}

namespace {
using namespace stunt_video;
using VideoPacket = stunt_stream::Packet;
#ifdef STUNT_STREAM_OPUS
struct StuntSound {
	stunt_audio::Capture capture;
	stunt_audio::Encoder encoder;
	stunt_audio::Output output;
	struct Block { uint32_t stamp; unsigned channels; std::vector<uint8_t> bytes; };
	std::deque<Block> incoming;
	bool capturing = false;
	uint32_t sent = 0, lastIncoming = 0;
	uint64_t attachTried = 0;
};
// The ASI stays loaded until process termination. Avoid WinMM teardown under the loader lock.
StuntSound& gSound = *new StuntSound;

void StopStuntAudioCapture() {
	if (!gSound.capturing) return;
	gSound.capture.Stop();
	gSound.encoder.Stop();
	gSound.capturing = false;
	gSound.attachTried = 0;
}
#endif
struct VideoLease {
	uint64_t subscription = 0, last = 0;
};
struct VideoRoute {
	stunt_stream::Chain chain;
	std::map<uint64_t, VideoLease> leases;
	std::map<uint64_t, uint64_t> congestion;
	uint64_t lastRoute = 0;
};

struct VideoSession {
	uint64_t lobby = 0, consumedEvent = 0, event = 0, target = 0, subscription = 0, nextSubscription = 0;
	uint64_t lastSubscribe = 0, lastPicture = 0, lastCapture = 0, budgetSince = 0, nextRoute = 0;
	uint64_t displayedSubscription = 0, lastFeedback = 0, lastKeyRequest = 0, sourceEpoch = 0, sourceRoute = 0, decodeEpoch = 0;
	uint32_t sent = 0, received = 0, lastDisplayedSequence = 0, lost = 0;
	uint32_t baseTransit = 0;
	unsigned lastQuality = 2;
	size_t sentBytes = 0;
	bool needKey = true, forceKey = true, sourceActive = false;
	bool resultsLogged = false;
	unsigned heldInput = 0;
	std::vector<uint64_t> members;
	std::map<uint64_t, VideoRoute> routes;
	stunt_stream::Adaptation adaptation;
	stunt_stream::Assembly picture;
	std::optional<Frame> display;
	std::wstring captionText;
	Frame caption;
	// Created lazily outside the loader lock. The DLL is loaded for the lifetime of Fouc.exe.
	Codec* codec = nullptr; // Loader detach must never join this worker.

	Codec& Worker() {
		if (!codec) codec = new Codec();
		return *codec;
	}
};
VideoSession gVideo;

uint64_t VideoHost() { return gHostLobby ? gSelf : gJoinHost; }

void HandleVideo(uint64_t sender, const VideoPacket& msg, const uint8_t* data, size_t bytes, uint64_t now);

bool ReserveStuntBandwidth(size_t bytes, uint64_t now) {
	if (now - gVideo.budgetSince >= 1000) {
		gVideo.budgetSince = now;
		gVideo.sentBytes = 0;
	}
	if (gVideo.sentBytes + bytes > 384 * 1024) return false;
	gVideo.sentBytes += bytes;
	return true;
}

bool SendVideo(uint64_t peer, const VideoPacket& msg, const void* data = nullptr, size_t bytes = 0) {
	if (!peer) return false;
	if (peer == gSelf) {
		HandleVideo(gSelf, msg, static_cast<const uint8_t*>(data), bytes, GetTickCount64());
		return true;
	}
	std::array<uint8_t, sizeof(VideoPacket) + stunt_stream::FragmentBytes> packet;
	memcpy(packet.data(), &msg, sizeof(msg));
	if (bytes) memcpy(packet.data() + sizeof(msg), data, bytes);
	const auto identity = Identity(peer);
	const int flags = msg.type == stunt_stream::Picture || msg.type == stunt_stream::Sound ? 1 | 4 : 8 | 1;
	return S.SendMessageToUser(gMsg, &identity, packet.data(), static_cast<uint32_t>(sizeof(msg) + bytes), flags, kStuntVideoChannel) == 1;
}

#ifdef STUNT_STREAM_OPUS
void SendStuntSound(uint64_t next, uint64_t route, uint64_t now) {
	if (!gSound.capturing && now - gSound.attachTried >= 1000) {
		gSound.attachTried = now;
		gSound.capturing = gSound.capture.Start(*reinterpret_cast<void**>(0x7640FC), stunt_audio::EffectGroups());
		if (gSound.capturing) Log("stunt video: effect capture attached");
	}
	if (!gSound.capturing) return;
	std::array<float, stunt_audio::Samples * 2> pcm;
	for (unsigned i = 0; i < 3; ++i) {
		uint32_t stamp = 0;
		if (!gSound.capture.ReadBlock(pcm, stamp, now)) break;
		const unsigned channels = gVideo.lastQuality ? 2 : 1;
		const auto encoded = gSound.encoder.Encode(pcm, channels);
		if (encoded.empty() || !ReserveStuntBandwidth(sizeof(VideoPacket) + encoded.size(), now)) continue;
		VideoPacket msg{gVideo.event, gSelf, route};
		msg.type = stunt_stream::Sound;
		msg.quality = static_cast<uint8_t>(gVideo.lastQuality);
		msg.sequence = ++gSound.sent;
		msg.stamp = stamp;
		msg.size = static_cast<uint32_t>(encoded.size());
		SendVideo(next, msg, encoded.data(), encoded.size());
	}
}
#endif

bool VideoMember(uint64_t peer) {
	return peer == gSelf || std::binary_search(gVideo.members.begin(), gVideo.members.end(), peer);
}

bool DriverLive(uint64_t driver, uint64_t now) {
	if (driver == gSelf) return gVideoStatusDriving;
	const auto it = gRaceStatusIn.find(driver);
	return it != gRaceStatusIn.end() && it->second.videoEvent == gVideo.event &&
		static_cast<DWORD>(now) - it->second.tick < 2700;
}

void ClearVideoView() {
	gVideo.display.reset();
	gVideo.picture = {};
	gVideo.received = gVideo.lastDisplayedSequence = gVideo.lost = 0;
	gVideo.baseTransit = 0;
	gVideo.needKey = true;
	++gVideo.decodeEpoch;
	if (gVideo.codec) gVideo.codec->Clear();
#ifdef STUNT_STREAM_OPUS
	gSound.incoming.clear();
	gSound.lastIncoming = 0;
	gSound.output.Stop();
#endif
}

void StopWatching() {
	if (gVideo.target) {
		VideoPacket msg{gVideo.event, gVideo.target, gVideo.subscription, gSelf};
		msg.type = stunt_stream::Leave;
		SendVideo(VideoHost(), msg);
		Log("stunt video: closed target %llu", gVideo.target);
	}
	gVideo.target = 0;
	ClearVideoView();
}

void ResetVideoEvent() {
	gVideoStatusEvent = 0;
	gVideoStatusDriving = false;
	gVideo.event = 0;
	gVideo.target = 0;
	ClearVideoView();
	gVideo.members.clear();
	gVideo.routes.clear();
	gVideo.lastCapture = gVideo.budgetSince = gVideo.sourceRoute = 0;
	gVideo.sent = 0;
	gVideo.sentBytes = 0;
	gVideo.sourceActive = false;
	gVideo.resultsLogged = false;
}

void UpdateVideoRoutes(uint64_t now) {
	for (auto& entry : gVideo.routes) {
		const uint64_t driver = entry.first;
		auto& route = entry.second;
		std::vector<uint64_t> viewers;
		for (auto it = route.leases.begin(); it != route.leases.end();) {
			if (!VideoMember(it->first) || now - it->second.last >= stunt_stream::LeaseMs)
				it = route.leases.erase(it);
			else { viewers.push_back(it->first); ++it; }
		}
		if (!VideoMember(driver) || !DriverLive(driver, now)) viewers.clear();
		const bool changed = viewers != route.chain.viewers;
		if (!changed && (viewers.empty() || now - route.lastRoute < 500)) continue;
		auto recipients = route.chain.viewers;
		recipients.insert(recipients.end(), viewers.begin(), viewers.end());
		recipients.push_back(driver);
		std::sort(recipients.begin(), recipients.end());
		recipients.erase(std::unique(recipients.begin(), recipients.end()), recipients.end());
		VideoPacket msg{gVideo.event, driver, changed ? ++gVideo.nextRoute : route.chain.generation};
		msg.type = stunt_stream::Route;
		for (auto recipient : recipients) {
			if (recipient != gSelf && VideoMember(recipient))
				SendVideo(recipient, msg, viewers.data(), viewers.size() * sizeof(uint64_t));
		}
		HandleVideo(gSelf, msg, reinterpret_cast<const uint8_t*>(viewers.data()), viewers.size() * sizeof(uint64_t), now);
		if (changed) Log("stunt video: route %llu for %llu, %u viewers", msg.route, driver, static_cast<unsigned>(viewers.size()));
	}
}

void HandleVideo(uint64_t sender, const VideoPacket& msg, const uint8_t* data, size_t bytes, uint64_t now) {
	if (!VideoMember(sender) || !VideoMember(msg.driver) || msg.event != gVideo.event || !stunt_stream::Valid(msg, bytes)) return;
	if (msg.type == stunt_stream::Watch || msg.type == stunt_stream::Leave) {
		if (VideoHost() != gSelf || sender != msg.viewer || sender == msg.driver) return;
		auto& leases = gVideo.routes[msg.driver].leases;
		if (msg.type == stunt_stream::Watch) {
			for (auto& entry : gVideo.routes) if (entry.first != msg.driver) entry.second.leases.erase(sender);
			auto& lease = leases[sender];
			if (msg.route >= lease.subscription) lease = {msg.route, now};
		} else {
			auto it = leases.find(sender);
			if (it != leases.end() && it->second.subscription == msg.route) leases.erase(it);
		}
		return;
	}
	if (msg.type == stunt_stream::Route) {
		if (sender != VideoHost()) return;
		auto& route = gVideo.routes[msg.driver];
		if (msg.route < route.chain.generation) return;
		std::vector<uint64_t> viewers(bytes / sizeof(uint64_t));
		if (bytes) memcpy(viewers.data(), data, bytes);
		if (!std::is_sorted(viewers.begin(), viewers.end()) ||
			std::adjacent_find(viewers.begin(), viewers.end()) != viewers.end() ||
			std::any_of(viewers.begin(), viewers.end(), [&](uint64_t id) { return !VideoMember(id) || id == msg.driver; })) return;
		if (msg.route == route.chain.generation) {
			if (viewers == route.chain.viewers) route.lastRoute = now;
			return;
		}
		route.lastRoute = now;
		route.chain = {msg.route, std::move(viewers)};
		route.congestion.clear();
		if (gVideo.target == msg.driver) {
			ClearVideoView();
			gVideo.lastPicture = now;
		}
		return;
	}
	auto it = gVideo.routes.find(msg.driver);
	if (it == gVideo.routes.end() || it->second.chain.generation != msg.route ||
		now - it->second.lastRoute >= stunt_stream::LeaseMs) return;
	auto& route = it->second;
	if (msg.driver == gSelf && route.chain.Previous(gSelf, sender)) {
		if (msg.type == stunt_stream::Presented) Log("stunt video: peer %llu displayed picture %u", sender, msg.sequence);
		if (msg.type == stunt_stream::Feedback) {
			if (msg.sequence) route.congestion[sender] = now;
			else route.congestion.erase(sender);
		}
		if (msg.type == stunt_stream::Recover) gVideo.forceKey = true;
	}
	if (gVideo.target != msg.driver || route.chain.Previous(msg.driver, gSelf) != sender ||
		(msg.type != stunt_stream::Picture && msg.type != stunt_stream::Sound)) return;
	if (const auto next = route.chain.Next(msg.driver, gSelf)) {
		if (!ReserveStuntBandwidth(sizeof(msg) + bytes, now) || !SendVideo(next, msg, data, bytes))
			++gVideo.lost;
	}
	if (msg.type == stunt_stream::Sound) {
#ifdef STUNT_STREAM_OPUS
		if (msg.sequence > gSound.lastIncoming) {
			gSound.lastIncoming = msg.sequence;
			if (gSound.incoming.size() == 5) gSound.incoming.pop_front();
			gSound.incoming.push_back({msg.stamp, msg.quality ? 2u : 1u, {data, data + bytes}});
		}
#endif
		return;
	}
	if (!gVideo.picture.Add(msg, data, bytes, now)) return;
	if (gVideo.received && msg.sequence != gVideo.received + 1) {
		gVideo.needKey = true;
		++gVideo.lost;
	}
	gVideo.received = msg.sequence;
	if (gVideo.needKey && !msg.key) {
		if (now - gVideo.lastKeyRequest >= 500) {
			VideoPacket recover{msg.event, msg.driver, msg.route};
			recover.type = stunt_stream::Recover;
			SendVideo(msg.driver, recover);
			gVideo.lastKeyRequest = now;
		}
		return;
	}
	if (gVideo.needKey && gVideo.codec) gVideo.codec->Clear();
	gVideo.needKey = false;
	const auto transit = static_cast<uint32_t>(now) - msg.stamp;
	if (!gVideo.baseTransit || static_cast<int32_t>(transit - gVideo.baseTransit) < 0) gVideo.baseTransit = transit;
	if (transit - gVideo.baseTransit > 200) ++gVideo.lost;
	Frame frame{msg.event, gVideo.decodeEpoch, msg.sequence, msg.width, msg.height, std::move(gVideo.picture.data)};
	frame.stamp = msg.stamp;
	frame.quality = msg.quality;
	frame.key = msg.key != 0;
	gVideo.Worker().Submit(true, std::move(frame));
}

void PumpVideo(uint64_t now) {
	void* messages[128];
	const int count = S.ReceiveMessagesOnChannel(gMsg, kStuntVideoChannel, messages, _countof(messages));
	for (int i = 0; i < count; ++i) {
		const auto raw = static_cast<const uint8_t*>(messages[i]);
		const auto data = *reinterpret_cast<const uint8_t* const*>(raw);
		const int length = *reinterpret_cast<const int*>(raw + 4);
		if (gIdentityOffset < 0) gIdentityOffset = (*reinterpret_cast<const int*>(raw + 16) == kIdentitySteamID &&
			*reinterpret_cast<const int*>(raw + 20) == 8) ? 16 : 12;
		const auto* identity = reinterpret_cast<const SteamNetworkingIdentity*>(raw + gIdentityOffset);
		VideoPacket msg{};
		if (identity->eType == kIdentitySteamID && length >= static_cast<int>(sizeof(msg))) {
			memcpy(&msg, data, sizeof(msg));
			HandleVideo(identity->steamID64, msg, data + sizeof(msg), length - sizeof(msg), now);
		}
		S.ReleaseMessage(messages[i]);
	}
}

unsigned VideoInput(IDirect3DDevice9* device) {
	enum { Watch = 1, Left = 2, Right = 4, Back = 8, Click = 16 };
	D3DDEVICE_CREATION_PARAMETERS params{};
	if (!device || FAILED(device->GetCreationParameters(&params)) || GetForegroundWindow() != params.hFocusWindow) return 0;
	auto key = [](int code) { return (GetAsyncKeyState(code) & 0x8000) != 0; };
	unsigned input = (key('V') ? Watch : 0) | (key(VK_LEFT) ? Left : 0) |
		(key(VK_RIGHT) ? Right : 0) | (key(VK_BACK) ? Back : 0);
	for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
		XINPUT_STATE state{};
		if (XInputGetState(i, &state) != ERROR_SUCCESS) continue;
		const WORD buttons = state.Gamepad.wButtons;
		if (buttons & XINPUT_GAMEPAD_X) input |= Watch;
		if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) input |= Left;
		if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) input |= Right;
		if (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) input |= Back;
	}
	if (key(VK_LBUTTON)) input |= Click;
	return input;
}

bool ClickedVideoCaption(IDirect3DDevice9* device) {
	D3DDEVICE_CREATION_PARAMETERS params{};
	POINT cursor{};
	RECT bounds{};
	return SUCCEEDED(device->GetCreationParameters(&params)) && GetCursorPos(&cursor) &&
		ScreenToClient(params.hFocusWindow, &cursor) && GetClientRect(params.hFocusWindow, &bounds) &&
		cursor.x >= bounds.right / 5 && cursor.x <= bounds.right * 4 / 5 &&
		cursor.y >= bounds.bottom * 93 / 100 && cursor.y <= bounds.bottom * 98 / 100;
}

void BeginWatching(uint64_t peer, uint64_t now) {
	StopWatching();
	gVideo.target = peer;
	gVideo.subscription = ++gVideo.nextSubscription;
	gVideo.lastPicture = gVideo.lastSubscribe = now;
	VideoPacket msg{gVideo.event, peer, gVideo.subscription, gSelf};
	msg.type = stunt_stream::Watch;
	SendVideo(VideoHost(), msg);
	Log("stunt video: subscribed to %llu", peer);
}
}

static void StuntVideoTick(void* renderer) {
	const bool results = std::exchange(gStuntResultsDrawn, false);
	if (!VideoPreviewEnabled()) return;
	static const bool peerTest = GetEnvironmentVariableW(L"FOUC_TEST_STUNT_VIDEO_PEER_HOST", nullptr, 0) != 0;
	static bool peerTestSelected = false;
	auto gf = GameFlowPtr();
	auto score = *reinterpret_cast<uint8_t**>(0x846514);
	const bool active = gStuntActive && gf && score && *reinterpret_cast<uintptr_t*>(score) == 0x6E3E4C;
	const bool replay = active && *reinterpret_cast<int*>(gf + 0x275C) != 0;
	const bool driving = active && !replay && !results && *reinterpret_cast<int*>(gf + 0x2738) == 8 &&
		*reinterpret_cast<int*>(gf + 0x2754) == 0;
	const bool watchingAllowed = active && !replay && results && gStuntOwnRounds > 0 &&
		gStuntOwnRounds < *reinterpret_cast<int*>(score + 0x3C);
	const uint64_t now = GetTickCount64();
	auto device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<char*>(renderer) + 0x60);
	const unsigned held = watchingAllowed ? VideoInput(device) : 0;
	const unsigned pressed = held & ~std::exchange(gVideo.heldInput, held);

	struct ReleaseIdleCodec {
		~ReleaseIdleCodec() {
			if (!gVideo.sourceActive && !gVideo.target) delete std::exchange(gVideo.codec, nullptr);
#ifdef STUNT_STREAM_OPUS
			if (!gVideo.sourceActive) StopStuntAudioCapture();
#endif
		}
	} release;
	bool capture = false;
	std::wstring caption;
	{
		EnterCriticalSection(&gLock);
		struct Unlock { ~Unlock() { LeaveCriticalSection(&gLock); } } unlock;
		const uint64_t lobby = gHostLobby ? gHostLobby : gJoinedLobby;
		if (lobby != gVideo.lobby) {
			ResetVideoEvent();
			gVideo.consumedEvent = 0;
			gVideo.lobby = lobby;
		}
		if (!active || !lobby || !SteamReady()) {
			if (gVideo.event && lobby && SteamReady()) {
				StopWatching();
			}
			ResetVideoEvent();
			return;
		}
		if (!gVideo.event) {
			const char* value = S.GetLobbyData(gMM, lobby, kVideoEventKey);
			uint64_t event = 0;
			int level = 0;
			if (!value || sscanf_s(value, "%llu:%d", &event, &level) != 2 ||
				event <= gVideo.consumedEvent || level != CurrentLevel()) return;
			gVideo.event = gVideo.consumedEvent = event;
			Log("stunt video: acquired event %llu", event);
		}
		gVideoStatusEvent = gVideo.event;
		gVideoStatusDriving = driving;
		gVideo.members = LobbyPeers();
		gVideo.members.erase(std::remove_if(gVideo.members.begin(), gVideo.members.end(), [](uint64_t id) {
			return std::find(gStuntPeers.begin(), gStuntPeers.end(), id) == gStuntPeers.end();
		}), gVideo.members.end());
		PumpVideo(now);
		if (VideoHost() == gSelf) UpdateVideoRoutes(now);
		if (watchingAllowed && !gVideo.resultsLogged) {
			Log("stunt video: between-attempt results ready");
			gVideo.resultsLogged = true;
		}
		std::vector<uint64_t> targets;
		for (auto peer : gVideo.members) if (DriverLive(peer, now)) targets.push_back(peer);
		if (!watchingAllowed) { if (gVideo.target) StopWatching(); }
		else {
			if (gVideo.codec) {
				auto decoded = gVideo.codec->Take(true);
				if (decoded && gVideo.target && decoded->event == gVideo.event && decoded->subscription == gVideo.decodeEpoch) {
					gVideo.display = std::move(decoded);
				}
			}
			auto current = std::find(targets.begin(), targets.end(), gVideo.target);
			if (gVideo.target && (current == targets.end() || now - gVideo.lastPicture >= 2000)) {
				const auto previous = gVideo.target;
				StopWatching();
				auto next = std::find_if(targets.begin(), targets.end(), [previous](uint64_t target) { return target != previous; });
				if (next != targets.end()) BeginWatching(*next, now);
			} else if (gVideo.target && (pressed & (1 | 8) || ((pressed & 16) && ClickedVideoCaption(device)))) StopWatching();
			else if (gVideo.target && targets.size() > 1 && (pressed & (2 | 4))) {
				const size_t index = static_cast<size_t>(current - targets.begin());
				BeginWatching(targets[(index + ((pressed & 2) ? targets.size() - 1 : 1)) % targets.size()], now);
			} else if (!gVideo.target && !targets.empty() && ((pressed & 1) || ((pressed & 16) && ClickedVideoCaption(device)))) {
				BeginWatching(targets.front(), now);
			}
			if (peerTest && !peerTestSelected && !gVideo.target && !targets.empty()) {
				BeginWatching(targets.front(), now);
				peerTestSelected = true;
			}
			if (gVideo.target && now - gVideo.lastSubscribe >= 500) {
				VideoPacket msg{gVideo.event, gVideo.target, gVideo.subscription, gSelf};
				msg.type = stunt_stream::Watch;
				SendVideo(VideoHost(), msg);
				gVideo.lastSubscribe = now;
			}
			if (gVideo.target) {
				auto route = gVideo.routes.find(gVideo.target);
				if (route != gVideo.routes.end() && now - gVideo.lastFeedback >= 500) {
					const auto& chain = route->second.chain;
					if (chain.Previous(gVideo.target, gSelf)) {
						VideoPacket feedback{gVideo.event, gVideo.target, chain.generation};
						feedback.type = stunt_stream::Feedback;
						feedback.sequence = gVideo.lost || now - gVideo.lastPicture >= 500 ? 1 : 0;
						SendVideo(gVideo.target, feedback);
						gVideo.lost = 0;
						gVideo.lastFeedback = now;
					}
				}
				const auto name = gStuntNames.find(gVideo.target);
				caption = (name == gStuntNames.end() ? L"Live" : name->second) + std::wstring(L"   |   Links/Rechts   |   Zurueck: V / LB");
			} else if (!targets.empty()) caption = L"ZUGUCKEN   [V / X]";
		}
		const auto route = gVideo.routes.find(gSelf);
		const auto next = driving && route != gVideo.routes.end() && now - route->second.lastRoute < stunt_stream::LeaseMs ?
			route->second.chain.Next(gSelf, gSelf) : 0;
		if (next && VideoMember(next)) {
			const auto routeId = route->second.chain.generation;
			if (!gVideo.sourceActive || routeId != gVideo.sourceRoute) {
				Log("stunt video: source started for route %llu", routeId);
				gVideo.sourceActive = true;
				gVideo.sourceRoute = routeId;
				gVideo.adaptation = {};
				gVideo.lastQuality = gVideo.adaptation.level;
				gVideo.forceKey = true;
				gVideo.sent = 0;
				++gVideo.sourceEpoch;
				if (gVideo.codec) gVideo.codec->Clear();
			}
#ifdef STUNT_STREAM_OPUS
			SendStuntSound(next, routeId, now);
#endif
			bool congested = std::any_of(route->second.congestion.begin(), route->second.congestion.end(),
				[now](const auto& feedback) { return now - feedback.second < 1200; });
			if (gVideo.codec) {
				auto frame = gVideo.codec->Take(false);
				if (frame && frame->event == gVideo.event && frame->subscription == gVideo.sourceEpoch) {
					const size_t size = frame->bytes.size();
					const size_t cost = size + ((size + stunt_stream::FragmentBytes - 1) / stunt_stream::FragmentBytes) * sizeof(VideoPacket);
					if (ReserveStuntBandwidth(cost, now)) {
						VideoPacket msg{gVideo.event, gSelf, routeId};
						msg.type = stunt_stream::Picture;
						msg.sequence = frame->sequence;
						msg.stamp = frame->stamp;
						msg.size = static_cast<uint32_t>(size);
						msg.width = static_cast<uint16_t>(frame->width);
						msg.height = static_cast<uint16_t>(frame->height);
						msg.quality = frame->quality;
						msg.key = frame->key;
						for (size_t offset = 0; offset < size; offset += stunt_stream::FragmentBytes, ++msg.fragment) {
							if (!SendVideo(next, msg, frame->bytes.data() + offset,
								(std::min)(stunt_stream::FragmentBytes, size - offset))) congested = true;
						}
					} else congested = true;
				}
			}
			gVideo.adaptation.Observe(congested, now);
			if (gVideo.lastQuality != gVideo.adaptation.level) {
				gVideo.lastQuality = gVideo.adaptation.level;
				gVideo.forceKey = true;
				++gVideo.sourceEpoch;
				if (gVideo.codec) gVideo.codec->Clear();
				Log("stunt video: quality changed to %u", gVideo.lastQuality);
			}
			capture = now - gVideo.lastCapture >= 1000 / stunt_stream::Levels[gVideo.lastQuality].fps;
			if (capture) gVideo.lastCapture = now;
		} else if (gVideo.sourceActive) {
			gVideo.sourceActive = false;
			if (gVideo.codec) gVideo.codec->Clear();
		}
	}
	if (capture) {
		Frame frame{gVideo.event, gVideo.sourceEpoch, gVideo.sent + 1, 0, 0, {}};
		frame.quality = static_cast<uint8_t>(gVideo.lastQuality);
		frame.stamp = static_cast<uint32_t>(now);
		frame.key = gVideo.forceKey || gVideo.sent % stunt_stream::Levels[gVideo.lastQuality].fps == 0;
		if (Capture(device, frame)) {
			++gVideo.sent;
			gVideo.forceKey = false;
			gVideo.Worker().Submit(false, std::move(frame));
		}
	}
	if (gVideo.display && Draw(device, *gVideo.display)) {
#ifdef STUNT_STREAM_OPUS
		while (!gSound.incoming.empty()) {
			const auto& block = gSound.incoming.front();
			const int32_t audioLead = static_cast<int32_t>(block.stamp - gVideo.display->stamp);
			if (audioLead > 40) break;
			if (audioLead >= -100) gSound.output.Play(block.bytes.data(), block.bytes.size(), block.channels);
			gSound.incoming.pop_front();
		}
#endif
		if (gVideo.display->sequence != gVideo.lastDisplayedSequence) gVideo.lastPicture = now;
		gVideo.lastDisplayedSequence = gVideo.display->sequence;
		if (gVideo.displayedSubscription != gVideo.subscription) {
			EnterCriticalSection(&gLock);
			auto route = gVideo.routes.find(gVideo.target);
			if (SteamReady() && route != gVideo.routes.end()) {
				VideoPacket msg{gVideo.event, gVideo.target, route->second.chain.generation};
				msg.type = stunt_stream::Presented;
				msg.sequence = gVideo.display->sequence;
				SendVideo(gVideo.target, msg);
			}
			LeaveCriticalSection(&gLock);
			gVideo.displayedSubscription = gVideo.subscription;
			Log("stunt video: first picture %u from %llu", gVideo.display->sequence, gVideo.target);
		}
	}
	if (!caption.empty()) {
		if (caption != gVideo.captionText) {
			gVideo.captionText = caption;
			gVideo.caption = MakeCaption(caption);
		}
		Draw(device, gVideo.caption, true);
	}
}

static void PlaceStuntHooks() {
	auto patch = [](uintptr_t slot, void* hook, void** orig) {
		DWORD old;
		VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old);
		*orig = *(void**)slot;
		*(void**)slot = hook;
		VirtualProtect((void*)slot, 4, old, &old);
	};
	if (*(uintptr_t*)kStuntInitSlot != 0x499A60 || *(uintptr_t*)kStuntSimSlot != 0x49B1E0) {
		Log("stunt hooks skipped: unexpected ScoreManagerStunt vtable");
		return;
	}
	patch(kStuntInitSlot, (void*)StuntInitHook, (void**)&gStuntInitOrig);
	patch(kStuntSimSlot, (void*)StuntSimHook, (void**)&gStuntSimOrig);
	if (*(uintptr_t*)kStuntResultsSlot == 0x49AEE0)
		patch(kStuntResultsSlot, (void*)StuntResultsHook, (void**)&gStuntResultsOrig);
	else Log("stunt ranking hook skipped: unexpected vtable");
	if (*(uintptr_t*)kSingleStuntTabDrawSlot == 0x4B71C0)
		patch(kSingleStuntTabDrawSlot, (void*)SingleStuntTabDrawHook, (void**)&gSingleStuntTabDrawOrig);
	else Log("stunt results hook skipped: unexpected vtable");
}

static void PlaceEndSceneHook() {
	auto slot = (uintptr_t*)kEndSceneSlot;
	gEndSceneOrig = (HRESULT(__thiscall*)(void*))*slot;
	DWORD old;
	VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old);
	*slot = (uintptr_t)&EndSceneHook;
	VirtualProtect(slot, 4, old, &old);
}

static bool PlaceScriptHook() {
	auto p = (uint8_t*)kScriptHookAddr;
	if (*p != 0xE8) { Log("ERROR: unexpected code at 0x%X, Lua functions not registered", (unsigned)kScriptHookAddr); return false; }
	int32_t rel; memcpy(&rel, p + 1, 4);
	gScriptHookPrev = (void(*)(void*, void*, int))(kScriptHookAddr + 5 + rel);
	int32_t nrel = (int32_t)((uintptr_t)&ScriptHook - (kScriptHookAddr + 5));
	DWORD old;
	VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
	memcpy(p + 1, &nrel, 4);
	VirtualProtect(p, 5, old, &old);
	return true;
}

// Chloe's copyright screen ("Chloe Collection", Special Thanks, "Press ENTER to Continue"): FlatOutUCChloeCollection_gcp.asi
// replaces the CALL 0x41FF10 at 0x55BA6F with a JMP to a loop that waits for Enter (skippablecopyright.h). The original
// call is written back as soon as Chloe's patch is in place (her plugin loads after this one), so the game continues
// without waiting.
static DWORD WINAPI CopyrightSkipThread(LPVOID) {
	auto p = (uint8_t*)0x55BA6F;
	static const uint8_t original[5] = { 0xE8, 0x9C, 0x44, 0xEC, 0xFF };   // call 0x41FF10
	for (int i = 0; i < 30000; i++) {
		if (p[0] == 0xE9) {
			DWORD old;
			VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
			memcpy(p, original, 5);
			VirtualProtect(p, 5, old, &old);
			Log("copyright screen: Chloe's wait for ENTER removed");
			return 0;
		}
		Sleep(1);
	}
	Log("copyright screen: Chloe's patch not found");
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(inst);
		InitializeCriticalSection(&gLock);
		InitializeCriticalSection(&gLogLock);
		GetModuleFileNameA(nullptr, gLogPath, MAX_PATH);
		if (char* slash = strrchr(gLogPath, '\\')) strcpy_s(slash + 1, gLogPath + MAX_PATH - (slash + 1), "ChloeSteamNet.log");
		DeleteFileA(gLogPath);
		// only the Fouc.exe this was written for (4242504 bytes, identical in the GFWL and the Steam version)
		char exe[MAX_PATH]; GetModuleFileNameA(nullptr, exe, MAX_PATH);
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (!GetFileAttributesExA(exe, GetFileExInfoStandard, &fa) || fa.nFileSizeLow != 4242504) {
			Log("unknown Fouc.exe, plugin disabled");
			return TRUE;
		}
		if (VideoPreviewEnabled()) kProtocol = "chloecustom-183-steam-4-stunt-stream-preview";
		Log("ChloeSteamNet loaded (protocol %s)", kProtocol);
		PlaceScriptHook();
		PlaceInstantActionFilter();
		if (!ReplaceCall(reinterpret_cast<uint8_t*>(0x4686BC), 0x469F50, uintptr_t(&AssignDerbyGrid)))
			Log("Derby grid patch unavailable: unexpected code or memory protection");
		if (!ReplaceCall(reinterpret_cast<uint8_t*>(0x4E83D9), 0x4E5810, uintptr_t(&DerbyNitroStub)))
			Log("Derby nitro patch unavailable: unexpected code or memory protection");
		PlaceEndSceneHook();
		PlaceStuntHooks();
		LoadAINames();
		LoadAIDrivers();
		PlaceAIDriverHook();
		PlaceDamageHook();
		if (HMODULE xl = GetModuleHandleA("xlive.dll"))
			if (!PatchImport(xl, "steam_api.dll", "SteamAPI_Shutdown", (void*)hkSteamShutdown, (void**)&oSteamShutdown))
				Log("xlive.dll: SteamAPI_Shutdown import not found");
		if (HANDLE th = CreateThread(nullptr, 0, CopyrightSkipThread, nullptr, 0, nullptr)) CloseHandle(th);
		TryPatchChloeMP();
	}
	if (reason == DLL_PROCESS_DETACH) gSteamShutdown = true;
	return TRUE;
}
