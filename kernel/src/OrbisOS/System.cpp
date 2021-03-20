#include "System.hpp"
#include <Utils/Logger.hpp>
#include <Utils/Kdlsym.hpp>

using namespace Mira::Utils;

uint8_t* System::AllocateProcessMemory(struct proc* p_Process, uint32_t p_Size, uint32_t p_Protection)
{
    auto _vm_map_lock = (void(*)(vm_map_t map, const char* file, int line))kdlsym(_vm_map_lock);
    auto _vm_map_findspace = (int(*)(vm_map_t map, vm_offset_t start, vm_size_t length, vm_offset_t *addr))kdlsym(_vm_map_findspace);
    auto _vm_map_insert = (int(*)(vm_map_t map, vm_object_t object, vm_ooffset_t offset,vm_offset_t start, vm_offset_t end, vm_prot_t prot, vm_prot_t max, int cow))kdlsym(_vm_map_insert);
    auto _vm_map_unlock = (void(*)(vm_map_t map))kdlsym(_vm_map_unlock);

    if (p_Process == nullptr)
        return nullptr;
    
    WriteLog(LL_Info, "Requested Size: (%x).", p_Size);
    p_Size = round_page(p_Size);
    WriteLog(LL_Info, "Adjusted Size (%x).", p_Size);

    vm_offset_t s_Address = 0;

    // Get the vmspace
    auto s_VmSpace = p_Process->p_vmspace;
    if (s_VmSpace == nullptr)
    {
        WriteLog(LL_Info, "invalid vmspace.");
        return nullptr;
    }

    // Get the vmmap
    vm_map_t s_VmMap = &s_VmSpace->vm_map;

    // Lock the vmmap
    _vm_map_lock(s_VmMap, __FILE__, __LINE__);

    do
    {
        // Find some free space to allocate memory
        auto s_Result = _vm_map_findspace(s_VmMap, s_VmMap->header.start, p_Size, &s_Address);
        if (s_Result != 0)
        {
            WriteLog(LL_Error, "vm_map_findspace returned (%d).", s_Result);
            break;
        }

        WriteLog(LL_Debug, "_vm_map_findspace returned address (%p).", s_Address);

        // Validate the address
        if (s_Address == 0)
        {
            WriteLog(LL_Error, "allocated address is invalid (%p).", s_Address);
            break;
        }

        // Insert the new stuff map
        s_Result = _vm_map_insert(s_VmMap, NULL, 0, s_Address, s_Address + p_Size, p_Protection, p_Protection, 0);
        if (s_Result != 0)
        {
            WriteLog(LL_Error, "vm_map_insert returned (%d).", s_Result);
            break;
        }

    } while (false);

    _vm_map_unlock(s_VmMap);

    return reinterpret_cast<uint8_t*>(s_Address);
}

UserProcessVmMap* System::GetUserProcessVmMap(struct proc* p_Process, struct proc* p_RequestingProcess)
{
    auto _vm_map_lock_read = (void(*)(vm_map_t map, const char *file, int line))kdlsym(_vm_map_lock_read);
	auto _vm_map_unlock_read = (void(*)(vm_map_t map, const char *file, int line))kdlsym(_vm_map_unlock_read);
    auto _vm_map_lookup_entry = (boolean_t(*)(vm_map_t, vm_offset_t, vm_map_entry_t *))kdlsym(vm_map_lookup_entry);
    auto copyout = (int(*)(const void *kaddr, void *udaddr, size_t len))kdlsym(copyout);

    // Validate process
    if (p_Process == nullptr)
    {
        WriteLog(LL_Error, "invalid process.");
        return nullptr;
    }

    // This is documented behavior, that if p_RequestingProcess is nullptr
    // It assumes that p_Process is the caller
    if (p_RequestingProcess == nullptr)
        p_RequestingProcess = p_Process;
    
    int32_t s_ProcessId = p_Process->p_pid;
    
    // Try to get the target process vmspace
    struct vmspace* s_VmSpace = p_Process->p_vmspace;
    if (s_VmSpace == nullptr)
    {
        WriteLog(LL_Error, "could not get vmspace of pid (%d).", s_ProcessId);
        return nullptr;
    }

    UserProcessVmMap* s_ReturnData = nullptr;

    // Get the vm map
    struct vm_map* s_VmMap = &s_VmSpace->vm_map;

    // Lock the vm map
    _vm_map_lock_read(s_VmMap, __FILE__, __LINE__);

    do
    {
        // Get the number of entries in this vm_map
        uint64_t s_NumEntries = s_VmMap->nentries;
        if (s_NumEntries == 0)
        {
            WriteLog(LL_Error, "numEntries == 0");
            break;
        }

        struct vm_map_entry* s_Entry = nullptr;
        auto s_Ret = _vm_map_lookup_entry(s_VmMap, 0, &s_Entry);
        if (s_Ret != 0)
        {
            WriteLog(LL_Error, "could not get vm_map_entry (%d).", s_Ret);
            break;
        }

        auto s_AllocationSize = sizeof(UserProcessVmMap) + (s_NumEntries * sizeof(ProcVmMapEntry));
        auto s_UserMap = reinterpret_cast<UserProcessVmMap*>(new uint8_t[s_AllocationSize]);
        if (s_UserMap == nullptr)
        {
            WriteLog(LL_Error, "could not allocate kernel allocation (0x%llx).", s_AllocationSize);
            break;
        }
        memset(s_UserMap, 0, s_AllocationSize);
        s_UserMap->AllocatedSize = s_AllocationSize;
        s_UserMap->EntryCount = s_NumEntries;

        // Iterate all of the maps and set them
        for (auto l_Index = 0; l_Index < s_NumEntries; ++l_Index)
        {
            s_UserMap->Entries[l_Index].start = s_Entry->start;
            s_UserMap->Entries[l_Index].end = s_Entry->end;
            s_UserMap->Entries[l_Index].offset = s_Entry->offset;
            s_UserMap->Entries[l_Index].prot = s_Entry->protection;
            memcpy(s_UserMap->Entries[l_Index].name, s_Entry->name, sizeof(s_UserMap->Entries[l_Index].name));

            if (!(s_Entry = s_Entry->next))
                break;
        }

        // Allocate memory in the requesting process
        auto s_Allocation = AllocateProcessMemory(p_RequestingProcess, s_AllocationSize, VM_PROT_READ | VM_PROT_WRITE);
        if (s_Allocation == nullptr)
        {
            WriteLog(LL_Error, "could not allocate any memory of size (0x%x).", s_AllocationSize);
            delete [] s_UserMap;
            break;
        }

        // Write out all of the data we just got
        if (!WriteProcessMemory(p_Process, s_Allocation, s_UserMap, s_AllocationSize))
        {
            WriteLog(LL_Error, "could not write to userland memory.");
            delete [] s_UserMap;
            FreeProcessMemory(p_RequestingProcess, s_Allocation);
            break;
        }

        // Free the allocated user map as we don't need it anymore
        delete [] s_UserMap;

        // Set our return data
        s_ReturnData = reinterpret_cast<UserProcessVmMap*>(s_Allocation);

    } while (false);

    _vm_map_unlock_read(s_VmMap, __FILE__, __LINE__);
    
    return s_ReturnData;
}