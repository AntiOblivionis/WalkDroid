/* WalkDroid adapter for the platform's libfuse-lite API. */
static int fuse_exfat_main(char* mount_options, char* mount_point)
{
    struct fuse_args mount_args = FUSE_ARGS_INIT(0, NULL);
    struct fuse_args loop_args = FUSE_ARGS_INIT(0, NULL);
    struct fuse_chan* channel = NULL;
    struct fuse* handle = NULL;
    int result = 1;

    if (fuse_opt_add_arg(&mount_args, "exfat") == -1 ||
        fuse_opt_add_arg(&mount_args, "-o") == -1 ||
        fuse_opt_add_arg(&mount_args, mount_options) == -1 ||
        fuse_opt_add_arg(&loop_args, "exfat") == -1)
        goto done;
    channel = fuse_mount(mount_point, &mount_args);
    if (!channel)
        goto done;
    handle = fuse_new(channel, &loop_args, &fuse_exfat_ops,
                      sizeof(fuse_exfat_ops), NULL);
    if (!handle)
        goto done;
    if (daemon(0, 0) == -1)
        goto done;
    result = fuse_loop(handle);

done:
    if (channel)
        fuse_unmount(mount_point, channel);
    if (handle)
        fuse_destroy(handle);
    else
        exfat_unmount(&ef);
    fuse_opt_free_args(&mount_args);
    fuse_opt_free_args(&loop_args);
    return result;
}
