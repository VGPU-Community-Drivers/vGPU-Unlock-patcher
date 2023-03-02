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

void cuda_h1(struct kprobe *p, struct pt_regs *regs, unsigned long flags)
{
    printk(KERN_INFO "nvidia: cuda_h1 [rbx+0x472](0x%zx)==0x%02x [rbx+0x477](0x%zx)==0x%02x [rbx+0x486](0x%zx)==0x%02x\n",
        regs->bx + 0x472, *(u8 *)(regs->bx + 0x472), regs->bx + 0x477, *(u8 *)(regs->bx + 0x477), regs->bx + 0x486, *(u8 *)(regs->bx + 0x486));
    if (cuda_host_enable >= 0) {
        *(u8 *)(regs->bx + 0x472) = cuda_host_enable;
        *(u8 *)(regs->bx + 0x486) = cuda_host_enable;
        printk(KERN_INFO "nvidia: cuda_h1 [rbx+0x472](0x%zx) set to 0x%02x, [rbx+0x486](0x%zx) set to 0x%02x\n",
            regs->bx + 0x472, *(u8 *)(regs->bx + 0x472), regs->bx + 0x486, *(u8 *)(regs->bx + 0x486));
    }
    //dump_stack();
}

int cuda_h2(struct kprobe *p, struct pt_regs *regs)
{
    printk(KERN_INFO "nvidia: cuda_h2 [r8+0x472](0x%zx)==0x%02x [r8+0x477](0x%zx)==0x%02x\n",
        regs->r8 + 0x472, *(u8 *)(regs->r8 + 0x472), regs->r8 + 0x477, *(u8 *)(regs->r8 + 0x477));
    //dump_stack();
    /*
        Call Trace:
         dump_stack+0x6b/0x83
         cuda_h2+0x37/0x3a [nvidia]
         opt_pre_handler+0x40/0x60
         optimized_callback+0x63/0x80
         0xffffffffc44b4126
         ? _nv011135rm+0xd0/0xd0 [nvidia]
         ? _nv034209rm+0xdc/0x190 [nvidia]
         ? rm_gpu_ops_get_pma_object+0x20/0x60 [nvidia]
         ? nvUvmInterfaceGetPmaObject+0x70/0xb0 [nvidia]
         ? uvm_pmm_gpu_init+0x29f/0x420 [nvidia_uvm]
         ? uvm_gpu_retain_by_uuid+0xb28/0x2240 [nvidia_uvm]
         ? insert_state+0x40/0xf0 [btrfs]
         ? uvm_va_space_register_gpu+0x3f/0x540 [nvidia_uvm]
         ? uvm_va_space_register_gpu+0x3f/0x540 [nvidia_uvm]
         ? uvm_api_register_gpu+0x49/0x70 [nvidia_uvm]
         ? uvm_ioctl+0x939/0x1370 [nvidia_uvm]
         ? __mod_memcg_lruvec_state+0x21/0xe0
         ? tomoyo_path_number_perm+0x66/0x1d0
         ? thread_context_non_interrupt_add+0x10c/0x1c0 [nvidia_uvm]
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
        if (id == 0xbfe247 && pt == 0x04d4 && a2 == 0x4000 && a1 == 0x0e) {
            static int prncount = 8;
            if (prncount) {
                printk(KERN_DEBUG "NVTRACE %06x:%04x %04x%02x\n", id, pt, a2, a1);
                prncount--;
            }
        } else
            printk(KERN_DEBUG "NVTRACE %06x:%04x %04x%02x\n", id, pt, a2, a1);
    }
    return 0;
}
#endif

void init_probes(void)
{
#ifdef SPOOF_ID
    spoof.post_handler = spoof_id;
    spoof.symbol_name = "_nv023661rm";
    spoof.offset = 0x6e;
    if (register_kprobe(&spoof) == 0)
        printk(KERN_INFO "nvidia: spoof devid kprobe hook registered\n");
    else
        printk(KERN_INFO "nvidia: spoof devid kprobe hook NOT registered\n");

#else
    printk(KERN_INFO "nvidia: spoof devid kprobe hook NOT compiled in\n");
#endif

#ifdef TEST_CUDA_HOST
    cuda_p1.post_handler = cuda_h1;
    cuda_p1.symbol_name = "_nv032315rm";
    cuda_p1.offset = 0xa0;
    if (register_kprobe(&cuda_p1) == 0)
        printk(KERN_INFO "nvidia: cuda_p1 kprobe hook registered\n");
    else
        printk(KERN_INFO "nvidia: cuda_p1 kprobe hook NOT registered\n");

    cuda_p2.pre_handler = cuda_h2;
    cuda_p2.symbol_name = "_nv034296rm";
    cuda_p2.offset = 0xdb;
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
        klogtrace.symbol_name = "_nv034972rm";      // vgpu-kvm blob
#else
        klogtrace.symbol_name = "_nv034972rm";      // consumer or grid blob
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
