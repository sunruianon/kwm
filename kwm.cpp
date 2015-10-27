#include "kwm.h"

kwm_code KWMCode;
export_table ExportTable;

std::vector<screen_info> DisplayLst;
std::vector<window_info> WindowLst;
std::map<int, std::vector<int> > SpacesOfWindow;
int CurrentSpace = 0;
int PrevSpace = -1;

ProcessSerialNumber FocusedPSN;
AXUIElementRef FocusedWindowRef;
window_info *FocusedWindow;
focus_option KwmFocusMode = FocusAutoraise;

uint32_t MaxDisplayCount = 5;
uint32_t ActiveDisplaysCount;
CGDirectDisplayID ActiveDisplays[5];

void Fatal(const std::string &Err)
{
    std::cout << Err << std::endl;
    exit(1);
}

bool CheckPrivileges()
{
    const void * Keys[] = { kAXTrustedCheckOptionPrompt };
    const void * Values[] = { kCFBooleanTrue };

    CFDictionaryRef Options;
    Options = CFDictionaryCreate(kCFAllocatorDefault, 
            Keys, Values, sizeof(Keys) / sizeof(*Keys),
            &kCFCopyStringDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);

    return AXIsProcessTrustedWithOptions(Options);
}

CGEventRef CGEventCallback(CGEventTapProxy Proxy, CGEventType Type, CGEventRef Event, void *Refcon)
{
    if(Type == kCGEventKeyDown)
    {
        UnloadKwmCode(&KWMCode);
        KWMCode = LoadKwmCode();

        CGEventFlags Flags = CGEventGetFlags(Event);
        bool CmdKey = (Flags & kCGEventFlagMaskCommand) == kCGEventFlagMaskCommand;
        bool AltKey = (Flags & kCGEventFlagMaskAlternate) == kCGEventFlagMaskAlternate;
        bool CtrlKey = (Flags & kCGEventFlagMaskControl) == kCGEventFlagMaskControl;
        CGKeyCode Keycode = (CGKeyCode)CGEventGetIntegerValueField(Event, kCGKeyboardEventKeycode);

        // Toggle keytap on | off
        if(KWMCode.KWMHotkeyCommands(&ExportTable, CmdKey, CtrlKey, AltKey, Keycode))
            return NULL;

        // capture custom hotkeys
        if(KWMCode.CustomHotkeyCommands(&ExportTable, CmdKey, CtrlKey, AltKey, Keycode))
            return NULL;

        // Let system hotkeys pass through as normal
        if(KWMCode.SystemHotkeyCommands(&ExportTable, CmdKey, CtrlKey, AltKey, Keycode))
            return Event;
            
        if(KwmFocusMode == FocusFollowsMouse)
        {
            CGEventSetIntegerValueField(Event, kCGKeyboardEventAutorepeat, 0);
            CGEventPostToPSN(&FocusedPSN, Event);
            return NULL;
        }
    }
    else if(Type == kCGEventMouseMoved)
    {
        if(KwmFocusMode != FocusDisabled)
        {
            DetectWindowBelowCursor();
        }
    }

    return Event;
}

void KwmRestart()
{
    DEBUG("KWM Restarting..")
    const char **ExecArgs = new const char*[2];
    ExecArgs[0] = "kwm";
    ExecArgs[1] = NULL;
    execv("/usr/local/bin/kwm", (char**)ExecArgs);
}

int main(int argc, char **argv)
{
    if(!CheckPrivileges())
        Fatal("Could not access OSX Accessibility!"); 

    CFMachPortRef EventTap;
    CGEventMask EventMask;
    CFRunLoopSourceRef RunLoopSource;

    EventMask = ((1 << kCGEventKeyDown) | (1 << kCGEventKeyUp) | (1 << kCGEventMouseMoved));
    EventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, 0, EventMask, CGEventCallback, NULL);

    if(!EventTap || !CGEventTapIsEnabled(EventTap))
        Fatal("could not tap keys, try running as root");
    
    KWMCode = LoadKwmCode();
    if(!KWMCode.IsValid)
        Fatal("Could not load hotkeys.so");

    ExportTable.FocusedWindowRef = FocusedWindowRef;
    ExportTable.FocusedWindow = FocusedWindow;
    ExportTable.KwmFocusMode = KwmFocusMode;;

    ExportTable.DetectWindowBelowCursor = &DetectWindowBelowCursor;
    ExportTable.SetWindowDimensions = &SetWindowDimensions;
    ExportTable.ToggleFocusedWindowFullscreen = &ToggleFocusedWindowFullscreen;
    ExportTable.SwapFocusedWindowWithNearest = &SwapFocusedWindowWithNearest;
    ExportTable.ShiftWindowFocus = &ShiftWindowFocus;
    ExportTable.CycleFocusedWindowDisplay = &CycleFocusedWindowDisplay;
    ExportTable.KwmRestart = &KwmRestart;

    GetActiveSpaces();
    GetActiveDisplays();
    DetectWindowBelowCursor();

    RunLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, EventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), RunLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(EventTap, true);
    CFRunLoopRun();
    return 0;
}
