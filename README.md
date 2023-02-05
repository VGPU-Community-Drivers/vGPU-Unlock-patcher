# vGPU-Unlock-patcher
Solution to patch vGPU_Unlock into Nvidia Driver

**_Kernel Support Update 2022-12-13::** 
Tested to work with 6.0.11 linux kernel, it may work with kernels
since v5.19-rc4-38-g34a255e67615 and possibly with kernels older
than v5.18-rc6-74-g8e432bb015b6, but not with versions in between these
two, it would need another build conditional for 5.18!
The update was also tested on 5.10 Kernel!

**_Support:_** [Join VGPU-Unlock discord for Support](https://discord.com/invite/5rQsSV3Byq)

## Usage

1. Pre-download original `.run` files:
   | Name | Version | Links |
   | ----------- | ----------- | ----------- |
   | NVIDIA-GRID-Linux-KVM-525.85.07-525.85.05-528.24 | Grid v15.1 | [Nvidia Download](https://enterprise-support.nvidia.com/s/login/?startURL=%2Fs%2F%3Ft%3D1657093205198), [Google Drive](https://drive.google.com/drive/folders/1Mwk0diSegzHx-7BeJdujPa1Vgyw5fd3s) |

2. Setup Spoofing - edit `patch.sh` file and search
   ```
   if $DO_VGPU; then
       applypatch ${TARGET} vcfg-testing.patch
       vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000
   fi
   ```
   Change the ID's `0x1E30 0x12BA 0x1E84 0x0000` to a matching set
   here:  
   `0x1E30 0x12BA` Quadro RTX 6000 to  
   `0x1E84 0x0000` RTX 2070 Super  

   E.g. P40 to 1080Ti:  
   `0x1B38 0x11D9` Tesla P40 to  
   `0x1B06 0x120F` 1080Ti  
   The new vcfgclone line in this example:  
   `vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x11D9 0x1B06 0x120F`

3. Run one of these commands:  
   `./patch.sh general-merge` creates a merge vgpu-kvm with consumer driver  
   `./patch.sh vgpu-kvm` just patch the vgpu-kvm driver (in case you use secondary gpu) #proxmox  
   `./patch.sh grid-merge` creates a merge vgpu-kvm with VGPU guest driver  
   `./patch.sh grid`  
   `./patch.sh general`  
   `./patch.sh vcfg`

## Changelog

- added `--docker-hack` option to fix `nvidia-docker-toolkit` not being able to detect the correct libraries
- `cudahost=1` nvidia module option of merged driver now works with all versions, enables also raytracing on host
- multiple fixes and tuning in the default profiles for rtx 2070+
- simplified patching focusing only on vgpu kvm blob, split the patch for merged driver extension
- supports setup of general-merge converting grid variant to general in case general run file is not available
- multiple versions in branches: 460.107, 470.141, 510.73, 510.85
- with 460.107 version ray tracing works on host with general-merge driver even with windows VMs

### Other Options 

`--repack` option that can be used to create unlocked/patched `.run` file (usually not necessary as you can simply start nvidia-installer from the directory).
`--docker-hack` option to patch the driver version to make it compatible with the `nvidia-docker-toolkit` so vGPU functionality and Docker can be used simultaneously with a merged driver

### Credits
- Thanks to the discord user @mbuchel for the experimental patches
- Thanks to the discord user @LIL'pingu for the extended 43 crash fix
- Special thanks to @DualCoder without his work (vGPU_Unlock) we would not be here
- and thanks to the discord user @snowman for creating this patcher
