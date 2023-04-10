============================================================
nvidia kernel module options provided by vgpu unlock patcher
============================================================

As the patching has been moved to runtime, there are new parameters
that can be used to tune the patches applied.

The options can be specified using a .conf file in /etc/modprobe.d
or put into linux kernel's command line configured in bootloader,
for example:

* using /etc/modprobe.d/nv.conf:
  ``options nvidia vgpukvm=1``
* or in kernel command line:
  ``nvidia.vgpukvm=1``

There are three kinds of options:
=================================

1. provided by source level patches

   * ``vgpukvm`` and ``kmalimit``
     these two options are provided by vgpu-kvm-optional-vgpu-v2.patch

     the kmalimit is rather for testing, so do not use that unless
     you know what are you doing

     vgpukvm allows to disable vgpu-kvm mode of host driver, basically
     switching it back to behavior of normal consumer desktop driver,
     particularly when using the "merged" variant of patched driver

     this may be used to create secondary boot entry within bootloader
     in order to boot without vgpu support, allowing easy "switching"
     of nvidia driver without reinstall of any files

   * ``nvprnfilter``
     basic filter for nvrm logs, to be used with verbose nvrm logging
     for debugging, like for example:
     ``options nvidia NVreg_ResmanDebugLevel=0 nvprnfilter=1``

2. provided by custom hooks into nvidia blob

   * ``cudahost``
     this was used with older versions of the vgpu unlock patcher
     to experimentally enable cuda support on host with vgpu kvm merged
     driver - now this option defaults to 0 with not merged driver
     and to 1 with a merged one, so you can use it to disable cuda with
     merged driver in case of some problems

   * ``vupdevid``
     this is mainly for testing override of pci devid, most of the time
     it does not need to be touched

   * ``klogtrace``
     this can be used to enable tracing of nvidia blob code, may be useful
     for development or for bugs analysis for example when comparing traces
     of a working case with a not working case

3. provided by runtime binary patches of the blob

   * ``vup_vgpusig``
     based on patch from mbuchel to disable vgpu config signature check

   * ``vup_kunlock``
     kernel driver level method of vgpu unlock which does not need any
     ioctl hooks in userspace

   * ``vup_merged``
     controls the patch to enable display output on host with merged driver

   * ``vup_qmode``
     optionally switch off the qmode

   * ``vup_sunlock``
     based on patch from LIL'pingu fixing xid 43 crashes when running
     vgpu with consumer cards (not needed when vup_kunlock is used)

Be careful when trying to tune the above options, usually the defaults
work best already and they may rather be useful for analysis of some problems.
