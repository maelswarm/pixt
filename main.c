//
//  main.c
//  
//
//  Created by fairy-slipper on 1/13/18.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <limits.h>
#include <ctype.h>

#define INIT 1
#define UPDATE 2

#define ANSI_COLOR_NORMAL  "\x1B[0m"
#define ANSI_COLOR_RED  "\x1B[31m"
#define ANSI_COLOR_GREEN   "\x1B[32m"
#define ANSI_COLOR_YELLOW  "\x1B[33m"
#define ANSI_COLOR_BLUE  "\x1B[34m"
#define ANSI_COLOR_PURPLE  "\x1B[35m"
#define ANSI_COLOR_CYAN  "\x1B[36m"
#define ANSI_COLOR_WHITE  "\x1B[37m"

#define ANSI_BLINK   "\x1b[5m"
#define ANSI_HIDE_CURSOR "\e[?25l"
#define ANSI_SHOW_CURSOR "\e[?25h"

const int MAX_CONSOLE_TEXT = 10000;
static int FILE_SIZE = 1000;

static volatile int appRunning = 1;
static struct winsize winsize;
static int cooked = 0;
static int editing = 0;
static int savePrompt = 0;
static int readyToRender = 1;

static char enteredChar = 0;
static char *consoleText;
static int cursorPosition = 2;

static int editingCursorPositionX = 1;
static int editingCursorPositionY = 1;

static FILE *fp = NULL;
static int fpOffset = 0;
static FILE *fpClone = NULL;

static char *newFileString;
static int newFileStrOffset = -1;
static int editingPageOffset;
static int currEditingPageOffset = 0;

struct dirent *pDirent;
struct dirent *selectedDirent;
static DIR *pDir;
static char cwd[PATH_MAX];
static char *currCloneFilePath;
static int directoryWidth = 0;
static int directoryHeight = 0;
static int contentCount;
static int numOfPagesNav = 0;
static int currPage = 0;

void appendChars(char subject[], const char insert[], int pos) {
    if(strlen(insert)+strlen(subject) > FILE_SIZE) {
        FILE_SIZE *= 2;
        newFileString = (char *)realloc(newFileString, FILE_SIZE);
    }
    char buf[FILE_SIZE];
    memset(buf, '\0', FILE_SIZE);
    strncpy(buf, subject, pos);
    int len = strlen(buf);
    strcpy(buf+len, insert);
    len += strlen(insert);
    strcpy(buf+len, subject+pos);
    strcpy(subject, buf);
}

void removeChar(char subject[], int pos) {
    memmove(&subject[pos], &subject[pos + 1], strlen(subject) - pos);
}

void analyzeDirectory() {
    rewinddir(pDir);
    directoryWidth = 0;
    directoryHeight = 0;
    while((pDirent = readdir(pDir)) != NULL) {
        if(directoryWidth < strlen(pDirent->d_name)) {
            directoryWidth = strlen(pDirent->d_name);
        }
        ++directoryHeight;
    }
    --directoryHeight;
    rewinddir(pDir);
    
    numOfPagesNav = (directoryHeight/winsize.ws_row) + 1;
}

void processVal(char val, int *idx) {
    int endIdx = *idx - 1;
    while(newFileString[endIdx] != '\n' && newFileString[endIdx] != ' ' && newFileString[endIdx] != '(' && newFileString[endIdx] != ')' && newFileString[endIdx] != ';' && newFileString[endIdx] != '\0') {
        endIdx++;
    }
    
    if(*idx - 1 == endIdx) {
        printf("%c", val);
        return;
    }
    
    char word[endIdx - *idx + 2];
    memset(word, '\0', sizeof(word));
    strncpy(word, &newFileString[*idx - 1], endIdx - *idx + 1);
    if(strcmp(word, "int") == 0) {
        printf("%sint%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 2;
    } else if(strcmp(word, "char") == 0) {
        printf("%schar%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 3;
    } else if(strcmp(word, "if") == 0) {
        printf("%sif%s", ANSI_COLOR_PURPLE, ANSI_COLOR_NORMAL);
        (*idx) += 1;
    } else if(strcmp(word, "else") == 0) {
        printf("%selse%s", ANSI_COLOR_PURPLE, ANSI_COLOR_NORMAL);
        (*idx) += 3;
    } else if(strcmp(word, "switch") == 0) {
        printf("%sswitch%s", ANSI_COLOR_PURPLE, ANSI_COLOR_NORMAL);
        (*idx) += 5;
    } else if(strcmp(word, "while") == 0) {
        printf("%swhile%s", ANSI_COLOR_PURPLE, ANSI_COLOR_NORMAL);
        (*idx) += 4;
    } else if(strcmp(word, "long") == 0) {
        printf("%slong%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 3;
    } else if(strcmp(word, "short") == 0) {
        printf("%sshort%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 4;
    } else if(strcmp(word, "float") == 0) {
        printf("%sfloat%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 4;
    } else if(strcmp(word, "double") == 0) {
        printf("%sdouble%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 5;
    } else if(strcmp(word, "struct") == 0) {
        printf("%sstruct%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 5;
    } else if(strcmp(word, "enum") == 0) {
        printf("%senum%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
        (*idx) += 3;
    } else if(strcmp(word, "return") == 0) {
        printf("%sreturn%s", ANSI_COLOR_YELLOW, ANSI_COLOR_NORMAL);
        (*idx) += 5;
    } else {
        
        int notNumber = 0;
        for(int i=0; i<strlen(word); i++) {
            if(!isdigit(word[i])) {
                notNumber = 1;
                break;
            }
        }
        if(!notNumber) {
            printf("%s%s%s", ANSI_COLOR_RED, word, ANSI_COLOR_NORMAL);
            (*idx) += (strlen(word) - 1);
            return;
        }
        
        int notAlnum = 0;
        for(int i=0; i<strlen(word); i++) {
            if(!isalnum(word[i])) {
                notAlnum = 1;
                break;
            }
        }
        if(!notAlnum) {
            if(newFileString[endIdx] == '(') {
                printf("%s%s%s", ANSI_COLOR_CYAN, word, ANSI_COLOR_NORMAL);
                (*idx) += (strlen(word) - 1);
                return;
            }
        }
        
        printf("%s", word);
        (*idx) += (strlen(word) - 1);
    }
}

void refreshDisplay(int type) {
    contentCount = 0;
    readyToRender = 0;
    analyzeDirectory();
    
    if(type != INIT) {
        printf("%s", ANSI_HIDE_CURSOR);
        printf("\033[%i;%iH", 1, 1);
        rewind(stdout);
        if(!cooked) {
            printf("\r");
        }
    }
    
    int pageOffset = 0;
    while((pDirent = readdir(pDir)) != NULL) {
        if((numOfPagesNav - 1) == 0 || currPage == 0) {
            rewinddir(pDir);
            break;
        } else if(pageOffset >= currPage * winsize.ws_row) {
            break;
        }
        ++pageOffset;
    }
    
    int rowOffset = 0;
    if(cooked) {
        rowOffset = 2;
    }
    
    if(!editing) {
        int newLineFlag = 0;
        for(int h=0; h<winsize.ws_row - rowOffset; h++) {
            pDirent = readdir(pDir);
            
            int contentLen;
            if(pDirent != NULL) {
                ++contentCount;
                contentLen = strlen(pDirent->d_name);
            }
            int nonPrintable = 0;
            int tabOccured = 0;
            if(newLineFlag > 0) {--newLineFlag;}
            for(int w=0; w<winsize.ws_col; w++) {
                if(w == 0) {
                    if(cursorPosition == h) {
                        printf("%s>%s", ANSI_COLOR_GREEN, ANSI_COLOR_NORMAL);
                        selectedDirent = pDirent;
                    } else {
                        printf(" ");
                    }
                } else {
                    if(pDirent != NULL && contentLen > w - 1 && pDirent->d_name[w - 1] != '\n' && pDirent->d_name[w - 1] != '\r') {
                        printf("%c", pDirent->d_name[w - 1]);
                    } else if(directoryWidth + 2 == w) {
                        printf("|");
                    } else if(directoryWidth + 3 == w) {
                        printf(" ");
                    } else if(fp != NULL && w > directoryWidth + 3) {
                        int val;
                        fpOffset = ftell(fp);
                        if(tabOccured || newLineFlag) {
                            printf(" ");
                        } else if(!nonPrintable) {
                            val = fgetc(fp);
                            if((31 < val && val < 127) || val == 9) {
                                if(val == 9) {
                                    tabOccured = 4;
                                    printf(" ");
                                } else {
                                    printf("%c", val);
                                }
                            } else {
                                nonPrintable = 1;
                                printf(" ");
                            }
                        } else {
                            printf(" ");
                            val = fgetc(fp);
                            if(val == '\n') {
                                newLineFlag = 2;
                            } else if((31 < val && val < 127) || val == 9) {
                                fseek(fp, -1, SEEK_CUR);
                            }
                        }
                    } else {
                        printf(" ");
                    }
                    if(tabOccured > 0) { --tabOccured;}
                }
            }
        }
        if(cooked) {
            printf("\n");
        }
    } else {
        int *i = (int *)malloc(sizeof(int));
        *i = editingPageOffset;
        for(int h=0; h<winsize.ws_row - rowOffset; h++) {
            int newline = 0;
            int tabOccured = 0;
            for(int w=0; w<winsize.ws_col; w++) {
                int val;
                if(tabOccured || newline) {
                    printf(" ");
                } else {
                    if((val = newFileString[(*i)++]) != '\0') {
                        if(val == 9) {
                            tabOccured = 4;
                            printf(" ");
                        } else if(val == '\n') {
                            newline = 1;
                            printf(" ");
                        } else {
                            int pre = *i;
                            processVal(val, i);
                            int post = *i;
                            w += (post - pre);
                        }
                    } else {
                        printf(" ");
                    }
                }
                if(tabOccured > 0) { --tabOccured;}
            }
        }
        free(i);
    }
    rewind(stdout);
    printf("%s", ANSI_SHOW_CURSOR);
    readyToRender = 1;
}

void downArrow(int enteredValue) {
    if(!editing) {
        if(fp != NULL) {
            fclose(fp);
        }
        ++cursorPosition;
        if(cursorPosition > contentCount - 1) {
            cursorPosition = 0;
            ++currPage;
            if(currPage > numOfPagesNav - 1) {
                currPage = 0;
            }
        }
        refreshDisplay(UPDATE);
        printf("%s", ANSI_HIDE_CURSOR);
    } else {
        int i = 0;
        int flagWasNegative = 0;
        if(newFileStrOffset == -1 || newFileString[newFileStrOffset] == 10) {
            ++newFileStrOffset;
            flagWasNegative = 1;
        }
        
        while(newFileString[newFileStrOffset] != '\n' && newFileString[newFileStrOffset] != '\0') {
            if (i > winsize.ws_col) {
                ++editingCursorPositionY;
                if(flagWasNegative) {
                    --newFileStrOffset;
                }
                --newFileStrOffset;
                if(editingCursorPositionY > winsize.ws_row) {
                    ++newFileStrOffset;
                    --editingCursorPositionY;
                    i = 0;
                    while(newFileString[editingPageOffset] != '\n' && newFileString[editingPageOffset] != '\0' && i < winsize.ws_col) {
                        if (newFileString[editingPageOffset] == '\t') {
                            i+=3;
                        }
                        ++editingPageOffset;
                        ++i;
                    }
                    ++editingPageOffset;
                    refreshDisplay(UPDATE);
                }
                printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                return;
            }
            
            if (newFileString[newFileStrOffset] == '\t') {
                i+=4;
            } else {
                ++i;
            }
            ++newFileStrOffset;
        }
        
        if(newFileString[newFileStrOffset] == '\0') {
            newFileStrOffset -= i;
            return;
        }
        
        if(newFileString[newFileStrOffset] == '\n') {
            ++newFileStrOffset;
        }
        
        if(i + editingCursorPositionX > winsize.ws_col) {
            editingCursorPositionX = ((i+editingCursorPositionX) % winsize.ws_col) - 1;
            ++editingCursorPositionY;
            newFileStrOffset-=2;
        } else {
            i = 1;
            int j = 0;
            while(i < editingCursorPositionX && newFileString[newFileStrOffset] != '\0' && newFileString[newFileStrOffset] != '\n') {
                if(newFileString[newFileStrOffset] == 9) {
                    i += 3;
                }
                ++i;
                ++j;
                ++newFileStrOffset;
            }
            --newFileStrOffset;
            ++editingCursorPositionY;
            editingCursorPositionX = i;
        }
        if(editingCursorPositionY > winsize.ws_row) {
            --editingCursorPositionY;
            int q = 0;
            int p = 0;
            while(newFileString[editingPageOffset+q] != '\n' && newFileString[editingPageOffset+q] != '\0') {
                if (q >= winsize.ws_col - 1) {
                    break;
                }
                if(newFileString[editingPageOffset+q] == 9) {
                    q+=3;
                }
                ++q;
                ++p;
            }
            editingPageOffset += (p+1);
            refreshDisplay(UPDATE);
        }
        printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
    }
}

void upArrow(int enteredValue) {
    if(!editing) {
        if(fp != NULL) {
            fclose(fp);
        }
        --cursorPosition;
        if(cursorPosition < 0) {
            cursorPosition = winsize.ws_row - 1;
            --currPage;
            if(currPage < 0) {
                currPage = numOfPagesNav - 1;
                if(currPage != 0) {
                    cursorPosition = (directoryHeight - 1)%winsize.ws_row;
                }
            }
        }
        refreshDisplay(UPDATE);
        printf("%s", ANSI_HIDE_CURSOR);
    } else {
        int i = 0;
        while(newFileStrOffset > -1 && newFileString[newFileStrOffset] != 10) {
            if (i >= winsize.ws_col - 1) {
                --newFileStrOffset;
                --editingCursorPositionY;
                if(editingCursorPositionY <= 0) {
                    ++editingCursorPositionY;
                    int j = 0;
                    i = 1;
                    while(editingPageOffset - i > -1 && newFileString[editingPageOffset-i] != 10) {
                        if(newFileString[editingPageOffset - i] == 9) {
                            j+=3;
                        }
                        ++j;
                        ++i;
                    }
                    
                    editingPageOffset -= winsize.ws_col;
                    refreshDisplay(UPDATE);
                }
                printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                return;
            }
            if (newFileString[newFileStrOffset] == '\t') {
                i+=4;
            } else {
                ++i;
            }
            --newFileStrOffset;
        }
        if(newFileString[newFileStrOffset] == '\n') {
            --newFileStrOffset;
        }
        
        int wrapFlag = 0;
        i = 0;
        while(newFileStrOffset > -1 && newFileString[newFileStrOffset] != 10) {
            if (i >= winsize.ws_col - 1) {
                wrapFlag = 1;
            }
            --newFileStrOffset;
            ++i;
        }
        ++newFileStrOffset;
        
        if(wrapFlag) {
            if(editingCursorPositionY <= 2) {
                editingCursorPositionX = (i)%winsize.ws_col;
                ++newFileStrOffset;
            } else {
                editingCursorPositionX = (i+1)%winsize.ws_col;
            }
        }
        
        i = 1;
        while(i < editingCursorPositionX) {
            if(newFileString[newFileStrOffset] == 10) {
                break;
            } else if(newFileString[newFileStrOffset] == '\0') {
                break;
            } else if(newFileString[newFileStrOffset] == 9) {
                i+=4;
                ++newFileStrOffset;
            } else {
                i++;
                ++newFileStrOffset;
            }
        }
        if(wrapFlag) {
            newFileStrOffset+=winsize.ws_col;
        }
        --newFileStrOffset;
        editingCursorPositionX = i;
        --editingCursorPositionY;
        if(editingCursorPositionY < 1) {
            if(wrapFlag) {
                ++editingCursorPositionY;
                i = 0;
                int j = 0;
                if(newFileString[editingPageOffset - j] == '\n') {
                    ++j;
                    ++i;
                }
                while(editingPageOffset - j > -1 && newFileString[editingPageOffset - j] != 10) {
                    if(newFileString[editingPageOffset - j] == 9) {
                        i+=3;
                    }
                    ++j;
                    ++i;
                }
                if(newFileString[editingPageOffset - j] == '\n') {
                    ++j;
                    ++i;
                }
                while(editingPageOffset - j > -1 && newFileString[editingPageOffset - j] != 10) {
                    if(newFileString[editingPageOffset - j] == 9) {
                        i+=3;
                    }
                    ++j;
                    ++i;
                }
                editingCursorPositionX = (i-1)%winsize.ws_col;
                editingPageOffset-=editingCursorPositionX;
            } else {
                ++editingCursorPositionY;
                if(newFileString[editingPageOffset] == '\n') {
                    --editingPageOffset;
                }
                while(editingPageOffset > -1 && newFileString[editingPageOffset] != 10) {
                    --editingPageOffset;
                }
                if(newFileString[editingPageOffset] == '\n') {
                    --editingPageOffset;
                }
                while(editingPageOffset > -1 && newFileString[editingPageOffset] != 10) {
                    --editingPageOffset;
                }
                ++editingPageOffset;
                i = 1;
                int j = 0;
                while(i < editingCursorPositionX) {
                    if(newFileString[editingPageOffset + j] == 10) {
                        break;
                    } else if(newFileString[editingPageOffset + j] == 9) {
                        i+=4;
                        ++j;
                    } else {
                        ++i;
                        ++j;
                    }
                }
                
                editingCursorPositionX = i;
            }
            refreshDisplay(UPDATE);
        }
    }
    printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
}

void rightArrow(int enteredValue) {
    if(!editing) {
        char tmp[sizeof(selectedDirent->d_name)];
        char cwdTmp[sizeof(cwd)];
        strcpy(tmp, selectedDirent->d_name);
        strcpy(cwdTmp, cwd);
        strcat(cwdTmp, "/");
        strcat(cwdTmp, tmp);
        closedir(pDir);
        pDir = opendir(cwdTmp);
        if (pDir != NULL) {
            strcpy(cwd, cwdTmp);
            cursorPosition = 2;
            currPage = 0;
        } else {
            pDir = opendir(cwd);
            fclose(fp);
            char filePath[sizeof(cwd)];
            memset(filePath, 0, sizeof(filePath));
            strcpy(filePath, cwd);
            strcat(filePath, "/");
            strcat(filePath, selectedDirent->d_name);
            strcpy(currCloneFilePath, filePath);
            strcat(currCloneFilePath, "-tmp");
            fp = fopen(filePath, "r");
        }
        refreshDisplay(UPDATE);
    } else {
        if(newFileString[newFileStrOffset+1] == '\0') {
            
        } else {
            ++newFileStrOffset;
            if(newFileString[newFileStrOffset] == 10 || enteredValue == 10) {
                editingCursorPositionX = 1;
                if(editingCursorPositionY < winsize.ws_row) {
                    ++editingCursorPositionY;
                } else {
                    while(newFileString[editingPageOffset] != 10 && newFileString[editingPageOffset] != '\0') {
                        ++editingPageOffset;
                    }
                    if (newFileString[editingPageOffset] == 10) {
                        ++editingPageOffset;
                    }
                    refreshDisplay(UPDATE);
                }
            } else if(newFileString[newFileStrOffset] == 9) {
                editingCursorPositionX += 4;
            } else {
                if(editingCursorPositionX < winsize.ws_col) {
                    ++editingCursorPositionX;
                } else {
                    if(editingCursorPositionY < winsize.ws_row) {
                        editingCursorPositionX = 1;
                        editingCursorPositionY++;
                    } else {
                        int i = 0;
                        while(newFileString[newFileStrOffset + i] != 10 && newFileString[newFileStrOffset + i] != '\0' && i<winsize.ws_col) {
                            if(newFileString[newFileStrOffset + i] == 9) {
                                i+=3;
                            }
                            ++i;
                            ++editingPageOffset;
                        }
                        editingCursorPositionX = 1;
                        --newFileStrOffset;
                        refreshDisplay(UPDATE);
                    }
                }
            }
            //printf("%c\n\n", newFileString[newFileStrOffset]);
            printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
        }
    }
}

void leftArrow(enteredValue) {
    if(!editing) {
        int len = strlen(cwd);
        if(strcmp(cwd, "/")) {
            for(int i=len-1; i>=0; i--) {
                if (cwd[i] == '/') {
                    cwd[i] = '\0';
                    if(cwd[0] == '\0') {
                        cwd[0] = '/';
                        cwd[1] = '\0';
                    }
                    break;
                }
            }
        }
        closedir (pDir);
        pDir = opendir(cwd);
        if (pDir == NULL) {
            printf ("Cannot open directory\n");
        }
        cursorPosition = 2;
        currPage = 0;
        refreshDisplay(UPDATE);
    } else {
        if(newFileStrOffset - 1 < -1) {
            
        } else {
            if (editingCursorPositionY <= 1 && editingCursorPositionX <= 1) {
                int i = 0;
                int j = 0;
                --editingPageOffset;
                if(newFileString[editingPageOffset] == 10) {
                    --editingPageOffset;
                }
                while(newFileString[editingPageOffset - j] != 10 && editingPageOffset - j > -2) {
                    if(newFileString[editingPageOffset - j] == 9) {
                        i+=3;
                    }
                    ++i;
                    ++j;
                }
                
                --newFileStrOffset;
                
                editingCursorPositionX = (i%winsize.ws_col)+1;
                for(int x=0; x<editingCursorPositionX-1; x++) {
                    --editingPageOffset;
                }
                
                ++editingPageOffset;
                refreshDisplay(UPDATE);
                printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                return;
            }
            if((newFileString[newFileStrOffset] == 10 && enteredValue != 127) || enteredValue == 600) {
                if(editingCursorPositionY == 1) {
                    --editingPageOffset;
                    if (newFileString[editingPageOffset] == 10) {
                        --editingPageOffset;
                    }
                    while(newFileString[editingPageOffset] != 10 && newFileString[editingPageOffset] != '\0') {
                        --editingPageOffset;
                    }
                    if (newFileString[editingPageOffset] == 10) {
                        ++editingPageOffset;
                    }
                    int i = 1;
                    int cnt = 1;
                    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -2) {
                        if(newFileString[newFileStrOffset - i] == 9) {
                            cnt += 4;
                            ++i;
                        } else {
                            ++cnt;
                            ++i;
                        }
                    }
                    editingCursorPositionX = cnt;
                    refreshDisplay(UPDATE);
                } else {
                    --editingCursorPositionY;
                    --newFileStrOffset;
                    int i = 0;
                    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -1) {
                        if(newFileString[newFileStrOffset - i] == 9) {
                            i+=3;
                        }
                        ++i;
                    }
                    editingCursorPositionX = (i+1)%winsize.ws_col;
                }
            } else if(newFileString[newFileStrOffset] == 9 || enteredValue == 700) {
                editingCursorPositionX -= 4;
            } else {
                if(editingCursorPositionX <= 1) {
                    int i = 0;
                    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -1 && i < winsize.ws_col) {
                        if(newFileString[newFileStrOffset - i] == 9) {
                            i+=3;
                        }
                        ++i;
                    }
                    --editingCursorPositionY;
                    editingCursorPositionX = i;
                    --newFileStrOffset;
                    --newFileStrOffset;
                } else {
                    --editingCursorPositionX;
                    --newFileStrOffset;
                }
            }
            printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
        }
    }
}

void cancelHandler(int x) {
    if(savePrompt) {
        savePrompt = 0;
        system("/bin/stty raw");
        cooked = 0;
        refreshDisplay(INIT);
        refreshDisplay(UPDATE);
        printf("%s", ANSI_SHOW_CURSOR);
        fputc(132, stdin);
    }
}

void *updateWindowSize() {
    for (;;) {
        int width = winsize.ws_col;
        int height = winsize.ws_row;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);
        if(width != winsize.ws_col || height != winsize.ws_row) {
            refreshDisplay(UPDATE);
        }
        usleep(250000);
    }
}

int main(int argc, char **argv) {
    
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);
    pthread_t windowSizeUpdateThread;
    pthread_create(&windowSizeUpdateThread, NULL, updateWindowSize, NULL);
    system("/bin/stty raw");
    signal(SIGINT, cancelHandler);
    
    consoleText = (char *)malloc(MAX_CONSOLE_TEXT*sizeof(char));
    currCloneFilePath = (char *)malloc(MAX_CONSOLE_TEXT*sizeof(char));
    newFileString = (char *)malloc(FILE_SIZE*sizeof(char));
    memset(newFileString, '\0', FILE_SIZE);
    editingPageOffset = 0;
    
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd() error");
    }
    
    pDir = opendir (cwd);
    if (pDir == NULL) {
        printf ("Cannot open directory\n");
    }
    
    refreshDisplay(INIT);
    refreshDisplay(UPDATE);
    
    char newFilename[1024];
    int newFilenamePos = 0;
    int lastEnteredChar = 0;
    int lastlastEnteredChar = 0;
    while ((enteredChar = fgetc(stdin)) != EOF && appRunning) {
        if(readyToRender) {
            //printf("%i\n", enteredChar);
            if((lastEnteredChar == 27 && enteredChar == 13) || (lastEnteredChar == 27 && enteredChar == 10)) {
                appRunning = 0;
                break;
            }
            if(savePrompt && enteredChar != 10) {
                newFilename[newFilenamePos++] = enteredChar;
                newFilename[newFilenamePos] = '\0';
                continue;
            }
            if(savePrompt && enteredChar == 10) {
                system("/bin/stty raw");
                cooked = 0;
                savePrompt = 0;
                newFilenamePos = 0;
                editing = 0;
                char filePath[sizeof(cwd)];
                memset(filePath, 0, sizeof(filePath));
                strcpy(filePath, cwd);
                strcat(filePath, "/");
                strcpy(currCloneFilePath, filePath);
                strcat(currCloneFilePath, newFilename);
                fpClone = fopen(currCloneFilePath, "w+");
                int val;
                int i = 0;
                while((val = newFileString[i++]) != '\0') {
                    int val2, val3, val4;
                    if(val == ' ') {
                        if((val2 = newFileString[i++]) == ' ') {
                            if((val3 = newFileString[i++]) == ' ') {
                                if((val4 = newFileString[i++]) == ' ') {
                                    fputc('\t', fpClone);
                                } else {
                                    fputc(val, fpClone);
                                    fputc(val2, fpClone);
                                    fputc(val3, fpClone);
                                    fputc(val4, fpClone);
                                }
                            } else {
                                fputc(val, fpClone);
                                fputc(val2, fpClone);
                                fputc(val3, fpClone);
                            }
                        } else {
                            fputc(val, fpClone);
                            fputc(val2, fpClone);
                        }
                    } else {
                        fputc(val, fpClone);
                    }
                }
                fclose(fpClone);
                refreshDisplay(UPDATE);
            }
            if(editing && enteredChar == 15) {
                savePrompt = 1;
                system ("/bin/stty cooked");
                cooked = 1;
                refreshDisplay(UPDATE);
                refreshDisplay(UPDATE);
                if (fp != NULL) {
                    printf("Hit CTRL-C to Cancel. Save as (%s): ", selectedDirent->d_name);
                } else {
                    printf("Hit CTRL-C to Cancel. Save as: ");
                }
                continue;
            }
            if(editing && enteredChar == 24) {
                editing = 0;
                fclose(fpClone);
                remove(currCloneFilePath);
                refreshDisplay(UPDATE);
                continue;
            }
            if(editing && enteredChar == 22) {
                while(editingCursorPositionY < winsize.ws_row) {
                    downArrow(-1);
                }
                for(int i=0; i<winsize.ws_row; i++) {
                    downArrow(-1);
                }
                continue;
            }
            if(editing && enteredChar == 25) {
                while(editingCursorPositionY > 1) {
                    upArrow(-1);
                }
                for(int i=0; i<winsize.ws_row; i++) {
                    upArrow(-1);
                }
                continue;
            }
            if(enteredChar == 10 && cooked) {
                refreshDisplay(UPDATE);
            }
            if(enteredChar == 13 && !cooked && !editing) {
                if(fp != NULL) {
                    editing = 1;
                    rewind(fp);
                    fseek(fp, 0L, SEEK_END);
                    int sizeFile = ftell(fp);
                    if(sizeFile > FILE_SIZE) {
                        FILE_SIZE = sizeFile * 2;
                        newFileString = (char *)realloc(newFileString, FILE_SIZE);
                    }
                    rewind(fp);
                    int val;
                    int i = 0;
                    while((val = fgetc(fp)) != EOF) {
                        if(val == 10) {
                            int j = 1;
                            while(newFileString[i-j] == ' ') {
                                ++j;
                            }
                            --j;
                            newFileString[i-j] = val;
                            i = i - j + 1;
                        } else if(val == 9) {
                            newFileString[i] = ' ';
                            newFileString[i+1] = ' ';
                            newFileString[i+2] = ' ';
                            newFileString[i+3] = ' ';
                            i+=4;
                        } else {
                            newFileString[i++] = val;
                        }
                    }
                    newFileString[i] = '\0';
                    rewind(fp);
                    refreshDisplay(UPDATE);
                    editingCursorPositionX = 1;
                    editingCursorPositionY = 1;
                    printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                }
            } else if(enteredChar == 14 && !cooked && !editing) {
                editing = 1;
                refreshDisplay(UPDATE);
                editingCursorPositionX = 1;
                editingCursorPositionY = 1;
                printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);

            } else if(!cooked) {
                if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 65) {
                    upArrow(-1);
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 66) {
                    downArrow(-1);
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 67) {
                    rightArrow(-1);
                } else if(lastlastEnteredChar == 27 && lastEnteredChar == 91 && enteredChar == 68) {
                    leftArrow(-1);
                } else if(lastlastEnteredChar != 27 && lastEnteredChar != 27 && ((enteredChar > 31 && enteredChar < 127) || (enteredChar == 13))) {
                    if(enteredChar == 13) {enteredChar = '\n';}
                    char tmp[2];
                    tmp[0] = enteredChar;
                    tmp[1] = '\0';
                    appendChars(newFileString, tmp, newFileStrOffset+1);
                    refreshDisplay(UPDATE);
                    rightArrow(enteredChar);
                } else if(enteredChar == 127) {
                    if(newFileStrOffset - 1 > -2) {
                        if(newFileString[newFileStrOffset] == 10) {
                            if(editingCursorPositionX <= 1) {
                                if(editingCursorPositionY <= 1) {
                                    removeChar(newFileString, newFileStrOffset);
                                    leftArrow(-1);
                                    --editingCursorPositionX;
                                } else {
                                    removeChar(newFileString, newFileStrOffset);
                                    leftArrow(-1);
                                    ++newFileStrOffset;
                                }
                            }
                        } else if(newFileString[newFileStrOffset] == 9) {
                            editingCursorPositionX -= 4;
                            removeChar(newFileString, newFileStrOffset);
                            --newFileStrOffset;
                        } else {
                            if(editingCursorPositionX <= 1) {
                                if(editingCursorPositionY <= 1) {
                                    int i = 0;
                                    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -2 && i<winsize.ws_col) {
                                        if(newFileString[newFileStrOffset - i] == 9) {
                                            i+=3;
                                        }
                                        i++;
                                        --editingPageOffset;
                                    }
                                    editingCursorPositionX = i;
                                    if(i == winsize.ws_col && newFileString[newFileStrOffset - i] != 10) {
                                        --newFileStrOffset;
                                    }
                                    removeChar(newFileString, newFileStrOffset);
                                    --newFileStrOffset;
                                } else {
                                    removeChar(newFileString, newFileStrOffset);
                                    int i = 0;
                                    while(newFileString[newFileStrOffset - i] != 10 && newFileStrOffset - i > -2 && i<winsize.ws_col) {
                                        if(newFileString[newFileStrOffset - i] == 9) {
                                            i+=3;
                                        }
                                        i++;
                                    }
                                    editingCursorPositionX = i;
                                    editingCursorPositionY--;
                                    --newFileStrOffset;
                                }
                            } else {
                                --editingCursorPositionX;
                                removeChar(newFileString, newFileStrOffset);
                                --newFileStrOffset;
                            }
                        }

                        refreshDisplay(UPDATE);

                        printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                    }
                } else if(enteredChar == 9) {
                    appendChars(newFileString, " ", newFileStrOffset+1);
                    rightArrow(enteredChar);
                    appendChars(newFileString, " ", newFileStrOffset+1);
                    rightArrow(enteredChar);
                    appendChars(newFileString, " ", newFileStrOffset+1);
                    rightArrow(enteredChar);
                    appendChars(newFileString, " ", newFileStrOffset+1);
                    rightArrow(enteredChar);
                    refreshDisplay(UPDATE);
                    printf("\033[%i;%iH", editingCursorPositionY, editingCursorPositionX);
                }
            }
            lastlastEnteredChar = lastEnteredChar;
            lastEnteredChar = enteredChar;
        }
    }
    
    printf(ANSI_COLOR_NORMAL);
    fclose(fp);
    fclose(fpClone);
    system ("/bin/stty cooked");
    printf("%s", ANSI_SHOW_CURSOR);
    closedir (pDir);
    free(consoleText);
    free(currCloneFilePath);
    
    return 0;
}
