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
#define BIT_DEPTH_DEVIDER 8
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
    int bytes_per_pixel;
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

void readPixelDataFromHeader(unsigned char *pixel_data_ptr, const int fd, const PackedFileHeader header) {
    //Place the data offset
    lseek(fd, header.data_offset, SEEK_SET);

    // Read the pixel data
    const ssize_t bytes_read = read(fd, pixel_data_ptr, header.image_size);
    if (bytes_read != header.image_size) {
        printf("Error: Failed to read pixel data\n");
        exit(EXIT_FAILURE);
    }
}

//Calculate the byte level properties for the rows of the image
Props calculateProps(const PackedFileHeader header) {
    Props p;
    p.bytes_per_pixel = header.bit_depth / BIT_DEPTH_DEVIDER;
    p.pixel_bytes_per_row = header.width * p.bytes_per_pixel;
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

//Allocates a ptr for the pixeldata for a given bm header
unsigned char *mallocPixelData(const PackedFileHeader h) {
    unsigned char *ptr = malloc(h.image_size);
    if (ptr == NULL) {
        printf("Error: Failed to allocate memory for pixel data\n");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void applyFilter(unsigned char *pixel_data_new, const unsigned char *pixel_data_original, const Filter filter,
                 const PackedFileHeader header_new,
                 const Props props_original,
                 const Props props_new) {
    for (int i = 0; i < header_new.height; i++) {
        //Calculate the row offset for the old and new image
        int new_offset = i * (props_new.pixel_bytes_per_row + props_new.padding_size);
        int old_offset = (i + 1) * (props_original.pixel_bytes_per_row + props_original.padding_size);

        for (int j = 0; j < header_new.width; j++) {
            //Calculate the current pixel position in the line for the old and the new (smaller) image
            int k = new_offset + props_new.bytes_per_pixel * j;
            int old_k = old_offset + props_original.bytes_per_pixel * (j + 1);

            //Reset the sum for each new pixel
            int sum_b = 0, sum_g = 0, sum_r = 0;

            //Loop through the nine surrounding pixels and calculate the weighted sum
            for (int offset_y = -1, filter_pos = 0; offset_y <= 1; offset_y++) {
                for (int offset_x = -1; offset_x <= 1; offset_x++, filter_pos++) {
                    int old_idx = old_k + (offset_y * props_original.total_bytes_per_row) + (
                                      offset_x * props_original.bytes_per_pixel);

                    //Adjust the sum with the weighted filter value
                    sum_b += (pixel_data_original[old_idx] * filter.kernel[filter_pos]);
                    sum_g += (pixel_data_original[old_idx + 1] * filter.kernel[filter_pos]);
                    sum_r += (pixel_data_original[old_idx + 2] * filter.kernel[filter_pos]);
                }
            }
            sum_b /= filter.divisor;
            sum_g /= filter.divisor;
            sum_r /= filter.divisor;

            pixel_data_new[k] = limitNumber(sum_b);
            pixel_data_new[k + 1] = limitNumber(sum_g);
            pixel_data_new[k + 2] = limitNumber(sum_r);
        }
    }
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
    const int fd_original = open(argv[1], O_RDWR);
    if (fd_original < 0) {
        printf("Error: Failed to open file\n");
        exit(EXIT_FAILURE);
    }

    //Read the original header and check if it is a BM file, check if image is large enough
    PackedFileHeader header_original;
    read(fd_original, &header_original, sizeof(PackedFileHeader));
    if (header_original.signature != 0x4D42) {
        printf("Error: not a bitmap file\n");
        exit(EXIT_FAILURE);
    }
    if (header_original.width < 3 || header_original.height < 3) {
        printf("Error: image width and height must be at least 3px");
        exit(EXIT_FAILURE);
    }

    //Make a copy of the header for the new file
    PackedFileHeader header_new;
    memcpy(&header_new, &header_original, sizeof(PackedFileHeader));

    //Reduce the image dimensions of the new image, the borders are not calculated
    header_new.width = header_original.width - 2;
    header_new.height = header_original.height - 2;

    //Calculate the byte level properties of the rows
    const Props props_original = calculateProps(header_original);
    const Props props_new = calculateProps(header_new);

    //Update the new header
    header_new.image_size = header_new.height * props_new.total_bytes_per_row;
    header_new.file_size = header_new.image_size + header_new.data_offset;

    // Print the image size, width, height and bit depth for debugging purposes
    printf("OLD: width: %i, height: %i, size: %i\n", header_original.width, header_original.height, header_original.image_size);
    printf("NEW: width: %i, height: %i, size: %i\n", header_new.width, header_new.height, header_new.image_size);

    // Allocate memory for the pixel data and read it
    unsigned char *pixel_data_original = mallocPixelData(header_original);
    unsigned char *pixel_data_new = mallocPixelData(header_new);
    readPixelDataFromHeader(pixel_data_original, fd_original, header_original);

    //apply the filter by calculating the new pixel data
    applyFilter(pixel_data_new, pixel_data_original, filter, header_new, props_original, props_new);

    //Open a new file for the program output
    int fd_new = open(FILE_NAME_OUTPUT, O_CREAT | O_RDWR, 0644);
    if (fd_new < 0) {
        printf("Error: Failed to open output file\n");
        free(pixel_data_original);
        free(pixel_data_new);
        exit(EXIT_FAILURE);
    }

    //Write the file header
    size_t bytes_written = write(fd_new, &header_new, BMP_HEADER_SIZE);
    if (bytes_written != BMP_HEADER_SIZE) {
        printf("Error: Failed to write header to output file\n");
        free(pixel_data_original);
        free(pixel_data_new);
        exit(EXIT_FAILURE);
    }

    bytes_written = write(fd_new, pixel_data_new, header_new.image_size);
    if (bytes_written != header_new.image_size) {
        printf("Error: Failed to write pixel-data to output file\n");
        free(pixel_data_original);
        free(pixel_data_new);
        exit(EXIT_FAILURE);
    }

    // Close the bitmap files
    close(fd_new);
    close(fd_original);

    // Free memory
    free(pixel_data_original);
    free(pixel_data_new);

    return EXIT_SUCCESS;
}
