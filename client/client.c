/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall `pkg-config fuse --cflags --libs` fusexmp.c -o fusexmp
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
// #define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

static const char *mount_point;

typedef struct vcfs_file_handle
{
    int fd;
} vcfs_file_handle;

char *vcfs_repo_path(const char *path)
{
    static const char *prefix = "/vcfs";

    size_t prefix_len = strlen(prefix);
    size_t mount_point_len = strlen(mount_point);
    size_t path_len = strlen(path);

    char *rpath = (char *)malloc(prefix_len + mount_point_len + path_len);
    if (rpath == NULL) {
        perror("vcfs_repo_path:malloc");
        return NULL;
    }

    strcpy(rpath, prefix);
    strcpy(rpath + prefix_len, mount_point);
    strcpy(rpath + prefix_len + mount_point_len, path);

    return rpath;
}

static int vcfs_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)path;

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;
    return fstat(fh->fd, stbuf);
}

static int vcfs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    char *rpath = vcfs_repo_path(path);

    res = lstat(rpath, stbuf);
    if (res == -1) {
        res = -errno;
    }

    free(rpath);

    return res;
}

static int vcfs_access(const char *path, int mask)
{
    int res = 0;

    char *rpath = vcfs_repo_path(path);

    res = access(rpath, mask);
    if (res == -1) {
        res = -errno;
    }

    free(rpath);

    return 0;
}

static int vcfs_readlink(const char *path, char *buf, size_t size)
{
    int res = 0;

    char *rpath = vcfs_repo_path(path);

    res = readlink(rpath, buf, size - 1);
    if (res == -1) {
        res = -errno;
    } else {
        buf[res] = '\0';
    }

    free(rpath);

    return 0;
}


static int vcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) path;

    dp = fdopendir(((vcfs_file_handle *)fi->fh)->fd);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static int vcfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;
    int ret = 0;

    char * rpath = vcfs_repo_path(path);

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    if (S_ISREG(mode)) {
        res = open(rpath, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(rpath, mode);
    else
        res = mknod(rpath, mode, rdev);
    if (res == -1)
        ret = -errno;

    free(rpath);
    return ret;
}

static int vcfs_mkdir(const char *path, mode_t mode)
{
    int res = 0;

    char * rpath = vcfs_repo_path(path);

    res = mkdir(rpath, mode);
    if (res == -1)
        res = -errno;

    free(rpath);
    return res;
}

static int vcfs_unlink(const char *path)
{
    int res = 0;

    char * rpath = vcfs_repo_path(path);

    res = unlink(rpath);
    if (res == -1)
        res =  -errno;

    return res;
}

static int vcfs_rmdir(const char *path)
{
    int res;

    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_symlink(const char *to, const char *from)
{
    int res;

    res = symlink(to, from);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_rename(const char *from, const char *to)
{
    int res;

    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_link(const char *from, const char *to)
{
    int res;

    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_chmod(const char *path, mode_t mode)
{
    int res;

    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_truncate(const char *path, off_t size)
{
    int res;

    res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    struct timeval tv[2];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    res = utimes(path, tv);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_open(const char *path, struct fuse_file_info *fi)
{
    int res = 0;

    vcfs_file_handle *fh = (vcfs_file_handle *)malloc(sizeof(vcfs_file_handle));
    if (fh == NULL) {
        res = -errno;
        goto err_alloc_fh;
    }

    char *rpath = vcfs_repo_path(path);
    if (rpath == NULL) {
        res = -errno;
        goto err_rpath;
    }

    fh->fd = open(rpath, fi->flags);
    if (fh->fd == -1) {
        res = -errno;
        goto err_open;
    }

    fi->fh = (intptr_t)fh;

err_open:
    free(rpath);
err_rpath:
    if (res) free(fh);
err_alloc_fh:
    return res;
}

static int vcfs_opendir(const char *path, struct fuse_file_info *fi)
{
    return vcfs_open(path, fi);
}

static int vcfs_read(const char *path, char *buf, size_t size, off_t offset,
            struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int vcfs_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int vcfs_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int vcfs_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;

    close(fh->fd);
    free(fh);

    return 0;
}

static int vcfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    return vcfs_release(path, fi);
}

static int vcfs_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int vcfs_setxattr(const char *path, const char *name, const char *value,
            size_t size, int flags)
{
    int res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int vcfs_getxattr(const char *path, const char *name, char *value,
            size_t size)
{
    int res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int vcfs_listxattr(const char *path, char *list, size_t size)
{
    int res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int vcfs_removexattr(const char *path, const char *name)
{
    int res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations vcfs_oper = {
    .getattr    = vcfs_getattr,
    .fgetattr   = vcfs_fgetattr,
    .access     = vcfs_access,
    .readlink   = vcfs_readlink,
    .readdir    = vcfs_readdir,
    .mknod      = vcfs_mknod,
    .mkdir      = vcfs_mkdir,
    .symlink    = vcfs_symlink,
    .unlink     = vcfs_unlink,
    .rmdir      = vcfs_rmdir,
    .rename     = vcfs_rename,
    .link       = vcfs_link,
    .chmod      = vcfs_chmod,
    .chown      = vcfs_chown,
    .truncate   = vcfs_truncate,
    .utimens    = vcfs_utimens,
    .open       = vcfs_open,
    .opendir    = vcfs_opendir,
    .read       = vcfs_read,
    .write      = vcfs_write,
    .statfs     = vcfs_statfs,
    .release    = vcfs_release,
    .releasedir = vcfs_releasedir,
    .fsync      = vcfs_fsync,
#ifdef HAVE_SETXATTR
    .setxattr   = vcfs_setxattr,
    .getxattr   = vcfs_getxattr,
    .listxattr  = vcfs_listxattr,
    .removexattr    = vcfs_removexattr,
#endif
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mnt>\n", argv[0]);
        return 1;
    }
    mount_point = argv[argc-1];

    umask(0);
    return fuse_main(argc, argv, &vcfs_oper, NULL);
}
