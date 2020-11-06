#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include "led-matrix.h"

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

    void render(rgb_matrix::FrameCanvas *buffer) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int pos = y * width + x;
                auto &pixel = pixels[pos];
                buffer->SetPixel(x, y, pixel.r, pixel.g, pixel.b);
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

void renderSprite(rgb_matrix::RGBMatrix *canvas, Sprite *wave, Sprite *fadeIn, Sprite *fadeOut) {
    Sprite *sprites[] = {fadeIn, wave, fadeOut};
    int loops[] = {1, 3, 1};
    auto buffer = canvas->CreateFrameCanvas();
    while (!interrupted) {
        for (int i = 0; !interrupted && i < 3; i++) {
            auto spr = sprites[i];
            for (int j = 0; !interrupted && j < loops[i]; j++) {
                for (int k = 0; !interrupted && k < spr->numFrames; k++) {
                    auto &frame = spr->frames[k];
                    buffer->Clear();
                    frame.render(buffer);
                    buffer = canvas->SwapOnVSync(buffer);
                    usleep(60 * 1000);
                }
            }
        }
    }
}

rgb_matrix::RGBMatrix::Options makeOptions() {
    rgb_matrix::RGBMatrix::Options options;
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

    auto *canvas = rgb_matrix::RGBMatrix::CreateFromFlags(&argc, &argv, &options);
    if (canvas == nullptr)
        return 1;

    auto waveSprite = Sprite::load("img/wave.bin");
    auto fadeInSprite = Sprite::load("img/fadein.bin");
    auto fadeOutSprite = Sprite::load("img/fadeout.bin");

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    renderSprite(canvas, &*waveSprite, &*fadeInSprite, &*fadeOutSprite);

    canvas->Clear();
    delete canvas;

    return 0;
}
