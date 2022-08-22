// File: main.cc
// Date: Wed Jun 17 20:29:58 2015 +0800
// Author: Yuxin Wu

#define _USE_MATH_DEFINES
#include <cmath>
#include <opencv2/opencv.hpp>

#include "feature/extrema.hh"
#include "feature/matcher.hh"
#include "feature/orientation.hh"
#include "lib/mat.h"
#include "lib/config.hh"
#include "lib/geometry.hh"
#include "lib/imgproc.hh"
#include "lib/planedrawer.hh"
#include "lib/polygon.hh"
#include "lib/timer.hh"
#include "stitch/cylstitcher.hh"
#include "stitch/match_info.hh"
#include "stitch/stitcher.hh"
#include "stitch/transform_estimate.hh"
#include "stitch/warp.hh"
#include "common/common.hh"
#include <ctime>
#include <cassert>
#include <unistd.h>

//#define IMGFILE(x) #x ".png"

#ifdef DISABLE_JPEG
#define IMGFILE(x) #x ".png"
#else
#define IMGFILE(x) #x ".jpg"
#endif


using namespace std;
using namespace pano;
using namespace config;

bool TEMPDEBUG = false;

const int LABEL_LEN = 7;

Mat32f opencv2img(cv::Mat img) {

	cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
	//cv::cvtColor(img, img, cv::COLOR_BGR2GRAY);
	unsigned w = img.cols, h = img.rows;
	Mat32f mat(h, w, 3);
	unsigned npixel = w * h;
	float* p = mat.ptr();
	unsigned char* data = img.data;

	REP(i, npixel) {
		*(p++) = (float)*(data++) / 255.0;
		*(p++) = (float)*(data++) / 255.0;
		*(p++) = (float)*(data++) / 255.0;
		data++;	// rgba
	}
	return mat;
	
}

cv::Mat img2opencv(Mat32f img) {

	int w = img.cols(), h = img.rows();
	cv::Mat res(h, w, CV_8UC3);
#pragma omp parallel for schedule(dynamic) 
	REP(i, h)
		REP(j, w) {
			res.ptr(i,j)[0] = img.at(i, j, 2) * 255.0;
			res.ptr(i,j)[1] = img.at(i, j, 1) * 255.0;
			res.ptr(i,j)[2] = img.at(i, j, 0) * 255.0;
		}

	//cv::cvtColor(res, res, cv::COLOR_BGR2RGBA);
	return res;
	
}

void test_extrema(const char* fname, int mode) {
	//auto mat = read_img(fname);
	cv::Mat image = cv::imread(fname);
	
	auto mat = opencv2img(image);
	
	ScaleSpace ss(mat, NUM_OCTAVE, NUM_SCALE);
	DOGSpace dog(ss);
	ExtremaDetector ex(dog);

	PlaneDrawer pld(mat);
	if (mode == 0) {
		auto extrema = ex.get_raw_extrema();
		PP(extrema.size());
		for (auto &i : extrema)
			pld.cross(i, LABEL_LEN / 2);
	} else if (mode == 1) {
		auto extrema = ex.get_extrema();
		cout << extrema.size() << endl;
		for (auto &i : extrema) {
			Coor c{(int)(i.real_coor.x * mat.width()), (int)(i.real_coor.y * mat.height())};
			pld.cross(c, LABEL_LEN / 2);
		}
	}
	write_rgb(IMGFILE(extrema), mat);
}

void test_orientation(const char* fname) {
	auto mat = read_img(fname);
	ScaleSpace ss(mat, NUM_OCTAVE, NUM_SCALE);
	DOGSpace dog(ss);
	ExtremaDetector ex(dog);
	auto extrema = ex.get_extrema();
	OrientationAssign ort(dog, ss, extrema);
	auto oriented_keypoint = ort.work();

	PlaneDrawer pld(mat);
	pld.set_rand_color();

	cout << "FeaturePoint size: " << oriented_keypoint.size() << endl;
	for (auto &i : oriented_keypoint)
		pld.arrow(Coor(i.real_coor.x * mat.width(), i.real_coor.y * mat.height()), i.dir, LABEL_LEN);
	write_rgb(IMGFILE(orientation), mat);
}

// draw feature and their match
void test_match(const char* f1, const char* f2) {
	list<Mat32f> imagelist;
	Mat32f pic1 = read_img(f1);
	Mat32f pic2 = read_img(f2);
	imagelist.push_back(pic1);
	imagelist.push_back(pic2);

	unique_ptr<FeatureDetector> detector;
	detector.reset(new SIFTDetector);
	vector<Descriptor> feat1 = detector->detect_feature(pic1),
										 feat2 = detector->detect_feature(pic2);
	print_debug("Feature: %lu, %lu\n", feat1.size(), feat2.size());

	Mat32f concatenated = hconcat(imagelist);
	PlaneDrawer pld(concatenated);

	FeatureMatcher match(feat1, feat2);
	auto ret = match.match();
	print_debug("Match size: %d\n", ret.size());
	for (auto &x : ret.data) {
		pld.set_rand_color();
		Vec2D coor1 = feat1[x.first].coor,
					coor2 = feat2[x.second].coor;
		Coor icoor1 = Coor(coor1.x + pic1.width()/2, coor1.y + pic1.height()/2);
		Coor icoor2 = Coor(coor2.x + pic2.width()/2 + pic1.width(), coor2.y + pic2.height()/2);
		pld.circle(icoor1, LABEL_LEN);
		pld.circle(icoor2, LABEL_LEN);
		pld.line(icoor1, icoor2);
	}
	write_rgb(IMGFILE(match), concatenated);
}

// draw inliers of the estimated homography
void test_inlier(const char* f1, const char* f2) {
	list<Mat32f> imagelist;
	//Mat32f pic1 = read_img(f1);
	//Mat32f pic2 = read_img(f2);
	cv::Mat image = cv::imread(f1);

	auto pic1 = opencv2img(image);
	image = cv::imread(f2);

	auto pic2 = opencv2img(image);
	
	imagelist.push_back(pic1);
	imagelist.push_back(pic2);

	unique_ptr<FeatureDetector> detector;
	detector.reset(new SIFTDetector);
	vector<Descriptor> feat1 = detector->detect_feature(pic1),
										 feat2 = detector->detect_feature(pic2);
	vector<Vec2D> kp1; for (auto& d : feat1) kp1.emplace_back(d.coor);
	vector<Vec2D> kp2; for (auto& d : feat2) kp2.emplace_back(d.coor);
	print_debug("Feature: %lu, %lu\n", feat1.size(), feat2.size());

	Mat32f concatenated = hconcat(imagelist);
	PlaneDrawer pld(concatenated);
	FeatureMatcher match(feat1, feat2);
	auto ret = match.match();
	print_debug("Match size: %d\n", ret.size());

	TransformEstimation est(ret, kp1, kp2,
			{pic1.width(), pic1.height()}, {pic2.width(), pic2.height()});
	MatchInfo info;
	est.get_transform(&info);
	print_debug("Inlier size: %lu, conf=%lf\n", info.match.size(), info.confidence);
	if (info.match.size() == 0)
		return;

	for (auto &x : info.match) {
		pld.set_rand_color();
		Vec2D coor1 = x.first,
					coor2 = x.second;
		Coor icoor1 = Coor(coor1.x + pic1.width()/2, coor1.y + pic1.height()/2);
		Coor icoor2 = Coor(coor2.x + pic2.width()/2, coor2.y + pic2.height()/2);
		pld.circle(icoor1, LABEL_LEN);
		pld.circle(icoor2 + Coor(pic1.width(), 0), LABEL_LEN);
		pld.line(icoor1, icoor2 + Coor(pic1.width(), 0));
	}
	pld.set_color(Color(0,0,0));
	Vec2D offset1(pic1.width()/2, pic1.height()/2);
	Vec2D offset2(pic2.width()/2 + pic1.width(), pic2.height()/2);

	// draw convex hull of inliers
	/*
	 *vector<Vec2D> pts1, pts2;
	 *for (auto& x : info.match) {
	 *  pts1.emplace_back(x.first + offset1);
	 *  pts2.emplace_back(x.second + offset2, 0));
	 *}
	 *auto hull = convex_hull(pts1);
	 *pld.polygon(hull);
	 *hull = convex_hull(pts2);
	 *pld.polygon(hull);
	 */

	// draw warped four edges
	Shape2D shape2{pic2.width(), pic2.height()}, shape1{pic1.width(), pic1.height()};

	// draw overlapping region
	Matrix homo(3,3);
	REP(i, 9) homo.ptr()[i] = info.homo[i];
	Homography inv = info.homo.inverse();
	auto p = overlap_region(shape1, shape2, homo, inv);
	PA(p);
	for (auto& v: p) v += offset1;
	pld.polygon(p);

	Matrix invM(3, 3);
	REP(i, 9) invM.ptr()[i] = inv[i];
	p = overlap_region(shape2, shape1, invM, info.homo);
	PA(p);
	for (auto& v: p) v += offset2;
	pld.polygon(p);

	write_rgb(IMGFILE(inlier), concatenated);
}

void test_warp(int argc, char* argv[]) {
	CylinderWarper warp(1);
	REPL(i, 2, argc) {
		Mat32f mat = read_img(argv[i]);
		warp.warp(mat);
		write_rgb(("warp" + to_string(i) + ".jpg").c_str(), mat);
	}
}

void work(int argc, char* argv[]) {
/*
 *  vector<Mat32f> imgs(argc - 1);
 *  {
 *    GuardedTimer tm("Read images");
 *#pragma omp parallel for schedule(dynamic)
 *    REPL(i, 1, argc)
 *      imgs[i-1] = read_img(argv[i]);
 *  }
 */
 	std::cout << "work start" << std::endl;
	vector<string> imgs;
	REPL(i, 1, argc) imgs.emplace_back(argv[i]);
	Mat32f res;
	StitcherBase *p;
	p = new CylinderStitcher(move(imgs));		

	res = p->build();
	if (CROP) {
		int oldw = res.width(), oldh = res.height();
		res = crop(res);
		print_debug("Crop from %dx%d to %dx%d\n", oldw, oldh, res.width(), res.height());
	}
	
	cv::Mat image = img2opencv(res);
	cv::namedWindow("Test window");
	
	cv::imshow("Test window", image);
	cv::waitKey(0);
	{
		GuardedTimer tm("Writing image");
		write_rgb(IMGFILE(out), res);
	}

}

void loop(int argc, char* argv[]) {

	LAZY_READ = 0;
	ifstream fin("crop");
	int shift = 100;
  	if(!fin.is_open())
    	error_exit("Parameter file can not read, please check file existed.\n");
	fin >> shift;
 	std::cout << "loop start" << std::endl;
	vector<string> imgs, imgs1;	
	REPL(i, 2, argc) imgs.emplace_back(" ");
	REP(i, 2) imgs1.emplace_back(" ");

	Mat32f res;
	CylinderStitcher *p = new CylinderStitcher(move(imgs)), *q = new CylinderStitcher(move(imgs1));
	std::cout << "load stream" << std::endl;
	if(OPENCAM) p->load_camera(imgs.size());
	else p->load_stream(imgs.size(), argv);
	while(char(cv::waitKey(30)) != 'q'){
		GuardedTimer tm("LOOP");
		res = p->build_stream(shift);
		Mat32f left(res.height(), int(res.width() / 2), 3);
		Mat32f right(res.height(), int(res.width() / 2), 3);
		REP(i, left.height())
		{
			float* dst = left.ptr(i, 0);
			const float* src = res.ptr(i);
			memcpy(dst, src, 3 * left.width() * sizeof(float));
		}
		REP(i, right.height())
		{
			float* dst = right.ptr(i, 0);
			const float* src = res.ptr(i, right.width());
			memcpy(dst, src, 3 * right.width() * sizeof(float));
		}
		res = q->build_two_image(right, left);
		// if (CROP) {
		// 	res = crop(res);
		// }
		cv::Mat image = img2opencv(res);
		cv::resize(image, image, image.size() / 2);
		cv::imshow("video window", image);
	}
	delete p;
	{
		GuardedTimer tm("Writing image");
		write_rgb(IMGFILE(out), res);
	}

}

void test(int argc, char* argv[]) {

 	std::cout << "test start" << std::endl;
	vector<string> imgs;
	REPL(i, 2, argc) imgs.emplace_back(argv[i]);
	Mat32f res;
	StitcherBase *p;
	p = new CylinderStitcher(move(imgs));		

	res = p->build();
	if (CROP) {
		int oldw = res.width(), oldh = res.height();
		res = crop(res);
		print_debug("Crop from %dx%d to %dx%d\n", oldw, oldh, res.width(), res.height());
	}
	
	cv::Mat image = img2opencv(res);
	cv::namedWindow("Test window");
	
	cv::imshow("Test window", image);
	cv::waitKey(0);
	{
		GuardedTimer tm("Writing image");
		write_rgb(IMGFILE(result), res);
	}
}

void parameter(int argc, char* argv[]) {

	LAZY_READ = 1;
	std::cout << "Generate parameter start!!" << std::endl;
	vector<string> imgs, imgs1;
	REPL(i, 2, argc) imgs.emplace_back(argv[i]);
	imgs1.emplace_back(argv[argc-1]); imgs1.emplace_back(argv[2]); 
	Mat32f res, res2;
	cv::Mat image = cv::imread(argv[2]);
	int times = image.cols * 0.03, crop = -1;
	CylinderStitcher *p = new CylinderStitcher(move(imgs)), 
					 *q = new CylinderStitcher(move(imgs1));
	for(int i = 0;i < times;i++){
		sleep(0.1);
		if(!p->build_save("parameter", res) || !q->build_save("parameter2", res2))
			continue;
		{
			string name = "result" + to_string(i) + ".jpg";
			GuardedTimer tm("Writing image");
			write_rgb(name, res);
			write_rgb("test.jpg", res2);
			crop = i;
		}
	}
	if(crop >= 0){
		ofstream fout("crop");
		m_assert(fout.good());
		fout << crop * 5;
		fout.close();
	}

}

void init_config() {
#define CFG(x) \
	x = Config.get(#x)
	const char* config_file = "config.cfg";
	ConfigParser Config(config_file);
	CFG(CYLINDER);
	CFG(TRANS);
	CFG(ESTIMATE_CAMERA);
	if (int(CYLINDER) + int(TRANS) + int(ESTIMATE_CAMERA) >= 2)
		error_exit("You set two many modes...\n");
	if (CYLINDER)
		print_debug("Run with cylinder mode.\n");
	else if (TRANS)
		print_debug("Run with translation mode.\n");
	else if (ESTIMATE_CAMERA)
		print_debug("Run with camera estimation mode.\n");
	else
		print_debug("Run with naive mode.\n");

	CFG(ORDERED_INPUT);
	if (!ORDERED_INPUT && !ESTIMATE_CAMERA)
		error_exit("Require ORDERED_INPUT under this mode!\n");

	CFG(CROP);
	CFG(STRAIGHTEN);
	CFG(FOCAL_LENGTH);
	CFG(MAX_OUTPUT_SIZE);
	CFG(LAZY_READ);	// TODO in cyl mode

	CFG(SIFT_WORKING_SIZE);
	CFG(NUM_OCTAVE);
	CFG(NUM_SCALE);
	CFG(SCALE_FACTOR);
	CFG(GAUSS_SIGMA);
	CFG(GAUSS_WINDOW_FACTOR);
	CFG(JUDGE_EXTREMA_DIFF_THRES);
	CFG(CONTRAST_THRES);
	CFG(PRE_COLOR_THRES);
	CFG(EDGE_RATIO);
	CFG(CALC_OFFSET_DEPTH);
	CFG(OFFSET_THRES);
	CFG(ORI_RADIUS);
	CFG(ORI_HIST_SMOOTH_COUNT);
	CFG(DESC_HIST_SCALE_FACTOR);
	CFG(DESC_INT_FACTOR);
	CFG(MATCH_REJECT_NEXT_RATIO);
	CFG(RANSAC_ITERATIONS);
	CFG(RANSAC_INLIER_THRES);
	CFG(INLIER_IN_MATCH_RATIO);
	CFG(INLIER_IN_POINTS_RATIO);
	CFG(SLOPE_PLAIN);
	CFG(LM_LAMBDA);
	CFG(MULTIPASS_BA);
	CFG(MULTIBAND);
	CFG(LOADHOMO);
	CFG(OPENCAM);
#undef CFG
}

void planet(const char* fname) {
	Mat32f test = read_img(fname);
	int w = test.width(), h = test.height();
	const int OUTSIZE = 1000, center = OUTSIZE / 2;
	Mat32f ret(OUTSIZE, OUTSIZE, 3);
	fill(ret, Color::NO);

	REP(i, OUTSIZE) REP(j, OUTSIZE) {
		real_t dist = hypot(center - i, center - j);
		if (dist >= center || dist == 0) continue;
		dist = dist / center;
		//dist = sqr(dist);	// TODO you can change this to see different effect
		dist = h - dist * h;

		real_t theta;
		if (j == center) {
			if (i < center)
				theta = M_PI / 2;
			else
				theta = 3 * M_PI / 2;
		} else {
			theta = atan((real_t)(center - i) / (center - j));
			if (theta < 0) theta += M_PI;
			if ((theta == 0) && (j > center)) theta += M_PI;
			if (center < i) theta += M_PI;
		}
		m_assert(0 <= theta);
		m_assert(2 * M_PI + EPS >= theta);

		theta = theta / (M_PI * 2) * w;

		update_min(dist, (real_t)h - 1);
		Color c = interpolate(test, dist, theta);
		float* p = ret.ptr(i, j);
		c.write_to(p);
	}
	write_rgb(IMGFILE(planet), ret);
}

int main(int argc, char* argv[]) {
	//if (argc <= 2)
	//	error_exit("Need at least two images to stitch.\n");
	TotalTimerGlobalGuard _g;
	srand(time(NULL));
	init_config();
	string command = argv[1];
	if (command == "raw_extrema")
		test_extrema(argv[2], 0);
	else if (command == "keypoint")
		test_extrema(argv[2], 1);
	else if (command == "orientation")
		test_orientation(argv[2]);
	else if (command == "match")
		test_match(argv[2], argv[3]);
	else if (command == "inlier")
		test_inlier(argv[2], argv[3]);
	else if (command == "warp")
		test_warp(argc, argv);
	else if (command == "planet")
		planet(argv[2]);
	else if (command == "loop")
		loop(argc, argv);
	else if (command == "test")
		test(argc, argv);
	else if (command == "parameter")
		parameter(argc, argv);
	else
		// the real routine
		work(argc, argv);
	return 0;
}

