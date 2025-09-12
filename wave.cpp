#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <utility>
#include <vector>
#include <memory>
#include <algorithm>
#include <random>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "led-matrix.h"
#include "graphics.h"

constexpr int COLUMNS = 32;
constexpr int ROWS = 16;

int randomColumn() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, COLUMNS - 1);
    return dist(rng);
}

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
        std::ifstream is(file, std::ios::binary);
        sprite.load(is);
        return sprite; // RAII closes file
    }
};

struct Animation {
    virtual void init(rgb_matrix::FrameCanvas *buffer) = 0;
    virtual int sleep() = 0;
    virtual bool render(rgb_matrix::FrameCanvas *buffer) = 0;
};

struct BuoyObs {
    std::string name;
    bool up;
    std::array<double, COLUMNS> waveHeights;

    static BuoyObs load(std::istream &is) {
        BuoyObs obs;
        std::string line;
        std::getline(is, line);
        obs.up = line[0] == '+';
        std::getline(is, line);
        obs.name = line;
        for (auto &waveHeight: obs.waveHeights) {
            std::getline(is, line);
            waveHeight = std::stof(line);
        }
        return obs;
    }

    static std::vector<BuoyObs> load(const char *file) {
        std::ifstream is(file);
        std::string line;
        std::getline(is, line);
        int buoys = std::stoi(line);
        std::vector<BuoyObs> vec(buoys);
        for (int i = 0; i < buoys; i++) {
            vec[i] = load(is);
        }
        is.close();
        return vec;
    }
};


struct ScrollingMessage : Animation {
    Frame *frame;
    rgb_matrix::Font *font;
    rgb_matrix::Color *color;
    BuoyObs *obs;
    int left;

    ScrollingMessage(Frame *frame, rgb_matrix::Font *font, rgb_matrix::Color *color, BuoyObs *obs)
            : frame(frame), font(font), color(color), obs(obs), left(0) {}

    void init(rgb_matrix::FrameCanvas *buffer) override {
        left = buffer->width();
    }

    int sleep() override {
        return 50;
    }

    int render(rgb_matrix::FrameCanvas *buffer, int x, int y) const {
        return rgb_matrix::DrawText(
                buffer,
                *font,
                x,
                y + font->baseline(),
                *color,
                nullptr,
                obs->name.c_str(),
                0);
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        frame->render(buffer, left, 3);
        int length = render(buffer, left + frame->width + 2, 0);
        left--;
        return left + frame->width + 2 + length < 0;
    }
};

struct WaveHeightChart : Animation {
    enum class DropState {
        INIT,
        FALLING,
        EXITING
    };

    struct Drop {
        DropState state;
        int y;
        const double factor;

        explicit Drop(double factor) : state(DropState::INIT), y(0), factor(factor) {}
    };

    struct Column {
        std::vector<Drop> drops;

        void init(int count, double terminalFactor) {
            drops.clear();
            drops.reserve(count);
            for (int i = 0; i < count; i++) {
                drops.emplace_back(i < count - 1 ? 1.0 : terminalFactor);
            };
        }

        void advanceDropsInMotion() {
            for (int i = 0; i < drops.size(); i++) {
                auto &drop = drops[i];
                if (drop.state == DropState::EXITING || (drop.state == DropState::FALLING && drop.y < ROWS - 1 - i)) {
                    drop.y++;
                }
            }
        }

        void transitionNextDrop(DropState from, DropState to) {
            auto it = std::find_if(
                    drops.begin(),
                    drops.end(),
                    [from](auto &d) { return d.state == from; });
            if (it != drops.end()) {
                it->state = to;
            }
        }

        void nextFallingDrop() {
            transitionNextDrop(DropState::INIT, DropState::FALLING);
        }

        void nextExitingDrop() {
            transitionNextDrop(DropState::FALLING, DropState::EXITING);
        }

        [[nodiscard]] bool allDropsCreated() const {
            return std::all_of(
                    drops.begin(),
                    drops.end(),
                    [](const Drop &d) { return d.state != DropState::INIT; });
        }

        [[nodiscard]] bool allDropsExiting() const {
            return std::all_of(
                    drops.begin(),
                    drops.end(),
                    [](const Drop &d) { return d.state == DropState::EXITING; });
        }

        [[nodiscard]] bool allDropsExited() const {
            return std::all_of(
                    drops.begin(),
                    drops.end(),
                    [](const Drop &d) { return d.state == DropState::EXITING && d.y >= ROWS; });
        }

        void renderDrops(int x, rgb_matrix::FrameCanvas *buffer) {
            for (const auto &drop: drops) {
                if (drop.state != DropState::INIT) {
                    int blue = static_cast<int>(255 * drop.factor);
                    buffer->SetPixel(x, drop.y, 0, 0, blue);
                }
            }
        }
    };

    struct Grid {
        std::array<Column, COLUMNS> columns;

        void init(const std::array<double, COLUMNS> &heights) {
            for (int x = 0; x < COLUMNS; x++) {
                double h = heights[x];
                int numColumns = static_cast<int>(std::ceil(h));
                double factor = h - std::floor(h);
                columns[x].init(numColumns, factor == 0 ? 1.0 : factor);
            }
        }

        void advanceDropsInMotion() {
            for (auto &column: columns) {
                column.advanceDropsInMotion();
            }
        }

        [[nodiscard]] bool allDropsCreated() const {
            return std::all_of(
                    columns.begin(),
                    columns.end(),
                    [](const Column &c) { return c.allDropsCreated(); });
        }

        [[nodiscard]] bool allDropsExiting() const {
            return std::all_of(
                    columns.begin(),
                    columns.end(),
                    [](const Column &c) { return c.allDropsExiting(); });
        }

        [[nodiscard]] bool allDropsExited() const {
            return std::all_of(
                    columns.begin(),
                    columns.end(),
                    [](const Column &c) { return c.allDropsExited(); });
        }

        void addRandomDrop() {
            int c = randomColumn();
            while (columns[c].allDropsCreated()) {
                c = (c + 1) % COLUMNS;
            }
            columns[c].nextFallingDrop();
        }

        void exitRandomDrop() {
            int c = randomColumn();
            while (columns[c].allDropsExiting()) {
                c = (c + 1) % COLUMNS;
            }
            columns[c].nextExitingDrop();
        }

        void renderDrops(rgb_matrix::FrameCanvas *buffer) {
            for (int x = 0; x < COLUMNS; x++) {
                columns[x].renderDrops(x, buffer);
            }
        }
    };

    static constexpr int SLEEP_DURATION_TICKS = 100;

    const BuoyObs *obs;
    Grid grid;
    int sleepTicks;

    explicit WaveHeightChart(BuoyObs *obs) : obs(obs), sleepTicks(0) {}

    void init(rgb_matrix::FrameCanvas *buffer) override {
        sleepTicks = 0;
        grid.init(obs->waveHeights);
    }

    int sleep() override {
        return 20;
    }

    bool render(rgb_matrix::FrameCanvas *buffer) override {
        grid.advanceDropsInMotion();
        if (!grid.allDropsCreated()) {
            grid.addRandomDrop();
        } else if (sleepTicks < SLEEP_DURATION_TICKS) {
            sleepTicks++;
        } else if (!grid.allDropsExiting()) {
            grid.exitRandomDrop();
        }
        grid.renderDrops(buffer);
        return grid.allDropsExited();
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

void renderLoop(
        std::vector<std::unique_ptr<Animation>> &animations,
        rgb_matrix::RGBMatrix *canvas,
        rgb_matrix::FrameCanvas *buffer) {
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

    auto waveAnimation = std::make_unique<SpriteAnimation>();
    waveAnimation->add(&fadeInSprite, 1);
    waveAnimation->add(&waveSprite, 4);
    waveAnimation->add(&fadeOutSprite, 1);

    std::vector<std::unique_ptr<Animation>> animations;
    animations.push_back(std::move(waveAnimation));

    std::vector<BuoyObs> observations = BuoyObs::load(argv[2]);
    for (auto &obs: observations) {
        auto &frame = arrowsSprite.frames[obs.up ? 0 : 1];
        animations.push_back(std::make_unique<ScrollingMessage>(&frame, &font, &color, &obs));
        animations.push_back(std::make_unique<WaveHeightChart>(&obs));
    }

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    auto buffer = canvas->CreateFrameCanvas();
    renderLoop(animations, canvas, buffer);

    canvas->Clear();
    delete canvas;

    return 0;
}
