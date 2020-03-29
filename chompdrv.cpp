#include <iostream>
#include <libusb.h>
#include <linux/uinput.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <signal.h>

#define VID             0x9A7A
#define PID             0xBA17
#define X_AXIS_BIT_1    2
#define X_AXIS_BIT_2    3
#define Y_AXIS_BIT_1    0
#define Y_AXIS_BIT_2    1
#define JS_BUTTON_BIT   4
#define X_AXIS_LEFT     -32767
#define X_AXIS_RIGHT    32767
#define Y_AXIS_UP       -32767
#define Y_AXIS_DOWN     32767
#define AXIS_STATIONARY 0
#define AXIS_INVALID    -1
#define USB_ENDPOINT    0x81

using namespace std;

//global variable for main loop. Needs to be accessed by the signal handler
bool running = true;

void emit(int fd, int type, int code, int val) {
    struct input_event ie;
    ie.type = type;
    ie.code = code;
    ie.value = val;
    ie.time.tv_sec = 0;
    ie.time.tv_usec = 0;
    write(fd, &ie, sizeof(ie));
}

int read_bit(char raw_input, int bit_position) {
    return (raw_input & (1 << bit_position)) >> bit_position;
}

int interpret_x_axis(char raw_input) {
    int bit_1 = read_bit(raw_input, X_AXIS_BIT_1);
    int bit_2 = read_bit(raw_input, X_AXIS_BIT_2);

    if (bit_1 == 1 && bit_2 == 1) 
        return X_AXIS_RIGHT;
    if (bit_1 == 1 && bit_2 == 0) 
        return X_AXIS_LEFT;
    if (bit_1 == 0 && bit_2 == 1) 
        return AXIS_STATIONARY;
    return AXIS_INVALID;
}

int interpret_y_axis(char raw_input) {
    int bit_1 = read_bit(raw_input, Y_AXIS_BIT_1);
    int bit_2 = read_bit(raw_input, Y_AXIS_BIT_2);

    if (bit_1 == 1 && bit_2 == 1) 
        return Y_AXIS_UP;
    if (bit_1 == 1 && bit_2 == 0) 
        return Y_AXIS_DOWN;
    if (bit_1 == 0 && bit_2 == 1) 
        return AXIS_STATIONARY;
    return AXIS_INVALID;
}

int interpret_button(char raw_input) { return read_bit(raw_input, JS_BUTTON_BIT); }

int get_device_handle(libusb_device_handle *handle, libusb_context *context, int product_id, int vendor_id) {
    libusb_device **devs;
    int r; //for return values
	ssize_t cnt; //holding number of devices in list
	r = libusb_init(&(context)); //initialize the library for the session we just declared
	if(r < 0) {
		cout<<"Init Error "<<r<<endl; //there was an error
		return 1;
	}

    libusb_set_debug(context, 3);
    cnt = libusb_get_device_list(context, &devs);
    if(cnt < 0) {
		cout<<"Get Device Error"<<endl; //there was an error
		return 1;
	}
	cout<<cnt<<" Devices in list."<<endl;
    handle = libusb_open_device_with_vid_pid(context, VID, PID);
    if (handle == NULL) {
        cout << "Error opening the device\n";
    }
}

void create_virtual_js(int fd, int product_id, int vendor_id, string name) {
    struct uinput_setup usetup;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_JOYSTICK);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = vendor_id;
    usetup.id.product = product_id;
    strcpy(usetup.name, name.c_str());

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    sleep(1);
}

void destroy_virtual_js(int fd) {
    ioctl(fd, UI_DEV_DESTROY);
}

void end_libusb_session(libusb_context *context, libusb_device_handle *handle) {
    libusb_release_interface(handle, 0);
    libusb_exit(context);
}

void cleanup_resources(int fd, libusb_context *context, libusb_device_handle *handle) {
    destroy_virtual_js(fd);
    libusb_release_interface(handle, 0);
    libusb_exit(context);
    close(fd);
}

void send_js_events(char data, int fd) {
    emit(fd, EV_ABS, ABS_X, interpret_x_axis(data));
    emit(fd, EV_ABS, ABS_Y, interpret_y_axis(data));
    emit(fd, EV_KEY, BTN_JOYSTICK, interpret_button(data));
    emit(fd, EV_SYN, SYN_REPORT, 0);  
}

void signal_handler(int sig) {
    cout << "\nCtrl-c caught. Cleaning up resources now\n";
    running = false;
}

struct chompdrv_t {
    unsigned char raw_byte;
    int uinput_fd;
    libusb_device_handle *dev_handle;
    libusb_context *context;
    string name = "Chomp Driver";
    struct sigaction sig_handler;
    int transferred;
} chomp_driver;

void setup_signal_handler(struct sigaction *sig_handler) {
    sig_handler->sa_handler = signal_handler;
    sigemptyset(&(sig_handler->sa_mask));
    sig_handler->sa_flags = 0;
    sigaction(SIGINT, sig_handler, NULL);
}

int main(int argc, char *argv[]) {
    setup_signal_handler(&(chomp_driver.sig_handler));
    cout << "Setup signal handler\n";

    chomp_driver.uinput_fd = open("/dev/uinput", O_WRONLY);  
    if (chomp_driver.uinput_fd == -1) {
        cout << "Error opening the file.\n";
        return 1;
    } else {
        cout << "/dev/input opened successfully\n";
    }

    libusb_device **devs;
    int r; //for return values
	ssize_t cnt; //holding number of devices in list
	r = libusb_init(&(chomp_driver.context)); //initialize the library for the session we just declared
	if(r < 0) {
		cout<<"Init Error "<<r<<endl; //there was an error
		return 1;
	}

    libusb_set_debug(chomp_driver.context, 3);
    cnt = libusb_get_device_list(chomp_driver.context, &devs);
    if(cnt < 0) {
		cout<<"Get Device Error"<<endl; //there was an error
		return 1;
	}
	cout<<cnt<<" usb devices discovered"<<endl;
    chomp_driver.dev_handle = libusb_open_device_with_vid_pid(chomp_driver.context, VID, PID);
    if (chomp_driver.dev_handle == NULL) {
        cout << "Error opening the device\nIs the virtual joystick program running?\n";
        return 1;
    } else {
        cout << "Device handle successfully obtained with product_id: " << std::hex << PID << " and vendor_id: " << std::hex << VID << endl; 
    }

    create_virtual_js(chomp_driver.uinput_fd, PID, VID, chomp_driver.name);
    cout << "Virtual joystick successfully created. Named: \"" << chomp_driver.name << "\"" << endl;

    cout << "Driver running successfully\n";
    while(running) {
        libusb_interrupt_transfer(chomp_driver.dev_handle, USB_ENDPOINT, &chomp_driver.raw_byte, sizeof(chomp_driver.raw_byte), &(chomp_driver.transferred), 0);
        send_js_events(chomp_driver.raw_byte, chomp_driver.uinput_fd);
    }

    cleanup_resources(chomp_driver.uinput_fd, chomp_driver.context, chomp_driver.dev_handle);
    return true;
}