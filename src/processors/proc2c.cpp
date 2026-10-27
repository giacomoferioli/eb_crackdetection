#include "processors/proc2c.hpp"

#include "image_saver.hpp"
#include "processors/specimenfinder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
constexpr float overlay_scaling = 1.0F;
constexpr float raw_scaling = 16.0F;

using Parabola = std::array<double,3>;

[[maybe_unused]] std::optional<Parabola> fit_parabola(const std::vector<Point>& points,double origin){
    double matrix[3][4]{};
    for(const Point& point : points){
        const double u = point.x - origin;
        const double terms[3]{u * u,u,1.0};
        for(int row = 0; row < 3; row++){
            for(int column = 0; column < 3; column++)
                matrix[row][column] += terms[row] * terms[column];
            matrix[row][3] += terms[row] * point.y;
        }
    }

    // [R] Solve the three normal equations with pivoting; shifting x by origin keeps the fit well scaled.
    for(int column = 0; column < 3; column++){
        int pivot = column;
        for(int row = column + 1; row < 3; row++)
            if(std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
                pivot = row;
        if(std::abs(matrix[pivot][column]) < 1e-12)
            return std::nullopt;
        if(pivot != column)
            for(int i = column; i < 4; i++)
                std::swap(matrix[column][i],matrix[pivot][i]);

        const double divisor = matrix[column][column];
        for(int i = column; i < 4; i++)
            matrix[column][i] /= divisor;
        for(int row = 0; row < 3; row++){
            if(row == column)
                continue;
            const double multiplier = matrix[row][column];
            for(int i = column; i < 4; i++)
                matrix[row][i] -= multiplier * matrix[column][i];
        }
    }
    return Parabola{matrix[0][3],matrix[1][3],matrix[2][3]};
}

[[maybe_unused]] double eval_parabola(const Parabola& parabola,double x,double origin){
    const double u = x - origin;
    return parabola[0] * u * u + parabola[1] * u + parabola[2];
}

Line fit_line(const std::vector<Point>& points){
    float center_x = 0.0F;
    float center_y = 0.0F;
    for(const Point& point : points){
        center_x += point.x;
        center_y += point.y;
    }
    center_x /= points.size();
    center_y /= points.size();

    float covariance = 0.0F;
    float variance = 0.0F;
    for(const Point& point : points){
        covariance += (point.x - center_x) * (point.y - center_y);
        variance += (point.x - center_x) * (point.x - center_x);
    }
    return Line{{center_x,center_y},covariance / variance,Line::ANY,1.0F};
}
}

Proc2c::Proc2c(Config cfg)
    : FrameProcessor(cfg),
      output_file("data/proc2cout.csv"),
      pixel_scale_file("data/proc2c_pixel_scale.csv") {}

void Proc2c::on_setup(unsigned framenum) {
    this->processed_frame.reset(width, height, 0.0F);
    dic_reference.reference_frame.reset(width, height, 0.0F);
    overlay_frame.reset(width, height, 0.0F);
    thickness_measurements.clear();
    recent_vertical_pixel_scales.clear();
    raw_vertical_pixels_per_mm = 0.0F;
    filtered_vertical_pixels_per_mm = 0.0F;
    current_horizontal_pixels_per_mm = cfg.pixels_per_mm;
    pixel_scale_file
        << "time_s,specimen_y_px,measurement_count,raw_vertical_pixels_per_mm,"
           "filtered_vertical_pixels_per_mm,reference_vertical_pixels_per_mm,"
           "horizontal_pixels_per_mm\n";

    img_saver = std::make_unique<ImageSaver>(
        [this](int ix) {
            const int gray = std::clamp(
                static_cast<int>(std::lround(this->processed_frame[ix] * raw_scaling + 128.0F)),
                0,
                255);
            const float alpha = std::clamp(overlay_frame[ix] * overlay_scaling, 0.0F, 1.0F);
            const unsigned red = static_cast<unsigned>(std::lround(alpha * 255.0F + (1.0F - alpha) * gray));
            const unsigned green_blue = static_cast<unsigned>(std::lround((1.0F - alpha) * gray));
            return red << 24U | green_blue << 16U | green_blue << 8U | 0xFFU;
        },
        width,
        height,
        "proc_images/");

    initialized = DIC_init(framenum);
}

void Proc2c::on_termination(unsigned) {
    output_file.close();
    pixel_scale_file.close();
}

bool Proc2c::DIC_init(unsigned framenum) {
    this->body_blocks.clear();
    flap_blocks.clear();

    const std::optional<SpecimenPositionInfo>& specimen = shared_data().specimen_position;
    if(!specimen || !specimen->top || specimen->thickness <= 0.0F){
        std::cerr << "Proc2c cannot initialize without valid specimen position data.\n";
        return false;
    }
    if(cfg.pixels_per_mm <= 0.0F || cfg.vertical_pixels_per_mm_reference <= 0.0F){
        std::cerr << "Proc2c requires positive horizontal and vertical reference scales.\n";
        return false;
    }

    procFrame();
    dic_reference.reference_frame = this->processed_frame;
    dic_reference.framenum = framenum;
    dic_reference.specimen_pos = *specimen;

    const int body_height = std::max(2, static_cast<int>(std::lround(specimen->thickness)));
    const int top_height = body_height / 2;
    const int bottom_height = body_height - top_height;
    const int first_x = width / 8;
    const int last_x = width - Proc2ccfg::right_margin - Proc2ccfg::block_width;

    //places the blocks
    for(int x = first_x; x <= last_x; x += Proc2ccfg::block_spacing){
        const float center_x = x + 0.5F * Proc2ccfg::block_width;
        const int top_y = static_cast<int>(std::lround(specimen->top.eval(center_x)));
        if(top_y < 0 || top_y + body_height > height)
            continue;

        this->body_blocks.emplace_back(x, top_y, Proc2ccfg::block_width, body_height);
        this->flap_blocks.push_back({
            DicBlock(x, top_y, Proc2ccfg::block_width, top_height),
            DicBlock(x, top_y + top_height, Proc2ccfg::block_width, bottom_height)
        });
    }

    if(this->body_blocks.empty()){
        std::cerr << "Proc2c could not place any candidate blocks inside the frame.\n";
        return false;
    }

    update_pixel_scale(framenum);
    publish_tracking_regions();
    return true;
}

void Proc2c::publish_tracking_regions(){
    ProcessingData& data = shared_data();
    data.dic_tracking_regions.clear();
    data.dic_tracking_regions.reserve(body_blocks.size() + 2 * flap_blocks.size());

    const auto publish = [&data](const DicBlock& block){
        if(block.active)
            data.dic_tracking_regions.push_back({block.x,block.y,block.w,block.h});
    };

    // Publish every independently tracked rectangle. Overlap is intentional:
    // the recorder turns these regions into a union mask before filtering.
    for(const DicBlock& block : body_blocks)
        publish(block);
    for(const DicFlapPair& pair : flap_blocks){
        publish(pair.top);
        publish(pair.bottom);
    }

    data.dic_tracking_locked = !data.dic_tracking_regions.empty();
    data.dic_tracking_regions_version++;
}

void Proc2c::procFrame() {
    this->processed_frame.fill(0.0F);
    BasicFrame<std::int16_t> raw_frame = get_raw_frame();

    // [R] Suppress isolated hot pixels before applying the same compact blur
    // used by the existing Proc2 processors.
    for(int y = 1; y < height - 1; y++){
        for(int x = 1; x < width - 1; x++){
            const float neighbour_average = (
                std::abs(raw_frame(x - 1, y)) + std::abs(raw_frame(x + 1, y)) +
                std::abs(raw_frame(x, y - 1)) + std::abs(raw_frame(x, y + 1))) / 4.0F;

            if(std::abs(raw_frame(x, y)) > 5 &&
               std::abs(raw_frame(x, y)) > 8.0F * neighbour_average){
                raw_frame(x, y) = raw_frame(x, y) > 0
                    ? static_cast<std::int16_t>(std::lround(neighbour_average))
                    : static_cast<std::int16_t>(-std::lround(neighbour_average));
            }
        }
    }

    for(int y = 1; y < height - 1; y++){
        for(int x = 1; x < width - 1; x++){
            this->processed_frame(x, y) = (
                12.0F * raw_frame(x, y) +
                2.0F * (raw_frame(x + 1, y) + raw_frame(x - 1, y) +
                        raw_frame(x, y + 1) + raw_frame(x, y - 1)) +
                raw_frame(x + 1, y + 1) + raw_frame(x - 1, y + 1) +
                raw_frame(x - 1, y - 1) + raw_frame(x + 1, y - 1)) / 24.0F;
        }
    }
}

float Proc2c::correlate(int x, int y, const DicBlock& block) const {
    // [R] Reject both negative and upper-bound violations before indexing either
    // the current image or the stored DIC reference patch.
    if(x < 0 || y < 0 || block.x0 < 0 || block.y0 < 0 ||
       x + block.w > width || y + block.h > height ||
       block.x0 + block.w > width || block.y0 + block.h > height){
        std::cerr << "[Proc2c/correlate] invalid current position " << x << ' ' << y
                  << ", reference block: " << block.x0 << ' ' << block.y0 << ' '
                  << block.w << ' ' << block.h << '\n';
        return std::numeric_limits<float>::infinity();
    }

    float squared_error = 0.0F;
    for(int dx = 0; dx < block.w; dx++){
        for(int dy = 0; dy < block.h; dy++){
            const float difference = this->processed_frame(x + dx, y + dy) - dic_reference.reference_frame(block.x0 + dx, block.y0 + dy);
            squared_error += difference * difference;
        }
    }
    return squared_error;
}

void Proc2c::update_blocks_position(int maxoffset){
    // [R] Body blocks and both half blocks are tracked independently against the same reference frame.
    auto update_block = [this,maxoffset](DicBlock& block){
        float minscore = std::numeric_limits<float>::infinity();
        int miny = block.y0;

        for(int y = block.y0; y < block.y0 + maxoffset; y++){
            float score = correlate(block.x0,y,block);
            if(score < minscore){
                minscore = score;
                miny = y;
            }
        }
        block.y = miny;
    };

    for(DicBlock& block : body_blocks)
        if(block.active)
            update_block(block);

    for(DicFlapPair& pair : flap_blocks){
        if(pair.top.active)
            update_block(pair.top);
        if(pair.bottom.active)
            update_block(pair.bottom);
    }
}

void Proc2c::disable_invalid_blocks(){
    if(!right_edge_checked){
        /* [R] Whether a right-edge candidate lies outside the specimen is fixed, so check it on the
         * first DIC movement before later crack opening can make a valid block move differently.
         * Remove consecutive outliers from the right by comparing each one with the median movement
         * of the other active body blocks. Their half blocks are disabled too because no specimen is there. */
        for(size_t i = body_blocks.size(); i-- > 0;){
            if(!body_blocks[i].active)
                continue;

            std::vector<int> displacements;
            for(size_t j = 0; j < body_blocks.size(); j++)
                if(j != i && body_blocks[j].active)
                    displacements.push_back(body_blocks[j].y - body_blocks[j].y0);
            if(displacements.empty())
                break;

            std::sort(displacements.begin(),displacements.end());
            const float median = displacements.size() % 2
                ? displacements[displacements.size() / 2]
                : 0.5F * (displacements[displacements.size() / 2 - 1] + displacements[displacements.size() / 2]);
            const float displacement = body_blocks[i].y - body_blocks[i].y0;
            if(std::abs(displacement - median) <= Proc2ccfg::max_right_block_displacement_error)
                break;

            body_blocks[i].active = false;
            flap_blocks[i].top.active = false;
            flap_blocks[i].bottom.active = false;
        }
        right_edge_checked = true;
    }

    /* [R] A widening pair marks cracked specimen. Disable only its full body block; the two half
     * blocks stay active and continue following the independently moving upper and lower flaps. */
    for(size_t i = 0; i < body_blocks.size(); i++){
        const float maximum_thickness = SFcfg::specimen_thickness * vertical_pixels_per_mm() *
                                        (1.0F + Proc2ccfg::max_uncracked_thickness_increase);
        if(body_blocks[i].active && flap_blocks[i].top.active && flap_blocks[i].bottom.active &&
           flap_blocks[i].bottom.bottom() - flap_blocks[i].top.y > maximum_thickness)
            body_blocks[i].active = false;
    }
}

void Proc2c::update_dic_reference(unsigned framenum){
    // [R] The full specimen search is restricted to the vertical area currently occupied by the body blocks.
    int search_y0 = height;
    int search_y1 = 0;
    int search_x0 = width;
    for(const DicBlock& block : body_blocks){
        if(!block.active)
            continue;
        search_x0 = std::min(search_x0,block.x);
        search_y0 = std::min(search_y0,block.y - Proc2ccfg::specimen_outline_search_interval / 2);
        search_y1 = std::max(search_y1,block.bottom() + Proc2ccfg::specimen_outline_search_interval / 2);
    }
    search_y0 = std::max(search_y0,1);
    search_y1 = std::min(search_y1,height - 1);

    std::optional<SpecimenPositionInfo> specimen;
    if(search_x0 < width){
        const SpecimenSearchParameters search_parameters{
            SFcfg::number_of_search_column,
            SFcfg::search_column_width,
            search_x0,
            search_y0,
            search_y1,
            SFcfg::max_speciment_tilt,
            dic_reference.specimen_pos.thickness,
            SFcfg::mininum_number_of_agreeing_guesses,
            SFcfg::maximum_number_of_disagreeing_guesses};
        specimen = processed_frame.find_specimen(search_parameters,&overlay_frame);
    }

    auto center_from_local_lines = [this](DicBlock& body,DicFlapPair& flaps){
        const Line blackline = processed_frame.find_hline(
            flaps.top.x,flaps.top.y - Proc2ccfg::specimen_outline_search_interval / 2,
            flaps.top.w,Proc2ccfg::specimen_outline_search_interval,Line::BLACK);
        const Line whiteline = processed_frame.find_hline(
            flaps.bottom.x,flaps.bottom.bottom() - Proc2ccfg::specimen_outline_search_interval / 2,
            flaps.bottom.w,Proc2ccfg::specimen_outline_search_interval,Line::WHITE);
        const float top = blackline.eval(flaps.top.center_x());
        const float bottom = whiteline.eval(flaps.bottom.center_x());
        if(body.active)
            body.y = std::round((top + bottom - body.h) / 2);
        flaps.top.y = std::round(top);
        flaps.bottom.y = std::round(bottom) - flaps.bottom.h;
    };

    if(specimen){
        dic_reference.specimen_pos = *specimen;
        for(size_t i = 0; i < body_blocks.size(); i++){
            DicBlock& body = body_blocks[i];
            DicFlapPair& flaps = flap_blocks[i];
            if(!flaps.top.active || !flaps.bottom.active)
                continue;

            // [R] Opened pairs must keep following their local outlines instead of being pulled closed.
            if(!body.active){
                center_from_local_lines(body,flaps);
                continue;
            }
            const float top = specimen->top.eval(body.center_x());
            const float bottom = top + specimen->thickness;

            /* [R] The body block covers the complete specimen thickness, so its y is chosen by aligning
             * its center with the midpoint between the detected top and bottom outlines. The top half
             * is anchored directly to the top outline. The bottom half is moved so its bottom edge,
             * rather than its top edge, coincides with the bottom outline. Their heights stay unchanged. */
            body.y = std::round((top + bottom - body.h) / 2);
            flaps.top.y = std::round(top);
            flaps.bottom.y = std::round(bottom) - flaps.bottom.h;
        }
    }else{
        /* [R] If the full specimen search fails, use the Proc2b-style local centering. The black top
         * outline is searched around the DIC position of the top half. The white bottom outline is
         * searched around the bottom edge of the DIC-positioned bottom half. These local outlines then
         * center the body block and anchor the two half blocks with the same rules used above. */
        for(size_t i = 0; i < body_blocks.size(); i++){
            if(flap_blocks[i].top.active && flap_blocks[i].bottom.active)
                center_from_local_lines(body_blocks[i],flap_blocks[i]);
        }

        if(cfg.automatic_distortion_compensation){
            std::vector<float> thicknesses;
            for(size_t i = 0; i < body_blocks.size(); i++)
                if(body_blocks[i].active && flap_blocks[i].top.active && flap_blocks[i].bottom.active)
                    thicknesses.push_back(flap_blocks[i].bottom.bottom() - flap_blocks[i].top.y);

            // [R] The known 5.2 mm uncracked body gives the pixel scale at the newly centered height.
            if(!thicknesses.empty()){
                std::sort(thicknesses.begin(),thicknesses.end());
                const size_t middle = thicknesses.size() / 2;
                dic_reference.specimen_pos.thickness = thicknesses.size() % 2
                    ? thicknesses[middle]
                    : 0.5F * (thicknesses[middle - 1] + thicknesses[middle]);
            }
        }
    }

    /* [R] Centering changed the current positions. Make those positions the new DIC coordinates and
     * copy the current processed image, so every body and half block references the patch it now covers. */
    for(DicBlock& block : body_blocks){
        if(block.active){
            block.x0 = block.x;
            block.y0 = block.y;
        }
    }
    for(DicFlapPair& pair : flap_blocks){
        if(pair.top.active){
            pair.top.x0 = pair.top.x;
            pair.top.y0 = pair.top.y;
        }
        if(pair.bottom.active){
            pair.bottom.x0 = pair.bottom.x;
            pair.bottom.y0 = pair.bottom.y;
        }
    }
    dic_reference.reference_frame = processed_frame;
    dic_reference.framenum = framenum;
    update_pixel_scale(framenum);
}

bool Proc2c::update_pixel_scale(unsigned framenum){
    thickness_measurements.clear();

    std::vector<size_t> candidates;
    for(size_t i = 0; i < body_blocks.size(); i++)
        if(body_blocks[i].active && flap_blocks[i].top.active && flap_blocks[i].bottom.active)
            candidates.push_back(i);

    // Keep measurements away from the deforming crack front and the specimen's right edge.
    if(candidates.size() >= Proc2ccfg::minimum_thickness_measurements + 3){
        candidates.erase(candidates.begin(),candidates.begin() + 2);
        candidates.pop_back();
    }else if(candidates.size() >= Proc2ccfg::minimum_thickness_measurements + 2){
        candidates.erase(candidates.begin());
        candidates.pop_back();
    }

    if(candidates.size() < Proc2ccfg::minimum_thickness_measurements)
        return false;

    const size_t sample_count = std::min(
        candidates.size(),Proc2ccfg::maximum_thickness_measurements);
    std::vector<size_t> sampled_indices;
    sampled_indices.reserve(sample_count);
    for(size_t sample = 0; sample < sample_count; sample++){
        const size_t candidate_ix = sample_count == 1
            ? candidates.size() / 2
            : (sample * (candidates.size() - 1) + (sample_count - 1) / 2) /
              (sample_count - 1);
        sampled_indices.push_back(candidates[candidate_ix]);
    }

    struct ScaleSample {
        float thickness;
        float center_y;
        size_t overlay_ix;
    };
    std::vector<ScaleSample> scale_samples;
    for(size_t block_ix : sampled_indices){
        const DicFlapPair& pair = flap_blocks[block_ix];
        const int top_search_y = pair.top.y - Proc2ccfg::specimen_outline_search_interval / 2;
        const int bottom_search_y = pair.bottom.bottom() -
                                    Proc2ccfg::specimen_outline_search_interval / 2;
        if(top_search_y < 1 || bottom_search_y < 1 ||
           top_search_y + Proc2ccfg::specimen_outline_search_interval >= height ||
           bottom_search_y + Proc2ccfg::specimen_outline_search_interval >= height)
            continue;

        const Line top_line = processed_frame.find_hline(
            pair.top.x,top_search_y,pair.top.w,
            Proc2ccfg::specimen_outline_search_interval,Line::BLACK);
        const Line bottom_line = processed_frame.find_hline(
            pair.bottom.x,bottom_search_y,pair.bottom.w,
            Proc2ccfg::specimen_outline_search_interval,Line::WHITE);
        if(!top_line || !bottom_line ||
           top_line.strength <= 0.5F * pair.top.w ||
           bottom_line.strength <= 0.5F * pair.bottom.w)
            continue;

        const int x = pair.top.center_x();
        const float top_y = top_line.eval(x);
        const float bottom_y = bottom_line.eval(x);
        const float thickness = bottom_y - top_y;
        if(!std::isfinite(thickness) || thickness <= 0.0F)
            continue;

        const size_t overlay_ix = thickness_measurements.size();
        thickness_measurements.push_back({x,top_y,bottom_y,false});
        scale_samples.push_back({thickness,0.5F * (top_y + bottom_y),overlay_ix});
    }

    if(scale_samples.size() < Proc2ccfg::minimum_thickness_measurements)
        return false;

    std::vector<float> thicknesses;
    thicknesses.reserve(scale_samples.size());
    for(const ScaleSample& sample : scale_samples)
        thicknesses.push_back(sample.thickness);
    std::sort(thicknesses.begin(),thicknesses.end());
    const auto median_of_sorted = [](const std::vector<float>& values){
        const size_t middle = values.size() / 2;
        return values.size() % 2
            ? values[middle]
            : 0.5F * (values[middle - 1] + values[middle]);
    };
    const float median_thickness = median_of_sorted(thicknesses);

    std::vector<float> deviations;
    deviations.reserve(scale_samples.size());
    for(const ScaleSample& sample : scale_samples)
        deviations.push_back(std::abs(sample.thickness - median_thickness));
    std::sort(deviations.begin(),deviations.end());
    const float mad = median_of_sorted(deviations);
    const float maximum_deviation = std::max(
        Proc2ccfg::minimum_thickness_outlier_distance,
        Proc2ccfg::thickness_outlier_sigma * 1.4826F * mad);

    float thickness_sum = 0.0F;
    float center_y_sum = 0.0F;
    size_t accepted_count = 0;
    for(const ScaleSample& sample : scale_samples){
        if(std::abs(sample.thickness - median_thickness) > maximum_deviation)
            continue;
        thickness_measurements[sample.overlay_ix].accepted = true;
        thickness_sum += sample.thickness;
        center_y_sum += sample.center_y;
        accepted_count++;
    }
    if(accepted_count < Proc2ccfg::minimum_thickness_measurements)
        return false;

    const float average_thickness = thickness_sum / accepted_count;
    const float specimen_y = center_y_sum / accepted_count;
    raw_vertical_pixels_per_mm = average_thickness / SFcfg::specimen_thickness;
    if(!std::isfinite(raw_vertical_pixels_per_mm) || raw_vertical_pixels_per_mm <= 0.0F)
        return false;

    // Keep the specimen search's expected thickness synchronized with the robust local measurement.
    dic_reference.specimen_pos.thickness = average_thickness;
    recent_vertical_pixel_scales.push_back(raw_vertical_pixels_per_mm);
    if(recent_vertical_pixel_scales.size() > Proc2ccfg::pixel_scale_median_window)
        recent_vertical_pixel_scales.erase(recent_vertical_pixel_scales.begin());

    std::vector<float> ordered_scales = recent_vertical_pixel_scales;
    std::sort(ordered_scales.begin(),ordered_scales.end());
    const float median_scale = median_of_sorted(ordered_scales);
    if(filtered_vertical_pixels_per_mm <= 0.0F){
        filtered_vertical_pixels_per_mm = median_scale;
    }else if(median_scale > filtered_vertical_pixels_per_mm){
        filtered_vertical_pixels_per_mm += Proc2ccfg::pixel_scale_smoothing_factor *
                                           (median_scale - filtered_vertical_pixels_per_mm);
    }

    current_horizontal_pixels_per_mm = cfg.pixels_per_mm * std::sqrt(
        filtered_vertical_pixels_per_mm / cfg.vertical_pixels_per_mm_reference);
    pixel_scale_file << framenum * cfg.processing_interval << ',' << specimen_y << ','
                     << accepted_count << ',' << raw_vertical_pixels_per_mm << ','
                     << filtered_vertical_pixels_per_mm << ','
                     << cfg.vertical_pixels_per_mm_reference << ','
                     << current_horizontal_pixels_per_mm << '\n';
    return true;
}

float Proc2c::get_start_position() const{
    return 0.0F;
}

float Proc2c::horizontal_pixels_per_mm() const{
    if(cfg.automatic_distortion_compensation && current_horizontal_pixels_per_mm > 0.0F)
        return current_horizontal_pixels_per_mm;
    return cfg.pixels_per_mm;
}

float Proc2c::vertical_pixels_per_mm() const{
    if(cfg.automatic_distortion_compensation && filtered_vertical_pixels_per_mm > 0.0F)
        return filtered_vertical_pixels_per_mm;
    return cfg.vertical_pixels_per_mm_reference;
}

float Proc2c::dy_since_ref(unsigned framenum) const{
    return (framenum - dic_reference.framenum) * cfg.processing_interval *
           cfg.displacement_rate * vertical_pixels_per_mm() / 60.0F;
}

float Proc2c::find_crack(float& interpolation_center_x_mm){
    interpolation_center_x_mm = 0.0F;
    std::vector<size_t> samples;
    size_t uncracked_start = 0;
    while(uncracked_start < body_blocks.size() && !body_blocks[uncracked_start].active)
        uncracked_start++;
    if(uncracked_start == body_blocks.size())
        return 0.0F;

    // [R] Use only the consecutive opened pairs immediately left of the intact specimen.
    for(size_t i = uncracked_start; i-- > 0 && samples.size() < Proc2ccfg::crack_fit_block_count;){
        if(body_blocks[i].active || !flap_blocks[i].top.active || !flap_blocks[i].bottom.active)
            break;
        samples.push_back(i);
    }
    if(samples.size() < Proc2ccfg::crack_fit_block_count)
        return 0.0F;
    std::reverse(samples.begin(),samples.end());

    std::vector<Point> top_points;
    std::vector<Point> bottom_points;
    for(size_t i : samples){
        const DicBlock& top = flap_blocks[i].top;
        const DicBlock& bottom = flap_blocks[i].bottom;
        const Line top_inner_line = processed_frame.find_hline(
            top.x,top.bottom() - Proc2ccfg::specimen_outline_search_interval / 2,
            top.w,Proc2ccfg::specimen_outline_search_interval,Line::WHITE);
        const Line bottom_inner_line = processed_frame.find_hline(
            bottom.x,bottom.y - Proc2ccfg::specimen_outline_search_interval / 2,
            bottom.w,Proc2ccfg::specimen_outline_search_interval,Line::BLACK);
        if(!top_inner_line || !bottom_inner_line)
            return 0.0F;

        // [R] Every x covered by the two local lines contributes to the linear fits and is drawn.
        for(int line_x = top.x; line_x < top.x + top.w; line_x++){
            top_points.push_back({(float)line_x,top_inner_line.eval(line_x)});
            bottom_points.push_back({(float)line_x,bottom_inner_line.eval(line_x)});
            const int top_y = std::round(top_inner_line.eval(line_x));
            const int bottom_y = std::round(bottom_inner_line.eval(line_x));
            if(top_y >= 0 && top_y < height)
                overlay_frame(line_x,top_y) += 0.5F;
            if(bottom_y >= 0 && bottom_y < height)
                overlay_frame(line_x,bottom_y) += 0.5F;
        }
    }

    const Line top_fit = fit_line(top_points);
    const Line bottom_fit = fit_line(bottom_points);

    // [R] Display both fitted crack surfaces over the sampled opening even when no crack zero is accepted.
    const int fit_x0 = flap_blocks[samples.front()].top.x;
    const int fit_x1 = flap_blocks[samples.back()].top.x + flap_blocks[samples.back()].top.w;
    /*
    for(int x = fit_x0; x < width; x++){
        const int top_y = std::round(top_fit.eval(x));
        const int bottom_y = std::round(bottom_fit.eval(x));
        if(top_y >= 0 && top_y < height)
            overlay_frame(x,top_y) = 1.0F;
        if(bottom_y >= 0 && bottom_y < height)
            overlay_frame(x,bottom_y) = 1.0F;
    }
    */

    const float distance_slope = bottom_fit.m - top_fit.m;
    if(std::abs(distance_slope) < 1e-6F)
        return 0.0F;

    // [R] The crack tip is the single intersection of the two fitted lines.
    const float origin = 0.5F * (fit_x0 + fit_x1);
    const float x_zero_dist = origin - (bottom_fit.eval(origin) - top_fit.eval(origin)) / distance_slope;

    // [R] Mark the accepted meeting point; get_start_position is a pixel offset before conversion to mm.
    const int x_zero_dist_ix = std::round(x_zero_dist);
    const int extrapolation_x0 = std::max(0,std::min(fit_x0,x_zero_dist_ix));
    for(int x = extrapolation_x0; x < fit_x0; x++){
        const int top_y = std::round(top_fit.eval(x));
        const int bottom_y = std::round(bottom_fit.eval(x));
        if(top_y >= 0 && top_y < height)
            overlay_frame(x,top_y) = 1.0F;
        if(bottom_y >= 0 && bottom_y < height)
            overlay_frame(x,bottom_y) = 1.0F;
    }
    if(x_zero_dist_ix >= 0 && x_zero_dist_ix < width)
        for(int y = 0; y < height; y++)
            overlay_frame(x_zero_dist_ix,y) = 1.0F;
    const DicFlapPair& middle_pair = flap_blocks[samples[samples.size() / 2]];
    interpolation_center_x_mm =
        (middle_pair.top.center_x() - get_start_position()) / horizontal_pixels_per_mm();
    return (x_zero_dist - get_start_position()) / horizontal_pixels_per_mm();
}

ProcessResult Proc2c::process_frame(unsigned framenum) {
    if(!initialized)
        return ProcessResult::ERR;

    overlay_frame.fill(0.0F);
    procFrame();

    // [R] Predicted motion decides when to track, center and commit a fresh DIC reference.
    if(dy_since_ref(framenum) > Proc2ccfg::max_displacement_before_DIC_ref_update){
        update_blocks_position(3 + std::ceil(dy_since_ref(framenum)));
        disable_invalid_blocks();
        update_dic_reference(framenum);
        publish_tracking_regions();
    }
    draw();
    float interpolation_center_x_mm = 0.0F;
    const float crack_length = find_crack(interpolation_center_x_mm);
    output_file << framenum * cfg.processing_interval << ',' << crack_length << ','
                << interpolation_center_x_mm << '\n';

    return ProcessResult::OK;
}

void Proc2c::draw(){
#if P2C_DRAW
    for( DicFlapPair p : flap_blocks){
        if( !p.top.active)
            continue;
        DicBlock b = p.top;
        for(int _ = 0; _ < 2; _++){
            if(b.active)
                for(int i = b.x; i < b.x + b.w; i++)
                    for(int j = b.y; j < b.bottom(); j++)
                        overlay_frame(i,j) = 0.3;
            b = p.bottom;
        }
    }

    for( DicBlock b : this->body_blocks){
        if(!b.active)
            continue;
        for(int i = b.x; i < b.x + b.w; i++){
            overlay_frame(i,b.y) += 0.3;
            overlay_frame(i,b.bottom()-1) += 0.3;
        }

        for(int j = b.y; j < b.bottom(); j++){
            overlay_frame(b.x,j) += 0.3;
            overlay_frame(b.x + b.w - 1,j) += 0.3;
        }
    }

    // Thickness samples are vertical markers; rejected outliers remain visible but dimmer.
    for(const ThicknessMeasurement& measurement : thickness_measurements){
        const float intensity = measurement.accepted ? 1.0F : 0.55F;
        const int top_y = std::clamp(static_cast<int>(std::lround(measurement.top_y)),0,height - 1);
        const int bottom_y = std::clamp(static_cast<int>(std::lround(measurement.bottom_y)),0,height - 1);
        for(int y = std::min(top_y,bottom_y); y <= std::max(top_y,bottom_y); y++)
            overlay_frame(measurement.x,y) = std::max(overlay_frame(measurement.x,y),intensity);
        for(int dx = -3; dx <= 3; dx++){
            const int x = measurement.x + dx;
            if(x >= 0 && x < width){
                overlay_frame(x,top_y) = std::max(overlay_frame(x,top_y),intensity);
                overlay_frame(x,bottom_y) = std::max(overlay_frame(x,bottom_y),intensity);
            }
        }
    }
#endif
}
