/* Fixed mmap16 interface. No runtime text modification or probes. */
#include <linux/module.h>
#include <linux/sysctl.h>
static int bits=16, minimum=16, maximum=16;
static struct ctl_table knobs[]={
 { .procname="mmap_rnd_bits", .data=&bits, .maxlen=sizeof(bits), .mode=0600,
   .proc_handler=proc_dointvec_minmax, .extra1=&minimum, .extra2=&maximum },{}
};
static struct ctl_table root[]={ { .procname="vm", .mode=0555, .child=knobs },{} };
static struct ctl_table_header *header;
static int __init start(void)
{
 header=register_sysctl_table(root);
 if (!header) return -ENOMEM;
 pr_info("zx9_aslr: mmap16 interface registered\n");
 return 0;
}
static void __exit stop(void) { unregister_sysctl_table(header); }
module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mmap16 interface");
