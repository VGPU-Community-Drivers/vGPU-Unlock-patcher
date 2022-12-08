# vGPU-Unlock-patcher
Solution to patch vGPU_Unlock into Nvidia Driver

## Usage

1. Pre-download original `.run` files:
   | Name | Version | Links |
   | ----------- | ----------- | ----------- |
   | NVIDIA-GRID-Linux-KVM-525.60.12-525.60.13-527.41 | Grid v15.0 | [Nvidia Download](https://enterprise-support.nvidia.com/s/login/?startURL=%2Fs%2F%3Ft%3D1657093205198), [Google Drive](https://drive.google.com/drive/folders/1Mwk0diSegzHx-7BeJdujPa1Vgyw5fd3s), [GitHub](https://github.com/VGPU-Community-Drivers/NV-VGPU-Driver/releases2) |

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

- `cudahost=1` nvidia module option of merged driver now works with all versions, enables also raytracing on host
- multiple fixes and tuning in the default profiles for rtx 2070+
- simplified patching focusing only on vgpu kvm blob, split the patch for merged driver extension
- supports setup of general-merge converting grid variant to general in case general run file is not available
- multiple versions in branches: 460.107, 470.141, 510.73, 510.85
- with 460.107 version ray tracing works on host with general-merge driver even with windows VMs

### Other Options 

`--repack` option that can be used to create unlocked/patched `.run` file (usually not necessary as you can simply start nvidia-installer from the directory).

### Credits
- Thanks to the discord user @mbuchel for the experimental patches
- Thanks to the discord user @LIL'pingu for the extended 43 crash fix
- Special thanks to @DualCoder without his work (vGPU_Unlock) we would not be here
- and thanks to the discord user @snowman for creating this patcher
