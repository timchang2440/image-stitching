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
Add parameter LOADHOMO in config.cfg.

You need to set **LOADHOMO** to 0 and run the code once to generate parameter.

It will save homography matrix parameter based on image set.

And then set **LOADHOMO** to 1 could skip the step before transformation.


Other details could be checked from original github.

# test