#pragma once
constexpr ULONG init_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x775, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x776, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG read_kernel_code = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x777, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
constexpr ULONG move_mouse = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x778, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);

namespace driver
{
    HANDLE m_driver_handle = nullptr; //handle to our driver

    struct info_t
    {
        HANDLE target_pid = 0;
        void* cr3_context = 0x0;
        void* target_address = 0x0;
        void* buffer_address = 0x0;
        SIZE_T size = 0;
        SIZE_T return_size = 0;
    };

    uintptr_t cr3_context = 0;
    uintptr_t vgk = 0;
    uintptr_t base = 0;
    uintptr_t process_id = 0;
    bool hvci_enabled = false;

    void initialize_driver(LPCSTR file_name)
    {
        m_driver_handle = CreateFileA(file_name, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); //get a handle to our driver
    }

    void attach_to_process(DWORD process_id) {
        info_t io_info;

        io_info.target_pid = (HANDLE)process_id;

        DeviceIoControl(m_driver_handle, init_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);
    }

    void RPMRAW(uintptr_t address, uintptr_t buffer, size_t size) {
        info_t io_info;
        io_info.cr3_context = (void*)cr3_context;
        io_info.target_address = (void*)address;
        io_info.buffer_address = (void*)buffer; 
        io_info.size = size;

        DeviceIoControl(m_driver_handle, read_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);
    }

    template<typename T>
    void RPMARRAY(uintptr_t address, T* array, size_t len)
    {
        RPMRAW(address, (uintptr_t)array, sizeof(T) * len);
    }

    template<typename T> T RPM(uintptr_t address) {
        info_t io_info;
        T read_data{};

        io_info.cr3_context = (void*)cr3_context;
        io_info.target_address = (void*)address;
        io_info.buffer_address = (void*)&read_data;
        io_info.size = sizeof(T);

        DeviceIoControl(m_driver_handle, read_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);

        return read_data;
    }

    template<typename T> T RKA(uintptr_t address, size_t additional_size = 0) {
        info_t io_info;
        T read_data{};

        io_info.target_address = (void*)address;
        io_info.buffer_address = (void*)&read_data;
        io_info.size = sizeof(T) + additional_size;

        DeviceIoControl(m_driver_handle, read_kernel_code, &io_info, sizeof(io_info), &io_info, sizeof(io_info), nullptr, nullptr);

        return read_data;
    }

}
