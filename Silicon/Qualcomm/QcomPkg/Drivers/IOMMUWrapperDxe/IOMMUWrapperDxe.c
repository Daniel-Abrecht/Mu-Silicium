#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/IoMmu.h>

// BUG: Currently, this doens't actually set up the mmu, it just assumes physical = virtual address and that all devices
// can access the memory. This is very likely very wrong, and the devices may not actually see that memory, but
// doing this correctly is hard, and I didn't get top do this properly yet. I'd probably need to use HALIOMMUDxe for this.
// There are some more issues wwith this very protocol / api, but let's keep that aside for now.

/**
  @param [in]      This            Pointer to the IOMMU protocol instance.
  @param [in]      Operation       The type of IOMMU operation.
  @param [in]      HostAddress     The host address to map.
  @param [in, out] NumberOfBytes   On input, the number of bytes to map. On output, the number of bytes mapped.
  @param [out]     DeviceAddress   The resulting device address.
  @param [out]     Mapping         A handle to the mapping.
**/
static EFI_STATUS EFIAPI IoMmuMap(
  IN     EDKII_IOMMU_PROTOCOL   *This,
  IN     EDKII_IOMMU_OPERATION  Operation,
  IN     VOID                   *HostAddress,
  IN OUT UINTN                  *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS   *DeviceAddress,
  OUT    VOID                   **Mapping
){
  *DeviceAddress = (EFI_PHYSICAL_ADDRESS)HostAddress;
  *Mapping = (VOID*)1;
  return EFI_SUCCESS;
}

/**
  @param [in]  This      Pointer to the IOMMU protocol instance.
  @param [in]  Mapping   The mapping to unmap.
**/
static EFI_STATUS EFIAPI IoMmuUnmap(
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  VOID                  *Mapping
){
  return EFI_SUCCESS;
}

/**
  @param [in]      This          Pointer to the IOMMU protocol instance.
  @param [in]      Type          The type of allocation to perform.
  @param [in]      MemoryType    The type of memory to allocate.
  @param [in]      Pages         The number of pages to allocate.
  @param [in, out] HostAddress   On input, the desired host address. On output, the allocated host address.
  @param [in]      Attributes    The memory attributes to use for the allocation.
**/
static EFI_STATUS EFIAPI IoMmuAllocateBuffer(
  IN     EDKII_IOMMU_PROTOCOL  *This,
  IN     EFI_ALLOCATE_TYPE     Type,
  IN     EFI_MEMORY_TYPE       MemoryType,
  IN     UINTN                 Pages,
  IN OUT VOID                  **HostAddress,
  IN     UINT64                Attributes
){
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  PhysicalAddress;

  if((This == NULL) || (Pages == 0) || (HostAddress == NULL)){
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto End;
  }

  if((Attributes & EDKII_IOMMU_ATTRIBUTE_DUAL_ADDRESS_CYCLE) == 0) {
    // Limit allocations to memory below 4GB
    PhysicalAddress = SIZE_4GB - 1;
    Type            = AllocateMaxAddress;
  }

  Status = gBS->AllocatePages(Type, MemoryType, Pages, &PhysicalAddress);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to allocate pages\n", __func__));
    goto End;
  }

  *HostAddress = (VOID *)(UINTN)PhysicalAddress;

End:
  ASSERT_EFI_ERROR(Status);
  return Status;
}

/**
  @param [in]  This          Pointer to the IOMMU protocol instance.
  @param [in]  Pages         The number of pages to free.
  @param [in]  HostAddress   The host address to free.
**/
static EFI_STATUS EFIAPI IoMmuFreeBuffer(
  IN  EDKII_IOMMU_PROTOCOL  *This,
  IN  UINTN                 Pages,
  IN  VOID                  *HostAddress
){
  EFI_STATUS  Status;

  if((This == NULL) || (HostAddress == NULL) || (Pages == 0)){
    DEBUG((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto End;
  }

  Status = gBS->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)HostAddress, Pages);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to free pages\n", __func__));
    goto End;
  }

End:
  ASSERT_EFI_ERROR(Status);
  return Status;
}


/**
  @param [in]  This          Pointer to the IOMMU protocol instance.
  @param [in]  DeviceHandle  The device handle to set attributes for.
  @param [in]  Mapping       The mapping to set attributes for.
  @param [in]  IoMmuAccess   The IOMMU access attributes for R/W.
**/
static EFI_STATUS EFIAPI IoMmuSetAttribute(
  IN EDKII_IOMMU_PROTOCOL  *This,
  IN EFI_HANDLE            DeviceHandle,
  IN VOID                  *Mapping,
  IN UINT64                IoMmuAccess
  )
{
  EFI_STATUS      Status = 0;

  if((This == NULL) || (Mapping == NULL) || ((IoMmuAccess & ~(EDKII_IOMMU_ACCESS_READ | EDKII_IOMMU_ACCESS_WRITE)) != 0)){
    DEBUG ((DEBUG_ERROR, "%a: Invalid parameter\n", __func__));
    Status = EFI_INVALID_PARAMETER;
    goto End;
  }

End:
  ASSERT_EFI_ERROR(Status);
  return Status;
}

static EDKII_IOMMU_PROTOCOL IoMmu = {
  EDKII_IOMMU_PROTOCOL_REVISION,
  .SetAttribute = IoMmuSetAttribute,
  .Map = IoMmuMap,
  .Unmap = IoMmuUnmap,
  .AllocateBuffer = IoMmuAllocateBuffer,
  .FreeBuffer = IoMmuFreeBuffer,
};

EFI_STATUS EFIAPI EntryPoint(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
){
  EFI_STATUS  Status;

  Status = gBS->InstallMultipleProtocolInterfaces(&ImageHandle, &gEdkiiIoMmuProtocolGuid, &IoMmu, NULL);
  if(EFI_ERROR(Status)){
    DEBUG ((DEBUG_ERROR, "%a: Failed to install gEdkiiIoMmuProtocolGuid\n", __func__));
  }

  return Status;
}
