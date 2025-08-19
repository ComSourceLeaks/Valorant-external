#include <Windows.h>
#include <TlHelp32.h>
#include <thread>
#include "lib.hpp"
#include "driver.hpp"
#include "utils.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include "offsets.hpp"

uintptr_t pml4base = 0;
uintptr_t uworld = 0;

void get_world()
{
	auto pml4_base = utils::find_pml4_base();
	printf("GetPML4Base: %p\n", (void*)pml4_base);

	uintptr_t UWOLRD_1 = driver::RPM<uintptr_t>(pml4_base + 0x1C0);


	uintptr_t uworld_offset = 0;

	for (int i = 0x0; i < 0x500; i += 0x8)
	{
		uintptr_t address_to_read = pml4_base + i;
		printf("[*] Trying to read at address: %p\n", (void*)address_to_read);

		auto potential_uworld = driver::RPM<uintptr_t>(address_to_read);

		printf("[+] tested potential_uworld Pointer: 0x%llx\n", potential_uworld);

		if (potential_uworld != 0)
		{
			uintptr_t level_address = potential_uworld + offsets::persistentlevel;
			printf("[*] Trying to read level at address: %p\n", (void*)level_address);

			auto level = driver::RPM<uintptr_t>(level_address);
			if (level != 0)
			{
				uintptr_t owning_world_address = level + offsets::uworld;
				printf("[*] Trying to read owning_world at address: %p\n", (void*)owning_world_address);

				auto owning_world = driver::RPM<uintptr_t>(owning_world_address);

				printf("[*] Offset: 0x%X\n", i);
				printf("    WorldPtr = 0x%llx\n", potential_uworld);
				printf("    ULevel = 0x%llx\n", level);
				printf("    OwningWorld = 0x%llx\n", owning_world);

				if (owning_world == potential_uworld)
				{
					uworld = potential_uworld;

					printf("[+] Found UWorld Offset: 0x%X\n", i);
					uworld_offset = i;
					break;
				}
			}
		}
	}

	if (uworld_offset == 0)
	{
		printf("[-] Failed to find UWorld offset!\n");
	}
}

void cache_game()
{
	auto last_update = std::chrono::steady_clock::now();
	int iteration_count = 0;

	while (true)
	{
		system("cls");

		// Zeitstempel ausgeben
		auto now = std::chrono::steady_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());



		if (driver::hvci_enabled) {
			printf("Base Address: 0x%llx\n", driver::base);
		}
		else {
			printf("VGK Address: 0x%llx\n", driver::vgk);
		}

		if (driver::hvci_enabled) {
			constexpr uint64_t uworldState = 0x9EF4050;
			uworld = driver::RPM<uintptr_t>(driver::base + uworldState);
		}
		else {
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_update).count();

			if (!uworld || elapsed > 15) {
				get_world();
				last_update = now;
			}
		}
		printf("UWorld: 0x%llx\n", uworld);

		std::cout << std::flush; 
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}

int main()
{
	SonyDriverHelper::api::Init(); // aimbot
	SonyDriverHelper::api::MouseMove(500, 500);
	driver::initialize_driver("\\\\.\\GamerDoc");


	while (!driver::process_id)
	{
		driver::process_id = utils::get_process_id(L"VALORANT-Win64-Shipping.exe");
		Sleep(1000);
	}

	driver::attach_to_process(driver::process_id);

	if (driver::hvci_enabled) {

		while (!driver::base)
		{
			driver::base = utils::get_process_base_id(driver::process_id);
			Sleep(1000);
		}
	}
	else {
		while (!driver::vgk)
		{
			driver::vgk = utils::GetDriverModuleBase("vgk.sys");
			Sleep(1000);
		}
	}

	std::thread(cache_game).detach();
	getchar();

	return 0;
}