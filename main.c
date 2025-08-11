#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <conio.h>
#include <stdbool.h>

#define MAX_TITLE 512
#define MAX_PROC 260
#define MAX_ENTRIES 20000
#define POLL_MS 500

typedef struct {
    int id;
    char app[MAX_PROC];
    char title[MAX_TITLE];
    time_t start_time;
    time_t end_time; // o means still open
} Entry;

static Entry entries[MAX_ENTRIES];
static int entry_count = 0;

void format_time(time_t t, char *out, size_t outsz){
    if (t == 0) {
        strncpy(out, "-", outsz); out[outsz-1] = '\0';
        return;
    }
    struct tm tmv;
    struct tm *lt = localtime(&t);
    if (!lt) {strncpy(out, "??", outsz); out[outsz-1] = '\0'; return; }
    tmv = *lt;
    strftime(out, outsz, "%Y-%m-%d %H:%M:%S", &tmv);
}

// get process exe filename by pid (e.g., notepad.exe). If not found, returns "Unknown".

void get_process_name_by_pid(DWORD pid, char *out, size_t outsz) {
    strncpy(out, "Unknown", outsz); out[outsz-1] = '\0';
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)){
        do {
            if (pe.th32ProcessID == pid){
                strncpy(out, pe.szExeFile, outsz);
                out[outsz-1] - '\0';
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
}

void print_table(){
    system("cls");
    printf("Tracking usage - press 'q' to stop\n\n");
    printf("%-4s %-20s %-45s %-19s %-19s\n", "ID", "App Name", "Window Title", "Start Time", "End Time");
    printf("---- -------------------- --------------------------------------------- ------------------- -------------------\n");
    for (int i = 0; i < entry_count; ++i){
        char start_s[64], end_s[64];
        format_time(entries[i].start_time, start_s, sizeof(start_s));
        format_time(entries[i].end_time, end_s, sizeof(end_s));

        // Truncate long titles nicely for display
        char title_shown[MAX_TITLE];
        if (strlen(entries[i].title) > 44){
            strncpy(title_shown, entries[i].title, 41);
            title_shown[41] = '\0';
            strcat(title_shown, "...");
        } else {
            strncpy(title_shown, entries[i].title, sizeof(title_shown));
            title_shown[sizeof(title_shown)-1] = '\0';
        }

        
        // app name also truncated
        char app_shown[32];
        if (strlen(entries[i].app) > 20) {
            strncpy(app_shown, entries[i].app, 17); app_shown[17] = '\0'; strcat(app_shown, "...");
        } else strncpy(app_shown, entries[i].app, sizeof(app_shown));

        printf("%-4d %-20s %-45s %-19s %-19s\n",
            entries[i].id,
            app_shown,
            title_shown,
            start_s,
            end_s
        );
    }
}

int main(void){
    printf("Windows Foreground Usage Tracker (Console)\n");
    printf("Press ENTER to start tracking. When tracking, press 'q' to stop.\n");
    printf("Note: This records when the foregound window or its title chanegs.\n\n");
    printf("Press ENTER to start... ");

    fflush(stdout);
    // wait for ENTER
    int c = getchar();
    (void)c;

    HWND last_hwnd = NULL;
    char last_title[MAX_TITLE] = "";
    DWORD last_pid = 0;

    printf("Started tracking. Press 'q' to stop.\n");
    // small initial draw
    print_table();

    while (1){
        // check user input (non-blocking)
        if(_kbhit()){
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                // finish last open entry
                if (entry_count > 0 && entries[entry_count-1].end_time == 0){
                    entries[entry_count-1].end_time = time(NULL);
                }
                print_table();
                printf("\nStopped tracking. Press any key to exit...\n");
                _getch();
                break;
            }
            // ignore other keys
        }

        HWND hwnd = GetForegroundWindow();
        if (hwnd != NULL){
            // get window title
            char title[MAX_TITLE] = "";
            if (GetWindowTextA(hwnd, title, MAX_TITLE) <= 0) {
                // no title text
                title[0] = '\0';
            }

            // get process id
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);

            // consider a "change" when hwnd changes OR title changes
            bool changed = false;
            if (last_hwnd == NULL && hwnd != NULL) changed = true;
            else if (last_hwnd != hwnd) changed = true;
            else if (strcmp(last_title, title) != 0) changed = true;

            if (changed){
                time_t now = time(NULL);

                // close previous entry
                if (entry_count > 0 && entries[entry_count-1].end_time == 0){
                    entries[entry_count-1].end_time = now;
                }

                // get process name
                char proc[MAX_PROC];
                get_process_name_by_pid(pid, proc, sizeof(proc));

                // create new entry (only if title not empty or we still want to capture)
                Entry e;
                e.id = entry_count + 1;
                strncpy(e.app, proc, sizeof(e.app));
                e.app[sizeof(e.app)-1] = '\0';
                if (title[0] == '\0') strncpy(e.title, "(no title)", sizeof(e.title));
                else strncpy(e.title, title, sizeof(e.title));
                e.title[sizeof(e.title)-1] = '\0';
                e.start_time = now;
                e.end_time = 0;

                if (entry_count < MAX_ENTRIES) {
                    entries[entry_count++] = e;
                } else {
                    // drop oldest if full
                    memmove(entries, entries+1, sizeof(Entry)*(MAX_ENTRIES-1));
                    entries[MAX_ENTRIES-1] = e;
                }

                // update last knowns
                last_hwnd = hwnd;
                strncpy(last_title, title, sizeof(last_title));
                last_title[sizeof(last_title)-1] = '\0';
                last_pid = pid;

                // redraw table
                print_table();
            }
        }
        Sleep(POLL_MS);
    }
    return 0;
}