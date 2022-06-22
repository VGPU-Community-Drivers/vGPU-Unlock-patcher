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

void init_probes(void)
{
#ifdef SPOOF_ID
    spoof.post_handler = spoof_id;
    spoof.symbol_name = "_nv021715rm";
    spoof.offset = 0x20;

    if (register_kprobe(&spoof) == 0)
        printk(KERN_INFO "nvidia: spoof devid kprobe hook registered\n");
    else
#endif
        printk(KERN_INFO "nvidia: spoof devid kprobe hook NOT registered\n");
}

void close_probes(void)
{
#ifdef SPOOF_ID
    unregister_kprobe(&spoof);
#endif
}
