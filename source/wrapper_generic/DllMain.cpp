#include "Hooking/Hooks.h"
#include "Util.h"

//
// sl.interposer.dll  loads  sl.common.dll
// sl.common.dll      loads  _nvngx.dll       <- we are here
// _nvngx.dll         loads  nvngx_dlssg.dll  <- intercept this stage
//
std::vector TargetLibrariesToHook = { L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss_g.dll", L"_nvngx.dll",
									  /*L"sl.latewarp.dll", L"nvngx_latewarp.dll", L"nvngx_dlssg.dll"*/ };

constexpr wchar_t TargetImplementationDll[] = L"nvngx_dlssg.dll";
constexpr wchar_t RelplacementImplementationDll[] = L"dlssg_to_fsr3_amd_is_better.dll";

constexpr wchar_t TargetEGSServicesDll[] = L"EOSSDK-Win64-Shipping.dll";
constexpr wchar_t TargetEGSOverlayDll[] = L"EOSOVH-Win64-Shipping.dll";

bool EnableAggressiveHooking;
constinit std::mutex HookedModuleLock;
std::unordered_set<HMODULE> HookedModuleList;

void *TryResolveNGXLibrary();
bool TryInterceptNvAPIFunction(void *ModuleHandle, const void *FunctionName, void **FunctionPointer);
bool TryRemapModule(const wchar_t *Path, HMODULE *ModuleHandle);
bool TryPatchImportsForModule(const wchar_t *Path, HMODULE ModuleHandle);

HMODULE WINAPI HookedLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	HMODULE libraryHandle = nullptr;

	if (!TryRemapModule(lpLibFileName, &libraryHandle))
		libraryHandle = LoadLibraryExW(lpLibFileName, hFile, dwFlags);

	TryPatchImportsForModule(lpLibFileName, libraryHandle);
	return libraryHandle;
}

HMODULE WINAPI HookedLoadLibraryW(LPCWSTR lpLibFileName)
{
	HMODULE libraryHandle = nullptr;

	if (!TryRemapModule(lpLibFileName, &libraryHandle))
		libraryHandle = LoadLibraryW(lpLibFileName);

	TryPatchImportsForModule(lpLibFileName, libraryHandle);
	return libraryHandle;
}

BOOL WINAPI HookedFreeLibrary(HMODULE hLibModule)
{
	const auto result = FreeLibrary(hLibModule);

	if (result)
	{
		// FreeLibrary doesn't tell us whether the DLL actually unloaded. Check again. NOTE: Not thread safe w.r.t. loader lock.
		std::scoped_lock lock(HookedModuleLock);

		if (HookedModuleList.contains(hLibModule))
		{
			HMODULE newHandle;

			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(hLibModule), &newHandle) ||
				newHandle != hLibModule)
				HookedModuleList.erase(hLibModule);
		}
	}

	return result;
}

FARPROC WINAPI HookedGetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	auto proc = GetProcAddress(hModule, lpProcName);
	TryInterceptNvAPIFunction(hModule, lpProcName, reinterpret_cast<void **>(&proc));

	return proc;
}

std::wstring_view RemapStreamlinePluginPath(const wchar_t *Path)
{
	if (!Path)
		return {};

	//
	// OTA-enabled Streamline plugins will load from paths resembling:
	//  "C:\ProgramData/AAAAAA/NGX/models/sl_dlss_0/versions/BBBBBB/files/CCC_DDDDDDD.dll"
	//  "C:\ProgramData/AAAAAA/NGX/models/dlssg/versions/BBBBBB/files/CCC_DDDDDDD.bin"
	//
	// DLL/folder names aren't 100% consistent with plugin names. Therefore we have to remap them by hand.
	//
	std::wstring_view pathView = Path;

	if (pathView.contains(L"/versions/") || pathView.contains(L"/VERSIONS/"))
	{
		if (pathView.contains(L"/sl_common_") || pathView.contains(L"/SL_COMMON_"))
			pathView = L"sl.common.dll";
		else if (pathView.contains(L"/sl_dlss_g_") || pathView.contains(L"/SL_DLSS_G_"))
			pathView = L"sl.dlss_g.dll";
		else if (pathView.contains(L"/dlssg") || pathView.contains(L"/DLSSG"))
			pathView = L"nvngx_dlssg.dll";
	}

	return pathView;
}

std::optional<HMODULE> LoadReplacementImplementationLibrary()
{
	if (wchar_t v[2]; GetEnvironmentVariableW(L"DLSSGTOFSR3_SkipReplacement", v, std::size(v)) == 1)
	{
		if (v[0] == L'1')
			return std::nullopt;
	}

	// Load the replacement library from the same directory as this DLL instead of CWD.
	wchar_t path[2048];
	Util::GetModulePath(path, true, nullptr);

	wcscat_s(path, RelplacementImplementationDll);
	const auto handle = LoadLibraryW(path);

	if (!handle)
	{
		constinit static bool once = false;

		if (!std::exchange(once, true))
			MessageBoxW(nullptr, path, L"dlssg-to-fsr3 failed to load implementation library.", MB_ICONERROR);
	}

	return handle;
}

bool TryRemapModule(const wchar_t *Path, HMODULE *ModuleHandle)
{
	const auto remappedPath = RemapStreamlinePluginPath(Path);

	if (remappedPath.ends_with(TargetImplementationDll))
	{
		const auto newHandle = LoadReplacementImplementationLibrary();

		if (newHandle.has_value())
		{
			*ModuleHandle = newHandle.value();
			return true;
		}
	}
	else if (remappedPath.ends_with(TargetEGSOverlayDll))
	{
		SetLastError(ERROR_MOD_NOT_FOUND);
		*ModuleHandle = nullptr;

		return true;
	}

	return false;
}

bool TryPatchImportsForModule(const wchar_t *Path, HMODULE ModuleHandle)
{
	if (!Path || !ModuleHandle)
		return false;

	const bool isTargetModule = std::ranges::any_of(
		TargetLibrariesToHook,
		[mappedPath = RemapStreamlinePluginPath(Path)](const wchar_t *Target)
		{
			return mappedPath.ends_with(Target);
		});

	if (!isTargetModule)
		return false;

	const bool moduleRequiresPatching = [&]()
	{
		std::scoped_lock lock(HookedModuleLock);

		if (HookedModuleList.size() > 100)
			HookedModuleList.clear();

		return HookedModuleList.emplace(ModuleHandle).second;
	}();

	if (moduleRequiresPatching)
	{
		if constexpr (false)
		{
			OutputDebugStringW(L"Patching imports for a new module: ");
			OutputDebugStringW(Path);
			OutputDebugStringW(L"...\n");
		}

		Hooks::RedirectImport(ModuleHandle, "KERNEL32.dll", "LoadLibraryW", &HookedLoadLibraryW, nullptr);
		Hooks::RedirectImport(ModuleHandle, "KERNEL32.dll", "LoadLibraryExW", &HookedLoadLibraryExW, nullptr);
		Hooks::RedirectImport(ModuleHandle, "KERNEL32.dll", "FreeLibrary", &HookedFreeLibrary, nullptr);

		if (EnableAggressiveHooking && !GetProcAddress(ModuleHandle, "slInit")) // Skip sl.interposer.dll (OTA version conflict)
			Hooks::RedirectImport(ModuleHandle, "KERNEL32.dll", "GetProcAddress", &HookedGetProcAddress, nullptr);
	}

	return true;
}

void ApplyAggressiveHookingWorkarounds()
{
	// Prevent EGS's overlay from loading
	TargetLibrariesToHook.push_back(TargetEGSServicesDll);
	LoadLibraryW(TargetEGSServicesDll);

	// NvRemixBridge doesn't use Streamline. Use nvngx.dll in lieu of sl.interposer.dll.
	if (GetModuleHandleW(L"NvRemixBridge.exe"))
		TryResolveNGXLibrary();

	//
	// Force SL's interposer to load early. It's not always present in the local directory. A bit of guesswork
	// is required.
	//
	// "Dying Light 2\                  ph\work\bin\x64\DyingLightGame_x64_rwdi.exe"
	// "Returnal\         Returnal\     Binaries\Win64\Returnal-Win64-Shipping.exe"
	// "Hogwarts Legacy\  Phoenix\      Binaries\Win64\HogwartsLegacy.exe"
	// "SW Jedi Survivor\ SwGame\       Binaries\Win64\JediSurvivor.exe"
	// "Atomic Heart\     AtomicHeart\  Binaries\Win64\AtomicHeart-Win64-Shipping.exe"
	// "MMS\              MidnightSuns\ Binaries\Win64\MidnightSuns-Win64-Shipping.exe"
	// "The Great Circle\               TheGreatCircle.exe"
	//
	// "Dying Light 2\    ph\work\bin\x64\sl.interposer.dll"
	// "Returnal\         Engine\Plugins\Streamline\Binaries\ThirdParty\Win64\sl.interposer.dll"
	// "Hogwarts Legacy\  Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\sl.interposer.dll"
	// "SW Jedi Survivor\ Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\sl.interposer.dll"
	// "Atomic Heart\     Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\sl.interposer.dll"
	// "MMS\              Engine\Plugins\Runtime\Nvidia\Streamline\Binaries\ThirdParty\Win64\sl.interposer.dll"
	// "The Great Circle\ streamline\sl.interposer.dll"
	//
	constinit static const wchar_t *bruteInterposerPaths[] = {
		L"sl.interposer.dll",
		L"..\\..\\..\\Engine\\Plugins\\Streamline\\Binaries\\ThirdParty\\Win64\\sl.interposer.dll",
		L"..\\..\\..\\Engine\\Plugins\\Runtime\\Nvidia\\Streamline\\Binaries\\ThirdParty\\Win64\\sl.interposer.dll",
		L"streamline\\sl.interposer.dll",
	};

	if (!LoadLibraryW(bruteInterposerPaths[0]))
	{
		for (auto interposer : bruteInterposerPaths)
		{
			wchar_t path[2048];
			if (!Util::GetModulePath(path, true, GetModuleHandleW(nullptr)))
				break;

			wcscat_s(path, interposer);

			if (LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
				break;
		}
	}
}

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		OutputDebugStringW(L"DEBUG: Shim built with commit ID " BUILD_GIT_COMMIT_HASH "\n");

		if constexpr (false)
		{
			// Low level backbuffer capture utility
			LoadLibraryW(L"d3d12-burstcapture.dll");
		}

		if (EnableAggressiveHooking)
			ApplyAggressiveHookingWorkarounds();

		// We probably loaded after sl.interposer.dll and sl.common.dll. Try patching them up front.
		bool anyPatched = std::ranges::count_if(
			TargetLibrariesToHook,
			[](const wchar_t *Target)
		{
			return TryPatchImportsForModule(Target, GetModuleHandleW(Target)) && _wcsicmp(Target, TargetEGSServicesDll) != 0;
		}) > 0;

		// If zero Streamline dlls were loaded we'll have to hook the game's LoadLibrary calls and wait
		if (!anyPatched && EnableAggressiveHooking)
			anyPatched = TryPatchImportsForModule(TargetLibrariesToHook[0], GetModuleHandleW(nullptr));

		// Hooks can't be removed once they're in place. Pin this DLL in memory.
		if (anyPatched)
		{
			GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
				reinterpret_cast<LPCWSTR>(hInstDLL),
				&hInstDLL);
		}
	}

	return TRUE;
}
