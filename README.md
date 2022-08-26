# image_stitching
 
## Introduction
This code is from [ppwwyyxx/OpenPanno](https://github.com/ppwwyyxx/OpenPano).
- Add opencv package for camera stream. 
- save/load homegraphy matrix to reduce some step.

## Compile Dependencies
- gcc >= 4.7
- cmake >= 3.1
- opencv >= 4.1.0

## Compile
    $ mkdir build && cd build && cmake .. && make
    $ cp ../config.cfg .

## Change
- Add parameter LOADHOMO in config.cfg.
    You need to set **LOADHOMO** to 0 and run the code once to generate parameter.
    It will save homography matrix parameter based on image set.
    And then set **LOADHOMO** to 1 could skip the step before transformation.
	
- Add parameter **LOADCAM** in config.cfg would swich load camera or video.
	In parameter phase, it will open camera and catch the pictures.
	Also need to type the file name according to the camera number.


## Usage
	/*Generate homography matrix step, use png or jpg format. It would generate parameter, parameter2 and crop file.*/
	./image_stitch parameter file1 file2 ...
	
	/*Only Test picture and use parameter matrix, not concatenate final and first picture.
	./image_stitch test file1 file2 ...
	
	/*Setting OPENCAM to 0 in config.cfg would Load video.*/
	./image_stitch loop file1 file2 ...
	
	/*Setting OPENCAM to 1 in config.cfg would open camera, use camera index.*/
	./image_stitch loop 0 1 ...