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
#define FILTER_OUTLINE 4

#define BMP_HEADER_SIZE 54

#define FILE_NAME_OUTPUT "filtered_image.bmp"

//struct for the file/bitmap header, packed so it can be written to a file without alignment
#pragma pack(push, 1)
typedef struct {
    //file header
    uint16_t signature;
    uint32_t file_size;
    uint32_t reserved;
    uint32_t data_offset;
    //bitmap header
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

//struct for a generic filter
typedef struct {
    int kernel[9];
    int divisor;
} Filter;

//struct for the byte level properties of the image rows
typedef struct {
    int pixel_bytes_per_row;
    int total_bytes_per_row;
    int padding_size;
} Props;

//determine a Filter by the argument given
Filter extractFilter(char **argv) {
    const int filter_num = atoi(argv[2]);
    Filter f;
    switch (filter_num) {
        case FILTER_SMOOTH:
            f = (Filter){
                {1, 1, 1, 1, 1, 1, 1, 1, 1}, 9
            };
            break;
        case FILTER_SHARP:
            f = (Filter){
                {0, -1, 0, -1, 5, -1, 0, -1, 0}, 1
            };
            break;
        case FILTER_EDGE:
            f = (Filter){
                {0, 1, 0, 1, -4, 1, 0, 1, 0}, 1
            };
            break;
        case FILTER_EMBOSS:
            f = (Filter){
                {2, 1, 0, 1, 1, -1, 0, -1, -2}, 1
            };
            break;
        case FILTER_OUTLINE:
            f = (Filter){{-1, -1, -1, -1, 8, -1, -1, -1, -1}, 1};
            break;
        default:
            printf("Error: Invalid filter\n");
            exit(EXIT_FAILURE);
    }
    return f;
}

unsigned char *readPixelDataFromHeader(const int fd, const PackedFileHeader header) {
    //Place the data offset
    lseek(fd, header.data_offset, SEEK_SET);

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

//Calculate the byte level properties for the rows of the image
Props calculateProps(const PackedFileHeader header, const int bytes_per_pixel) {
    Props p;
    p.pixel_bytes_per_row = header.width * bytes_per_pixel;
    p.total_bytes_per_row = (p.pixel_bytes_per_row + 3) & ~3;
    p.padding_size = p.total_bytes_per_row - p.pixel_bytes_per_row;
    return p;
}

//Limits a number from 0 to 255 preventing over-/underflow
unsigned char limitNumber(const int number) {
    if (number > 255) return 255;
    if (number < 0) return 0;
    return number;
}


unsigned char *applyFilter(const Filter filter, const PackedFileHeader new_header, const unsigned char *pixel_data,
                           const Props original_props,
                           const Props new_props, const int bytes_per_pixel) {
    unsigned char *new_pixel_data = malloc(new_header.image_size);

    for (int i = 0; i < new_header.height; i++) {
        //Calculate the row offset for the old and new image
        int new_offset = i * (new_props.pixel_bytes_per_row + new_props.padding_size);
        int old_offset = (i + 1) * (original_props.pixel_bytes_per_row + original_props.padding_size);

        for (int j = 0; j < new_header.width; j++) {
            //Calculate the current pixel position in the line for the old and the new (smaller) image
            int k = new_offset + bytes_per_pixel * j;
            int old_k = old_offset + bytes_per_pixel * (j + 1);

            //Reset the sum for each new pixel
            int sum_b = 0, sum_g = 0, sum_r = 0;

            //Loop through the nine surrounding pixels and calculate the weighted sum
            for (int offset_line = -1, filter_pos = 0; offset_line <= 1; offset_line++) {
                for (int offset_row = -1; offset_row <= 1; offset_row++, filter_pos++) {
                    int old_idx = old_k + (offset_row * original_props.total_bytes_per_row) + (
                                      offset_line * bytes_per_pixel);

                    //Adjust the sum with the weighted filter value
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
    //Check if argc is right
    if (argc != 3) {
        printf("Usage: %s [filename] [filter]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //Set the filter chosen by the user
    const Filter filter = extractFilter(argv);

    //Open the original file
    const int fd_old = open(argv[1], O_RDWR);
    if (fd_old < 0) {
        printf("Error: Failed to open file\n");
        exit(EXIT_FAILURE);
    }

    //Read the original header and check if it is a BM file
    PackedFileHeader header;
    read(fd_old, &header, sizeof(PackedFileHeader));
    if (header.signature != 0x4D42) {
        printf("Error: not a bitmap file\n");
        exit(EXIT_FAILURE);
    }

    //Make a copy of the header for the new file
    PackedFileHeader new_header;
    memcpy(&new_header, &header, sizeof(PackedFileHeader));

    //Reduce the image dimensions of the new image, the borders are not calculated
    new_header.width = header.width - 2;
    new_header.height = header.height - 2;

    //Calculate the byte level properties of the rows
    const int bytes_per_pixel = header.bit_depth / 8;
    const Props original_props = calculateProps(header, bytes_per_pixel);
    const Props new_props = calculateProps(new_header, bytes_per_pixel);

    //Update the new header
    new_header.image_size = new_header.height * new_props.total_bytes_per_row;
    new_header.file_size = new_header.image_size + new_header.data_offset;

    // Print the image size, width, height and bit depth for debugging purposes
    printf("OLD: width: %i, height: %i, size: %i\n", header.width, header.height, header.image_size);
    printf("NEW: width: %i, height: %i, size: %i\n", new_header.width, new_header.height, new_header.image_size);

    //Read the original pixel data
    unsigned char *old_pixel_data = readPixelDataFromHeader(fd_old, header);

    //Calculate the new pixel data
    unsigned char *new_pixel_data = applyFilter(filter, new_header, old_pixel_data, original_props, new_props,
                                                bytes_per_pixel);

    //Open a new file for the program output
    int fd_new = open(FILE_NAME_OUTPUT, O_CREAT | O_RDWR, 0644);
    if (fd_new < 0) {
        printf("Error: Failed to open output file\n");
        free(old_pixel_data);
        free(new_pixel_data);
        exit(EXIT_FAILURE);
    }

    //Write the file header
    size_t bytes_written = write(fd_new, &new_header, BMP_HEADER_SIZE);
    if (bytes_written != BMP_HEADER_SIZE) {
        printf("Error: Failed to write header to output file\n");
        free(old_pixel_data);
        free(new_pixel_data);
        exit(EXIT_FAILURE);
    }


    bytes_written = write(fd_new, new_pixel_data, new_header.image_size);
    if (bytes_written != new_header.image_size) {
        printf("Error: Failed to write pixel-data to output file\n");
        free(old_pixel_data);
        free(new_pixel_data);
        exit(EXIT_FAILURE);
    }

    // Close the bitmap files
    close(fd_new);
    close(fd_old);

    // Free memory
    free(old_pixel_data);
    free(new_pixel_data);

    return EXIT_SUCCESS;
}
