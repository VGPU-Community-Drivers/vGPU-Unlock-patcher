#include <linux/kprobes.h>

#ifdef SPOOF_ID

static struct kprobe spoof;

extern uint16_t vgpu_unlock_pci_devid_to_vgpu_capable(uint16_t pci_devid);

void spoof_id(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    uint16_t devid, newid;
    devid = regs->r12;
    newid = vgpu_unlock_pci_devid_to_vgpu_capable(devid);
    printk(KERN_INFO "nvidia: spoofing device id from 0x%04x to 0x%04x\n", devid, newid);
    regs->r12 = newid;
}

#endif

#ifdef TEST_CUDA_HOST

int cuda_host_enable = -1;
module_param_named(cudahost, cuda_host_enable, int, 0600);

static struct kprobe cuda_p1, cuda_p2;

int cuda_h1(struct kprobe *p, struct pt_regs *regs)
{
    printk(KERN_INFO "nvidia: cuda_h1 [rsi+0x412](0x%zx)==0x%02x [rdi+0x54d](0x%zx)==0x%02x\n",
        regs->si + 0x412, *(u8 *)(regs->si + 0x412), regs->di + 0x54d, *(u8 *)(regs->di + 0x54d));
    if (cuda_host_enable >= 0) {
        *(u8 *)(regs->si + 0x412) = cuda_host_enable;
        printk(KERN_INFO "nvidia: cuda_h1 [rsi+0x412](0x%zx) set to 0x%02x\n",
            regs->si + 0x412, *(u8 *)(regs->si + 0x412));
    }
    //dump_stack();
    return 0;
}

int cuda_h2(struct kprobe *p, struct pt_regs *regs)
{
    printk(KERN_INFO "nvidia: cuda_h2 [r13+0x412](0x%zx)==0x%02x [r13+0x414](0x%zx)==0x%02x\n",
        regs->r13 + 0x412, *(u8 *)(regs->r13 + 0x412), regs->r13 + 0x414, *(u8 *)(regs->r13 + 0x414));
    //dump_stack();
    /*
        Call Trace:
         dump_stack+0x6b/0x83
         cuda_h2+0x37/0x3a [nvidia]
         opt_pre_handler+0x40/0x60
         optimized_callback+0x63/0x80
         0xffffffffc05f44f6
         ? _nv010184rm+0xe0/0xe0 [nvidia]
         ? _nv029990rm+0xd7/0x190 [nvidia]
         ? rm_gpu_ops_get_pma_object+0x20/0x60 [nvidia]
         ? nvUvmInterfaceGetPmaObject+0x50/0x80 [nvidia]
         ? uvm_pmm_gpu_init+0x2b9/0x430 [nvidia_uvm]
         ? add_gpu+0x397/0x1230 [nvidia_uvm]
         ? kmem_cache_free+0x103/0x410
         ? uvm_gpu_retain_by_uuid+0x25a/0x2a0 [nvidia_uvm]
         ? uvm_gpu_retain_by_uuid+0x25a/0x2a0 [nvidia_uvm]
         ? uvm_va_space_register_gpu+0x3f/0x4f0 [nvidia_uvm]
         ? walk_component+0x70/0x1d0
         ? uvm_api_register_gpu+0x49/0x70 [nvidia_uvm]
         ? uvm_ioctl+0x939/0x1370 [nvidia_uvm]
         ? try_to_unlazy+0x4c/0x90
         ? __mod_memcg_lruvec_state+0x21/0xe0
         ? tomoyo_path_number_perm+0x66/0x1d0
         ? thread_context_non_interrupt_add+0x104/0x1c0 [nvidia_uvm]
         ? uvm_unlocked_ioctl_entry.part.0+0xa6/0x100 [nvidia_uvm]
         ? __x64_sys_ioctl+0x83/0xb0
         ? do_syscall_64+0x33/0x80
         ? entry_SYSCALL_64_after_hwframe+0x44/0xa9
    */
    return 0;
}

#endif

#ifdef KLOGTRACE
int klogtrace_enable = -1;
module_param_named(klogtrace, klogtrace_enable, int, 0600);

static struct kprobe klogtrace;

static int klogtrace_hook(struct kprobe *p, struct pt_regs *regs)
{
    if (klogtrace_enable) {
        int id = regs->di & 0xffffff;
        int pt = (regs->si >> 16) & 0xffff;
        int a1 = (regs->di >> 24) & 0xff;
        int a2 = regs->si & 0xffff;
        printk(KERN_DEBUG "NVTRACE %06x:%04x %04x%02x\n", id, pt, a2, a1);
    }
    return 0;
}
#endif

void init_probes(void)
{
#ifdef SPOOF_ID
    spoof.post_handler = spoof_id;
    spoof.symbol_name = "_nv021727rm";
    spoof.offset = 0x20;
    if (register_kprobe(&spoof) == 0)
        printk(KERN_INFO "nvidia: spoof devid kprobe hook registered\n");
    else
        printk(KERN_INFO "nvidia: spoof devid kprobe hook NOT registered\n");

#else
    printk(KERN_INFO "nvidia: spoof devid kprobe hook NOT compiled in\n");
#endif

#ifdef TEST_CUDA_HOST
    cuda_p1.pre_handler = cuda_h1;
    cuda_p1.symbol_name = "_nv028447rm";
    cuda_p1.offset = 0x32;
    if (register_kprobe(&cuda_p1) == 0)
        printk(KERN_INFO "nvidia: cuda_p1 kprobe hook registered\n");
    else
        printk(KERN_INFO "nvidia: cuda_p1 kprobe hook NOT registered\n");

    cuda_p2.pre_handler = cuda_h2;
    cuda_p2.symbol_name = "_nv030003rm";
    cuda_p2.offset = 0xd6;
    if (register_kprobe(&cuda_p2) == 0)
        printk(KERN_INFO "nvidia: cuda_p2 kprobe hook registered\n");
    else
        printk(KERN_INFO "nvidia: cuda_p2 kprobe hook NOT registered\n");
#else
    printk(KERN_INFO "nvidia: cuda host kprobe hook NOT compiled in\n");
#endif

#ifdef KLOGTRACE
    if (klogtrace_enable >= 0) {
        klogtrace.pre_handler = klogtrace_hook;
#ifdef NV_VGPU_KVM_BUILD
        klogtrace.symbol_name = "_nv030615rm";      // vgpu-kvm blob
#else
        klogtrace.symbol_name = "_nv030615rm";      // consumer or grid blob
#endif
        klogtrace.offset = 0;
        if (register_kprobe(&klogtrace) == 0)
            printk(KERN_INFO "nvidia: klogtrace kprobe hook registered (enable=%d)\n",
                   klogtrace_enable);
        else
            printk(KERN_INFO "nvidia: klogtrace kprobe hook NOT registered\n");
     }
#endif
}

void close_probes(void)
{
#ifdef SPOOF_ID
    unregister_kprobe(&spoof);
#endif

#ifdef TEST_CUDA_HOST
    unregister_kprobe(&cuda_p1);
    unregister_kprobe(&cuda_p2);
#endif

#ifdef KLOGTRACE
    if (klogtrace_enable >= 0)
        unregister_kprobe(&klogtrace);
#endif
}
