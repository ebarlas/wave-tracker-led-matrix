#include "led-matrix.h"

#include <unistd.h>
#include <signal.h>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

volatile bool interrupted = false;

void interruptHandler(int signo) {
    interrupted = true;
}

void renderLoop(Canvas *canvas) {
    int row = 8;
    int col = 0;
    while (!interrupted) {
        canvas->Clear();
        canvas->SetPixel(col, row, 0, 0, 255);
        col = (col + 1) % canvas->width();
        usleep(10 * 1000);
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
    options.hardware_mapping = "classic-pi1";
    return options;
}

int main(int argc, char *argv[]) {
    auto options = makeOptions();

    auto *canvas = RGBMatrix::CreateFromFlags(&argc, &argv, &options);
    if (canvas == nullptr)
        return 1;

    signal(SIGTERM, interruptHandler);
    signal(SIGINT, interruptHandler);

    renderLoop(canvas);

    canvas->Clear();
    delete canvas;

    return 0;
}
