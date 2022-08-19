#!/bin/bash

GNRL="NVIDIA-Linux-x86_64-510.85.02"
VGPU="NVIDIA-Linux-x86_64-510.85.03-vgpu-kvm"
GRID="NVIDIA-Linux-x86_64-510.85.02-grid"
WSYS="NVIDIA-Windows-x86_64-512.15"

SPOOF=true
CUDAH=true
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
DO_GNRL=false
DO_GRID=false
DO_MRGD=false
DO_WSYS=false
DO_UNLK=true

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
    grid)
        DO_GNRL=true
        DO_GRID=true
        SOURCE="${GRID}"
        TARGET="${GRID}-patched"
        DO_UNLK=false
        SPOOF=false
        CUDAH=false
        ;;
    general)
        DO_GNRL=true
        DO_GRID=true
        GRID="${GNRL}"
        SOURCE="${GRID}"
        TARGET="${GRID}-patched"
        DO_UNLK=false
        SPOOF=false
        CUDAH=false
        ;;
    wsys)
        DO_WSYS=true
        SOURCE="${WSYS}"
        TARGET="${WSYS}-patched"
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
        echo "Usage: $0 [options] <vgpu-kvm | grid | general | wsys | grid-merge | general-merge | vcfg>"
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
        applypatch ${GNRL} vgpu-kvm-merge-grid-scripts.patch -R
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

if $DO_WSYS && [ ! -e "${WSYS}/nvlddmkm.sys" ]; then
    echo "source ${WSYS}/nvlddmkm.sys not found, will try to extract it"
    NV_WIN_DRV_INSTALLER=`ls -1 ${VER_TARGET}*[-_]win*[-_]64bit*.exe | head -n 1`
    [ -e "$NV_WIN_DRV_INSTALLER" ] || die "nvidia windows driver installer version $VER_TAGET not found"
    which 7z &>/dev/null || die "install p7zip-full for 7z tool (http://p7zip.sourceforge.net/)"
    which msexpand &>/dev/null || die "install mscompress (https://github.com/stapelberg/mscompress)"
    rm -rf ${WSYS}
    SYS_SRC="Display.Driver/nvlddmkm.sy_"
    echo "extracting nvlddmkm.sys, please wait..."
    7z x -o${WSYS} "$NV_WIN_DRV_INSTALLER" "${SYS_SRC}" &>/dev/null
    SYS_SRC="${WSYS}/${SYS_SRC}"
    SYS_DST=`echo "${SYS_SRC%_}s" | sed -e 's:Display.Driver/::'`
    [ -e "${SYS_SRC}" ] || die "packed driver (${SYS_SRC}) not found"
    msexpand < ${SYS_SRC} > ${SYS_DST}
    rm -rf "${WSYS}/Display.Driver"
    echo "extracted ${SYS_DST} from windows driver installer"
fi

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
$CP ${SOURCE} ${TARGET}

if $DO_WSYS; then
    which osslsigncode &>/dev/null || die "install osslsigncode (https://github.com/mtrojnar/osslsigncode)"

    echo -n "removing sys file signature ... "
    osslsigncode remove-signature -in ${TARGET}/nvlddmkm.sys -out ${TARGET}/nvlddmkm-unsigned.sys
    rm -f ${TARGET}/nvlddmkm.sys

    echo "about to patch ${TARGET}/nvlddmkm-unsigned.sys"
    blobpatch ${TARGET}/nvlddmkm-unsigned.sys patches/wsys-${VER_TARGET}.diff || exit 1

    echo -n "signing the sys file with test certificate ... "
    osslsigncode sign -pkcs12 patches/wsys-test-cert.pfx -pass P@ss0wrd -n "nvidia-driver-vgpu-unlock" \
        -t http://timestamp.digicert.com -in ${TARGET}/nvlddmkm-unsigned.sys -out ${TARGET}/nvlddmkm.sys

    exit 0
fi

if $DO_GNRL; then
    VER_BLOB=${VER_TARGET}
    grep -q '^GRID_BUILD=1' ${TARGET}/kernel/conftest.sh || sed -e '/^# GRID_BUILD /aGRID_BUILD=1' -i ${TARGET}/kernel/conftest.sh
fi

if $DO_UNLK; then
    echo "applying vgpu_unlock hooks"
    mkdir -p ${TARGET}/kernel/unlock
    $CP unlock/kern.ld ${TARGET}/kernel/nvidia
    $CP unlock/vgpu_unlock_hooks.c ${TARGET}/kernel/unlock
    echo 'ldflags-y += -T $(src)/nvidia/kern.ld' >> ${TARGET}/kernel/nvidia/nvidia.Kbuild
    sed -e 's:^\(#include "nv-time\.h"\):\1\n#include "../unlock/vgpu_unlock_hooks.c":' -i ${TARGET}/kernel/nvidia/os-interface.c
    sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/i2c_nvswitch.c / a \
kernel/unlock/vgpu_unlock_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu\
kernel/nvidia/kern.ld 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
    applypatch ${TARGET} vgpu_unlock_hooks-510.patch
fi

if $SPOOF || $CUDAH; then
    $SPOOF && echo "applying spoof devid kprobe hook"
    $CUDAH && echo "applying host cuda test kprobe hook"
    $CP patches/kp_hooks.c ${TARGET}/kernel/unlock
    echo 'NVIDIA_SOURCES += unlock/kp_hooks.c' >> ${TARGET}/kernel/nvidia/nvidia-sources.Kbuild
    $SPOOF && sed -e '/^NVIDIA_CFLAGS += .*DEBUG/aNVIDIA_CFLAGS += -DSPOOF_ID' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    $CUDAH && sed -e '/^NVIDIA_CFLAGS += .*DEBUG/aNVIDIA_CFLAGS += -DTEST_CUDA_HOST' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/i2c_nvswitch.c / a \
kernel/unlock/kp_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
    applypatch ${TARGET} setup-kprobe-hooks.patch
fi

$DO_MRGD && $OPTVGPU && applypatch ${TARGET} vgpu-kvm-merged-optional-vgpu.patch

blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary patches/blob-${VER_BLOB}.diff || exit 1
$DO_MRGD && { blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary patches/blob-${VER_BLOB}-merged.diff || exit 1; }

if $DO_VGPU; then
    applypatch ${TARGET} vcfg-testing.patch
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000	# RTX 2070 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1C82 0x0000		# GTX 1050 Ti 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x17FD 0x0000		# Tesla M40
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x2184 0x0000	# GTX 1660 6GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f03 0x0000	# RTX 2060 12GB
fi

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
