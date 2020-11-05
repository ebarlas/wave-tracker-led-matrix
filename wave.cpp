#include "led-matrix.h"

#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

uint8_t readByte(std::istream &is) {
    char t;
    is.get(t);
    return (uint8_t) t;
}

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    void load(std::istream &is) {
        r = readByte(is);
        g = readByte(is);
        b = readByte(is);
    }
};

struct Frame {
    uint8_t width;
    uint8_t height;
    std::vector<Rgb> pixels;

    void load(std::istream &is) {
        width = readByte(is);
        height = readByte(is);
        pixels.resize(width * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                pixels[y * width + x].load(is);
            }
        }
    }
};

struct Sprite {
    uint8_t numFrames;
    std::vector<Frame> frames;

    void load(std::istream &is) {
        numFrames = readByte(is);
        frames.resize(numFrames);
        for (int i = 0; i < numFrames; i++) {
            frames[i].load(is);
        }
    }

    static std::unique_ptr<Sprite> load(const char *file) {
        auto ptr = std::make_unique<Sprite>();
        std::ifstream is(file);
        ptr->load(is);
        is.close();
        return ptr;
    }
};

volatile bool interrupted = false;

void interruptHandler(int signo) {
    interrupted = true;
}

void renderSprite(RGBMatrix *canvas, Sprite &sprite) {
    auto buffer = canvas->CreateFrameCanvas();
    int frame = 0;
    while (!interrupted) {
        buffer->Clear();
        auto &f = sprite.frames[frame];
        for (int y = 0; y < f.height; y++) {
            for (int x = 0; x < f.width; x++) {
                int pos = y * f.width + x;
                auto &pixel = f.pixels[pos];
                buffer->SetPixel(x, y, pixel.r, pixel.g, pixel.b);
            }
        }
        buffer = canvas->SwapOnVSync(buffer);
        frame = (frame + 1) % sprite.numFrames;
        usleep(60 * 1000);
    }
}

RGBMatrix::Options makeOptions() {
    RGBMatrix::Options options;
    options.hardware_mapping = "regular";
    options.rows = 16;
    options.cols = 32;
    options.chain_length = 1;
    options.parallel = 1;
    options.show_refresh_rate = false;
    options.limit_refresh_rate_hz = 60;
    options.brightness = 75;
    options.hardware_mapping = "classic-pi1";
    return options;
}

int main(int argc, char *argv[]) {
    auto options = makeOptions();

    auto *canvas = RGBMatrix::CreateFromFlags(&argc, &argv, &options);
    if (canvas == nullptr)
        return 1;

    auto ptr = Sprite::load("wave.bin");

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    renderSprite(canvas, *ptr);

    canvas->Clear();
    delete canvas;

    return 0;
}
