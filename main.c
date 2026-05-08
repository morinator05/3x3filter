#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define FILTER_SMOOTH 0
#define FILTER_SHARP 1
#define FILTER_EDGE 2
#define FILTER_EMBOSS 3

#define BMP_HEADER_SIZE 54
#define FILE_HEADER_SIZE 14

typedef struct {
    uint32_t image_size;
    int32_t width;
    int32_t height;
    uint16_t bit_depth;
}Header;

typedef struct  {
    int kernel[9];
    float multiplier;
}Filter;

Filter extractFilter (char** argv) {
    const int8_t filter_num = atoi(argv[2]);
    Filter f;
    switch (filter_num) {
        case FILTER_SMOOTH:
            f = (Filter) {{1, 1, 1, 1, 1, 1, 1, 1, 1}, 1/9};
            break;
        case FILTER_SHARP:
            f = (Filter) {};
            break;
        default:
            printf("Error: Invalid filter\n");
            exit(EXIT_FAILURE);
    }
    return f;
}

int openFile(const char* filename) {
    const int fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error: Failed to open file\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

Header readBitmapHeader(int fd) {
    unsigned char bmp_header[BMP_HEADER_SIZE];

    // Read the bitmap header
    ssize_t bytes_read = read(fd, bmp_header, BMP_HEADER_SIZE);
    // If there are less bytes read, then the file is not a valid bitmap file
    if (bytes_read != BMP_HEADER_SIZE) {
        printf("Error: Invalid bitmap header\n");
        exit(EXIT_FAILURE);
    }

    // Check the file signature to make sure it's a bitmap file
    if (bmp_header[0] != 'B' || bmp_header[1] != 'M') {
        printf("Error: It's not a bitmap image\n");
        exit(EXIT_FAILURE);
    }

    Header bitmap_header;

    bitmap_header.image_size = *(uint32_t*) &bmp_header[2];
    bitmap_header.width = *(int32_t*) &bmp_header[18];
    bitmap_header.height = *(int32_t*) &bmp_header[22];
    bitmap_header.bit_depth =  *(uint16_t*) &bmp_header[28];

    if (bitmap_header.width < 3 || bitmap_header.height < 3) {
        printf("Error: Bitmap width and height must both be >= 3\n");
        exit(EXIT_FAILURE);
    }

    return bitmap_header;
}

void applyFilter() {

}

int main(int argc, char** argv) {

    if (argc != 3) {
        printf("Usage: %s [filter] [filename]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Filter filter = extractFilter(argv);

    const int fd = openFile(argv[1]);

    Header old_header = readBitmapHeader(fd);

    // Print the image size, width, height and bit depth for debugging purposes
    printf( "width: %i, height: %i, size: %i\n", old_header.width, old_header.height, old_header.image_size);

    // Allocate memory for the pixel data
    unsigned char* pixel_data = (unsigned char*) malloc(old_header.image_size - BMP_HEADER_SIZE);
    // If the memory allocation fails, exit the program
    if (pixel_data == NULL) {
        printf("Error: Failed to allocate memory for pixel data\n");
        exit(EXIT_FAILURE);
    }

    // Read the pixel data
    ssize_t bytes_read = read(fd, pixel_data, old_header.image_size - BMP_HEADER_SIZE);
    if (bytes_read != old_header.image_size - BMP_HEADER_SIZE) {
        printf("Error: Failed to read pixel data\n");
        exit(EXIT_FAILURE);
    }

    int bytes_per_pixel = old_header.bit_depth / 8;
    int pixel_bytes_per_row = old_header.width * bytes_per_pixel;
    int total_bytes_per_row = (pixel_bytes_per_row + 3) & ~3;
    int padding_size = total_bytes_per_row - pixel_bytes_per_row;

    int new_width = old_header.width - 2;
    int new_height = old_header.height - 2;
    int new_pixel_row = new_width * bytes_per_pixel;
    int new_total_row = (new_pixel_row + 3) & ~3;
    int new_padding_size = new_total_row - new_pixel_row;
    int new_image_size = new_height * new_total_row;

    unsigned char* new_pixel_data = (unsigned char*) malloc(new_image_size);

    //Apply filter
    for (int i = 0; i < new_height; i++) {
        int offset = i * (new_pixel_row + new_padding_size);
        int old_offset = (i + 1) * (pixel_bytes_per_row + padding_size);
        for (int j = 0; j < new_width; j++) {

            int k = offset + bytes_per_pixel * j;
            int old_k = old_offset + bytes_per_pixel * (j + 1);

            unsigned int sum_b = 0, sum_g = 0, sum_r = 0;

            for (int offset_line = -1; offset_line <= 1; offset_line++) {
                for (int offset_row = -1; offset_row <= 1; offset_row++) {
                    int old_idx = old_k + (offset_row * total_bytes_per_row) + (offset_line * bytes_per_pixel);

                    //TODO: multiply each value by its factor defined somewhere else
                    sum_b += pixel_data[old_idx];
                    sum_g += pixel_data[old_idx + 1];
                    sum_r += pixel_data[old_idx + 2];
                }
            }

            //TODO
            sum_b /= 9;
            sum_g /= 9;
            sum_r /= 9;

            new_pixel_data[k] = (unsigned char) sum_b;
            new_pixel_data[k + 1] = (unsigned char)  sum_g;
            new_pixel_data[k + 2] = (unsigned char)  sum_r;

        }
    }

    //Write new files size
    int new_file_size = new_image_size + BMP_HEADER_SIZE;
    lseek(fd, 2, SEEK_SET);
    write(fd, &new_file_size, 4);

    //Write new width and height
    lseek(fd, FILE_HEADER_SIZE + 4, SEEK_SET);
    write(fd, &new_width, 4);
    write(fd, &new_height, 4);

    lseek(fd, FILE_HEADER_SIZE + 20, SEEK_SET);
    write(fd, &new_image_size, 4);

    // Reset the file pointer to the start of the pixel data
    lseek(fd, BMP_HEADER_SIZE, SEEK_SET);

    // Overwrite the pixel data of the opened file
    ssize_t bytes_written = write(fd, new_pixel_data, new_image_size);
    if (bytes_written != new_image_size) {
        printf("Error: Failed to write pixel data\n");
        exit(EXIT_FAILURE);
    }

    // Close the bitmap file
    close(fd);

    // Free memory
    free(pixel_data);
    free(new_pixel_data);

    return EXIT_SUCCESS;
}


