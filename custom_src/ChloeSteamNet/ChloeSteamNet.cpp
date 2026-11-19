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
#include <deque>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------------------------------------------------------
// constants

static const char*    kProtocol      = "chloecustom-183-steam-1"; // lobbies of other pack versions are not listed
static const char*    kKeyProtocol   = "chloecustommp";
static const char*    kKeyName       = "name";
static const char*    kKeyHost       = "host";
static const uint16_t kServerPort    = 23756;                     // ChloeNet_CreateGame(23756) in chloemultiplayer.bed
static const int      kChannel       = 23756;                     // SteamNetworkingMessages channel (ZOOM's xlive uses its own)
static const uint8_t  kTypeData      = 0x43;
static const uint8_t  kTypeHello     = 0x48;   // host -> lobby members, client -> host while connecting
static const uint8_t  kTypeHelloAck  = 0x41;   // host -> client (answer to a client hello)
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
static bool SteamReady() {
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
	}
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

static bool gImportsPatched = false;
static void TryPatchChloeMP() {
	if (gImportsPatched) return;
	HMODULE mp = GetModuleHandleA("FlatOutUCCustomMP_gcp.asi");
	if (!mp) return;
	gImportsPatched = PatchImports(mp);
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

static int L_RefreshLobbies(void*) {
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
	auto gf = *(uint8_t**)0x9298FAC;               // GameFlow
	if (!gf) return;
	if (*(int*)(gf + 0x4A4) != 2) { gStuntLogged = false; return; }   // nGameState != GAME_STATE_RACE
	if (*(int*)(gf + 0x514) != 2) return;                            // nGameRules != GR_STUNT
	if (!TryEnterCriticalSection(&gLock)) return;                    // network thread busy, next frame
	bool inMultiplayer = !gSocketPort.empty();                       // Chloe's multiplayer sockets open
	LeaveCriticalSection(&gLock);
	if (!inMultiplayer) return;
	auto host = *(uint8_t**)(gf + 0x2820);
	if (!host) return;
	uint32_t levelId = *(uint32_t*)(host + 0x2098C);
	int flowPos = *(int*)(gf + 0x520);                               // nStuntStartPosition
	int pos = StuntStartForLevel(levelId);
	if (!pos) pos = flowPos;
	if (pos < 1 || pos > 8) return;
	auto begin = *(uint8_t***)(host + 0x14), end = *(uint8_t***)(host + 0x18);
	if (!begin || end < begin || end - begin > 32) return;
	for (auto it = begin; it != end; ++it) {
		uint8_t* ply = *it;
		if (!ply || !*(void**)(ply + 0x294)) continue;               // pCar
		uint32_t& start = *(uint32_t*)(ply + 0x2CC);                 // nStartPosition
		if (!gStuntLogged) Log("stunt level %u: start point %d (GameFlow %d), player %d had %u", levelId, pos, flowPos, (int)(it - begin), start);
		start = (uint32_t)pos;
	}
	gStuntLogged = true;
}

static HRESULT __fastcall EndSceneHook(void* self, void*) {
	FixStuntStartPositions();
	return gEndSceneOrig(self);
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
		Log("ChloeSteamNet loaded (protocol %s)", kProtocol);
		PlaceScriptHook();
		PlaceEndSceneHook();
		TryPatchChloeMP();
	}
	return TRUE;
}
