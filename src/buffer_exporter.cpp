#include <memory>
#include <QPixmap>

#include "buffer_exporter.hpp"

template<typename T> float get_multiplier() {
    return 255.f/static_cast<float>(std::numeric_limits<T>::max());
}

template<>
float get_multiplier<float>() {
    return 255.f;
}

template<typename T> T get_max_intensity() {
    return std::numeric_limits<T>::max();
}

template<>
float get_max_intensity<float>() {
    return 1.f;
}

template<typename T>
void export_bitmap(const char *fname, const Buffer *buffer)
{
    int width_i = static_cast<int>(buffer->buffer_width_f);
    int height_i = static_cast<int>(buffer->buffer_height_f);

    vector<uint8_t> processed_buffer(4 * width_i * height_i);
    uint8_t default_channel_vals[] = {0, 0, 0, 255};

    uint8_t* out_ptr = processed_buffer.data();

    const float *bc_comp = buffer->auto_buffer_contrast_brightness();
    float color_scale = get_multiplier<T>();

    const float maxIntensity = get_max_intensity<T>();

    uint8_t pixel_layout[4];
    for(int c = 0; c < 4; ++c) {
        switch(buffer->get_pixel_layout()[c]) {
        case 'r':
            pixel_layout[c] = 0;
            break;
        case 'g':
            pixel_layout[c] = 1;
            break;
        case 'b':
            pixel_layout[c] = 2;
            break;
        case 'a':
            pixel_layout[c] = 3;
            break;
        }
    }

    const T* in_ptr = reinterpret_cast<T*>(buffer->buffer);
    int input_stride = buffer->channels * buffer->step;
    uint8_t unformatted_pixel[4];

    for(int y = 0; y < height_i; ++y) {
        for(int x = 0; x < width_i; ++x) {
            int col_offset = x * buffer->channels;
            int c;

            // Perform contrast normalization
            for(c = 0; c < buffer->channels; ++c)  {
                float in_val = static_cast<float>(in_ptr[col_offset + c]);

                unformatted_pixel[c] = static_cast<uint8_t>(
                            clamp((in_val * bc_comp[c] +
                                   bc_comp[4 + c] * maxIntensity) *
                                           color_scale, 0.f, 255.f));
            }

            // Grayscale: Repeat first channel into G and B
            if(buffer->channels == 1) {
                for(; c < 3; ++c) {
                  unformatted_pixel[c] = unformatted_pixel[0];
                }
            }

            // The remaining, non-filled channels will be set to a default value
            for(; c < 4; ++c) {
              unformatted_pixel[c] = default_channel_vals[c];
            }

            // Reorganize pixel layout according to user provided format
            for(int c = 0; c < 4; ++c) {
                out_ptr[pixel_layout[c]] = unformatted_pixel[c];
            }
            out_ptr += 4;
        }

        in_ptr += input_stride;
    }

    const int bytes_per_line = width_i * 4;
    QImage output_image(processed_buffer.data(), width_i, height_i, bytes_per_line, QImage::Format_RGBA8888);
    output_image.save(fname, "png");
}

template<typename T>
const char* get_type_descriptor();

template<>
const char* get_type_descriptor<uint8_t>() {
    return "uint8";
}

template<>
const char* get_type_descriptor<uint16_t>() {
    return "uint16";
}

template<>
const char* get_type_descriptor<int16_t>() {
    return "int16";
}

template<>
const char* get_type_descriptor<int32_t>() {
    return "int32";
}

template<>
const char* get_type_descriptor<float>() {
    return "float";
}

template<typename T>
void export_binary(const char *fname,
                   const Buffer *buffer)
{
    int width_i = static_cast<int>(buffer->buffer_width_f);
    int height_i = static_cast<int>(buffer->buffer_height_f);

    const T* in_ptr = reinterpret_cast<T*>(buffer->buffer);

    FILE* fhandle = fopen(fname, "wb");

    if(fhandle != NULL) {
      fprintf(fhandle, "%s\n", get_type_descriptor<T>());
      fwrite(&height_i, sizeof(int), 1, fhandle);
      fwrite(&width_i, sizeof(int), 1, fhandle);
      fwrite(&buffer->channels, sizeof(int), 1, fhandle);
      for(int y = 0; y < height_i; ++y) {
          fwrite(in_ptr + y * buffer->step * buffer->channels,
                 sizeof(T),
                 width_i * buffer->channels, fhandle);
      }
      fclose(fhandle);
    }
}

void BufferExporter::export_buffer(const Buffer *buffer,
                                   const std::string &path,
                                   BufferExporter::OutputType type)
{
    if(type == OutputType::Bitmap) {
        switch(buffer->type) {
        case Buffer::BufferType::UnsignedByte:
          export_bitmap<uint8_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::UnsignedShort:
          export_bitmap<uint16_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Short:
          export_bitmap<int16_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Int32:
          export_bitmap<int32_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Float32:
        case Buffer::BufferType::Float64:
          export_bitmap<float>(path.c_str(), buffer);
            break;
        }
    } else {
        // Matlab/Octave matrix (load with the giw_load.m function)
        switch(buffer->type) {
        case Buffer::BufferType::UnsignedByte:
          export_binary<uint8_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::UnsignedShort:
          export_binary<uint16_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Short:
          export_binary<int16_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Int32:
          export_binary<int32_t>(path.c_str(), buffer);
            break;
        case Buffer::BufferType::Float32:
        case Buffer::BufferType::Float64:
          export_binary<float>(path.c_str(), buffer);
            break;
        }
    }
}
