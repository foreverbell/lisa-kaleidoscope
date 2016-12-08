// Requires libjpeg.
// sudo apt-get install libjpeg-dev

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <jpeglib.h>
#include <list>
#include <memory>
#include <type_traits>
#include <vector>

using std::list;
using std::unique_ptr;
using std::vector;

class JPEG final {
 public:
  JPEG() : width_(0), height_(0), data_ptr_(nullptr) { }

  JPEG(JPEG const&) = delete;
  JPEG& operator=(JPEG const&) = delete;

  ~JPEG() {
    if (data_ptr_ != nullptr) {
      delete [] data_ptr_;
    }
  }

  int width() const { return width_; }
  int height() const { return height_; }
  uint8_t* ptr() const { return data_ptr_; }
  uint8_t* at(int x, int y) { return data_ptr_ + (y * width_ + x) * 3; }

  bool load(const char* path) {
    if (data_ptr_ != nullptr) {
      delete [] data_ptr_;
    }

    int r, g, b;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY jbuffer;
    int row_stride;
    FILE* infile = fopen(path, "rb");

    if (infile == nullptr) {
      return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    width_ = cinfo.output_width;
    height_ = cinfo.output_height;

    uint8_t* ptr = new uint8_t[width_ * height_ * 3];

    if (ptr == nullptr) {
      return false;
    }
    data_ptr_ = ptr;

    row_stride = width_ * cinfo.output_components;
    jbuffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, jbuffer, 1);
      for (int x = 0; x < width_; ++x) {
        r = jbuffer[0][cinfo.output_components * x];
        if (cinfo.output_components > 2) {
          g = jbuffer[0][cinfo.output_components * x + 1];
          b = jbuffer[0][cinfo.output_components * x + 2];
        } else {
          g = r;
          b = r;
        }
        *(ptr++) = r;
        *(ptr++) = g;
        *(ptr++) = b;
      }
    }
    fclose(infile);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return true;
  }

  bool create(int width, int height) {
    if (data_ptr_ != nullptr) {
      delete [] data_ptr_;
    }

    width_ = width;
    height_ = height;

    data_ptr_ = new uint8_t[width_ * height_ * 3];

    if (data_ptr_ != nullptr) {
      std::fill(data_ptr_, data_ptr_ + width_ * height_ * 3, 0);
    }
    return data_ptr_ != nullptr;
  }

  bool save(const char* path) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    FILE *outfile = fopen(path, "wb");

    if (outfile == nullptr) {
      return false;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = width_;
    cinfo.image_height = height_;
    cinfo.input_components = 3;  // 3 bytes per pixel.
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_start_compress(&cinfo, 1);

    while(cinfo.next_scanline < cinfo.image_height) {
      row_pointer[0] = (unsigned char*) &data_ptr_[cinfo.next_scanline * cinfo.image_width * cinfo.input_components];
      jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);

    return true;
  }

  // Use square distance as metric.
  static int64_t distance(const JPEG& lhs, const JPEG& rhs) {
    int w = lhs.width(), h = rhs.height();
    const uint8_t* ptr1 = lhs.ptr(), *ptr2 = rhs.ptr();
    int64_t ret = 0;

    assert(w == rhs.width() && h == rhs.height());

    for (int i = 0, c = w * h * 3; i < c; ++i, ++ptr1, ++ptr2) {
      int tmp = *ptr1 - *ptr2;
      ret += tmp * tmp;
    }
    return ret;
  }

 private:
  int width_, height_;
  uint8_t* data_ptr_;
};

struct Circle {
  int x, y, radius;
  float a;
  uint8_t r, g, b;
};
struct Square : public Circle { };

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
    new_shape.a = float(rand() % 100 + 20) / 255.0;

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
    float r_blend = it->a * it->r;
    float g_blend = it->a * it->g;
    float b_blend = it->a * it->b;

    for (int y = from; y <= to; ++y) {
      int stride;

      if (std::is_same<T, Circle>::value) {
        stride = int(sqrt(float(radius2 - (y - it->y) * (y - it->y))));
      } else if (std::is_same<T, Square>::value) {
        stride = it->radius;
      }

      for (int x = std::max(0, it->x - stride), to_x = std::min(w - 1, it->x + stride); x <= to_x; ++x) {
        uint8_t* ptr = img->at(x, y);
        *ptr = (1 - it->a) * (*ptr) + r_blend;
        ptr += 1;
        *ptr = (1 - it->a) * (*ptr) + g_blend;
        ptr += 1;
        *ptr = (1 - it->a) * (*ptr) + b_blend;
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
    list<T> old_best = shapes[0];

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
      for (int i = 0; i < shapes.size(); ++i) {
        if (i != best_index) {
          shapes[i] = shapes[best_index];
        }
      }
    } else {
      for (int i = 0; i < shapes.size(); ++i) {
        shapes[i] = old_best;
      }
    }
    if (it % 10 == 0) {
      unique_ptr<JPEG> tmp = draw(shapes[0], w, h);
      tmp->save("/tmp/lisa-output.jpg");
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
