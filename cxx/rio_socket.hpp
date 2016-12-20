#ifndef RIO_SOCKET_HPP
#define RIO_SOCKET_HPP

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RIO_BUFSIZE 8192     /* internal buffer size is 8K */

typedef struct {
  int rio_fd;                /* file descriptor for this internal buffer */
  int rio_cnt;               /* unread bytes in internal buffer */
  char *rio_bufptr;          /* next unread byte in internal buffer */
  char rio_buf[RIO_BUFSIZE]; /* internal buffer */
} rio_t;

// robust socket IO utilities.
// copied from the book <<Computer Systems: A Programmer's Perspective>>,
// with underlaying syscall read/write changed to recv/send.

ssize_t rio_writen(int fd, const void *buf, size_t n) {
  size_t nleft = n;
  ssize_t nwritten = 0;
  const char *bufptr = (const char *) buf;

  while (nleft > 0) {
    /* flag is set to MSG_NOSIGNAL to ignore SIGPIPE when writing to a permaturely closed socket */
    nwritten = send(fd, bufptr, nleft, MSG_NOSIGNAL);
    if (nwritten < 0) {
      if (errno == EINTR) {
        nwritten = 0;
      } else {
        return nwritten;
      }
    }
    nleft -= nwritten;
    bufptr += nwritten;
  }
  return n;
}

void rio_initb(rio_t *rioptr, int fd) {
  rioptr->rio_fd = fd;
  rioptr->rio_cnt = 0;
  rioptr->rio_bufptr = rioptr->rio_buf;
}

static ssize_t rio_read(rio_t *rioptr, char *buf, size_t n) {
  int cnt;

  /* if the buffer is empty, read until we get something or EOF */
  while (rioptr->rio_cnt <= 0) {
    rioptr->rio_cnt = recv(rioptr->rio_fd, rioptr->rio_buf, RIO_BUFSIZE, 0);
    if (rioptr->rio_cnt < 0) {
      if (errno != EINTR) {
        return -1;
      }
    } else if (rioptr->rio_cnt == 0) {
      return 0;
    } else {
      rioptr->rio_bufptr = rioptr->rio_buf;
    }
  }

  /* copy min(n, rioptr->rio_cnt) bytes from internal buffer to user buffer */
  cnt = n;
  if (rioptr->rio_cnt < cnt) {
    cnt = rioptr->rio_cnt;
  }
  memcpy(buf, rioptr->rio_bufptr, cnt);
  rioptr->rio_bufptr += cnt;
  rioptr->rio_cnt -= cnt;
  return cnt;
}

ssize_t rio_readlineb(rio_t *rioptr, void **buf) {
  int n, ret;
  int alloc = 128;
  char c, *bufptr;

  bufptr = (char *) malloc(alloc);
  if (bufptr == NULL) {
    return -1;
  }

  for (n = 1; ; ++n) {
    if (n >= alloc) {
      void *tmp = malloc(alloc * 2);
      if (tmp == NULL) {
        free(bufptr);
        return -1;
      } else {
        memcpy(tmp, bufptr, alloc);
        free(bufptr);
        bufptr = (char *) tmp;
        alloc *= 2;
      }
    }
    ret = rio_read(rioptr, &c, 1);
    if (ret == 1) {
      *(bufptr + n - 1) = c;
      if (c == '\n') {
        break;
      }
    } else if (ret == 0) {
      if (n == 1) {
        n -= 1;
      }
      break;
    } else {
      free(bufptr);
      return ret;
    }
  }
  *(bufptr + n) = 0;
  *buf = bufptr;
  return n;
}

#endif // RIO_SOCKET_HPP
