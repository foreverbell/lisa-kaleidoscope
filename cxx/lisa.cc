#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <memory>
#include <type_traits>
#include <vector>
#include <string>

#include "jpeg.hpp"

using std::list;
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
void loop(const JPEG& lisa) {
  const int population = 100;
  vector<list<T>> shapes(population);

  int w = lisa.width(), h = lisa.height();
  int64_t old_best_score = ~0ull >> 1;

  for (int it = 0; ; ++it) {
    int64_t best_score = 0;
    int best_index = -1;
    list<T> best = shapes[0];

    for (int i = 0; i < shapes.size(); ++i) {
      mutate<T>(&shapes[i], w, h);

      unique_ptr<JPEG> tmp = draw<T>(shapes[i], w, h);
      int64_t score = JPEG::distance(*tmp, lisa);
      if (best_index == -1 || score < best_score) {
        best_score = score;
        best_index = i;
      }
    }
    if (best_score < old_best_score) {
      old_best_score = best_score;
      best = shapes[best_index];
    }
    for (int i = 0; i < shapes.size(); ++i) {
      shapes[i] = best;
    }
    if (it % 30 == 0) {
      unique_ptr<JPEG> tmp = draw(shapes[0], w, h);
      tmp->save("/tmp/lisa.jpg");
      std::clog << it << " " << shapes[0].size() << " " << old_best_score << std::endl;
    }
  }
}

int main(int argv, char** argc) {
  JPEG lisa;
  lisa.load("../pics/lisa.jpg");

  if (argv == 2 && strcmp(argc[1], "-square") == 0) {
    std::clog << "square" << std::endl;
    loop<Square>(lisa);
  } else {
    std::clog << "circle" << std::endl;
    loop<Circle>(lisa);
  }

  return 0;
}
