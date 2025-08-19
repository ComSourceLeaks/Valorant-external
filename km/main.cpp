#include <ntifs.h>
#define page_offset_size 12
static const uintptr_t pmask = (~0xfull << 8) & 0xfffffffffull;


typedef int BOOL;
typedef unsigned int DWORD;
typedef ULONG_PTR QWORD;

#pragma warning(disable : 4201)


extern "C"
{
	NTKERNELAPI NTSTATUS IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);

	QWORD _KeAcquireSpinLockAtDpcLevel;
	QWORD _KeReleaseSpinLockFromDpcLevel;
	QWORD _IofCompleteRequest;
	QWORD _IoReleaseRemoveLockEx;

	NTSYSCALLAPI
		POBJECT_TYPE* IoDriverObjectType;

	NTSYSCALLAPI
		NTSTATUS
		ObReferenceObjectByName(
			__in PUNICODE_STRING ObjectName,
			__in ULONG Attributes,
			__in_opt PACCESS_STATE AccessState,
			__in_opt ACCESS_MASK DesiredAccess,
			__in POBJECT_TYPE ObjectType,
			__in KPROCESSOR_MODE AccessMode,
			__inout_opt PVOID ParseContext,
			__out PVOID* Object
		);
}

constexpr ULONG init_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read_kernel_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

struct info_t 
{
	HANDLE target_pid = 0;
	void* cr3_context = 0x0;
	void* target_address = 0x0;
	void* buffer_address = 0x0;
	SIZE_T size = 0;
	SIZE_T return_size = 0;
};

bool ReadKernel(uintptr_t address, void* buffer, size_t size)
{
	if (buffer == nullptr)
		return false;

	RtlCopyMemory(buffer, (void*)address, size);
	return true;
}

NTSTATUS read_physical_memory(PVOID address, PVOID buffer, SIZE_T size, PSIZE_T bytes)
{
	if (!address) return STATUS_UNSUCCESSFUL;
	MM_COPY_ADDRESS to_read = { 0 };
	to_read.PhysicalAddress.QuadPart = (LONGLONG)address;
	return MmCopyMemory(buffer, to_read, size, MM_COPY_MEMORY_PHYSICAL, bytes);
}
uintptr_t translate_linear(uintptr_t directory_table_base, uintptr_t virtual_address)
{
	directory_table_base &= ~0xf;
	uintptr_t pageoffset = virtual_address & ~(~0ul << page_offset_size);
	uintptr_t pte = ((virtual_address >> 12) & (0x1ffll));
	uintptr_t pt = ((virtual_address >> 21) & (0x1ffll));
	uintptr_t pd = ((virtual_address >> 30) & (0x1ffll));
	uintptr_t pdp = ((virtual_address >> 39) & (0x1ffll));
	SIZE_T readsize = 0;
	uintptr_t pdpe = 0;
	read_physical_memory((PVOID)(directory_table_base + 8 * pdp), &pdpe, sizeof(pdpe), &readsize);
	if (~pdpe & 1) return 0;
	uintptr_t pde = 0;
	read_physical_memory((PVOID)((pdpe & pmask) + 8 * pd), &pde, sizeof(pde), &readsize);
	if (~pde & 1) return 0;
	if (pde & 0x80) return (pde & (~0ull << 42 >> 12)) + (virtual_address & ~(~0ull << 30));
	uintptr_t ptraddr = 0;
	read_physical_memory((PVOID)((pde & pmask) + 8 * pt), &ptraddr, sizeof(ptraddr), &readsize);
	if (~ptraddr & 1) return 0;
	if (ptraddr & 0x80) return (ptraddr & pmask) + (virtual_address & ~(~0ull << 21));
	virtual_address = 0;
	read_physical_memory((PVOID)((ptraddr & pmask) + 8 * pte), &virtual_address, sizeof(virtual_address), &readsize);
	virtual_address &= pmask;
	if (!virtual_address) return 0;
	return virtual_address + pageoffset;
}

NTSTATUS read_process_memory(PEPROCESS process, PVOID address, PVOID buffer, size_t size, PVOID cr3_context)
{
	if (!process) return STATUS_UNSUCCESSFUL;
	uintptr_t process_base = (uintptr_t)cr3_context;
	if (!process_base) return STATUS_UNSUCCESSFUL;
	uintptr_t physical_address = translate_linear(process_base, (uintptr_t)address);
	if (!physical_address) return STATUS_UNSUCCESSFUL;
	uintptr_t final_size = min(PAGE_SIZE - (physical_address & 0xFFF), size);
	SIZE_T bytes_trough = 0;
	read_physical_memory((PVOID)physical_address, buffer, final_size, &bytes_trough);
	return STATUS_SUCCESS;
}

NTSTATUS ctl_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);

	static PEPROCESS s_target_process;

	irp->IoStatus.Information = sizeof(info_t);
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto buffer = (info_t*)irp->AssociatedIrp.SystemBuffer;

	if (stack) { //add error checking
		if (buffer && sizeof(*buffer) >= sizeof(info_t)) {
			const auto ctl_code = stack->Parameters.DeviceIoControl.IoControlCode;

			switch (ctl_code)
			{
			case init_code:
				PsLookupProcessByProcessId(buffer->target_pid, &s_target_process);
				break;
			case read_code:
				read_process_memory(s_target_process, buffer->target_address, buffer->buffer_address, buffer->size, buffer->cr3_context);
				break;
			case read_kernel_code:
				ReadKernel((uintptr_t)buffer->target_address, buffer->buffer_address, buffer->size);
				break;
			}
		}
	}

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS unsupported_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);

	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS create_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS close_io(PDEVICE_OBJECT device_obj, PIRP irp) {
	UNREFERENCED_PARAMETER(device_obj);

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return irp->IoStatus.Status;
}

NTSTATUS real_main(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(registery_path);

	UNICODE_STRING dev_name, sym_link;
	PDEVICE_OBJECT dev_obj;

	RtlInitUnicodeString(&dev_name, L"\\Device\\GamerDoc");
	auto status = IoCreateDevice(driver_obj, 0, &dev_name, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &dev_obj);
	if (status != STATUS_SUCCESS) return status;

	RtlInitUnicodeString(&sym_link, L"\\DosDevices\\GamerDoc");
	status = IoCreateSymbolicLink(&sym_link, &dev_name);
	if (status != STATUS_SUCCESS) return status;

	SetFlag(dev_obj->Flags, DO_BUFFERED_IO); //set DO_BUFFERED_IO bit to 1

	for (int t = 0; t <= IRP_MJ_MAXIMUM_FUNCTION; t++) //set all MajorFunction's to unsupported
		driver_obj->MajorFunction[t] = unsupported_io;

	//then set supported functions to appropriate handlers
	driver_obj->MajorFunction[IRP_MJ_CREATE] = create_io; //link our io create function
	driver_obj->MajorFunction[IRP_MJ_CLOSE] = close_io; //link our io close function
	driver_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ctl_io; //link our control code handler
	driver_obj->DriverUnload = NULL; //add later

	ClearFlag(dev_obj->Flags, DO_DEVICE_INITIALIZING); //set DO_DEVICE_INITIALIZING bit to 0 (we are done initializing)
	return status;
}

extern "C" NTSTATUS CustomDriverEntry(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registery_path) {
	UNREFERENCED_PARAMETER(driver_obj);
	UNREFERENCED_PARAMETER(registery_path);

	_KeAcquireSpinLockAtDpcLevel = (QWORD)KeAcquireSpinLockAtDpcLevel;
	_KeReleaseSpinLockFromDpcLevel = (QWORD)KeReleaseSpinLockFromDpcLevel;
	_IofCompleteRequest = (QWORD)IofCompleteRequest;
	_IoReleaseRemoveLockEx = (QWORD)IoReleaseRemoveLockEx;

	UNICODE_STRING  drv_name;
	RtlInitUnicodeString(&drv_name, L"\\Driver\\GamerDoc");
	IoCreateDriver(&drv_name, &real_main); //kdmapper support 

	return STATUS_SUCCESS;
}
