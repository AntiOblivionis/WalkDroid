/* Commit a data record by its payload, not by its wrapping reservation value. */
static int wd_source_commit_record(struct sony_source_queue *queue,
                                   const struct sony_source_record *record,
                                   int copy_error)
{
    struct sony_source_record committed;
    if(record->payload_bytes)
        return sony_source_queue_read_end(queue,record->reservation,copy_error,&committed);
    return copy_error?sony_source_queue_abort(queue,copy_error):0;
}
