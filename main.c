#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

//TODO: add error checks

#define FILTER_SMOOTH 0
#define FILTER_SHARP 1
#define FILTER_EDGE 2
#define FILTER_EMBOSS 3
#define FILTER_OUTLINE 4

#pragma pack(push, 1)
typedef struct {
    uint16_t signature;
    uint32_t fileSize;
    uint32_t reserved;
    uint32_t dataOffset;

    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t number_of_planes;
    uint16_t bit_depth;
    uint32_t compression_type;
    uint32_t image_size;
    int32_t horizontal_resolution;
    int32_t vertical_resolution;
    uint32_t colors_used_count;
    uint32_t important_colors_count;
} PackedFileHeader;
#pragma pack(pop)

typedef struct {
    int kernel[9];
    int divisor;
} Filter;

typedef struct {
    int pixel_bytes_per_row;
    int total_bytes_per_row;
    int padding_size;
} Props;

Filter extractFilter(char **argv) {
    const int filter_num = atoi(argv[2]);
    Filter f;
    switch (filter_num) {
        case FILTER_SMOOTH:
            f = (Filter){{ 1, 1, 1,
                                    1, 1, 1,
                                    1, 1, 1},
                9};
            break;
        case FILTER_SHARP:
            f = (Filter){{  0,-1, 0,
                                    -1, 5,-1,
                                     0,-1, 0},
                1};
            break;
        case FILTER_EDGE:
            f = (Filter){{  0, 1, 0,
                                     1,-4, 1,
                                     0, 1, 0},
                1};
            break;
        case FILTER_EMBOSS:
            f = (Filter){{  2, 1, 0,
                                     1, 1,-1,
                                     0,-1,-2},
                1};
            break;
        case FILTER_OUTLINE:
            f = (Filter){{  -1, -1, -1, -1, 8, -1, -1, -1, -1}, 1};
            break;
        default:
            printf("Error: Invalid filter\n");
            exit(EXIT_FAILURE);
    }
    return f;
}

int openFile(const char *filename) {
    const int fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error: Failed to open file\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

PackedFileHeader readBitmapHeader(const int fd) {
    PackedFileHeader bitmap_header;
    read(fd, &bitmap_header, sizeof(PackedFileHeader));
    return bitmap_header;
}

unsigned char *readPixelDataFromHeader(const int fd, const PackedFileHeader header) {
    lseek(fd, header.dataOffset, SEEK_SET);
    // Allocate memory for the pixel data
    unsigned char *pixel_data = (unsigned char *) malloc(header.image_size);
    // If the memory allocation fails, exit the program
    if (pixel_data == NULL) {
        printf("Error: Failed to allocate memory for pixel data\n");
        exit(EXIT_FAILURE);
    }

    // Read the pixel data
    const ssize_t bytes_read = read(fd, pixel_data, header.image_size);
    if (bytes_read != header.image_size) {
        printf("Error: Failed to read pixel data\n");
        exit(EXIT_FAILURE);
    }
    return pixel_data;
}

Props setProps(const PackedFileHeader header, const int bytes_per_pixel) {
    Props props;
    props.pixel_bytes_per_row = header.width * bytes_per_pixel;
    props.total_bytes_per_row = (props.pixel_bytes_per_row + 3) & ~3;
    props.padding_size = props.total_bytes_per_row - props.pixel_bytes_per_row;
    return props;
}

unsigned char limitNumber(const int number) {
    if (number > 255) {
        return 255;
    }
    if (number < 0) {
        return 0;
    }
    return number;
}

unsigned char *applyFilter(const Filter filter, const PackedFileHeader new_header, const unsigned char *pixel_data,
                           const Props original_props,
                           const Props new_props, const int bytes_per_pixel) {
    unsigned char *new_pixel_data = (unsigned char *) malloc(new_header.image_size);

    for (int i = 0; i < new_header.height; i++) {
        int new_offset = i * (new_props.pixel_bytes_per_row + new_props.padding_size);
        int old_offset = (i + 1) * (original_props.pixel_bytes_per_row + original_props.padding_size);

        for (int j = 0; j < new_header.width; j++) {
            int k = new_offset + bytes_per_pixel * j;
            int old_k = old_offset + bytes_per_pixel * (j + 1);

            int sum_b = 0, sum_g = 0, sum_r = 0;

            for (int offset_line = -1, filter_pos = 0; offset_line <= 1; offset_line++) {
                for (int offset_row = -1; offset_row <= 1; offset_row++, filter_pos++) {
                    int old_idx = old_k + (offset_row * original_props.total_bytes_per_row) + (
                                      offset_line * bytes_per_pixel);
                    //adjust the weighted sum with the correct filter value

                    //printf("%d\n", filter.kernel[filter_pos]);
                    sum_b += (pixel_data[old_idx] * filter.kernel[filter_pos]);
                    sum_g += (pixel_data[old_idx + 1] * filter.kernel[filter_pos]);
                    sum_r += (pixel_data[old_idx + 2] * filter.kernel[filter_pos]);
                }
            }
            sum_b /= filter.divisor;
            sum_g /= filter.divisor;
            sum_r /= filter.divisor;

            new_pixel_data[k] = limitNumber(sum_b);
            new_pixel_data[k + 1] = limitNumber(sum_g);
            new_pixel_data[k + 2] = limitNumber(sum_r);
        }
    }
    return new_pixel_data;
}

int main(const int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s [filter] [filename]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const Filter filter = extractFilter(argv);

    const int fd_old = openFile(argv[1]);

    PackedFileHeader header = readBitmapHeader(fd_old);
    PackedFileHeader new_header;
    memcpy(&new_header, &header, sizeof(PackedFileHeader));

    //new image loses dimension due to the border not being calculated
    new_header.width = header.width - 2;
    new_header.height = header.height - 2;

    //Calculate all the needed sizes for the original and the new file
    const int bytes_per_pixel = header.bit_depth / 8;
    const Props original_props = setProps(header, bytes_per_pixel);
    const Props new_props = setProps(new_header, bytes_per_pixel);

    new_header.image_size = new_header.height * new_props.total_bytes_per_row;
    new_header.fileSize = new_header.image_size + new_header.dataOffset;

    // Print the image size, width, height and bit depth for debugging purposes
    printf("width: %i, height: %i, size: %i\n", header.width, header.height, header.image_size);
    printf("width: %i, height: %i, size: %i\n", new_header.width, new_header.height, new_header.image_size);

    unsigned char *old_pixel_data = readPixelDataFromHeader(fd_old, header);

    unsigned char *new_pixel_data = applyFilter(filter, new_header, old_pixel_data, original_props, new_props,
                                                bytes_per_pixel);

    int fd_new = open("output.bmp", O_CREAT | O_RDWR, 0644);
    write(fd_new, &new_header, new_header.dataOffset);
    write(fd_new, new_pixel_data, new_header.image_size);

    // Close the bitmap file
    close(fd_new);
    close(fd_old);

    // Free memory
    free(old_pixel_data);
    free(new_pixel_data);

    return EXIT_SUCCESS;
}
