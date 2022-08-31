//File: blender.cc
//Author: Yuxin Wu <ppwwyyxx@gmail.com>

#include "blender.hh"

#include <iostream>
#include "lib/config.hh"
#include "lib/imgproc.hh"
#include "lib/timer.hh"
using namespace std;
using namespace config;

namespace pano {

void LinearBlender::add_image(
			const Coor& upper_left,
			const Coor& bottom_right,
			ImageRef &img,
			std::function<Vec2D(Coor)> coor_func) {
	images.emplace_back(ImageToAdd{Range{upper_left, bottom_right}, img, coor_func});
	target_size.update_max(bottom_right);
}

Mat32f LinearBlender::run() {
	Mat32f target(target_size.y, target_size.x, 3);

#define GET_COLOR_AND_W \
					Vec2D img_coor = img.map_coor(i, j); \
					if (img_coor.isNaN()) continue; \
					float r = img_coor.y, c = img_coor.x; \
					auto color = Color(img.imgref.img->ptr(r, c));\
					if (color.x < 0) continue; \
					float	w = 0.5 - fabs(c / img.imgref.width() - 0.5); \
					color *= w

	if (LAZY_READ) {
		// Use weighted pixel, to iterate over images (and free them) instead of target.
		// Will be a little bit slower
		Mat<float> weight(target_size.y, target_size.x, 1);
		memset(weight.ptr(), 0, target_size.y * target_size.x * sizeof(float));
		fill(target, Color::BLACK);
#pragma omp parallel for schedule(dynamic)
		REP(k, (int)images.size()) {
			auto& img = images[k];
			img.imgref.load();
			auto& range = img.range;
			for (int i = range.min.y; i < range.max.y; ++i) {
				float *row = target.ptr(i);
				float *wrow = weight.ptr(i);
				for (int j = range.min.x; j < range.max.x; ++j) {
					GET_COLOR_AND_W;
					//#pragma omp critical
					{
						row[j*3] += color.x;
						row[j*3+1] += color.y;
						row[j*3+2] += color.z;
						wrow[j] += w;
					}
				}
			}
			img.imgref.release();
		}
#pragma omp parallel for schedule(dynamic)
		REP(i, target.height()) {
			auto row = target.ptr(i);
			auto wrow = weight.ptr(i);
			REP(j, target.width()) {
				if (wrow[j]) {
					*(row++) /= wrow[j]; *(row++) /= wrow[j]; *(row++) /= wrow[j];
				} else {
					*(row++) = -1; *(row++) = -1; *(row++) = -1;
				}
			}
		}
	} else {
		fill(target, Color::BLACK);
#pragma omp parallel for schedule(dynamic)
		for (int i = 0; i < target.height(); i ++) {
			float *row = target.ptr(i);
			for (int j = 0; j < target.width(); j ++) {
				Color isum = Color::BLACK;
				float wsum = 0;
				for (auto& img : images) if (img.range.contain(i, j)) {
					GET_COLOR_AND_W;
					isum += color;
					wsum += w;
				}
				if (wsum > 0)	// keep original Color::NO
					(isum / wsum).write_to(row + j * 3);
			}
		}
	}
	return target;
}

Matuc LinearBlender::run_uc() {
    Matuc target(target_size.y, target_size.x, 3);
    fill(target, 0);
    int smoothPixel = 30, size = images.size();
    for (int z=0; z<size; z++) {
        int minPosition, maxPosition;
        if (z==0) {
            minPosition = images[z].range.min.x;
            maxPosition = ((images[z].range.max.x+images[z+1].range.min.x)>>1)-smoothPixel;
        } else if (z==(size-1)) {
            minPosition = ((images[z-1].range.max.x+images[z].range.min.x)>>1)+smoothPixel;
            maxPosition = images[z].range.max.x;
        } else {
            minPosition = ((images[z-1].range.max.x+images[z].range.min.x)>>1)+smoothPixel;
            maxPosition = ((images[z].range.max.x+images[z+1].range.min.x)>>1)-smoothPixel;
        }
#pragma omp parallel for schedule(dynamic)                        
        for (int i = images[z].range.min.y; i < images[z].range.max.y; i ++) {
            auto des = target.ptr(i, minPosition);
            auto src = images[z].imgref.imguc->ptr(i, minPosition-images[z].range.min.x);
            memcpy(des, src, sizeof(unsigned char)*(maxPosition-minPosition)*3);
        }
    }

    for (int z=0; z<(size-1); z++) {
        int rightCenter = (images[z+1].range.min.x + images[z].range.max.x) >> 1;
#pragma omp parallel for schedule(dynamic)
        for (int i = images[z].range.min.y; i < images[z].range.max.y; i ++) {
            int count = 0;
            for (int j=(rightCenter-smoothPixel); j<=(rightCenter+smoothPixel); j++) {
                unsigned char* p = images[z].imgref.imguc->ptr(i, j-images[z].range.min.x);
                unsigned char* q = images[z+1].imgref.imguc->ptr(i, j-images[z+1].range.min.x);
                target.at(i, j, 0) = (((2*smoothPixel-count)*p[0]/(2*smoothPixel))+(count*q[0]/(2*smoothPixel)));
                target.at(i, j, 1) = (((2*smoothPixel-count)*p[1]/(2*smoothPixel))+(count*q[1]/(2*smoothPixel)));
                target.at(i, j, 2) = (((2*smoothPixel-count)*p[2]/(2*smoothPixel))+(count*q[2]/(2*smoothPixel)));
                count++;
            }
        }
    }
    return target;
}

}
