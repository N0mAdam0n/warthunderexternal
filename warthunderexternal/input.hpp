#pragma once

namespace Input {
    bool Init();
    void Shutdown();
    bool IsReady();
    void MoveMouseRelative(int dx, int dy);
}