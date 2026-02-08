#include <stdio.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <termios.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct framebuffer {
    int fd;
    char *fbp;
    char *buffer;
    int width;
    int height;
    int bpp;
} framebuffer;


typedef struct Image {
    int width;
    int height;
    int bpp;
    int stride;
    uint8_t *data;
} Image;

framebuffer *framebuffer_create(const char *device);
void framebuffer_destroy(framebuffer *fb);
void framebuffer_update(framebuffer *fb);
void framebuffer_clear_color(framebuffer *fb, uint8_t r, uint8_t g, uint8_t b);
void framebuffer_draw_image(framebuffer *image, int x, int y, Image *img);


Image *Image_load(const char *filename);
void Image_free(Image *img);

Image *Image_resize_linear(Image *src, int new_width, int new_height);






int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: zfbv <device> <input>\nExample: zfbv /dev/fb0 images/test2.jpg\n");
        return 1;
    }

    // framebuffer
    framebuffer *fb = framebuffer_create(argv[1]);
    if (fb == NULL) {
        return 1;
    }

    // image
    Image *img = Image_load(argv[2]);
    if (img == NULL) {
        framebuffer_destroy(fb);
        return 1;
    }

    // resized image
    int resized_width, resized_height;
    float scale_w = (float) fb->width / (float) img->width;
    float scale_h = (float) fb->height / (float) img->height;
    float scale = scale_w < scale_h ? scale_w : scale_h;
    float default_scale = scale * 0.8f;
    scale = default_scale;

    resized_width = (int) (img->width * scale);
    resized_height = (int) (img->height * scale);
    
    Image *resized = Image_resize_linear(img, resized_width, resized_height);
    if (resized == NULL) {
        Image_free(img);
        framebuffer_destroy(fb);
        return 1;
    }

    float prev_scale = scale;


    // config terminal
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);


    // main loop
    char ch;
    while (1) {
        int pos_x = (fb->width - resized->width) / 2;
        int pos_y = (fb->height - resized->height) / 2;
        
        framebuffer_clear_color(fb, 0, 0, 0);
        framebuffer_draw_image(fb, pos_x, pos_y, resized);
        framebuffer_update(fb);

        // handle input
        ch = getchar();
        
        if (ch == 'r') {
            scale = default_scale;
        }
        else if (ch == '+') {
            scale *= 1.2f;
        }
        else if (ch == '-') {
            scale /= 1.2f;
        }
        else if (ch == 'q') {
            break;
        }

        

        // clamp scale
        scale = scale < 0.1f ? 0.1f : scale;
        scale = scale > 5.0f ? 5.0f : scale;
        if (scale == prev_scale) { // no change
            continue;
        }
        prev_scale = scale;

        // resize image
        resized_width = (int) (img->width * scale);
        resized_height = (int) (img->height * scale);
        Image *new_resized = Image_resize_linear(img, resized_width, resized_height);
        if (new_resized != NULL) {
            Image_free(resized);
            resized = new_resized;
        }
    }

    // cleanup
    Image_free(img);
    Image_free(resized);
    framebuffer_destroy(fb);

    // restore terminal
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return 0;
}



framebuffer *framebuffer_create(const char *device) {
    framebuffer *fb = malloc(sizeof(framebuffer));
    if (fb == NULL) {
        printf("Failed to allocate framebuffer struct\n");
        return NULL;
    }

    fb->fd = open(device, O_RDWR);
    if (fb->fd == -1) {
        printf("Failed to open framebuffer device\n");
        free(fb);
        return NULL;
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        printf("Failed to get variable screen info\n");
        close(fb->fd);
        free(fb);
        return NULL;
    }

    fb->width = vinfo.xres;
    fb->height = vinfo.yres;
    fb->bpp = vinfo.bits_per_pixel / 8;

    int screensize = fb->width * fb->height * fb->bpp;
    fb->fbp = (char *) mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->fbp == MAP_FAILED) {
        printf("Failed to map framebuffer\n");
        close(fb->fd);
        free(fb);
        return NULL;
    }

    fb->buffer = malloc(screensize);
    if (fb->buffer == NULL) {
        printf("Failed to allocate framebuffer buffer\n");
        munmap(fb->fbp, screensize);
        close(fb->fd);
        free(fb);        return NULL;
    }
    printf("Framebuffer opened: %dx%d, %d bpp\n", fb->width, fb->height, fb->bpp);
    return fb;
}

void framebuffer_destroy(framebuffer *fb) {
    if (fb == NULL) return;
    int screensize = fb->width * fb->height * fb->bpp;
    munmap(fb->fbp, screensize);
    close(fb->fd);
    free(fb->buffer);
    free(fb);
}

void framebuffer_update(framebuffer *fb) {
    if (fb == NULL || fb->buffer == NULL || fb->fbp == NULL) return;
    int screensize = fb->width * fb->height * fb->bpp;
    memcpy(fb->fbp, fb->buffer, screensize);
}

void framebuffer_clear_color(framebuffer *fb, uint8_t r, uint8_t g, uint8_t b) {
    if (fb == NULL || fb->fbp == NULL) return;
    int screensize = fb->width * fb->height * fb->bpp;
    int bpp = fb->bpp;
    
    if (bpp < 3) {
        printf("Unsupported bits per pixel: %d\n", bpp * 8);
        return;
    }

    for (int i = 0; i < screensize; i += fb->bpp) {
        fb->buffer[i] = b;
        fb->buffer[i + 1] = g;
        fb->buffer[i + 2] = r;
    }
}

void framebuffer_draw_image(framebuffer *fb, int x_offset, int y_offset, Image *img) {
    if (fb == NULL || img == NULL) return;

    int screen_x_start = (x_offset < 0) ? 0 : x_offset;
    int screen_y_start = (y_offset < 0) ? 0 : y_offset;
    int screen_x_end = (x_offset + img->width > fb->width) ? fb->width : x_offset + img->width;
    int screen_y_end = (y_offset + img->height > fb->height) ? fb->height : y_offset + img->height;

    if (screen_x_start >= screen_x_end || screen_y_start >= screen_y_end) {
        return;
    }


    int bpp = fb->bpp;
    if (bpp < 3) {
        printf("Unsupported bits per pixel: %d\n", bpp * 8);
        return;
    }


    for (int y = screen_y_start; y < screen_y_end; y++) {
        int fb_row_offset = y * fb->width * fb->bpp;
    
        int img_y = y - y_offset;
        int img_row_offset = img_y * img->width * img->bpp;

        for (int x = screen_x_start; x < screen_x_end; x++) {
            int img_x = x - x_offset;
            
            int fb_idx = fb_row_offset + (x * fb->bpp);
            int img_idx = img_row_offset + (img_x * img->bpp);

            for (int c = 0; c < img->bpp; c++) {
                fb->buffer[fb_idx + c] = img->data[img_idx + img->bpp - 1 - c];
            }
        }
    }
}



Image *Image_load(const char *filename) {
    Image *img = malloc(sizeof(Image));
    if (img == NULL) {
        printf("Failed to allocate Image struct\n");
        return NULL;
    }

    img->data = stbi_load(filename, &img->width, &img->height, NULL, 3);
    if (img->data == NULL) {
        printf("Failed to load image: %s\n", filename);
        free(img);
        return NULL;
    }
    img->bpp = 3;
    img->stride = img->width * img->bpp;
    return img;
}

void Image_free(Image *img) {
    if (img == NULL) return;
    if (img->data != NULL) {
        stbi_image_free(img->data);
    }
    free(img);
}

Image *Image_resize_linear(Image *src, int new_width, int new_height) {
    Image *resized = malloc(sizeof(Image));
    if (resized == NULL) {
        printf("Failed to allocate resized Image struct\n");
        return NULL;
    }

    resized->width = new_width;
    resized->height = new_height;
    resized->bpp = src->bpp;
    resized->stride = resized->width * resized->bpp;
    resized->data = malloc(resized->height * resized->stride);
    if (resized->data == NULL) {
        printf("Failed to allocate resized image data\n");
        free(resized);
        return NULL;    
    }

    float x_ratio = (float) src->width / (float) new_width;
    float y_ratio = (float) src->height / (float) new_height;

    for (int y = 0; y < new_height; y++) {
        for (int x = 0; x < new_width; x++) {
            int src_x = (int) (x * x_ratio);
            int src_y = (int) (y * y_ratio);
            int src_index = src_y * src->stride + src_x * src->bpp;
            int dst_index = y * resized->stride + x * resized->bpp;
            for (int c = 0; c < src->bpp; c++) {
                resized->data[dst_index + c] = src->data[src_index + c];
            }
        }
    }

    return resized;
}