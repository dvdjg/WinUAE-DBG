#include "sysconfig.h"
#include <Ws2tcpip.h>
#include "sysdeps.h"

#include <thread>
#include <vector>
#include <array>

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "inputdevice.h"
#include "uae.h"
#include "debugmem.h"
#include "render.h" // AmigaMonitor
#include "custom.h"
#include "xwin.h" // xcolnr
#include "drawing.h" // color_entry
#include "win32.h"
#include "savestate.h"
#include "disk.h"

extern BITMAPINFO* screenshot_get_bi();
extern void* screenshot_get_bits();
extern int screenshot_prepare(int monid, struct vidbuffer* vb);
extern void vsync_display_render();

// from main.cpp
extern struct uae_prefs currprefs;

// from newcpu.cpp
/*static*/ extern int baseclock;

// from custom.cpp
/*static*/ extern struct color_entry current_colors;
extern uae_u8 *save_custom(size_t *len, uae_u8 *dstptr, int full);
extern int debug_safe_addr(uaecptr addr, int size);

// from debug.cpp
extern uae_u8 *get_real_address_debug(uaecptr addr);
extern void initialize_memwatch(int mode);
extern void memwatch_setup();
/*static*/ extern int trace_mode;
/*static*/ extern uae_u32 trace_param[3];
/*static*/ extern uaecptr processptr;
/*static*/ extern uae_char *processname;
/*static*/ extern int memwatch_triggered;
/*static*/ extern struct memwatch_node mwhit;
extern int debug_illegal;
extern uae_u64 debug_illegal_mask;

// from writelog.cpp
extern int consoleopen;

#include "barto_gdbserver.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// VS2022: Test or Release/FullRelease config 
// -s input.config=1 -s input.1.keyboard.0.button.41.GRAVE=SPC_SINGLESTEP.0    -s use_gui=no -s quickstart=a500,1 -s debugging_features=gdbserver -s filesystem=rw,dh0:c:\Users\Chuck\Documents\Visual_Studio_Code\amiga-debug\bin\dh0
// c:\Users\Chuck\Documents\Visual_Studio_Code\amiga-debug\bin\opt\bin> m68k-amiga-elf-gdb.exe -ex "set debug remote 1" -ex "target remote :2345" -ex "monitor profile xxx" ..\..\..\template\a.mingw.elf

namespace barto_gdbserver {
	bool is_connected();
	bool data_available();
	void disconnect();

	// ELF .text section base address for amiga-gcc toolchain
	constexpr uaecptr ELF_TEXT_BASE = 0x400;

	static bool in_handle_packet = false;
	struct tracker {
		tracker() { backup = in_handle_packet; in_handle_packet = true; }
		~tracker() { in_handle_packet = backup; }
	private: 
		bool backup;
	};

	void barto_log(const char* format, ...);
	void barto_log(const wchar_t* format, ...);

	// Log file for debugging (writable via monitor logfile command)
	static FILE* log_file = nullptr;
	static std::string log_file_path;

	static std::string string_replace_all(const std::string& str, const std::string& search, const std::string& replace) {
		std::string copy(str);
		size_t start = 0;
		for(;;) {
			auto p = copy.find(search, start);
			if(p == std::string::npos)
				break;

			copy.replace(p, search.size(), replace);
			start = p + replace.size();
		}
		return copy;
	}

	static std::string string_to_utf8(LPCWSTR string) {
		int len = WideCharToMultiByte(CP_UTF8, 0, string, -1, nullptr, 0, nullptr, nullptr);
		std::unique_ptr<char[]> buffer(new char[len]);
		WideCharToMultiByte(CP_UTF8, 0, string, -1, buffer.get(), len, nullptr, nullptr);
		return std::string(buffer.get());
	}

	static std::wstring utf8_to_wide(const std::string& utf8) {
		if(utf8.empty()) return std::wstring();
		int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
		std::wstring out(len, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], len);
		return out;
	}

	static constexpr char hex[]{ "0123456789abcdef" };
	static std::string hex8(uint8_t v) {
		std::string ret;
		ret += hex[v >> 4];
		ret += hex[v & 0xf];
		return ret;
	}
	static std::string hex32(uint32_t v) {
		std::string ret;
		for(int i = 28; i >= 0; i -= 4)
			ret += hex[(v >> i) & 0xf];
		return ret;
	}

	// MCP-WINUAE-EMU EXTENSION: Convert hex character to int (-1 if invalid)
	static int hex_to_int(char c) {
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'a' && c <= 'f') return c - 'a' + 10;
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	static std::string from_hex(const std::string& s) {
		std::string ret;
		for(size_t i = 0, len = s.length() & ~1; i < len; i += 2) {
			uint8_t v{};
			if(s[i] >= '0' && s[i] <= '9')
				v |= (s[i] - '0') << 4;
			else if(s[i] >= 'a' && s[i] <= 'f')
				v |= (s[i] - 'a' + 10) << 4;
			else if(s[i] >= 'A' && s[i] <= 'F')
				v |= (s[i] - 'A' + 10) << 4;
			if(s[i + 1] >= '0' && s[i + 1] <= '9')
				v |= (s[i + 1] - '0');
			else if(s[i + 1] >= 'a' && s[i + 1] <= 'f')
				v |= (s[i + 1] - 'a' + 10);
			else if(s[i + 1] >= 'A' && s[i + 1] <= 'F')
				v |= (s[i + 1] - 'A' + 10);
			ret += (char)v;
		}
		return ret;
	}

	static std::string to_hex_addr(uaecptr addr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%08x", addr);
		return std::string(buf);
	}

	static std::string to_hex(const std::string& s) {
		std::string ret;
		for(size_t i = 0, len = s.length(); i < len; i++) {
			uint8_t v = s[i];
			ret += hex[v >> 4];
			ret += hex[v & 0xf];
		}
		return ret;
	}

/*	#pragma comment(lib, "Bcrypt.lib")
	#ifndef NT_SUCCESS
		#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
	#endif
	std::array<uint8_t, 32> sha256(const void* addr, size_t size) {
		std::array<uint8_t, 32> hash{};

		BCRYPT_ALG_HANDLE AlgHandle = nullptr;
		BCRYPT_HASH_HANDLE HashHandle = nullptr;
		if(NT_SUCCESS(BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG))) {
			DWORD HashLength = 0;
			DWORD ResultLength = 0;
			if(NT_SUCCESS(BCryptGetProperty(AlgHandle, BCRYPT_HASH_LENGTH, (PBYTE)&HashLength, sizeof(HashLength), &ResultLength, 0)) && HashLength == hash.size()) {
				if(NT_SUCCESS(BCryptCreateHash(AlgHandle, &HashHandle, nullptr, 0, nullptr, 0, 0))) {
					(void)BCryptHashData(HashHandle, kickmem_bank.baseaddr, kickmem_bank.allocated_size, 0);
					(void)BCryptFinishHash(HashHandle, hash.data(), (ULONG)hash.size(), 0);
					BCryptDestroyHash(HashHandle);
				}
			}
			BCryptCloseAlgorithmProvider(AlgHandle, 0);
		}

		return hash;
	}

	std::array<uint8_t, 16> sha1(const void* addr, size_t size) {
		std::array<uint8_t, 16> hash{};

		BCRYPT_ALG_HANDLE AlgHandle = nullptr;
		BCRYPT_HASH_HANDLE HashHandle = nullptr;
		if(NT_SUCCESS(BCryptOpenAlgorithmProvider(&AlgHandle, BCRYPT_SHA1_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG))) {
			DWORD HashLength = 0;
			DWORD ResultLength = 0;
			if(NT_SUCCESS(BCryptGetProperty(AlgHandle, BCRYPT_HASH_LENGTH, (PBYTE)&HashLength, sizeof(HashLength), &ResultLength, 0)) && HashLength == hash.size()) {
				if(NT_SUCCESS(BCryptCreateHash(AlgHandle, &HashHandle, nullptr, 0, nullptr, 0, 0))) {
					(void)BCryptHashData(HashHandle, kickmem_bank.baseaddr, kickmem_bank.allocated_size, 0);
					(void)BCryptFinishHash(HashHandle, hash.data(), (ULONG)hash.size(), 0);
					BCryptDestroyHash(HashHandle);
				}
			}
			BCryptCloseAlgorithmProvider(AlgHandle, 0);
		}

		return hash;
	}
*/

	std::thread connect_thread;
	PADDRINFOW socketinfo;
	SOCKET gdbsocket{ INVALID_SOCKET };
	SOCKET gdbconn{ INVALID_SOCKET };
	char socketaddr[sizeof(SOCKADDR_INET)];
	bool useAck{ true };
	uint32_t baseText{};
	uint32_t sizeText{};
	uint32_t systemStackLower{}, systemStackUpper{};
	uint32_t stackLower{}, stackUpper{};
	std::vector<uint32_t> sections; // base for every section
	
	// Store original ELF addresses for breakpoints (for deferred relocation)
	std::vector<uaecptr> breakpoint_elf_addresses;
	
	// Relocate all existing breakpoints when baseText is calculated
	void relocate_breakpoints() {
		barto_log("RELOC: relocate_breakpoints() called. baseText=0x%x, pending=%d\n", 
			baseText, (int)breakpoint_elf_addresses.size());
		if(baseText < ELF_TEXT_BASE) {
			barto_log("RELOC: SKIP - baseText(0x%x) < ELF_TEXT_BASE(0x%x)\n", baseText, ELF_TEXT_BASE);
			return;
		}
		uaecptr loadOffset = baseText - ELF_TEXT_BASE;
		barto_log("RELOC: loadOffset = 0x%x\n", loadOffset);
		int relocated = 0;
		for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
			uaecptr elfAddr = breakpoint_elf_addresses[i];
			if(elfAddr >= ELF_TEXT_BASE && elfAddr < ELF_TEXT_BASE + 0x100000) {
				uaecptr relocatedAddr = elfAddr + loadOffset;
				for(auto& bpn : bpnodes) {
					if(bpn.enabled && bpn.value1 == elfAddr) {
						barto_log("RELOC: BP[%d] 0x%x -> 0x%x\n", (int)i, elfAddr, relocatedAddr);
						bpn.value1 = relocatedAddr;
						relocated++;
						break;
					}
				}
			}
		}
		barto_log("RELOC: Done. Relocated %d breakpoints\n", relocated);
	}
	
	std::string profile_outname;
	int profile_num_frames{};
	int profile_frame_count{};
	std::unique_ptr<cpu_profiler_unwind[]> profile_unwind{};

	// FIX: Save trigger processname locally because deactivate_debugger() clears the global
	std::string saved_processname;

	// Helper: Find a process by name in the Exec task lists
	// Returns the address of the Process structure, or 0 if not found
	// The name matching is case-insensitive and supports partial matches (prefix ':' means any)
	uaecptr find_process_by_name(const char* target_name) {
		if(!target_name || !target_name[0]) return 0;
		
		auto execbase = get_long_debug(4);
		if(!execbase) return 0;
		
		// Check if target_name starts with ':' (means match any process with that suffix)
		bool match_suffix = (target_name[0] == ':');
		const char* match_pattern = match_suffix ? (target_name + 1) : target_name;
		
		barto_log("FINDPROC: Looking for process matching '%s' (suffix_match=%d)\n", 
			match_pattern, match_suffix);
		
		// Iterate over TaskReady and TaskWait lists
		// TaskReady: execbase + 406 (offset for Ready list head)
		// TaskWait: execbase + 420 (offset for Wait list head)
		// Also check the current task at execbase + 276
		
		auto check_task = [&](uaecptr task_addr) -> uaecptr {
			if(!task_addr) return 0;
			
			auto ln_Type = get_byte_debug(task_addr + 8);
			if(ln_Type != 13) return 0; // Not a process (NT_PROCESS = 13)
			
			auto ln_Name_ptr = get_long_debug(task_addr + 10);
			if(!ln_Name_ptr) return 0;
			
			auto ln_Name = reinterpret_cast<char*>(get_real_address_debug(ln_Name_ptr));
			if(!ln_Name) return 0;
			
			barto_log("FINDPROC:   Checking process '%s' at 0x%x\n", ln_Name, task_addr);
			
			// Match: either exact match or suffix match (if target starts with ':')
			bool matches = false;
			if(match_suffix) {
				// Match if ln_Name ends with match_pattern (case-insensitive)
				size_t name_len = strlen(ln_Name);
				size_t pattern_len = strlen(match_pattern);
				if(name_len >= pattern_len) {
					matches = (_stricmp(ln_Name + name_len - pattern_len, match_pattern) == 0);
				}
			} else {
				// Exact match (case-insensitive)
				matches = (_stricmp(ln_Name, match_pattern) == 0);
			}
			
			if(matches) {
				barto_log("FINDPROC:   MATCH FOUND: '%s' at 0x%x\n", ln_Name, task_addr);
				return task_addr;
			}
			return 0;
		};
		
		// Helper to iterate a task list
		auto iterate_list = [&](uaecptr list_head) -> uaecptr {
			// List structure: lh_Head at offset 0
			auto node = get_long_debug(list_head);
			while(node) {
				// Check ln_Succ to see if we've reached the end (points to list tail)
				auto ln_Succ = get_long_debug(node);
				if(!ln_Succ) break;
				
				auto found = check_task(node);
				if(found) return found;
				
				node = ln_Succ;
			}
			return 0;
		};
		
		// First check ThisTask (current process)
		auto thisTask = get_long_debug(execbase + 276);
		auto found = check_task(thisTask);
		if(found) return found;
		
		// Then check TaskReady list (offset 406)
		found = iterate_list(execbase + 406);
		if(found) return found;
		
		// Then check TaskWait list (offset 420) 
		found = iterate_list(execbase + 420);
		if(found) return found;
		
		barto_log("FINDPROC: Process '%s' NOT FOUND\n", target_name);
		return 0;
	}

	// Helper: Find a CLI process that has a module matching the given name
	// Returns the Process address and sets outSegList to the module's SegList
	// This is useful for finding programs loaded via "Run" command
	uaecptr find_cli_with_module(const char* module_name, uaecptr* outSegList) {
		if(!module_name || !module_name[0]) return 0;
		if(outSegList) *outSegList = 0;
		
		auto execbase = get_long_debug(4);
		if(!execbase) return 0;
		
		// BADDR macro: convert BPTR to APTR
		auto BADDR = [](uaecptr bptr) -> uaecptr { return bptr << 2; };
		
		// Strip ':' prefix if present
		bool match_suffix = (module_name[0] == ':');
		const char* match_pattern = match_suffix ? (module_name + 1) : module_name;
		
		barto_log("FINDCLI: Looking for CLI with module matching '%s'\n", match_pattern);
		
		auto check_cli_process = [&](uaecptr task_addr) -> bool {
			if(!task_addr) return false;
			
			auto ln_Type = get_byte_debug(task_addr + 8);
			if(ln_Type != 13) return false; // NT_PROCESS = 13
			
			// Get pr_CLI (BPTR to CommandLineInterface)
			auto pr_CLI = get_long_debug(task_addr + 0xAC);
			if(!pr_CLI) return false;
			
			auto cli = BADDR(pr_CLI);
			if(!cli) return false;
			
			// Get cli_CommandName (BSTR - first byte is length)
			auto cli_CommandName = BADDR(get_long_debug(cli + 0x10));
			char cmd_name[256] = {0};
			if(cli_CommandName) {
				auto len = get_byte_debug(cli_CommandName);
				for(int i = 0; i < len && i < 255; i++) {
					cmd_name[i] = get_byte_debug(cli_CommandName + 1 + i);
				}
			}
			
			// Get cli_Module (BPTR to SegList)
			auto cli_Module = get_long_debug(cli + 0x3C);
			uaecptr segList = cli_Module ? BADDR(cli_Module) : 0;
			
			auto ln_Name_ptr = get_long_debug(task_addr + 10);
			auto ln_Name = ln_Name_ptr ? reinterpret_cast<char*>(get_real_address_debug(ln_Name_ptr)) : nullptr;
			
			barto_log("FINDCLI:   Process '%s' at 0x%x: cli=0x%x, cmd='%s', module=0x%x\n",
				ln_Name ? ln_Name : "?", task_addr, cli, cmd_name, segList);
			
			// Match command name or process name
			bool matches = false;
			if(cmd_name[0]) {
				if(match_suffix) {
					size_t name_len = strlen(cmd_name);
					size_t pattern_len = strlen(match_pattern);
					if(name_len >= pattern_len) {
						matches = (_stricmp(cmd_name + name_len - pattern_len, match_pattern) == 0);
					}
				} else {
					matches = (_stricmp(cmd_name, match_pattern) == 0);
				}
			}
			
			if(matches && segList) {
				barto_log("FINDCLI:   MATCH FOUND: '%s' -> segList=0x%x\n", cmd_name, segList);
				if(outSegList) *outSegList = segList;
				return true;
			}
			return false;
		};
		
		// Helper to iterate a task list
		auto iterate_list = [&](uaecptr list_head) -> uaecptr {
			auto node = get_long_debug(list_head);
			while(node) {
				auto ln_Succ = get_long_debug(node);
				if(!ln_Succ) break;
				
				if(check_cli_process(node)) return node;
				
				node = ln_Succ;
			}
			return 0;
		};
		
		// Check ThisTask
		auto thisTask = get_long_debug(execbase + 276);
		if(check_cli_process(thisTask)) return thisTask;
		
		// Check TaskReady and TaskWait
		auto found = iterate_list(execbase + 406);
		if(found) return found;
		
		found = iterate_list(execbase + 420);
		if(found) return found;
		
		barto_log("FINDCLI: Module '%s' NOT FOUND\n", module_name);
		return 0;
	}

	// Helper: call deactivate_debugger() but preserve processname for GDB server
	void deactivate_debugger_preserve_processname() {
		// Save processname before deactivate clears it
		if(processname && saved_processname.empty()) {
			saved_processname = processname;
		}
		deactivate_debugger();
		// Restore processname (use _strdup for char* strings)
		if(!saved_processname.empty() && !processname) {
			processname = _strdup(saved_processname.c_str());
		}
	}

	enum class state {
		inited,
		connected,
		debugging,
		profile,
		profiling,
	};

	state debugger_state{ state::inited };

	bool is_connected() {
		socklen_t sa_len = sizeof(SOCKADDR_INET);
		if(gdbsocket == INVALID_SOCKET)
			return false;
		if(gdbconn == INVALID_SOCKET) {
			struct timeval tv;
			fd_set fd;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			fd.fd_array[0] = gdbsocket;
			fd.fd_count = 1;
			if(select(1, &fd, nullptr, nullptr, &tv)) {
				gdbconn = accept(gdbsocket, (struct sockaddr*)socketaddr, &sa_len);
				if(gdbconn != INVALID_SOCKET)
					barto_log("GDBSERVER: connection accepted\n");
			}
		}
		return gdbconn != INVALID_SOCKET;
	}

	bool data_available() {
		if(is_connected()) {
			struct timeval tv;
			fd_set fd;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			fd.fd_array[0] = gdbconn;
			fd.fd_count = 1;
			int err = select(1, &fd, nullptr, nullptr, &tv);
			if(err == SOCKET_ERROR) {
				disconnect();
				return false;
			}
			if(err > 0)
				return true;
		}
		return false;
	}

	bool listen() {
		barto_log("GDBSERVER: listen()\n");

		assert(debugger_state == state::inited);

		WSADATA wsaData = { 0 };
		if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			DWORD lasterror = WSAGetLastError();
			barto_log(_T("GDBSERVER: can't open winsock, error %d\n"), lasterror);
			return false;
		}
		int err;
		const int one = 1;
		const struct linger linger_1s = { 1, 1 };
		constexpr auto name = _T("127.0.0.1");
		constexpr auto port = _T("2345");

		err = GetAddrInfoW(name, port, nullptr, &socketinfo);
		if(err < 0) {
			barto_log(_T("GDBSERVER: GetAddrInfoW() failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}
		gdbsocket = socket(socketinfo->ai_family, socketinfo->ai_socktype, socketinfo->ai_protocol);
		if(gdbsocket == INVALID_SOCKET) {
			barto_log(_T("GDBSERVER: socket() failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}
		err = ::bind(gdbsocket, socketinfo->ai_addr, (int)socketinfo->ai_addrlen);
		if(err < 0) {
			barto_log(_T("GDBSERVER: bind() failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}
		err = ::listen(gdbsocket, 1);
		if(err < 0) {
			barto_log(_T("GDBSERVER: listen() failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}
		err = setsockopt(gdbsocket, SOL_SOCKET, SO_LINGER, (char*)&linger_1s, sizeof linger_1s);
		if(err < 0) {
			barto_log(_T("GDBSERVER: setsockopt(SO_LINGER) failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}
		err = setsockopt(gdbsocket, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof one);
		if(err < 0) {
			barto_log(_T("GDBSERVER: setsockopt(SO_REUSEADDR) failed, %s:%s: %d\n"), name, port, WSAGetLastError());
			return false;
		}

		barto_log("GDBSERVER: listen() succeeded\n");
		return true;
	}

	bool init() {
		if(currprefs.debugging_features & (1 << 2)) { // "gdbserver"
			close();

			warpmode(1);
			//cfgfile_modify(-1, _T("cpu_speed max"), 0, nullptr, 0);
			//cfgfile_modify(-1, _T("cpu_cycle_exact false"), 0, nullptr, 0);
			//cfgfile_modify(-1, _T("cpu_memory_cycle_exact false"), 0, nullptr, 0);
			//cfgfile_modify(-1, _T("blitter_cycle_exact false"), 0, nullptr, 0);
			//cfgfile_modify(-1, _T("warp true"), 0, nullptr, 0); // last

			// disable console
			static TCHAR empty[2] = { 0 };
			setconsolemode(empty, 1);
			consoleopen = 1;

			// Auto-open log file for debugging
			if(!log_file) {
				char temp_path[MAX_PATH];
				if(GetTempPathA(MAX_PATH, temp_path)) {
					log_file_path = std::string(temp_path) + "winuae-gdb.log";
					log_file = fopen(log_file_path.c_str(), "w");
				}
			}

			// VERSION IDENTIFICATION - Always log at startup
			barto_log("========================================\n");
			barto_log("GDBSERVER: WinUAE-DBG v2.0 (axewater fork)\n");
			barto_log("GDBSERVER: Build: %s %s\n", __DATE__, __TIME__);
#ifdef _WIN64
			barto_log("GDBSERVER: Architecture: x64\n");
#else
			barto_log("GDBSERVER: Architecture: x86\n");
#endif
			barto_log("GDBSERVER: ELF_TEXT_BASE=0x%x\n", ELF_TEXT_BASE);
			if(log_file) {
				barto_log("GDBSERVER: Log file: %s\n", log_file_path.c_str());
			}
			barto_log("========================================\n");

			activate_debugger();
			initialize_memwatch(0);

			if(currprefs.debugging_trigger[0]) {
				// from debug.cpp@process_breakpoint()
				processptr = 0;
				xfree(processname);
				processname = nullptr;
				processname = ua(currprefs.debugging_trigger);
				trace_mode = TRACE_CHECKONLY;
				barto_log("GDBSERVER: DEBUG state::inited - debugging_trigger='%s', processname set to '%s'\n", 
					currprefs.debugging_trigger, processname ? processname : "(null)");
			} else {
				// savestate debugging
				baseText = 0;
				sizeText = 0x7fff'ffff;
			}

			// call as early as possible to avoid delays with GDB having to retry to connect...
			listen();
		}

		return true;
	}

	void close() {
		barto_log(_T("GDBSERVER: close()\n"));
		if(gdbconn != INVALID_SOCKET)
			closesocket(gdbconn);
		gdbconn = INVALID_SOCKET;
		if(gdbsocket != INVALID_SOCKET)
			closesocket(gdbsocket);
		gdbsocket = INVALID_SOCKET;
		if(socketinfo)
			FreeAddrInfoW(socketinfo);
		socketinfo = nullptr;
		WSACleanup();
	}

	void disconnect() {
		if(gdbconn == INVALID_SOCKET)
			return;
		closesocket(gdbconn);
		gdbconn = INVALID_SOCKET;
		barto_log(_T("GDBSERVER: disconnect\n"));
	}

	// from binutils-gdb/gdb/m68k-tdep.c
/*	static const char* m68k_register_names[] = {
		"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
		"a0", "a1", "a2", "a3", "a4", "a5", "a6", "sp", //BARTO
		"sr", "pc", //BARTO
		"fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7",
		"fpcontrol", "fpstatus", "fpiaddr"
	}*/
	enum regnames {
		D0, D1, D2, D3, D4, D5, D6, D7,
		A0, A1, A2, A3, A4, A5, A6, A7,
		SR, PC
	};

	static std::string get_register(int reg) {
		uint32_t regvalue{};
		// need to byteswap because GDB expects 68k big-endian
		switch(reg) {
		case SR: 
			regvalue = regs.sr; 
			break;
		case PC: 
			regvalue = M68K_GETPC; 
			break;
		case D0: case D1: case D2: case D3: case D4: case D5: case D6: case D7:
			regvalue = m68k_dreg(regs, reg - D0);
			break;
		case A0: case A1: case A2: case A3: case A4: case A5: case A6: case A7:
			regvalue = m68k_areg(regs, reg - A0);
			break;
		default:
			return "xxxxxxxx";
		}
		return hex32(regvalue);
	}

	static std::string get_registers() {
		barto_log("GDBSERVER: PC=%x\n", M68K_GETPC);
		std::string ret;
		for(int reg = 0; reg < 18; reg++)
			ret += get_register(reg);
		return ret;
	}

	// MCP-WINUAE-EMU EXTENSION: Write individual register
	static bool set_register(int reg, uint32_t value) {
		barto_log("GDBSERVER: set_register(%d, 0x%x)\n", reg, value);
		switch(reg) {
		case SR:
			regs.sr = value & 0xFFFF;
			MakeFromSR();
			break;
		case PC:
			m68k_setpc(value);
			break;
		case D0: case D1: case D2: case D3: case D4: case D5: case D6: case D7:
			m68k_dreg(regs, reg - D0) = value;
			break;
		case A0: case A1: case A2: case A3: case A4: case A5: case A6: case A7:
			m68k_areg(regs, reg - A0) = value;
			break;
		default:
			return false;
		}
		return true;
	}

	// MCP-WINUAE-EMU EXTENSION: Write all registers from hex string
	static bool set_registers(const std::string& hex_data) {
		if(hex_data.length() < 18 * 8)
			return false;
		for(int reg = 0; reg < 18; reg++) {
			uint32_t value = strtoul(hex_data.substr(reg * 8, 8).c_str(), nullptr, 16);
			if(!set_register(reg, value))
				return false;
		}
		return true;
	}

	void print_breakpoints() {
		barto_log("GDBSERVER: Breakpoints:\n");
		for(auto& bpn : bpnodes) {
			if(bpn.enabled) {
				barto_log("GDBSERVER: - %d, 0x%x, 0x%x\n", bpn.type, bpn.value1, bpn.value2);
			}
		}
	}

	void print_watchpoints() {
		barto_log("GDBSERVER: Watchpoints:\n");
		for(auto& mwn : mwnodes) {
			if(mwn.size) {
				barto_log("GDBSERVER: - 0x%x, 0x%x\n", mwn.addr, mwn.size);
			}
		}
	}

	void send_ack(const std::string& ack) {
		if(useAck && !ack.empty()) {
			barto_log("GDBSERVER: <- %s\n", ack.c_str());
			int result = send(gdbconn, ack.data(), (int)ack.length(), 0);
			if(result == SOCKET_ERROR)
				barto_log(_T("GDBSERVER: error sending ack: %d\n"), WSAGetLastError());
		}
	}

	void send_response(std::string response) {
		tracker _;
		if(!response.empty()) {
			barto_log("GDBSERVER: <- %s\n", response.substr(1).c_str());
			uint8_t cksum{};
			for(size_t i = 1; i < response.length(); i++)
				cksum += response[i];
			response += '#';
			response += hex[cksum >> 4];
			response += hex[cksum & 0xf];
			int result = send(gdbconn, response.data(), (int)response.length(), 0);
			if(result == SOCKET_ERROR)
				barto_log(_T("GDBSERVER: error sending data: %d\n"), WSAGetLastError());
		}
	}

	void handle_packet() {
		tracker _;
		if(data_available()) {
			char buf[512];
			auto result = recv(gdbconn, buf, sizeof(buf) - 1, 0);
			if(result > 0) {
				buf[result] = '\0';
				barto_log("GDBSERVER: received %d bytes: >>%s<<\n", result, buf);
				std::string request{ buf }, ack{}, response;
				while(!request.empty() && (request[0] == '+' || request[0] == '-')) {
					if(request[0] == '+') {
						request = request.substr(1);
					} else if(request[0] == '-') {
						barto_log("GDBSERVER: client non-ack'd our last packet\n");
						request = request.substr(1);
					}
				}
				if(!request.empty() && request[0] == 0x03) {
					// Ctrl+C
					ack = "+";
					response = "$";
					response += "S05"; // SIGTRAP
					debugger_state = state::debugging;
					activate_debugger();
				} else if(!request.empty() && request[0] == '$') {
					ack = "-";
					auto end = request.find('#');
					if(end != std::string::npos) {
						uint8_t cksum{};
						for(size_t i = 1; i < end; i++)
							cksum += request[i];
						if(request.length() >= end + 2) {
							if(tolower(request[end + 1]) == hex[cksum >> 4] && tolower(request[end + 2]) == hex[cksum & 0xf]) {
								request = request.substr(1, end - 1);
								barto_log("GDBSERVER: -> %s\n", request.c_str());
								ack = "+";
								response = "$";
								if(request.substr(0, strlen("qSupported")) == "qSupported") {
									response += "PacketSize=512;BreakpointCommands+;swbreak+;hwbreak+;QStartNoAckMode+;vContSupported+;";
								} else if(request.substr(0, strlen("qAttached")) == "qAttached") {
									response += "1";
								} else if(request.substr(0, strlen("qTStatus")) == "qTStatus") {
									response += "T0";
								} else if(request.substr(0, strlen("QStartNoAckMode")) == "QStartNoAckMode") {
									send_ack(ack);
									useAck = false;
									response += "OK";
								} else if(request.substr(0, strlen("qfThreadInfo")) == "qfThreadInfo") {
									response += "m1";
								} else if(request.substr(0, strlen("qsThreadInfo")) == "qsThreadInfo") {
									response += "l";
								} else if(request.substr(0, strlen("qC")) == "qC") {
									response += "QC1";
								} else if(request.substr(0, strlen("qOffsets")) == "qOffsets") {
									// FIX: If baseText was already detected by AUTODETECT, use it directly
									// This is critical because the client may call qOffsets after stopping
									// at a breakpoint, and we need to return the detected offset
									if(baseText > 0 && sizeText > 0) {
										barto_log("GDBSERVER: qOffsets - using AUTODETECTED baseText=0x%x, sizeText=0x%x (%d sections)\n",
											baseText, sizeText, (int)sections.size());
										// Return sections if available, otherwise just baseText
										if(!sections.empty()) {
											response = "$";
											for(size_t i = 0; i < sections.size(); i++) {
												if(i > 0) response += ";";
												response += hex32(sections[i]);
											}
										} else {
											// Single section case (most common for AUTODETECT)
											response = "$" + hex32(baseText);
										}
									} else {
									// Fall through to process search if no AUTODETECT data
									auto BADDR = [](auto bptr) { return bptr << 2; };
									auto BSTR = [](auto bstr) { return std::string(reinterpret_cast<char*>(bstr) + 1, bstr[0]); };
									auto execbase = get_long_debug(4);
									
									// FIX: Search for the target process by name instead of using ThisTask
									// ThisTask might be any system process (e.g., CON, Workbench) when qOffsets is called
									uaecptr TargetProcess = 0;
									uaecptr cliSegList = 0; // Set by find_cli_with_module if found
									const char* search_name = processname ? processname : (saved_processname.empty() ? nullptr : saved_processname.c_str());
									
									barto_log("GDBSERVER: qOffsets - searching for process (processname='%s', saved='%s')\n",
										processname ? processname : "(null)", 
										saved_processname.empty() ? "(empty)" : saved_processname.c_str());
									
									if(search_name && search_name[0]) {
										// First try to find process by exact name
										TargetProcess = find_process_by_name(search_name);
										
										// If not found, try to find a CLI with that module loaded
										if(!TargetProcess) {
											barto_log("GDBSERVER: qOffsets - process not found, trying CLI module search\n");
											TargetProcess = find_cli_with_module(search_name, &cliSegList);
										}
									}
									
									// Fallback to ThisTask if process not found by name
									if(!TargetProcess) {
										TargetProcess = get_long_debug(execbase + 276);
										barto_log("GDBSERVER: qOffsets - using ThisTask fallback: 0x%x\n", TargetProcess);
									}
									
									response += "E01";
									if(TargetProcess) {
										auto ln_Name = reinterpret_cast<char*>(get_real_address_debug(get_long_debug(TargetProcess + 10)));
										barto_log("GDBSERVER: qOffsets - selected process: '%s' at 0x%x\n", 
											ln_Name ? ln_Name : "(null)", TargetProcess);
										auto ln_Type = get_byte_debug(TargetProcess + 8);
										bool process = ln_Type == 13; // NT_PROCESS
										sections.clear();
										if(process) {
											constexpr auto sizeofLN = 14;
											auto tc_SPLower = get_long_debug(TargetProcess + sizeofLN + 44);
											auto tc_SPUpper = get_long_debug(TargetProcess + sizeofLN + 48) - 2;
											stackLower = tc_SPLower;
											stackUpper = tc_SPUpper;

											systemStackLower = get_long_debug(execbase + 58);
											systemStackUpper = get_long_debug(execbase + 54);
											
											// If cliSegList was found by find_cli_with_module, use it directly
											uaecptr segList = 0;
											if(cliSegList) {
												barto_log("GDBSERVER: qOffsets - using segList from find_cli_with_module: 0x%x\n", cliSegList);
												segList = cliSegList;
											} else {
												// Standard lookup from process structure
												auto pr_SegList = BADDR(get_long_debug(TargetProcess + 128));
												auto numSegLists = get_long_debug(pr_SegList + 0);
												segList = BADDR(get_long_debug(pr_SegList + 12));
												auto pr_CLI = BADDR(get_long_debug(TargetProcess + 172));
												int pr_TaskNum = get_long_debug(TargetProcess + 140);
												if(pr_CLI && pr_TaskNum) {
													auto cli_CommandName = BSTR(get_real_address_debug(BADDR(get_long_debug(pr_CLI + 16))));
													barto_log("GDBSERVER: qOffsets - CLI command: '%s'\n", cli_CommandName.c_str());
													segList = BADDR(get_long_debug(pr_CLI + 60));
													auto pr_StackSize = get_long_debug(TargetProcess + 132);
													stackUpper = m68k_areg(regs, A7 - A0);
													stackLower = stackUpper - pr_StackSize;
												}
											}
											baseText = 0;
											barto_log("GDBSERVER: qOffsets - scanning segments for process '%s':\n", 
												ln_Name ? ln_Name : "(null)");
											for(int i = 0; segList; i++) {
												auto size = get_long_debug(segList - 4) - 4;
												auto base = segList + 4;
												if(i == 0) {
													baseText = base;
													sizeText = size;
												}
												if(i == 0)
													response = "$";
												else
													response += ";";
												response += hex32(base);
												sections.push_back(base);
												barto_log("GDBSERVER:   hunk[%d]: base=0x%x, size=0x%x\n", i, base, size);
												segList = BADDR(get_long_debug(segList));
											}
											barto_log("GDBSERVER: qOffsets - RESULT: baseText=0x%x, sizeText=0x%x\n",
												baseText, sizeText);
											relocate_breakpoints();
										} else {
											barto_log("GDBSERVER: qOffsets - ERROR: selected task is not a process (type=%d)\n", ln_Type);
										}
									} else {
										barto_log("GDBSERVER: qOffsets - ERROR: no process found\n");
									}
									} // end of else block for process search
								} else if(request.substr(0, strlen("qRcmd,")) == "qRcmd,") {
									// "monitor" command. used for profiling
									auto cmd = from_hex(request.substr(strlen("qRcmd,")));
									barto_log("GDBSERVER:   monitor %s\n", cmd.c_str());
									// syntax: monitor profile <num_frames> <unwind_file> <out_file>
									if(cmd.substr(0, strlen("profile")) == "profile") {
										auto s = cmd.substr(strlen("profile "));
										std::string profile_unwindname;
										profile_num_frames = 0;
										profile_outname.clear();

										// get num_frames
										while(s[0] >= '0' && s[0] <= '9') {
											profile_num_frames = profile_num_frames * 10 + s[0] - '0';
											s = s.substr(1);
										}
										profile_num_frames = max(1, min(100, profile_num_frames));
										s = s.substr(1); // skip space

										// get profile_unwindname
										if(s.substr(0, 1) == "\"") {
											auto last = s.find('\"', 1);
											if(last != std::string::npos) {
												profile_unwindname = s.substr(1, last - 1);
												s = s.substr(last + 1);
											} else {
												s.clear();
											}
										} else {
											auto last = s.find(' ', 1);
											if(last != std::string::npos) {
												profile_unwindname = s.substr(0, last);
												s = s.substr(last + 1);
											} else {
												s.clear();
											}
										}

										s = s.substr(1); // skip space

										// get profile_outname
										if(s.substr(0, 1) == "\"") {
											auto last = s.find('\"', 1);
											if(last != std::string::npos) {
												profile_outname = s.substr(1, last - 1);
												s = s.substr(last + 1);
											} else {
												s.clear();
											}
										} else {
											profile_outname = s.substr(1);
										}

										profile_unwind.reset();
										if(!profile_unwindname.empty()) {
											if(auto f = fopen(profile_unwindname.c_str(), "rb")) {
												profile_unwind = std::make_unique<cpu_profiler_unwind[]>(sizeText >> 1);
												fread(profile_unwind.get(), sizeof(cpu_profiler_unwind), sizeText >> 1, f);
												fclose(f);
											}
										}

										if(!profile_outname.empty()) {
											send_ack(ack);
											profile_frame_count = 0;
											debugger_state = state::profile;
											deactivate_debugger();
											return; // response is sent when profile is finished (vsync)
										}
									} else if(cmd.substr(0, strlen("screenshot")) == "screenshot") {
									// syntax: monitor screenshot <filepath>
									auto s = cmd.substr(strlen("screenshot"));
									// trim leading whitespace
									while(!s.empty() && s[0] == ' ')
										s = s.substr(1);
									if(s.empty()) {
										barto_log("GDBSERVER: screenshot: no path specified\n");
										response += "E01";
									} else {
										std::string filepath = s;
										// strip surrounding quotes if present
										if(filepath.size() >= 2 && filepath.front() == '"' && filepath.back() == '"')
											filepath = filepath.substr(1, filepath.size() - 2);

										barto_log("GDBSERVER: screenshot to '%s'\n", filepath.c_str());

										// render current frame and capture screenshot
										vsync_display_render();
										int monid = getfocusedmonitor();
										vidbuf_description* avidinfo = &adisplays[monid].gfxvidinfo;
										vidbuffer* vb = &avidinfo->drawbuffer;
										if(screenshot_prepare(monid, vb) == 1) {
											auto sbi = screenshot_get_bi();
											auto sbi_bits = (const uint8_t*)screenshot_get_bits();
											if(sbi && sbi_bits && sbi->bmiHeader.biBitCount == 24) {
												const auto w = sbi->bmiHeader.biWidth;
												const auto h = sbi->bmiHeader.biHeight;
												const auto pitch = sbi->bmiHeader.biSizeImage / sbi->bmiHeader.biHeight;
												// flip vertically and swap BGR -> RGB
												auto bits = std::make_unique<uint8_t[]>(w * 3 * h);
												for(int y = 0; y < h; y++) {
													for(int x = 0; x < w; x++) {
														bits[y * w * 3 + x * 3 + 0] = sbi_bits[(h - 1 - y) * pitch + x * 3 + 2];
														bits[y * w * 3 + x * 3 + 1] = sbi_bits[(h - 1 - y) * pitch + x * 3 + 1];
														bits[y * w * 3 + x * 3 + 2] = sbi_bits[(h - 1 - y) * pitch + x * 3 + 0];
													}
												}
												// write PNG to file using stb_image_write
												if(stbi_write_png(filepath.c_str(), w, h, 3, bits.get(), w * 3)) {
													barto_log("GDBSERVER: screenshot saved: %dx%d to '%s'\n", w, h, filepath.c_str());
													// send back dimensions as hex-encoded text
													char info[256];
													snprintf(info, sizeof(info), "OK %dx%d %s", w, h, filepath.c_str());
													response += to_hex(std::string(info));
												} else {
													barto_log("GDBSERVER: screenshot write failed: '%s'\n", filepath.c_str());
													response += "E03";
												}
											} else {
												barto_log("GDBSERVER: screenshot: unsupported format (bpp=%d)\n",
													sbi ? sbi->bmiHeader.biBitCount : 0);
												response += "E02";
											}
										} else {
											barto_log("GDBSERVER: screenshot_prepare failed\n");
											response += "E02";
										}
									}
								} else if(cmd.substr(0, strlen("disasm")) == "disasm") {
									// syntax: monitor disasm <addr> [<count>]
									auto s = cmd.substr(strlen("disasm"));
									while(!s.empty() && s[0] == ' ')
										s = s.substr(1);
									if(s.empty()) {
										barto_log("GDBSERVER: disasm: no address specified\n");
										response += "E01";
									} else {
										uaecptr addr = strtoul(s.c_str(), nullptr, 16);
										int count = 10; // default
										auto space = s.find(' ');
										if(space != std::string::npos)
											count = max(1, min(100, atoi(s.c_str() + space + 1)));

										barto_log("GDBSERVER: disasm at 0x%x, count %d\n", addr, count);
										std::string output;
										TCHAR instrname[256], instrcode[256];
										for(int i = 0; i < count; i++) {
											uaecptr nextpc;
											instrname[0] = 0;
											instrcode[0] = 0;
											sm68k_disasm(instrname, instrcode, addr, &nextpc, 0xffffffff);
											char line[1024];
											auto instrname_utf8 = string_to_utf8(instrname);
											auto instrcode_utf8 = string_to_utf8(instrcode);
											snprintf(line, sizeof(line), "%08x : %-20s %s\n",
												addr, instrcode_utf8.c_str(), instrname_utf8.c_str());
											output += line;
											addr = nextpc;
										}
										response += to_hex(output);
									}
								} else if(cmd == "reset" && currprefs.debugging_trigger[0]) {
										savestate_quick(0, 0); // restore state saved at process entry
										barto_debug_resources_count = 0;
										response += "OK";
									} else if(cmd.substr(0, strlen("input event ")) == "input event ") {
										// syntax: monitor input event <event_id> [state]
										// event_id: WinUAE input event ID from config (e.g. keyboard scancode mapping)
										// state: 1=press, 0=release, 2=toggle (default 1)
										auto s = cmd.substr(strlen("input event "));
										int nr = atoi(s.c_str());
										int state = 1;
										auto space = s.find(' ');
										if(space != std::string::npos)
											state = atoi(s.c_str() + space + 1);
										send_input_event(nr, state, 1, 0);
										response += "OK";
									} else if(cmd.substr(0, strlen("input key ")) == "input key ") {
										// syntax: monitor input key <scancode> <1|0>
										// Amiga raw scancode 0x00-0x7F. Event ID = 256 + scancode (common default).
										auto sk = cmd.substr(strlen("input key "));
										int scancode = strtol(sk.c_str(), nullptr, 0);
										int kstate = 1;
										auto sp2 = sk.find(' ');
										if(sp2 != std::string::npos)
											kstate = atoi(sk.c_str() + sp2 + 1);
										const int evt = 256 + (scancode & 0x7F);
										send_input_event(evt, kstate, 1, 0);
										response += "OK";
									} else if(cmd.substr(0, strlen("input joy ")) == "input joy ") {
										// syntax: monitor input joy <port> <left|right|up|down|fire|2nd|3rd> <1|0>
										// port: 0=joystick 1, 1=joystick 2
										auto sj = cmd.substr(strlen("input joy "));
										int port = atoi(sj.c_str());
										auto sp1 = sj.find(' ');
										if(sp1 == std::string::npos) { response += "E01"; } else {
											auto rest = sj.substr(sp1 + 1);
											auto sp2 = rest.find(' ');
											std::string dir = sp2 != std::string::npos ? rest.substr(0, sp2) : rest;
											int state = (sp2 != std::string::npos) ? atoi(rest.c_str() + sp2 + 1) : 1;
											int evt = -1;
											if(port == 0) {
												if(dir == "left") evt = INPUTEVENT_JOY1_LEFT; else if(dir == "right") evt = INPUTEVENT_JOY1_RIGHT;
												else if(dir == "up") evt = INPUTEVENT_JOY1_UP; else if(dir == "down") evt = INPUTEVENT_JOY1_DOWN;
												else if(dir == "fire" || dir == "b1") evt = INPUTEVENT_JOY1_FIRE_BUTTON;
												else if(dir == "2nd" || dir == "b2") evt = INPUTEVENT_JOY1_2ND_BUTTON; else if(dir == "3rd" || dir == "b3") evt = INPUTEVENT_JOY1_3RD_BUTTON;
											} else if(port == 1) {
												if(dir == "left") evt = INPUTEVENT_JOY2_LEFT; else if(dir == "right") evt = INPUTEVENT_JOY2_RIGHT;
												else if(dir == "up") evt = INPUTEVENT_JOY2_UP; else if(dir == "down") evt = INPUTEVENT_JOY2_DOWN;
												else if(dir == "fire" || dir == "b1") evt = INPUTEVENT_JOY2_FIRE_BUTTON;
												else if(dir == "2nd" || dir == "b2") evt = INPUTEVENT_JOY2_2ND_BUTTON; else if(dir == "3rd" || dir == "b3") evt = INPUTEVENT_JOY2_3RD_BUTTON;
											}
											if(evt >= 0) { send_input_event(evt, state, 1, 0); response += "OK"; } else { response += "E01"; }
										}
									} else if(cmd.substr(0, strlen("input mouse move ")) == "input mouse move ") {
										// syntax: monitor input mouse move <dx> <dy>
										// Relative mouse movement (deltas)
										auto sm = cmd.substr(strlen("input mouse move "));
										int dx = atoi(sm.c_str());
										auto sp = sm.find(' ');
										int dy = sp != std::string::npos ? atoi(sm.c_str() + sp + 1) : 0;
										setmousestate(0, 0, dx, 0);
										setmousestate(0, 1, dy, 0);
										response += "OK";
									} else if(cmd.substr(0, strlen("input mouse abs ")) == "input mouse abs ") {
										// syntax: monitor input mouse abs <x> <y>
										auto sa = cmd.substr(strlen("input mouse abs "));
										int x = atoi(sa.c_str());
										auto sp = sa.find(' ');
										int y = sp != std::string::npos ? atoi(sa.c_str() + sp + 1) : 0;
										setmousestate(0, 0, x, 1);
										setmousestate(0, 1, y, 1);
										response += "OK";
									} else if(cmd.substr(0, strlen("input mouse button ")) == "input mouse button ") {
										// syntax: monitor input mouse button <0|1|2> <1|0>
										// 0=left, 1=right, 2=middle
										auto sb = cmd.substr(strlen("input mouse button "));
										int btn = atoi(sb.c_str());
										int bstate = 1;
										auto sp = sb.find(' ');
										if(sp != std::string::npos) bstate = atoi(sb.c_str() + sp + 1);
										if(btn >= 0 && btn <= 2) {
											setmousebuttonstate(0, btn, bstate);
											response += "OK";
										} else { response += "E01"; }
									} else if(cmd.size() >= 4 && cmd.substr(0, 3) == "df0" && cmd[3] == ' ') {
										// syntax: monitor df0 insert <path> | monitor df0 eject
										// MCP-WINUAE-EMU: paths can be quoted to handle spaces
										auto rest = cmd.substr(4);
										while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
										if(rest.substr(0, 7) == "insert ") {
											std::string path_utf8 = rest.substr(7);
											while(!path_utf8.empty() && path_utf8[0] == ' ') path_utf8 = path_utf8.substr(1);
											// Strip quotes if present
											if(path_utf8.size() >= 2 && path_utf8.front() == '"' && path_utf8.back() == '"')
												path_utf8 = path_utf8.substr(1, path_utf8.size() - 2);
											std::wstring path_w = utf8_to_wide(path_utf8);
											barto_log("GDBSERVER: df0 insert '%s'\n", path_utf8.c_str());
											if(!path_w.empty()) {
												disk_insert(0, path_w.c_str());
												response += "OK";
											} else { response += "E01"; }
										} else if(rest == "eject") {
											disk_eject(0);
											response += "OK";
										} else { response += "E01"; }
									} else if(cmd.size() >= 4 && cmd.substr(0, 3) == "df1" && cmd[3] == ' ') {
										auto rest = cmd.substr(4);
										while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
										if(rest.substr(0, 7) == "insert ") {
											std::string path_utf8 = rest.substr(7);
											while(!path_utf8.empty() && path_utf8[0] == ' ') path_utf8 = path_utf8.substr(1);
											if(path_utf8.size() >= 2 && path_utf8.front() == '"' && path_utf8.back() == '"')
												path_utf8 = path_utf8.substr(1, path_utf8.size() - 2);
											std::wstring path_w = utf8_to_wide(path_utf8);
											if(!path_w.empty()) {
												disk_insert(1, path_w.c_str());
												response += "OK";
											} else { response += "E01"; }
										} else if(rest == "eject") {
											disk_eject(1);
											response += "OK";
										} else { response += "E01"; }
									} else if(cmd.size() >= 4 && cmd.substr(0, 3) == "df2" && cmd[3] == ' ') {
										auto rest = cmd.substr(4);
										while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
										if(rest.substr(0, 7) == "insert ") {
											std::string path_utf8 = rest.substr(7);
											while(!path_utf8.empty() && path_utf8[0] == ' ') path_utf8 = path_utf8.substr(1);
											if(path_utf8.size() >= 2 && path_utf8.front() == '"' && path_utf8.back() == '"')
												path_utf8 = path_utf8.substr(1, path_utf8.size() - 2);
											std::wstring path_w = utf8_to_wide(path_utf8);
											if(!path_w.empty()) {
												disk_insert(2, path_w.c_str());
												response += "OK";
											} else { response += "E01"; }
										} else if(rest == "eject") {
											disk_eject(2);
											response += "OK";
										} else { response += "E01"; }
									} else if(cmd.size() >= 4 && cmd.substr(0, 3) == "df3" && cmd[3] == ' ') {
										auto rest = cmd.substr(4);
										while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
										if(rest.substr(0, 7) == "insert ") {
											std::string path_utf8 = rest.substr(7);
											while(!path_utf8.empty() && path_utf8[0] == ' ') path_utf8 = path_utf8.substr(1);
											if(path_utf8.size() >= 2 && path_utf8.front() == '"' && path_utf8.back() == '"')
												path_utf8 = path_utf8.substr(1, path_utf8.size() - 2);
											std::wstring path_w = utf8_to_wide(path_utf8);
											if(!path_w.empty()) {
												disk_insert(3, path_w.c_str());
												response += "OK";
											} else { response += "E01"; }
										} else if(rest == "eject") {
											disk_eject(3);
											response += "OK";
										} else { response += "E01"; }
								} else if(cmd == "offset" || cmd.substr(0, strlen("offset ")) == "offset ") {
									// syntax: monitor offset [set <address>]
									// Without args: Returns Text=<baseText>;Data=<baseData>;Bss=<baseBss>;LoadOffset=<loadOffset>;SizeText=<sizeText>
									// With "set <addr>": Sets baseText manually and relocates breakpoints
									auto rest = cmd.length() > 7 ? cmd.substr(7) : "";
									while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
									
									if(rest.substr(0, 4) == "set ") {
										// Parse and set baseText manually
										auto addr_str = rest.substr(4);
										while(!addr_str.empty() && addr_str[0] == ' ') addr_str = addr_str.substr(1);
										uaecptr new_baseText = strtoul(addr_str.c_str(), nullptr, 16);
										barto_log("OFFSET: Manual set baseText=0x%x (was 0x%x)\n", new_baseText, baseText);
										baseText = new_baseText;
										sizeText = 0x100000; // Assume 1MB max
										// Now relocate any pending breakpoints
										relocate_breakpoints();
										response += "OK";
									} else {
										// Read mode
										char info[256];
										uaecptr loadOff = (baseText != 0) ? (baseText - ELF_TEXT_BASE) : 0;
										// Find data and bss sections from sections vector
										uaecptr dataBase = 0, bssBase = 0;
										if(sections.size() >= 2) dataBase = sections[1];
										if(sections.size() >= 3) bssBase = sections[2];
										snprintf(info, sizeof(info), 
											"Text=%x;Data=%x;Bss=%x;LoadOffset=%x;SizeText=%x",
											baseText, dataBase, bssBase, loadOff, sizeText);
										barto_log("GDBSERVER: monitor offset -> %s\n", info);
										response += to_hex(std::string(info));
									}
								} else if(cmd.substr(0, strlen("logfile")) == "logfile") {
									// DEBUGGING: Enable logging to file
									// syntax: monitor logfile <path> - start logging to file
									// syntax: monitor logfile off - stop logging
									// syntax: monitor logfile - show current status
									auto rest = cmd.length() > 7 ? cmd.substr(7) : "";
									while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
									
									if(rest.empty()) {
										// Status
										std::string status = log_file ? ("Logging to: " + log_file_path) : "Logging disabled";
										response += to_hex(status);
									} else if(rest == "off") {
										if(log_file) {
											barto_log("LOGFILE: Closing log file\n");
											fclose(log_file);
											log_file = nullptr;
											log_file_path.clear();
										}
										response += "OK";
									} else {
										// Open new log file
										if(log_file) {
											fclose(log_file);
											log_file = nullptr;
										}
										log_file_path = rest;
										log_file = fopen(log_file_path.c_str(), "w");
										if(log_file) {
											barto_log("LOGFILE: Opened '%s' for logging\n", log_file_path.c_str());
											response += "OK";
										} else {
											log_file_path.clear();
											response += "E01";
										}
									}
								} else if(cmd == "findproc" || cmd.substr(0, strlen("findproc ")) == "findproc ") {
									// Re-scan process list and update baseText
									// syntax: monitor findproc [name] - scan for process and update baseText
									// Also searches for CLI processes with matching module name
									auto rest = cmd.length() > 8 ? cmd.substr(8) : "";
									while(!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
									
									const char* search_name = !rest.empty() ? rest.c_str() :
										(processname ? processname : (saved_processname.empty() ? nullptr : saved_processname.c_str()));
									
									if(!search_name || !search_name[0]) {
										response += to_hex(std::string("No process name to search"));
									} else {
										barto_log("FINDPROC: monitor findproc searching for '%s'\n", search_name);
										auto BADDR = [](auto bptr) -> uaecptr { return bptr << 2; };
										
										// First try to find by process name
										uaecptr proc = find_process_by_name(search_name);
										uaecptr segList = 0;
										
										if(proc) {
											// Found the process, get its SegList
											auto pr_SegList = BADDR(get_long_debug(proc + 0x80));
											auto pr_CLI = get_long_debug(proc + 0xAC);
											
											if(pr_CLI) {
												auto cli = BADDR(pr_CLI);
												auto cli_Module = get_long_debug(cli + 0x3C);
												if(cli_Module) segList = BADDR(cli_Module);
											}
											if(!segList && pr_SegList) {
												segList = pr_SegList;
											}
										} else {
											// Not found by name, try finding CLI with that module
											barto_log("FINDPROC: process not found, trying CLI module search\n");
											proc = find_cli_with_module(search_name, &segList);
										}
										
										if(proc && segList) {
											baseText = segList + 4;
											sizeText = get_long_debug(segList - 4) - 4;
											char info[512];
											snprintf(info, sizeof(info), 
												"Found module '%s' at proc=0x%x, segList=0x%x, baseText=0x%x, size=0x%x",
												search_name, proc, segList, baseText, sizeText);
											barto_log("FINDPROC: %s\n", info);
											relocate_breakpoints();
											response += to_hex(std::string(info));
										} else if(proc) {
											response += to_hex(std::string("Process found but no SegList"));
										} else {
											// List all processes and CLIs for debugging
											std::string output = "Process/Module not found. Current processes:\n";
											auto execbase = get_long_debug(4);
											auto iterate_list = [&](uaecptr list_head, const char* list_name) {
												auto node = get_long_debug(list_head);
												while(node) {
													auto ln_Succ = get_long_debug(node);
													if(!ln_Succ) break;
													auto ln_Type = get_byte_debug(node + 8);
													if(ln_Type == 13) { // NT_PROCESS
														auto ln_Name_ptr = get_long_debug(node + 10);
														auto ln_Name = ln_Name_ptr ? reinterpret_cast<char*>(get_real_address_debug(ln_Name_ptr)) : nullptr;
														char line[256];
														
														// Check for CLI info
														auto pr_CLI = get_long_debug(node + 0xAC);
														if(pr_CLI) {
															auto cli = BADDR(pr_CLI);
															auto cli_CommandName = BADDR(get_long_debug(cli + 0x10));
															char cmd_name[64] = {0};
															if(cli_CommandName) {
																auto len = get_byte_debug(cli_CommandName);
																for(int i = 0; i < len && i < 63; i++) {
																	cmd_name[i] = get_byte_debug(cli_CommandName + 1 + i);
																}
															}
															auto cli_Module = get_long_debug(cli + 0x3C);
															snprintf(line, sizeof(line), "  %s: '%s' at 0x%x (CLI cmd='%s', module=0x%x)\n", 
																list_name, ln_Name ? ln_Name : "?", node, cmd_name, cli_Module ? BADDR(cli_Module) : 0);
														} else {
															snprintf(line, sizeof(line), "  %s: '%s' at 0x%x\n", 
																list_name, ln_Name ? ln_Name : "?", node);
														}
														output += line;
													}
													node = ln_Succ;
												}
											};
											auto thisTask = get_long_debug(execbase + 276);
											if(thisTask) {
												auto ln_Type = get_byte_debug(thisTask + 8);
												if(ln_Type == 13) {
													auto ln_Name_ptr = get_long_debug(thisTask + 10);
													auto ln_Name = ln_Name_ptr ? reinterpret_cast<char*>(get_real_address_debug(ln_Name_ptr)) : nullptr;
													char line[128];
													snprintf(line, sizeof(line), "  ThisTask: '%s' at 0x%x\n", ln_Name ? ln_Name : "?", thisTask);
													output += line;
												}
											}
											iterate_list(execbase + 406, "TaskReady");
											iterate_list(execbase + 420, "TaskWait");
											response += to_hex(output);
										}
									}
								} else if(cmd == "breakpoints" || cmd == "bp") {
									// DEBUGGING: List all active breakpoints with their addresses
									// syntax: monitor breakpoints  (or: monitor bp)
									std::string output = "Breakpoints:\n";
									output += "  baseText=0x" + to_hex_addr(baseText) + "\n";
									output += "  ELF_TEXT_BASE=0x" + to_hex_addr(ELF_TEXT_BASE) + "\n";
									output += "  loadOffset=0x" + to_hex_addr(baseText >= ELF_TEXT_BASE ? (baseText - ELF_TEXT_BASE) : 0) + "\n";
									output += "  pending_elf_addresses=" + std::to_string(breakpoint_elf_addresses.size()) + "\n";
									int bp_count = 0;
									for(const auto& bpn : bpnodes) {
										if(bpn.enabled && bpn.type == BREAKPOINT_REG_PC) {
											char line[128];
											snprintf(line, sizeof(line), "  BP[%d]: addr=0x%08x\n", bp_count++, bpn.value1);
											output += line;
										}
									}
									if(bp_count == 0) output += "  (no active breakpoints)\n";
									// Also show pending ELF addresses
									for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
										char line[128];
										snprintf(line, sizeof(line), "  PENDING_ELF[%d]: 0x%08x\n", (int)i, breakpoint_elf_addresses[i]);
										output += line;
									}
									barto_log("GDBSERVER: monitor breakpoints\n%s", output.c_str());
									response += to_hex(output);
								} else if(cmd.substr(0, strlen("findcode")) == "findcode") {
									// Search for a hex pattern in memory to find where program is loaded
									// syntax: monitor findcode <hex_bytes> [start_addr] [end_addr]
									// Example: monitor findcode 4fefffe048e7 c00000 d00000
									auto args = cmd.substr(strlen("findcode"));
									while(!args.empty() && args[0] == ' ') args = args.substr(1);
									
									// Parse hex pattern
									std::vector<uint8_t> pattern;
									std::string hex_str;
									size_t space1 = args.find(' ');
									if(space1 != std::string::npos) {
										hex_str = args.substr(0, space1);
										args = args.substr(space1 + 1);
									} else {
										hex_str = args;
										args.clear();
									}
									
									for(size_t i = 0; i + 1 < hex_str.length(); i += 2) {
										pattern.push_back((uint8_t)strtoul(hex_str.substr(i, 2).c_str(), nullptr, 16));
									}
									
									if(pattern.empty()) {
										response += to_hex(std::string("Usage: monitor findcode <hex_pattern> [start] [end]\n"));
									} else {
										// Parse optional start/end addresses
										uaecptr start_addr = 0x400;     // Skip zero page
										uaecptr end_addr = 0x200000;    // Chip RAM by default
										
										while(!args.empty() && args[0] == ' ') args = args.substr(1);
										if(!args.empty()) {
											size_t space2 = args.find(' ');
											if(space2 != std::string::npos) {
												start_addr = strtoul(args.substr(0, space2).c_str(), nullptr, 16);
												end_addr = strtoul(args.substr(space2 + 1).c_str(), nullptr, 16);
											} else {
												start_addr = strtoul(args.c_str(), nullptr, 16);
											}
										}
										
										barto_log("FINDCODE: Searching for %d-byte pattern in 0x%x-0x%x\n",
											(int)pattern.size(), start_addr, end_addr);
										
										std::string output = "Searching for pattern...\n";
										int found_count = 0;
										
										for(uaecptr addr = start_addr; addr < end_addr - pattern.size() && found_count < 10; addr++) {
											bool match = true;
											for(size_t i = 0; i < pattern.size() && match; i++) {
												if(get_byte_debug(addr + i) != pattern[i]) {
													match = false;
												}
											}
											if(match) {
												char line[64];
												snprintf(line, sizeof(line), "  Found at 0x%08x\n", addr);
												output += line;
												barto_log("FINDCODE: Found at 0x%x\n", addr);
												found_count++;
												addr += pattern.size() - 1; // Skip past this match
											}
										}
										
										if(found_count == 0) {
											output += "  (no matches found)\n";
										} else {
											char line[64];
											snprintf(line, sizeof(line), "Total: %d matches\n", found_count);
											output += line;
										}
										response += to_hex(output);
									}
								} else if(cmd.substr(0, strlen("warp")) == "warp") {
									// MCP-WINUAE-EMU EXTENSION: Warp/turbo mode control
									// syntax: monitor warp <0|1|on|off|status>
										auto s = cmd.substr(strlen("warp"));
										while(!s.empty() && s[0] == ' ') s = s.substr(1);
										if(s.empty() || s == "status") {
											char info[64];
											snprintf(info, sizeof(info), "warp=%d", currprefs.turbo_emulation ? 1 : 0);
											response += to_hex(std::string(info));
										} else if(s == "1" || s == "on") {
											warpmode(1);
											response += "OK";
										} else if(s == "0" || s == "off") {
											warpmode(0);
											response += "OK";
										} else {
											response += "E01";
										}
									} else {
										// unknown monitor command
										response += "E01";
									}
								} else if(request.substr(0, strlen("vCont?")) == "vCont?") {
									response += "vCont;c;C;s;S;t;r";
								} else if(request.substr(0, strlen("vCont;")) == "vCont;") {
									auto actions = request.substr(strlen("vCont;"));
									while(!actions.empty()) {
										std::string action;
										// split actions by ';'
										auto semi = actions.find(';');
										if(semi != std::string::npos) {
											action = actions.substr(0, semi);
											actions = actions.substr(semi + 1);
										} else {
											action = actions;
											actions.clear();
										}
										// thread specified by ':'
										auto colon = action.find(':');
										if(colon != std::string::npos) {
											// ignore thread ID
											action = action.substr(0, colon);
										}

										// hmm.. what to do with multiple actions?!

										if(action == "s") { // single-step
											// step over - GDB does this in a different way
											//auto pc = M68K_GETPC;
											//decltype(pc) nextpc;
											//m68k_disasm(pc, &nextpc, pc, 1);
											//trace_mode = TRACE_MATCH_PC;
											//trace_param1 = nextpc;

											// step in
											trace_param[0] = 1;
											trace_mode = TRACE_SKIP_INS;

											exception_debugging = 1;
											debugger_state = state::connected;
											
											// MCP-WINUAE-EMU FIX: Must deactivate debugger to let emulation continue
											// but preserve exception_debugging for trace to work
											deactivate_debugger_preserve_processname();
											exception_debugging = 1;
											
											send_ack(ack);
											return;
										} else if(action == "c") { // continue
											debugger_state = state::connected;
											
											// Enable breakpoint checking if there are active breakpoints
											int bp_count = 0;
											for(const auto& bpn : bpnodes) {
												if(bpn.enabled && bpn.type == BREAKPOINT_REG_PC) {
													bp_count++;
												}
											}
											
											deactivate_debugger_preserve_processname();
											
											if(bp_count > 0) {
												// Re-enable trace mode and debugging after deactivate cleared them
												trace_mode = TRACE_CHECKONLY;
												debugging = -1;
												set_special(SPCFLAG_BRK);
												barto_log("GDBSERVER: vCont;c - enabling TRACE_CHECKONLY for %d breakpoints, debugging=%d\n", bp_count, debugging);
											}
											
											send_ack(ack);
											return;
										} else if(action[0] == 'r') { // keep stepping in range
											auto comma = action.find(',', 3);
											if(comma != std::string::npos) {
												uaecptr start = strtoul(action.data() + 1, nullptr, 16);
												uaecptr end = strtoul(action.data() + comma + 1, nullptr, 16);
												trace_mode = TRACE_NRANGE_PC;
												trace_param[0] = start;
												trace_param[1] = end;
												debugger_state = state::connected;
												
												// MCP-WINUAE-EMU FIX: Must deactivate debugger to let emulation continue
												deactivate_debugger_preserve_processname();
												exception_debugging = 1;
												
												send_ack(ack);
												return;
											}
										} else {
											barto_log("GDBSERVER: unknown vCont action: %s\n", action.c_str());
										}
									}
								} else if(request[0] == 'H') {
									response += "OK";
								} else if(request[0] == 'T') {
									response += "OK";
/*								} else if(request.substr(0, strlen("vRun")) == "vRun") {
									debugger_state = state::wait_for_process;
									activate_debugger();
									send_ack(ack);
									return;
*/								} else if(request[0] == 'D') { // detach
									response += "OK";
/*								} else if(request[0] == '!') { // enable extended mode
									response += "OK";
*/								} else if(request[0] == '?') { // reason for stopping
									response += "S05"; // SIGTRAP
								} else if(request[0] == 's') { // single-step
									assert(!"should have used vCont;s");
								} else if(request[0] == 'c') { // continue
									assert(!"should have used vCont;c");
								} else if(request[0] == 'k') { // kill
									uae_quit();
									deactivate_debugger();
									return;
								} else if(request.substr(0, 2) == "Z0") { // set software breakpoint
									auto comma = request.find(',', strlen("Z0"));
									if(comma != std::string::npos) {
										uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
										barto_log("Z0: Received breakpoint request: addr=0x%x, baseText=0x%x, sizeText=0x%x\n", adr, baseText, sizeText);
										
										// Determine if address needs relocation:
										// 1. If address is already in loaded program range (baseText..baseText+sizeText) -> use directly
										// 2. If address is in ELF range (0x400..0x100400) -> relocate by adding loadOffset
										// 3. Otherwise -> use directly (raw Amiga address)
										uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
										uaecptr relocatedAdr = adr;
										
										bool isInLoadedRange = (baseText > 0 && sizeText > 0 && 
											adr >= baseText && adr < baseText + sizeText);
										bool isInElfRange = (adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000);
										
										if(isInLoadedRange) {
											// Address is already in the loaded program's memory range - use as-is
											barto_log("Z0: Address 0x%x is in loaded range [0x%x-0x%x], using directly\n", 
												adr, baseText, baseText + sizeText);
											relocatedAdr = adr;
										} else if(loadOffset > 0 && isInElfRange) {
											// ELF address - relocate
											relocatedAdr = adr + loadOffset;
											barto_log("Z0: RELOCATED 0x%x -> 0x%x (loadOffset=0x%x)\n", adr, relocatedAdr, loadOffset);
										} else {
											// Raw address or unknown - use as-is
											barto_log("Z0: Using address 0x%x directly (loadOffset=0x%x)\n", adr, loadOffset);
										}
										if(adr == 0xffffffff) {
											// step out of kickstart
											trace_mode = TRACE_RANGE_PC;
											trace_param[0] = 0;
											trace_param[1] = 0xF80000;
											response += "OK";
										} else {
											// Store ELF address for potential deferred relocation
											breakpoint_elf_addresses.push_back(adr);
											for(auto& bpn : bpnodes) {
												if(bpn.enabled)
													continue;
											// Store original ELF address if no relocation yet
											// Will be relocated later when qOffsets provides baseText
											bpn.value1 = relocatedAdr;
											bpn.type = BREAKPOINT_REG_PC;
											bpn.oper = BREAKPOINT_CMP_EQUAL;
											bpn.enabled = 1;
											trace_mode = TRACE_CHECKONLY; // Enable breakpoint checking
											barto_log("Z0: BP set at 0x%x, trace_mode=TRACE_CHECKONLY\n", relocatedAdr);
											print_breakpoints();
											response += "OK";
											break;
											}
											// TODO: error when too many breakpoints!
										}
									} else
										response += "E01";
								} else if(request.substr(0, 2) == "z0") { // clear software breakpoint
									auto comma = request.find(',', strlen("z0"));
									if(comma != std::string::npos) {
										uaecptr adr = strtoul(request.data() + strlen("z0,"), nullptr, 16);
										// Apply same relocation logic as Z0
										uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
										uaecptr relocatedAdr = adr;
										
										bool isInLoadedRange = (baseText > 0 && sizeText > 0 && 
											adr >= baseText && adr < baseText + sizeText);
										bool isInElfRange = (adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000);
										
										if(isInLoadedRange) {
											relocatedAdr = adr;
										} else if(loadOffset > 0 && isInElfRange) {
											relocatedAdr = adr + loadOffset;
										}
										if(adr == 0xffffffff) {
											response += "OK";
										} else {
											for(auto& bpn : bpnodes) {
												if(bpn.enabled && bpn.value1 == relocatedAdr) {
													bpn.enabled = 0;
													trace_mode = 0;
													for(const auto& bp : bpnodes)
														if(bp.enabled) { trace_mode = TRACE_CHECKONLY; break; }
													print_breakpoints();
													response += "OK";
													break;
												}
											}
											// TODO: error when breakpoint not found
										}
									} else
										response += "E01";
								} else if(request.substr(0, 2) == "Z2" || request.substr(0, 2) == "Z3" || request.substr(0, 2) == "Z4") { // Z2: write watchpoint, Z3: read watchpoint, Z4: access watchpoint
									int rwi = 0;
									if(request[1] == '2')
										rwi = 2; // write
									else if(request[1] == '3')
										rwi = 1; // read
									else
										rwi = 1 | 2; // read + write
									auto comma = request.find(',', strlen("Z2"));
									auto comma2 = request.find(',', strlen("Z2,"));
									if(comma != std::string::npos && comma2 != std::string::npos) {
										uaecptr adr = strtoul(request.data() + strlen("Z2,"), nullptr, 16);
										int size = strtoul(request.data() + comma2 + 1, nullptr, 16);
										barto_log("GDBSERVER: write watchpoint at 0x%x, size 0x%x\n", adr, size);
										for(auto& mwn : mwnodes) {
											if(mwn.size)
												continue;
											mwn.addr = adr;
											mwn.size = size;
											mwn.rwi = rwi;
											// defaults from debug.cpp@memwatch()
											mwn.val_enabled = 0;
											mwn.val_mask = 0xffffffff;
											mwn.val = 0;
											mwn.access_mask = MW_MASK_ALL;
											mwn.reg = 0xffffffff;
											mwn.frozen = 0;
											mwn.modval_written = 0;
											mwn.mustchange = 0;
											mwn.bus_error = 0;
											mwn.reportonly = false;
											mwn.nobreak = false;
											print_watchpoints();
											response += "OK";
											break;
										}
										memwatch_setup();
										// TODO: error when too many watchpoints!
									} else
										response += "E01";
								} else if(request.substr(0, 2) == "z2" || request.substr(0, 2) == "z3" || request.substr(0, 2) == "z4") { // Z2: clear write watchpoint, Z3: clear read watchpoint, Z4: clear access watchpoint
									auto comma = request.find(',', strlen("z2"));
									if(comma != std::string::npos) {
										uaecptr adr = strtoul(request.data() + strlen("z2,"), nullptr, 16);
										for(auto& mwn : mwnodes) {
											if(mwn.size && mwn.addr == adr) {
												mwn.size = 0;
												trace_mode = 0;
												print_watchpoints();
												response += "OK";
												break;
											}
											// TODO: error when watchpoint not found
										}
										memwatch_setup();
									} else
										response += "E01";
								} else if(request[0] == 'g') { // get registers
									response += get_registers();
								} else if(request[0] == 'G') { // MCP-WINUAE-EMU: write all registers
									if(set_registers(request.substr(1)))
										response += "OK";
									else
										response += "E01";
								} else if(request[0] == 'p') { // get register
									response += get_register(strtoul(request.data() + 1, nullptr, 16));
								} else if(request[0] == 'P') { // MCP-WINUAE-EMU: write single register
									auto eq = request.find('=');
									if(eq != std::string::npos) {
										int reg = strtoul(request.data() + 1, nullptr, 16);
										uint32_t value = strtoul(request.data() + eq + 1, nullptr, 16);
										if(set_register(reg, value))
											response += "OK";
										else
											response += "E01";
									} else
										response += "E01";
								} else if(request[0] == 'M') { // MCP-WINUAE-EMU: write memory
									auto comma = request.find(',');
									auto colon = request.find(':');
									if(comma != std::string::npos && colon != std::string::npos) {
										uaecptr adr = strtoul(request.data() + 1, nullptr, 16);
										int len = strtoul(request.data() + comma + 1, nullptr, 16);
										std::string hex_data = request.substr(colon + 1);
										barto_log("GDBSERVER: write 0x%x bytes at 0x%x\n", len, adr);
										bool ok = true;
										for(int i = 0; i < len && i * 2 + 1 < hex_data.length(); i++) {
											int hi = hex_to_int(hex_data[i * 2]);
											int lo = hex_to_int(hex_data[i * 2 + 1]);
											if(hi < 0 || lo < 0) {
												ok = false;
												break;
											}
											uae_u8 byte = (hi << 4) | lo;
											addrbank* ad = &get_mem_bank(adr + i);
											if(ad) {
												ad->bput(adr + i, byte);
											} else {
												ok = false;
												break;
											}
										}
										response += ok ? "OK" : "E01";
									} else
										response += "E01";
								} else if(request[0] == 'm') { // read memory
									auto comma = request.find(',');
									if(comma != std::string::npos) {
										std::string mem;
										uaecptr adr = strtoul(request.data() + strlen("m"), nullptr, 16);
										int len = strtoul(request.data() + comma + 1, nullptr, 16);
										addrbank* ad;
										uae_u8 *custom_data = NULL;
										size_t custom_save_length = 0;
										barto_log("GDBSERVER: want 0x%x bytes at 0x%x\n", len, adr);
										while(len-- > 0) {
											int data = -1;
											if (debug_safe_addr(adr, 1)) {
												ad = &get_mem_bank(adr);
												data = ad->bget(adr);
											} else {
												if ((adr >= 0xdff000) && (adr < 0xdff1fe)) {
													if (custom_data == NULL) {
														custom_data = save_custom(&custom_save_length, 0, 1);
													}
													int idx = (adr & 0x1ff) + 4;
													if ((idx > 0) && (idx < custom_save_length)) {
														data = custom_data[idx];
													}
												}
											}

											if(data == -1) {
												barto_log("GDBSERVER: error reading memory at 0x%x\n", len, adr);
												response += "E01";
												mem.clear();
												break;
											}
											mem += hex[data >> 4];
											mem += hex[data & 0xf];
											adr++;
										}
										response += mem;
									} else
										response += "E01";
								}
							} else
								barto_log("GDBSERVER: packet checksum mismatch: got %c%c, want %c%c\n", tolower(request[end + 1]), tolower(request[end + 2]), hex[cksum >> 4], hex[cksum & 0xf]);
						} else
							barto_log("GDBSERVER: packet checksum missing\n");
					} else
						barto_log("GDBSERVER: packet end marker '#' not found\n");
				}

				send_ack(ack);
				send_response(response);
			} else if(result == 0) {
				disconnect();
			} else {
				barto_log(_T("GDBSERVER: error receiving data: %d\n"), WSAGetLastError());
				disconnect();
			}
		}
		if(!is_connected()) {
			debugger_state = state::inited;
			close();
			deactivate_debugger();
		}
	}

	// called during pause_emulation
	void vsync() {
		if(!(currprefs.debugging_features & (1 << 2))) // "gdbserver"
			return;

		// continue emulation if receiving debug commands
		if(debugger_state == state::connected && data_available()) {
			resumepaused(9);
			// handle_packet will be called in next call to vsync_pre
		}
	}

	void vsync_pre() {
		if(!(currprefs.debugging_features & (1 << 2))) // "gdbserver"
			return;

		// MCP-WINUAE-EMU EXTENSION: Auto-activate debugger when GDB client connects
		// This allows games loaded from ADF to be debugged without a debugging_trigger
		if(debugger_state == state::inited && is_connected()) {
			barto_log("GDBSERVER: Client connected, activating debugger for MCP mode\n");
			useAck = true;
			debugger_state = state::debugging;
			debugmem_trace = true;
			// Set up for generic debugging (no specific process)
			baseText = 0;
			sizeText = 0x7fff'ffff;
			activate_debugger();
		}

		static uae_u32 profile_start_cycles{};
		static size_t profile_custom_regs_size{};
		static uae_u8* profile_custom_regs{}; // at start of profile 
		static size_t profile_custom_agacolors_size{};
		static uae_u8* profile_custom_agacolors{};
		static FILE* profile_outfile{};

		if(debugger_state == state::profile) {
start_profile:
			// start profiling
			barto_log("PRF: %d/%d\n", profile_frame_count + 1, profile_num_frames);
			if(profile_frame_count == 0) {
				profile_outfile = fopen(profile_outname.c_str(), "wb");
				if(!profile_outfile) {
					send_response("$E01");
					debugger_state = state::debugging;
					activate_debugger();
					return;
				}
				int section_count = (int)sections.size();
				fwrite(&profile_num_frames, sizeof(int), 1, profile_outfile);
				fwrite(&section_count, sizeof(int), 1, profile_outfile);
				fwrite(sections.data(), sizeof(uint32_t), section_count, profile_outfile);
				fwrite(&systemStackLower, sizeof(uint32_t), 1, profile_outfile);
				fwrite(&systemStackUpper, sizeof(uint32_t), 1, profile_outfile);
				fwrite(&stackLower, sizeof(uint32_t), 1, profile_outfile);
				fwrite(&stackUpper, sizeof(uint32_t), 1, profile_outfile);

				// store chipmem
				auto profile_chipmem_size = chipmem_bank.reserved_size;
				auto profile_chipmem = std::make_unique<uint8_t[]>(profile_chipmem_size);
				memcpy(profile_chipmem.get(), chipmem_bank.baseaddr, profile_chipmem_size);

				// store bogomem
				auto profile_bogomem_size = bogomem_bank.reserved_size;
				auto profile_bogomem = std::make_unique<uint8_t[]>(profile_bogomem_size);
				memcpy(profile_bogomem.get(), bogomem_bank.baseaddr, profile_bogomem_size);

				// kickstart
				// from memory.cpp@save_rom()
				auto kick_start = 0xf80000;
				auto kick_real_start = kickmem_bank.baseaddr;
				auto kick_size = kickmem_bank.reserved_size;
				// 256KB or 512KB ROM?
				int i;
				for(i = 0; i < kick_size / 2 - 4; i++) {
					if(get_long_debug(i + kick_start) != get_long_debug(i + kick_start + kick_size / 2))
						break;
				}
				if(i == kick_size / 2 - 4) {
					kick_size /= 2;
					kick_start += ROM_SIZE_256;
				}

				fwrite(&kick_size, sizeof(kick_size), 1, profile_outfile);
				fwrite(kick_real_start, 1, kick_size, profile_outfile);

				// memory
				fwrite(&profile_chipmem_size, sizeof(profile_chipmem_size), 1, profile_outfile);
				fwrite(profile_chipmem.get(), 1, profile_chipmem_size, profile_outfile);
				fwrite(&profile_bogomem_size, sizeof(profile_bogomem_size), 1, profile_outfile);
				fwrite(profile_bogomem.get(), 1, profile_bogomem_size, profile_outfile);

				// CPU information
				fwrite(&baseclock, sizeof(int), 1, profile_outfile);
				fwrite(&cpucycleunit, sizeof(int), 1, profile_outfile);
			}

			// store custom registers
			profile_custom_regs = save_custom(&profile_custom_regs_size, nullptr, TRUE);
			profile_custom_agacolors = save_custom_agacolors(&profile_custom_agacolors_size, nullptr);

			// reset idle
			if(barto_debug_idle_count > 0) {
				barto_debug_idle[0] = barto_debug_idle[barto_debug_idle_count - 1] & 0x80000000;
				barto_debug_idle_count = 1;
			}

			// start profiler
			start_cpu_profiler(baseText, baseText + sizeText, profile_unwind.get());
			debug_dma = 1;
			profile_start_cycles = static_cast<uae_u32>(get_cycles() / cpucycleunit);
			//barto_log("GDBSERVER: Start CPU Profiler @ %u cycles\n", get_cycles() / cpucycleunit);
			debugger_state = state::profiling;
		} else if(debugger_state == state::profiling) {
			profile_frame_count++;
			// end profiling
			stop_cpu_profiler();
			debug_dma = 0;
			uae_u32 profile_end_cycles = static_cast<uae_u32>(get_cycles() / cpucycleunit);
			//barto_log("GDBSERVER: Stop CPU Profiler @ %u cycles => %u cycles\n", profile_end_cycles, profile_end_cycles - profile_start_cycles);

			// process dma records
			static constexpr int NR_DMA_REC_HPOS_IN = 256, NR_DMA_REC_VPOS_IN = 1000;
			static constexpr int NR_DMA_REC_HPOS_OUT = 227, NR_DMA_REC_VPOS_OUT = 313;
			auto dma_in = get_dma_records();
			auto dma_out = std::make_unique<dma_rec[]>(NR_DMA_REC_HPOS_OUT * NR_DMA_REC_VPOS_OUT);
			for(size_t y = 0; y < NR_DMA_REC_VPOS_OUT; y++) {
				for(size_t x = 0; x < NR_DMA_REC_HPOS_OUT; x++) {
					dma_out[y * NR_DMA_REC_HPOS_OUT + x] = dma_in[y * NR_DMA_REC_HPOS_IN + x];
				}
			}

			int profile_cycles = profile_end_cycles - profile_start_cycles;

			// calculate idle cycles
			int idle_cycles = 0;
			int last_idle = 0;
			for(int i = 0; i < barto_debug_idle_count; i++) {
				auto this_idle = barto_debug_idle[i];
				if((last_idle & 0x80000000) && !(this_idle & 0x80000000)) { // idle->busy
					idle_cycles += (this_idle & 0x7fffffff) - max(profile_start_cycles, (last_idle & 0x7fffffff));
				}

				if((this_idle ^ last_idle) & 0x80000000)
					last_idle = this_idle;
			}
			if(last_idle & 0x80000000)
				idle_cycles += profile_end_cycles - max(profile_start_cycles, (last_idle & 0x7fffffff));
			//barto_log("idle_cycles: %d\n", idle_cycles);

			// Custom Regs
			int custom_len = (int)profile_custom_regs_size;
			fwrite(&custom_len, sizeof(int), 1, profile_outfile);
			fwrite(profile_custom_regs, 1, custom_len, profile_outfile);
			free(profile_custom_regs);
			profile_custom_regs = nullptr;

			// AGA colors
			int custom_agacolors_len = (int)profile_custom_agacolors_size;
			fwrite(&custom_agacolors_len, sizeof(int), 1, profile_outfile);
			if(profile_custom_agacolors) {
				fwrite(profile_custom_agacolors, 1, custom_agacolors_len, profile_outfile);
				free(profile_custom_agacolors);
			}
			profile_custom_agacolors = nullptr;

			// DMA
			int dmarec_size = sizeof(dma_rec);
			int dmarec_count = NR_DMA_REC_HPOS_OUT * NR_DMA_REC_VPOS_OUT;
			fwrite(&dmarec_size, sizeof(int), 1, profile_outfile);
			fwrite(&dmarec_count, sizeof(int), 1, profile_outfile);
			fwrite(dma_out.get(), sizeof(dma_rec), NR_DMA_REC_HPOS_OUT * NR_DMA_REC_VPOS_OUT, profile_outfile);

			// resources
			int resource_size = sizeof(barto_debug_resource);
			int resource_count = barto_debug_resources_count;
			fwrite(&resource_size, sizeof(int), 1, profile_outfile);
			fwrite(&resource_count, sizeof(int), 1, profile_outfile);
			fwrite(barto_debug_resources, resource_size, resource_count, profile_outfile);

			fwrite(&profile_cycles, sizeof(int), 1, profile_outfile);
			fwrite(&idle_cycles, sizeof(int), 1, profile_outfile);

			// profiles
			int profile_count = get_cpu_profiler_output_count();
			fwrite(&profile_count, sizeof(int), 1, profile_outfile);
			fwrite(get_cpu_profiler_output(), sizeof(uae_u32), profile_count, profile_outfile);
			// write screenshot
			vsync_display_render(); // make sure current frame is rendered, this may be a line or so too early, but don't know how to hook render
			int monid = getfocusedmonitor();
			AmigaMonitor* mon = &AMonitors[monid];
			vidbuf_description* avidinfo = &adisplays[monid].gfxvidinfo;
			vidbuffer* vb = &avidinfo->drawbuffer; // too old
			// just write JPEGs if we do multi-frame profile (file too big with PNGs)
			if(screenshot_prepare(monid, vb) == 1) {
				auto bi = screenshot_get_bi();
				auto bi_bits = (const uint8_t*)screenshot_get_bits();
				if(bi->bmiHeader.biBitCount == 24) {
					// need to flip bits and swap rgb channels
					const auto w = bi->bmiHeader.biWidth;
					const auto h = bi->bmiHeader.biHeight;
					const auto pitch = bi->bmiHeader.biSizeImage / bi->bmiHeader.biHeight;
					auto bits = std::make_unique<uint8_t[]>(w * 3 * h);
					for(int y = 0; y < bi->bmiHeader.biHeight; y++) {
						for(int x = 0; x < bi->bmiHeader.biWidth; x++) {
							bits[y * w * 3 + x * 3 + 0] = bi_bits[(h - 1 - y) * pitch + x * 3 + 2];
							bits[y * w * 3 + x * 3 + 1] = bi_bits[(h - 1 - y) * pitch + x * 3 + 1];
							bits[y * w * 3 + x * 3 + 2] = bi_bits[(h - 1 - y) * pitch + x * 3 + 0];
						}
					}
					struct write_context_t {
						uint8_t data[2'000'000]{};
						int size = 0;
						int type = 0;
					};
					auto write_context = std::make_unique<write_context_t>();
					auto write_func = [](void* _context, void* data, int size) {
						auto context = (write_context_t*)_context;
						memcpy(&context->data[context->size], data, size);
						context->size += size;
					};
					if(profile_num_frames > 1) {
						stbi_write_jpg_to_func(write_func, write_context.get(), w, h, 3, bits.get(), 50);
						write_context->type = 0; // JPG
					} else {
						stbi_write_png_to_func(write_func, write_context.get(), w, h, 3, bits.get(), w * 3);
						write_context->type = 1; // PNG
					}
					write_context->size = (write_context->size + 3) & ~3; // pad to 32bit
					fwrite(&write_context->size, sizeof(int), 1, profile_outfile);
					fwrite(&write_context->type, sizeof(int), 1, profile_outfile);
					fwrite(write_context->data, 1, write_context->size, profile_outfile);
				}
			}

			if(profile_frame_count == profile_num_frames) {
				fclose(profile_outfile);
				send_response("$OK");

				debugger_state = state::debugging;
				activate_debugger();
			} else {
				debugger_state = state::profile;
				goto start_profile;
			}
		}

		if(debugger_state == state::connected && data_available()) {
			handle_packet();
		}
	}

	void vsync_post() {
		if(!(currprefs.debugging_features & (1 << 2))) // "gdbserver"
			return;
	}

	uaecptr KPutCharX{};
	uaecptr Trap7{};
	uaecptr AddressError{};
	uaecptr IllegalError{};
	std::string KPutCharOutput;

	void output(const char* string) {
		if(gdbconn != INVALID_SOCKET && !in_handle_packet) {
			std::string response = "$O";
			while(*string)
				response += hex8(*string++);
			send_response(response);
		}
	}

	void log_output(const TCHAR* tstring) {
		auto utf8 = string_to_utf8(tstring);
		if(utf8.substr(0, 5) == "DBG: ") {
			utf8 = utf8.substr(0, utf8.length() - 1); // get rid of extra newline from uaelib
			for(size_t start = 0;;) { // append "DBG: " to every newline, because GDB splits text by lines and vscode doesn't know that the extra lines are DBG output
				auto p = utf8.find('\n', start);
				if(p == std::string::npos || p == utf8.length() - 1)
					break;

				utf8.replace(p, 1, "\nDBG: ");
				start = p + 6;
			}

		}
		output(utf8.c_str());
	}

	void barto_log(const char* format, ...) {
		char buffer[16*1024];
		va_list parms;
		va_start(parms, format);
		vsprintf(buffer, format, parms);
		OutputDebugStringA(buffer);
		output(buffer);
		// Also write to log file if enabled
		if(log_file) {
			fputs(buffer, log_file);
			fflush(log_file);
		}
		va_end(parms);
	}

	void barto_log(const wchar_t* format, ...) {
		wchar_t buffer[16*1024];
		va_list parms;
		va_start(parms, format);
		vswprintf(buffer, format, parms);
		OutputDebugStringW(buffer);
		output(string_to_utf8(buffer).c_str());
		va_end(parms);
	}

	// returns true if gdbserver handles debugging
	bool debug() {
		if(!(currprefs.debugging_features & (1 << 2))) // "gdbserver"
			return false;

		warpmode(0);
		//cfgfile_modify(-1, _T("warp false"), 0, nullptr, 0);
		//cfgfile_modify(-1, _T("cpu_speed real"), 0, nullptr, 0);
		//cfgfile_modify(-1, _T("cpu_cycle_exact true"), 0, nullptr, 0);
		//cfgfile_modify(-1, _T("cpu_memory_cycle_exact true"), 0, nullptr, 0);
		//cfgfile_modify(-1, _T("blitter_cycle_exact true"), 0, nullptr, 0);

		// break at start of process
		if(debugger_state == state::inited) {
			if(currprefs.debugging_trigger[0]) {
				//KPutCharX
				auto execbase = get_long_debug(4);
				KPutCharX = execbase - 0x204;
				for(auto& bpn : bpnodes) {
					if(bpn.enabled)
						continue;
					bpn.value1 = KPutCharX;
					bpn.type = BREAKPOINT_REG_PC;
					bpn.oper = BREAKPOINT_CMP_EQUAL;
					bpn.enabled = 1;
					barto_log("GDBSERVER: Breakpoint for KPutCharX at 0x%x installed\n", bpn.value1);
					break;
				}

				// TRAP#7 breakpoint (GCC generates this opcode when it encounters undefined behavior)
				Trap7 = get_long_debug(regs.vbr + 0x9c);
				for(auto& bpn : bpnodes) {
					if(bpn.enabled)
						continue;
					bpn.value1 = Trap7;
					bpn.type = BREAKPOINT_REG_PC;
					bpn.oper = BREAKPOINT_CMP_EQUAL;
					bpn.enabled = 1;
					barto_log("GDBSERVER: Breakpoint for TRAP#7 at 0x%x installed\n", bpn.value1);
					break;
				}

				AddressError = get_long_debug(regs.vbr + 3 * 4);
				for(auto& bpn : bpnodes) {
					if(bpn.enabled)
						continue;
					bpn.value1 = AddressError;
					bpn.type = BREAKPOINT_REG_PC;
					bpn.oper = BREAKPOINT_CMP_EQUAL;
					bpn.enabled = 1;
					barto_log("GDBSERVER: Breakpoint for AddressError at 0x%x installed\n", bpn.value1);
					break;
				}

				IllegalError = get_long_debug(regs.vbr + 4 * 4);
				for(auto& bpn : bpnodes) {
					if(bpn.enabled)
						continue;
					bpn.value1 = IllegalError;
					bpn.type = BREAKPOINT_REG_PC;
					bpn.oper = BREAKPOINT_CMP_EQUAL;
					bpn.enabled = 1;
					barto_log("GDBSERVER: Breakpoint for IllegalError at 0x%x installed\n", bpn.value1);
					break;
				}

				// watchpoint for NULL (GCC sees this as undefined behavior)
				// disabled for now, always triggered in OpenScreen()
				/*for(auto& mwn : mwnodes) {
					if(mwn.size)
						continue;
					mwn.addr = 0;
					mwn.size = 4;
					mwn.rwi = 1 | 2; // read + write
					// defaults from debug.cpp@memwatch()
					mwn.val_enabled = 0;
					mwn.val_mask = 0xffffffff;
					mwn.val = 0;
					mwn.access_mask = MW_MASK_CPU_D_R | MW_MASK_CPU_D_W; // CPU data read/write only
					mwn.reg = 0xffffffff;
					mwn.frozen = 0;
					mwn.modval_written = 0;
					mwn.mustchange = 0;
					mwn.bus_error = 0;
					mwn.reportonly = false;
					mwn.nobreak = false;
					memwatch_setup();
					barto_log("GDBSERVER: Watchpoint for NULL installed\n");
					break;
				}*/

				// enable break at exceptions - doesn't break when exceptions occur in Kickstart
				debug_illegal = 1;
				debug_illegal_mask = (1 << 3) || (1 << 4); // 3 = address error, 4 = illegal instruction

				// BUG FIX: Do NOT reset processname here - it's needed for baseText calculation
				// in state::connected handler. The original code incorrectly copied lines from
				// debug.cpp@process_breakpoint() which clears processname, but that function is
				// for interactive debugger commands, not for GDB server initialization.
				// processptr = 0;           // REMOVED
				// xfree(processname);       // REMOVED  
				// processname = nullptr;    // REMOVED - this prevented baseText calculation!
				barto_log("GDBSERVER: DEBUG Trigger detected! Saving state. processname='%s' (should NOT be null)\n",
					processname ? processname : "(null)");
				savestate_quick(0, 1); // save state for "monitor reset"
			}
			barto_log("GDBSERVER: Waiting for connection...\n");
			while(!is_connected()) {
				barto_log(".");
				Sleep(100);
			}
			barto_log("\n");
			useAck = true;
			debugger_state = state::debugging;
			//debugmem_enable_stackframe(true); // crashes WinUAE if stackframe overrun
			debugmem_trace = true;
		}

		// something stopped execution and entered debugger
		if(debugger_state == state::connected) {
//while(!IsDebuggerPresent()) Sleep(100); __debugbreak();
			auto pc = munge24(m68k_getpc());
			barto_log("GDBSERVER: state::connected PC=0x%x baseText=0x%x\n", pc, baseText);
			if (pc == KPutCharX) {
				// if this is too slow, hook uaelib trap#86
				auto ascii = static_cast<uint8_t>(m68k_dreg(regs, 0));
				KPutCharOutput += ascii;
				if(ascii == '\0') {
					std::string response = "$O";
					for(const auto& ch : KPutCharOutput)
						response += hex8(ch);
					send_response(response);
					KPutCharOutput.clear();
				}
				deactivate_debugger();
				return true;
			}

			std::string response{ "S05" };

			//if(memwatch_triggered) // can't use, debug() will reset it, so just check mwhit
			if(mwhit.size) {
				for(const auto& mwn : mwnodes) {
					if(mwn.size && mwhit.addr >= mwn.addr && mwhit.addr < mwn.addr + mwn.size) {
						if(mwn.addr == 0) {
							response = "S0B"; // undefined behavior -> SIGSEGV
						} else {
//while(!IsDebuggerPresent()) Sleep(100); __debugbreak();
//							auto data = get_long_debug(mwn.addr);
							response = "T05";
							if(mwhit.rwi == 2)
								response += "watch";
							else if(mwhit.rwi == 1)
								response += "rwatch";
							else
								response += "awatch";
							response += ":";
							response += hex32(mwhit.addr);
							response += ";";
						}
						// so we don't trigger again
						mwhit.size = 0;
						mwhit.addr = 0;
						goto send_response;
					}
				}
			}
			// AUTO-DETECTION: If PC is in user range and we have unrelocated ELF breakpoints,
			// try to detect baseText automatically by checking if PC matches any ELF BP + offset
			if(baseText < ELF_TEXT_BASE && breakpoint_elf_addresses.size() > 0 && pc >= 0x1000 && pc < 0x1000000) {
				// PC is in potential user code range, check if it could be a relocated ELF address
				for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
					uaecptr elfAddr = breakpoint_elf_addresses[i];
					if(elfAddr >= ELF_TEXT_BASE && elfAddr < ELF_TEXT_BASE + 0x100000 && pc > elfAddr) {
						// Calculate what baseText would be if this ELF address was at PC
						uaecptr potential_loadOffset = pc - elfAddr;
						uaecptr potential_baseText = ELF_TEXT_BASE + potential_loadOffset;
						
						// Validate: the load offset should be reasonable
						// Programs typically load in Chip RAM (0x0-0x200000) or Fast RAM (0xC00000+)
						// The offset should move addresses from ELF base (0x400) to user RAM
						bool valid_range = (potential_baseText >= 0x1000 && potential_baseText < 0x200000) ||  // Chip RAM
						                   (potential_baseText >= 0xC00000 && potential_baseText < 0x1000000); // Fast RAM
						
						if(valid_range) {
							barto_log("AUTODETECT: PC=0x%x could match ELF BP 0x%x with baseText=0x%x (loadOffset=0x%x)\n",
								pc, elfAddr, potential_baseText, potential_loadOffset);
							baseText = potential_baseText;
							sizeText = 0x100000;
							relocate_breakpoints();
							break;
						}
					}
				}
			}

			for(const auto& bpn : bpnodes) {
				// ORIGINAL BARTMAN CODE: GDB sends already-relocated addresses
				// The modified GDB (m68k-amiga-elf-gdb) applies qOffsets relocation internally
				// So we just compare directly without any transformation
				if(bpn.enabled && bpn.type == BREAKPOINT_REG_PC && bpn.value1 == pc) {
					// see binutils-gdb/include/gdb/signals.def for number of signals
					if(pc == Trap7) {
						response = "S07"; // TRAP#7 -> SIGEMT
						// unwind PC & stack for better debugging experience (otherwise we're probably just somewhere in Kickstart)
						regs.pc = regs.instruction_pc_user_exception - 2;
						m68k_areg(regs, A7 - A0) = regs.usp;
					} else if(pc == AddressError) {
						response = "S0A"; // AddressError -> SIGBUS
						// unwind PC & stack for better debugging experience (otherwise we're probably just somewhere in Kickstart)
						regs.pc = regs.instruction_pc_user_exception; // don't know size of opcode that caused exception
						m68k_areg(regs, A7 - A0) = regs.usp;
					} else if(pc == IllegalError) {
						response = "S04"; // AddressError -> SIGILL
						// unwind PC & stack for better debugging experience (otherwise we're probably just somewhere in Kickstart)
						regs.pc = regs.instruction_pc_user_exception; // don't know size of opcode that caused exception
						m68k_areg(regs, A7 - A0) = regs.usp;
					} else {
						response = "T05swbreak:;";
					}
					goto send_response;
				}
			}
send_response:
			send_response("$" + response);
			trace_mode = 0;
			debugger_state = state::debugging;
		}

		// debugger active
		while(debugger_state == state::debugging) {
			handle_packet();

			MSG msg{};
			while(PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			Sleep(1);
		}

		return true;
	}
} // namespace barto_gdbserver
