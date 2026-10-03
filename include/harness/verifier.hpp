#ifndef CONCURRENT_LAB_VERIFIER_HPP
#define CONCURRENT_LAB_VERIFIER_HPP

#include <cstdint>

namespace lab::harness {
// Expect 0, 1, 2, ... . Only the consumer needs this object, so no atomic synchronization is required.
// The producer increments the sequence in each payload.
// The consumer checks that the sequence is exactly what it expects.
// Checking each value catches missing, duplicate and reordered items.
class SequenceVerifier {
public:
    bool stepCheck(std::uint64_t sequence) {
        if (sequence != next_) {
            valid_ = false; // Remember a failure even if later values look correct.
            return false;
        }
        ++next_;
        return true;
    }
    // A correct prefix is not enough: the entire expected sequence must arrive.
    bool completionCheck(std::uint64_t expected_count) const {
        return valid_ && next_ == expected_count;
    }
private:
    std::uint64_t next_ = 0;
    bool valid_ = true;
};
} // namespace lab::harness
#endif
