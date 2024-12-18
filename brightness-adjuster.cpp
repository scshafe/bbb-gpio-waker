
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cassert>

#include <mutex>
#include <condition_variable>
#include <thread>

#include <chrono>

#define SCREEN_TIME 4  // screen will stay on for 120 seconds after motion ceases to be detected
#define DEVICE_PATH "/dev/pir_ms_device"

#define DAY_BRIGHTNESS_CMD "echo 100 > /sys/class/backlight/backlight/brightness"
#define NIGHT_BRIGHTNESS_CMD "echo 25 > /sys/class/backlight/backlight/brightness"
#define OFF_BRIGHTNESS_CMD "echo 0 > /sys/class/backlight/backlight/brightness"

#define SET_SCREEN_DAY system(DAY_BRIGHTNESS_CMD);
#define SET_SCREEN_NIGHT system(NIGHT_BRIGHTNESS_CMD);
#define SET_SCREEN_OFF system(OFF_BRIGHTNESS_CMD);


// 

std::ifstream ifms_fd;

// ~~~~~~~~ LOCKING vars ~~~~~~~~

std::mutex m;
std::condition_variable cv;
bool ready = false;
bool processed = false;

// ~~~~~~~~ MOTION vars ~~~~~~~~

bool motion_detected;             // records the state of the GPIO motion sensing
#define no_motion_detected !motion_detected

// ~~~~~~~~ SCREEN vars ~~~~~~~~

bool screen_on;
#define screen_off !screen_on

// ~~~~~~~~ TIMER vars ~~~~~~~~

typedef std::chrono::steady_clock::time_point timestamp;
// typedef std::chrono::seconds chron_seconds;
timestamp most_recent_timestamp;



bool time_expired();              // function will return true if it is time to turn off screen
std::chrono::seconds calc_sleep_time();
void set_new_off_time();



void turn_off_screen();
void turn_on_screen();

std::istream* get_motion_sensor_fd();


/*
    Two Threads:

    1: one that will sleep until the most recent_time + SCREEN_TIME has been reached

    2: main thread which will read
*/

void screen_sleeper_thread()
{
    while(true)
    {
        std::unique_lock<std::mutex> lk(m);
        if (motion_detected || screen_off)
        {
#ifdef DEBUG
            std::cout << "waiting on condition" << std::endl;
#endif
            cv.wait(lk, []{ return screen_on && no_motion_detected; });
        }

        if (!time_expired())
        {
            std::chrono::seconds sleep_time_duration = calc_sleep_time();
            lk.unlock();
#ifdef DEBUG
            std::cout << "starting sleep for: " << sleep_time_duration.count() << std::endl;
#endif
            std::this_thread::sleep_for(sleep_time_duration);
#ifdef DEBUG
            std::cout << "waking" << std::endl;
#endif
        }
        else
        {
#ifdef DEBUG
            std::cout << "turning off screen" << std::endl;
#endif
            turn_off_screen();
            lk.unlock();
        }
    }
}


int main()
{
    // start screen_sleeper_thread
    std::thread screen_sleeper_t(screen_sleeper_thread);
    turn_off_screen();
   
    std::istream *ms_fd = get_motion_sensor_fd();

    // enter main loop
    while (true)
    {
        char tmp = 'x';
        ms_fd->read(&tmp, 1);
#ifdef DEBUG
        std::cout << "val: " << tmp << std::endl;
#endif
        assert(tmp == '1' || tmp == '0');
        
        std::unique_lock<std::mutex> lk(m);
        if (tmp == '1')
        {
            motion_detected = true;
            turn_on_screen();
            lk.unlock();
        }
        else
        {
#ifdef DEBUG
            std::cout << "beginning screen shutoff timer" << std::endl;
#endif
            motion_detected = false;
            set_new_off_time();
            lk.unlock();
            cv.notify_all();
        }
#ifdef DEBUG
        ms_fd->read(&tmp, 1);   // must read in the newline here
#endif
        
        
    }
}


bool time_expired()
{
    return (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - most_recent_timestamp).count() > SCREEN_TIME - 1);
}

std::chrono::seconds calc_sleep_time()
{
    return std::chrono::seconds{SCREEN_TIME} - std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - most_recent_timestamp);
}

void set_new_off_time()
{
    most_recent_timestamp = std::chrono::steady_clock::now();
}



std::istream* get_motion_sensor_fd()
{
#ifdef DEBUG  // provide an ifstream where cin can provide states 1 at a time
    std::istream *ms_fd = &std::cin;
    ms_fd->rdbuf()->pubsetbuf(0, 0);
    return ms_fd;
#else
     // setup fd
    std::istream *ms_fd;
    // std::ifstream ifms_fd;
    ifms_fd.rdbuf()->pubsetbuf(0,0);
    ifms_fd.open(DEVICE_PATH, std::ios::binary);
    ms_fd = &ifms_fd;
    return ms_fd;
#endif
    
}

void turn_on_screen()
{
    // add night / day logic here
    SET_SCREEN_DAY;
    screen_on = true;

}

void turn_off_screen()
{
    SET_SCREEN_OFF;
    screen_on = false;
}