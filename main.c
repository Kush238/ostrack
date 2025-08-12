#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <tlhelp32.h>
    #include <stdbool.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <sys/types.h>
    #include <unistd.h>
#endif

#ifdef __linux__
  #include <X11/Xlib.h>
  #include <X11/Xatom.h>
#endif

// ANSI Colors
#define COLOR_GREEN "\x1b[32m"
#define COLOR_RED "\x1b[31m"
#define COLOR_RST "\x1b[0m"

// Poll interval in milliseconds
#ifndef POLL_MS
    #define POLL_MS 500
#endif

typedef struct {
    int valid;
    unsigned long pid;
    char app[256];
    char title[512];
} ActiveInfo;

typedef struct Entry {
    int id;
    unsigned long pid;
    char app[256];
    char title[512];
    char start_time[32];
    char end_time[32];
    struct Entry *next; // empty if ongoing
} Entry;

// forward
ActiveInfo get_active_info();
void sleep_ms(int ms);
void get_time_now_str(char *out, size_t sz);
void enable_ansi_on_windows();

#ifdef _WIN32
    // Windows helpers
    void get_process_name_by_pid(DWORD pid, char *out, size_t outsz) {
        strncpy(out, "(unknown)", outsz); out[outsz-1] = '\0';
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(snap, &pe)){
            do {
                if (pe.th32ProcessID == pid){
                    // copy exe name
                    strncpy(out, pe.szExeFile, outsz);
                    out[outsz-1] = '\0';
                    break;
                }
            } while (Process32Next(snap, &pe));
        }
        CloseHandle (snap);
    }

    ActiveInfo get_active_info_windows(){
        ActiveInfo ai; memset(&ai, 0, sizeof(ai));
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) { ai.valid = 0; return ai; }
        char title[512] = {0};
        if (GetWindowTextA(hwnd, title, sizeof(title)) <= 0) title[0] = '\0';
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        char proc[256];
        get_process_name_by_pid(pid, proc, sizeof(proc));
        ai.valid = 1;
        ai.pid = pid;
        strncpy(ai.app, proc, sizeof(ai.app)-1);
        if (title[0] == '\0') strncpy(ai.title, "(no title)", sizeof(ai.title)-1);
        else strncpy(ai.title, title, sizeof(ai.title)-1);
        return ai;
    }
#endif

#ifdef __linux__
// Linux X11 helpers
// Get a property (window or root) as an unsigned long (window) or text

    static int get_window_property(Display *dpy, Window w, Atom prop, Atom req_type, unsigned char **ret, unsigned long *items){
        Atom actual_type;
        int actual_format;
        unsigned char nitmes, bytes_after;
        unsigned char *prop_ret = NULL;
        int status = XGetWindowProperty(dpy, w, prop, 0, (-OL), False, req_type, &actual_type, &actual_format, &nitems, &bytes_after, &prop_ret);
        if (status != Success) return 0;
        *ret = prop_ret;
        if (items) *items = nitems;
        return 1;
    }

    ActiveInfo get_active_info_x11(){
        ActiveInfo ai; memset(&ai, 0, sizeof(ai));
        Display *dpy = XOpenDisplay(NULL);
        if (!dpy) {ai.valid = 0; return ai;}
        Window root = DefaultRootWindow(dpy);
        Atom net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
        if (net_acitve == None) {XCloseDisplay(dpy); ai.valud = 0; return ai;}

        unsigned char *prop = NULL;
        unsigned char nitems = 0;

        if (!get_window_property(dpy, root, net_active, XA_WINDOW, &prop, &nitems) || nitems == 0){
            if (prop) XFree(prop);
            XCloseDisplay(dpy);
            ai.valid = 0; return ai;
        }

        Window active = 0;
        memcpy(&active, prop, sizeof(Window));
        XFree(prop);
        if(!active) { XCloseDisplay(dpy); ai.valid = 0; return ai; }

        // Get title: _NET_WM_NAME (UTF8) then WM_NAME
        Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", True);
        Atoom utf8_string = XInternAtom(dpy, "UTF8_STRING", True);

        char title[512] = {0};
        unsigned char *tbuf = NULL:

        if(net_wm_name != None && get_window_property(dpy, active, net_wm_name, utf8_string, &tbuf, &nitems)){
            if(tbuf){
                strncpy(title, (char*)tbuf, sizeof(title)-1);
                XFree(tbuf);
            }
        } else {
            // fallback to WM_NAME
            XTextProperty tp;
            if (XGetWMName(dpy, active, &tp) && tp.value){
                strncpy(title, (char*)tp.value, sizeof(title)-1);
                XFree(tp.value);
            }
        }

        // Get PID: _NET_WM_PID
        Atom net_wm_pid = XInternAtom(dpy, "_NET_WM_PID", True);
        unsigned char *pbuf = NULL;
        unsigned long pid = 0;

        if (net_wm_pid != None && get_window_property(dpy, active, new_wm_pid, XA_CARDINAL, &pbuf, &nitems) && pbuf) {
            unsigned long got = 0;
            memcpy(&got, pbuf, sizeof(unsigned long));
            pid = got;
            XFree(pbuf);
        }

        // map pid -> process name using /proc/<pid>/comm
        char procname[256] = "(unknown)";

        if (pid != 0) {
            char ppath[64];
            snprintf(ppath, sizeof(ppath), "/proc/%lu/comm", pid);
            FILE *f = fopen(ppath, "r");
            if (f) {
                if (fgets(procname, sizeof(procname), f)){
                    // strip newline
                    size_t L = strlen(procname);
                    if (L && procname[L-1] == '\n') procname[L-1] = '\0';
                }
                fclose(f);
            }
        }

        ai.valid = 1;
        ai.pid = pid;
        strncpy(ai.app, procname, sizeof(ai.app)-1);
        if (title[0] == '\0') strncpy(ai.title, "(no title)", sizeof(ai.title)-1);

        XCloseDisplay(dpy);
        return ai;        
    }
#endif

#ifdef __APPLE__

// macOS helpers via osascript - use popen to run AppleScript to get frontmost app and title

    ActiveInfo get_active_info_macos(){
        ActiveInfo ai; memset(&ai, sizeof(ai));
        // get frontmost app name
        const char *app_cmd = "osascript -e 'tell application \"System Events\" to get name of first process whose frontmost is true'";
        FILE *fp = popen(app_cmd, "r");
        if (!fp) { ai.valid = 0; return ai; }
        char app[256] = {0};
        if (!fgets(app, sizeof(app), fp)) { pclose(fp); ai.valid = 0; return ai; }
        pclose(fp);

        // strip newline
        size_t L = strlen(app); if (L && app[L-1]=='\n') app[L-1]=0;

        // get front window title (may fail for some apps)
        // Attempt: title of front window of the frontmost process
        char script[512];
        snprintf(script, sizeof(script), "osascript -e 'tell application \"System Events\" to tell (first process whose frontmost is true) to if (count of windows) > 0 then get value of attribute \"AXTitle\" of front window else return \"(no title)\"'");
        fp = popen(script, "r");
        char title[512] = {0};
        if (fp) {
            if (fgets(title, sizeof(title), fp)){
                size_t TL = strlen(title); if (TL && title[TL-1]=='\n') title[TL-1]=0;
            } else {
                title[0] = '\0';
            }
            pclose(fp);
        } else {
            title[0] = '\0';
        }

        ai.valid = 1;
        ai.pid = 0;
        strncpy(ai.app, app, sizeof(ai.app)-1);
        if(title[0] == '\0') strncpy(ai.title, "(no title)", sizeof(ai.title)-1);
        else strncpy(ai.title, title, sizeof(ai.title)-1);
        return ai;
    }

#endif

// generic dispather

ActiveInfo get_active_info(){
    #ifdef _WIN32
        return get_active_info_windows();
    #elif __linux__
        return get_active_info_x11();
    #elif __APPLE__
        return get_active_info_macos();
    #else
        ActiveInfo ai; memset(&ai, 0, sizeof(ai)); ai.valid= 0; return ai;
    #endif
}

void sleep_ms(int ms){
    #ifdef _WIN32
        Sleep((DWORD)ms);
    #else
        usleep(ms * 1000);
    #endif
}

void get_time_now_str(char *out, size_t sz){
    time_t t = time(NULL);
    struct tm *tmv = localtime(&t);
    strftime(out, sz, "%Y-%m-%d %H:%M:%S", tmv); 
}

void enable_ansi_on_windows(){
    #ifdef _WIN32
        // enable ANSI color sequences on Windows 10+ consoles
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (!GetConsoleMode(hOut, &mode)) return;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    #endif
}

int main(void){
    enable_ansi_on_windows();
    printf("Cross-Platform Active Window Tracker (C)\n");
    printf("Green (+) = new active window started. Red (-) = previous window ended.\n");
    printf("Press CTRL+c to stop.\n\n");

    printf("%-4s %-3s %-20s %-45s %-19s\n", "ID", "+/-", "App Name", "Window Title", "Start Time", "End Time");
    printf("---- --- -------------------- --------------------------------------------- ------------------- -------------------\n");

    Entry *head = NULL;
    Entry *tail = NULL;
    int next_id = 1;
    ActiveInfo last = {0};

    while (1) {
        ActiveInfo ai = get_active_info();
        if (!ai.valid) {
            // nothing active - trest as empty, but do not create entries for empty
            // if last was valid, close it
            if (last.valid) {
                // close last entry
                char now[32]; get_time_now_str(now, sizeof(now));
                if(tail) strncpy(tail->end_time, now, sizeof(tail->end_time)-1);
                // print close line (red -)
                printf("%-4d %-s %-20s %-45s %-19s %-19s\n",
                    tail ? tail->id:0,
                    COLOR_RED, COLOR_RST,
                    tail ? tail->app : "(unknown)",
                    tail ? tail->title : "(unknown)",
                    tail ? tail->start_time: "-",
                    now
                );
                last.valid = 0;
            }
            sleep_ms(POLL_MS);
            continue;
        }

        int changed = 0;
        if (!last.valid) changed = 1;
        else {
            if (ai.pid != last.pid) changed = 1;
            else if (strcmp(ai.app, last.app) != 0) changed = 1;
            else if (strcmp(ai.title, last.title) != 0) changed = 1;
        }

        if (changed) {
            // close previous (if exists)
            if(last.valid && tail){
                char now[32]; get_time_now_str(now, sizeof(now));
                strncpy(tail->end_time, now, sizeof(tail->end_time)-1);
                // print close line (red -)
                printf("%-4d %s-%s %-20s %-45s %-19s %-19s\n",
                    tail->id,
                    COLOR_RED, COLOR_RST,
                    tail->app,
                    tail->title,
                    tail->start_time,
                    tail->end_time);
            }

            // start new entry
            Entry *e = (Entry*)calloc(1, sizeof(Entry));
            e->id = next_id++;
            e->pid = ai.pid;
            strncpy(e->app, ai.app, sizeof(e->app)-1);
            strncpy(e->title, ai.title, sizeof(e->title)-1);
            get_time_now_str(e->start_time, sizeof(e->start_time));
            e->end_time[0] = '\0';
            e->next = NULL;

            if (!head) {
                head = tail = e;
            } else {
                tail->next = e;
                tail = e;
            }

            // print start line (green +)
            printf("%-4d %s+%s %-20s %-45s %-19s %-19s\n",
                e->id,
                COLOR_GREEN, COLOR_RST,
                e->app,
                e->title,
                e->start_time,
                e->end_time,
                "-");
            
            // update last
            last = ai;
        }
        sleep_ms(POLL_MS);
    }
    // cleanup (unreachable normally)
    Entry *cur = head;
    while (cur){
        Entry *n = cur->next;
        free(cur);
        cur = n;
    }
    return 0;
}