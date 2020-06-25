/* 
 * vim: tabstop=8 shiftwidth=8
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <png.h>

#define SUB_PNG_VERSION "1.6.37"
#define PNG_SIG_LENGTH 8

#define sub_image_row_size(image) (sizeof(uint8_t) * 3 * (image)->width)

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

#define DEBUG_BOOL(b) printf("%s\n", (b) ? "true" : "false")

#define MAX_BOX_RADIUS 30
#define MAX_BOX_DIAM (2 * MAX_BOX_RADIUS)

#define true 1
#define false 0

enum sub_tri_state {
        SUB_NONE = 0,
        SUB_FALSE,
        SUB_TRUE
};

struct sub_png_reader {
        png_struct *png;
        png_info *info;
};

struct sub_png_writer {
        png_struct *png;
        png_info *info;
        FILE *file;
};

/*
 * 8-bit RGB image data
 */
struct sub_image {
        size_t width;
        size_t height;
        uint8_t *data;
};

struct sub_pixel {
        uint8_t r;
        uint8_t g;
        uint8_t b;
};

const struct sub_pixel WHITE = { 0xff, 0xff, 0xff };
const struct sub_pixel BLACK = { 0x00, 0x00, 0x00 };

struct sub_box {
        size_t top;
        size_t bottom;
        size_t left;
        size_t right;
};

/*
 * sub_pixel functions
 */

int sub_pixel_equal(struct sub_pixel p1, struct sub_pixel p2) {
        return p1.r == p2.r && p1.g == p2.g && p1.b == p2.b;
}

/*
 * Reads the first 8 bytes and checks them against the magic png header
 * Should probably be followed with a call to "png_set_sig_bytes"
 */
int sub_check_if_png(FILE *file) {
        uint8_t bytes[PNG_SIG_LENGTH];
        fread(bytes, sizeof(uint8_t), PNG_SIG_LENGTH, file);
        return png_sig_cmp(bytes, 0, PNG_SIG_LENGTH) == 0;
}

/*
 * struct sub_png reading functions
 */

void sub_png_reader_init(struct sub_png_reader *reader, char *file_name) {
        png_struct *png;
        png_info *info;
        FILE *file;

        png = png_create_read_struct(SUB_PNG_VERSION, NULL, NULL, NULL);
        if (png == NULL) {
                fprintf(stderr, "Unable to create read struct\n");
                exit(EXIT_FAILURE);
        }

        info = png_create_info_struct(png);
        if (info == NULL) {
                fprintf(stderr, "Unable to create info struct\n");
                exit(EXIT_FAILURE);
        }

        file = fopen(file_name, "rb");
        if (file == NULL) {
                fprintf(stderr, "Unable to open file %s for reading\n", file_name);
                exit(EXIT_FAILURE);
        } else if (!sub_check_if_png(file)) {
                fprintf(stderr, "File %s is not a png file\n", file_name);
                exit(EXIT_FAILURE);
        }

        png_init_io(png, file);
        png_set_sig_bytes(png, PNG_SIG_LENGTH);
        png_read_png(
                png, info,
                PNG_TRANSFORM_STRIP_16 |
                PNG_TRANSFORM_PACKING |
                PNG_TRANSFORM_GRAY_TO_RGB |
                PNG_TRANSFORM_STRIP_ALPHA,
                NULL);

        reader->png = png;
        reader->info = info;
        fclose(file);
}

void sub_png_reader_destroy(struct sub_png_reader *reader) {
        png_destroy_read_struct(&reader->png, &reader->info, NULL);
}

void sub_png_reader_load_image(struct sub_png_reader *reader, struct sub_image *image) {
        size_t i, row_size;
        uint8_t **image_rows;

        image->width = png_get_image_width(reader->png, reader->info);
        image->height = png_get_image_height(reader->png, reader->info);
        row_size = sub_image_row_size(image);
        image->data = malloc(row_size * image->height);

        image_rows = png_get_rows(reader->png, reader->info);
        for(i = 0; i < image->height; i++)
                memcpy(image->data + i * row_size, image_rows[i], row_size);
}

/*
 * struct sub_png writing functions
 */

void sub_png_writer_init(struct sub_png_writer *writer, char *file_name) {
        png_struct *png;
        png_info *info;
        FILE *file;

        png = png_create_write_struct(SUB_PNG_VERSION, NULL, NULL, NULL);
        if (png == NULL) {
                fprintf(stderr, "Unable to create write struct\n");
                exit(EXIT_FAILURE);
        }

        info = png_create_info_struct(png);
        if (info == NULL) {
                fprintf(stderr, "Unable to create info struct\n");
                exit(EXIT_FAILURE);
        }

        file = fopen(file_name, "wb");
        if (file == NULL) {
                fprintf(stderr, "Unable to open file %s for writing\n", file_name);
                exit(EXIT_FAILURE);
        }

        png_init_io(png, file);
        png_set_compression_level(png, 2);
        writer->png = png;
        writer->info = info;
        writer->file = file;
}

void sub_png_writer_destroy(struct sub_png_writer *writer) {
        png_destroy_write_struct(&writer->png, &writer->info);
        fclose(writer->file);
}

void sub_png_writer_save_image_cropped(struct sub_png_writer *writer,
                struct sub_image *image, struct sub_box *crop) {
        uint8_t **data;
        size_t row_size, i, width, height;

        width = crop->right - crop->left;
        height = crop->bottom - crop->top;

        row_size = sub_image_row_size(image);
        data = malloc(sizeof(uint8_t *) * height);
        for (i = 0; i < height; i++)
                data[i] = image->data + (i + crop->top) * row_size + 3 * crop->left;

        png_set_IHDR(
                writer->png,
                writer->info,
                width,
                height,
                8, /* bit depth of 8 */
                PNG_COLOR_TYPE_RGB,
                PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_DEFAULT,
                PNG_FILTER_TYPE_DEFAULT);
        png_set_rows(writer->png, writer->info, data);
        png_write_png(
                writer->png,
                writer->info,
                PNG_TRANSFORM_IDENTITY,
                NULL);
        free(data);
}

/*
 * struct sub_image functions
 */

void sub_image_destroy(struct sub_image *image) {
        free(image->data);
}

struct sub_pixel sub_image_get_pixel(struct sub_image *image, size_t x, size_t y) {
        struct sub_pixel pixel;
        size_t row_size;

        /* printf("%ld; %ld\n", x, y); */

        row_size = sub_image_row_size(image);
        pixel.r = image->data[row_size * y + 3 * x];
        pixel.g = image->data[row_size * y + 3 * x + 1];
        pixel.b = image->data[row_size * y + 3 * x + 2];

        return pixel;
}

void sub_image_set_pixel(struct sub_image *image,
                struct sub_pixel pixel,
                size_t x, size_t y) {
        size_t row_size = sub_image_row_size(image);
        image->data[row_size * y + 3 * x]     = pixel.r;
        image->data[row_size * y + 3 * x + 1] = pixel.g;
        image->data[row_size * y + 3 * x + 2] = pixel.b;
}

struct sub_point  {
        size_t x;
        size_t y;
};

void sub_stack_push(struct sub_point **stack, size_t *size, size_t x, size_t y) {
        (*stack)[*size].x = x;
        (*stack)[*size].y = y;
        *size += 1;
}

struct sub_point sub_stack_pop(struct sub_point **stack, size_t *size) {
        *size -= 1;
        *stack += 1;
        return (*stack)[-1];
}

/*
 * Box functions
 */

size_t sub_box_area(struct sub_box *box) {
        return (box->right - box->left) * (box->bottom - box->top);
}

int sub_box_contains(struct sub_box *outer, struct sub_box *inner) {
        return outer->right >= inner->right &&
                outer->left <= inner->left &&
                outer->bottom >= inner->bottom &&
                outer->top <= inner->bottom;
}

struct sub_point _s[100000];

/* this has (n+1) off bye one errors */
void sub_image_find_box(struct sub_image *image, struct sub_box *box,
                struct sub_pixel color, size_t x, size_t y) {
        size_t stack_size;
        size_t left, right, top, bottom;
        enum sub_tri_state pixel_states[MAX_BOX_DIAM][MAX_BOX_DIAM] = {SUB_NONE};
        struct sub_point *stack = _s;

        stack_size = 0;
        sub_stack_push(&stack, &stack_size, x, y);

#define get_state(p) (pixel_states[(p).x - left][(p).y - top])

        left = (x > MAX_BOX_RADIUS) ? x - MAX_BOX_RADIUS : 0;
        right = min(image->width, x + MAX_BOX_RADIUS);
        top = (y > MAX_BOX_RADIUS) ? y - MAX_BOX_RADIUS : 0;
        bottom = min(image->height, y + MAX_BOX_RADIUS);

        box->left = box->right = x;
        box->top = box->bottom = y;

        while (stack_size > 0) {
                struct sub_point point;
                struct sub_pixel c;

                point = sub_stack_pop(&stack, &stack_size);
                sub_image_get_pixel(image, 0, 0);
                c = sub_image_get_pixel(image, point.x, point.y);
                if (get_state(point) != SUB_NONE) {
                        continue;
                } else if (!sub_pixel_equal(color, c)) {
                        get_state(point) = SUB_FALSE;
                        continue;
                }

                get_state(point) = SUB_TRUE;
                box->left = min(box->left, point.x);
                box->right = max(box->right, point.x);
                box->top = min(box->top, point.y);
                box->bottom = max(box->bottom, point.y);

                if (point.x - 1 >= left)
                        sub_stack_push(&stack, &stack_size, point.x - 1, point.y);
                if (point.x + 1 < right)
                        sub_stack_push(&stack, &stack_size, point.x + 1, point.y);
                if (point.y - 1 >= top)
                        sub_stack_push(&stack, &stack_size, point.x, point.y - 1);
                if (point.y + 1 < bottom)
                        sub_stack_push(&stack, &stack_size, point.x, point.y + 1);
        }


#undef get_state
}

void sub_load_image(struct sub_image *image, char *file_name) {
        struct sub_png_reader reader;
        sub_png_reader_init(&reader, file_name);
        sub_png_reader_load_image(&reader, image);
        sub_png_reader_destroy(&reader);
}

void sub_save_image_cropped(struct sub_image *image, struct sub_box *box, char *file_name) {
        struct sub_png_writer writer;
        sub_png_writer_init(&writer, file_name);
        sub_png_writer_save_image_cropped(&writer, image, box);
        sub_png_writer_destroy(&writer);
}

int main() {
        struct sub_image im;
        struct sub_box box, crop;
        size_t i;

        sub_load_image(&im, "1.png");

        crop.right = crop.bottom = 0;
        crop.top = im.height;
        crop.left = im.width;

        for(i = 0; i < im.width; i++) {
                struct sub_box inner;

                sub_image_find_box(&im, &box, BLACK, i, 1020);
                if (sub_box_area(&box) == 0) continue;
                sub_image_find_box(&im, &inner, WHITE, i+5, 1020);
                if (sub_box_area(&box) == 0 || !sub_box_contains(&box, &inner)) {
                        continue;
                }

                i = box.right;
                crop.left = min(crop.left, box.left);
                crop.right = max(crop.right, box.right);
                crop.top = min(crop.top, box.top);
                crop.bottom = max(crop.bottom, box.bottom);
        }

        printf("left:   %ld\n", crop.left);
        printf("right:  %ld\n", crop.right);
        printf("top:    %ld\n", crop.top);
        printf("bottom: %ld\n", crop.bottom);

        sub_save_image_cropped(&im, &crop, "cropped.png");

        sub_image_destroy(&im);
        return EXIT_SUCCESS;
}
