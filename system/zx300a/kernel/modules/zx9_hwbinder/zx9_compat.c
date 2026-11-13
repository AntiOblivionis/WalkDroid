/* GPL-2.0: module-only adapters to the exact native kernel functions.
 * No kernel text patch, no hooks, no skipped LSM check. Resolve all before registering any device.
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/fdtable.h>
#include <linux/security.h>
static typeof(&__alloc_fd) zx9_ptr___alloc_fd;
int __alloc_fd(struct files_struct *files, unsigned start, unsigned end, unsigned flags) { return zx9_ptr___alloc_fd(files,start,end,flags); }
static typeof(&__close_fd) zx9_ptr___close_fd;
int __close_fd(struct files_struct *files, unsigned fd) { return zx9_ptr___close_fd(files,fd); }
static typeof(&__fd_install) zx9_ptr___fd_install;
void __fd_install(struct files_struct *files, unsigned fd, struct file *file) { return zx9_ptr___fd_install(files,fd,file); }
static typeof(&__lock_task_sighand) zx9_ptr___lock_task_sighand;
struct sighand_struct * __lock_task_sighand(struct task_struct *task, unsigned long *flags) { return zx9_ptr___lock_task_sighand(task,flags); }
static typeof(&can_nice) zx9_ptr_can_nice;
int can_nice(const struct task_struct *task, const int nice) { return zx9_ptr_can_nice(task,nice); }
static typeof(&get_files_struct) zx9_ptr_get_files_struct;
struct files_struct * get_files_struct(struct task_struct *task) { return zx9_ptr_get_files_struct(task); }
static typeof(&put_files_struct) zx9_ptr_put_files_struct;
void put_files_struct(struct files_struct *files) { return zx9_ptr_put_files_struct(files); }
static typeof(&get_vm_area) zx9_ptr_get_vm_area;
struct vm_struct * get_vm_area(unsigned long size, unsigned long flags) { return zx9_ptr_get_vm_area(size,flags); }
static typeof(&unmap_kernel_range) zx9_ptr_unmap_kernel_range;
void unmap_kernel_range(unsigned long addr, unsigned long size) { return zx9_ptr_unmap_kernel_range(addr,size); }
static typeof(&zap_page_range) zx9_ptr_zap_page_range;
void zap_page_range(struct vm_area_struct *vma, unsigned long address, unsigned long size, struct zap_details *details) { return zx9_ptr_zap_page_range(vma,address,size,details); }
static typeof(&security_binder_set_context_mgr) zx9_ptr_security_binder_set_context_mgr;
int security_binder_set_context_mgr(struct task_struct *mgr) { return zx9_ptr_security_binder_set_context_mgr(mgr); }
static typeof(&security_binder_transaction) zx9_ptr_security_binder_transaction;
int security_binder_transaction(struct task_struct *from, struct task_struct *to) { return zx9_ptr_security_binder_transaction(from,to); }
static typeof(&security_binder_transfer_binder) zx9_ptr_security_binder_transfer_binder;
int security_binder_transfer_binder(struct task_struct *from, struct task_struct *to) { return zx9_ptr_security_binder_transfer_binder(from,to); }
static typeof(&security_binder_transfer_file) zx9_ptr_security_binder_transfer_file;
int security_binder_transfer_file(struct task_struct *from, struct task_struct *to, struct file *file) { return zx9_ptr_security_binder_transfer_file(from,to,file); }
int zx9_resolve_compat(void) { unsigned long addr;
addr=kallsyms_lookup_name("__alloc_fd"); zx9_ptr___alloc_fd=(typeof(zx9_ptr___alloc_fd))addr;
addr=kallsyms_lookup_name("__close_fd"); zx9_ptr___close_fd=(typeof(zx9_ptr___close_fd))addr;
addr=kallsyms_lookup_name("__fd_install"); zx9_ptr___fd_install=(typeof(zx9_ptr___fd_install))addr;
addr=kallsyms_lookup_name("__lock_task_sighand"); zx9_ptr___lock_task_sighand=(typeof(zx9_ptr___lock_task_sighand))addr;
addr=kallsyms_lookup_name("can_nice"); zx9_ptr_can_nice=(typeof(zx9_ptr_can_nice))addr;
addr=kallsyms_lookup_name("get_files_struct"); zx9_ptr_get_files_struct=(typeof(zx9_ptr_get_files_struct))addr;
addr=kallsyms_lookup_name("put_files_struct"); zx9_ptr_put_files_struct=(typeof(zx9_ptr_put_files_struct))addr;
addr=kallsyms_lookup_name("get_vm_area"); zx9_ptr_get_vm_area=(typeof(zx9_ptr_get_vm_area))addr;
addr=kallsyms_lookup_name("unmap_kernel_range"); zx9_ptr_unmap_kernel_range=(typeof(zx9_ptr_unmap_kernel_range))addr;
addr=kallsyms_lookup_name("zap_page_range"); zx9_ptr_zap_page_range=(typeof(zx9_ptr_zap_page_range))addr;
addr=kallsyms_lookup_name("security_binder_set_context_mgr"); zx9_ptr_security_binder_set_context_mgr=(typeof(zx9_ptr_security_binder_set_context_mgr))addr;
addr=kallsyms_lookup_name("security_binder_transaction"); zx9_ptr_security_binder_transaction=(typeof(zx9_ptr_security_binder_transaction))addr;
addr=kallsyms_lookup_name("security_binder_transfer_binder"); zx9_ptr_security_binder_transfer_binder=(typeof(zx9_ptr_security_binder_transfer_binder))addr;
addr=kallsyms_lookup_name("security_binder_transfer_file"); zx9_ptr_security_binder_transfer_file=(typeof(zx9_ptr_security_binder_transfer_file))addr;
return 0; }
