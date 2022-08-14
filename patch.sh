#!/bin/bash

GNRL="NVIDIA-Linux-x86_64-460.106.00"
VGPU="NVIDIA-Linux-x86_64-460.107-vgpu-kvm"
GRID="NVIDIA-Linux-x86_64-460.106.00-grid"

SPOOF=true
CUDAH=true
MIGRATION=true
OPTVGPU=true
REPACK=false
SWITCH_GRID_TO_GNRL=false

if [ ! -e "${GNRL}.run" -a -e "${GRID}.run" ]; then
    SWITCH_GRID_TO_GNRL=true
fi

VER_VGPU=`echo ${VGPU} | awk -F- '{print $4}'`
VER_GRID=`echo ${GRID} | awk -F- '{print $4}'`

CP="cp -a"

case `stat -f --format=%T .` in
    btrfs | xfs)
        CP="$CP --reflink"
        ;;
esac

VERIFY_BLOBPATCH=false
if [ -n "`which xxd`" ]; then
    VERIFY_BLOBPATCH=true
fi

vcfgclone() {
    echo "vcfgclone ${2#0x}:${3#0x} -> ${4#0x}:${5#0x}"
    sed -e '/<pgpu/ b found' -e b -e ':found' -e '/<\/pgpu>/ b clone' -e N -e 'b found' \
        -e ':clone' -e p -e "s/\(.* deviceId=\"\)${2}\(\" subsystemVendorId=\"\)0x10de\(\" subsystemId=\"\)${3}\(\".*\)/\1${4}\20x10de\3${5}\4/" -e t -e d -i ${1}
}

vcfgpatch() {
    echo "vcfgpatch ${2#0x}:${3#0x} -> ${4#0x}:${5#0x}"
    sed -e "s/\(.* deviceId=\"\)${2}\(\" subsystemVendorId=\"\)0x10de\(\" subsystemId=\"\)${3}\(\".*\)/\1${4}\20x10de\3${5}\4/" -i ${1}
}


DO_VGPU=false
DO_GRID=false
DO_MRGD=false

while [ $# -gt 0 -a "${1:0:2}" = "--" ]
do
    if [ "$1" = "--no-spoof" ]; then
        shift
        SPOOF=false
    fi
    if [ "$1" = "--no-host-cuda" ]; then
        shift
        CUDAH=false
    fi
    if [ "$1" = "--no-migration" ]; then
        shift
        MIGRATION=false
    fi
    if [ "$1" = "--no-opt-vgpu" ]; then
        shift
        OPTVGPU=false
    fi
    if [ "$1" = "--repack" ]; then
        shift
        REPACK=true
    fi
done

case "$1" in
    vgpu*)
        CUDAH=false
        DO_VGPU=true
        SOURCE="${VGPU}"
        TARGET="${VGPU}-patched"
        ;;
    grid-merge)
        DO_VGPU=true
        DO_GRID=true
        DO_MRGD=true
        MRGD="${GRID}-vgpu-kvm"
        SOURCE="${MRGD}"
        TARGET="${MRGD}-patched"
        SWITCH_GRID_TO_GNRL=false
        ;;
    general-merge)
        DO_VGPU=true
        DO_GRID=true
        DO_MRGD=true
        if $SWITCH_GRID_TO_GNRL; then
            echo "WARNING: did not find general driver, trying to create it from grid one"
            GNRL="${GRID/-grid}"
        else
            GRID="${GNRL}"
        fi
        MRGD="${GNRL}-merged-vgpu-kvm"
        SOURCE="${MRGD}"
        TARGET="${MRGD}-patched"
        ;;
    vcfg)
        shift
        [ $# -eq 5 -a -e "$1" ] || {
            echo "Usage: $0 vcfg xmlfile devid subdevid cdevid csubdevid"
            exit 1
        }
        vcfgclone "$@"
        #vcfgpatch "$@"
        exit $?
        ;;
    *)
        echo "Usage: $0 [options] <vgpu-kvm | grid-merge | general-merge | vcfg>"
        exit 1
        ;;
esac

VER_TARGET=`echo ${SOURCE} | awk -F- '{print $4}'`
VER_BLOB=`echo ${VGPU} | awk -F- '{print $4}'`

die() {
    echo "$@"
    exit 1
}

extract() {
    [ -e ${1} ] || die "package ${1} not found"
    TDIR="${2}"
    if [ -z "${TDIR}" ]; then
        TDIR="${1%.run}"
    fi
    if [ -d ${TDIR} ]; then
        echo "WARNING: skipping extract of ${1} as it seems already extracted in ${TDIR}"
        return 0
    fi
    $REPACK && sh ${1} --lsm > ${TARGET}.lsm
    sh ${1} --extract-only --target ${TDIR}
    chmod -R u+w ${TDIR}
}

blobpatch_byte() {
    echo -n "blobpatch ${2}: ${3} ${4}"
    CHK=""
    if $VERIFY_BLOBPATCH; then
        CHK=$(dd if=${1} skip=`printf "%d" 0x${2}` bs=1 count=1 2>/dev/null | xxd -u -i | sed -e 's/ *0[xX]//')
    fi
    if [ -z "${CHK}" -o "${CHK}" = "${3}" ]; then
        echo -e -n "\x${4}" | dd of=${1} seek=`printf "%d" 0x${2}` bs=1 count=1 conv=notrunc &>/dev/null
        if [ -n "${CHK}" ]; then
            echo " [x]"
        else
            echo
        fi
    else
        echo
        die "blobpatch failed: expected ${3} got ${CHK} instead"
    fi
}

blobpatch() {
    echo "blobpatch ${2}"
    local status=2
    while read addr a b && [ -n "${addr}" ]
    do
        if [ "${addr%:}" != "${addr}" ]; then
            blobpatch_byte ${1} ${addr%:} ${a} ${b} || break
        else
            sum=`sha256sum -b ${1} | awk '{print $1}'`
            [ "${sum}" != "${addr}" ] && break
            status=$(($status - 1))
        fi
    done < ${2}
    [ $status -ne 0 ] && echo "blobpatch of ${1} failed, status=$status"
    return $status
}

applypatch() {
    echo "applypatch ${2} ${3}"
    patch -d ${1} -p1 --no-backup-if-mismatch ${3} < patches/${2}
}

$DO_VGPU && extract ${VGPU}.run
$DO_GRID && {
    if $SWITCH_GRID_TO_GNRL; then
        extract ${GRID}.run ${GNRL}
        #applypatch ${GNRL} vgpu-kvm-merge-grid-scripts.patch -R
        GRID=${GNRL}
        grep ' MODULE:vgpu$\|kernel/nvidia/nv-vgpu-vmbus' ${GRID}/.manifest | awk -F' ' '{print $1}' | while read i
        do
            rm -f ${GRID}/$i
        done
        sed -e '/ MODULE:vgpu$/ d' -e '/kernel\/nvidia\/nv-vgpu-vmbus/ d'  -i ${GRID}/.manifest
        sed -e '/nvidia\/nv-vgpu-vmbus.c/ d' -i ${GRID}/kernel/nvidia/nvidia-sources.Kbuild
        sed -e '/^[ \t]*GRID_BUILD=1/ d' -i ${GRID}/kernel/conftest.sh
    else
        extract ${GRID}.run
    fi
}

if $DO_MRGD; then
    echo "about to create merged driver"
    rm -rf ${SOURCE}
    mkdir ${SOURCE}
    $CP ${VGPU}/. ${SOURCE}
    rm ${SOURCE}/libnvidia-ml.so.${VER_VGPU}
    $CP ${GRID}/. ${SOURCE}
    if [ "${GRID/${TARGET_VER}}" = "${GRID}" ]; then
        USE_KVM_BLOB="kernel/nvidia/nv-kernel.o_binary"
        VER_BLOB=`echo ${VGPU} | awk -F- '{print $4}'`
    else
        USE_KVM_BLOB=""
    fi
    for i in LICENSE kernel/nvidia/nvidia-sources.Kbuild init-scripts/{post-install,pre-uninstall} nvidia-bug-report.sh ${USE_KVM_BLOB}
    do
        $CP ${VGPU}/$i ${SOURCE}/$i
    done
    sed -e '/^# VGX_KVM_BUILD/aVGX_KVM_BUILD=1' -i ${SOURCE}/kernel/conftest.sh
    sed -e 's/^\(nvidia .*nvidia-drm.*\)/\1 nvidia-vgpu-vfio/' -i ${SOURCE}/.manifest
    diff -u ${VGPU}/.manifest ${GRID}/.manifest \
    | grep -B 1 '^-.* MODULE:\(vgpu\|installer\)$' | grep -v '^--$' \
    | sed -e '/^ / s:/:\\/:g' -e 's:^ \(.*\):/\1/ a \\:' -e 's:^-\(.*\):\1\\:' | head -c -2 \
    | sed -e ':append' -e '/\\\n\// b found' -e N -e 'b append' -e ':found' -e 's:\\\n/:\n/:' \
    > manifest-merge.sed
    echo "merging .manifest file"
    sed -f manifest-merge.sed -i ${SOURCE}/.manifest
    rm manifest-merge.sed
    [ -e ${SOURCE}/nvidia-gridd ] && applypatch ${SOURCE} vgpu-kvm-merge-grid-scripts.patch
    applypatch ${SOURCE} disable-nvidia-blob-version-check.patch
fi

rm -rf ${TARGET}
mkdir -p ${TARGET}/kernel/unlock
$CP ${SOURCE}/. ${TARGET}

echo "applying vgpu_unlock hooks"
$CP unlock/kern.ld ${TARGET}/kernel/nvidia
$CP unlock/vgpu_unlock_hooks.c ${TARGET}/kernel/unlock
echo 'ldflags-y += -T $(src)/nvidia/kern.ld' >> ${TARGET}/kernel/nvidia/nvidia.Kbuild
sed -e 's:^\(#include "nv-time\.h"\):\1\n#include "../unlock/vgpu_unlock_hooks.c":' -i ${TARGET}/kernel/nvidia/os-interface.c
sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/linux_nvswitch.h / a \
kernel/unlock/vgpu_unlock_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu\
kernel/nvidia/kern.ld 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
applypatch ${TARGET} vgpu_unlock_hooks-510.patch

if $SPOOF || $CUDAH; then
    $SPOOF && echo "applying spoof devid kprobe hook"
    $CUDAH && echo "applying host cuda test kprobe hook"
    $CP patches/kp_hooks.c ${TARGET}/kernel/unlock
    echo 'NVIDIA_SOURCES += unlock/kp_hooks.c' >> ${TARGET}/kernel/nvidia/nvidia-sources.Kbuild
    $SPOOF && sed -e '/^NVIDIA_CFLAGS += .*DEBUG/aNVIDIA_CFLAGS += -DSPOOF_ID' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    $CUDAH && sed -e '/^NVIDIA_CFLAGS += .*DEBUG/aNVIDIA_CFLAGS += -DTEST_CUDA_HOST' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/linux_nvswitch.h / a \
kernel/unlock/kp_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
    applypatch ${TARGET} setup-kprobe-hooks.patch
else
    echo "kprobe hooks NOT applied"
fi

$DO_MRGD && $OPTVGPU && applypatch ${TARGET} vgpu-kvm-merged-optional-vgpu.patch

blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary patches/blob-${VER_BLOB}.diff || exit 1
$DO_MRGD && { blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary patches/blob-${VER_BLOB}-merged.diff || exit 1; }

if $DO_VGPU; then
    applypatch ${TARGET} vcfg-testing.patch
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000	# RTX 2070 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1C82 0x0000		# GTX 1050 Ti 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x17FD 0x0000		# Tesla M40
fi

$MIGRATION && [ -e ${TARGET}/kernel/nvidia-vgpu-vfio/nvidia-vgpu-vfio.Kbuild ] && {
    echo "enable migration support (just for testing)"
    sed -e 's/\(NV_KVM_MIGRATION_UAPI ?=\) 0/\1 1/' -i ${TARGET}/kernel/nvidia-vgpu-vfio/nvidia-vgpu-vfio.Kbuild
}

if $REPACK; then
    REPACK_OPTS="${REPACK_OPTS:---silent}"
    [ -e ${TARGET}.lsm ] && REPACK_OPTS="${REPACK_OPTS} --lsm ${TARGET}.lsm"
    [ -e ${TARGET}/pkg-history.txt ] && REPACK_OPTS="${REPACK_OPTS} --pkg-history ${TARGET}/pkg-history.txt"
    echo "about to create ${TARGET}.run file"
    ./${TARGET}/makeself.sh ${REPACK_OPTS} --version-string "${VER_TARGET}" --target-os Linux --target-arch x86_64 \
        ${TARGET} ${TARGET}.run \
        "NVIDIA Accelerated Graphics Driver for Linux-x86_64 ${TARGET#NVIDIA-Linux-x86_64-}" \
        ./nvidia-installer
    rm -f ${TARGET}.lsm
    echo "done"
fi
