#!/bin/bash

BASEDIR=$(dirname $0)

GNRL="NVIDIA-Linux-x86_64-535.43.02"
VGPU="NVIDIA-Linux-x86_64-535.43.02-vgpu-kvm"
GRID="NVIDIA-Linux-x86_64-535.43.02-grid"
#WSYS="NVIDIA-Windows-x86_64-474.30"
#WSYS="NVIDIA-Windows-x86_64-512.15"
#WSYS="NVIDIA-Windows-x86_64-516.25"
#WSYS="NVIDIA-Windows-x86_64-516.59"
#WSYS="NVIDIA-Windows-x86_64-527.41"
#WSYS="NVIDIA-Windows-x86_64-528.24"
#WSYS="NVIDIA-Windows-x86_64-528.89"
#WSYS="NVIDIA-Windows-x86_64-531.41"
WSYS="NVIDIA-Windows-x86_64-535.98"

KLOGT=true
TESTSIGN=true
SETUP_TESTSIGN=false
REPACK=false
SWITCH_GRID_TO_GNRL=false

if [ ! -e "${GNRL}.run" -a -e "${GRID}.run" ]; then
    SWITCH_GRID_TO_GNRL=true
fi

VER_VGPU=`echo ${VGPU} | awk -F- '{print $4}'`
VER_GRID=`echo ${GRID} | awk -F- '{print $4}'`

NVGPLOPTPATCH=false
FORCEUSENVGPL=false
TDMABUFEXPORT=false
ENVYPROBES=false

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
DO_LK6P=false

while [ $# -gt 0 -a "${1:0:2}" = "--" ]
do
    case "$1" in
        --no-klogtrace)
            shift
            KLOGT=false
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
        --lk6-patches)
            shift
            DO_LK6P=true
            ;;
        --repack)
            shift
            REPACK=true
            ;;
        --force-nvidia-gpl-I-know-it-is-wrong)
            shift
            NVGPLOPTPATCH=true
            ;;
        --enable-nvidia-gpl-for-experimenting)
            shift
            FORCEUSENVGPL=true
            ;;
        --test-dmabuf-export)
            shift
            TDMABUFEXPORT=true
            ;;
        --envy-probes)
            shift
            ENVYPROBES=true
            ;;
        *)
            echo "Unknown option $1"
            shift
            ;;
    esac
done

case "$1" in
    vgpu*)
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
        SWITCH_GRID_TO_GNRL=false
        ;;
    general)
        DO_GNRL=true
        DO_GRID=true
        if $SWITCH_GRID_TO_GNRL; then
            echo "WARNING: did not find general driver, trying to create it from grid one"
            GNRL="${GRID/-grid}"
        else
            GRID="${GNRL}"
        fi
        SOURCE="${GNRL}"
        TARGET="${GNRL}-patched"
        DO_UNLK=false
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
    diff2c)
        shift
        [ $# -ge 1 -a -e "$1" ] || {
            echo "Usage: $0 diff2c file.diff [blobfile]"
            exit 1
        }
        cfile="${1%.diff}.c"
        if [ -n "$2" -a -e "$2" -a "${2%nv-kernel.o_binary}" != "$2" ]; then
            blob="$2"
        else
            blob=`basename "${1%.diff}" | sed -e 's/blob-\([0-9.]\+\).*/\1/'`
            blob=`find "$BASEDIR" -mindepth 1 -maxdepth 1 -type d -name "NVIDIA-Linux-x86_64-${blob}*" | head -n 1`
            [ -n "$blob" ] && blob="${blob}/kernel/nvidia/nv-kernel.o_binary"
            [ -n "$blob" -a -e "$blob" ] || blob=""
        fi
        if [ -n "$blob" ]; then
            offset=`nm "$blob" | sed -n -e '/ rm_ioctl$/ s/0*\([0-9a-f]*\) .*/0x\1/p'`
        fi
        (
            [ -n "$offset" ] && echo "#define RM_IOCTL_OFFSET $offset"
            sed -e 's/\(000000000\|0\)\([^:]*\): \([0-9A-F]\+\) \([0-9A-F]\+\).*/\t{ 0x0\2, 0x\3, 0x\4 },/' \
                -e '/^[0-9a-f]\+$/ d' -e 's:^\([ \t]*\)#:\t\1//:' "$1"
        ) > "$cfile"
        exit 0
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
    TDIR="${2}"
    if [ -z "${TDIR}" ]; then
        TDIR="${1%.run}"
    fi
    if [ -e ${1} ]; then
        $REPACK && sh ${1} --lsm > ${TARGET}.lsm
    fi
    if [ -d ${TDIR} ]; then
        echo "WARNING: skipping extract of ${1} as it seems already extracted in ${TDIR}"
        return 0
    fi
    [ -e ${1} ] || die "package ${1} not found"
    sh ${1} --extract-only --target ${TDIR}
    echo >> ${TDIR}/kernel/nvidia/nvidia.Kbuild
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

echo "WARNING: this is highly experimental frankenstein setup for vgpu drivers!"
if [ ! -d "${VGPU}" ]; then
    VGPUa="NVIDIA-Linux-x86_64-525.105.14-vgpu-kvm"
    VGPUb="NVIDIA-Linux-x86_64-535.43.02"
    va=`echo ${VGPUa} | awk -F- '{print $4}'`
    vb=`echo ${VGPUb} | awk -F- '{print $4}'`
    [ -e ${VGPUa}.run -a -e ${VGPUb}.run ] || die "some of ${VGPUa} ${VGPUb} run files missing"
    which patchelf &>/dev/null || die "patchelf not found"
    REPACK=false extract ${VGPUa}.run
    extract ${VGPUb}.run
    set -x
    rm -rf "${VGPU}"
    $CP ${VGPUa} ${VGPU}
    $CP ${VGPUb}/kernel/{common,nvidia,Kbuild,Makefile,conftest.sh} ${VGPU}/kernel/
    $CP ${VGPUb}/firmware ${VGPU}/ ; rm ${VGPU}/firmware/gsp_ad10x.bin ; sed -e "s/gsp_ad10x\.bin/gsp_ga10x.bin/" -i ${VGPU}/.manifest
    rm ${VGPU}/libnvidia-ml.so.${va}
    $CP ${VGPUb}/{nvidia-smi,libnvidia-ml.so.${vb}} ${VGPU}/
    sed -e '/^# VGX_KVM_BUILD/aVGX_KVM_BUILD=1' -i ${VGPU}/kernel/conftest.sh
    sed -e '/^NV_CONFTEST_TYPE_COMPILE_TESTS/ s/\(+= mdev_parent\)$/\1_ops/' -i ${VGPU}/kernel/nvidia-vgpu-vfio/nvidia-vgpu-vfio.Kbuild
    sed -e '/nv_uvm_interface.c/aNVIDIA_SOURCES += nvidia/nv-vgpu-vfio-interface.c' -i ${VGPU}/kernel/nvidia/nvidia-sources.Kbuild
    grep 'kernel/\(common\|nvidia\)/.*\(nv-dmabuf\|nvkms\)' ${VGPUb}/.manifest >> ${VGPU}/.manifest
    sed -e "s/${va//./\\.}/${vb}/g" -i ${VGPU}/.manifest
    for s in libnvidia-vgpu.so libnvidia-vgxcfg.so
    do
        mv ${VGPU}/${s}.${va} ${VGPU}/${s}.${vb}
        sed -e "s/${va//./\\.}/${vb}\\x00/g" -i ${VGPU}/${s}.${vb}
    done
    gcc -o ${VGPU}/libvgpucompat.so -shared -fPIC -O2 -s -Wall "$BASEDIR/patches/cvgpu.c"
    echo "libvgpucompat.so 0755 VGX_LIB NATIVE MODULE:vgpu" >> ${VGPU}/.manifest
    for s in nvidia-vgpu-mgr nvidia-vgpud
    do
        $CP ${VGPUa}/${s} ${VGPU}/
        sed -e "s/${va//./\\.}/${vb}\\x00/g" -i ${VGPU}/${s}
        patchelf --add-needed libvgpucompat.so ${VGPU}/${s}
    done
    set +x
    blobpatch ${VGPU}/libnvidia-vgpu.so.${vb} "$BASEDIR/patches/libnvidia-vgpu.so.${vb}.diff" || exit 1
else
    echo "WARNING: skipping frankenstein setup as ${VGPU} already exists"
    echo -e "${VGPU}/libvgpucompat.so: $BASEDIR/patches/cvgpu.c\n\tgcc -o \$@ -shared -fPIC -O2 -s -Wall \$<" | make -f - || die "build of libvgpucompat.so failed"
fi

$DO_VGPU && extract ${VGPU}.run
$DO_GRID && {
    if $SWITCH_GRID_TO_GNRL; then
      if [ -d ${GNRL} ]; then
        echo "WARNING: skipping switch from grid to general as it seems present in ${GNRL}"
      else
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
      fi
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
    SYS_SRC=( "Display.Driver/nvlddmkm.sy*" )
    if $DO_LIBS; then
        SYS_SRC+=(
            "Display.Driver/nvd3dum.dl*"
            "Display.Driver/nvd3dumx.dl*"
            "Display.Driver/nvldumd.dl*"
            "Display.Driver/nvldumdx.dl*"
            "Display.Driver/nvoglv32.dl*"
            "Display.Driver/nvoglv64.dl*"
            "Display.Driver/nvwgf2um.dl*"
            "Display.Driver/nvwgf2umx.dl*"
        )
    fi
    echo "extracting the driver installer, please wait..."
    7z x -o${WSYS} "$NV_WIN_DRV_INSTALLER" ${SYS_SRC[*]} &>/dev/null
    pushd ${WSYS} &>/dev/null
    for i in ${SYS_SRC[*]}
    do
        if [ "${i%_}" != "${i}" ]; then
            t=`basename "$i" | sed -e 's/sy_$/sys/' -e 's/dl_/dll/'`
            echo "$i -> $t"
            msexpand < $i > $t
        else
            t=`basename "$i"`
            echo "$i -> $t"
            mv $i .
        fi
    done
    popd &>/dev/null
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
        $CP -f ${VGPU}/$i ${SOURCE}/$i
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

echo "integrating runtime nv blob hooks"
mkdir -p ${TARGET}/kernel/unlock
$CP "$BASEDIR/patches/nv_hooks.c" ${TARGET}/kernel/unlock
echo 'NVIDIA_SOURCES += unlock/nv_hooks.c' >> ${TARGET}/kernel/nvidia/nvidia-sources.Kbuild
echo 'OBJECT_FILES_NON_STANDARD_nv_hooks.o := y' >> ${TARGET}/kernel/nvidia/nvidia.Kbuild
sed -i ${TARGET}/.manifest -e '/^kernel\/nvidia\/i2c_nvswitch.c / a \
kernel/unlock/nv_hooks.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu'
echo
applypatch ${TARGET} setup-vup-hooks.patch
applypatch ${TARGET} filter-for-nvrm-logs.patch
$NVGPLOPTPATCH && {
    applypatch ${TARGET} switch-option-to-gpl-for-debug.patch
    $FORCEUSENVGPL && sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DFORCE_GPL_FOR_EXPERIMENTING' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
}
$TDMABUFEXPORT && {
    applypatch ${TARGET} test-dmabuf-export.patch
    cp -p ${TARGET}/kernel-open/nvidia/nv-dmabuf.c ${TARGET}/kernel/nvidia/nv-dmabuf.c
}
$DO_VGPU && applypatch ${TARGET} vgpu-kvm-optional-vgpu-v2.patch

$DO_MRGD && {
    sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DVUP_MERGED_DRIVER=1' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    blobpatch ${TARGET}/libnvidia-ml.so.${VER_TARGET} "$BASEDIR/patches/libnvidia-ml.so.${VER_TARGET%.*}.diff" || exit 1
}

$DO_LIBS && {
    for i in nvidia_drv.so {.,32}/libnvidia-{,e}glcore.so.${VER_TARGET}
    do
        libspatch ${TARGET}/${i}
    done
}

if $DO_VGPU; then
    if $DO_LK6P; then
        applypatch ${TARGET} vgpu-kvm-kernel-6.1-compat.patch
        applypatch ${TARGET} vgpu-kvm-kernel-6.2-compat.patch
        sed -e 's/^\([ \t]*mdev_set_iommu_device\)();/\1XXX();/' -i ${TARGET}/kernel/conftest.sh
    fi
    applypatch ${TARGET} vgpu-kvm-nvidia-535.43-compat.patch
    applypatch ${TARGET} workaround-for-cards-with-inforom-error.patch
    applypatch ${TARGET} vcfg-testing.patch
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E84 0x0000	# RTX 2070 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1E81 0x0000	# RTX 2080 super 8GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f03 0x0000	# RTX 2060 12GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f11 0x0000	# RTX 2060 Mobile 6GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x2184 0x0000	# GTX 1660 6GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1f95 0x0000	# GTX 1650 Ti Mobile 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1EB1 0x0000	# Quadro RTX 4000
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1E30 0x12BA 0x1ff2 0x0000	# Quadro T400 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1C82 0x0000		# GTX 1050 Ti 4GB
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1B81 0x0000		# GTX 1070
    vcfgclone ${TARGET}/vgpuConfig.xml 0x1B38 0x0 0x1D01 0x0000		# GTX 1030 -> Tesla P40
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x17FD 0x0000		# Tesla M40 -> Tesla M60
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x13C0 0x0000		# GTX 980 -> Tesla M60
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13F2 0x0 0x13D7 0x0000		# GTX 980M -> Tesla M60
    vcfgclone ${TARGET}/vgpuConfig.xml 0x13BD 0x1160 0x139A 0x0000	# GTX 950M -> Tesla M10
    echo
fi

$ENVYPROBES && {
    applypatch ${TARGET} envy_probes-ioctl-hooks-from-mbuchel.patch
    sed -e '/^NVIDIA_CFLAGS += .*BIT_MACROS$/aNVIDIA_CFLAGS += -DENVY_LINUX' -i ${TARGET}/kernel/nvidia/nvidia.Kbuild
    echo 'NVIDIA_SOURCES += unlock/envy_probes.c' >> ${TARGET}/kernel/nvidia/nvidia-sources.Kbuild
    echo "kernel/common/inc/envy_probes.h 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu" >>${TARGET}/.manifest
    echo "kernel/unlock/envy_probes.c 0644 KERNEL_MODULE_SRC INHERIT_PATH_DEPTH:1 MODULE:vgpu" >>${TARGET}/.manifest
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
