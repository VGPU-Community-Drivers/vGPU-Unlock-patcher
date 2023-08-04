#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <asm/ioctl.h>

#define REQ_QUERY_GPU 0xC020462A

#define OP_READ_DEV_TYPE 0x800289 // *result type is uint64_t.
#define OP_READ_PCI_ID 0x20801801 // *result type is uint16_t[4], the second
// element (index 1) is the device ID, the
// forth element (index 3) is the subsystem
// ID.

#define DEV_TYPE_VGPU_CAPABLE 3

#define STATUS_OK 0
#define STATUS_TRY_AGAIN 3

typedef int( * ioctl_t)(int fd, int request, void * data);

typedef struct iodata_t {
  uint32_t unknown_1; // Initialized prior to call.
  uint32_t unknown_2; // Initialized prior to call.
  uint32_t op_type; // Operation type, see comment below.
  uint32_t padding_1; // Always set to 0 prior to call.
  void * result; // Pointer initialized prior to call.
  // Pointee initialized to 0 prior to call.
  // Pointee is written by ioctl call.
  uint32_t op_size; // Set to 0x10 for READ_PCI_ID and set to 4 for
  // READ_DEV_TYPE prior to call.
  uint32_t status; // Written by ioctl call. See comment below.
}
iodata_t;

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/kernel-open/common/inc/nv-ioctl-numa.h
 */

#if !defined(__aligned)
#define __aligned(n) __attribute__((aligned(n)))
#endif
#define NV_IOCTL_MAGIC      'F'
#define NV_IOCTL_BASE       200
#define NV_ESC_NUMA_INFO         (NV_IOCTL_BASE + 15)
#define NV_IOCTL_NUMA_INFO_MAX_OFFLINE_ADDRESSES 64
typedef struct offline_addresses
{
    uint64_t addresses[NV_IOCTL_NUMA_INFO_MAX_OFFLINE_ADDRESSES] __aligned(8);
    uint32_t numEntries;
} nv_offline_addresses_t;
/* per-device NUMA memory info as assigned by the system */
typedef struct nv_ioctl_numa_info
{
    int32_t  nid;
    int32_t  status;
    uint64_t memblock_size __aligned(8);
    uint64_t numa_mem_addr __aligned(8);
    uint64_t numa_mem_size __aligned(8);
    nv_offline_addresses_t offline_addresses __aligned(8);
} nv_ioctl_numa_info_t;
typedef struct nv_ioctl_numa_info_535
{
    int32_t  nid;
    int32_t  status;
    uint64_t memblock_size __aligned(8);
    uint64_t numa_mem_addr __aligned(8);
    uint64_t numa_mem_size __aligned(8);
    uint8_t  use_auto_online;             // <<-- !! git diff 515.43.04..535.43.02 -- kernel-open/common/inc/nv-ioctl-numa.h
    nv_offline_addresses_t offline_addresses __aligned(8);
} nv_ioctl_numa_info_t_535;


/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/nvtypes.h
 */
typedef uint8_t            NvU8;
typedef uint32_t           NvU32;
typedef uint32_t           NvV32;
typedef uint64_t           NvU64;
typedef void*              NvP64;
typedef struct { NvU32 val; } NvHandle;
typedef NvU8 NvBool;
#define NV_TRUE           ((NvBool)(0 == 0))
#define NV_FALSE          ((NvBool)(0 != 0))
#define NV_ALIGN_BYTES(size) __attribute__ ((aligned (size)))
#define NV_DECLARE_ALIGNED(TYPE_VAR, ALIGN) TYPE_VAR __attribute__ ((aligned (ALIGN)))


/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/nv_vgpu_types.h
 */

#define VM_UUID_SIZE            16

/* This enum represents types of VM identifiers */
typedef enum VM_ID_TYPE {
    VM_ID_DOMAIN_ID = 0,
    VM_ID_UUID = 1,
} VM_ID_TYPE;

/* This structure represents VM identifier */
typedef union VM_ID {
    NvU8 vmUuid[VM_UUID_SIZE];
    NV_DECLARE_ALIGNED(NvU64 vmId, 8);
} VM_ID;


/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080event.h
 */

#define NV2080_CTRL_CMD_EVENT_SET_GUEST_MSI (0x20800305) /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_EVENT_INTERFACE_ID << 8) | NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS_MESSAGE_ID" */

typedef struct NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS {
    NV_DECLARE_ALIGNED(NvU64 guestMSIAddr, 8);
    NvU32    guestMSIData;
    NvHandle hSemMemory;
    NvBool   isReset;
    VM_ID_TYPE vmIdType;                        // <<--
    NV_DECLARE_ALIGNED(VM_ID guestVmId, 8);     // <<--
} NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS;
// git diff -bB 535.43.02..535.54.03 -- src/common/sdk/nvidia/inc/ctrl/ctrl2080/ctrl2080event.h
typedef struct NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS_535 {
    NV_DECLARE_ALIGNED(NvU64 guestMSIAddr, 8);
    NvU32    guestMSIData;
    NvHandle hSemMemory;
    NvBool   isReset;
    NvU8     vgpuUuid[VM_UUID_SIZE];            // <<--
    NV_DECLARE_ALIGNED(NvU64 domainId, 8);      // <<--
} NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS_535;

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/src/nvidia/arch/nvalloc/unix/include/nv_escape.h
 */
#define NV_ESC_RM_ALLOC                             0x2B

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/src/common/sdk/nvidia/inc/nvos.h
 */
/* parameters */
typedef struct
{
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvV32    hClass;
    NvP64    pAllocParms NV_ALIGN_BYTES(8);
    NvV32    status;
} NVOS21_PARAMETERS;

typedef struct
{
    NvHandle hRoot;
    NvHandle hObjectParent;
    NvHandle hObjectNew;
    NvV32    hClass;
    NvP64    pAllocParms NV_ALIGN_BYTES(8);
    NvU32    paramsSize;  // <<-- !! git diff 530.30.02..535.43.02 -- src/common/sdk/nvidia/inc/nvos.h
    NvV32    status;
} NVOS21_PARAMETERS_535;

/* New struct with rights requested */
typedef struct
{
    NvHandle hRoot;                               // [IN] client handle
    NvHandle hObjectParent;                       // [IN] parent handle of new object
    NvHandle hObjectNew;                          // [INOUT] new object handle, 0 to generate
    NvV32    hClass;                              // [in] class num of new object
    NvP64    pAllocParms NV_ALIGN_BYTES(8);       // [IN] class-specific alloc parameters
    NvP64    pRightsRequested NV_ALIGN_BYTES(8);  // [IN] RS_ACCESS_MASK to request rights, or NULL
    NvU32    flags;                               // [IN] flags for FINN serialization
    NvV32    status;                              // [OUT] status
} NVOS64_PARAMETERS;

typedef struct
{
    NvHandle hRoot;                               // [IN] client handle
    NvHandle hObjectParent;                       // [IN] parent handle of new object
    NvHandle hObjectNew;                          // [INOUT] new object handle, 0 to generate
    NvV32    hClass;                              // [in] class num of new object
    NvP64    pAllocParms NV_ALIGN_BYTES(8);       // [IN] class-specific alloc parameters
    NvP64    pRightsRequested NV_ALIGN_BYTES(8);  // [IN] RS_ACCESS_MASK to request rights, or NULL
    NvU32    paramsSize;                          // [IN] Size of alloc params  // <<-- !! git diff 530.30.02..535.43.02 -- src/common/sdk/nvidia/inc/nvos.h
    NvU32    flags;                               // [IN] flags for FINN serialization
    NvV32    status;                              // [OUT] status
} NVOS64_PARAMETERS_535;



//#define DO_FIXUPS
static int dbg_trace_level = 0;
static volatile char spoof_devid_opt[] = "enable_spoof_devid=0";

static uint32_t ioctl_fixups[][3] = {
#ifdef DO_FIXUPS
    { 0x20800102, 0x001f4, 0x00204 },
    { 0x20801228, 0x001b0, 0x001c0 },
    { 0x2080a0a4, 0x1073c, 0x10740 },
    { 0x2080182B, 0x00014, 0x0001c },
    { 0xa0840101, 0x00104, 0x00100 },
    { 0x20800A2A, 0x00cc0, 0x00d40 },
    { 0x20800188, 0x00004, 0x00008 },
    { 0x20800305, 0x00028, 0x00030 },
#endif
    { 0, 0, 0 }
};

static const char BPATH[] = "/tmp/vgpu-traces";

static void dbgdump(uint32_t *pstatus, int request, uint32_t op_type, uint8_t *buff, uint32_t size, const char *tag)
{
    static int idx = -1;
    int fd;
    char fname[256];
    if (idx < 0) {
        if (idx == -2)
            return;
        else if (strcmp(program_invocation_short_name, "nvidia-vgpud") == 0)
            idx = 0;
        else if (strcmp(program_invocation_short_name, "nvidia-vgpu-mgr") == 0)
            idx = 0x100;
        else {
            idx = -2;
            return;
        }
        mkdir(BPATH, 0777);
    }
    if (tag == NULL)
        tag = "";
    if (pstatus == 0)
        snprintf(fname, sizeof(fname), "%s/%04x-%08x-%08x-%04x-req%s.bin", BPATH, idx, (uint32_t)request, op_type, size, tag);
    else
        snprintf(fname, sizeof(fname), "%s/%04x-%08x-%08x-%04x-res-%02x%s.bin", BPATH, idx, (uint32_t)request, op_type, size, *pstatus, tag);
    fd = creat(fname, 0666);
    if (fd >= 0) {
        if (size != write(fd, buff, size))
            syslog(LOG_INFO, "cvgpu dbgdump-%04x write error: %d\n", idx, errno);
        close(fd);
    }
    if (tag[0] == '\0')
        idx++;
}

int ioctl(int fd, int request, void *data)
{
  static ioctl_t real_ioctl = 0;
  static int spoof_devid_enabled = -1;
  void *old_buff = NULL;
  void *new_buff = NULL;
  iodata_t *iodata;
  int ret;
  int i;
  uint32_t *iofp;

  if (!real_ioctl)
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
  if (spoof_devid_enabled < 0)
    spoof_devid_enabled = spoof_devid_opt[strlen((char *)spoof_devid_opt) - 1] == '1' ? 1 : 0;

  iofp = NULL;

#ifdef DO_FIXUPS
  if ((uint32_t)request == _IOWR(NV_IOCTL_MAGIC, NV_ESC_NUMA_INFO, nv_ioctl_numa_info_t)) {
      nv_ioctl_numa_info_t_535 *ninfo_new;
      nv_ioctl_numa_info_t *ninfo_old = data;
      ninfo_new = malloc(sizeof(nv_ioctl_numa_info_t_535));
      if (ninfo_new != NULL) {
          ninfo_new->nid = ninfo_old->nid;
          ninfo_new->status = ninfo_old->status;
          ninfo_new->memblock_size = ninfo_old->memblock_size;
          ninfo_new->numa_mem_addr = ninfo_old->numa_mem_addr;
          ninfo_new->numa_mem_size = ninfo_old->numa_mem_size;
          ninfo_new->use_auto_online = 0;
          memcpy(&ninfo_new->offline_addresses, &ninfo_old->offline_addresses, sizeof(nv_offline_addresses_t));

          ret = real_ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_NUMA_INFO, nv_ioctl_numa_info_t_535), ninfo_new);

          ninfo_old->nid = ninfo_new->nid;
          ninfo_old->status = ninfo_new->status;
          ninfo_old->memblock_size = ninfo_new->memblock_size;
          ninfo_old->numa_mem_addr = ninfo_new->numa_mem_addr;
          ninfo_old->numa_mem_size = ninfo_new->numa_mem_size;
          memcpy(&ninfo_old->offline_addresses, &ninfo_new->offline_addresses, sizeof(nv_offline_addresses_t));
          free(ninfo_new);
          return ret;
      }
  }
#endif

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/generated/g_allclasses.h
 */
#define MAXWELL_CHANNEL_GPFIFO_A                 (0x0000b06f)
#define VOLTA_CHANNEL_GPFIFO_A                   (0x0000c36f)
#define PASCAL_CHANNEL_GPFIFO_A                  (0x0000c06f)
#define TURING_CHANNEL_GPFIFO_A                  (0x0000c46f)
#define AMPERE_CHANNEL_GPFIFO_A                  (0x0000c56f)
#define HOPPER_CHANNEL_GPFIFO_A                  (0x0000c86f)

  if ((uint32_t)request == _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS21_PARAMETERS_535)) {
      NVOS21_PARAMETERS_535 *np = data;
      void *op;
      if (dbg_trace_level >= 3)
          syslog(LOG_INFO, "ioctl_request NV_ESC_RM_ALLOC NVOS21 hClass=0x%04x pAllocParms=%p paramsSize=0x%02x\n", np->hClass, np->pAllocParms, np->paramsSize);
      switch (np->hClass) {
      case MAXWELL_CHANNEL_GPFIFO_A:
      case VOLTA_CHANNEL_GPFIFO_A:
      case PASCAL_CHANNEL_GPFIFO_A:
      case TURING_CHANNEL_GPFIFO_A:
      case AMPERE_CHANNEL_GPFIFO_A:
      case HOPPER_CHANNEL_GPFIFO_A:
          op = np->pAllocParms;
          np->pAllocParms = calloc(1, 0x130 + 4 * 3 * 2 + 4 * 8);
          // ^^-- !! git diff 535.54.03..535.86.05 -- src/common/sdk/nvidia/inc/alloc/alloc_channel.h
          //         libnvidia-vgpu.so.535.54.03: _nv009201vgpu(): memset(v28, 0, 0x130uLL);
          memcpy(np->pAllocParms, op, 0x130);
          ret = real_ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS21_PARAMETERS_535), np);
          memcpy(op, np->pAllocParms, 0x130);
          np->pAllocParms = op;
          break;
      default:
          ret = real_ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS21_PARAMETERS_535), np);
          break;
      }
      if (dbg_trace_level >= 3)
          syslog(np->status == 0 ? LOG_INFO : LOG_ERR, "\\_>> ret=%d paramsSize 0x%02x status=0x%02x\n", ret, np->paramsSize, np->status);
      return ret;
  }

  if ((uint32_t)request == _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS64_PARAMETERS_535)) {
      NVOS64_PARAMETERS_535 *np = data;
      void *op;
      if (dbg_trace_level >= 3)
          syslog(LOG_INFO, "ioctl_request NV_ESC_RM_ALLOC NVOS64 hClass=0x%04x pAllocParms=%p paramsSize=0x%02x\n", np->hClass, np->pAllocParms, np->paramsSize);
      switch (np->hClass) {
      case MAXWELL_CHANNEL_GPFIFO_A:
      case VOLTA_CHANNEL_GPFIFO_A:
      case PASCAL_CHANNEL_GPFIFO_A:
      case TURING_CHANNEL_GPFIFO_A:
      case AMPERE_CHANNEL_GPFIFO_A:
      case HOPPER_CHANNEL_GPFIFO_A:
          op = np->pAllocParms;
          np->pAllocParms = calloc(1, 0x130 + 4 * 3 * 2 + 4 * 8);
          // ^^-- !! git diff 535.54.03..535.86.05 -- src/common/sdk/nvidia/inc/alloc/alloc_channel.h
          //         libnvidia-vgpu.so.535.54.03: _nv009201vgpu(): memset(v28, 0, 0x130uLL);
          memcpy(np->pAllocParms, op, 0x130);
          ret = real_ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS64_PARAMETERS_535), np);
          memcpy(op, np->pAllocParms, 0x130);
          np->pAllocParms = op;
          break;
      default:
          ret = real_ioctl(fd, _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, NVOS64_PARAMETERS_535), np);
          break;
      }
      if (dbg_trace_level >= 3)
          syslog(np->status == 0 ? LOG_INFO : LOG_ERR, "\\_>> ret=%d paramsSize 0x%02x status=0x%02x\n", ret, np->paramsSize, np->status);
      return ret;
  }

  if ((uint32_t)request == REQ_QUERY_GPU) {
    iodata = (iodata_t *)data;

    if (dbg_trace_level >= 3)
        syslog(LOG_INFO, "ioctl_request op_type 0x%x op_size 0x%02x\n", iodata->op_type, iodata->op_size);

    for (i = 0; ioctl_fixups[i][0] != 0; i++) {
        if (iodata->op_type == ioctl_fixups[i][0] && iodata->op_size == ioctl_fixups[i][1]) {
            if (dbg_trace_level >= 2)
                syslog(LOG_INFO, "fixing ioctl_request op_type 0x%x op_size 0x%x -> 0x%x\n",
                    iodata->op_type, iodata->op_size, ioctl_fixups[i][2]);
            new_buff = malloc(ioctl_fixups[i][2]);
            if (new_buff != NULL) {
                iofp = &ioctl_fixups[i][0];
                old_buff = iodata->result;
                iodata->result = new_buff;
                if (iodata->op_size < iofp[2]) {
                    memcpy(iodata->result, old_buff, iodata->op_size);
                    memset((uint8_t *)iodata->result + iodata->op_size, 0, iofp[2] - iodata->op_size);
                } else {
                    // struct shrinked, very likely that additional fixups are needed
                    memcpy(iodata->result, old_buff, iofp[2]);
                    //memset(old_buff + iofp[2], 0, iodata->op_size - iofp[2]);
                }
                iodata->op_size = iofp[2];
                if (iodata->op_type == NV2080_CTRL_CMD_EVENT_SET_GUEST_MSI) {
                    NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS *old_msip = old_buff;
                    NV2080_CTRL_EVENT_SET_GUEST_MSI_PARAMS_535 *new_msip = new_buff;
                    if (dbg_trace_level >= 4)
                        dbgdump(NULL, request, iodata->op_type, old_buff, iofp[1], "-orig");
                    switch (old_msip->vmIdType) {
                    case VM_ID_DOMAIN_ID:
                        memset(&new_msip->vgpuUuid, 0, sizeof(new_msip->vgpuUuid));
                        new_msip->domainId = old_msip->guestVmId.vmId;
                        break;
                    case VM_ID_UUID:
                        memcpy(&new_msip->vgpuUuid, &old_msip->guestVmId.vmUuid, sizeof(new_msip->vgpuUuid));
                        new_msip->domainId = 0;
                        break;
                    }
                }
            }
            break;
        }
    }
    if (dbg_trace_level >= 4)
        dbgdump(NULL, request, iodata->op_type, iodata->result, iodata->op_size, NULL);
  }

  ret = real_ioctl(fd, request, data);

  // Not a call we care about.
  if ((uint32_t) request != REQ_QUERY_GPU) return ret;
  iodata = (iodata_t *)data;

  if (dbg_trace_level >= 3 || (iodata->status != 0 && dbg_trace_level >= 2))
      syslog(iodata->status == 0 ? LOG_INFO : LOG_ERR, "\\_>> op_type 0x%x op_size 0x%x: status=0x%02x\n",
            iodata->op_type, iodata->op_size, iodata->status);

#ifdef DO_FIXUPS
  if (iofp != NULL) {
        if (iodata->op_size == iofp[2])
            iodata->op_size = iofp[1];
        else
            syslog(LOG_ERR, "... ioctl_request op_type 0x%x op_size 0x%x -> 0x%x NOT MATCHING (0x%x)\n",
                iodata->op_type, iofp[1], iofp[2], iodata->op_size);
        if (iodata->result == new_buff) {
            uint32_t size = iofp[1] < iofp[2] ? iofp[1] : iofp[2];
            iodata->result = old_buff;
            if (iodata->op_type == NV2080_CTRL_CMD_EVENT_SET_GUEST_MSI) {
                // keep the content of the old request struct
            } else
                memcpy(old_buff, new_buff, size);
        } else
            syslog(LOG_ERR, "... ioctl_request op_type 0x%x op_size 0x%x -> 0x%x lost new_buff\n",
                iodata->op_type, iofp[1], iofp[2]);
        free(new_buff);
  }
#if 0
  if (iodata->op_type == 0x137) {
    int pos;
    char buf[256];
    uint32_t *ver;
    pos = 0;
    buf[0] = '\0';
    for (i = 0; i < iodata->op_size; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %02x", ((uint8_t *)iodata->result)[i]);
        if (pos >= sizeof(buf) - 1)
            break;
    }
    syslog(LOG_INFO, "RESULT: ioctl_request op_type 0x%x op_size 0x%x:%s\n",
          iodata->op_type, iodata->op_size, buf);
    ver = iodata->result;
    ver[0] = 0x070001;
    ver[1] = 0x110001;
    ver[2] = 0x070001;
    ver[3] = 0x110001;
    pos = 0;
    buf[0] = '\0';
    for (i = 0; i < iodata->op_size; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %02x", ((uint8_t *)iodata->result)[i]);
        if (pos >= sizeof(buf) - 1)
            break;
    }
    syslog(LOG_INFO, "FIXEDT: ioctl_request op_type 0x%x op_size 0x%x:%s\n",
          iodata->op_type, iodata->op_size, buf);
  }
#endif
#endif

  if (dbg_trace_level >= 4)
    dbgdump(&iodata->status, request, iodata->op_type, iodata->result, iodata->op_size, NULL);

  // Call failed
  if (ret < 0) return ret;

  //Driver will try again
  if (iodata -> status == STATUS_TRY_AGAIN) return ret;

  if (spoof_devid_enabled > 0 && iodata->op_type == OP_READ_PCI_ID) {
    // Lookup address of the device and subsystem IDs.
    uint16_t * devid_ptr = (uint16_t * ) iodata -> result + 1;
    uint16_t * subsysid_ptr = (uint16_t * ) iodata -> result + 3;
    // Now we replace the device ID with a spoofed value that needs to
    // be determined such that the spoofed value represents a GPU with
    // vGPU support that uses the same GPU chip as our actual GPU.
    uint16_t actual_devid = * devid_ptr;
    uint16_t spoofed_devid = actual_devid;
    uint16_t actual_subsysid = * subsysid_ptr;
    uint16_t spoofed_subsysid = actual_subsysid;

    // Maxwell
    if ((0x1340 <= actual_devid && actual_devid <= 0x13bd) ||
      (0x174d <= actual_devid && actual_devid <= 0x179c)) {
      spoofed_devid = 0x13bd; // Tesla M10
      spoofed_subsysid = 0x1160;
    }
    // Maxwell 2.0
    if ((0x13c0 <= actual_devid && actual_devid <= 0x1436) ||
      (0x1617 <= actual_devid && actual_devid <= 0x1667) ||
      (0x17c2 <= actual_devid && actual_devid <= 0x17fd)) {
      spoofed_devid = 0x13f2; // Tesla M60
    }
    // Pascal
    if ((0x15f0 <= actual_devid && actual_devid <= 0x15f1) ||
      (0x1b00 <= actual_devid && actual_devid <= 0x1d56) ||
      (0x1725 <= actual_devid && actual_devid <= 0x172f)) {
      spoofed_devid = 0x1b38; // Tesla P40
    }
    // GV100 Volta
    if (actual_devid == 0x1d81 || // TITAN V
      actual_devid == 0x1dba) { // Quadro GV100 32GB
      spoofed_devid = 0x1db6; // Tesla V100 32GB PCIE
    }
    // Turing
    if ((0x1e02 <= actual_devid && actual_devid <= 0x1ff9) ||
      (0x2182 <= actual_devid && actual_devid <= 0x21d1)) {
      spoofed_devid = 0x1e30; // Quadro RTX 6000
      spoofed_subsysid = 0x12ba;
      //spoofed_devid = 0x1e89; // GeForce RTX 2060 6GB
      //spoofed_subsysid = 0x134d;
      //spoofed_devid = 0x1e84; // GeForce RTX 2070 8GB
      //spoofed_subsysid = 0x134d;
    }
    // Ampere
    if (0x2200 <= actual_devid && actual_devid <= 0x2600) {
      spoofed_devid = 0x2230; // RTX A6000
    }
    * devid_ptr = spoofed_devid;
    * subsysid_ptr = spoofed_subsysid;
  }

  if (spoof_devid_enabled > 0 && iodata -> op_type == OP_READ_DEV_TYPE) {
    // Set device type to vGPU capable.
    uint64_t * dev_type_ptr = (uint64_t * ) iodata -> result;
    * dev_type_ptr = DEV_TYPE_VGPU_CAPABLE;
  }

  if(iodata->status != STATUS_OK) {
#ifdef DO_FIXUPS
    // Things seems to work fine even if some operations that fail
    // result in failed assertions. So here we change the status
    // value for these cases to cleanup the logs for nvidia-vgpu-mgr.
    if (
      //iodata->op_type == 0xA0820104 ||
      iodata->op_type == 0xA082012C ||
      iodata->op_type == NV2080_CTRL_CMD_EVENT_SET_GUEST_MSI ||    // still fails, we would need to set domainId probably with 535.54.03
      //iodata->op_type == 0x90960103 ||
      //iodata->op_type == 0x90960101 ||
      0)
    {
        iodata->status = STATUS_OK;
    } else
#endif
    {
        if (dbg_trace_level >= 1)
            syslog(LOG_ERR, "op_type: 0x%08X failed, status 0x%X.", iodata->op_type, iodata->status);
    }
  }

  // op_type 0x2080014b op_size 0x5: status=0x57
  // ctrl/ctrl2080/ctrl2080gpu.h:#define NV2080_CTRL_CMD_GPU_GET_INFOROM_OBJECT_VERSION (0x2080014bU)
  // /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_GPU_INTERFACE_ID << 8) | NV2080_CTRL_GPU_GET_INFOROM_OBJECT_VERSION_PARAMS_MESSAGE_ID" */
  // Workaround for some Maxwell cards not supporting reading inforom.
  if (iodata->op_type == 0x2080014b && iodata->status == 0x56) {
    iodata->status = 0x57;
  }

  return ret;
}
