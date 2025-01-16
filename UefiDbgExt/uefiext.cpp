/*++

Copyright (c) Microsoft Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent

Module Name:

    uefiext.cpp

Abstract:

    This file contains core UEFI debug commands.

--*/

#include "uefiext.h"

UEFI_ENV  gUefiEnv = DXE;

HRESULT
NotifyOnTargetAccessible (
  PDEBUG_CONTROL  Control
  )
{
  //
  // Attempt to determine what environment the debugger is in.
  //

  return S_OK;
}

HRESULT CALLBACK
setenv (
  PDEBUG_CLIENT4  Client,
  PCSTR           args
  )
{
  INIT_API ();

  if (_stricmp (args, "PEI") == 0) {
    gUefiEnv = PEI;
  } else if (_stricmp (args, "DXE") == 0) {
    gUefiEnv = DXE;
  } else if (_stricmp (args, "MM") == 0) {
    gUefiEnv = MM;
  } else if (_stricmp (args, "rust") == 0) {
    gUefiEnv = RUST;
  } else {
    dprintf ("Unknown environment type! Supported types: PEI, DXE, MM, rust\n");
  }

  EXIT_API ();
  return S_OK;
}

HRESULT CALLBACK
help (
  PDEBUG_CLIENT4  Client,
  PCSTR           args
  )
{
  INIT_API ();

  UNREFERENCED_PARAMETER (args);

  dprintf (
    "Help for uefiext.dll\n"
    "\nBasic Commands:\n"
    "  help                - Shows this help\n"
    "  init                - Detects and initializes windbg for debugging UEFI.\n"
    "  setenv              - Set the extensions environment mode\n"
    "\nModule Discovery:\n"
    "  findall             - Attempts to detect environment and load all modules\n"
    "  findmodule          - Find the currently running module\n"
    "  loadmodules         - Find and loads symbols for all modules in the debug list\n"
    "  elf                 - Dumps the headers of an ELF image\n"
    "\nData Parsing:\n"
    "  memorymap           - Prints the current memory map\n"
    "  hobs                - Enumerates the hand off blocks\n"
    "  protocols           - Lists the protocols from the protocol list.\n"
    "  handles             - Prints the handles list.\n"
    "  linkedlist          - Parses a UEFI style linked list of entries.\n"
    "  efierror            - Translates an EFI error code.\n"
    "  advlog              - Prints the advanced logger memory log.\n"
    "\nUEFI Debugger:\n"
    "  info                - Queries information about the UEFI debugger\n"
    "  monitor             - Sends direct monitor commands\n"
    "  modulebreak         - Sets a break on load for the provided module. e.g. 'shell'\n"
    "  readmsr             - Reads a MSR value (x86 only)\n"
    "  readvar             - Reads a UEFI variable\n"
    "  reboot              - Reboots the system\n"
    );

  EXIT_API ();

  return S_OK;
}

HRESULT CALLBACK
init (
  PDEBUG_CLIENT4  Client,
  PCSTR           args
  )
{
  ULONG  TargetClass = 0;
  ULONG  TargetQual  = 0;
  ULONG  Mask;
  PCSTR  Output;

  INIT_API ();

  UNREFERENCED_PARAMETER (args);

  dprintf ("Initializing UEFI Debugger Extension\n");
  g_ExtControl->GetDebuggeeType (&TargetClass, &TargetQual);
  if ((TargetClass == DEBUG_CLASS_KERNEL) && (TargetQual == DEBUG_KERNEL_EXDI_DRIVER)) {
    // Enabled the verbose flag in the output mask. This is required for .exdicmd
    // output.
    Client->GetOutputMask (&Mask);
    Client->SetOutputMask (Mask | DEBUG_OUTPUT_VERBOSE);

    // Detect if this is a UEFI software debugger.
    Output = ExecuteCommandWithOutput (Client, ".exdicmd target:0:?");
    if (strstr (Output, "Rust Debugger") != NULL) {
      dprintf ("Rust UEFI Debugger detected.\n");
      gUefiEnv = RUST;
    } else if (strstr (Output, "DXE UEFI Debugger") != NULL) {
      dprintf ("DXE UEFI Debugger detected.\n");
      gUefiEnv = DXE;
    } else {
      dprintf ("Unknown environment, assuming DXE.\n");
      gUefiEnv = DXE;
    }

    dprintf ("Scanning for images.\n");
    if (gUefiEnv == DXE) {
      g_ExtControl->Execute (
                      DEBUG_OUTCTL_ALL_CLIENTS,
                      "!uefiext.findall",
                      DEBUG_EXECUTE_DEFAULT
                      );
    } else {
      g_ExtControl->Execute (
                      DEBUG_OUTCTL_ALL_CLIENTS,
                      "!uefiext.findmodule",
                      DEBUG_EXECUTE_DEFAULT
                      );
    }
  }

  EXIT_API ();

  return S_OK;
}

// Used to capture output from debugger commands
CHAR  mOutput[1024];

class OutputCallbacks : public IDebugOutputCallbacks {
public:

STDMETHOD (QueryInterface)(THIS_ REFIID InterfaceId, PVOID *Interface) {
  if (InterfaceId == __uuidof (IDebugOutputCallbacks)) {
    *Interface = (IDebugOutputCallbacks *)this;
    AddRef ();
    return S_OK;
  } else {
    *Interface = NULL;
    return E_NOINTERFACE;
  }
}

STDMETHOD_ (ULONG, AddRef)(THIS) {
  return 1;
}

STDMETHOD_ (ULONG, Release)(THIS) {
  return 1;
}

STDMETHOD (Output)(THIS_ ULONG Mask, PCSTR Text) {
  strcpy_s (mOutput, sizeof (mOutput), Text);
  return S_OK;
}
};

OutputCallbacks  mOutputCallback;

PSTR
ExecuteCommandWithOutput (
  PDEBUG_CLIENT4  Client,
  PCSTR           Command
  )
{
  PDEBUG_OUTPUT_CALLBACKS  Callbacks;

  ZeroMemory (mOutput, sizeof (mOutput));

  Client->GetOutputCallbacks (&Callbacks);
  Client->SetOutputCallbacks (&mOutputCallback);
  g_ExtControl->Execute (
                  DEBUG_OUTCTL_ALL_CLIENTS,
                  Command,
                  DEBUG_EXECUTE_DEFAULT
                  );
  Client->SetOutputCallbacks (Callbacks);
  return mOutput;
}

PSTR
MonitorCommandWithOutput (
  PDEBUG_CLIENT4  Client,
  PCSTR           MonitorCommand
  )
{
  CHAR   Command[512];
  PSTR   Output;
  ULONG  Mask;
  PCSTR  Preamble = "Target command response: ";
  PCSTR  Ending   = "exdiCmd:";

  sprintf_s (Command, sizeof (Command), ".exdicmd target:0:%s", MonitorCommand);

  Client->GetOutputMask (&Mask);
  Client->SetOutputMask (Mask | DEBUG_OUTPUT_VERBOSE);
  Output = ExecuteCommandWithOutput (Client, Command);
  Client->SetOutputMask (Mask);

  // Clean up the output.
  if (strstr (Output, Preamble) != NULL) {
    Output = strstr (Output, Preamble) + strlen (Preamble);
  }

  if (strstr (Output, Ending) != NULL) {
    *strstr (Output, Ending) = 0;
  }

  return Output;
}
