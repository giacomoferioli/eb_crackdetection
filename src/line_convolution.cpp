#include <algorithm>
#include <cmath>
#include <cstddef>

// Creates a (2 * n + 1) by (2 * n + 1) row-major convolution matrix.
// angle is expressed in radians: angle == 0 detects a horizontal line.
// thickness is the full width of the line in pixels.
void create_line_convolution_matrix(float* matrix, int n, float angle,float thickness) {
    if (matrix == nullptr || n < 0) {
        return;
    }

    const std::size_t side = 2U * static_cast<std::size_t>(n) + 1U;

    if (thickness <= 0.0F) {
        std::fill(matrix, matrix + side * side, 0.0F);
        return;
    }

    const float perpendicular_x = -std::sin(angle);
    const float perpendicular_y = std::cos(angle);
    const float half_thickness = thickness * 0.5F;
    float weight_sum = 0.0F;

    for (std::size_t row = 0; row < side; ++row) {
        const float y = static_cast<float>(row) - static_cast<float>(n);

        for (std::size_t column = 0; column < side; ++column) {
            const float x =static_cast<float>(column) - static_cast<float>(n);

            // Distance from the pixel centre to the line through the centre of
            // the matrix.  The extra half pixel gives partially covered edge
            // pixels a fractional, always non-negative weight.
            const float distance =std::abs(x * perpendicular_x + y * perpendicular_y);
            const float weight = half_thickness + 0.5F - distance;

            const float clamped_weight = std::clamp(weight, 0.0F, 1.0F);
            matrix[row * side + column] = clamped_weight;
            weight_sum += clamped_weight;
        }
    }

    // A kernel whose weights sum to one cannot produce a value greater than
    // one when every input pixel is in the range [0, 1].
    if (weight_sum > 0.0F) {
        for (std::size_t index = 0; index < side * side; ++index) {
            matrix[index] /= weight_sum;
        }
    }
}
