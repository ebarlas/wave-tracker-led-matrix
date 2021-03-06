#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>
#include <memory>
#include <algorithm>
#include "led-matrix.h"
#include "graphics.h"

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

    void render(rgb_matrix::FrameCanvas *buffer, int x, int y) const {
        buffer->SetPixel(x, y, r, g, b);
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

    void render(rgb_matrix::FrameCanvas *buffer, int left, int top) const {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int xBuf = left + x;
                int yBuf = top + y;
                if (xBuf >= 0 && xBuf < buffer->width() && yBuf >= 0 && yBuf < buffer->height()) {
                    pixels[y * width + x].render(buffer, xBuf, yBuf);
                }
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

    static Sprite load(const char *file) {
        Sprite sprite;
        std::ifstream is(file);
        sprite.load(is);
        is.close();
        return sprite;
    }
};

struct Animation {
    virtual void init(rgb_matrix::FrameCanvas *buffer) = 0;
    virtual int sleep() = 0;
    virtual bool render(rgb_matrix::FrameCanvas *buffer) = 0;
};

struct ScrollingMessage : Animation {
    Frame *frame;
    rgb_matrix::Font *font;
    rgb_matrix::Color *color;
    std::string message;
    int left;

    ScrollingMessage(Frame *frame, rgb_matrix::Font *font, rgb_matrix::Color *color, std::string message)
            : frame(frame), font(font), color(color), message(std::move(message)) {}

    void init(rgb_matrix::FrameCanvas *buffer) override {
        left = buffer->width();
    }

    int sleep() override {
        return 55;
    }

    int render(rgb_matrix::FrameCanvas *buffer, int x, int y) const {
        return rgb_matrix::DrawText(
                buffer,
                *font,
                x,
                y + font->baseline(),
                *color,
                nullptr,
                message.c_str(),
                0);
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        frame->render(buffer, left, 3);
        int length = render(buffer, left + frame->width + 2, 0);
        left--;
        return left + frame->width + 2 + length < 0;
    }
};

volatile bool interrupted = false;

void interruptHandler(int signo) {
    interrupted = true;
}

rgb_matrix::RGBMatrix::Options makeOptions() {
    rgb_matrix::RGBMatrix::Options options;
    options.rows = 16;
    options.cols = 32;
    options.chain_length = 1;
    options.parallel = 1;
    options.show_refresh_rate = false;
    options.brightness = 100;
    options.hardware_mapping = "regular";
    return options;
}

struct BuoyObs {
    std::string message;
    bool up;

    static BuoyObs load(std::istream &is) {
        std::string message;
        std::getline(is, message);
        return {message.substr(1), message[0] == '+'};
    }

    static std::vector<BuoyObs> load(const char *file) {
        std::vector<BuoyObs> vec;
        std::ifstream is(file);
        while (is.good()) {
            vec.push_back(load(is));
        }
        is.close();
        return vec;
    }
};

struct SpriteLoop {
    Sprite *sprite;
    int count;
};

struct SpriteAnimation : Animation {
    std::vector<SpriteLoop> sprites;
    int stage = 0;
    int loop = 0;
    int frame = 0;

    void add(Sprite *sprite, int count) {
        sprites.push_back({sprite, count});
    }

    void init(rgb_matrix::FrameCanvas *buffer) override {
        stage = 0;
    }

    int sleep() override {
        return 70;
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        auto &spr = sprites[stage];
        auto &sprFrame = spr.sprite->frames[frame];
        sprFrame.render(buffer, 0, 0);
        frame++;
        // last sprite frame
        if (frame == spr.sprite->numFrames) {
            frame = 0;
            loop++;
            // last cycle
            if (loop == spr.count) {
                loop = 0;
                stage++;
                // last sprite
                if (stage == static_cast<int>(sprites.size())) {
                    return true;
                }
            }
        }
        return false;
    }
};

void renderLoop(std::vector<Animation *> &animations, rgb_matrix::RGBMatrix *canvas, rgb_matrix::FrameCanvas *buffer) {
    auto it = animations.begin();
    (*it)->init(buffer);
    while (!interrupted) {
        buffer->Clear();
        bool complete = (*it)->render(buffer);
        buffer = canvas->SwapOnVSync(buffer);
        if (complete) {
            it++;
            if (it == animations.end()) {
                it = animations.begin();
            }
            (*it)->init(buffer);
        }
        usleep((*it)->sleep() * 1000);
    }
}

std::vector<ScrollingMessage> makeMessages(
        std::vector<BuoyObs> &observations,
        rgb_matrix::Font &font,
        rgb_matrix::Color &color,
        Sprite &arrowsSprite) {
    std::vector<ScrollingMessage> messages;
    for (auto &obs : observations) {
        auto &frame = arrowsSprite.frames[obs.up ? 0 : 1];
        messages.emplace_back(&frame, &font, &color, obs.message);
    }
    return messages;
}

int main(int argc, char *argv[]) {
    rgb_matrix::Font font;
    if (!font.LoadFont(argv[1])) {
        std::cerr << "Unable to load font: " << argv[1] << std::endl;
        return 1;
    }

    auto options = makeOptions();
    rgb_matrix::RuntimeOptions runtimeOptions;

    auto *canvas = rgb_matrix::RGBMatrix::CreateFromOptions(options, runtimeOptions);
    if (canvas == nullptr) {
        std::cerr << "Unable to create canvas" << std::endl;
        return 1;
    }

    rgb_matrix::Color color{0, 0, 255};

    auto waveSprite = Sprite::load("img/wave.bin");
    auto fadeInSprite = Sprite::load("img/fadein.bin");
    auto fadeOutSprite = Sprite::load("img/fadeout.bin");
    auto arrowsSprite = Sprite::load("img/arrows.bin");

    SpriteAnimation waveAnimation;
    waveAnimation.add(&fadeInSprite, 1);
    waveAnimation.add(&waveSprite, 4);
    waveAnimation.add(&fadeOutSprite, 1);

    std::vector<BuoyObs> observations = BuoyObs::load(argv[2]);
    std::vector<ScrollingMessage> messages = makeMessages(observations, font, color, arrowsSprite);

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    auto buffer = canvas->CreateFrameCanvas();

    std::vector<Animation *> animations;
    animations.push_back(&waveAnimation);
    std::for_each(messages.begin(), messages.end(), [&animations](ScrollingMessage &m) { animations.push_back(&m); });

    renderLoop(animations, canvas, buffer);

    canvas->Clear();
    delete canvas;

    return 0;
}
