Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!ifndef VERSION
  !define VERSION "1.0.1"
!endif
Name "Glassify"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Glassify"
InstallDirRegKey HKCU "Software\Glassify" "InstallDirectory"
RequestExecutionLevel user
SetCompressor /SOLID lzma
Icon "..\resources\app.ico"
UninstallIcon "..\resources\app.ico"
VIProductVersion "1.0.1.0"
VIAddVersionKey "ProductName" "Glassify"
VIAddVersionKey "FileDescription" "Glassify per-user installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright 2026 xaicat"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Glassify.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro StopController PREFIX
Function ${PREFIX}StopController
  FindWindow $0 "WinFadeX.Next"
  ${If} $0 != 0
    System::Call 'user32::GetWindowThreadProcessId(p r0, *i .r2) i'
    SendMessage $0 0x8003 4 0 $3 /TIMEOUT=2000
    System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i r2) p .r4'
    System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i r3) p .r5'
    SendMessage $0 0x8003 3 0 /TIMEOUT=2000
    StrCpy $6 0
    ${If} $4 != 0
      System::Call 'kernel32::WaitForSingleObject(p r4, i 10000) i .r6'
      System::Call 'kernel32::CloseHandle(p r4)'
    ${EndIf}
    StrCpy $7 0
    ${If} $5 != 0
      System::Call 'kernel32::WaitForSingleObject(p r5, i 10000) i .r7'
      System::Call 'kernel32::CloseHandle(p r5)'
    ${EndIf}
    ${If} $6 == 258
    ${OrIf} $7 == 258
      IfSilent stop 0
      MessageBox MB_OK|MB_ICONINFORMATION "Glassify is still restoring its windows or taskbar. Wait a moment, then run setup again."
      Goto stop
    ${EndIf}
    retry:
    StrCpy $1 0
    again:
      FindWindow $0 "WinFadeX.Next"
      ${If} $0 == 0
        Return
      ${EndIf}
      Sleep 100
      IntOp $1 $1 + 1
      ${If} $1 < 100
        Goto again
      ${EndIf}
    IfSilent stop 0
    MessageBox MB_RETRYCANCEL|MB_ICONINFORMATION "Quit Glassify or the older WinFadeX preview using its tray menu, then choose Retry." IDRETRY retry
    stop:
      Abort "The running controller must exit before files can change."
  ${EndIf}
FunctionEnd
!macroend
!insertmacro StopController ""
!insertmacro StopController "un."
Function .onInit
  SetShellVarContext current
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "This release requires 64-bit Windows."
    Abort
  ${EndIf}
  Call StopController
FunctionEnd
Function un.onInit
  SetShellVarContext current
  Call un.StopController
FunctionEnd

Section "Glassify (required)" SEC_APP
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\Glassify"
  CreateShortcut "$SMPROGRAMS\Glassify\Glassify.lnk" "$INSTDIR\Glassify.exe"
  CreateShortcut "$SMPROGRAMS\Glassify\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Glassify" "InstallDirectory" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "DisplayName" "Glassify"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "Publisher" "xaicat"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "URLInfoAbout" "https://github.com/xaicat/Glassify"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "DisplayIcon" "$INSTDIR\Glassify.exe,0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify" "NoRepair" 1
SectionEnd
Section /o "Desktop shortcut" SEC_DESKTOP
  CreateShortcut "$DESKTOP\Glassify.lnk" "$INSTDIR\Glassify.exe"
SectionEnd
Section /o "Start when I sign in" SEC_STARTUP
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Glassify" '$\"$INSTDIR\Glassify.exe$\" --background'
SectionEnd
Section "Uninstall"
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Glassify"
  ${If} $0 == '$\"$INSTDIR\Glassify.exe$\" --background'
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Glassify"
  ${EndIf}
  Delete "$SMPROGRAMS\Glassify\Glassify.lnk"
  Delete "$SMPROGRAMS\Glassify\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Glassify"
  Delete "$DESKTOP\Glassify.lnk"
  !include "${UNINSTALL_FILES}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Glassify"
  DeleteRegKey HKCU "Software\Glassify"
  ; User preferences in LocalAppData\Glassify are deliberately retained.
SectionEnd
