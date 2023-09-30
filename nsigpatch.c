// gcc -fshort-wchar nsigpatch.c -o nsigpatch

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>

#define NFUNCS 4
static const wchar_t crypt32_dll[] = L"crypt32.dll";
static const char *crypt32_fns[NFUNCS] = { "CryptQueryObject", "CryptMsgClose", "CertCloseStore", "CertFreeCertificateContext" };
static const uint8_t *crypt32_fps[NFUNCS];

static const uint8_t lea_rcx[] = { 0x48, 0x8d, 0x0d };
static const uint8_t lea_rdx[] = { 0x48, 0x8d, 0x15 };
static const uint8_t call_disp[] = { 0xe8 };

static const int patch_old[] = { -1, 0x8B, -1, 0x55, -1, -1, -1, 0x8d, -1, -1, -1, -1, -1, 0x48, 0x81, 0xec };
static const int patch_old2[] = { -1, 0x8B, -1, 0x55, -1, -1, -1, -1, 0x8d, -1, -1, -1, -1, -1, 0x48, 0x81, 0xec };
static const int patch_old3[] = { -1, 0x8B, -1, -1, -1, -1, -1, 0x55, -1, -1, -1, 0x8d, -1, -1, -1, -1, -1, 0x48, 0x81, 0xec };
static const int patch_old4[] = { -1, 0x8B, -1, -1, -1, -1, -1, 0x55, -1, -1, -1, -1, 0x8d, -1, -1, -1, -1, -1, 0x48, 0x81, 0xec };
static const int patch_old5[] = { 0x48, 0x89, -1, -1, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55 };

static const int patch_old6[] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05,  -1 ,  -1 ,  -1 ,  -1 , 0x48, 0x85, 0xC0, 0x75, 0x1E };
static const int patch_old7[] = { 0x4C, 0x8B,  -1 , 0x55,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1  };
static const int patch_old8[] = { 0x48, 0x8B,  -1 , 0x55,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1 ,  -1  };

static const uint8_t patch_new[] = { 0x31, 0xc0, 0xff, 0xc8, 0x48, 0x85, 0xd2, 0x74, 0x02, 0x89, 0x02, 0xc3 };

static const uint8_t push_offs[] = { 0x68 };
static const int patch32_old[]  = { 0x55, 0x8B, 0xEC, 0x81, 0xEC,   -1,   -1,   -1,   -1, 0xa1,   -1,   -1,   -1,   -1, 0x33, 0xc5};
static const int patch32_old2[] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC,   -1,   -1,   -1,   -1, 0x53 };
static const int patch32_old3[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC,   -1,   -1,   -1,   -1, 0xA1 };

static const int patch32_old4[] = { 0xA1,  -1 ,  -1 ,  -1 ,  -1 , 0x85, 0xC0, 0x75,  -1 , 0x50, 0x68 };
static const int patch32_old5[] = { 0xA1,  -1 ,  -1 ,  -1 ,  -1 , 0x56, 0x85, 0xC0, 0x75 };
static const int patch32_old6[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x6C, 0xA1,  -1 ,  -1 ,  -1 ,  -1 , 0x33, 0xC5, 0x89, 0x45, 0xFC };
static const int patch32_old7[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC };

static const int patch32_new[]  = { 0x31, 0xC0, 0x48, 0x85, 0xD2, 0x74, 0x02, 0x89, 0x02, 0xC3 };
static const int patch32_new2[] = { 0x8B, 0x44, 0x24, 0x08, 0x85, 0xc0, 0x75, 0x06, 0xeb, 0x08,   -1,   -1,   -1,   -1, 0x89, 0x00, 0x31, 0xc0, 0x48, 0xc2, 0x08, 0x00 };
static const int patch32_new3[] = { 0x8B, 0x44, 0x24, 0x08, 0xeb, 0x05,   -1,   -1,   -1,   -1,   -1, 0x85, 0xc0, 0x74, 0x02, 0x89, 0x00, 0x31, 0xc0, 0x48, 0xc2, 0x08, 0x00 };


uint8_t *find_xref(const uint8_t *buf, int bufsize,
                   const uint8_t *ipref, int ipsize,
                   const void *needle, int nlen,
                   const uint8_t *ipfrom, int gapsize)
{
    const uint8_t *np, *cp = buf;
    const uint8_t *ni, *ci;
    int csize = bufsize;
    int isize;
    uint32_t noffs;

    do {
        np = memmem(cp, csize, needle, nlen);
        if (np == NULL)
            return NULL;
        ci = buf;
        isize = bufsize;
        if (ipfrom != NULL) {
            ci = ipfrom;
            isize -= ci - buf;
        }
        do {
            if (ipref != NULL && ipsize > 0) {
                ni = memmem(ci, isize, ipref, ipsize);
                if (ni == NULL)
                    break;
            } else
                ni = ci;
            noffs = ni - buf + ipsize + sizeof(uint32_t);
            noffs = np - buf - noffs + gapsize;
            if (noffs == *(uint32_t *)(ni + ipsize))
                return (uint8_t *)ni;
            isize -= ni - ci + 1;
            ci += ni - ci + 1;
        } while (isize >= ipsize);
        csize -= np - cp + 1;
        cp += np - cp + 1;
    } while (csize >= nlen);

    return NULL;
}

uint8_t *find_xref32(const uint8_t *buf, int bufsize,
                     const uint8_t *ipref, int ipsize,
                     const void *needle, int nlen,
                     const uint8_t *ipfrom, int gapsize)
{
    const uint8_t *np, *cp = buf;
    const uint8_t *ni, *ci;
    int csize = bufsize;
    int isize;
    uint32_t noffs;

    do {
        np = memmem(cp, csize, needle, nlen);
        if (np == NULL)
            return NULL;
        ci = buf;
        isize = bufsize;
        if (ipfrom != NULL) {
            ci = ipfrom;
            isize -= ci - buf;
        }
        do {
            if (ipref != NULL && ipsize > 0) {
                ni = memmem(ci, isize, ipref, ipsize);
                if (ni == NULL)
                    break;
            } else
                ni = ci;
            noffs = 0x10000000 + np - buf + gapsize;
            if (noffs == *(uint32_t *)(ni + ipsize))
                return (uint8_t *)ni;
            isize -= ni - ci + 1;
            ci += ni - ci + 1;
        } while (isize >= ipsize);
        csize -= np - cp + 1;
        cp += np - cp + 1;
    } while (csize >= nlen);

    return NULL;
}

uint8_t *find_rel_xref(const uint8_t *buf, int bufsize,
                       const uint8_t *ipref, int ipsize,
                       const uint8_t *np,
                       const uint8_t *ipfrom)
{
    const uint8_t *cp = buf;
    const uint8_t *ni, *ci;
    int csize = bufsize;
    int isize;
    uint32_t noffs;

        ci = buf;
        isize = bufsize;
        if (ipfrom != NULL) {
            ci = ipfrom;
            isize -= ci - buf;
        }
        do {
            if (ipref != NULL && ipsize > 0) {
                ni = memmem(ci, isize, ipref, ipsize);
                if (ni == NULL)
                    break;
            } else
                ni = ci;
            noffs = ni - buf + ipsize + sizeof(uint32_t);
            noffs = np - buf - noffs;
            if (noffs == *(uint32_t *)(ni + ipsize))
                return (uint8_t *)ni;
            isize -= ni - ci + 1;
            ci += ni - ci + 1;
        } while (isize >= ipsize);

    return NULL;
}

uint8_t *find_entry(const uint8_t *file, const uint8_t *cp, const int *templ, int templsize)
{
    int i;
    const uint8_t *bp = cp;

    while (cp > file && bp - cp < 384) {
        for (i = 0; i < templsize; i++)
            if (templ[i] >= 0 && cp[i] != templ[i])
                break;
        if (i == templsize)
            return (uint8_t *)cp;
        cp--;
    }

    return NULL;
}

int main(int argc, char **argv)
{
    int i, fd;
    struct stat st;
    uint8_t *file;
    uint8_t *p1, *p2, *cp;
    int gapsize;
    int bitsmode = 64;

    assert(sizeof(wchar_t) == 2);

    if (argc < 2) {
        printf("Usage: %s filetopatch\n", argv[0]);
        return 1;
    }
    fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return 1;
    }
    file = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (file == (uint8_t *)-1) {
        perror("mmap");
        return 1;
    }

    p1 = memmem(file, st.st_size, crypt32_dll, sizeof(crypt32_dll));
    for (i = 0; i < NFUNCS; i++)
        if ((crypt32_fps[i] = memmem(file, st.st_size, crypt32_fns[i], strlen(crypt32_fns[i]) + 1)) == NULL) {
            p1 = NULL;
            break;
        }
    if (p1 == NULL) {
        printf("nsigpatch %s failed to locate strings\n", argv[1]);
        munmap(file, st.st_size);
        close(fd);
        return 1;
    }
    printf("nsigpatch %s positive match at %08lx", argv[1], p1 - file);
    for (i = 0; i < NFUNCS; i++)
        printf(", %08lx", crypt32_fps[i] - file);
    printf("\n");

    for (gapsize = 0; gapsize <= 0x4000; gapsize += 0x100) {
        for (p1 = file; p1 != NULL; p1++) {
            p1 = find_xref(file, st.st_size, lea_rcx, sizeof(lea_rcx),
                           crypt32_dll, sizeof(crypt32_dll),
                           p1, gapsize);
            if (p1 == NULL)
                break;
            //printf("nsigpatch gapsize=0x%04x p1=%08lx (crypt32_dll)\n", gapsize, p1 - file);
            for (i = 0; i < NFUNCS; i++) {
                p2 = find_xref(file,  st.st_size,
                               lea_rdx, sizeof(lea_rdx),
                               crypt32_fns[i], strlen(crypt32_fns[i]) + 1,
                               p1 + sizeof(lea_rcx) + 4, gapsize);
                if (p2 == NULL)
                    break;
                //printf("nsigpatch                p2=%08lx (%s)\n", p2 - file, crypt32_fns[i]);
                if (p2 - p1 > 0x200) {
                    p2 = NULL;
                    break;
                }
                crypt32_fps[i] = p2;
            }
            if (p2 != NULL)
                break;
        }
        if (p1 != NULL)
            break;
    }

    if (p1 == NULL) {
        bitsmode = 32;
        for (gapsize = 0; gapsize <= 0x4000; gapsize += 0x100) {
            for (p1 = file; p1 != NULL; p1++) {
                p1 = find_xref32(file, st.st_size, NULL, 0,
                                 crypt32_dll, sizeof(crypt32_dll),
                                 p1, gapsize);
                if (p1 == NULL)
                    break;
                //printf("nsigpatch gapsize=0x%04x p1=%08lx (crypt32_dll)\n", gapsize, p1 - file);
                for (i = 0; i < NFUNCS; i++) {
                    p2 = find_xref32(file, st.st_size, push_offs, sizeof(push_offs),
                                     crypt32_fns[i], strlen(crypt32_fns[i]) + 1,
                                     p1 + 4, gapsize);
                    if (p2 == NULL)
                        break;
                    //printf("nsigpatch                p2=%08lx (%s)\n", p2 - file, crypt32_fns[i]);
                    if (p2 - p1 > 0x300) {
                        p2 = NULL;
                        break;
                    }
                    crypt32_fps[i] = p2;
                }
                if (p2 != NULL) {
                    //printf("* nsigpatch %s found xrefs/%d at %08x %08x (%d) gapsize=0x%04x:\n",
                    //    argv[1], bitsmode, p1 - file, crypt32_fps[0] - file, crypt32_fps[0] - p1, gapsize);
                    break;
                }
            }
            if (p1 != NULL) {
                p1--;
                break;
            }
        }
    }


    cp = NULL;
    if (p1 != NULL) {
        printf("nsigpatch %s found xrefs/%d at %08lx", argv[1], bitsmode, p1 - file);
        for (i = 0; i < NFUNCS; i++)
            printf(", %08lx", crypt32_fps[i] - file);
        printf(" gapsize=0x%04x\n", gapsize);
        switch (bitsmode) {
        case 64:
            cp = find_entry(file, p1, patch_old, sizeof(patch_old) / sizeof(patch_old[0]));
            if (cp == NULL)
                cp = find_entry(file, p1, patch_old2, sizeof(patch_old2) / sizeof(patch_old2[0]));
            if (cp == NULL)
                cp = find_entry(file, p1, patch_old3, sizeof(patch_old3) / sizeof(patch_old3[0]));
            if (cp == NULL)
                cp = find_entry(file, p1, patch_old4, sizeof(patch_old4) / sizeof(patch_old4[0]));
            if (cp == NULL)
                cp = find_entry(file, p1, patch_old5, sizeof(patch_old5) / sizeof(patch_old5[0]));
            if (cp == NULL) {
                cp = find_entry(file, p1, patch_old6, sizeof(patch_old6) / sizeof(patch_old6[0]));
                if (cp != NULL) {
                    p2 = find_rel_xref(file, st.st_size,
                                       call_disp, sizeof(call_disp),
                                       cp, NULL);
                    printf("nsigpatch %s found call rel at %08lx\n", argv[1], p2 - file);
                    cp = NULL;
                    if (p2 != NULL) {
                        cp = find_entry(file, p2, patch_old7, sizeof(patch_old7) / sizeof(patch_old7[0]));
                        if (cp == NULL)
                            cp = find_entry(file, p2, patch_old8, sizeof(patch_old8) / sizeof(patch_old8[0]));
                    }
                }
            }

            if (cp != NULL) {
                for (i = 0; i < sizeof(patch_new); i++) {
                    printf("nsigpatch %08lX: %02X %02X\n", cp - file + i, cp[i], patch_new[i]);
                    cp[i] = patch_new[i];
                }
            } else
                printf("nsigpatch %s failed to locate function entry\n", argv[1]);
            break;
        case 32:
            p2 = p1;
            for (i = 0; i < 2 && p1 != NULL; i++) {
                cp = find_entry(file, p1, patch32_old, sizeof(patch32_old) / sizeof(patch32_old[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old2, sizeof(patch32_old2) / sizeof(patch32_old2[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old3, sizeof(patch32_old3) / sizeof(patch32_old3[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old4, sizeof(patch32_old4) / sizeof(patch32_old4[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old5, sizeof(patch32_old5) / sizeof(patch32_old5[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old6, sizeof(patch32_old6) / sizeof(patch32_old6[0]));
                if (cp == NULL)
                    cp = find_entry(file, p1, patch32_old7, sizeof(patch32_old7) / sizeof(patch32_old7[0]));
                if (1 /* force 2nd level for now */) {
                    if (cp != NULL && i == 0) {
                        p1 = find_rel_xref(file, st.st_size,
                                           call_disp, sizeof(call_disp),
                                           cp, NULL);
                        printf("nsigpatch %s found call rel at %08lx\n", argv[1], p1 - file);
                    }
                    //if (cp != NULL && i == 1)
                    //    printf("nsigpatch %s found call entry: %08lx\n", argv[1], cp - file);
                }
            }
            p1 = p2;

            if (cp != NULL) {
                const int *patch32 = patch32_new;
                int size = sizeof(patch32_new) / sizeof(patch32_new[0]);
                if (p1[0] == push_offs[0]) {
                    patch32 = patch32_new3;
                    size = sizeof(patch32_new3) / sizeof(patch32_new3[0]);
                }
                for (i = 0; i < size; i++) {
                    if (patch32[i] >= 0) {
                        printf("nsigpatch %08lX: %02X %02X\n", cp - file + i, cp[i], patch32[i]);
                        cp[i] = patch32[i];
                    }
                }
            } else
                printf("nsigpatch %s failed to locate function entry\n", argv[1]);
            break;
        }
    }
    if (cp == NULL)
        fprintf(stderr, "nsigpatch of %s failed\n", argv[1]);

    munmap(file, st.st_size);
    close(fd);
    return cp != NULL ? 0 : 1;
}
