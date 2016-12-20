#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

#include "jpeg.hpp"
#include "rio_socket.hpp"

using std::list;
using std::lock_guard;
using std::mutex;
using std::string;
using std::thread;
using std::unique_ptr;
using std::vector;

struct Circle {
  int x, y, radius;
  uint8_t a;
  uint8_t r, g, b;
};
struct Square {
  int x, y, radius;
  uint8_t a;
  uint8_t r, g, b;
};

template <typename T>
void mutate(list<T>* shapes, int w, int h) {
  if (rand() & 1) {
    // Add a new shape to the list tail.
    T new_shape;

    new_shape.x = rand() % w;
    new_shape.y = rand() % h;
    new_shape.radius = rand() % 50 + 1;
    new_shape.r = rand() & 0xff;
    new_shape.g = rand() & 0xff;
    new_shape.b = rand() & 0xff;
    new_shape.a = rand() % 100 + 20;

    shapes->push_back(new_shape);
  } else {
    // Randomly delete a shape at any position.
    if (!shapes->empty()) {
      int index = rand() % shapes->size();
      typename list<T>::iterator it = shapes->begin();
      std::advance(it, index);
      shapes->erase(it);
    }
  }

  // Continue mutating with 50% possibility.
  if (rand() & 1) {
    mutate<T>(shapes, w, h);
  }
}

// I like static polymorphism, I hate virtual functions!
template <
  typename T,
  typename = typename std::enable_if<std::is_same<T, Circle>::value || std::is_same<T, Square>::value>::type
>
unique_ptr<JPEG> draw(const list<T>& shapes, int w, int h) {
  unique_ptr<JPEG> img = std::make_unique<JPEG>();

  img->create(w, h);

  for (typename list<T>::const_iterator it = shapes.begin(); it != shapes.end(); ++it) {
    int from = std::max(0, it->y - it->radius), to = std::min(h - 1, it->y + it->radius);
    int radius2 = it->radius * it->radius;
    int r_blend = int(it->a) * it->r;
    int g_blend = int(it->a) * it->g;
    int b_blend = int(it->a) * it->b;

    for (int y = from; y <= to; ++y) {
      int stride;

      if (std::is_same<T, Circle>::value) {
        stride = int(sqrt(float(radius2 - (y - it->y) * (y - it->y))));
      } else if (std::is_same<T, Square>::value) {
        stride = it->radius;
      }

      for (int x = std::max(0, it->x - stride), to_x = std::min(w - 1, it->x + stride); x <= to_x; ++x) {
        uint8_t* ptr = img->at(x, y);
        *ptr = ((255 - it->a) * (*ptr) + r_blend) >> 8;
        ptr += 1;
        *ptr = ((255 - it->a) * (*ptr) + g_blend) >> 8;
        ptr += 1;
        *ptr = ((255 - it->a) * (*ptr) + b_blend) >> 8;
      }
    }
  }
  return img;
}

template <typename T>
struct Lisa {
  mutex mutex;
  int width, height;
  int round;     // which round is right now
  int64_t score; // the best score of lisa
  list<T> shape; // the best shape
};

template <typename T>
void lisaLoop(const JPEG& img, Lisa<T>* lisa) {
  const int population = 100;
  vector<list<T>> shapes(population);

  int w = img.width(), h = img.height();

  lisa->score = ~0ull >> 1;
  lisa->shape = shapes[0];

  for (int it = 0; ; ++it) {
    int best_index = -1;
    int64_t best_score = 0;

    for (int i = 0; i < shapes.size(); ++i) {
      mutate<T>(&shapes[i], w, h);

      unique_ptr<JPEG> tmp = draw<T>(shapes[i], w, h);
      int64_t score = JPEG::distance(*tmp, img);
      if (best_index == -1 || score < best_score) {
        best_score = score;
        best_index = i;
      }
    }
    {
      lock_guard<mutex> scope_lock(lisa->mutex);

      lisa->round = it;
      if (best_score < lisa->score) {
        lisa->score = best_score;
        lisa->shape = shapes[best_index];
      } else {
        best_score = lisa->score;
      }
      shapes[0] = lisa->shape;
    }
    for (int i = 1; i < shapes.size(); ++i) {
      shapes[i] = shapes[0];
    }
    if (it % 30 == 0) {
      fprintf(stderr, "round = %d, shapes = %zu, best = %ld.\n", it, shapes[0].size(), best_score);
    }
  }
}

int openListenfd(int port) {
  int listenfd, optval = 1;
  int ret;
  struct sockaddr_in server_addr;

  if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    return listenfd;
  }

  if ((ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(optval))) < 0) {
    return ret;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); /* restrict incoming client to localhost */
  server_addr.sin_port = htons((unsigned short) port);

  if ((ret = bind(listenfd, (struct sockaddr *) &server_addr, sizeof(server_addr))) < 0) {
    return ret;
  }

  if ((ret = listen(listenfd, 16)) < 0) {
    return ret;
  }

  return listenfd;
}

// To make things simple, we will sliently discard all bad requests.
template <typename T>
void httpHandler(int connfd, Lisa<T>* lisa) {
  rio_t rio;
  int nread;
  char* buf;

  rio_initb(&rio, connfd);
  nread = rio_readlineb(&rio, (void**) &buf);
  if (nread < 0) {
    return;
  }

  unique_ptr<char, decltype(free)*> buf_free(buf, free);

  FILE* pipe = fdopen(connfd, "wb");

  if (pipe == nullptr) {
    return;
  }

  unique_ptr<FILE, decltype(fclose)*> pipe_close(pipe, fclose);

  if (strncasecmp(buf, "GET ", 4) != 0) {
    return;
  }
  buf += 4;
  for (char* cur = buf; *cur != 0; ++cur) {
    if (*cur == ' ') {
      *cur = 0;
      break;
    }
  }

  // TODO: Use rio_writen.
  if (strcasecmp(buf, "/lisa") == 0) {
    lock_guard<mutex> scope_lock(lisa->mutex);

    fprintf(pipe, "HTTP/1.0 200 OK\r\n");
    fprintf(pipe, "Content-Type: text/html\r\n");
    fprintf(pipe, "\r\n");
    fprintf(pipe, R"(
      <html>
        Round: %d<br/>
        Score: %ld<br/>
        <img src="lisa.jpg">
      </html>
      )", lisa->round, lisa->score);
  } else if (strcasecmp(buf, "/lisa.jpg") == 0) {
    lock_guard<mutex> scope_lock(lisa->mutex);

    fprintf(pipe, "HTTP/1.0 200 OK\r\n");
    fprintf(pipe, "Content-Type: image/jpeg\r\n");
    fprintf(pipe, "\r\n");

    draw(lisa->shape, lisa->width, lisa->height)->save(pipe);
  }
}

template <typename T>
void run(int listenfd, const JPEG& img) {
  Lisa<T> lisa;

  lisa.width = img.width();
  lisa.height = img.height();

  auto th = thread([&]() {
    int connfd;
    socklen_t sockaddr_len;
    struct sockaddr_in client_addr;

    sockaddr_len = sizeof(client_addr);
    while (1) {
      memset(&client_addr, 0, sockaddr_len);
      if ((connfd = accept(listenfd, (struct sockaddr *) &client_addr, &sockaddr_len)) < 0) {
        if (errno != EINTR && errno != ECONNABORTED) {
          fprintf(stderr, "error: unable to accept a new connect descriptor.\n");
          continue;
        }
      } else {
        fprintf(stderr, "connection from %s, port %d.\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        httpHandler(connfd, &lisa);
        close(connfd);
      }
    }
  });

  lisaLoop<T>(img, &lisa);
}

int main(int argv, char** argc) {
  JPEG img;
  FILE* infile = fopen("../pics/lisa.jpg", "rb");
  img.load(infile);
  fclose(infile);

  int listenfd = openListenfd(8080);
  if (listenfd < 0) {
    fprintf(stderr, "error: unable to open socket listen descriptor.\n");
    return 1;
  }

  if (argv == 2 && strcmp(argc[1], "-square") == 0) {
    fprintf(stderr, "square.\n");
    run<Square>(listenfd, img);
  } else {
    fprintf(stderr, "circle.\n");
    run<Circle>(listenfd, img);
  }

  return 0;
}
