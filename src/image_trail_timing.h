#pragma once

namespace atdd::image_trail {

// Converts HPL2's image-trail amount into the temporary amount needed by the
// stock shader-alpha calculation so its 60 Hz persistence is preserved over
// real time at any presentation interval.
[[nodiscard]] bool CorrectAmount(float amount,
                                 float frameTimeSeconds,
                                 float& correctedAmount) noexcept;

} // namespace atdd::image_trail
