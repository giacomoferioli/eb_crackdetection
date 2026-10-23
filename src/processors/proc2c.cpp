#include "processors/proc2c.hpp"

#include "image_saver.hpp"
#include "processors/specimenfinder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
constexpr float overlay_scaling = 1.0F;
constexpr float raw_scaling = 16.0F;
}

Proc2c::Proc2c(Config cfg) : FrameProcessor(cfg) {}

void Proc2c::on_setup(unsigned framenum) {
    this->processed_frame.reset(width, height, 0.0F);
    dic_reference.reference_frame.reset(width, height, 0.0F);
    overlay_frame.reset(width, height, 0.0F);

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

void Proc2c::on_termination(unsigned) {}

bool Proc2c::DIC_init(unsigned framenum) {
    this->body_blocks.clear();
    flap_blocks.clear();

    const std::optional<SpecimenPositionInfo>& specimen = shared_data().specimen_position;
    if(!specimen || !specimen->top || specimen->thickness <= 0.0F){
        std::cerr << "Proc2c cannot initialize without valid specimen position data.\n";
        return false;
    }

    procFrame();
    dic_reference.reference_frame = this->processed_frame;
    dic_reference.framenum = framenum;
    dic_reference.specimen_pos = *specimen;
    uncracked_specimen_thickness = specimen->thickness;

    const int body_height = std::max(2, static_cast<int>(std::lround(specimen->thickness)));
    const int top_height = body_height / 2;
    const int bottom_height = body_height - top_height;
    const int first_x = width / 4;
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

    return true;
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
    const float maximum_thickness = uncracked_specimen_thickness * (1.0F + Proc2ccfg::max_uncracked_thickness_increase);
    for(size_t i = 0; i < body_blocks.size(); i++){
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
    }
    draw();

    return ProcessResult::OK;
}

void Proc2c::draw(){
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
}
