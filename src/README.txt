This project demonstrates how to use the USB-UART Bridge, HDMI Sink and HDMI Source with the ZYNQ processor. Vivado is used to build the demo's hardware platform, and Xilinx SDK is used to program the bitstream onto the board and to build and deploy a C application. Video data streams in through the HDMI in port and out through the HDMI out port. A UART interface is available to configure what is output through HDMI. There are 3 display frame buffers that the user can choose to display or write to. The configuring options are shown in the table below.

The demo uses the usb-uart bridge to configure the HDMI Display , the Arty Z7-20 must be connected to a computer over MicroUSB, which must be running a serial terminal. For more information on how to set up and use a serial terminal, such as Tera Term or PuTTY, refer to this tutorial.

Option	Function
1	Change the resolution of the HDMI output to the monitor.
2	Change the frame buffer to display on the HDMI monitor.
3/4	Store a test pattern in the chosen video frame buffer - color bar or blended.
5	Start/Stop streaming video data from HDMI to the chosen video frame buffer.
6	Change the video frame buffer that HDMI data is streamed into.
7	Invert and store the current video frame into the next video frame buffer and display it.
8	Scale the current video frame to the display resolution, store it into the next video frame buffer, and then display it.