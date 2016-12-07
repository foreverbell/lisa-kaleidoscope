// Requires libjpeg.
// sudo apt-get install libjpeg-dev

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <jpeglib.h>
#include <list>
#include <memory>
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

 private:
  int width_, height_;
  uint8_t* data_ptr_;
};

struct Circle {
  int x, y, radius;
  float a;
  uint8_t r, g, b;
};
using Circles = list<Circle>;

void mutate(Circles* circles, int w, int h) {
  if (rand() & 1) {
    // Add a new circle to the list tail.
    Circle new_circle;

    new_circle.x = rand() % w;
    new_circle.y = rand() % h;
    new_circle.radius = rand() % 50 + 1;
    new_circle.r = rand() & 0xff;
    new_circle.g = rand() & 0xff;
    new_circle.b = rand() & 0xff;
    new_circle.a = float(rand() % 100 + 20) / 255.0;

    circles->push_back(new_circle);
  } else {
    // Randomly delete a circle at any position.
    if (!circles->empty()) {
      int index = rand() % circles->size();
      Circles::iterator it = circles->begin();
      advance(it, index);
      circles->erase(it);
    }
  }

  // Continue mutating with 50% possibility.
  if (rand() & 1) {
    mutate(circles, w, h);
  }
}

// Use square distance as metric.
int64_t distance(const JPEG& lhs, const JPEG& rhs) {
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

unique_ptr<JPEG> draw(const Circles& circles, int w, int h) {
  unique_ptr<JPEG> img = std::make_unique<JPEG>();

  img->create(w, h);
  // The earlier circle is at bottom.
  for (Circles::const_iterator it = circles.begin(); it != circles.end(); ++it) {
    int from = std::max(0, it->y - it->radius), to = std::min(h - 1, it->y + it->radius);
    for (int y = from; y <= to; ++y) {
      int stride = int(sqrt(float(it->radius * it->radius - (y - it->y) * (y - it->y))));
      for (int x = std::max(0, it->x - stride), to_x = std::min(w - 1, it->x + stride); x <= to_x; ++x) {
        uint8_t* ptr = img->at(x, y);
        uint8_t r = *ptr, g = *(ptr + 1), b = *(ptr + 2);
        r = (1 - it->a) * r + it->a * it->r;
        g = (1 - it->a) * g + it->a * it->g;
        b = (1 - it->a) * b + it->a * it->b;
        *ptr = r;
        *(ptr + 1) = g;
        *(ptr + 2) = b;
      }
    }
  }
  return img;
}

int main() {
  const int population = 100;
  JPEG lisa;
  vector<Circles> circles;

  lisa.load("../lisa.jpg");
  circles.resize(population);

  int w = lisa.width(), h = lisa.height();
  int64_t old_best_score = ~0ull >> 1;

  for (int it = 0; ; ++it) {
    int64_t best_score = 0;
    int best_index = -1;
    Circles old_best = circles[0];

    for (int i = 0; i < circles.size(); ++i) {
      mutate(&circles[i], w, h);

      unique_ptr<JPEG> tmp = draw(circles[i], w, h);
      int64_t score = distance(*tmp, lisa);
      if (best_index == -1 || score < best_score) {
        best_score = score;
        best_index = i;
      }
    }
    if (best_score < old_best_score) {
      old_best_score = best_score;
      for (int i = 0; i < circles.size(); ++i) {
        if (i != best_index) {
          circles[i] = circles[best_index];
        }
      }
    } else {
      for (int i = 0; i < circles.size(); ++i) {
        circles[i] = old_best;
      }
    }
    if (it % 10 == 0) {
      unique_ptr<JPEG> tmp = draw(circles[0], w, h);    
      tmp->save("/tmp/lisa-output.jpg");
      std::cout << it << " " << circles[0].size() << " " << old_best_score << std::endl;
    }
  }

  return 0;
}
