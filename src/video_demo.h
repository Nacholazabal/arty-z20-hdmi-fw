/************************************************************************/
/*																		*/
/*	video_demo.h	--	ZYBO Video demonstration 						*/
/*																		*/
/************************************************************************/
/*	Author: Sam Bobrowicz												*/
/*	Copyright 2015, Digilent Inc.										*/
/************************************************************************/
/*  Module Description: 												*/
/*																		*/
/*		This file contains code for running a demonstration of the		*/
/*		Video input and output capabilities on the ZYBO. It is a good	*/
/*		example of how to properly use the display_ctrl and				*/
/*		video_capture drivers.											*/
/*																		*/
/*																		*/
/************************************************************************/
/*  Revision History:													*/
/* 																		*/
/*		11/25/2015(SamB): Created										*/
/*																		*/
/************************************************************************/

#ifndef VIDEO_DEMO_H_
#define VIDEO_DEMO_H_

/* ------------------------------------------------------------ */
/*				Include File Definitions						*/
/* ------------------------------------------------------------ */

#include "xil_types.h"

/* ------------------------------------------------------------ */
/*					Miscellaneous Declarations					*/
/* ------------------------------------------------------------ */

#define DEMO_PATTERN_0 0
#define DEMO_PATTERN_1 1

#define DEMO_MAX_FRAME (1920*1080*3)
#define DEMO_STRIDE (1920 * 3)

/*
 * Configure the Video capture driver to start streaming on signal
 * detection
 */
#define DEMO_START_ON_DET 1

/*
 * Two-level logging macros.
 * LOG_INFO / LOG_WARN / LOG_ERR are always active.
 * LOG_DBG is compiled out unless DEBUG_VERBOSE is defined (add -DDEBUG_VERBOSE
 * to the compiler flags in Xilinx SDK project settings to enable it).
 *
 * Note: xil_printf does not support %f — format floats manually as integer + fractional parts.
 */
#include "xil_printf.h"
#define LOG_INFO(fmt, ...)  xil_printf("[INFO] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  xil_printf("[WARN] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   xil_printf("[ERR ] " fmt "\r\n", ##__VA_ARGS__)
#ifdef DEBUG_VERBOSE
#define LOG_DBG(fmt, ...)   xil_printf("[DBG ] " fmt "\r\n", ##__VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)   do {} while(0)
#endif

/* ------------------------------------------------------------ */
/*					Procedure Declarations						*/
/* ------------------------------------------------------------ */

int DemoInitialize();
void DemoRun();
void DemoPrintMenu();
void DemoChangeRes();
void DemoCRMenu();
void DemoInvertFrame(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride);
void DemoPrintTest(u8 *frame, u32 width, u32 height, u32 stride, int pattern);
void DemoScaleFrame(u8 *srcFrame, u8 *destFrame, u32 srcWidth, u32 srcHeight, u32 destWidth, u32 destHeight, u32 stride);
void DemoISR(void *callBackRef, void *pVideo);
void DemoDiagnostics();

/* ------------------------------------------------------------ */

/************************************************************************/

#endif /* VIDEO_DEMO_H_ */
