/* riscos.c -  RISC OS stuff
 *	Copyright (C) 2001 Free Software Foundation, Inc.
 *
 * This file is part of GnuPG for RISC OS.
 *
 * GnuPG is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GnuPG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef __RISCOS__C__
#define __RISCOS__C__

#include <config.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <kernel.h>
#include <sys/swis.h>
#include "util.h"
#include "memory.h"

#define __UNIXLIB_INTERNALS
#include <sys/unix.h>
#undef __UNIXLIB_INTERNALS

/* RISC OS file open descriptor control list */

struct fds_item {
    int fd;
    struct fds_item *next;
};
static struct fds_item *fds_list = NULL;
static int initialized = 0;


/* RISC OS functions */

pid_t
riscos_getpid(void)
{
    _kernel_swi_regs r;

    r.r[0] = 3;
    if (_kernel_swi(Wimp_ReadSysInfo, &r, &r))
        log_fatal("Wimp_ReadSysInfo failed: Couldn't get WimpState (R0=3)!\n");

    if (!r.r[0])
        return (pid_t) 0;

    r.r[0] = 5;
    if (_kernel_swi(Wimp_ReadSysInfo, &r, &r))
        log_fatal("Wimp_ReadSysInfo failed: Couldn't get task handle (R0=5)!\n");

    return (pid_t) r.r[0];
}

int
riscos_kill(pid_t pid, int sig)
{
    _kernel_swi_regs r;
    int buf[4];

    if (sig)
        kill(pid, sig);

    r.r[0] = 0;
    do {
        r.r[1] = (int) buf;
        r.r[2] = 16;
        if (_kernel_swi(TaskManager_EnumerateTasks, &r, &r))
            log_fatal("TaskManager_EnumerateTasks failed!\n");
        if (buf[0] == pid)
            return 0;
    } while (r.r[0] >= 0);

    return __set_errno(ESRCH);
}

FILE *
riscos_fopen(const char *filename, const char *mode)
{
    FILE *fp;
    _kernel_swi_regs r;
    _kernel_oserror *e;
    int filetype;

    r.r[0] = 17;
    r.r[1] = (int) filename;
    if (e =_kernel_swi(OS_File, &r, &r))
        log_fatal("Can't retrieve object information for %s\n", filename);
    if (r.r[0] == 2) {
        errno = EISDIR;
        return NULL;
    }
    if (r.r[0] == 3) {
        /* setting file to to non-image file, after fopening, restore */
        filetype = (r.r[2] >> 8) & 0xfff;
        set_filetype(filename, 0xfff);
        fp = fopen(filename, mode);
        set_filetype(filename, filetype);
    } else {
      fp = fopen(filename, mode);
    }
    return fp;
}

int
riscos_open(const char *filename, int oflag, ...)
{
    va_list ap;
    _kernel_swi_regs r;
    _kernel_oserror *e;
    int fd, mode, filetype;

    r.r[0] = 17;
    r.r[1] = (int) filename;
    if (e =_kernel_swi(OS_File, &r, &r))
        log_fatal("Can't retrieve object information for %s\n", filename);
    if (r.r[0] == 2) {
        errno = EISDIR;
        return NULL;
    }

    va_start(ap, oflag);
    mode = va_arg(ap, int);
    va_end(ap);

    if (r.r[0] == 3) {
        /* setting file to to non-image file, after opening, restore */
        filetype = (r.r[2] >> 8) & 0xfff;
        set_filetype(filename, 0xfff);
        if (!mode)
            fd = open(filename, oflag);
        else
            fd = open(filename, oflag, mode);
        set_filetype(filename, filetype);
    } else {
        if (!mode)
            fd = open(filename, oflag);
        else
            fd = open(filename, oflag, mode);
    }
    return fd;
}

int
riscos_fstat(int fildes, struct stat *buf)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    char *filename;
    int rc, filetype;
    int handle = (int) __u->fd[fildes].handle;

    r.r[0] = 7;
    r.r[1] = handle;
    r.r[2] = 0;
    r.r[5] = 0;
    if (e = _kernel_swi(OS_Args, &r, &r))
        log_fatal("Can't convert from file handle to name\n");

    filename = m_alloc(1 - r.r[5]);

    r.r[0] = 7;
    r.r[1] = handle;
    r.r[2] = (int) filename;
    r.r[5] = 1-r.r[5];
    if (e = _kernel_swi(OS_Args, &r, &r))
        log_fatal("Can't convert from file handle to name\n");

    r.r[0] = 17;
    r.r[1] = (int) filename;
    if (e =_kernel_swi(OS_File, &r, &r))
        log_fatal("Can't retrieve object information for %s\n", filename);
    if (r.r[0] == 2) {
        errno = EISDIR;
        return NULL;
    }
    if (r.r[0] == 3) {
        /* setting file to to non-image file, after fstating, restore */
        filetype = (r.r[2] >> 8) & 0xfff;
        set_filetype(filename, 0xfff);
        rc = fstat(fildes, buf);
        set_filetype(filename, filetype);
    } else {
        rc = fstat(fildes, buf);
    }

    m_free(filename);

    return rc;
}

#ifdef DEBUG
void
dump_fdlist(void)
{
    struct fds_item *iter = fds_list;
    printf("list of open file descriptors:\n");
    while (iter) {
        printf("  %i\n", iter->fd);
        iter = iter->next;
    }
}
#endif /* DEBUG */

int
fdopenfile(const char *filename, const int allow_write)
{
    struct fds_item *h;
    int fd;
    if (allow_write)
        fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
    else
        fd = open(filename, O_RDONLY);
    if (fd == -1)
        log_error("can't open file %s: %i, %s\n", filename, errno, strerror(errno));

    if (!initialized) {
        atexit (close_fds);
        initialized = 1;
    }

    h = fds_list;
    fds_list = (struct fds_item *) m_alloc(sizeof(struct fds_item));
    fds_list->fd = fd;
    fds_list->next = h;

    return fd;
}

void
close_fds(void)
{
    FILE *fp;
    struct fds_item *h = fds_list;
    while( fds_list ) {
        h = fds_list->next;
        fp = fdopen (fds_list->fd, "a");
        if (fp)
            fflush(fp);
        close(fds_list->fd);
        m_free(fds_list);
        fds_list = h;
    }
}

int
renamefile(const char *old, const char *new)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;

    r.r[0] = 25;
    r.r[1] = (int) old;
    r.r[2] = (int) new;
    if (e = _kernel_swi(OS_FSControl, &r, &r)) {
        if (e->errnum == 214)
            return __set_errno(ENOENT);
        if (e->errnum == 176)
            return __set_errno(EEXIST);
        printf("Error during renaming: %i, %s\n", e->errnum, e->errmess);
    }
    return 0;
}

char *
gstrans(const char *old)
{
    _kernel_swi_regs r;
    int c = 0;
    int size = 256;
    char *buf, *tmp;

    buf = (char *) m_alloc(size);
    do {
        r.r[0] = (int) old;
        r.r[1] = (int) buf;
        r.r[2] = size;
        _kernel_swi_c(OS_GSTrans, &r, &r, &c);
        if (c) {
            size += 256;
            tmp = (char *) m_realloc(buf, size);
            if (!tmp)
                 log_fatal("Can't claim memory for OS_GSTrans buffer!\n");
            buf = tmp;
        }
    } while (c);

    buf[r.r[2]] = '\0';
    tmp = (char *) m_realloc(buf, r.r[2] + 1);
    if (!tmp)
        log_fatal("Couldn't realloc memory after OS_GSTrans!\n");

    return tmp;
}

void
set_filetype(const char *filename, const int type)
{
    _kernel_swi_regs r;

    r.r[0] = 18;
    r.r[1] = (int) filename;
    r.r[2] = type;
    
    if (_kernel_swi(OS_File, &r, &r))
        log_fatal("Can't set filetype for %s\n", filename);
}        

void
not_implemented(const char *feature)
{
    log_info("%s is not implemented in the RISC OS version!\n", feature);
}

#endif /* !__RISCOS__C__ */
