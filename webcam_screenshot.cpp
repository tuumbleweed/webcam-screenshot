#include <iostream>
#include <stdio.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <fstream>
#include <chrono>
#include <thread>

using namespace std;

char const *deviceName = "/dev/video0";
char const *screenshotDir = "./screenshots/";
int const delay = 2000;

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}

// Open the device, get file descriptor to the video device
const int getFileDescriptor(const char *deviceName) {
    int fileDescriptor;
    fileDescriptor = open(deviceName, O_RDWR);
    printf("Opening webcam device %s, fileDescriptor: %d\n", deviceName, fileDescriptor);
    if(fileDescriptor < 0){
        char buffer[100];
        sprintf(buffer, "Failed to open device %s", deviceName);
        perror(buffer);
        exit(1);
    }
    return fileDescriptor;
}

//  Ask the device if it can capture frames
void checkDeviceCapability(int fileDescriptor) {
    v4l2_capability capability;
    if(ioctl(fileDescriptor, VIDIOC_QUERYCAP, &capability) < 0){
        // something went wrong... exit
        perror("Failed to get device capabilities, VIDIOC_QUERYCAP");
        exit(1);
    }
}

// Set Image format
void setImageFormat(int width, int height, int fileDescriptor) {
    v4l2_format imageFormat;
    imageFormat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    imageFormat.fmt.pix.width = width;
    imageFormat.fmt.pix.height = height;
    imageFormat.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    imageFormat.fmt.pix.field = V4L2_FIELD_NONE;
    // tell the device you are using this format
    if(ioctl(fileDescriptor, VIDIOC_S_FMT, &imageFormat) < 0){
        perror("Device could not set format, VIDIOC_S_FMT");
        exit(1);
    }
}

// Request Buffers from the device
void requestBuffersFromDevice(int fileDescriptor) {
    v4l2_requestbuffers requestBuffer = {0};
    requestBuffer.count = 1; // one request buffer
    requestBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // request a buffer wich we an use for capturing frames
    requestBuffer.memory = V4L2_MEMORY_MMAP;

    if(ioctl(fileDescriptor, VIDIOC_REQBUFS, &requestBuffer) < 0){
        perror("Could not request buffer from device, VIDIOC_REQBUFS");
        exit(1);
    }
}

// Query the buffer to get raw data ie. ask for the your requested buffer
// and allocate memory for it
std::pair<v4l2_buffer, char*> queryBuffer(int fileDescriptor) {
    v4l2_buffer queryBuffer = {0};
    queryBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    queryBuffer.memory = V4L2_MEMORY_MMAP;
    queryBuffer.index = 0;
    if(ioctl(fileDescriptor, VIDIOC_QUERYBUF, &queryBuffer) < 0){
        perror("Device did not return the buffer information, VIDIOC_QUERYBUF");
        exit(1);
    }
    // use a pointer to point to the newly created buffer
    // mmap() will map the memory address of the device to
    // an address in memory
    char* buffer = (char*)mmap(NULL, queryBuffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fileDescriptor, queryBuffer.m.offset);
    memset(buffer, 0, queryBuffer.length);
    return std::make_pair(queryBuffer, buffer);
}

void activateStreaming(int fileDescriptor, v4l2_buffer bufferInfo) {
    // Activate streaming
    // int type = bufferInfo.type; // why?
    if(ioctl(fileDescriptor, VIDIOC_STREAMON, &bufferInfo.type) < 0){
        perror("Could not start streaming, VIDIOC_STREAMON");
        exit(1);
    }
    // Give the device time to get ready
    this_thread::sleep_for(chrono::milliseconds(50));
}

void queueDequeue(int fileDescriptor, v4l2_buffer* bufferInfo) {
    // Queue the buffer
    if(ioctl(fileDescriptor, VIDIOC_QBUF, bufferInfo) < 0){
        perror("Could not queue buffer, VIDIOC_QBUF");
        exit(1);
    }
    // Dequeue the buffer
    if(ioctl(fileDescriptor, VIDIOC_DQBUF, bufferInfo) < 0){
        perror("Could not dequeue the buffer, VIDIOC_DQBUF");
        exit(1);
    }
    // // Frames get written after dequeuing the buffer
    // cout << "Buffer has: " << (double)bufferInfo->bytesused / 1024
    //         << " KBytes of data" << endl;
}

// save data to filePath
void saveFile(string filePath, char* buffer, v4l2_buffer bufferInfo) {
    printf("Saving file %s (%f KBytes)\n", filePath.c_str(), (double)bufferInfo.bytesused / 1024);
    // Write the data out to file
    ofstream outFile;
    outFile.open(filePath, ios::binary| ios::app);
    // save all at once
    outFile.write(buffer, (double)bufferInfo.bytesused);
    // Close the file
    outFile.close();
}

// end streaming
void endStreaming(int fileDescriptor, v4l2_buffer bufferInfo) {
    if(ioctl(fileDescriptor, VIDIOC_STREAMOFF, &bufferInfo.type) < 0){
        perror("Could not end streaming, VIDIOC_STREAMOFF");
        exit(1);
    }
}

int main() {
    int fileDescriptor = getFileDescriptor(deviceName);
    checkDeviceCapability(fileDescriptor);
    setImageFormat(1280, 720, fileDescriptor);
    requestBuffersFromDevice(fileDescriptor);
    v4l2_buffer bufferInfo; char* buffer; std::tie(bufferInfo, buffer) = queryBuffer(fileDescriptor);
    activateStreaming(fileDescriptor, bufferInfo);

    while (true) {
        queueDequeue(fileDescriptor, &bufferInfo);
        saveFile(screenshotDir + currentDateTime() + ".jpeg", buffer, bufferInfo);
        this_thread::sleep_for(chrono::milliseconds(delay));
    }

    endStreaming(fileDescriptor, bufferInfo);
    close(fileDescriptor);
    return 0;
}
