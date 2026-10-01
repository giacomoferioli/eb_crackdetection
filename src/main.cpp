#include "processing.hpp"

int main() {
    DummyProcessor application(load_config("data/config.json"));
    return application.run();
}
