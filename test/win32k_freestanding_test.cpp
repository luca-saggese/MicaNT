#include <cassert>
#include <cstdint>
#include <cstring>

#include "micant/km/win32k/freestanding.hpp"

namespace {

MicaWin32kRequest makeRequest(uint32_t operation) {
    MicaWin32kRequest request{};
    request.size = sizeof(request);
    request.version = MICANT_WIN32K_REQUEST_VERSION;
    request.operation = operation;
    return request;
}

void setText(uint16_t (&destination)[MICANT_WIN32K_TEXT_CAPACITY],
             const char* text) {
    size_t index = 0;
    while (text[index] && index + 1 < MICANT_WIN32K_TEXT_CAPACITY) {
        destination[index] = static_cast<uint8_t>(text[index]);
        ++index;
    }
}

} // namespace

int main() {
    using micant::NtStatus;
    using micant::win32k::freestanding::dispatchRequest;

    auto registration = makeRequest(MICANT_WIN32K_OP_REGISTER_CLASS);
    setText(registration.className, "MicaNT.Explorer.Desktop");
    assert(dispatchRequest(registration, 42) == NtStatus::Success);
    assert(registration.result != 0);

    auto creation = makeRequest(MICANT_WIN32K_OP_CREATE_WINDOW);
    setText(creation.className, "MicaNT.Explorer.Desktop");
    setText(creation.title, "MicaNT Desktop");
    creation.args[1] = 0x10000000;
    creation.args[2] = 10;
    creation.args[3] = 20;
    creation.args[4] = 640;
    creation.args[5] = 480;
    assert(dispatchRequest(creation, 42) == NtStatus::Success);
    const uint64_t window = creation.result;
    assert(window != 0);

    auto clientRect = makeRequest(MICANT_WIN32K_OP_GET_CLIENT_RECT);
    clientRect.args[0] = window;
    assert(dispatchRequest(clientRect, 43) == NtStatus::InvalidHandle);
    assert(dispatchRequest(clientRect, 42) == NtStatus::Success);
    assert(clientRect.rect.left == 0 && clientRect.rect.top == 0);
    assert(clientRect.rect.right == 640 && clientRect.rect.bottom == 480);

    auto show = makeRequest(MICANT_WIN32K_OP_SHOW_WINDOW);
    show.args[0] = window;
    show.args[1] = 5;
    assert(dispatchRequest(show, 42) == NtStatus::Success);

    auto position = makeRequest(MICANT_WIN32K_OP_SET_WINDOW_POS);
    position.args[0] = window;
    position.args[1] = 30;
    position.args[2] = 40;
    position.args[3] = 800;
    position.args[4] = 600;
    assert(dispatchRequest(position, 42) == NtStatus::Success);

    auto windowRect = makeRequest(MICANT_WIN32K_OP_GET_WINDOW_RECT);
    windowRect.args[0] = window;
    assert(dispatchRequest(windowRect, 42) == NtStatus::Success);
    assert(windowRect.rect.left == 30 && windowRect.rect.top == 40);
    assert(windowRect.rect.right == 830 && windowRect.rect.bottom == 640);

    auto post = makeRequest(MICANT_WIN32K_OP_POST_MESSAGE);
    post.args[0] = window;
    post.args[1] = 0x0100;
    post.args[2] = 'M';
    assert(dispatchRequest(post, 42) == NtStatus::Success);

    auto peek = makeRequest(MICANT_WIN32K_OP_PEEK_MESSAGE);
    peek.args[0] = window;
    peek.args[3] = 1;
    assert(dispatchRequest(peek, 42) == NtStatus::Success);
    assert(peek.result == 1 && peek.messageId == 0x0100);
    assert(peek.messageWParam == 'M');

    auto enumerate = makeRequest(MICANT_WIN32K_OP_ENUM_WINDOWS);
    assert(dispatchRequest(enumerate, 43) == NtStatus::Success);
    assert(enumerate.result == 0);
    assert(dispatchRequest(enumerate, 42) == NtStatus::Success);
    assert(enumerate.result == 1 && enumerate.windowHandles[0] == window);

    auto getText = makeRequest(MICANT_WIN32K_OP_GET_WINDOW_TEXT);
    getText.args[0] = window;
    assert(dispatchRequest(getText, 42) == NtStatus::Success);
    assert(getText.result == std::strlen("MicaNT Desktop"));
    assert(getText.text[0] == 'M');

    auto destroy = makeRequest(MICANT_WIN32K_OP_DESTROY_WINDOW);
    destroy.args[0] = window;
    assert(dispatchRequest(destroy, 42) == NtStatus::Success);
    assert(dispatchRequest(clientRect, 42) == NtStatus::InvalidHandle);

    auto quit = makeRequest(MICANT_WIN32K_OP_POST_QUIT);
    quit.args[0] = 7;
    assert(dispatchRequest(quit, 43) == NtStatus::Success);
    auto otherQueue = makeRequest(MICANT_WIN32K_OP_PEEK_MESSAGE);
    otherQueue.args[3] = 1;
    assert(dispatchRequest(otherQueue, 42) == NtStatus::Success);
    assert(otherQueue.result == 0);
    auto quitQueue = makeRequest(MICANT_WIN32K_OP_GET_MESSAGE);
    quitQueue.args[3] = 1;
    assert(dispatchRequest(quitQueue, 43) == NtStatus::Success);
    assert(quitQueue.result == 1 && quitQueue.messageId == 0x0012 &&
           quitQueue.messageWParam == 7);

    micant::win32k::freestanding::releaseProcess(42);
    auto registrationAfterRundown = makeRequest(MICANT_WIN32K_OP_REGISTER_CLASS);
    setText(registrationAfterRundown.className, "MicaNT.Explorer.Desktop");
    assert(dispatchRequest(registrationAfterRundown, 42) == NtStatus::Success);

    auto inputWindowRequest = makeRequest(MICANT_WIN32K_OP_CREATE_WINDOW);
    setText(inputWindowRequest.className, "MicaNT.Explorer.Desktop");
    setText(inputWindowRequest.title, "Input target");
    inputWindowRequest.args[1] = 0x10000000;
    inputWindowRequest.args[2] = 100;
    inputWindowRequest.args[3] = 100;
    inputWindowRequest.args[4] = 300;
    inputWindowRequest.args[5] = 200;
    assert(dispatchRequest(inputWindowRequest, 42) == NtStatus::Success);
    const uint64_t inputWindow = inputWindowRequest.result;
    micant::win32k::freestanding::dispatchMouseInput({150, 130, 0, 10});
    auto mouseMove = makeRequest(MICANT_WIN32K_OP_PEEK_MESSAGE);
    mouseMove.args[0] = inputWindow;
    mouseMove.args[3] = 1;
    assert(dispatchRequest(mouseMove, 42) == NtStatus::Success);
    assert(mouseMove.result == 1 && mouseMove.messageId == 0x0200);
    assert(mouseMove.messageLParam == 0x001E0032);
    assert(mouseMove.messageX == 150 && mouseMove.messageY == 130);

    micant::win32k::freestanding::dispatchMouseInput({150, 130, 1, 11});
    auto mouseDown = makeRequest(MICANT_WIN32K_OP_PEEK_MESSAGE);
    mouseDown.args[0] = inputWindow;
    mouseDown.args[3] = 1;
    assert(dispatchRequest(mouseDown, 42) == NtStatus::Success);
    assert(mouseDown.result == 1 && mouseDown.messageId == 0x0201);
    assert(mouseDown.messageWParam == 1);
    micant::win32k::freestanding::releaseProcess(42);
}
