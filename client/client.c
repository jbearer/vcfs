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
#include <netinet/ip.h>

static const char *mount_point;
static int port;
static unsigned long ip;

typedef struct vcfs_file_handle
{
    int fd;
} vcfs_file_handle;

static char *vcfs_repo_path(const char *path)
{
    const char *prefix = getenv("VCFS_PREFIX");
    if (prefix == NULL) {
        prefix = "/vcfs";
    }

    size_t prefix_len = strlen(prefix);
    size_t mount_point_len = strlen(mount_point);
    size_t path_len = strlen(path);

    char *rpath = (char *)malloc(prefix_len + mount_point_len + path_len);
    if (rpath == NULL) {
        perror("vcfs_repo_path:malloc");
        abort();
    }

    strcpy(rpath, prefix);
    strcpy(rpath + prefix_len, mount_point);
    strcpy(rpath + prefix_len + mount_point_len, path);

    printf("path is : %s \n", rpath);

    return rpath;
}

void pull_if_needed() {
    int sockfd = *(int *)(fuse_get_context()->private_data);

    uint32_t size;
    if (read(sockfd, &size, sizeof(size)) != sizeof(size)) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return;
        } else {
            // offline mode
            return;
        }
    }
    size = ntohl(size);

    char * buf = (char *) malloc(size);
    if (buf == NULL) {
        perror("malloc");
        abort();
    }
    if (read(sockfd, buf, size) != size) {
        perror("read");
        abort();
    }

    printf("need to pull %.*s\n", size, buf);

    if (system("git pull")) {
        printf("failed git pull\n");
        // either offline or merge conflict
    }

    free(buf);
}

static void * vcfs_init(struct fuse_conn_info *conn)
{
    (void) conn;
    char * path = vcfs_repo_path("");
    if (chdir(path)) {
        perror("change path");
        abort();
    }
    free(path);

    int * sockfd = (int *) malloc(sizeof(int));
    if (sockfd == NULL) {
        perror("malloc fail");
        abort();
    }
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0) {
        perror("socket");
        abort();
    }

    struct sockaddr_in hookaddr = {0};
    hookaddr.sin_family = AF_INET;
    hookaddr.sin_port = htons(port);
    hookaddr.sin_addr.s_addr = htonl(ip);

    if (connect(*sockfd, (struct sockaddr *) &hookaddr, sizeof(hookaddr)) < 0) {
        perror("hook connection");
        abort();
    }

    // make socket non blocking
    int flags = fcntl(*sockfd, F_GETFL, 0);
    if (flags == -1) abort();
    flags |= O_NONBLOCK;
    if (fcntl(*sockfd, F_SETFL, flags)) abort();

    return sockfd;
}

static void vcfs_destroy(void* private_data)
{
    close(*(int *)private_data);
    free(private_data);
}

static int vcfs_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    (void)path;

    pull_if_needed();

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;
    return fstat(fh->fd, stbuf);
}

static int vcfs_getattr(const char *path, struct stat *stbuf)
{
    pull_if_needed();

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
    pull_if_needed();

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
    pull_if_needed();

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
    pull_if_needed();

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
    pull_if_needed();

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
    pull_if_needed();

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
    pull_if_needed();

    int res = 0;

    char * rpath = vcfs_repo_path(path);

    res = unlink(rpath);
    if (res == -1)
        res =  -errno;

    free(rpath);
    return res;
}

static int vcfs_rmdir(const char *path)
{
    pull_if_needed();

    int res = 0;

    char * rpath = vcfs_repo_path(path);

    res = rmdir(rpath);
    if (res == -1)
        res = -errno;

    free(rpath);
    return res;
}

static int vcfs_symlink(const char *to, const char *from)
{
    pull_if_needed();

    int res = 0;

    char * rfrom = vcfs_repo_path(from);

    res = symlink(to, rfrom);
    if (res == -1)
        res = -errno;

    free(rfrom);
    return res;
}

static int vcfs_rename(const char *from, const char *to)
{
    pull_if_needed();

    int res = 0;

    char * rfrom = vcfs_repo_path(from);
    char * rto = vcfs_repo_path(to);

    char * buf = malloc(30 + strlen(rfrom) + strlen(rto));

    sprintf(buf, "git ls-files --error-unmatch %s", rfrom);
    if (system(buf)) {
        // file untracked
        res = rename(rfrom, rto);
        if (res == -1)
            res = -errno;
    } else {
        sprintf(buf, "git mv %s %s", rfrom, rto);
        printf("going to git mv: %s \n", buf);
        if (system(buf)) {
            res = -1;
        }
    }

    free(rfrom);
    free(rto);
    return res;
}

static int vcfs_link(const char *from, const char *to)
{
    pull_if_needed();

    char * rfrom = vcfs_repo_path(from);
    char * rto = vcfs_repo_path(to);

    int res = link(rfrom, rto);
    if (res == -1)
        res = -errno;

    free(rto);
    free(rfrom);

    return res;
}

static int vcfs_chmod(const char *path, mode_t mode)
{
    pull_if_needed();

    char * rpath = vcfs_repo_path(path);

    int res = chmod(rpath, mode);
    if (res == -1)
        res =  -errno;

    free(rpath);

    return res;
}

static int vcfs_chown(const char *path, uid_t uid, gid_t gid)
{
    pull_if_needed();

    char * rpath = vcfs_repo_path(path);

    int res = lchown(rpath, uid, gid);
    if (res == -1)
        res = -errno;

    free(rpath);

    return res;
}

static int vcfs_truncate(const char *path, off_t size)
{
    pull_if_needed();

    char * rpath = vcfs_repo_path(path);

    int res = truncate(rpath, size);
    if (res == -1)
        res = -errno;

    free(rpath);

    return res;
}

static int vcfs_utimens(const char *path, const struct timespec ts[2])
{
    pull_if_needed();

    char * rpath = vcfs_repo_path(path);

    struct timeval tv[2];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    int res = utimes(rpath, tv);
    if (res == -1)
        res = -errno;

    free(rpath);

    return res;
}

static int vcfs_open(const char *path, struct fuse_file_info *fi)
{
    pull_if_needed();

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
    (void)path;

    pull_if_needed();

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;

    int res = pread(fh->fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int vcfs_write(const char *path, const char *buf, size_t size,
             off_t offset, struct fuse_file_info *fi)
{
    (void)path;

    pull_if_needed();

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;

    int res = pwrite(fh->fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int vcfs_statfs(const char *path, struct statvfs *stbuf)
{
    pull_if_needed();

    char *rpath = vcfs_repo_path(path);

    int res = statvfs(rpath, stbuf);
    if (res == -1) {
        res = -errno;
    }

    free(rpath);

    return res;
}

static int vcfs_fsync(const char *path, int isdatasync,
             struct fuse_file_info *fi)
{
    (void)path;
    (void)isdatasync;
    (void)fi;

    pull_if_needed();

    if (system("git diff-index --quiet HEAD --") == 0) {
        // No changes
        return 0;
    }

    if (system("git commit -am \"automated commit\"")) {
        return -1;
    }

    if (system("git push")) {
        return -1;
    }

    return 0;
}

static int vcfs_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;

    vcfs_file_handle *fh = (vcfs_file_handle *)fi->fh;

    close(fh->fd);
    free(fh);

    return vcfs_fsync(path, 0, fi);
}

static int vcfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    return vcfs_release(path, fi);
}

static struct fuse_operations vcfs_oper = {
    .init       = vcfs_init,
    .destroy    =vcfs_destroy,
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
    .fsync      = vcfs_fsync
};

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <mnt> <ip> <port>\n", argv[0]);
        return 1;
    }
    mount_point = argv[argc-3];
    const char *ip_str = argv[argc-2];
    port = atoi(argv[argc-1]);

    int ip_bytes[4];
    if (sscanf(ip_str, "%d.%d.%d.%d", ip_bytes, ip_bytes + 1, ip_bytes + 2, ip_bytes + 3) != 4) {
        fprintf(stderr, "Invalid ip address %s\n", ip_str);
        return 1;
    }
    ip = ip_bytes[3] + ip_bytes[2]*256 + ip_bytes[1]*256*256 + ip_bytes[0]*256*256*256;

    umask(0);
    return fuse_main(argc-2, argv, &vcfs_oper, NULL);
}
