#pragma once

namespace dtb {
// One candidate needs two consecutive, spaced observations. Scheduling and
// candidate selection stay in the backend; this tracks only evidence.
class ModeWriteProbe {
public:
    enum class Result { Pending, Retained, Rejected };
    void reset() { m_reads = m_matches = 0; }
    Result observe(bool matches) {
        ++m_reads;
        m_matches = matches ? m_matches + 1 : 0;
        if (m_matches >= 2)
            return Result::Retained;
        return m_reads >= 3 ? Result::Rejected : Result::Pending;
    }
private:
    int m_reads = 0;
    int m_matches = 0;
};
} // namespace dtb
