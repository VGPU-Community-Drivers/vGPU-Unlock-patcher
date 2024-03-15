# vGPU-Unlock-patcher
A solution to patch vGPU_Unlock into nvidia driver, including possibility to create a merged one.

**_Support:_** [Join VGPU-Unlock discord for Support](https://discord.com/invite/5rQsSV3Byq)

This repository contains a submodule, so please clone this project recursively, i.e. using `git clone --recursive` command.

## Usage

1. download original vgpu kvm `.run` files as available in a grid package `.zip` release, use the version matching the name of chosen branch of this project (the latest one is recommended)
   - versions of vgpu releases are listed [here](https://docs.nvidia.com/grid/index.html)
   - downloads are available from nvidia site [here](https://nvid.nvidia.com/dashboard/), evaluation account may be obtained [here](https://www.nvidia.com/object/vgpu-evaluation.html)
   - if you need merged driver, download also standard linux desktop driver .run file, the version as set in GNRL in the beginning of the patch.sh script

2. optionally edit `patch.sh` file to add support for your gpu if it was not included yet
   - search for "vcfgclone" lines, like for example:
        ```shell
        vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000
        ```
     the first two hex numbers select officially supported gpu as listed in vgpuConfig.xml (which can be extracted from vgpu kvm .run file)
     
     the example above is for Quadro RTX 6000 listed in the xml file as following:
        ```xml
        <pgpu><devId deviceId="0x1E30" subsystemId="0x12BA"/></pgpu>
        ```
     (fields that are not interesting for this example have been omitted)
     
   - the "vcfgclone" line example above is adding support for RTX 2070 Super, which has 10de:1e84 pci device id (that you can find for your gpu via `lspci -nn` command), so we are using the device id part, the last number 0x0000 is subdevice id, which may be used to differentiate some specific models, usually not needed, so we can use zero number there

   - just try to match the gpu architecture when adding a vcfgclone line, i.e. clone an officially supported pascal gpu if your gpu is pascal based

   - another example would be adding support for GTX 1080 Ti by cloning Tesla P40:
     Tesla P40 has 10de:1b38 pci device id and 10de:11d9 subsystem device id, listed in the xml as
        ```shell
        <pgpu><devId deviceId="0x1B38" subsystemId="0x0"/></pgpu>
        ```
        
        while the 1080 Ti can have 10de:1b06 pci devid with 10de:120f subsystem id for example, so the new vcfgclone line would have the first two numbers from the xml, the third number pci dev id of the card to be added and the fourth number can be zero or subsystem id (0x120f):
        ```shell
        vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1B06 0x0000
        ```
        
   - when adding new vcfgclone lines, always refer into the xml to get the first two parameters and be sure to copy them case sensitively as the script searches for them as they are provided

3. Run one of these commands, depending on what you need:
      ```shell
      # a driver merged from vgpu-kvm with consumer driver (cuda and opengl for host too)
      ./patch.sh general-merge
      # display output on host not needed (proxmox) or you have secondary gpu
      ./patch.sh vgpu-kvm
      # driver for linux vm
      ./patch.sh grid
      # driver for linux vm functionally similar to grid one but using consumer .run as input
      ./patch.sh general
      # stuff for windows vm
      ./patch.sh wsys
      ```

4. Change directory into the output folder (ending with `-patched`) to install the patched driver:
      ```shell
      ./nvidia-installer --dkms
      ```
   If you have installed the driver before, the command above might fail, with the error log mentioning some services are still running. In that case, manually stop then with `systemctl stop nvidia*.service` and try again.

5. Optionally, if you want to override existing profiles, you can edit `/usr/share/nvidia/vgpu/vgpuConfig.xml`. The change will take effect after reboot or driver reloading.

   [Formula](https://discord.com/channels/829786927829745685/1162008346551926824/1171897739576086650) for overriding `profileSize` for X GiB of VRAM (X >= 1):
   ```
   profileSize = X * 0x40000000
   fbReservation = 0x8000000 + (profileSize - 0x40000000) / 0x10
   framebuffer = profileSize - fbReservation
   ```

   The minimal `profileSize` was [reported to be 384 MiB](https://discord.com/channels/829786927829745685/1162008346551926824/1168010185181249597). 

## Changelog
Please see commits history [here](https://github.com/VGPU-Community-Drivers/vGPU-Unlock-patcher/commits/).

### Other Options 
- any options need to be put before the target name
- `--repack` option that can be used to create unlocked/patched `.run` file (usually not necessary as you can simply start nvidia-installer from the directory).

### Credits
- Thanks to the discord user @mbuchel for the experimental patches
- Thanks to the discord user @LIL'pingu for the extended 43 crash fix
- Special thanks to @DualCoder without his work (vGPU_Unlock) we would not be here
- and thanks to the discord user @snowman for creating this patcher
