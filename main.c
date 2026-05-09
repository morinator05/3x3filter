#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define FILTER_SMOOTH 0
#define FILTER_SHARP 1
#define FILTER_EDGE 2
#define FILTER_EMBOSS 3

#define BMP_HEADER_SIZE 54
#define FILE_HEADER_SIZE 14

typedef struct
{
    uint32_t image_size;
    int32_t width;
    int32_t height;
    uint16_t bit_depth;
} Header;

typedef struct
{
    int kernel[9];
    int divisor;
} Filter;

typedef struct {
    int pixel_bytes_per_row;
    int total_bytes_per_row;
    int padding_size;
} Props;

Filter extractFilter(char** argv)
{
    const int8_t filter_num = atoi(argv[2]);
    Filter f;
    switch (filter_num)
    {
    case FILTER_SMOOTH:
        f = (Filter){{1, 1, 1, 1, 1, 1, 1, 1, 1}, 9};
        break;
    case FILTER_SHARP:
        f = (Filter){{0, -1, 0, -1, 5, -1, 0, -1, 0}, 1};
        break;
    case FILTER_EDGE:
        f = (Filter){{0, 1, 0, 1, -4, 1, 0, 1, 0}, 1};
        break;
    case FILTER_EMBOSS:
        f = (Filter){{2, 1, 0, 1, 1, -1, 0, -1, -2}, 1};
        break;
    default:
        printf("Error: Invalid filter\n");
        exit(EXIT_FAILURE);
    }
    return f;
}

int openFile(const char* filename)
{
    const int fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("Error: Failed to open file\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

Header readBitmapHeader(int fd)
{
    unsigned char bmp_header[BMP_HEADER_SIZE];

    // Read the bitmap header
    ssize_t bytes_read = read(fd, bmp_header, BMP_HEADER_SIZE);
    // If there are less bytes read, then the file is not a valid bitmap file
    if (bytes_read != BMP_HEADER_SIZE)
    {
        printf("Error: Invalid bitmap header\n");
        exit(EXIT_FAILURE);
    }

    // Check the file signature to make sure it's a bitmap file
    if (bmp_header[0] != 'B' || bmp_header[1] != 'M')
    {
        printf("Error: It's not a bitmap image\n");
        exit(EXIT_FAILURE);
    }

    Header bitmap_header;

    bitmap_header.image_size = *(uint32_t*)&bmp_header[2];
    bitmap_header.width = *(int32_t*)&bmp_header[18];
    bitmap_header.height = *(int32_t*)&bmp_header[22];
    bitmap_header.bit_depth = *(uint16_t*)&bmp_header[28];

    if (bitmap_header.width < 3 || bitmap_header.height < 3)
    {
        printf("Error: Bitmap width and height must both be >= 3\n");
        exit(EXIT_FAILURE);
    }

    return bitmap_header;
}

unsigned char* readPixelDataFromHeader(int fd, Header header) {
    // Allocate memory for the pixel data
    unsigned char* pixel_data = (unsigned char*)malloc(header.image_size - BMP_HEADER_SIZE);
    // If the memory allocation fails, exit the program
    if (pixel_data == NULL)
    {
        printf("Error: Failed to allocate memory for pixel data\n");
        exit(EXIT_FAILURE);
    }

    // Read the pixel data
    ssize_t bytes_read = read(fd, pixel_data, header.image_size - BMP_HEADER_SIZE);
    if (bytes_read != header.image_size - BMP_HEADER_SIZE)
    {
        printf("Error: Failed to read pixel data\n");
        exit(EXIT_FAILURE);
    }
    return pixel_data;
}

Props setProps(Header header, int bytes_per_pixel) {
    Props props;
    props.pixel_bytes_per_row = header.width * bytes_per_pixel;
    props.total_bytes_per_row = (props.pixel_bytes_per_row + 3) & ~3;
    props.padding_size = props.total_bytes_per_row - props.pixel_bytes_per_row;
    return props;
}

unsigned char limitNumber(int number) {
    if (number > 255) {
        return 255;
    }
    if (number <= 0) {
        return 0;
    }
    return number;
}

unsigned char* applyFilter(Filter filter, Header new_header, unsigned char* pixel_data, Props original_props, Props new_props, int bytes_per_pixel)
{
    unsigned char* new_pixel_data = (unsigned char*)malloc(new_header.image_size);

    for (int i = 0; i < new_header.height; i++)
    {
        int new_offset = i * ( new_props.pixel_bytes_per_row + new_props.padding_size);
        int old_offset = (i + 1) * (original_props.pixel_bytes_per_row + original_props.padding_size);
        for (int j = 0; j < new_header.width; j++)
        {
            int k = new_offset + bytes_per_pixel * j;
            int old_k = old_offset + bytes_per_pixel * (j + 1);

            int sum_b = 0, sum_g = 0, sum_r = 0;

            for (int offset_line = -1, filter_pos = 0; offset_line <= 1; offset_line++)
            {
                for (int offset_row = -1; offset_row <= 1; offset_row++, filter_pos++)
                {
                    int old_idx = old_k + (offset_row * original_props.total_bytes_per_row) + (offset_line * bytes_per_pixel);

                    //adjust the weighted sum with the correct filter value
                    //printf("%d", filter.kernel[filter_pos]);
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



int main(const int argc, char** argv)
{
    if (argc != 3)
    {
        printf("Usage: %s [filter] [filename]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const Filter filter = extractFilter(argv);

    const int fd = openFile(argv[1]);

    const Header header = readBitmapHeader(fd);
    Header new_header;
    memcpy(&new_header, &header, sizeof(Header));

    //new image loses dimension due to the border not being calculated
    new_header.width = header.width - 2;
    new_header.height = header.height - 2;

    // Print the image size, width, height and bit depth for debugging purposes
    printf("width: %i, height: %i, size: %i\n", header.width, header.height, header.image_size);

    unsigned char* pixel_data = readPixelDataFromHeader(fd, header);

    //Calculate all the needed sizes for the original and the new file
    const int bytes_per_pixel = header.bit_depth / 8;
    const Props original_props = setProps(header, bytes_per_pixel);

    const Props new_props = setProps(new_header, bytes_per_pixel);

    new_header.image_size = new_header.height * new_props.total_bytes_per_row;

    unsigned char* new_pixel_data = applyFilter(filter, new_header, pixel_data, original_props, new_props, bytes_per_pixel);

    //TODO: make this more elegant
    //Write new files size
    int new_file_size = new_header.image_size + BMP_HEADER_SIZE;
    lseek(fd, 2, SEEK_SET);
    write(fd, &new_file_size, 4);

    //Write new width and height
    lseek(fd, FILE_HEADER_SIZE + 4, SEEK_SET);
    write(fd, &new_header.width, 4);
    write(fd, &new_header.height, 4);

    lseek(fd, FILE_HEADER_SIZE + 20, SEEK_SET);
    write(fd, &new_header.image_size, 4);

    // Reset the file pointer to the start of the pixel data
    lseek(fd, BMP_HEADER_SIZE, SEEK_SET);

    // Overwrite the pixel data of the opened file
    ssize_t bytes_written = write(fd, new_pixel_data, new_header.image_size);
    if (bytes_written != new_header.image_size)
    {
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
