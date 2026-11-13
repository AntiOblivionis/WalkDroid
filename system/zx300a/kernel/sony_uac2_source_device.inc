/* Included after wd_uac_source. Source lock never spans user access, sleeping
 * allocation, deregistration or a destructor. read_mutex serializes one FD's
 * operations. Producer only takes source lock, never read_mutex/misc_mtx.
 * Linux3.10 misc_open holds misc_mtx over our open: misc_deregister therefore
 * waits for open to acquire its own kref before the parent reference is put.
 */
#include "sony_uac2_source_commit.inc"
static void wd_source_free(struct kref *ref)
{
    struct wd_uac_source *s=container_of(ref,struct wd_uac_source,refs);
    if(WARN_ON(s->registered||s->parent_live||s->consumer))return;
    vfree(s->storage);vfree(s->raw);vfree(s->pcm);vfree(s->record_buffer);kfree(s);
}
static int wd_source_file_open(struct inode *inode,struct file *file)
{
    struct wd_uac_source *s=container_of(file->private_data,struct wd_uac_source,misc);
    unsigned long flags;int rc=0;
    if(!mutex_trylock(&s->read_mutex))return -EBUSY;
    spin_lock_irqsave(&wd_uac_data_lock,flags);spin_lock(&s->lock);
    if(!s->parent_live)rc=-ENODEV;
    else if(s->consumer)rc=-EBUSY;
    else{
        kref_get(&s->refs);s->consumer=1;
        file->private_data=s;
    }
    spin_unlock(&s->lock);spin_unlock_irqrestore(&wd_uac_data_lock,flags);mutex_unlock(&s->read_mutex);return rc;
}
static int wd_source_file_release(struct inode *inode,struct file *file)
{
    struct wd_uac_source *s=file->private_data;unsigned long flags;
    if(!s)return 0;
    mutex_lock(&s->read_mutex);spin_lock_irqsave(&s->lock,flags);
    s->consumer=0;s->consumer_claimed=0;s->consumer_source=0;
    s->feedback_source=0;s->feedback_initial_frames=0;file->private_data=NULL;
    spin_unlock_irqrestore(&s->lock,flags);mutex_unlock(&s->read_mutex);
    wake_up_interruptible(&s->ready);kref_put(&s->refs,wd_source_free);return 0;
}
static long wd_source_file_ioctl(struct file *file,unsigned int cmd,unsigned long arg)
{
    struct wd_uac_source *s=file->private_data;struct sony_source_info info;
    struct sony_source_caps caps;sony_sq_u64 id=0;unsigned long flags;void __user *user=(void __user *)arg;
    struct sony_source_feedback_info feedback;
    int rc=s?0:-EBADF,copied=0;if(rc)return rc;
    if(!mutex_trylock(&s->read_mutex))return -EBUSY;
    if(cmd!=SONY_SOURCE_IOCTL_CLAIM&&cmd!=SONY_SOURCE_IOCTL_RELEASE&&cmd!=SONY_SOURCE_IOCTL_CAPS&&
       cmd!=SONY_SOURCE_IOCTL_PEEK&&cmd!=SONY_SOURCE_IOCTL_FEEDBACK){rc=-ENOTTY;goto out;}
    spin_lock_irqsave(&s->lock,flags);
    if(cmd==SONY_SOURCE_IOCTL_CAPS){
        memset(&caps,0,sizeof(caps));caps.magic=SONY_SOURCE_QUEUE_MAGIC;
        caps.version=SONY_SOURCE_QUEUE_VERSION;caps.bytes=sizeof(caps);caps.capacity=s->queue.capacity;
        caps.instance=s->queue.instance;caps.descriptor_limit=SONY_SOURCE_QUEUE_SLOTS;caps.queued_sources=s->queue.count;
        caps.accepted_bytes=s->queue.accepted;caps.consumed_bytes=s->queue.consumed;caps.delivered_bytes=s->queue.delivered;
        caps.error=s->queue.first_error;caps.flags=(s->parent_live?SONY_SOURCE_DEVICE_PARENT_LIVE:0)|
            (s->consumer?SONY_SOURCE_DEVICE_CONSUMER:0)|(caps.error?SONY_SOURCE_DEVICE_FAULTED:0);
    }else if(cmd==SONY_SOURCE_IOCTL_FEEDBACK)feedback=s->feedback;
    else if(cmd==SONY_SOURCE_IOCTL_PEEK)rc=sony_source_queue_peek(&s->queue,&info);
    else if(cmd==SONY_SOURCE_IOCTL_CLAIM){
        rc=sony_source_queue_claim(&s->queue,0,0,0,0,0,&info);
        if(!rc){s->consumer_claimed=1;s->consumer_source=s->feedback_source=info.source_id;
                s->feedback_initial_frames=info.accepted_bytes/8;}
    }else{
        rc=sony_source_queue_release(&s->queue,id);if(!rc){s->consumer_claimed=0;s->consumer_source=0;s->feedback_source=0;s->feedback_initial_frames=0;}
    }
    spin_unlock_irqrestore(&s->lock,flags);if(rc)goto out;
    if(cmd==SONY_SOURCE_IOCTL_CAPS)copied=copy_to_user(user,&caps,sizeof(caps));
    else if(cmd==SONY_SOURCE_IOCTL_FEEDBACK)copied=copy_to_user(user,&feedback,sizeof(feedback));
    else if(cmd!=SONY_SOURCE_IOCTL_RELEASE)copied=copy_to_user(user,&info,sizeof(info));
    if(copied){rc=-EFAULT;if(cmd==SONY_SOURCE_IOCTL_CLAIM)wd_source_error(s,rc);}
out:mutex_unlock(&s->read_mutex);return rc;
}
static ssize_t wd_source_file_read(struct file *file,char __user *user,size_t count,loff_t *offset)
{
    struct wd_uac_source *s=file->private_data;struct sony_source_record record;
    unsigned capacity,bytes;unsigned long flags;int rc=s?0:-EBADF,copy_error;
    if(rc)return rc;if(!count)return 0;
    if(count<sizeof(record)+8)return -EINVAL;
    capacity=count-sizeof(record);if(capacity>SONY_SOURCE_QUEUE_MAX_READ)capacity=SONY_SOURCE_QUEUE_MAX_READ;capacity&=~7u;
    if(!mutex_trylock(&s->read_mutex))return -EBUSY;
    spin_lock_irqsave(&s->lock,flags);
    rc=sony_source_queue_read_begin(&s->queue,s->consumer_source,s->record_buffer+sizeof(record),capacity,&record);
    if(rc==SONY_SOURCE_QUEUE_END){
        memset(&record,0,sizeof(record));rc=sony_source_queue_peek(&s->queue,&record.source);
        if(!rc)record.byte_offset=record.source.consumed_bytes;
    }else if(!rc){
        /* The caller sees this header only on a positive read return. Avoid a
         * second header copy after commit, which could fail after delivery. */
        record.source.delivered_bytes=record.source.consumed_bytes;
    }
    spin_unlock_irqrestore(&s->lock,flags);if(rc)goto out;
    memcpy(s->record_buffer,&record,sizeof(record));bytes=sizeof(record)+record.payload_bytes;
    copy_error=copy_to_user(user,s->record_buffer,bytes)?-EFAULT:0;
    spin_lock_irqsave(&s->lock,flags);
    rc=wd_source_commit_record(&s->queue,&record,copy_error);
    spin_unlock_irqrestore(&s->lock,flags);
    if(rc)wake_up_interruptible(&s->ready);
out:mutex_unlock(&s->read_mutex);return rc?rc:bytes;
}
static unsigned int wd_source_file_poll(struct file *file,poll_table *wait)
{
    struct wd_uac_source *s=file->private_data;struct sony_source_info info;
    unsigned long flags;unsigned mask=0;if(!s)return POLLERR;
    poll_wait(file,&s->ready,wait);spin_lock_irqsave(&s->lock,flags);
    if(!sony_source_queue_peek(&s->queue,&info)&&
       (!s->consumer_claimed||info.accepted_bytes>info.consumed_bytes||info.flags&SONY_SOURCE_FINAL))mask|=POLLIN|POLLRDNORM;
    if(!s->parent_live)mask|=POLLHUP;
    spin_unlock_irqrestore(&s->lock,flags);return mask;
}
static const struct file_operations wd_source_file_ops={
    .owner=THIS_MODULE,.open=wd_source_file_open,.release=wd_source_file_release,
    .read=wd_source_file_read,.unlocked_ioctl=wd_source_file_ioctl,.poll=wd_source_file_poll,.llseek=no_llseek,
};
static noinline void wd_source_destroy(struct wd_uac_source *s)
{
    unsigned long flags;int rc;if(!s)return;
    spin_lock_irqsave(&s->lock,flags);s->parent_live=0;sony_source_queue_abort(&s->queue,-ENODEV);
    spin_unlock_irqrestore(&s->lock,flags);wake_up_interruptible(&s->ready);
    /* Never acquire read_mutex before misc_deregister (misc_open holds the
     * misc global mutex while entering our open/read_mutex). */
    if(s->registered){rc=misc_deregister(&s->misc);if(rc){wd_source_error(s,rc);return;}s->registered=0;}
    kref_put(&s->refs,wd_source_free);
}
static noinline int wd_source_create(struct audio_dev *a)
{
    struct wd_uac_source *s;int rc;
    if(a->wd_source)return -EALREADY;
    s=kzalloc(sizeof(*s),GFP_KERNEL);if(!s)return -ENOMEM;
    kref_init(&s->refs);spin_lock_init(&s->lock);mutex_init(&s->read_mutex);init_waitqueue_head(&s->ready);
    /* CPU-only staging, never handed to a DMA engine. A112+32768byte record
     * rounds a kmalloc request up to64KiB/order4 and fails on the live Android
     * allocator despite sufficient free pages. Allocate mapped individual
     * pages here in process context, before registering the consumer. */
    s->storage=vzalloc(SONY_SOURCE_QUEUE_MAX_BYTES);s->raw=vzalloc(WD_SOURCE_RAW_MAX);
    s->pcm=vzalloc(SONY_SOURCE_QUEUE_MAX_READ);
    s->record_buffer=vzalloc(sizeof(struct sony_source_record)+SONY_SOURCE_QUEUE_MAX_READ);
    if(!s->storage||!s->raw||!s->pcm||!s->record_buffer){rc=-ENOMEM;goto fail;}
    rc=sony_source_queue_init(&s->queue,s->storage,SONY_SOURCE_QUEUE_MAX_BYTES,wd_uac_boundary.instance);if(rc)goto fail;
    wd_source_feedback_init(s);
    s->misc.minor=MISC_DYNAMIC_MINOR;s->misc.name="walkdroid-uac-source";s->misc.fops=&wd_source_file_ops;
    s->parent_live=1;rc=misc_register(&s->misc);if(rc)goto fail;
    s->registered=1;a->wd_source=s;return 0;
fail:s->parent_live=0;kref_put(&s->refs,wd_source_free);return rc;
}
