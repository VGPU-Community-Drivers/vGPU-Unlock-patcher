#include "os-interface.h"
#include "nv-linux.h"

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/preempt.h>


#ifndef preempt_enable_no_resched
#ifdef CONFIG_PREEMPT_COUNT
#define sched_preempt_enable_no_resched() \
	do { \
		barrier(); \
		preempt_count_dec(); \
	} while (0)
#define preempt_enable_no_resched() sched_preempt_enable_no_resched()
#else
#define preempt_enable_no_resched() barrier()
#endif
#endif

#ifndef X86_CR4_CET_BIT
#define X86_CR4_CET_BIT 23
#endif

#ifndef VUP_MERGED_DRIVER
#define VUP_MERGED_DRIVER 0
#endif

struct vup_hook_info {
	void (*func)(void);
	const struct kernel_param *param;
	u32 offset;
	int ovgpu;
	s16 pbytes[14];
};

#define VUP_HOOK(offs, name, vgpuopt, cbytes...) \
	{ vup_hook_##name##_naked, &__param_##name, offs, vgpuopt, { cbytes, -1 } }


struct vup_patch_item {
	u32 offset;
	u8 oldval;
	u8 newval;
};

struct vup_patch_info {
	const struct kernel_param *param;
	struct vup_patch_item *items;
	int count;
	int enabv;
	int ovgpu;
};

#define VUP_PATCH_DEF(name, defval, enabval, vgpuopt) \
static int vup_patch_##name = defval; \
module_param_named(vup_##name, vup_patch_##name, int, 0400); \
static struct vup_patch_info vup_patch_info_##name = { \
	.param = &__param_vup_##name, \
	.items = vup_diff_##name, \
	.count = ARRAY_SIZE(vup_diff_##name), \
	.enabv = enabval, \
	.ovgpu = vgpuopt, \
}
#define VUP_PATCH(name) &vup_patch_info_##name

static int vup_vgpukvm_opt;
static int vup_cr4_cet_enabled;

#if defined(NV_VGPU_KVM_BUILD)
static int vup_cudahost = VUP_MERGED_DRIVER;
module_param_named(cudahost, vup_cudahost, int, 0400);

__attribute__((used))
static void vup_hook_cudahost(u8 *flag)
{
	printk(KERN_INFO "nvidia: vup_hook cudahost=%d flag=%d\n",
	       vup_cudahost, *flag);
	if (vup_cudahost > 0)
		*flag = vup_cudahost;
}

__attribute__((naked, no_instrument_function, no_stack_protector,
               no_split_stack, noclone, function_return("keep")))
static void vup_hook_cudahost_naked(void)
{
	asm (
		"push   %rdi            \n"
		"push   %rsi            \n"
		"push   %rdx            \n"
		"push   %rcx            \n"
		"push   %r8             \n"
		"push   %r9             \n"
		"lea   0x4ae(%rbx), %rdi\n"
		"call  vup_hook_cudahost\n"
		"pop    %r9             \n"
		"pop    %r8             \n"
		"pop    %rcx            \n"
		"pop    %rdx            \n"
		"pop    %rsi            \n"
		"pop    %rdi            \n"
		"cmpb   $0, 0x7f1(%r14) \n"
		"ret                    \n"
		"int3                   \n"
	);
}
STACK_FRAME_NON_STANDARD(vup_hook_cudahost_naked);
#endif


static int vup_vupdevid;
module_param_named(vupdevid, vup_vupdevid, int, 0400);

__attribute__((used))
static u32 vup_hook_vupdevid(u32 devid, u32 subdevid)
{
	printk(KERN_INFO "nvidia: vup_hook_vupdevid 10de:%04x %04x:%04x\n",
	       devid, subdevid & 0xffff, subdevid >> 16);
	return vup_vupdevid;
}

__attribute__((naked, no_instrument_function, no_stack_protector,
               no_split_stack, noclone, function_return("keep")))
static void vup_hook_vupdevid_naked(void)
{
	asm (
		"push   %rdi            \n"
		"push   %rsi            \n"
		"push   %rdx            \n"
		"push   %rcx            \n"
		"push   %r8             \n"
		"push   %r9             \n"
		"mov    %r15, %rdi      \n"
		"mov   0xa80(%r13), %esi\n"
		"call  vup_hook_vupdevid\n"
		"test   %eax, %eax      \n"
		"mov    $1, %r14d       \n"
		"cmovne %eax, %r15d     \n"
		"pop    %r9             \n"
		"pop    %r8             \n"
		"pop    %rcx            \n"
		"pop    %rdx            \n"
		"pop    %rsi            \n"
		"pop    %rdi            \n"
		"mov    %r12, %rax      \n"
		"mov    %r15d, %ebx     \n"
		"ret                    \n"
		"int3                   \n"
	);
}
STACK_FRAME_NON_STANDARD(vup_hook_vupdevid_naked);

static int vup_klogtrace_filter[][2] = {
	{ 0xbfe247, 0x04d8 },
	{ 0xe3cee1, 0x0679 },
};

static int vup_klogtrace;
module_param_named(klogtrace, vup_klogtrace, int, 0600);

static int vup_klogtrace_filtercnt = 8;
module_param_named(klogtracefc, vup_klogtrace_filtercnt, int, 0600);

__attribute__((used))
static void vup_hook_klogtrace(u64 rdi, u64 rsi)
{
	int i;
	int id, pt, a1, a2;
	if (vup_klogtrace < 1)
		return;
	id = rdi & 0xffffff;
	pt = (rsi >> 16) & 0xffff;
	a1 = (rdi >> 24) & 0xff;
	a2 = rsi & 0xffff;
	if (vup_klogtrace == 1) {
		for (i = 0; i < ARRAY_SIZE(vup_klogtrace_filter); i++)
			if (id == vup_klogtrace_filter[i][0]
			    && pt == vup_klogtrace_filter[i][1])
			{
				if (vup_klogtrace_filtercnt == 0)
					return;
				vup_klogtrace_filtercnt--;
			}
	}
	printk(KERN_DEBUG "NVTRACE %06x:%04x %04x%02x\n", id, pt, a2, a1);
}

__attribute__((naked, no_instrument_function, no_stack_protector,
               no_split_stack, noclone, function_return("keep")))
static void vup_hook_klogtrace_naked(void)
{
	asm (
		"push   %rdi            \n"
		"push   %rsi            \n"
		"push   %rdx            \n"
		"push   %rcx            \n"
		"push   %r8             \n"
		"push   %r9             \n"
		"call vup_hook_klogtrace\n"
		"pop    %r9             \n"
		"pop    %r8             \n"
		"pop    %rcx            \n"
		"pop    %rdx            \n"
		"pop    %rsi            \n"
		"pop    %rdi            \n"
		"sub    $0x430, %rbp    \n"
		"ret                    \n"
		"int3                   \n"
	);
}
STACK_FRAME_NON_STANDARD(vup_hook_klogtrace_naked);


static struct vup_hook_info vup_hooks[] = {
#if defined(NV_VGPU_KVM_BUILD)
	VUP_HOOK(0x003E4674, cudahost, 1, 0x41, 0x80, 0xBE, 0xF1, 0x07, 0x00, 0x00, 0x00),
	VUP_HOOK(0x004E35C6, vupdevid, 1, 0x4C, 0x89, 0xE0, 0x44, 0x89, 0xFB),
	VUP_HOOK(0x000141FF, klogtrace,0, 0x48, 0x81, 0xED, 0x30, 0x04, 0x00, 0x00),
#else
	VUP_HOOK(0x004E35C6, vupdevid, 1, 0x4C, 0x89, 0xE0, 0x44, 0x89, 0xFB),
	VUP_HOOK(0x000141FF, klogtrace,0, 0x48, 0x81, 0xED, 0x30, 0x04, 0x00, 0x00),
#endif
};


#if defined(NV_VGPU_KVM_BUILD)

#define RM_IOCTL_OFFSET 0xa627b0

static struct vup_patch_item vup_diff_vgpusig[] = {
	// based on patch from mbuchel to disable vgpu config signature
	{ 0x000B5643, 0x85, 0x31 },
};
VUP_PATCH_DEF(vgpusig, 1, 1, 1);

static struct vup_patch_item vup_diff_kunlock[] = {
	{ 0x000B7A88, 0x75, 0xEB },
	{ 0x000B7FE9, 0x01, 0x00 },
	{ 0x004D61B8, 0xE0, 0xC8 },
	{ 0x004D9085, 0x95, 0x93 },
	{ 0x004E31A2, 0x01, 0x05 },
	{ 0x004E3812, 0x75, 0xEB },
	{ 0x004EAACF, 0xF8, 0xC8 },
};
VUP_PATCH_DEF(kunlock, 1, 1, 1);

static struct vup_patch_item vup_diff_qmode[] = {
	{ 0x004E8B06, 0x0D, 0x07 },
	{ 0x004E8B0F, 0x84, 0x85 },
};
VUP_PATCH_DEF(qmode, 0, 0, 1);

static struct vup_patch_item vup_diff_merged[] = {
	{ 0x000AD8B6, 0x74, 0xEB },
	{ 0x000ADF58, 0x74, 0xEB },
	{ 0x004BF6D5, 0x2A, 0x00 },
};
VUP_PATCH_DEF(merged, VUP_MERGED_DRIVER, 1, 1);

static struct vup_patch_item vup_diff_fbcon[] = {
	{ 0x00A69C00, 0x84, 0x30 },
};
VUP_PATCH_DEF(fbcon, VUP_MERGED_DRIVER, 1, 1);

static struct vup_patch_item vup_diff_sunlock[] = {
	// based on patch from LIL'pingu fixing xid 43 crashes
	{ 0x00892C3A, 0x10, 0x00 },
};
VUP_PATCH_DEF(sunlock, 0, 1, 1);

static struct vup_patch_item vup_diff_gspvgpu[] = {
	{ 0x00031DF5, 0x1B, 0x00 },
	{ 0x00031E01, 0x74, 0xEB },
	//{ 0x00880449, 0x85, 0x31 },
};
VUP_PATCH_DEF(gspvgpu, 0, 1, 1);

struct vup_patch_info *vup_patches[] = {
	VUP_PATCH(vgpusig),
	VUP_PATCH(kunlock),
	VUP_PATCH(qmode),
	VUP_PATCH(merged),
	VUP_PATCH(fbcon),
	VUP_PATCH(sunlock),
	VUP_PATCH(gspvgpu),
};

#elif defined(NV_GRID_BUILD)

static int vup_gridext = 1;
module_param_named(gridext, vup_gridext, int, 0400);

#define RM_IOCTL_OFFSET 0xa627b0
static struct vup_patch_item vup_diff_general[] = {
	{ 0x000B7A88, 0x75, 0xEB },
	{ 0x00896B49, 0x09, 0x00 },
	{ 0x00A726E2, 0x3D, 0x00 },
};
VUP_PATCH_DEF(general, 1, 1, 1);
struct vup_patch_info *vup_patches[] = {
	VUP_PATCH(general),
};

#endif


static void vup_inject_hooks(u8 *blob)
{
	int i, j, arg, size;
	struct vup_hook_info *hi;
	char logbuf[128];

	logbuf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(vup_hooks); i++) {
		hi = &vup_hooks[i];
		if (vup_vgpukvm_opt == 0 && hi->ovgpu)
			continue;
		j = strlen(logbuf);
		size = sizeof(logbuf) - j;
		arg = *(int *)hi->param->arg;
		j = snprintf(logbuf + j, size, arg < 10 ? " %s=%d" : " %s=0x%x",
			     hi->param->name, arg);
		if (j >= size)
			printk(KERN_WARNING "nvidia: vup_inject_hooks "
			       "logbuf too small (%s)\n", hi->param->name);
		if (arg < 0)
			continue;
		for (j = 0; hi->pbytes[j] >= 0; j++)
			if (blob[hi->offset + j] != hi->pbytes[j])
				break;
		if (hi->pbytes[j] >= 0) {
			printk(KERN_ERR "nvidia: vup_inject_hooks %s "
			       "failed (%d)\n", hi->param->name, j);
			continue;
		}
		j -= 5;
		blob[hi->offset + j] = 0xe8;
		*(u32 *)(&blob[hi->offset + j + 1]) =
			(u8 *)hi->func - &blob[hi->offset + j + 5];
		for (j--; j >= 0; j--)
			blob[hi->offset + j] = 0x90;
	}
	printk(KERN_INFO "nvidia: vup_inject_hooks cetbit=%d%s\n",
	       vup_cr4_cet_enabled, logbuf);
}

static void vup_apply_patches(u8 *base)
{
	int i, j, arg, size;
	struct vup_patch_info *pi;
	const char *name;
	char logbuf[256];

	logbuf[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(vup_patches); i++) {
		pi = vup_patches[i];
		if (vup_vgpukvm_opt == 0 && pi->ovgpu)
			continue;
		j = strlen(logbuf);
		size = sizeof(logbuf) - j;
		arg = *(int *)pi->param->arg;
		name = pi->param->name;
		if (strncmp(name, "vup_", 4) == 0)
			name += 4;
		j = snprintf(logbuf + j, size, arg < 10 ? " %s=%d" : " %s=0x%x",
			     name, arg);
		if (j >= size)
			printk(KERN_WARNING "nvidia: vup_apply_patches "
			       "logbuf too small (%s)\n", name);
		if (arg != pi->enabv)
			continue;
		for (j = 0; j < pi->count; j++)
			if (base[pi->items[j].offset] != pi->items[j].oldval)
				break;
		if (j != pi->count) {
			printk(KERN_ERR "nvidia: vup_apply_patches %s "
			       "failed (%d)\n", name, j);
			continue;
		}
		for (j = 0; j < pi->count; j++)
			base[pi->items[j].offset] = pi->items[j].newval;
	}
	printk(KERN_INFO "nvidia: vup_apply_patches%s\n", logbuf);
}


static inline void vup_set_cr0(unsigned long val)
{
	asm volatile("mov %0, %%cr0" : "+r"(val) : : "memory");
}

static inline void vup_set_cr4(unsigned long val)
{
	asm volatile("mov %0, %%cr4" : "+r"(val) : : "memory");
}

static int vup_patching_start(void)
{
	unsigned long cr0;
	unsigned long cr4;
	preempt_disable();
	barrier();
	cr4 = __read_cr4();
	if (test_bit(X86_CR4_CET_BIT, &cr4)) {
		vup_cr4_cet_enabled = 1;
		clear_bit(X86_CR4_CET_BIT, &cr4);
		vup_set_cr4(cr4);
		barrier();
	}
	cr0 = read_cr0();
	clear_bit(16, &cr0);
	vup_set_cr0(cr0);
	barrier();
	cr0 = read_cr0();
	if (test_bit(16, &cr0) != 0)
		return 0;
	return 1;
}

static void vup_patching_done(void)
{
	unsigned long cr0 = read_cr0();
	set_bit(16, &cr0);
	vup_set_cr0(cr0);
	barrier();
	if (vup_cr4_cet_enabled) {
		unsigned long cr4 = __read_cr4();
		set_bit(X86_CR4_CET_BIT, &cr4);
		vup_set_cr4(cr4);
		barrier();
	}
	preempt_enable_no_resched();
}

void vup_hooks_init(void)
{
	u8 *blob = (u8 *)rm_ioctl - RM_IOCTL_OFFSET;

#if defined(NV_VGPU_KVM_BUILD)
	vup_vgpukvm_opt = nv_vgpu_kvm_enable;
#elif defined(NV_GRID_BUILD)
	vup_vgpukvm_opt = vup_gridext;
#endif

	if (vup_patching_start()) {
		vup_inject_hooks(blob);
		if (vup_vupdevid == 0)
			vup_vupdevid = 0x1e30;
		vup_apply_patches(blob - 0x40);
	}
	vup_patching_done();
}

void vup_hooks_exit(void)
{
}
