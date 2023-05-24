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

typedef struct vgpucfg_new_t {
    uint32_t gpu_type;
    char card_name[32];
    char vgpu_type[32];
    uint8_t signature[128];
    char features[128];
    uint32_t max_instances;
    uint32_t num_displays;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t max_pixels;
    uint32_t frl_config;
    uint32_t cuda_enabled;
    uint32_t ecc_supported;
    uint32_t mig_instance_size;
    uint32_t multi_vgpu_supported;
    uint32_t pad1;
    uint64_t pci_id;
    uint64_t pci_device_id;
    uint64_t framebuffer;
    uint64_t mappable_video_size;
    uint64_t framebuffer_reservation;
    uint32_t encoder_capacity;
    uint32_t pad2;
    uint64_t bar1_length;
    uint32_t frl_enabled;
    char  adapterName[64];
    uint16_t adapterName_Unicode[64];
    char  shortGpuNameString[64];
    char  licensedProductName[128];
    uint32_t vgpuExtraParams[1024];
    uint32_t ftraceEnable;
    uint32_t gpuDirectSupported;
    uint32_t nvlinkP2PSupported;
    uint32_t gpuInstanceProfileId;
} __attribute__((packed)) vgpucfg_new_t;

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/blob/525.53/src/common/sdk/nvidia/inc/ctrl/ctrla081.h
 */

typedef uint8_t NvU8;
typedef uint16_t NvU16;
typedef uint32_t NvU32;
typedef uint64_t NvU64;
typedef NvU8 NvBool;
typedef struct { NvU32 val; } NvHandle;
#define NVA081_VGPU_STRING_BUFFER_SIZE       32
#define NVA081_VGPU_SIGNATURE_SIZE           128
#define NV2080_GPU_MAX_NAME_STRING_LENGTH                  (0x0000040U)
#define NV_GRID_LICENSE_INFO_MAX_LENGTH                     (128)
#define NVA081_EXTRA_PARAMETERS_SIZE         1024
#define NV_DECLARE_ALIGNED(TYPE_VAR, ALIGN) TYPE_VAR __attribute__ ((aligned (ALIGN)))
typedef struct NVA081_CTRL_VGPU_INFO {
    // This structure should be in sync with NVA082_CTRL_CMD_HOST_VGPU_DEVICE_GET_VGPU_TYPE_INFO_PARAMS
    NvU32 vgpuType;
    NvU8  vgpuName[NVA081_VGPU_STRING_BUFFER_SIZE];
    NvU8  vgpuClass[NVA081_VGPU_STRING_BUFFER_SIZE];
    NvU8  vgpuSignature[NVA081_VGPU_SIGNATURE_SIZE];
    NvU8  license[NV_GRID_LICENSE_INFO_MAX_LENGTH];
    NvU32 maxInstance;
    NvU32 numHeads;
    NvU32 maxResolutionX;
    NvU32 maxResolutionY;
    NvU32 maxPixels;
    NvU32 frlConfig;
    NvU32 cudaEnabled;
    NvU32 eccSupported;
    NvU32 gpuInstanceSize;
    NvU32 multiVgpuSupported;
    NV_DECLARE_ALIGNED(NvU64 vdevId, 8);
    NV_DECLARE_ALIGNED(NvU64 pdevId, 8);
    NV_DECLARE_ALIGNED(NvU64 profileSize, 8);
    NV_DECLARE_ALIGNED(NvU64 fbLength, 8);
    NV_DECLARE_ALIGNED(NvU64 gspHeapSize, 8);
    NV_DECLARE_ALIGNED(NvU64 fbReservation, 8);
    NV_DECLARE_ALIGNED(NvU64 mappableVideoSize, 8);
    NvU32 encoderCapacity;
    NV_DECLARE_ALIGNED(NvU64 bar1Length, 8);
    NvU32 frlEnable;
    NvU8  adapterName[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvU16 adapterName_Unicode[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvU8  shortGpuNameString[NV2080_GPU_MAX_NAME_STRING_LENGTH];
    NvU8  licensedProductName[NV_GRID_LICENSE_INFO_MAX_LENGTH];
    NvU32 vgpuExtraParams[NVA081_EXTRA_PARAMETERS_SIZE];
    NvU32 ftraceEnable;
    NvU32 gpuDirectSupported;
    NvU32 nvlinkP2PSupported;
    NvU32 multiVgpuExclusive;
    NvU32 exclusiveType;
    NvU32 exclusiveSize;
    // used only by NVML
    NvU32 gpuInstanceProfileId;
} NVA081_CTRL_VGPU_INFO;

/*
 * https://github.com/NVIDIA/open-gpu-kernel-modules/blob/525.53/src/common/sdk/nvidia/inc/ctrl/ctrl90f1.h
 * https://github.com/NVIDIA/open-gpu-kernel-modules/blob/525.53/src/common/sdk/nvidia/inc/mmu_fmt_types.h
 */

#define GMMU_FMT_MAX_LEVELS  6U
#define MMU_FMT_MAX_SUB_LEVELS 2

typedef struct MMU_FMT_LEVEL_515 {
    NvU8   virtAddrBitLo;
    NvU8   virtAddrBitHi;
    NvU8   entrySize;
    NvBool bPageTable;
    NvU8   numSubLevels;
    NV_DECLARE_ALIGNED(struct MMU_FMT_LEVEL_515 *subLevels, 8);
} MMU_FMT_LEVEL_515;

typedef struct NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS_515 {
    NvHandle hSubDevice;
    NvU32    subDeviceId;
    NV_DECLARE_ALIGNED(NvU64 virtAddress, 8);
    NV_DECLARE_ALIGNED(NvU64 pageSize, 8);
    NvU32    numLevels;
    struct {
        NV_DECLARE_ALIGNED(struct MMU_FMT_LEVEL_515 *pFmt, 8);
        NV_DECLARE_ALIGNED(MMU_FMT_LEVEL_515 levelFmt, 8);
        NV_DECLARE_ALIGNED(MMU_FMT_LEVEL_515 sublevelFmt[MMU_FMT_MAX_SUB_LEVELS], 8);
        NV_DECLARE_ALIGNED(NvU64 physAddress, 8);
        NvU32 aperture;
        NV_DECLARE_ALIGNED(NvU64 size, 8);
    } levels[GMMU_FMT_MAX_LEVELS];
} NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS_515;

typedef struct MMU_FMT_LEVEL {
    NvU8   virtAddrBitLo;
    NvU8   virtAddrBitHi;
    NvU8   entrySize;
    NvBool bPageTable;
    NvU8   numSubLevels;
    NvU32  pageLevelIdTag;  // <<-- !! git diff 515.43.04..525.53 -- src/common/sdk/nvidia/inc/mmu_fmt_types.h
    NV_DECLARE_ALIGNED(struct MMU_FMT_LEVEL *subLevels, 8);
} MMU_FMT_LEVEL;

typedef struct NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS {
    NvHandle hSubDevice;
    NvU32    subDeviceId;
    NV_DECLARE_ALIGNED(NvU64 virtAddress, 8);
    NV_DECLARE_ALIGNED(NvU64 pageSize, 8);
    NvU32    numLevels;
    struct {
        NV_DECLARE_ALIGNED(struct MMU_FMT_LEVEL *pFmt, 8);
        NV_DECLARE_ALIGNED(MMU_FMT_LEVEL levelFmt, 8);
        NV_DECLARE_ALIGNED(MMU_FMT_LEVEL sublevelFmt[MMU_FMT_MAX_SUB_LEVELS], 8);
        NV_DECLARE_ALIGNED(NvU64 physAddress, 8);
        NvU32 aperture;
        NV_DECLARE_ALIGNED(NvU64 size, 8);
    } levels[GMMU_FMT_MAX_LEVELS];
} NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS;

static void copy_mmu_fmt_level(MMU_FMT_LEVEL *dst,
                               MMU_FMT_LEVEL_515 *src,
                               NvU32 plitag)
{
    if (!src->virtAddrBitLo && !src->virtAddrBitHi && !src->entrySize
        && !src->bPageTable && !src->numSubLevels && !src->subLevels)
    {
        plitag = 0;
    }
    dst->virtAddrBitLo = src->virtAddrBitLo;
    dst->virtAddrBitHi = src->virtAddrBitHi;
    dst->entrySize = src->entrySize;
    dst->bPageTable = src->bPageTable;
    dst->numSubLevels = src->numSubLevels;
    dst->pageLevelIdTag = plitag;
    dst->subLevels = (void *)src->subLevels;
}

#define DO_FIXUPS
static int dbg_trace_level = 0;

static uint32_t ioctl_fixups[][3] = {
#ifdef DO_FIXUPS
    { 0xa0810101, 0x1368, 0x1348 },
    { 0x20800170, 0x00fc, 0x00d4 },
    { 0x20801228, 0x01b0, 0x01a8 },
    { 0x20802a0a, 0x0044, 0x0028 },
    { 0xa0810103, 0x1360, 0x1348 },
    { 0x2080012f, 0x03d8, 0x0388 },
    { 0x20800a2a, 0x0cc0, 0x0c80 },
    { 0x20800a26, 0x0940, 0x0900 },
    { 0x00800292, 0x0284, 0x01d4 },
    { 0x90f10102, 0x0290, 0x0200 },

    // ctrl/ctrl2080/ctrl2080perf.h:#define NV2080_CTRL_CMD_PERF_GET_GPUMON_PERFMON_UTIL_SAMPLES_V2 (0x20802096)
    //{ 0x20802096, 0x84d0, 0xb870 },    // even with additional fix it looks like working, it fails later
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

static void dbglog_vgpuinfo(NVA081_CTRL_VGPU_INFO *vi)
{
    syslog(LOG_INFO, "vgpunfo vgpuType: 0x%x\n", vi->vgpuType);
    syslog(LOG_INFO, "vgpunfo vgpuName: %s\n", vi->vgpuName);
    syslog(LOG_INFO, "vgpunfo vgpuClass: %s\n", vi->vgpuClass);
    syslog(LOG_INFO, "vgpunfo vgpuSignature: %02x %02x %02x %02x ...\n",
        vi->vgpuSignature[0], vi->vgpuSignature[1], vi->vgpuSignature[2], vi->vgpuSignature[3]);
    syslog(LOG_INFO, "vgpunfo license: %s\n", vi->license);
    syslog(LOG_INFO, "vgpunfo maxInstance: %d\n", vi->maxInstance);
    syslog(LOG_INFO, "vgpunfo numHeads: %d\n", vi->numHeads);
    syslog(LOG_INFO, "vgpunfo maxResolutionX: %d\n", vi->maxResolutionX);
    syslog(LOG_INFO, "vgpunfo maxResolutionY: %d\n", vi->maxResolutionY);
    syslog(LOG_INFO, "vgpunfo maxPixels: %d\n", vi->maxPixels);
    syslog(LOG_INFO, "vgpunfo frlConfig: 0x%02x\n", vi->frlConfig);
    syslog(LOG_INFO, "vgpunfo cudaEnabled: %d\n", vi->cudaEnabled);
    syslog(LOG_INFO, "vgpunfo eccSupported: %d\n", vi->eccSupported);
    syslog(LOG_INFO, "vgpunfo gpuInstanceSize: %d\n", vi->gpuInstanceSize);
    syslog(LOG_INFO, "vgpunfo multiVgpuSupported: %d\n", vi->multiVgpuSupported);
    syslog(LOG_INFO, "vgpunfo vdevId: 0x%08lx\n", vi->vdevId);
    syslog(LOG_INFO, "vgpunfo pdevId: 0x%08lx\n", vi->pdevId);
    syslog(LOG_INFO, "vgpunfo profileSize: 0x%08lx\n", vi->profileSize);
    syslog(LOG_INFO, "vgpunfo fbLength: 0x%08lx\n", vi->fbLength);
    syslog(LOG_INFO, "vgpunfo gspHeapSize: 0x%08lx\n", vi->gspHeapSize);
    syslog(LOG_INFO, "vgpunfo fbReservation: 0x%08lx\n", vi->fbReservation);
    syslog(LOG_INFO, "vgpunfo mappableVideoSize: 0x%08lx\n", vi->mappableVideoSize);
    syslog(LOG_INFO, "vgpunfo encoderCapacity: 0x%02x\n", vi->encoderCapacity);
    syslog(LOG_INFO, "vgpunfo bar1Length: 0x%02lx\n", vi->bar1Length);
    syslog(LOG_INFO, "vgpunfo frlEnable: %d\n", vi->frlEnable);

    syslog(LOG_INFO, "vgpunfo adapterName: %s\n", vi->adapterName);
    syslog(LOG_INFO, "vgpunfo adapterName_Unicode: %04x %04x ...\n",
        vi->adapterName_Unicode[0], vi->adapterName_Unicode[1]);
    syslog(LOG_INFO, "vgpunfo shortGpuNameString: %s\n", vi->shortGpuNameString);
    syslog(LOG_INFO, "vgpunfo licensedProductName: %s\n", vi->licensedProductName);
    syslog(LOG_INFO, "vgpunfo vgpuExtraParams: %08x %08x ...\n",
        vi->vgpuExtraParams[0], vi->vgpuExtraParams[1]);
    syslog(LOG_INFO, "vgpunfo ftraceEnable: %d\n", vi->ftraceEnable);

    syslog(LOG_INFO, "vgpunfo gpuDirectSupported: %d\n", vi->gpuDirectSupported);
    syslog(LOG_INFO, "vgpunfo nvlinkP2PSupported: %d\n", vi->nvlinkP2PSupported);
    syslog(LOG_INFO, "vgpunfo multiVgpuExclusive: %d\n", vi->multiVgpuExclusive);
    syslog(LOG_INFO, "vgpunfo exclusiveType: %d\n", vi->exclusiveType);
    syslog(LOG_INFO, "vgpunfo exclusiveSize: 0x%08x\n", vi->exclusiveSize);
    syslog(LOG_INFO, "vgpunfo gpuInstanceProfileId: %d\n", vi->gpuInstanceProfileId);
}

static void dbglog_vgpucfg(vgpucfg_new_t *cfg)
{
    syslog(LOG_INFO, "vgpucfg gpu_type: 0x%x\n", cfg->gpu_type);
    syslog(LOG_INFO, "vgpucfg card_name: %s\n", cfg->card_name);
    syslog(LOG_INFO, "vgpucfg vgpu_type: %s\n", cfg->vgpu_type);
    syslog(LOG_INFO, "vgpucfg features: %s\n", cfg->features);
    syslog(LOG_INFO, "vgpucfg max_instances: %d\n", cfg->max_instances);
    syslog(LOG_INFO, "vgpucfg num_displays: %d\n", cfg->num_displays);
    syslog(LOG_INFO, "vgpucfg display_width: %d\n", cfg->display_width);
    syslog(LOG_INFO, "vgpucfg display_height: %d\n", cfg->display_height);
    syslog(LOG_INFO, "vgpucfg max_pixels: %d\n", cfg->max_pixels);
    syslog(LOG_INFO, "vgpucfg frl_config: 0x%02x\n", cfg->frl_config);
    syslog(LOG_INFO, "vgpucfg cuda_enabled: %d\n", cfg->cuda_enabled);
    syslog(LOG_INFO, "vgpucfg ecc_supported: %d\n", cfg->ecc_supported);
    syslog(LOG_INFO, "vgpucfg mig_instance_size: %d\n", cfg->mig_instance_size);
    syslog(LOG_INFO, "vgpucfg multi_vgpu_supported: %d\n", cfg->multi_vgpu_supported);
    syslog(LOG_INFO, "vgpucfg pci_id: 0x%08lx\n", cfg->pci_id);
    syslog(LOG_INFO, "vgpucfg pci_device_id: 0x%08lx\n", cfg->pci_device_id);
    syslog(LOG_INFO, "vgpucfg framebuffer: 0x%08lx\n", cfg->framebuffer);
    syslog(LOG_INFO, "vgpucfg mappable_video_size: 0x%08lx\n", cfg->mappable_video_size);
    syslog(LOG_INFO, "vgpucfg framebuffer_reservation: 0x%08lx\n",
        cfg->framebuffer_reservation);
    syslog(LOG_INFO, "vgpucfg encoder_capacity: 0x%02x\n", cfg->encoder_capacity);
    syslog(LOG_INFO, "vgpucfg bar1_length: 0x%02lx\n", cfg->bar1_length);
    syslog(LOG_INFO, "vgpucfg frl_enabled: %d\n", cfg->frl_enabled);
    syslog(LOG_INFO, "vgpucfg adapterName: %s\n", cfg->adapterName);
}

int ioctl(int fd, int request, void *data)
{
  static ioctl_t real_ioctl = 0;
  void *old_buff = NULL;
  void *new_buff = NULL;
  iodata_t *iodata;
  int ret;
  int i, j;
  uint32_t *iofp;

  if (!real_ioctl)
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");

  iofp = NULL;

  if ((uint32_t)request == REQ_QUERY_GPU) {
    iodata = (iodata_t *)data;

    if (dbg_trace_level >= 3)
        syslog(LOG_INFO, "ioctl_request op_type 0x%x op_size 0x%02x\n", iodata->op_type, iodata->op_size);
    if (dbg_trace_level >= 4)
        dbgdump(NULL, request, iodata->op_type, iodata->result, iodata->op_size, NULL);

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
                if (iodata->op_type == 0x20801228)
                    (*(uint8_t *)iodata->result)--;
                if (iodata->op_type == 0x20802096 && ((uint32_t *)iodata->result)[0] == 0x02
                    && ((uint32_t *)iodata->result)[1] == iofp[1] - 0x10)
                {
                    // without this it fails with: VGPU message 60 failed, result code: 0x1f
                    ((uint32_t *)iodata->result)[1] = iofp[2] - 0x10;
                }
            }
            break;
        }
    }

    if (iodata -> op_type == 0xa0810101) {
#ifdef DO_FIXUPS
        static int show = 1;
        NVA081_CTRL_VGPU_INFO *vi = old_buff + 8;
        vgpucfg_new_t *cfg = new_buff + 8;

        if (show && dbg_trace_level >= 3) {
            dbglog_vgpuinfo(vi);
            syslog(LOG_INFO, "\n");
        }

        memset(cfg, 0, iodata->op_size - 8);
        memcpy(cfg, vi, offsetof(vgpucfg_new_t, max_instances));
        memcpy(&cfg->max_instances, &vi->maxInstance,
            offsetof(NVA081_CTRL_VGPU_INFO, vdevId)
                - offsetof(NVA081_CTRL_VGPU_INFO, maxInstance));
        cfg->pci_id = vi->vdevId;
        cfg->pci_device_id = vi->pdevId;
        cfg->framebuffer = vi->fbLength;
        cfg->mappable_video_size = vi->mappableVideoSize;
        cfg->framebuffer_reservation = vi->fbReservation;
        cfg->encoder_capacity = vi->encoderCapacity;
        cfg->bar1_length = vi->bar1Length;
        cfg->frl_enabled = vi->frlEnable;
        memcpy(&cfg->adapterName, &vi->adapterName,
            offsetof(NVA081_CTRL_VGPU_INFO, multiVgpuExclusive)
                - offsetof(NVA081_CTRL_VGPU_INFO, adapterName));
        cfg->gpuInstanceProfileId = vi->gpuInstanceProfileId;

        if (show && dbg_trace_level >= 3) {
            show = 0;
            dbglog_vgpucfg(cfg);
        }
#endif
    }
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
            if (iodata->op_type == 0xa0810101) {
                memcpy(old_buff, new_buff, 8);
            } else if (iodata->op_type == 0x20802a0a && iofp[1] > iofp[2]) {
                // ctrl/ctrl2080/ctrl2080ce.h:#define NV2080_CTRL_CMD_CE_GET_ALL_CAPS (0x20802a0a)
                // /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_CE_INTERFACE_ID << 8) | NV2080_CTRL_CE_GET_ALL_CAPS_PARAMS_MESSAGE_ID" */
                memcpy(old_buff, new_buff, iofp[2] - 4);
                memcpy(old_buff + iofp[1] - 4, new_buff + iofp[2] - 4, 4);
            } else if (iodata->op_type == 0xa0810103 && iofp[1] > iofp[2]) {
                // ctrl/ctrla081.h:#define NVA081_CTRL_CMD_VGPU_CONFIG_GET_VGPU_TYPE_INFO (0xa0810103)
                // /* finn: Evaluated from "(FINN_NVA081_VGPU_CONFIG_VGPU_CONFIG_INTERFACE_ID << 8) | NVA081_CTRL_VGPU_CONFIG_GET_VGPU_TYPE_INFO_PARAMS_MESSAGE_ID" */
                NVA081_CTRL_VGPU_INFO *vi = old_buff + 8;
                vgpucfg_new_t *cfg = new_buff + 8;

                if (dbg_trace_level >= 4)
                    dbgdump(&iodata->status, request, iodata->op_type, new_buff, iofp[2], "-orig");

                memset(vi, 0, iodata->op_size - 8);
                memcpy(vi, cfg, offsetof(vgpucfg_new_t, max_instances));
                memcpy(&vi->maxInstance, &cfg->max_instances, offsetof(NVA081_CTRL_VGPU_INFO, vdevId) - offsetof(NVA081_CTRL_VGPU_INFO, maxInstance));
                vi->vdevId = cfg->pci_id;
                vi->pdevId = cfg->pci_device_id;
                vi->fbLength = cfg->framebuffer;
                vi->mappableVideoSize = cfg->mappable_video_size;
                vi->fbReservation = cfg->framebuffer_reservation;
                vi->profileSize = vi->fbLength + vi->fbReservation;
                vi->encoderCapacity = cfg->encoder_capacity;
                vi->bar1Length = cfg->bar1_length;
                vi->frlEnable = cfg->frl_enabled;
                memcpy(&vi->adapterName, &cfg->adapterName, offsetof(NVA081_CTRL_VGPU_INFO, multiVgpuExclusive) - offsetof(NVA081_CTRL_VGPU_INFO, adapterName));
                vi->gpuInstanceProfileId = cfg->gpuInstanceProfileId;

                {
                    static int show = 1;
                    NVA081_CTRL_VGPU_INFO *vi = old_buff + 8;
                    vgpucfg_new_t *cfg = new_buff + 8;
                    if (show && dbg_trace_level >= 3) {
                        show = 0;
                        dbglog_vgpucfg(cfg);
                        syslog(LOG_INFO, "\n");
                        dbglog_vgpuinfo(vi);
                    }
                }
            } else if (iodata->op_type == 0x90f10102 && iofp[1] > iofp[2]) {
                // ctrl/ctrl90f1.h:#define NV90F1_CTRL_CMD_VASPACE_GET_PAGE_LEVEL_INFO (0x90f10102U)
                // /* finn: Evaluated from "(FINN_FERMI_VASPACE_A_VASPACE_INTERFACE_ID << 8) | NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS_MESSAGE_ID" */
                NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS_515 *vaold = (void *)new_buff;
                NV90F1_CTRL_VASPACE_GET_PAGE_LEVEL_INFO_PARAMS *vanew =  (void *)old_buff;
//                syslog(LOG_INFO, "page level info fixups vaold=%p\n", vaold);
                memset(vanew, 0, iofp[1]);
                vanew->hSubDevice = vaold->hSubDevice;
                vanew->subDeviceId = vaold->subDeviceId;
                vanew->virtAddress = vaold->virtAddress;
                vanew->pageSize = vaold->pageSize;
                vanew->numLevels = vaold->numLevels;
                for (i = 0; i < GMMU_FMT_MAX_LEVELS; i++) {
//                    syslog(LOG_INFO, "page level info fixups %d pFmt=%p &levelFmt=%p\n",
//                        i, vaold->levels[i].pFmt, &vaold->levels[i].levelFmt);
                    vanew->levels[i].pFmt = (void *)vaold->levels[i].pFmt;
//                    syslog(LOG_INFO, "page level info fixups %d levelFmt.subLevels=%p\n",
//                        i, vaold->levels[i].levelFmt.subLevels);
                    copy_mmu_fmt_level(&vanew->levels[i].levelFmt, &vaold->levels[i].levelFmt, i);
                    for (j = 0; j < MMU_FMT_MAX_SUB_LEVELS; j++) {
//                        syslog(LOG_INFO, "page level info fixups %d sublevelFmt[%d].subLevels=%p "
//                            "&vaold->levels[i].sublevelFmt[j]=%p\n",
//                            i, j, vaold->levels[i].sublevelFmt[j].subLevels,
//                            &vaold->levels[i].sublevelFmt[j]);
                        copy_mmu_fmt_level(&vanew->levels[i].sublevelFmt[j],
                            &vaold->levels[i].sublevelFmt[j], i + 1);
                    }
                    vanew->levels[i].physAddress = vaold->levels[i].physAddress;
                    vanew->levels[i].aperture = vaold->levels[i].aperture;
                    vanew->levels[i].size = vaold->levels[i].size;
                }
            } else if (iodata->op_type == 0x20802096 && ((uint32_t *)iodata->result)[0] == 0x02
                    && ((uint32_t *)iodata->result)[1] == iofp[2] - 0x10)
            {
                // ctrl/ctrl2080/ctrl2080perf.h:#define NV2080_CTRL_CMD_PERF_GET_GPUMON_PERFMON_UTIL_SAMPLES_V2 (0x20802096)
                // FIXME: did not check the returned stucture for incompatibilities!!
                //        it does not immediately fail, but later fails with many
                //        timeout and register read errors, so better to fail this one
                ((uint32_t *)iodata->result)[1] = iofp[1] - 0x10;
                memcpy(old_buff, new_buff, size);
            } else
                memcpy(old_buff, new_buff, size);
        } else
            syslog(LOG_ERR, "... ioctl_request op_type 0x%x op_size 0x%x -> 0x%x lost new_buff\n",
                iodata->op_type, iofp[1], iofp[2]);
        if (iodata->op_type == 0x20801228)
            (*(uint8_t *)iodata->result)++;
        free(new_buff);
  }
#endif

  if (dbg_trace_level >= 4)
    dbgdump(&iodata->status, request, iodata->op_type, iodata->result, iodata->op_size, NULL);

  // Call failed
  if (ret < 0) return ret;


  //Driver will try again
  if (iodata -> status == STATUS_TRY_AGAIN) return ret;

#ifdef DO_FIXUPS
    if (iodata->op_type == 0xa0840107 && iodata->status == 0x1f) {
        uint32_t *req = iodata->result;
        if ((req[0] == 0x03 || req[0] == 0x04) && req[1] == 0x02)
            iodata->status = STATUS_OK;
    }
#endif

  if(iodata->status != STATUS_OK) {
#ifdef DO_FIXUPS
    // Things seems to work fine even if some operations that fail
    // result in failed assertions. So here we change the status
    // value for these cases to cleanup the logs for nvidia-vgpu-mgr.
    if (
      //iodata->op_type == 0xA0820104 ||
      //iodata->op_type == 0xA082012C ||

      // op_type 0xa0810119 op_size 0x4: status=0x56
      // error: failed to set pGPU fractional multivgpu information: 56
      // error: failed to send vGPU configuration info to RM: 9
      iodata->op_type == 0xa0810119 ||

      // op_type 0x2080182b op_size 0x14: status=0x56
      // error: vmiop_log: Assertion Failed at 0x40d057c4:272
      // error: vmiop_log: (0x0): Failed to get c2c info
      // error: vmiop_log: (0x0): Failed to get C2C info
      // error: vmiop_log: (0x0): Initialization: Failed to get static information error 2
      // error: vmiop_log: (0x0): init_device_instance failed for inst 0 with error 2 (unable to setup host connection state)
      // error: vmiop_log: (0x0): Initialization: init_device_instance failed error 2
      // error: vmiop_log: display_init failed for inst: 0
      // ctrl2080/ctrl2080bus.h:#define NV2080_CTRL_CMD_BUS_GET_C2C_INFO (0x2080182b)
      // /* finn: Evaluated from "(FINN_NV20_SUBDEVICE_0_BUS_INTERFACE_ID << 8) | NV2080_CTRL_CMD_BUS_GET_C2C_INFO_PARAMS_MESSAGE_ID" */
      iodata->op_type == 0x2080182b ||

      //iodata->op_type == 0x90960103 ||
      //iodata->op_type == 0x90960101 ||
      0)
    {
        iodata->status = STATUS_OK;
    } else {
        if (dbg_trace_level >= 1)
            syslog(LOG_ERR, "op_type: 0x%08X failed, status 0x%X.", iodata->op_type, iodata->status);
    }
#endif
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
