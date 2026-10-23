#include "grandmp/windows/remote_player_binding.hpp"

namespace grandmp::windows::remote_player_binding {
void withFinally(void (*body)(void*),void (*restore)(void*) noexcept,void* context) {
    __try { body(context); }
    __finally { restore(context); }
}
} // namespace grandmp::windows::remote_player_binding
