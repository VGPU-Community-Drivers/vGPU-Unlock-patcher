#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>
#include <syslog.h>

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
  uint32_t unknown_4; // Set to 0x10 for READ_PCI_ID and set to 4 for
  // READ_DEV_TYPE prior to call.
  uint32_t status; // Written by ioctl call. See comment below.
}
iodata_t;

int ioctl(int fd, int request, void * data) {
  static ioctl_t real_ioctl = 0;
  if (!real_ioctl)
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");

  int ret = real_ioctl(fd, request, data);

  // Not a call we care about.
  if ((uint32_t) request != REQ_QUERY_GPU) return ret;

  // Call failed
  if (ret < 0) return ret;

  iodata_t * iodata = (iodata_t * ) data;

  //Driver will try again
  if (iodata -> status == STATUS_TRY_AGAIN) return ret;

  if (iodata -> op_type == OP_READ_PCI_ID) {
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
    if (0x1340 <= actual_devid && actual_devid <= 0x13bd ||
      0x174d <= actual_devid && actual_devid <= 0x179c) {
      spoofed_devid = 0x13bd; // Tesla M10
      spoofed_subsysid = 0x1160;
    }
    // Maxwell 2.0
    if (0x13c0 <= actual_devid && actual_devid <= 0x1436 ||
      0x1617 <= actual_devid && actual_devid <= 0x1667 ||
      0x17c2 <= actual_devid && actual_devid <= 0x17fd) {
      spoofed_devid = 0x13f2; // Tesla M60
    }
    // Pascal
    if (0x15f0 <= actual_devid && actual_devid <= 0x15f1 ||
      0x1b00 <= actual_devid && actual_devid <= 0x1d56 ||
      0x1725 <= actual_devid && actual_devid <= 0x172f) {
      spoofed_devid = 0x1b38; // Tesla P40
    }
    // GV100 Volta
    if (actual_devid == 0x1d81 || // TITAN V
      actual_devid == 0x1dba) { // Quadro GV100 32GB
      spoofed_devid = 0x1db6; // Tesla V100 32GB PCIE
    }
    // Turing
    if (0x1e02 <= actual_devid && actual_devid <= 0x1ff9 ||
      0x2182 <= actual_devid && actual_devid <= 0x21d1) {
      spoofed_devid = 0x1e30; // Quadro RTX 6000
      spoofed_subsysid = 0x12ba;
    }
    // Ampere
    if (0x2200 <= actual_devid && actual_devid <= 0x2600) {
      spoofed_devid = 0x2230; // RTX A6000
    }
    * devid_ptr = spoofed_devid;
    * subsysid_ptr = spoofed_subsysid;
  }

  if (iodata -> op_type == OP_READ_DEV_TYPE) {
    // Set device type to vGPU capable.
    uint64_t * dev_type_ptr = (uint64_t * ) iodata -> result;
    * dev_type_ptr = DEV_TYPE_VGPU_CAPABLE;
  }

  if(iodata->status != STATUS_OK) {
    // Things seems to work fine even if some operations that fail
    // result in failed assertions. So here we change the status
    // value for these cases to cleanup the logs for nvidia-vgpu-mgr.
    if(iodata->op_type == 0xA0820104 ||
      iodata->op_type == 0x90960103 ||
      iodata->op_type == 0x90960101) {
        iodata->op_type = STATUS_OK;
        } else {
          syslog(LOG_ERR, "op_type: 0x%08X failed, status 0x%X.", iodata->op_type, iodata->status);
      }
  }

  // Workaround for some Maxwell cards not supporting reading inforom.
  if (iodata -> op_type == 0x2080014b && iodata -> status == 0x56) {
    iodata -> status = 0x57;
  }

  return ret;
}
