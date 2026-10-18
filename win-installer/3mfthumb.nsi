; 3mfthumb.nsi
;
; Windows 11 installer for 3mfthumb.
;
; Copyright 2026 Don Eisele <https://toysmakeuspowerful.com/>
; Copyright 2026 Henry Kroll III <nospam@thenerdshow.com>
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
;
; Builds a per-user installer (no administrator rights needed) that:
;   1. installs 3mfthumbprovider.dll, the COM IThumbnailProvider shell
;      extension that makes Windows Explorer show .3mf and .glb thumbnails
;   2. installs its runtime dependencies, lib3mf.dll and libwinpthread-1.dll
;   3. optionally installs the 3mfthumb.exe command-line tool
;   4. registers / unregisters everything under HKCU\Software\Classes
;
; Compile with: makensis 3mfthumb.nsi   (produces 3mfthumb-setup.exe)
; The binaries under staging/ are produced by build.sh (mingw-w64).

Unicode true
CPU amd64

; ---------------------------------------------------------------------------
; Metadata
; ---------------------------------------------------------------------------
Name "3mfthumb"
OutFile "3mfthumb-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\3mfthumb"
InstallDirRegKey HKCU "Software\3mfthumb" "InstallDir"
RequestExecutionLevel user
CRCCheck on
SetCompressor /SOLID lzma

VIProductVersion "1.0.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "3mfthumb"
VIAddVersionKey /LANG=1033 "ProductVersion" "1.0.0"
VIAddVersionKey /LANG=1033 "FileDescription" "3MF thumbnail handler for Windows Explorer"
VIAddVersionKey /LANG=1033 "FileVersion" "1.0.0.0"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (C) 2026 Henry Kroll III, Don Eisele"
VIAddVersionKey /LANG=1033 "Comments" "Shows 3MF previews in Windows Explorer, rendering the model when the file has no embedded thumbnail."

; ---------------------------------------------------------------------------
; Modern UI 2
; ---------------------------------------------------------------------------
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_LICENSEPAGE_RADIOBUTTONS

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Sections
; ---------------------------------------------------------------------------
Section "Windows Explorer thumbnail handler" SecShell
  SectionIn RO

  SetOutPath "$INSTDIR"
  File "staging\3mfthumbprovider.dll"
  File "staging\lib3mf.dll"
  ; The provider itself imports libwinpthread-1.dll, so RegDLL below fails
  ; without it even when the optional command-line tool is not installed.
  File "staging\libwinpthread-1.dll"

  ; Register the COM server (writes only to HKCU\Software\Classes).
  ; RegDLL loads the DLL and calls DllRegisterServer, which also pokes
  ; Explorer with SHCNE_ASSOCCHANGED so it picks up the handler.
  ClearErrors
  RegDLL "$INSTDIR\3mfthumbprovider.dll"
  ${If} ${Errors}
    MessageBox MB_ICONSTOP|MB_OK "Failed to register the thumbnail handler.$\r$\n$\r$\nInstallation is incomplete; the files are in place but Explorer cannot use them. Try reinstalling, or register manually with:$\r$\n  regsvr32 3mfthumbprovider.dll"
  ${EndIf}
SectionEnd

Section "3mfthumb command-line tool" SecCli
!ifdef HAS_CLI
  SetOutPath "$INSTDIR"
  File "staging\3mfthumb.exe"

  ; Start-menu entry for the CLI so users can find it.
  CreateDirectory "$SMPROGRAMS\3mfthumb"
  CreateShortcut "$SMPROGRAMS\3mfthumb\3mfthumb Command Line.lnk" "$INSTDIR\3mfthumb.exe" "" "$INSTDIR\3mfthumb.exe" 0
!endif
SectionEnd

Section "Start Menu shortcuts" SecShortcuts
  SectionIn RO
  CreateDirectory "$SMPROGRAMS\3mfthumb"
  CreateShortcut "$SMPROGRAMS\3mfthumb\Uninstall 3mfthumb.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
SectionEnd

; ---------------------------------------------------------------------------
; Installer
; ---------------------------------------------------------------------------
Section -Post
  SetOutPath "$INSTDIR"

  ; Uninstaller + control-panel entry (per-user, no admin needed).
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "DisplayName" "3mfthumb"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "DisplayVersion" "1.2.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "Publisher" "Henry Kroll III"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "DisplayIcon" "$INSTDIR\3mfthumbprovider.dll"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb" "NoRepair" 1

  WriteRegStr HKCU "Software\3mfthumb" "InstallDir" "$INSTDIR"

  ; Let Explorer know file associations changed.
  System::Call "shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)"
SectionEnd

; ---------------------------------------------------------------------------
; Functions
; ---------------------------------------------------------------------------
Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP|MB_OK "3mfthumb requires 64-bit Windows (amd64)."
    Abort
  ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; Uninstaller
; ---------------------------------------------------------------------------
Section "Uninstall"
  ; Unregister the COM server before deleting the DLL.
  ClearErrors
  UnRegDLL "$INSTDIR\3mfthumbprovider.dll"

  ; Uninstaller entry.
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3mfthumb"
  DeleteRegKey HKCU "Software\3mfthumb"

  ; Files.
  Delete "$INSTDIR\3mfthumbprovider.dll"
  Delete "$INSTDIR\lib3mf.dll"
!ifdef HAS_CLI
  Delete "$INSTDIR\3mfthumb.exe"
!endif
  Delete "$INSTDIR\libwinpthread-1.dll"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  ; Start-menu.
!ifdef HAS_CLI
  Delete "$SMPROGRAMS\3mfthumb\3mfthumb Command Line.lnk"
!endif
  Delete "$SMPROGRAMS\3mfthumb\Uninstall 3mfthumb.lnk"
  RMDir "$SMPROGRAMS\3mfthumb"

  ; Nudge Explorer to drop its cached .3mf handler.
  System::Call "shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)"
SectionEnd
