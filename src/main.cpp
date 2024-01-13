#include <rio.h>

#include "editor.h"

static constexpr rio::InitializeArg cInitializeArg = {
    .window = {
        .resizable = true,
        .gl_major = 4,
        .gl_minor = 3
    }
};

int main()
{
    if (!rio::Initialize<Editor>(cInitializeArg))
        return -1;

    rio::EnterMainLoop();

    rio::Exit();
    return 0;
}
