#!/bin/bash

BASEDIR=$(dirname $0)

GNRL="NVIDIA-Linux-x86_64-525.85.05"
VGPU="NVIDIA-Linux-x86_64-525.85.07-vgpu-kvm"
GRID="NVIDIA-Linux-x86_64-525.85.05-grid"
#WSYS="NVIDIA-Windows-x86_64-512.15"
#WSYS="NVIDIA-Windows-x86_64-527.41"
WSYS="NVIDIA-Windows-x86_64-528.24"

SPOOF=true
CUDAH=true
KLOGT=true
OPTVGPU=true
TESTSIGN=true
SETUP_TESTSIGN=false
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
DO_LIBS=true

while [ $# -gt 0 -a "${1:0:2}" = "--" ]
do
    case "$1" in
        --no-spoof)
            shift
            SPOOF=false
            ;;
        --no-host-cuda)
            shift
            CUDAH=false
            ;;
        --no-klogtrace)
            shift
            KLOGT=false
            ;;
        --no-opt-vgpu)
            shift
            OPTVGPU=false
            ;;
        --no-libs-patch)
            shift
            DO_LIBS=false
            ;;
        --no-testsign)
            shift
            TESTSIGN=false
            ;;
        --create-cert)
            shift
            SETUP_TESTSIGN=true
            ;;
        --repack)
            shift
            REPACK=true
            ;;
        *)
            echo "Unknown option $1"
            shift
            ;;
    esac
done

case "$1" in
    vgpu*)
        CUDAH=false
        DO_VGPU=true
        SOURCE="${VGPU}"
        TARGET="${VGPU}-patched"
        DO_LIBS=false
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
    echo "blobpatch ${2}: ${3} ${4}"
    CHK=$(od -t x1 -A n --skip-bytes=`printf '%d' 0x${2}` --read-bytes=1 "${1}" 2>/dev/null | tr -d ' \n')
    if [ "${CHK^^}" = "${3}" ]; then
        echo -e -n "\x${4}" | dd of=${1} seek=`printf "%d" 0x${2}` bs=1 count=1 conv=notrunc &>/dev/null
    else
        die "blobpatch failed: expected ${3} got ${CHK^^} instead"
    fi
}

blobpatch() {
    echo "blobpatch ${2}"
    local status=2
    while read addr a b
    do
        if [ "${addr###}" != "${addr}" ]; then
            # skip comments (commeted out lines) if present
            echo "${addr} ${a} ${b}"
        elif [ "${addr%:}" != "${addr}" -a -n "${a}" -a -n "${b}" ]; then
            blobpatch_byte ${1} ${addr%:} ${a} ${b} || break
        elif [ -z "${addr}" ]; then
            # skip empty lines
            continue
        else
            sum=`sha256sum -b ${1} | awk '{print $1}'`
            if [ "${sum}" = "${addr}" ]; then
                status=$(($status - 1))
            fi
        fi
    done < ${2}
    [ $status -ne 0 ] && echo "blobpatch of ${1} failed, status=$status"
    echo
    return $status
}

applypatch() {
    echo "applypatch ${2} ${3}"
    patch -d ${1} -p1 --no-backup-if-mismatch ${3} < "$BASEDIR/patches/${2}"
    echo
}

libspatch() {
    sed -e 's/\x89\x06\x01\x20/\x40\x00\x01\x20/g' \
        -e 's/\xb8\x89\x06\x01\x00/\xb8\x40\x00\x01\x00/g' \
        -e 's/\x21\x40\xa2\x01\x41/\x21\x00\x10\x00\x41/g' \
        -e 's/\xa1\x40\xa2\x01\x41/\xa1\x00\x10\x00\x41/g' \
        -e 's/\xa2\x01\xc0\x18\x00\x03\x8c\x71/\x10\x00\xc0\x18\x00\x03\x8c\x31/g' \
        -e 's/\xa2\x01\xc0\x18\x00\x03\x0c\x70/\x10\x00\xc0\x18\x00\x03\x0c\x30/g' \
        -i "$@"
}

$SETUP_TESTSIGN && {
    which makecert &>/dev/null || die "install makecert (mono-devel) (https://github.com/mono/mono/tree/main/mcs/tools/security)"
    echo "creating test code signing certificate"
    mkdir -p wtestsign
    makecert -r -n "CN=Test CA" -a sha256 -cy authority -sky signature -sv wtestsign/test-ca.pvk wtestsign/test-ca.cer &>/dev/null || die "makecert Test CA failed"
    makecert -n "CN=Test SPC" -a sha256 -cy end -sky signature -iv wtestsign/test-ca.pvk -ic wtestsign/test-ca.cer \
        -eku 1.3.6.1.5.5.7.3.3 -m 36 -sv wtestsign/test-spc.pvk -p12 wtestsign/wsys-test-cert.pfx P@ss0wrd wtestsign/test-spc.cer &>/dev/null || die "makecert Test SPC failed"
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
    echo "source ${WSYS}/nvlddmkm.sys not found, will try to extract the installer"
    NV_WIN_DRV_INSTALLER=`ls -1 ${VER_TARGET}*[-_]win*[-_]64bit*.exe | head -n 1`
    [ -e "$NV_WIN_DRV_INSTALLER" ] || die "nvidia windows driver installer version $VER_TAGET not found"
    which 7z &>/dev/null || die "install p7zip-full for 7z tool (http://p7zip.sourceforge.net/)"
    which msexpand &>/dev/null || die "install mscompress (https://github.com/stapelberg/mscompress)"
    rm -rf ${WSYS}
    SYS_SRC=( "Display.Driver/nvlddmkm.sy_" )
    if $DO_LIBS; then
        SYS_SRC+=(
            "Display.Driver/nvd3dum.dl_"
            "Display.Driver/nvd3dumx.dl_"
            "Display.Driver/nvldumd.dl_"
            "Display.Driver/nvldumdx.dl_"
            "Display.Driver/nvoglv32.dl_"
            "Display.Driver/nvoglv64.dl_"
            "Display.Driver/nvwgf2um.dl_"
            "Display.Driver/nvwgf2umx.dl_"
        )
    fi
    echo "extracting the driver installer, please wait..."
    7z x -o${WSYS} "$NV_WIN_DRV_INSTALLER" ${SYS_SRC[*]} &>/dev/null
    for i in ${SYS_SRC[*]}
    do
        if [ "${i%_}" != "${i}" ]; then
            t=`basename "$i" | sed -e 's/sy_$/sys/' -e 's/dl_/dll/'`
            echo "$i -> $t"
            msexpand < ${WSYS}/$i > ${WSYS}/$t
        else
            t=`basename "$i"`
            echo "$i -> $t"
            mv ${WSYS}/$i ${WSYS}
        fi
    done
    rm -rf ${WSYS}/{Display.Driver,GFExperience}
    echo "extracted needed stuff from the driver installer"
fi

if $DO_MRGD; then
    echo "about to create merged driver"
    rm -rf ${SOURCE}
    mkdir ${SOURCE}
    $CP ${VGPU}/. ${SOURCE}
    rm ${SOURCE}/libnvidia-ml.so.${VER_VGPU}
    $CP ${GRID}/. ${SOURCE}
    for i in LICENSE kernel/nvidia/nvidia-sources.Kbuild init-scripts/{post-install,pre-uninstall} nvidia-bug-report.sh kernel/nvidia/nv-kernel.o_binary
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
    if [ ! -x "$BASEDIR"/nsigpatch ]; then
        which gcc &>/dev/null || die "gcc is needed to compile nsigpatch tool"
        gcc -fshort-wchar "$BASEDIR"/nsigpatch.c -o "$BASEDIR"/nsigpatch || die "build of nsigpatch tool failed"
    fi
    $TESTSIGN && { [ -e wtestsign/wsys-test-cert.pfx ] || die "testsign certificate missing, try with --create-cert option"; }

    rm -f ${SOURCE}/*-unsigned
    find ${SOURCE} -iname '*.sys' -o -iname '*.dll' | while read i
    do
        t=`basename "${i}"`
        echo "remove signature: ${t}"
        osslsigncode remove-signature -in ${i} -out ${i}-unsigned &>/dev/null || die "osslsigncode remove-signature failed"
        $CP ${i}-unsigned ${TARGET}/${t}-unsigned
    done

    if [ -e "$BASEDIR/patches/wsys-${VER_TARGET}.diff" ]; then
        echo "about to patch ${TARGET}/nvlddmkm.sys-unsigned"
        if [ -e "$BASEDIR/patches/wsys-${VER_TARGET}-klogtrace.diff" ]; then
            $KLOGT && { blobpatch ${TARGET}/nvlddmkm.sys-unsigned "$BASEDIR/patches/wsys-${VER_TARGET}-klogtrace.diff" || exit 1; }
        fi
        blobpatch ${TARGET}/nvlddmkm.sys-unsigned "$BASEDIR/patches/wsys-${VER_TARGET}.diff" || exit 1
    fi

    for i in ${TARGET}/*-unsigned
    do
        t=${i%-unsigned}
        [ "${t%.sys}" = "${t}" ] && {
            libspatch ${i}
            "$BASEDIR"/nsigpatch ${i} || exit 1
        }
        rm -f ${t}

        if $TESTSIGN; then
            echo -n "creating ${t} signed with a test certificate ... "
            osslsigncode sign -pkcs12 wtestsign/wsys-test-cert.pfx -pass P@ss0wrd -n "nvidia-driver-vgpu-unlock" \
                -t http://timestamp.digicert.com -in ${i} -out ${t}
        else
            echo "testsigning skipped: ${t}"
            $CP ${i} ${t}
        fi
        echo
    done

    exit 0
fi

if $DO_GNRL; then
    VER_BLOB=${VER_TARGET}
    grep -q '^GRID_BUILD=1' ${TARGET}/kernel/conftest.sh || sed -e '/^# GRID_BUILD /aGRID_BUILD=1' -i ${TARGET}/kernel/conftest.sh
fi

if $DO_UNLK; then
    echo "applying vgpu_unlock hooks"
    mkdir -p ${TARGET}/kernel/unlock
    $CP "$BASEDIR/unlock/kern.ld" ${TARGET}/kernel/nvidia
    $CP "$BASEDIR/unlock/vgpu_unlock_hooks.c" ${TARGET}/kernel/unlock
    echo 'ldflags-y += -T $(src)/nvidia/kern.ld' >> ${TARGET}/kernel/nvidia/nvidia.Kbuild
    sed -e 's:^\(#include "nv-time\.h"\):\1\n#include "../unlock/vgpu_unlock_hooks.c":' -i ${TARGET}/kernel/nvidia/os-interface.c
    sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/i2c_nvswitch.c / a \
kernel/unlock/vgpu_unlock_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu\
kernel/nvidia/kern.ld 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
    applypatch ${TARGET} vgpu_unlock_hooks-510.patch
fi

if $SPOOF || $CUDAH || $KLOGT; then
    $SPOOF && echo "applying spoof devid kprobe hook"
    $CUDAH && echo "applying host cuda test kprobe hook"
    $KLOGT && echo "applying klogtrace kprobe hook"
    mkdir -p ${TARGET}/kernel/unlock
    $CP "$BASEDIR/patches/kp_hooks.c" ${TARGET}/kernel/unlock
    echo 'NVIDIA_SOURCES += unlock/kp_hooks.c' >> ${TARGET}/kernel/nvidia/nvidia-sources.Kbuild
    $SPOOF && sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DSPOOF_ID' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    $CUDAH && sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DTEST_CUDA_HOST' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    $KLOGT && sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DKLOGTRACE' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/i2c_nvswitch.c / a \
kernel/unlock/kp_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
    applypatch ${TARGET} setup-kprobe-hooks.patch
    $KLOGT && applypatch ${TARGET} filter-for-nvrm-logs.patch
fi

$DO_MRGD && $OPTVGPU && applypatch ${TARGET} vgpu-kvm-merged-optional-vgpu.patch

blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary "$BASEDIR/patches/blob-${VER_BLOB}.diff" || exit 1
$DO_MRGD && {
    blobpatch ${TARGET}/kernel/nvidia/nv-kernel.o_binary "$BASEDIR/patches/blob-${VER_BLOB}-merged.diff" || exit 1
    blobpatch ${TARGET}/libnvidia-ml.so.${VER_TARGET} "$BASEDIR/patches/libnvidia-ml.so.${VER_TARGET%.*}.diff" || exit 1
}

$DO_LIBS && {
    for i in nvidia_drv.so {.,32}/libnvidia-{,e}glcore.so.${VER_TARGET}
    do
        libspatch ${TARGET}/${i}
    done
}

if $DO_VGPU; then
    applypatch ${TARGET} vcfg-testing.patch
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000	# RTX 2070 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E81 0x0000	# RTX 2080 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f03 0x0000	# RTX 2060 12GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x2184 0x0000	# GTX 1660 6GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1ff2 0x0000	# Quadro T400 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1C82 0x0000		# GTX 1050 Ti 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1B81 0x0000		# GTX 1070
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x17FD 0x0000		# Tesla M40 -> Tesla M60

    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x13C0 0x0000		# GTX 980 -> Tesla M60
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13BD 0x1160 0x139A 0x0000	# GTX 950M -> Tesla M10
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f95 0x0000  # GTX 1650 Ti Mobile 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1D01 0x0000     # GTX 1030 -> Tesla P40
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
