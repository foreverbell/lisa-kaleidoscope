#include <cstdint>
#include <jpeglib.h>
#include <memory>

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
    jpeg_set_quality(&cinfo, 100, 1);
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

