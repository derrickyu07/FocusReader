# FocusReader

FocusReader is a small Windows screen reader prototype. It watches which control has keyboard focus anywhere on the desktop and speaks it aloud, the same core loop that tools like JAWS and NVDA are built around.

I built it in three days as a learning project, to get hands on with the stack Windows assistive technology is written in: native C++ for the low level accessibility and speech work, and C# with WPF for the user facing app.

## How it works

FocusReader is split into two parts:

**FocusReaderCore (C++ DLL)**
- Subscribes to focus change events through **UI Automation (UIA)**, the Windows accessibility API that exposes every control on screen (its name, role, and state) to assistive technology
- Builds an announcement from the focused element, such as "Save, button"
- Speaks it through the Windows speech engine (**SAPI**, the Speech API built into Windows)

**FocusReader (C# WPF app)**
- Starts and stops the native engine
- Shows a live log of what was announced
- Lets the user adjust settings like speech rate and volume

The WPF app calls into the DLL through **P/Invoke**, the .NET mechanism for calling functions exported from native code.

```
Windows desktop
      │  focus change events (UI Automation)
      ▼
FocusReaderCore.dll (C++)  ──►  Windows speech (SAPI)
      ▲
      │  P/Invoke calls
FocusReader (C# WPF)
```

Keeping the accessibility and speech work in C++ and the interface in C# mirrors how many Windows assistive technology products are structured: performance sensitive, close to the OS code stays native, while the UI uses a modern .NET framework.

## Project structure

```
FocusReader.sln
├── FocusReaderCore/      C++ DLL: UI Automation event handling and speech
└── FocusReader/          C# WPF app: controls, settings, announcement log
```

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with these workloads:
  - Desktop development with C++
  - .NET desktop development

## Build and run

1. Open `FocusReader.sln` in Visual Studio
2. Set the platform to **x64**, so the C# app and the C++ DLL match
3. Set **FocusReader** as the startup project
4. Build and run with **F5**
5. Click **Start**, then move focus around Windows with Tab and the arrow keys

