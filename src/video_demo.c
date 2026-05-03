/************************************************************************/
/*																		*/
/*	video_demo.c	--	ZYBO Video demonstration 						*/
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

/* ------------------------------------------------------------ */
/*				Include File Definitions						*/
/* ------------------------------------------------------------ */

#include "video_demo.h"
#include "video_capture/video_capture.h"
#include "display_ctrl/display_ctrl.h"
#include "intc/intc.h"
#include <stdio.h>
#include "xuartps.h"
#include "math.h"
#include <ctype.h>
#include <stdlib.h>
#include "xil_types.h"
#include "xil_cache.h"
#include "timer_ps/timer_ps.h"
#include "overlay_ctrl/overlay_ctrl.h"
#include "subtitle_hw/subtitle_hw.h"
#include "xparameters.h"

/*
 * XPAR redefines
 */
#define DYNCLK_BASEADDR XPAR_AXI_DYNCLK_0_BASEADDR
#define VGA_VDMA_ID XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VTC_ID XPAR_VTC_0_DEVICE_ID
#define VID_VTC_ID XPAR_VTC_1_DEVICE_ID
#define VID_GPIO_ID XPAR_AXI_GPIO_VIDEO_DEVICE_ID
#define VID_VTC_IRPT_ID XPS_FPGA3_INT_ID
#define VID_GPIO_IRPT_ID XPS_FPGA4_INT_ID
#define SCU_TIMER_ID XPAR_SCUTIMER_DEVICE_ID
#define UART_BASEADDR XPAR_PS7_UART_0_BASEADDR

/* ------------------------------------------------------------ */
/*				Global Variables								*/
/* ------------------------------------------------------------ */

/*
 * Display and Video Driver structs
 */
DisplayCtrl dispCtrl;
XAxiVdma vdma;
VideoCapture videoCapt;
INTC intc;
volatile char fRefresh; //flag used to trigger a refresh of the Menu on video detect

/*
 * Framebuffers for video data
 */
/*
 * Frame buffers aligned to 64 bytes. The AXI VDMA S2MM channel has a 64-bit
 * (8-byte) AXI data bus and rejects buffer addresses that are not word-aligned
 * (returns XST_INVALID_PARAM = 15 from DmaSetBufferAddr). DEMO_MAX_FRAME =
 * 1920*1080*3 = 6,220,800 bytes, which is divisible by 64, so all three frames
 * remain 64-byte aligned.
 */
u8 frameBuf[DISPLAY_NUM_FRAMES][DEMO_MAX_FRAME] __attribute__((aligned(64)));
u8 *pFrames[DISPLAY_NUM_FRAMES]; //array of pointers to the frame buffers

/*
 * Interrupt vector table
 */
const ivt_t ivt[] = {
	videoGpioIvt(VID_GPIO_IRPT_ID, &videoCapt),
	videoVtcIvt(VID_VTC_IRPT_ID, &(videoCapt.vtc))
};

/* ------------------------------------------------------------ */
/*				Procedure Definitions							*/
/* ------------------------------------------------------------ */

int main(void)
{
	if (DemoInitialize() != XST_SUCCESS)
	{
		xil_printf("\r\n[ERR] Demo initialization failed - system halted.\r\n");
		xil_printf("[ERR] If 'VDMA CfgInitialize failed': program the FPGA bitstream\r\n");
		xil_printf("[ERR] in Xilinx SDK before running (Xilinx -> Program FPGA).\r\n");
		while (1);
	}

	DemoRun();

	return 0;
}


int DemoInitialize()
{
	int Status;
	XAxiVdma_Config *vdmaConfig;
	int i;

	xil_printf("\r\n");
	LOG_INFO("=== Arty Z7 HDMI-In Demo Initializing ===");

	/*
	 * Initialize an array of pointers to the 3 frame buffers
	 */
	for (i = 0; i < DISPLAY_NUM_FRAMES; i++)
	{
		pFrames[i] = frameBuf[i];
	}
	LOG_DBG("Frame buffer pointers: [0]=0x%08X [1]=0x%08X [2]=0x%08X",
			(u32)pFrames[0], (u32)pFrames[1], (u32)pFrames[2]);

	/*
	 * Initialize a timer used for a simple delay
	 */
	Status = TimerInitialize(SCU_TIMER_ID);
	if (Status != XST_SUCCESS)
		LOG_WARN("SCU Timer init returned %d (timer may still work)", Status);
	else
		LOG_INFO("SCU Timer init OK");

	/*
	 * Initialize VDMA driver
	 */
	LOG_INFO("Looking up VDMA config (ID=%d)...", VGA_VDMA_ID);
	vdmaConfig = XAxiVdma_LookupConfig(VGA_VDMA_ID);
	if (!vdmaConfig)
	{
		LOG_ERR("No VDMA found for ID %d - check xparameters.h", VGA_VDMA_ID);
		return XST_FAILURE;
	}
	Status = XAxiVdma_CfgInitialize(&vdma, vdmaConfig, vdmaConfig->BaseAddress);
	if (Status != XST_SUCCESS)
	{
		LOG_ERR("VDMA CfgInitialize failed at base=0x%08X - is the FPGA bitstream programmed?",
				vdmaConfig->BaseAddress);
		return XST_FAILURE;
	}
	LOG_INFO("VDMA init OK (base=0x%08X)", vdmaConfig->BaseAddress);

	/*
	 * Initialize the Display controller and start it
	 */
	LOG_INFO("Initializing display controller (VTC ID=%d, dynclk=0x%08X)...",
			DISP_VTC_ID, DYNCLK_BASEADDR);
	Status = DisplayInitialize(&dispCtrl, &vdma, DISP_VTC_ID, DYNCLK_BASEADDR, pFrames, DEMO_STRIDE);
	if (Status != XST_SUCCESS)
	{
		LOG_ERR("Display controller init failed: %d", Status);
		return XST_FAILURE;
	}
	LOG_INFO("Display controller init OK");

	LOG_INFO("Starting display output...");
	Status = DisplayStart(&dispCtrl);
	if (Status != XST_SUCCESS)
	{
		LOG_ERR("DisplayStart failed: %d", Status);
		return XST_FAILURE;
	}
	LOG_INFO("Display started: %s (stride=%d, frame=%d)",
			dispCtrl.vMode.label, dispCtrl.stride, dispCtrl.curFrame);

	/*
	 * Initialize the Interrupt controller and start it.
	 */
	LOG_INFO("Initializing interrupt controller...");
	Status = fnInitInterruptController(&intc);
	if(Status != XST_SUCCESS) {
		LOG_ERR("Interrupt controller init failed: %d", Status);
		return XST_FAILURE;
	}
	fnEnableInterrupts(&intc, &ivt[0], sizeof(ivt)/sizeof(ivt[0]));
	LOG_INFO("Interrupts enabled: GPIO (ID=%d), VTC (ID=%d)",
			VID_GPIO_IRPT_ID, VID_VTC_IRPT_ID);

	/*
	 * Initialize the Video Capture device
	 */
	LOG_INFO("Initializing video capture (GPIO ID=%d, VTC ID=%d, startOnDet=%d)...",
			VID_GPIO_ID, VID_VTC_ID, DEMO_START_ON_DET);
	Status = VideoInitialize(&videoCapt, &intc, &vdma, VID_GPIO_ID, VID_VTC_ID, VID_VTC_IRPT_ID, pFrames, DEMO_STRIDE, DEMO_START_ON_DET);
	if (Status != XST_SUCCESS)
	{
		LOG_ERR("Video capture init failed: %d", Status);
		return XST_FAILURE;
	}
	LOG_INFO("Video capture init OK (HPD asserted - source should begin transmitting)");

	/*
	 * Set the Video Detect callback to trigger the menu to reset, displaying the new detected resolution
	 */
	VideoSetCallback(&videoCapt, DemoISR, (void *)(char *)&fRefresh);
	LOG_INFO("Signal detect/loss callback registered");

	/*
	 * Initialize subtitle BRAM overlay (axis_video_overlay_rect).
	 * This IP owns XPAR_AXIS_VIDEO_OVERLAY_R_0_BASEADDR; its register map
	 * supersedes the old overlay_ctrl layout, so overlay_init_defaults /
	 * overlay_apply_all are no longer called here.
	 * NOTE: SUBTITLE_BRAM_BASEADDR and SUBTITLE_OVLY_BASEADDR in
	 * subtitle_hw.h must match your xparameters.h before building.
	 */
	LOG_INFO("Initializing subtitle hardware...");
	subtitle_hw_init();
	LOG_INFO("Subtitle hardware initialized.");

	LOG_INFO("=== Init complete. Connect HDMI source, then press 5 to stream. ===");
	xil_printf("\r\n");

	return XST_SUCCESS;
}

void DemoRun()
{
	int nextFrame = 0;
	char userInput = 0;
	int Status;

	/* Flush UART FIFO */
	while (XUartPs_IsReceiveData(UART_BASEADDR))
	{
		XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
	}

	while (userInput != 'q')
	{
		fRefresh = 0;
		DemoPrintMenu();
		fRefresh = 0; /* suppress any interrupt that fired during menu print */

		/* Wait for data on UART or video detect interrupt */
		while (!XUartPs_IsReceiveData(UART_BASEADDR) && !fRefresh)
		{}

		/* Store the first character in the UART receive FIFO and echo it */
		if (XUartPs_IsReceiveData(UART_BASEADDR))
		{
			userInput = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
			xil_printf("%c\r\n", userInput);
		}
		else  //Refresh triggered by video detect interrupt
		{
			userInput = 'r';
		}

		switch (userInput)
		{
		case '1':
			DemoChangeRes();
			LOG_INFO("Display resolution: %s", dispCtrl.vMode.label);
			break;
		case '2':
			nextFrame = dispCtrl.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
				nextFrame = 0;
			LOG_INFO("Display frame buffer: %d -> %d", dispCtrl.curFrame, nextFrame);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			break;
		case '3':
			LOG_INFO("Writing blended gradient pattern to display frame %d...", dispCtrl.curFrame);
			DemoPrintTest(pFrames[dispCtrl.curFrame], dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE, DEMO_PATTERN_0);
			LOG_INFO("Done.");
			break;
		case '4':
			LOG_INFO("Writing color bar pattern to display frame %d...", dispCtrl.curFrame);
			DemoPrintTest(pFrames[dispCtrl.curFrame], dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE, DEMO_PATTERN_1);
			LOG_INFO("Done.");
			break;
		case '5':
			if (videoCapt.state == VIDEO_STREAMING)
			{
				LOG_INFO("Stopping video capture...");
				VideoStop(&videoCapt);
				LOG_INFO("Video capture stopped (state: PAUSED)");
			}
			else
			{
				LOG_INFO("Starting video capture...");
				Status = VideoStart(&videoCapt);
				if (Status == XST_NO_DATA)
					LOG_WARN("No HDMI signal detected (state=DISCONNECTED) - connect source first");
				else if (Status != XST_SUCCESS)
					LOG_ERR("VideoStart failed: %d (VDMA error)", Status);
				else
					LOG_INFO("Video capture streaming: %dx%d on frame %d",
							videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo,
							videoCapt.curFrame);
			}
			TimerDelay(1000000);
			break;
		case '6':
			nextFrame = videoCapt.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
				nextFrame = 0;
			LOG_INFO("Capture frame buffer: %d -> %d", videoCapt.curFrame, nextFrame);
			VideoChangeFrame(&videoCapt, nextFrame);
			break;
		case '7':
			nextFrame = videoCapt.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
				nextFrame = 0;
			LOG_INFO("Inverting %dx%d frame (capture=%d -> display=%d)...",
					videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo,
					videoCapt.curFrame, nextFrame);
			VideoStop(&videoCapt);
			DemoInvertFrame(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			LOG_INFO("Done. Displaying inverted frame %d.", nextFrame);
			break;
		case '8':
			nextFrame = videoCapt.curFrame + 1;
			if (nextFrame >= DISPLAY_NUM_FRAMES)
				nextFrame = 0;
			LOG_INFO("Scaling %dx%d -> %dx%d (this may take a moment)...",
					videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo,
					dispCtrl.vMode.width, dispCtrl.vMode.height);
			VideoStop(&videoCapt);
			DemoScaleFrame(pFrames[videoCapt.curFrame], pFrames[nextFrame], videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo, dispCtrl.vMode.width, dispCtrl.vMode.height, DEMO_STRIDE);
			VideoStart(&videoCapt);
			DisplayChangeFrame(&dispCtrl, nextFrame);
			LOG_INFO("Done. Displaying scaled frame %d.", nextFrame);
			break;
		case 'o':
			LOG_INFO("Entering overlay control mode...");
			overlay_uart_interactive_loop();
			break;
		case 't':
			LOG_INFO("Running subtitle hardware self-test...");
			subtitle_hw_self_test();
			break;
		case 'd':
			DemoDiagnostics();
			/* Wait for any key before returning to menu */
			while (!XUartPs_IsReceiveData(UART_BASEADDR))
			{}
			XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET); /* consume the key */
			break;
		case 'q':
			LOG_INFO("Quitting demo.");
			break;
		case 'r':
			/* Triggered by DemoISR on HDMI signal event */
			if (videoCapt.state == VIDEO_DISCONNECTED)
				LOG_WARN("HDMI event: source disconnected");
			else
				LOG_INFO("HDMI event: source connected (%dx%d, state=%s)",
						videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo,
						videoCapt.state == VIDEO_STREAMING ? "STREAMING" : "PAUSED");
			TimerDelay(2000000); /* 2s debounce - prevents refresh storm from rapid interrupts */
			fRefresh = 0;        /* suppress any interrupts that fired during debounce */
			break;
		default :
			xil_printf("\r\nInvalid selection '%c'\r\n", userInput);
			TimerDelay(500000);
		}
	}

	return;
}

void DemoPrintMenu()
{
	char captResStr[24];

	xil_printf("\x1B[H"); //Set cursor to top left of terminal
	xil_printf("\x1B[2J"); //Clear terminal

	xil_printf("**************************************************\n\r");
	xil_printf("*            Arty Z7 HDMI-In Demo                *\n\r");
	xil_printf("**************************************************\n\r");
	xil_printf("* --- DISPLAY OUTPUT ---                         *\n\r");
	xil_printf("*  Resolution : %32s*\n\r", dispCtrl.vMode.label);
	{ int _i = (int)dispCtrl.pxlFreq; int _f = (int)((dispCtrl.pxlFreq - _i) * 1000 + 0.5);
	  xil_printf("*  Pixel Clock: %24d.%03d MHz*\n\r", _i, _f); }
	xil_printf("*  Frame Buf  : %32d*\n\r", dispCtrl.curFrame);
	xil_printf("* --- HDMI INPUT ---                             *\n\r");

	switch (videoCapt.state)
	{
	case VIDEO_DISCONNECTED:
		xil_printf("*  Status     : %32s*\n\r", "DISCONNECTED");
		xil_printf("*  Resolution : %32s*\n\r", "N/A");
		break;
	case VIDEO_PAUSED:
		xil_printf("*  Status     : %32s*\n\r", "PAUSED (signal OK)");
		snprintf(captResStr, sizeof(captResStr), "%dx%d",
				videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo);
		xil_printf("*  Resolution : %32s*\n\r", captResStr);
		break;
	case VIDEO_STREAMING:
		xil_printf("*  Status     : %32s*\n\r", "STREAMING");
		snprintf(captResStr, sizeof(captResStr), "%dx%d",
				videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo);
		xil_printf("*  Resolution : %32s*\n\r", captResStr);
		break;
	default:
		xil_printf("*  Status     : %32s*\n\r", "UNKNOWN");
		xil_printf("*  Resolution : %32s*\n\r", "N/A");
		break;
	}
	xil_printf("*  Frame Buf  : %32d*\n\r", videoCapt.curFrame);
	xil_printf("**************************************************\n\r");
	xil_printf("\n\r");
	xil_printf("1 - Change Display Resolution\n\r");
	xil_printf("2 - Change Display Frame Buffer Index\n\r");
	xil_printf("3 - Write Blended Test Pattern to Display Frame Buffer\n\r");
	xil_printf("4 - Write Color Bar Test Pattern to Display Frame Buffer\n\r");
	xil_printf("5 - Start/Stop Video Capture (HDMI In -> Frame Buffer)\n\r");
	xil_printf("6 - Change Video Capture Frame Buffer Index\n\r");
	xil_printf("7 - Grab Frame and Invert Colors\n\r");
	xil_printf("8 - Grab Frame and Scale to Display Resolution\n\r");
	xil_printf("o - Overlay Control Mode (move/resize/color subtitle bar)\n\r");
	xil_printf("t - Subtitle Hardware Self-Test (checkerboard + blink)\n\r");
	xil_printf("d - Print System Diagnostics\n\r");
	xil_printf("q - Quit\n\r");
	xil_printf("\n\r");

	/* Context hint based on video state */
	if (videoCapt.state == VIDEO_DISCONNECTED)
		xil_printf(">> No HDMI source detected. Connect source and wait for signal.\n\r");
	else if (videoCapt.state == VIDEO_PAUSED)
		xil_printf(">> HDMI signal detected but not streaming. Press 5 to start.\n\r");
	else /* VIDEO_STREAMING */
		xil_printf(">> Streaming active. Display buf (%d) and capture buf (%d) %s for passthrough.\n\r",
				dispCtrl.curFrame, videoCapt.curFrame,
				(dispCtrl.curFrame == videoCapt.curFrame) ? "MATCH - OK" : "MISMATCH - adjust with 2/6");

	xil_printf("\n\rEnter selection: ");
}

void DemoChangeRes()
{
	int fResSet = 0;
	int status;
	char userInput = 0;

	if (dispCtrl.dynClkAddr == 0)
	{
		LOG_ERR("Cannot change resolution: display hardware not initialized");
		return;
	}

	/* Flush UART FIFO */
	while (XUartPs_IsReceiveData(UART_BASEADDR))
	{
		XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
	}

	while (!fResSet)
	{
		DemoCRMenu();

		/* Wait for data on UART */
		while (!XUartPs_IsReceiveData(UART_BASEADDR))
		{}

		/* Store the first character in the UART recieve FIFO and echo it */
		userInput = XUartPs_ReadReg(UART_BASEADDR, XUARTPS_FIFO_OFFSET);
		xil_printf("%c\r\n", userInput);
		status = XST_SUCCESS;
		switch (userInput)
		{
		case '1':
			LOG_INFO("Changing display to 640x480...");
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_640x480);
			status = (status != XST_SUCCESS) ? status : DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '2':
			LOG_INFO("Changing display to 800x600...");
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_800x600);
			status = (status != XST_SUCCESS) ? status : DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '3':
			LOG_INFO("Changing display to 1280x720...");
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1280x720);
			status = (status != XST_SUCCESS) ? status : DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '4':
			LOG_INFO("Changing display to 1280x1024...");
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1280x1024);
			status = (status != XST_SUCCESS) ? status : DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case '5':
			LOG_INFO("Changing display to 1920x1080...");
			status = DisplayStop(&dispCtrl);
			DisplaySetMode(&dispCtrl, &VMODE_1920x1080);
			status = (status != XST_SUCCESS) ? status : DisplayStart(&dispCtrl);
			fResSet = 1;
			break;
		case 'q':
			fResSet = 1;
			break;
		default :
			xil_printf("\r\nInvalid Selection\r\n");
			TimerDelay(500000);
		}
		if (status == XST_DMA_ERROR)
		{
			LOG_WARN("AXI VDMA error detected and cleared (insufficient memory bandwidth?)");
		}
		else if (status != XST_SUCCESS && fResSet)
		{
			LOG_ERR("Display start failed: %d", status);
		}
	}
}

void DemoCRMenu()
{
	xil_printf("\x1B[H"); //Set cursor to top left of terminal
	xil_printf("\x1B[2J"); //Clear terminal
	xil_printf("**************************************************\n\r");
	xil_printf("*                Arty Z7 HDMI In Demo            *\n\r");
	xil_printf("**************************************************\n\r");
	xil_printf("*Current Resolution: %28s*\n\r", dispCtrl.vMode.label);
	{ int _i = (int)dispCtrl.pxlFreq; int _f = (int)((dispCtrl.pxlFreq - _i) * 1000 + 0.5);
	  xil_printf("*Pixel Clock Freq. (MHz): %19d.%03d*\n\r", _i, _f); }
	xil_printf("**************************************************\n\r");
	xil_printf("\n\r");
	xil_printf("1 - %s\n\r", VMODE_640x480.label);
	xil_printf("2 - %s\n\r", VMODE_800x600.label);
	xil_printf("3 - %s\n\r", VMODE_1280x720.label);
	xil_printf("4 - %s\n\r", VMODE_1280x1024.label);
	xil_printf("5 - %s\n\r", VMODE_1920x1080.label);
	xil_printf("q - Quit (don't change resolution)\n\r");
	xil_printf("\n\r");
	xil_printf("Select a new resolution:");
}

void DemoInvertFrame(u8 *srcFrame, u8 *destFrame, u32 width, u32 height, u32 stride)
{
	u32 xcoi, ycoi;
	u32 lineStart = 0;
	for(ycoi = 0; ycoi < height; ycoi++)
	{
		for(xcoi = 0; xcoi < (width * 3); xcoi+=3)
		{
			destFrame[xcoi + lineStart] = ~srcFrame[xcoi + lineStart];         //Red
			destFrame[xcoi + lineStart + 1] = ~srcFrame[xcoi + lineStart + 1]; //Blue
			destFrame[xcoi + lineStart + 2] = ~srcFrame[xcoi + lineStart + 2]; //Green
		}
		lineStart += stride;
	}
	/*
	 * Flush the framebuffer memory range to ensure changes are written to the
	 * actual memory, and therefore accessible by the VDMA.
	 */
	Xil_DCacheFlushRange((unsigned int) destFrame, DEMO_MAX_FRAME);
}


/*
 * Bilinear interpolation algorithm. Assumes both frames have the same stride.
 */
void DemoScaleFrame(u8 *srcFrame, u8 *destFrame, u32 srcWidth, u32 srcHeight, u32 destWidth, u32 destHeight, u32 stride)
{
	float xInc, yInc; // Width/height of a destination frame pixel in the source frame coordinate system
	float xcoSrc, ycoSrc; // Location of the destination pixel being operated on in the source frame coordinate system
	float x1y1, x2y1, x1y2, x2y2; //Used to store the color data of the four nearest source pixels to the destination pixel
	int ix1y1, ix2y1, ix1y2, ix2y2; //indexes into the source frame for the four nearest source pixels to the destination pixel
	float xDist, yDist; //distances between destination pixel and x1y1 source pixels in source frame coordinate system

	int xcoDest, ycoDest; // Location of the destination pixel being operated on in the destination coordinate system
	int iy1; //Used to store the index of the first source pixel in the line with y1
	int iDest; //index of the pixel data in the destination frame being operated on

	int i;

	xInc = ((float) srcWidth - 1.0) / ((float) destWidth);
	yInc = ((float) srcHeight - 1.0) / ((float) destHeight);

	ycoSrc = 0.0;
	for (ycoDest = 0; ycoDest < destHeight; ycoDest++)
	{
		iy1 = ((int) ycoSrc) * stride;
		yDist = ycoSrc - ((float) ((int) ycoSrc));

		/*
		 * Save some cycles in the loop below by presetting the destination
		 * index to the first pixel in the current line
		 */
		iDest = ycoDest * stride;

		xcoSrc = 0.0;
		for (xcoDest = 0; xcoDest < destWidth; xcoDest++)
		{
			ix1y1 = iy1 + ((int) xcoSrc) * 3;
			ix2y1 = ix1y1 + 3;
			ix1y2 = ix1y1 + stride;
			ix2y2 = ix1y1 + stride + 3;

			xDist = xcoSrc - ((float) ((int) xcoSrc));

			/*
			 * For loop handles all three colors
			 */
			for (i = 0; i < 3; i++)
			{
				x1y1 = (float) srcFrame[ix1y1 + i];
				x2y1 = (float) srcFrame[ix2y1 + i];
				x1y2 = (float) srcFrame[ix1y2 + i];
				x2y2 = (float) srcFrame[ix2y2 + i];

				/*
				 * Bilinear interpolation function
				 */
				destFrame[iDest] = (u8) ((1.0-yDist)*((1.0-xDist)*x1y1+xDist*x2y1) + yDist*((1.0-xDist)*x1y2+xDist*x2y2));
				iDest++;
			}
			xcoSrc += xInc;
		}
		ycoSrc += yInc;
	}

	/*
	 * Flush the framebuffer memory range to ensure changes are written to the
	 * actual memory, and therefore accessible by the VDMA.
	 */
	Xil_DCacheFlushRange((unsigned int) destFrame, DEMO_MAX_FRAME);

	return;
}

void DemoPrintTest(u8 *frame, u32 width, u32 height, u32 stride, int pattern)
{
	u32 xcoi, ycoi;
	u32 iPixelAddr;
	u8 wRed, wBlue, wGreen;
	u32 wCurrentInt;
	double fRed, fBlue, fGreen, fColor;
	u32 xLeft, xMid, xRight, xInt;
	u32 yMid, yInt;
	double xInc, yInc;


	switch (pattern)
	{
	case DEMO_PATTERN_0:

		xInt = width / 4; //Four intervals, each with width/4 pixels
		xLeft = xInt * 3;
		xMid = xInt * 2 * 3;
		xRight = xInt * 3 * 3;
		xInc = 256.0 / ((double) xInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		yInt = height / 2; //Two intervals, each with width/2 lines
		yMid = yInt;
		yInc = 256.0 / ((double) yInt); //256 color intensities are cycled through per interval (overflow must be caught when color=256.0)

		fBlue = 0.0;
		fRed = 256.0;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{
			/*
			 * Convert color intensities to integers < 256, and trim values >=256
			 */
			wRed = (fRed >= 256.0) ? 255 : ((u8) fRed);
			wBlue = (fBlue >= 256.0) ? 255 : ((u8) fBlue);
			iPixelAddr = xcoi;
			fGreen = 0.0;
			for(ycoi = 0; ycoi < height; ycoi++)
			{

				wGreen = (fGreen >= 256.0) ? 255 : ((u8) fGreen);
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;
				if (ycoi < yMid)
				{
					fGreen += yInc;
				}
				else
				{
					fGreen -= yInc;
				}

				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				iPixelAddr += stride;
			}

			if (xcoi < xLeft)
			{
				fBlue = 0.0;
				fRed -= xInc;
			}
			else if (xcoi < xMid)
			{
				fBlue += xInc;
				fRed += xInc;
			}
			else if (xcoi < xRight)
			{
				fBlue -= xInc;
				fRed -= xInc;
			}
			else
			{
				fBlue += xInc;
				fRed = 0;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		Xil_DCacheFlushRange((unsigned int) frame, DEMO_MAX_FRAME);
		break;
	case DEMO_PATTERN_1:

		xInt = width / 7; //Seven intervals, each with width/7 pixels
		xInc = 256.0 / ((double) xInt); //256 color intensities per interval. Notice that overflow is handled for this pattern.

		fColor = 0.0;
		wCurrentInt = 1;
		for(xcoi = 0; xcoi < (width*3); xcoi+=3)
		{

			/*
			 * Just draw white in the last partial interval (when width is not divisible by 7)
			 */
			if (wCurrentInt > 7)
			{
				wRed = 255;
				wBlue = 255;
				wGreen = 255;
			}
			else
			{
				if (wCurrentInt & 0b001)
					wRed = (u8) fColor;
				else
					wRed = 0;

				if (wCurrentInt & 0b010)
					wBlue = (u8) fColor;
				else
					wBlue = 0;

				if (wCurrentInt & 0b100)
					wGreen = (u8) fColor;
				else
					wGreen = 0;
			}

			iPixelAddr = xcoi;

			for(ycoi = 0; ycoi < height; ycoi++)
			{
				frame[iPixelAddr] = wRed;
				frame[iPixelAddr + 1] = wBlue;
				frame[iPixelAddr + 2] = wGreen;
				/*
				 * This pattern is printed one vertical line at a time, so the address must be incremented
				 * by the stride instead of just 1.
				 */
				iPixelAddr += stride;
			}

			fColor += xInc;
			if (fColor >= 256.0)
			{
				fColor = 0.0;
				wCurrentInt++;
			}
		}
		/*
		 * Flush the framebuffer memory range to ensure changes are written to the
		 * actual memory, and therefore accessible by the VDMA.
		 */
		Xil_DCacheFlushRange((unsigned int) frame, DEMO_MAX_FRAME);
		break;
	default :
		xil_printf("Error: invalid pattern passed to DemoPrintTest");
	}
}

void DemoISR(void *callBackRef, void *pVideo)
{
	char *data = (char *) callBackRef;
	*data = 1; //set fRefresh to 1
}

void DemoDiagnostics()
{
	xil_printf("\r\n");
	xil_printf("============ SYSTEM DIAGNOSTICS ============\r\n");

	/* --- Display Controller --- */
	xil_printf("-- Display Controller --\r\n");
	xil_printf("  State       : %s\r\n",
			(dispCtrl.state == DISPLAY_RUNNING) ? "RUNNING" : "STOPPED");
	xil_printf("  Mode        : %s\r\n", dispCtrl.vMode.label);
	xil_printf("  H/V Active  : %dx%d\r\n",
			dispCtrl.vMode.width, dispCtrl.vMode.height);
	xil_printf("  H Total     : %d  V Total  : %d\r\n",
			dispCtrl.vMode.hmax + 1, dispCtrl.vMode.vmax + 1);
	xil_printf("  HFrontPorch : %d  HSyncWidth: %d  HBackPorch: %d\r\n",
			dispCtrl.vMode.hps - dispCtrl.vMode.width,
			dispCtrl.vMode.hpe - dispCtrl.vMode.hps,
			(dispCtrl.vMode.hmax + 1) - dispCtrl.vMode.hpe);
	xil_printf("  VFrontPorch : %d  VSyncWidth: %d  VBackPorch: %d\r\n",
			dispCtrl.vMode.vps - dispCtrl.vMode.height,
			dispCtrl.vMode.vpe - dispCtrl.vMode.vps,
			(dispCtrl.vMode.vmax + 1) - dispCtrl.vMode.vpe);
	{ int _ai = (int)dispCtrl.pxlFreq;    int _af = (int)((dispCtrl.pxlFreq    - _ai) * 1000 + 0.5);
	  int _bi = (int)dispCtrl.vMode.freq; int _bf = (int)((dispCtrl.vMode.freq - _bi) * 1000 + 0.5);
	  xil_printf("  Pixel Clock : %d.%03d MHz (requested %d.%03d MHz)\r\n", _ai, _af, _bi, _bf); }
	xil_printf("  Frame Buf   : %d  Stride: %d bytes\r\n",
			dispCtrl.curFrame, dispCtrl.stride);

	/* --- Video Capture --- */
	xil_printf("-- Video Capture --\r\n");
	switch (videoCapt.state)
	{
	case VIDEO_DISCONNECTED: xil_printf("  State       : DISCONNECTED\r\n"); break;
	case VIDEO_PAUSED:       xil_printf("  State       : PAUSED (signal present)\r\n"); break;
	case VIDEO_STREAMING:    xil_printf("  State       : STREAMING\r\n"); break;
	default:                 xil_printf("  State       : UNKNOWN (%d)\r\n", videoCapt.state); break;
	}
	if (videoCapt.state != VIDEO_DISCONNECTED)
	{
		xil_printf("  Resolution  : %dx%d\r\n",
				videoCapt.timing.HActiveVideo, videoCapt.timing.VActiveVideo);
		xil_printf("  H Total     : %d  V Total  : %d\r\n",
				videoCapt.timing.HActiveVideo + videoCapt.timing.HFrontPorch +
				videoCapt.timing.HSyncWidth + videoCapt.timing.HBackPorch,
				videoCapt.timing.VActiveVideo + videoCapt.timing.V0FrontPorch +
				videoCapt.timing.V0SyncWidth + videoCapt.timing.V0BackPorch);
		xil_printf("  HFrontPorch : %d  HSyncWidth: %d  HBackPorch: %d\r\n",
				videoCapt.timing.HFrontPorch,
				videoCapt.timing.HSyncWidth,
				videoCapt.timing.HBackPorch);
		xil_printf("  VFrontPorch : %d  VSyncWidth: %d  VBackPorch: %d\r\n",
				videoCapt.timing.V0FrontPorch,
				videoCapt.timing.V0SyncWidth,
				videoCapt.timing.V0BackPorch);
	}
	else
	{
		xil_printf("  Resolution  : N/A\r\n");
	}
	xil_printf("  Frame Buf   : %d  Stride: %d bytes\r\n",
			videoCapt.curFrame, videoCapt.stride);
	xil_printf("  startOnDetect: %s\r\n",
			videoCapt.startOnDetect ? "ENABLED (auto-starts on signal)" : "DISABLED");

	/* --- Memory Layout --- */
	xil_printf("-- Frame Buffer Memory --\r\n");
	xil_printf("  Frame 0     : 0x%08X\r\n", (u32)pFrames[0]);
	xil_printf("  Frame 1     : 0x%08X\r\n", (u32)pFrames[1]);
	xil_printf("  Frame 2     : 0x%08X\r\n", (u32)pFrames[2]);
	xil_printf("  Frame size  : %d bytes (%d MB each)\r\n",
			DEMO_MAX_FRAME, DEMO_MAX_FRAME / (1024 * 1024));
	xil_printf("  Total       : %d bytes (%d MB)\r\n",
			DEMO_MAX_FRAME * DISPLAY_NUM_FRAMES,
			(DEMO_MAX_FRAME * DISPLAY_NUM_FRAMES) / (1024 * 1024));

	/* --- GPIO Hardware Registers --- */
	xil_printf("-- GPIO Registers (AXI_GPIO_VIDEO) --\r\n");
	{
		u32 hpd    = XGpio_DiscreteRead(&videoCapt.gpio, 1); /* CH1: HPD output */
		u32 locked = XGpio_DiscreteRead(&videoCapt.gpio, 2); /* CH2: DVI2RGB MMCM locked input */
		u32 gpioISR = XGpio_InterruptGetStatus(&videoCapt.gpio);  /* pending interrupt flags */
		u32 gpioIER = XGpio_InterruptGetEnabled(&videoCapt.gpio); /* interrupt enable mask */
		xil_printf("  HPD output  : %d  (should be 1 to tell source to transmit)\r\n", (int)hpd);
		xil_printf("  Locked input: %d  (1 = DVI2RGB MMCM locked to incoming HDMI clock)\r\n", (int)locked);
		xil_printf("  GPIO ISR    : 0x%02X  (0x2 = CH2 edge pending; clears on read in ISR)\r\n", (unsigned int)gpioISR);
		xil_printf("  GPIO IER    : 0x%02X  (0x2 = CH2 interrupt enabled)\r\n", (unsigned int)gpioIER);
		if (!hpd)
			xil_printf("  [WARN] HPD is LOW - source will not transmit!\r\n");
		if (!locked)
			xil_printf("  [WARN] Locked=0: DVI2RGB MMCM not locked (no valid HDMI clock on input)\r\n");
		else
			xil_printf("  [OK]   Locked=1 but state=DISCONNECTED: interrupt path may be broken\r\n");
	}

	/* --- GIC Interrupt Enable Status --- */
	xil_printf("-- GIC Interrupt Enable --\r\n");
	{
		/* GICD_ISENABLER: bit set = interrupt enabled at distributor */
		u32 gpioWord = XScuGic_DistReadReg(&intc,
				XSCUGIC_ENABLE_SET_OFFSET + ((VID_GPIO_IRPT_ID / 32U) * 4U));
		u32 vtcWord  = XScuGic_DistReadReg(&intc,
				XSCUGIC_ENABLE_SET_OFFSET + ((VID_VTC_IRPT_ID  / 32U) * 4U));
		u32 gpioEnabled = (gpioWord >> (VID_GPIO_IRPT_ID % 32U)) & 1U;
		u32 vtcEnabled  = (vtcWord  >> (VID_VTC_IRPT_ID  % 32U)) & 1U;
		xil_printf("  GPIO IRQ %d : %s (at GIC distributor)\r\n",
				VID_GPIO_IRPT_ID, gpioEnabled ? "ENABLED" : "DISABLED");
		xil_printf("  VTC  IRQ %d : %s (at GIC distributor)\r\n",
				VID_VTC_IRPT_ID,  vtcEnabled  ? "ENABLED" : "DISABLED");
		if (!gpioEnabled)
			xil_printf("  [WARN] GPIO interrupt disabled at GIC - no detection possible\r\n");
	}

	/* --- VDMA S2MM (write/capture) Status --- */
	xil_printf("-- VDMA S2MM (capture channel) --\r\n");
	{
		u32 s2mmStat = XAxiVdma_GetStatus(&vdma, XAXIVDMA_WRITE);
		xil_printf("  S2MM status : 0x%08X\r\n", (unsigned int)s2mmStat);
		xil_printf("  Running     : %s\r\n", (s2mmStat & 0x1) ? "no (halted)" : "yes");
		if (s2mmStat & XAXIVDMA_SR_ERR_ALL_MASK)
			xil_printf("  [WARN] VDMA S2MM error bits set: 0x%08X\r\n",
					(unsigned int)(s2mmStat & XAXIVDMA_SR_ERR_ALL_MASK));
	}

	/* --- Passthrough Check --- */
	xil_printf("-- Passthrough Check --\r\n");
	xil_printf("  Display buf : %d   Capture buf : %d   -> %s\r\n",
			dispCtrl.curFrame, videoCapt.curFrame,
			(dispCtrl.curFrame == videoCapt.curFrame)
				? "MATCH (correct for passthrough)"
				: "MISMATCH! Use keys 2 or 6 to align them");

	xil_printf("============================================\r\n");
	xil_printf("Press any key to return to menu...\r\n");
}


