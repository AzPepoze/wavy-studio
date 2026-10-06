#include "audio/Transport.hpp"

namespace wavy {
void Transport::setLoop(std::int64_t begin, std::int64_t end, bool enabled) noexcept {
    loopVersion_.fetch_add(1);
    loopBegin_.store(begin);
    loopEnd_.store(end);
    loopEnabled_.store(enabled && begin >= 0 && end > begin);
    loopVersion_.fetch_add(1);
}
} // namespace wavy
