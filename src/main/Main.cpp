// Main.cpp
//
// PASSWORD TECH
// Copyright (c) 2002-2023 by Christian Thoeing <c.thoeing@web.de>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//---------------------------------------------------------------------------
#include <vcl.h>
#include <clipbrd.hpp>
#include <Math.hpp>
#include <DateUtils.hpp>
#include <Clipbrd.hpp>
#include <SysUtils.hpp>
#include <StrUtils.hpp>
#include <Registry.hpp>
#include <stdio.h>
#include <shellapi.h>
#include <algorithm>
#include <io.h>
#include <System.Threading.hpp>
#pragma hdrstop

#include "Main.h"
#include "ProgramDef.h"
#include "StringFileStreamW.h"
#include "Language.h"
#include "hrtimer.h"
#include "PasswList.h"
#include "CryptText.h"
#include "Util.h"
#include "PasswEnter.h"
#include "base64.h"
#include "Progress.h"
#include "About.h"
#include "CreateRandDataFile.h"
#include "QuickHelp.h"
#include "FastPRNG.h"
#include "ProfileEditor.h"
#include "CreateTrigramFile.h"
#include "MPPasswGen.h"
#include "Configuration.h"
#include "ProvideEntropy.h"
#include "dragdrop.h"
#include "TopMostManager.h"
#include "PasswManager.h"
#include "InfoBox.h"
#include "TaskCancel.h"
#include "chacha.h"
#include "SendKeys.h"
#include "sha256.h"
#include "sha512.h"
#include "AESCtrPRNG.h"
#include "SecureClipboard.h"
#include "PasswMngColSelect.h"
#include "PasswMngKeyValEdit.h"
#include "PasswMngDbSettings.h"
#include "PasswMngDbProp.h"
#include "PasswMngPwHistory.h"
#include "zxcvbn.h"
#ifdef _WIN64
#include "../crypto/blake2/blake2.h"
#else
#include "../crypto/blake2/ref/blake2.h"
#endif
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma resource "*.dfm"
TMainForm *MainForm;

CmdLineOptions g_cmdLineOptions;
std::unique_ptr<TMemIniFile> g_pIni;
bool g_blFakeIniFile = false;
std::vector<std::unique_ptr<PWGenProfile>> g_profileList;
RandomGenerator* g_pRandSrc = nullptr;
std::unique_ptr<RandomGenerator> g_pKeySeededPRNG;
WString g_sExePath;
WString g_sAppDataPath;
bool g_blConsole = false;
int g_nAppState = 0;
int g_nDisplayDlg = 0;
Configuration g_config;
AnsiString g_asDonorInfo;
WString g_sNewline;
TerminateAction g_terminateAction = TerminateAction::None;

extern HANDLE g_hAppMutex;

const WString
CONFIG_ID             = "Main",
CONFIG_PROFILE_ID     = "PWGenProfile";

const int
ENTROPY_TIMER_MAX     = 8,
ENTROPY_SYSTEM        = 24,
ENTROPY_MOUSECLICK    = 2,
ENTROPY_MOUSEMOVE     = 2,
ENTROPY_MOUSEWHEEL    = 0,
ENTROPY_KEYBOARD      = 1,

LANGUAGE_MAX_ITEMS    = 32,

TIMER_TOUCHPOOL       = 2,
TIMER_RANDOMIZE       = 10,
TIMER_MOVEPOOL        = 300,
TIMER_WRITESEEDFILE   = 457,
// the prime 457 was chosen so that there is a sufficient delay
// between moving the pool and writing a new seed file, and that
// both events will coincide quite late (after ~38 hours)

PASSW_MAX_CHARS       = 10000,
PASSW_MAX_WORDS       = 100,
PASSWFORMAT_MAX_CHARS = 16000,
PASSWSCRIPT_MAX_CHARS = 16000,

LISTS_MAX_ENTRIES     = 50;

const word64
PASSW_MAX_NUM         = 1'000'000'000'000ull,
#ifdef _WIN64
PASSWLIST_MAX_BYTES   = 4000000000;
#else
PASSWLIST_MAX_BYTES   =  500000000;
#endif


const int CHARSETLIST_DEFAULTENTRIES_NUM = 12;
const char* CHARSETLIST_DEFAULTENTRIES[CHARSETLIST_DEFAULTENTRIES_NUM] =
{
  "<AZ><az><09>",
  "<AZ><az><09><symbols>",
  "<AZ>:2+<az><09>:3+<symbols>:1+",
  "<AZ><az><09><symbols><high>",
  "<AZ>:1<az>:1+<09>:1+<symbols>:1<high>:1",
  "<easytoread>",
  "<Hex>",
  "<hex>",
  "<base64>",
  "<phonetic>",
  "<phoneticu>",
  "<phoneticx>"
};

const int FORMATLIST_DEFAULTENTRIES_NUM = 10;
const char* FORMATLIST_DEFAULTENTRIES[FORMATLIST_DEFAULTENTRIES_NUM] =
{
  "{4u4l2ds}",
  "{6ALd}",
  "3[8q ]",
  "*d",
  "*10-20A",
  "6q2ds6q2ds",
  "5[w-2d ]",
  "U9A",
  "32h",
  "5[2h-]2h"
};

const char
WLFNLIST_DEFAULT[] = "<default>",
PASSWORD_CHAR = '*',
PROFILES_MENU_SHORTCUTS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

const int
MAINFORM_TAG_REPAINT_COMBOBOXES = 1,
PASSWBOX_TAG_PASSW_TEST = 1,
PASSWBOX_TAG_PASSW_GEN = 2;

int FindPWGenProfile(const WString& sName)
{
  for (auto it = g_profileList.begin(); it != g_profileList.end(); it++) {
    if (AnsiSameText((*it)->ProfileName, sName))
      return it - g_profileList.begin();
  }
  return -1;
}

WString RemoveCommentAndTrim(WString sStr)
{
  if (sStr.IsEmpty())
    return sStr;
  if (sStr[1] == '[') {
    int nPos = sStr.Pos("]");
    if (nPos > 0)
      sStr.Delete(1, nPos);
  }
  return sStr.Trim();
}

bool IsRandomPoolActive(void)
{
  return typeid(*g_pRandSrc) == typeid(RandomPool);
}

void BeforeDisplayDlg(void)
{
  g_nDisplayDlg++;
}

void AfterDisplayDlg(void)
{
  if (g_nDisplayDlg) g_nDisplayDlg--;
}

bool IsDisplayDlg(void)
{
  return g_nDisplayDlg > 0;
}

bool CheckThreadRunning(void)
{
  return TUpdateCheckThread::ThreadRunning() ||
    TSendKeysThread::ThreadRunning() ||
    TaskCancelManager::GetInstance().GetNumTasksRunning();
}

class TagOverrider {
public:
  TagOverrider(TControl* pControl, int nOverride)
    : m_pControl(pControl), m_nTag(pControl->Tag)
  {
    m_pControl->Tag = nOverride;
  }

  ~TagOverrider()
  {
    m_pControl->Tag = m_nTag;
  }

private:
  TControl* m_pControl;
  int m_nTag;
};


extern "C" int blake2s_self_test(void);

//---------------------------------------------------------------------------
__fastcall TMainForm::TMainForm(TComponent* Owner)
  : TForm(Owner), m_randPool(RandomPool::GetInstance()),
    m_entropyMng(EntropyManager::GetInstance()),
    m_blStartup(true), //m_blRestart(false),
    m_nNumStartupErrors(0), m_passwGen(&RandomPool::GetInstance()),
    m_nAutoClearClipCnt(0), m_nAutoClearPasswCnt(0), m_pUpdCheckThread(nullptr)
{
//  SetSecureMemoryManager();

  Application->OnMessage = AppMessage;
  Application->OnException = AppException;
  Application->OnMinimize = AppMinimize;
  Application->OnRestore = AppRestore;
  Application->OnDeactivate = AppDeactivate;

  Application->Title = PROGRAM_NAME;
  Caption = PROGRAM_NAME;
  LogoLbl->Font->Color = clWindowText;
  TrayIcon->Hint = Caption;
  TrayIcon->Icon = Application->Icon;

  Constraints->MinHeight = Height;
  Constraints->MinWidth = Width;

  // test crypto modules before further initializing...
  if (aes_self_test(0) != 0)
    throw Exception("AES self test failed");
  if (chacha_self_test() != 0)
    throw Exception("ChaCha20 self test failed");
  if (sha256_self_test(0) != 0)
    throw Exception("SHA-256 self test failed");
  if (sha512_self_test(0) != 0)
    throw Exception("SHA-512 self test failed");
  if (blake2s_self_test() != 0)
    throw Exception("BLAKE2 self test failed");
  if (base64_self_test(0) != 0)
    throw Exception("Base64 self test failed");

  // set up PRNGs and related stuff
  HighResTimerCheck();
  g_pRandSrc = &m_randPool;
  m_entropyMng.MaxTimerEntropyBits = ENTROPY_TIMER_MAX;
  m_entropyMng.SystemEntropyBits = ENTROPY_SYSTEM;
  //g_pEntropyMng.reset(new EntropyManager(ENTROPY_TIMER_MAX, ENTROPY_SYSTEM));
  //g_pEntropyMng->AddSystemEntropy();

  SecureClipboard::GetInstance().RegisterOnSetDataFun(OnSetSensitiveClipboardData);

  // initialize the OLE library
  OleInitialize(nullptr);

  RegisterDropWindow(PasswBox->Handle, &m_pPasswBoxDropTarget);

  // set up basic appearance of the program
  //EntropyProgress->MaxValue = RandomPool::MAX_ENTROPY;
  CharsLengthSpinBtn->Max = PASSW_MAX_CHARS;
  WordsNumSpinBtn->Max = PASSW_MAX_WORDS;

  if (g_blConsole) {
    SetConsoleOutputCP(CP_UTF8);
    Caption = Caption + " (console)";
    std::wcout << FormatW("This is %s version %s.", PROGRAM_NAME,
      PROGRAM_VERSION).c_str() << std::endl;
  }

  LoadLangConfig();

  // read the seed file and incorporate contents into the random pool
  // (do this directly after LoadLangConfig() because we can translate
  // error messages now)
  m_sRandSeedFileName = g_sAppDataPath + WString(PROGRAM_RANDSEEDFILE);
  if (FileExists(m_sRandSeedFileName)) {
    if (!m_randPool.ReadSeedFile(m_sRandSeedFileName))
      MsgBox(TRLFormat("Could not read random seed file\n\"%s\".",
          m_sRandSeedFileName.c_str()), MB_ICONERROR);
  }

  // create a seed file NOW or overwrite the existing one
  WriteRandSeedFile();

  // process errors in the command line arguments
  if (!g_cmdLineOptions.UnknownSwitches.IsEmpty()) {
    *g_cmdLineOptions.UnknownSwitches.LastChar() = '.';
    DelayStartupError(TRLFormat("Unknown command line switch(es):\n%s",
        g_cmdLineOptions.UnknownSwitches.c_str()));
  }

  MainMenu_Help_About->Caption = TRLFormat("About %s...", PROGRAM_NAME);
  TrayMenu_About->Caption = MainMenu_Help_About->Caption;
  TrayMenu_Restore->Caption = TRLFormat("Restore %s", PROGRAM_NAME);
  //m_sEntropyBitsLbl = TRL("Entropy bits:");

  m_sCharSetHelp = TQuickHelpForm::FormatString(FormatW(
        "%s\n"
        "<AZ> = A..Z\t<base64>,<b64> = <AZ><az><09>, +, /\n"
        "<az> = a..z\t<easytoread>,<etr> = <AZ><az><09> %s\n"
        "<09> = 0..9\t<symbols>,<sym> = %s (!\"#$%%...)\n"
        "<Hex> = 0..9, A..F\t<brackets>,<brac> = %s (()[]{}<>)\n"
        "<hex> = 0..9, a..f\t<punctuation>,<punct> = %s (,.;:)\n"
        "<highansi>,<high> = %s\n"
        "<phonetic> = %s\n"
        "<phoneticu> = %s\n"
        "<phoneticx> = %s\n\n"
        "%s\n"
        "<placeholder>:N[+]\n"
        "%s\n\n"
        "%s",
        TRL("You can use the following placeholders:").c_str(),
        TRL("without ambiguous characters").c_str(),
        TRL("Special symbols").c_str(),
        TRL("Brackets").c_str(),
        TRL("Punctuation marks").c_str(),
        TRL("Higher ANSI characters").c_str(),
        TRL("Generate phonetic password (lower-case letters)").c_str(),
        TRL("Generate phonetic password (upper-case letters)").c_str(),
        TRL("Generate phonetic password (mixed-case letters)").c_str(),
        TRL("You can use the following syntax to specify a (minimum) number N "
          "of characters\nfor a character set:").c_str(),
        TRL("N = Include exactly N characters. N+ = Include at least N characters.").c_str(),
        TRL("Comments may be provided in square brackets \"[...]\"\n"
          "at the beginning of the sequence.").c_str()
      ));

  m_sFormatPasswHelp = TQuickHelpForm::FormatString(FormatW(
        "%s\n\n"
        "%s\n\n"
        "x = %s\tv = %s (aeiou)\n"
        "a = a..z, 0..9\tV = %s\n"
        "A = A..Z, a..z, 0..9\tZ = %s\n"
        "U = A..Z, 0..9\tc = %s (bcdf...)\n"
        "d = 0..9\tC = %s\n"
        "h = 0..9, a..f\tz = %s\n"
        "H = 0..9, A..F\tp = %s (,.;:)\n"
        "l = a..z\tb = %s (()[]{}<>)\n"
        "L = A..Z, a..z\ts = %s (!\"#$%%...)\n"
        "u = A..Z\tS = A + %s\n"
        "y = %s\tE = A %s\n"
        "q = %s\n"
        "Q = %s\n"
        "r = %s\n\n",
        TRL("Format specifiers have the form \"[*][N]x\" or \"[*][M-N]x\", where the\n"
          "optional argument \"N\" specifies the number of repetitions (between 1 and\n"
          "99999), \"M-N\" specifies a random number of repetitions in the range\n"
          "from M to N, and \"x\" is the actual format specifier/placeholder. The\n"
          "optional asterisk (*) means that each character must occur only once in\n"
          "the sequence. Example: \"16u\" means \"Insert 16 random upper-case letters\n"
          "into the password\".").c_str(),
        TRL("List of placeholders with optional argument \"N\":").c_str(),
        TRL("Custom character set").c_str(),
        TRL("Vowels, lower-case").c_str(),
        TRL("Vowels, mixed-case").c_str(),
        TRL("Vowels, upper-case").c_str(),
        TRL("Consonants, lower-case").c_str(),
        TRL("Consonants, mixed-case").c_str(),
        TRL("Consonants, upper-case").c_str(),
        TRL("Punctuation marks").c_str(),
        TRL("Brackets").c_str(),
        TRL("Special symbols").c_str(),
        TRL("Special symbols").c_str(),
        TRL("Higher ANSI characters").c_str(),
        TRL("without ambiguous characters").c_str(),
        TRL("Generate phonetic password (lower-case letters)").c_str(),
        TRL("Generate phonetic password (upper-case letters)").c_str(),
        TRL("Generate phonetic password (mixed-case letters)") .c_str()
      ));

  m_sFormatPasswHelp += ConvertCr2Crlf(FormatW(
        "%s\n\n"
        "P = %s\n"
        "{N}w = %s\n"
        "{N}W = %s\n"
        "{N}[...] = %s\n"
        "{N}{...} = %s\n"
        "{N}<<...>> = %s\n\n"
        "%s\n"
        "* %s\n"
        "* %s\n",
        TRL("List of special format specifiers\n(possible usage of argument \"N\" "
          "is indicated by \"{N}\"):").c_str(),
        TRL("Insert password generated via \"Include characters/words\"").c_str(),
        TRL("Words from word list, multiple words are separated by spaces").c_str(),
        TRL("Words, multiple words are not separated at all").c_str(),
        TRL("Repeat input sequence in brackets N times").c_str(),
        TRL("Randomly permute formatted sequence in brackets, keep N characters").c_str(),
        TRL("Treat sequence as character set and insert N characters").c_str(),
        TRL("Additional notes:").c_str(),
        TRL("Text enclosed with quotation marks \"...\" is copied verbatim\nto the password.").c_str(),
        TRL("Comments may be provided in square brackets \"[...]\"\n"
          "at the beginning of the sequence.").c_str()
      ));

  PasswSecurityBarPanel->Width = 0;

  OpenDlg->Filter = FormatW("%s (*.*)|*.*|%s (*.txt)|*.txt|%s (*.tgm)|"
      "*.tgm|%s (*.lua)|*.lua",
      TRL("All files").c_str(),
      TRL("Text files").c_str(),
      TRL("Trigram files").c_str(),
      TRL("Lua scripts").c_str());

  SaveDlg->Filter = OpenDlg->Filter;

  // load configuration from INI file
  LoadConfig();
}
//---------------------------------------------------------------------------
__fastcall TMainForm::~TMainForm()
{
  DeactivateHotKeys();

  PasswBox->Tag = 0;
  ClearEditBoxTextBuf(PasswBox, 256);

  // remove the PasswBox from the list of drop targets
  UnregisterDropWindow(PasswBox->Handle, m_pPasswBoxDropTarget);

  // uninitialize the OLE library
  OleUninitialize();

  CloseHandle(g_hAppMutex);

  // restart the program
  if (g_terminateAction == TerminateAction::RestartProgram)
    ExecuteShellOp(Application->ExeName, false);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormClose(TObject *Sender, TCloseAction &Action)
{
  if (MainMenu_Options_SaveSettingsOnExit->Checked && !g_cmdLineOptions.ConfigReadOnly
     || g_terminateAction == TerminateAction::RestartProgram)
    SaveConfig();

  WriteRandSeedFile();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::StartupAction(void)
{
  if (m_blStartup) {
    m_blStartup = false;

    // load profile specified by command line parameter or program configuration
    if (!g_cmdLineOptions.ProfileName.IsEmpty() ||
        (g_config.LoadProfileStartup && !g_config.LoadProfileName.IsEmpty())) {
      WString sProfileName = !g_cmdLineOptions.ProfileName.IsEmpty() ?
        g_cmdLineOptions.ProfileName : g_config.LoadProfileName;
      if (LoadProfile(sProfileName)) {
        if (!g_blConsole) {
          ProfileList->ItemIndex = FindPWGenProfile(sProfileName);
        }
      }
      else {
        WString sErrMsg = TRLFormat(
          "Profile \"%s\" not found.", sProfileName.c_str());
        if (!g_blConsole || g_cmdLineOptions.GenNumPassw == 0)
          DelayStartupError(sErrMsg);
        else {
          sErrMsg = TRL("Error") + " - " + sErrMsg;
          std::wcout << WStringToUtf8(sErrMsg).c_str() << std::endl;
        }
        if (g_cmdLineOptions.ProfileName.IsEmpty()) {
          g_config.LoadProfileStartup = false;
          g_config.LoadProfileName = WString();
        }
        else
          g_cmdLineOptions.ProfileName = WString();
      }
    }

    if (g_cmdLineOptions.GenNumPassw > 0) {
      if (g_blConsole) {
        if (!g_cmdLineOptions.ProfileName.IsEmpty())
          //wprintf((TRL("Selected profile \"%s\".")+"\n").c_str(), m_sCmdLineProfileName.c_str());
          std::wcout << WStringToUtf8(TRLFormat("Selected profile \"%s\".",
            g_cmdLineOptions.ProfileName.c_str())).c_str() << std::endl;
        //wprintf((TRL("Generating %d password(s) ...")+"\n\n").c_str(), m_nCmdLineNumOfPassw);
        std::wcout << WStringToUtf8(TRLFormat("Generating %d password(s) ...",
          g_cmdLineOptions.GenNumPassw)).c_str() << std::endl << std::endl;
        GeneratePassw(gpdConsole);
        Close();
        return;
      }
      else
        DelayStartupError(TRLFormat("Command line function \"gen\" is only effective\n"
            "when %s is run from the console.", PROGRAM_NAME));
    }

    WString sDefaultGUIFontStr = FontToString(Font);

    if (g_config.GUIFontString.IsEmpty())
      g_config.GUIFontString = sDefaultGUIFontStr;
    else if (!SameText(g_config.GUIFontString, sDefaultGUIFontStr))
      ChangeGUIFont(g_config.GUIFontString);

    PasswOptionsDlg->MaxWordLenSpinBtn->Max = WORDLIST_MAX_WORDLEN;
    PasswOptionsDlg->SetOptions(m_passwOptions);
    SetAdvancedBtnCaption();

    ConfigurationDlg->SetLanguageList(m_languages);
    ConfigurationDlg->SetOptions(g_config);

    AboutForm->SetDonorUI();
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormActivate(TObject *Sender)
{
  static bool blFirstTime = true;
  if (blFirstTime) {
    blFirstTime = false;

    Application->BringToFront();

    MainMenu_Options_AlwaysOnTopClick(this);

    if (m_nNumStartupErrors != 0) {
      MsgBox(TRLFormat("%s encountered %d error(s) during startup:",
          PROGRAM_NAME, m_nNumStartupErrors) + m_sStartupErrors, MB_ICONWARNING);
      m_sStartupErrors = WString();
    }

    if (m_asDonorKey.IsEmpty() &&
        (g_pIni->ReadString(CONFIG_ID, "LastVersion", "") != PROGRAM_VERSION ||
        fprng_rand(15) == 0))
      MsgBox(TRLFormat("If you like %s, please consider\nmaking a donation. Thank you!",
          PROGRAM_NAME), MB_ICONINFORMATION);

    if (g_config.AutoCheckUpdates != acuDisabled) {
      TDateTime now = TDateTime::CurrentDate();
      m_lastUpdateCheck =
        g_pIni->ReadDate(CONFIG_ID, "LastUpdateCheck", TDateTime());

      bool blNeedCheck = false;
      switch (g_config.AutoCheckUpdates) {
      case acuDaily:
        blNeedCheck = DaysBetween(now, m_lastUpdateCheck) > 0;
        break;
      case acuWeekly:
        blNeedCheck = WeeksBetween(now, m_lastUpdateCheck) > 0;
        break;
      case acuMonthly:
        blNeedCheck = MonthsBetween(now, m_lastUpdateCheck) > 0;
        break;
      default:
        break;
      }

      if (blNeedCheck) {
        MainMenu_Help_CheckForUpdates->Enabled = false;
        m_pUpdCheckThread = new TUpdateCheckThread(OnUpdCheckThreadTerminate);
        //m_pUpdCheckThread->OnTerminate = OnUpdCheckThreadTerminate;
        //m_blUpdCheckThreadRunning = true;
      }
    }

    if (g_config.Database.OpenWindowOnStartup ||
        !g_cmdLineOptions.PasswDbFileName.IsEmpty())
      PasswMngForm->Show();
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormPaint(TObject *Sender)
{
  if (Tag == MAINFORM_TAG_REPAINT_COMBOBOXES) {
    Tag = 0;
    if (ActiveControl != CharSetList)
      CharSetList->SelLength = 0;
    if (ActiveControl != WLFNList)
      WLFNList->SelLength = 0;
    if (ActiveControl != FormatList)
      FormatList->SelLength = 0;
    if (ActiveControl != ScriptList)
      ScriptList->SelLength = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::OnUpdCheckThreadTerminate(TObject* Sender)
{
  if (m_pUpdCheckThread->Result != TUpdateCheckThread::CheckResult::Error)
    m_lastUpdateCheck = TDateTime::CurrentDate();

  MainMenu_Help_CheckForUpdates->Enabled = true;

  //m_blUpdCheckThreadRunning = false;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::DelayStartupError(const WString& sMsg)
{
  m_nNumStartupErrors++;
  m_sStartupErrors += FormatW("\n\n%d) %s", m_nNumStartupErrors, sMsg.c_str());
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::LoadLangConfig(void)
{
  // which language do we have to set?
  WString sLangStr = g_pIni->ReadString(CONFIG_ID, "Language", "");
  if (sLangStr.IsEmpty())
    sLangStr = LANGUAGE_DEFAULT_CODE;

  bool blDefaultLang = sLangStr == LANGUAGE_DEFAULT_CODE ||
    SameText(sLangStr, WString(LANGUAGE_DEFAULT_NAME));

  LanguageEntry e;
  e.Code = LANGUAGE_DEFAULT_CODE;
  e.Name = LANGUAGE_DEFAULT_NAME;
  e.Version = PROGRAM_VERSION;
  m_languages.push_back(e);

  // look for language files in the program directory
  WString sFindFile = g_sExePath + "*.*";
  TSearchRec srw;

  WString sLangFileName;
  int nLangIndex = 0;

  if (FindFirst(sFindFile, faAnyFile, srw) == 0) {
    do {
      WString sExt = ExtractFileExt(srw.Name);
      if (!SameText(sExt, ".lng") && !SameText(sExt, ".po"))
        continue;

      WString sFileName = g_sExePath + ExtractFileName(srw.Name);

      try {
        LanguageSupport ls(sFileName, true);

        if (std::find_if(m_languages.begin(), m_languages.end(),
              [&ls](const LanguageEntry& e) { return e.Code == ls.LanguageCode; })
              != m_languages.end())
          continue;

        LanguageEntry e;
        e.FileName = sFileName;
        e.Code = ls.LanguageCode;
        e.Name = ls.LanguageName;
        e.Version = ls.LanguageVersion;

        m_languages.push_back(e);

        if (!blDefaultLang && nLangIndex == 0 && e.Code == sLangStr)
        {
          sLangFileName = sFileName;
          nLangIndex = m_languages.size() - 1;
        }
      }
      catch (ELanguageError& e) {
        DelayStartupError(srw.Name + ": " + e.Message + ".");
      }
      catch (...) {
      }
    }
    while (FindNext(srw) == 0 && m_languages.size() < LANGUAGE_MAX_ITEMS);

    FindClose(srw);
  }

  // nothing found?
  if (!blDefaultLang && nLangIndex == 0)
    DelayStartupError(FormatW("Could not find language \"%s\".", sLangStr.c_str()));

  // change language if necessary
  if (nLangIndex != 0 && !ChangeLanguage(sLangFileName))
    nLangIndex = 0;

  m_sHelpFileName = g_sExePath;
  if (g_pLangSupp && !g_pLangSupp->HelpFileName.IsEmpty())
    m_sHelpFileName += g_pLangSupp->HelpFileName;
  else
    m_sHelpFileName += PROGRAM_HELPFILE;

  g_config.LanguageIndex = nLangIndex;
  //MainMenu_Options_Language->Items[nLangIndex]->Checked = true;
}
//---------------------------------------------------------------------------
bool __fastcall TMainForm::ChangeLanguage(const WString& sLangFileName)
{
  try {
    g_pLangSupp = std::make_unique<LanguageSupport>(sLangFileName);

    for (int nI = 0; nI < 4; nI++)
      TRLS(g_msgBoxCaptionList[nI]);

    TRLCaption(ProfileLbl);
    TRLCaption(SettingsGroup);
    TRLCaption(IncludeCharsCheck);
    TRLCaption(CharsLengthLbl);
    TRLCaption(CharSetLbl);
    TRLCaption(IncludeWordsCheck);
    TRLCaption(WordsNumLbl);
    TRLCaption(WordListFileLbl);
    TRLCaption(CombineWordsCharsCheck);
    TRLCaption(SpecifyLengthCheck);
    TRLCaption(FormatPasswCheck);
    TRLCaption(RunScriptCheck);
    TRLCaption(MultiplePasswLbl);
    TRLCaption(GenerateBtn2);
    TRLCaption(PasswGroup);
    TRLCaption(GenerateBtn);
    TRLCaption(PasswInfoLbl);

    TRLMenu(MainMenu);
    TRLMenu(ListMenu);
    TRLMenu(TrayMenu);
    TRLMenu(PasswBoxMenu);
    TRLMenu(AdvancedOptionsMenu);
    TRLMenu(GenerateMenu);

    TRLHint(HelpBtn);
    TRLHint(ClearClipBtn);
    TRLHint(ConfigBtn);
    TRLHint(CryptTextBtn);
    TRLHint(ProfileEditorBtn);
    TRLHint(MPPasswGenBtn);
    TRLHint(PasswMngBtn);
    TRLHint(CharSetHelpBtn);
    TRLHint(CharSetInfoBtn);
    TRLHint(BrowseBtn);
    TRLHint(WordListInfoBtn);
    TRLHint(FormatPasswHelpBtn);
    //TRLHint(GenerateBtn3);
    TRLHint(TogglePasswBtn);
    TRLHint(PasswSecurityBar);
    TRLHint(ReloadProfileBtn);
    TRLHint(AddProfileBtn);
    TRLHint(ReloadScriptBtn);
    TRLHint(BrowseBtn2);
  }
  catch (Exception& e) {
    DelayStartupError(FormatW("Error while loading language file\n\"%s\":\n%s.",
        sLangFileName.c_str(), e.Message.c_str()));
    if (g_pLangSupp)
      g_pLangSupp.reset();
    return false;
  }
  return true;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WriteRandSeedFile(bool blShowError)
{
  if (!g_cmdLineOptions.ConfigReadOnly) {
    if (!m_randPool.WriteSeedFile(m_sRandSeedFileName) && blShowError) {
      WString sMsg = TRLFormat("Could not write to random seed file\n\"%s\".",
          m_sRandSeedFileName.c_str());
      if (m_blStartup)
        DelayStartupError(sMsg);
      else
        MsgBox(sMsg, MB_ICONERROR);
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::SetDonorUI(int nDonorType)
{
  WString sInfo = WString(PROGRAM_NAME) + " " + WString(PROGRAM_VERSION);
  if (nDonorType < 0)
    sInfo += " (Community)";
  else
    sInfo += ((nDonorType == DONOR_TYPE_PRO) ? " (DONOR PRO)" : " (DONOR)");
  StatusBar->Panels->Items[0]->Text = sInfo;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::LoadConfig(void)
{
  //AnsiString asLastVersion = g_pIni->ReadString(CONFIG_ID, "LastVersion", "");

  AnsiString asDonorKey = g_pIni->ReadString(CONFIG_ID, "DonorKey", "");
  if (!asDonorKey.IsEmpty()) {
    AnsiString asDonorId;
    int nDonorType;
    switch (CheckDonorKey(asDonorKey, &asDonorId, &nDonorType)) {
    case DONOR_KEY_VALID:
      m_asDonorKey = asDonorKey.Trim();
      g_asDonorInfo = asDonorId;
      SetDonorUI(nDonorType);
      break;
    case DONOR_KEY_EXPIRED:
      DelayStartupError(TRL("Your donor key has expired: The maximum\n"
          "number of updates has been reached."));
      break;
    default:
      DelayStartupError(TRL("Donor key is invalid."));
    }
  }

  if (m_asDonorKey.IsEmpty())
    SetDonorUI(-1);

  int nTop = g_pIni->ReadInteger(CONFIG_ID, "WindowTop", INT_MAX);
  int nLeft = g_pIni->ReadInteger(CONFIG_ID, "WindowLeft", INT_MAX);
  if (nTop == INT_MAX || nLeft == INT_MAX)
    Position = poScreenCenter;
  else {
    Top = nTop;
    Left = nLeft;
  }

  Height = g_pIni->ReadInteger(CONFIG_ID, "WindowHeight", Height);
  Width = g_pIni->ReadInteger(CONFIG_ID, "WindowWidth", Width);

  MainMenu_Options_AlwaysOnTop->Checked =
    g_pIni->ReadBool(CONFIG_ID, "AlwaysOnTop", false);

  IncludeCharsCheck->Checked = g_pIni->ReadBool(CONFIG_ID, "IncludeChars", true);

  CharsLengthSpinBtn->Position = static_cast<short>
    (g_pIni->ReadInteger(CONFIG_ID, "CharsLength", 16));

  // load the advanced password options before loading the character set!
  WString sPasswOptions = g_pIni->ReadString(CONFIG_ID,
    "AdvancedPasswOptions", "");
  m_passwOptions.Flags = StrToIntDef(sPasswOptions, 0);

  m_passwOptions.AmbigChars = g_pIni->ReadString(CONFIG_ID, "AmbigChars", "");
  m_passwOptions.SpecialSymbols = g_pIni->ReadString(CONFIG_ID, "SpecialSym", "");
  m_passwOptions.TrigramFileName = g_pIni->ReadString(CONFIG_ID, "TrigramFile", "");
  if (LoadTrigramFile(m_passwOptions.TrigramFileName) <= 0)
    m_passwOptions.TrigramFileName = WString();

  CharSetList->Items->CommaText = g_pIni->ReadString(CONFIG_ID,
    "CharSetListEntries", "");

  int nI;
  if (CharSetList->Items->Count == 0) {
    for (nI = 0; nI < CHARSETLIST_DEFAULTENTRIES_NUM; nI++)
      CharSetList->Items->Add(CHARSETLIST_DEFAULTENTRIES[nI]);
  }
  else {
    while (CharSetList->Items->Count > LISTS_MAX_ENTRIES)
      CharSetList->Items->Delete(CharSetList->Items->Count - 1);
  }

  if (LoadCharSet(CharSetList->Items->Strings[0]) == 0) {
    DelayStartupError(TRLFormat("Character set \"%s\" is invalid.",
        CharSetList->Items->Strings[0].c_str()));
    CharSetList->Items->Strings[0] = CHARSETLIST_DEFAULTENTRIES[0];
    LoadCharSet(CHARSETLIST_DEFAULTENTRIES[0]);
  }
  CharSetList->ItemIndex = 0;

  int nMaxWordLen = g_pIni->ReadInteger(CONFIG_ID, "MaxWordLen", WORDLIST_MAX_WORDLEN);
  if (nMaxWordLen < 1 || nMaxWordLen > WORDLIST_MAX_WORDLEN)
    nMaxWordLen = WORDLIST_MAX_WORDLEN;
  m_passwOptions.MaxWordLen = nMaxWordLen;

  IncludeWordsCheck->Checked = g_pIni->ReadBool(CONFIG_ID, "IncludeWords", false);

  WordsNumSpinBtn->Position = short(g_pIni->ReadInteger(CONFIG_ID, "WordsNum", 8));

  WLFNList->Items->CommaText = g_pIni->ReadString(CONFIG_ID, "WLFNListEntries", "");

  if (WLFNList->Items->Count == 0) {
    WLFNList->Items->Add(WLFNLIST_DEFAULT);
    LoadWordListFile();
  }
  else if (WLFNList->Items->Strings[0] == WString(WLFNLIST_DEFAULT))
    LoadWordListFile();
  else {
    WString sFileName = WLFNList->Items->Strings[0];
    if (LoadWordListFile(sFileName) <= 0) {
      DelayStartupError(TRLFormat("Error while loading word list file\n\"%s\".",
          sFileName.c_str()));
      WLFNList->Items->Strings[0] = WLFNLIST_DEFAULT;
      LoadWordListFile();
    }
  }

  while (WLFNList->Items->Count > LISTS_MAX_ENTRIES)
    WLFNList->Items->Delete(WLFNList->Items->Count - 1);

  WLFNList->ItemIndex = 0;

  CombineWordsCharsCheck->Checked = g_pIni->ReadBool(CONFIG_ID,
      "CombineWordsChars", false);

  SpecifyLengthCheck->Checked = g_pIni->ReadBool(CONFIG_ID, "PassphrSpecLen", false);
  SpecifyLengthBox->Text = g_pIni->ReadString(CONFIG_ID, "PassphrSpecLenString", "");

  FormatPasswCheck->Checked = g_pIni->ReadBool(CONFIG_ID, "FormatPassw", false);

  FormatList->Items->CommaText = g_pIni->ReadString(CONFIG_ID, "FormatListEntries", "");

  if (FormatList->Items->Count == 0) {
    for (nI = 0; nI < FORMATLIST_DEFAULTENTRIES_NUM; nI++)
      FormatList->Items->Add(FORMATLIST_DEFAULTENTRIES[nI]);
  }
  else {
    while (FormatList->Items->Count > LISTS_MAX_ENTRIES)
      FormatList->Items->Delete(FormatList->Items->Count - 1);
  }

  FormatList->ItemIndex = 0;

  RunScriptCheck->Checked = g_pIni->ReadBool(CONFIG_ID, "RunScript", false);
  ScriptList->Items->CommaText = g_pIni->ReadString(CONFIG_ID, "ScriptListEntries", "");
  while (ScriptList->Items->Count > LISTS_MAX_ENTRIES)
      ScriptList->Items->Delete(ScriptList->Items->Count - 1);

  ScriptList->ItemIndex = 0;

  NumPasswBox->Text = g_pIni->ReadString(CONFIG_ID, "NumPassw", "100");
  //NumPasswBoxExit(this);

  TogglePasswBtn->Down = g_pIni->ReadBool(CONFIG_ID, "HidePassw", false);
  TogglePasswBtnClick(this);

  m_blShowEntProgress = !g_pIni->ReadBool(CONFIG_ID, "HideEntropyProgress", false);
  MainMenu_Options_HideEntProgress->Checked = !m_blShowEntProgress;

  g_config.AutoClearClip = g_pIni->ReadBool(CONFIG_ID, "AutoClearClipboard", true);
  g_config.AutoClearClipTime = g_pIni->ReadInteger(CONFIG_ID,
      "AutoClearClipboardTime", AUTOCLEARCLIPTIME_DEFAULT);
  SecureClipboard::GetInstance().AutoClear = g_config.AutoClearClip;

  if (g_config.AutoClearClipTime < AUTOCLEARCLIPTIME_MIN ||
      g_config.AutoClearClipTime > AUTOCLEARCLIPTIME_MAX)
    g_config.AutoClearClipTime = AUTOCLEARCLIPTIME_DEFAULT;

  g_config.AutoClearPassw = g_pIni->ReadBool(CONFIG_ID, "AutoClearPasswBox", true);
  g_config.AutoClearPasswTime = g_pIni->ReadInteger(CONFIG_ID,
    "AutoClearPasswBoxTime", AUTOCLEARPASSWTIME_DEFAULT);
  if (g_config.AutoClearPasswTime < AUTOCLEARPASSWTIME_MIN ||
      g_config.AutoClearPasswTime > AUTOCLEARPASSWTIME_MAX)
    g_config.AutoClearPasswTime = AUTOCLEARPASSWTIME_DEFAULT;

  int nCipher = g_pIni->ReadInteger(CONFIG_ID, "RandomPoolCipher",
    static_cast<int>(RandomPool::CipherType::ChaCha20));
  if (nCipher >= 0 && nCipher <= static_cast<int>(RandomPool::CipherType::ChaCha8))
  {
    g_config.RandomPoolCipher = nCipher;
    m_randPool.ChangeCipher(static_cast<RandomPool::CipherType>(nCipher));
  }

  g_config.TestCommonPassw = g_pIni->ReadBool(CONFIG_ID, "TestCommonPassw", true);
  if (g_config.TestCommonPassw) {
    try {
      std::unique_ptr<TStringFileStreamW> pFile(
        new TStringFileStreamW(g_sExePath + "common_passwords.txt", fmOpenRead));
      const int BUF_SIZE = 256;
      wchar_t buf[BUF_SIZE];
      while (pFile->ReadString(buf, BUF_SIZE) != 0 &&
             m_commonPassw.size() < 1000000) {
        WString sPassw = Trim(buf);
        m_commonPassw.insert(sPassw.c_str());
      }
      if (!m_commonPassw.empty())
        m_nCommonPasswEntropy = FloorEntropyBits(
          Log2(static_cast<double>(m_commonPassw.size())));
    }
    catch (Exception& e) {
      m_commonPassw.clear();
      DelayStartupError(TRLFormat("Error while loading list of common passwords:\n%s.",
          e.Message.c_str()));
    }
  }

  g_config.UseAdvancedPasswEst = g_pIni->ReadBool(CONFIG_ID,
    "UseAdvancedPasswEst", true);

  g_config.ShowSysTrayIconConst = g_pIni->ReadBool(CONFIG_ID,
    "SystemTrayIconShowConst", true);

  if ((!g_blConsole || g_cmdLineOptions.GenNumPassw == 0) &&
      (!Application->ShowMainForm || g_config.ShowSysTrayIconConst)) {
    TrayIcon->Visible = true;
  }

  g_config.MinimizeToSysTray = g_pIni->ReadBool(CONFIG_ID,
      "SystemTrayIconMinimize", true);

  g_config.MinimizeAutotype = g_pIni->ReadBool(CONFIG_ID, "MinimizeAutotype", true);

  g_config.AutotypeDelay = g_pIni->ReadInteger(CONFIG_ID, "AutotypeDelay", 75);

  g_config.ConfirmExit = g_pIni->ReadBool(CONFIG_ID, "ConfirmExit", false);

  g_config.LaunchSystemStartup = g_pIni->ReadBool(CONFIG_ID, "LaunchSystemStartup",
    false);

  g_config.LoadProfileStartup = g_pIni->ReadBool(CONFIG_ID, "LoadProfileStartup",
    false);

  g_config.LoadProfileName = g_pIni->ReadString(CONFIG_ID,
    "LoadProfileStartupName", "");

  MainMenu_Options_SaveSettingsOnExit->Checked = g_pIni->ReadBool(CONFIG_ID,
      "SaveSettingsOnExit", true);

  PasswBoxMenu_Editable->Checked = g_pIni->ReadBool(CONFIG_ID,
      "PasswEditable", true);
  PasswBoxMenu_EnablePasswTest->Checked = g_pIni->ReadBool(CONFIG_ID,
      "EnablePasswTest", true);
  PasswBoxMenu_EditableClick(this);
  PasswBoxMenu_EnablePasswTestClick(this);

  StringToFont(g_pIni->ReadString(CONFIG_ID, "PasswBoxFont", ""), PasswBox->Font);
  FontDlg->Font = PasswBox->Font;

  IncludeCharsCheckClick(this);

  //TShortCut defaultHotKey = ShortCut('P', TShiftState() << ssShift << ssCtrl);
  AnsiString asHotKeys = g_pIni->ReadString(CONFIG_ID, "HotKeyList", "");
  if (asHotKeys.Length() >= 5) {
    char* p = asHotKeys.c_str();
    do {
      TShortCut hotKey = static_cast<TShortCut>(strtol(p, &p, 10));
      if (*p == '\0' || hotKey == 0)
        break;

      HotKeyEntry hke;
      hke.Action = static_cast<HotKeyAction>(strtol(++p, &p, 10));
      if (*p == '\0' || hke.Action < hkaNone || hke.Action > hkaSearchDatabaseAutotype)
        break;

      hke.ShowMainWin = static_cast<bool>(strtol(++p, &p, 10));

      if (hke.Action != hkaNone || hke.ShowMainWin)
        g_config.HotKeys.emplace(hotKey, hke);
    } while (*p++ != '\0' && g_config.HotKeys.size() < HOTKEYS_MAX_NUM);

    ActivateHotKeys(g_config.HotKeys);
  }

  CharacterEncoding fileEncoding = static_cast<CharacterEncoding>(
      g_pIni->ReadInteger(CONFIG_ID, "FileEncoding", ceUtf8));
  if (fileEncoding < ceAnsi || fileEncoding > ceUtf8)
    fileEncoding = ceUtf8;

  g_config.FileEncoding = fileEncoding;

  NewlineChar newline = static_cast<NewlineChar>(g_pIni->ReadInteger(
    CONFIG_ID, "FileNewlineChar", nlcWindows));
  if (newline < nlcWindows || newline > nlcUnix)
    newline = nlcWindows;

  g_config.FileNewlineChar = newline;
  g_sNewline = NewlineCharToString(newline);

  g_config.AutoCheckUpdates = AutoCheckUpdates(
    g_pIni->ReadInteger(CONFIG_ID, "AutoCheckUpdates", acuWeekly));
  if (g_config.AutoCheckUpdates < acuDaily ||
      g_config.AutoCheckUpdates > acuDisabled)
    g_config.AutoCheckUpdates = acuWeekly;

  WString sGUIFontStr = g_pIni->ReadString(CONFIG_ID, "GUIFont", "");
  int nPos = sGUIFontStr.Pos(";");
  if (nPos >= 2) {
    g_config.GUIFontString = sGUIFontStr;
  }

  // read the profiles and delete all profile entries afterwards
  for (nI = 0; g_profileList.size() < PROFILES_MAX_NUM; nI++) {
    WString sProfileId = CONFIG_PROFILE_ID + IntToStr(nI);
    if (!g_pIni->SectionExists(sProfileId))
      break;

    WString sProfileName = g_pIni->ReadString(sProfileId, "ProfileName", "");
    if (sProfileName.IsEmpty())
      continue;

    //bool blNameExists = FindPWGenProfile(sProfileName) >= 0;

    if (FindPWGenProfile(sProfileName) >= 0)
      continue;

    auto pProfile = std::make_unique<PWGenProfile>();
    pProfile->ProfileName = sProfileName;
    pProfile->IncludeChars = g_pIni->ReadBool(sProfileId, "IncludeChars", false);
    pProfile->CharsLength = g_pIni->ReadInteger(sProfileId, "CharsLength", 1);
    pProfile->CharSet = g_pIni->ReadString(sProfileId, "CharSet", "");
    pProfile->IncludeWords = g_pIni->ReadBool(sProfileId, "IncludeWords", false);
    pProfile->WordsNum = g_pIni->ReadInteger(sProfileId, "WordsNum", 1);
    pProfile->WordListFileName = g_pIni->ReadString(sProfileId, "WordListFileName", "");
    pProfile->CombineWordsChars = g_pIni->ReadBool(sProfileId, "CombineWordsChars",
        false);
    pProfile->SpecifyLength = g_pIni->ReadBool(sProfileId, "PassphrSpecLen", false);
    pProfile->SpecifyLengthString = g_pIni->ReadString(sProfileId,
        "PassphrSpecLenString", "");
    pProfile->FormatPassw = g_pIni->ReadBool(sProfileId, "FormatPassw", false);
    pProfile->FormatString = g_pIni->ReadString(sProfileId, "FormatString", "");
    pProfile->RunScript = g_pIni->ReadBool(sProfileId, "RunScript", false);
    pProfile->ScriptFileName = g_pIni->ReadString(sProfileId, "ScriptFileName", "");
    pProfile->NumPassw = g_pIni->ReadInteger(sProfileId, "NumPassw", 2);

    sPasswOptions = g_pIni->ReadString(sProfileId, "AdvancedPasswOptions", "");
    pProfile->AdvancedOptionsUsed = !sPasswOptions.IsEmpty();

    if (pProfile->AdvancedOptionsUsed) {
      pProfile->AdvancedPasswOptions.Flags = StrToIntDef(sPasswOptions, 0);
      pProfile->AdvancedPasswOptions.AmbigChars =
        g_pIni->ReadString(sProfileId, "AmbigChars", "");
      pProfile->AdvancedPasswOptions.SpecialSymbols =
        g_pIni->ReadString(sProfileId, "SpecialSym", "");
      pProfile->AdvancedPasswOptions.MaxWordLen =
        g_pIni->ReadInteger(sProfileId, "MaxWordLen", WORDLIST_MAX_WORDLEN);
      pProfile->AdvancedPasswOptions.TrigramFileName =
        g_pIni->ReadString(sProfileId, "TrigramFile", "");
    }

    g_profileList.push_back(std::move(pProfile));
  }

  UpdateProfileControls();
}
//---------------------------------------------------------------------------
bool __fastcall TMainForm::SaveConfig(void)
{
  if (g_blFakeIniFile)
    return true;

  try {
    g_pIni->Clear();

    g_pIni->WriteString(CONFIG_ID, "LastVersion", PROGRAM_VERSION);
    g_pIni->WriteString(CONFIG_ID, "Language",
      m_languages[g_config.LanguageIndex].Code);
    if (m_lastUpdateCheck != TDateTime())
      g_pIni->WriteDate(CONFIG_ID, "LastUpdateCheck", m_lastUpdateCheck);
    g_pIni->WriteString(CONFIG_ID, "DonorKey", m_asDonorKey);
    g_pIni->WriteString(CONFIG_ID, "GUIStyle", g_config.UiStyleName);
    g_pIni->WriteString(CONFIG_ID, "GUIFont", g_config.GUIFontString);
    g_pIni->WriteInteger(CONFIG_ID, "WindowTop", Top);
    g_pIni->WriteInteger(CONFIG_ID, "WindowLeft", Left);
    g_pIni->WriteInteger(CONFIG_ID, "WindowHeight", Height);
    g_pIni->WriteInteger(CONFIG_ID, "WindowWidth", Width);
    g_pIni->WriteBool(CONFIG_ID, "AlwaysOnTop", MainMenu_Options_AlwaysOnTop->Checked);
    g_pIni->WriteBool(CONFIG_ID, "IncludeChars", IncludeCharsCheck->Checked);
    g_pIni->WriteInteger(CONFIG_ID, "CharsLength", CharsLengthSpinBtn->Position);
    g_pIni->WriteString(CONFIG_ID, "CharSetListEntries", CharSetList->Items->CommaText);
    g_pIni->WriteBool(CONFIG_ID, "IncludeWords", IncludeWordsCheck->Checked);
    g_pIni->WriteInteger(CONFIG_ID, "WordsNum", WordsNumSpinBtn->Position);
    g_pIni->WriteString(CONFIG_ID, "WLFNListEntries", WLFNList->Items->CommaText);
    g_pIni->WriteBool(CONFIG_ID, "CombineWordsChars", CombineWordsCharsCheck->Checked);
    g_pIni->WriteBool(CONFIG_ID, "PassphrSpecLen", SpecifyLengthCheck->Checked);
    g_pIni->WriteString(CONFIG_ID, "PassphrSpecLenString", SpecifyLengthBox->Text);
    g_pIni->WriteBool(CONFIG_ID, "FormatPassw", FormatPasswCheck->Checked);
    g_pIni->WriteString(CONFIG_ID, "FormatListEntries", FormatList->Items->CommaText);
    g_pIni->WriteBool(CONFIG_ID, "RunScript", RunScriptCheck->Checked);
    g_pIni->WriteString(CONFIG_ID, "ScriptListEntries", ScriptList->Items->CommaText);
    g_pIni->WriteString(CONFIG_ID, "NumPassw", NumPasswBox->Text);
    g_pIni->WriteBool(CONFIG_ID, "HidePassw", TogglePasswBtn->Down);
    g_pIni->WriteBool(CONFIG_ID, "HideEntropyProgress",
      MainMenu_Options_HideEntProgress->Checked);
    g_pIni->WriteInteger(CONFIG_ID, "AdvancedPasswOptions", m_passwOptions.Flags);
    g_pIni->WriteString(CONFIG_ID, "AmbigChars", m_passwOptions.AmbigChars);
    g_pIni->WriteString(CONFIG_ID, "SpecialSym", m_passwOptions.SpecialSymbols);
    g_pIni->WriteInteger(CONFIG_ID, "MaxWordLen", m_passwOptions.MaxWordLen);
    g_pIni->WriteString(CONFIG_ID, "TrigramFile", m_passwOptions.TrigramFileName);
    g_pIni->WriteBool(CONFIG_ID, "AutoClearClipboard", g_config.AutoClearClip);
    g_pIni->WriteInteger(CONFIG_ID, "AutoClearClipboardTime",
      g_config.AutoClearClipTime);
    g_pIni->WriteBool(CONFIG_ID, "AutoClearPasswBox", g_config.AutoClearPassw);
    g_pIni->WriteInteger(CONFIG_ID, "AutoClearPasswBoxTime",
      g_config.AutoClearPasswTime);
    g_pIni->WriteBool(CONFIG_ID, "SystemTrayIconShowConst",
      g_config.ShowSysTrayIconConst);
    g_pIni->WriteBool(CONFIG_ID, "SystemTrayIconMinimize",
      g_config.MinimizeToSysTray);
    g_pIni->WriteBool(CONFIG_ID, "MinimizeAutotype", g_config.MinimizeAutotype);
    g_pIni->WriteInteger(CONFIG_ID, "AutotypeDelay", g_config.AutotypeDelay);
    g_pIni->WriteBool(CONFIG_ID, "ConfirmExit", g_config.ConfirmExit);
    g_pIni->WriteBool(CONFIG_ID, "LaunchSystemStartup",
      g_config.LaunchSystemStartup);
    g_pIni->WriteBool(CONFIG_ID, "LoadProfileStartup", g_config.LoadProfileStartup);
    g_pIni->WriteString(CONFIG_ID, "LoadProfileStartupName", g_config.LoadProfileName);
    g_pIni->WriteInteger(CONFIG_ID, "RandomPoolCipher", g_config.RandomPoolCipher);
    g_pIni->WriteBool(CONFIG_ID, "TestCommonPassw", g_config.TestCommonPassw);
    g_pIni->WriteBool(CONFIG_ID, "UseAdvancedPasswEst",
      g_config.UseAdvancedPasswEst);
    g_pIni->WriteInteger(CONFIG_ID, "AutoCheckUpdates", g_config.AutoCheckUpdates);
    g_pIni->WriteBool(CONFIG_ID, "SaveSettingsOnExit",
      MainMenu_Options_SaveSettingsOnExit->Checked);
    g_pIni->WriteBool(CONFIG_ID,"PasswEditable", PasswBoxMenu_Editable->Checked);
    g_pIni->WriteBool(CONFIG_ID, "EnablePasswTest",
      PasswBoxMenu_EnablePasswTest->Checked);

    g_pIni->WriteString(CONFIG_ID, "PasswBoxFont", FontToString(PasswBox->Font));

    WString sHotKeys;
    for (const auto& kv : g_config.HotKeys)
    {
      sHotKeys += FormatW("%d,%d,%d;", kv.first, kv.second.Action,
          kv.second.ShowMainWin);
    }
    g_pIni->WriteString(CONFIG_ID, "HotKeyList", sHotKeys);

    g_pIni->WriteInteger(CONFIG_ID, "FileEncoding",
      static_cast<int>(g_config.FileEncoding));

    g_pIni->WriteInteger(CONFIG_ID, "FileNewlineChar",
      static_cast<int>(g_config.FileNewlineChar));

    ConfigurationDlg->SaveConfig();
    PasswOptionsDlg->SaveConfig();
    PasswListForm->SaveConfig();
    PasswEnterDlg->SaveConfig();
    CreateRandDataFileDlg->SaveConfig();
    CreateTrigramFileDlg->SaveConfig();
    MPPasswGenForm->SaveConfig();
    QuickHelpForm->SaveConfig();
    ProvideEntropyDlg->SaveConfig();
    ProfileEditDlg->SaveConfig();
    PasswMngForm->SaveConfig();
    PasswDbSettingsDlg->SaveConfig();
    PasswMngKeyValDlg->SaveConfig();
    PasswMngDbPropDlg->SaveConfig();
    PasswHistoryDlg->SaveConfig();

    // now save the profiles
    for (auto it = g_profileList.begin(); it != g_profileList.end(); it++) {
      WString sProfileId = CONFIG_PROFILE_ID + IntToStr(it - g_profileList.begin());

      auto pProfile = it->get();

      g_pIni->WriteString(sProfileId, "ProfileName", pProfile->ProfileName);
      g_pIni->WriteBool(sProfileId, "IncludeChars", pProfile->IncludeChars);
      g_pIni->WriteInteger(sProfileId, "CharsLength", pProfile->CharsLength);
      g_pIni->WriteString(sProfileId, "CharSet", pProfile->CharSet);
      g_pIni->WriteBool(sProfileId, "IncludeWords", pProfile->IncludeWords);
      g_pIni->WriteInteger(sProfileId, "WordsNum", pProfile->WordsNum);
      g_pIni->WriteString(sProfileId, "WordListFileName", pProfile->WordListFileName);
      g_pIni->WriteBool(sProfileId, "CombineWordsChars", pProfile->CombineWordsChars);
      g_pIni->WriteBool(sProfileId, "PassphrSpecLen", pProfile->SpecifyLength);
      g_pIni->WriteString(sProfileId, "PassphrSpecLenString",
        pProfile->SpecifyLengthString);
      g_pIni->WriteBool(sProfileId, "FormatPassw", pProfile->FormatPassw);
      g_pIni->WriteString(sProfileId, "FormatString", pProfile->FormatString);
      g_pIni->WriteBool(sProfileId, "RunScript", pProfile->RunScript);
      g_pIni->WriteString(sProfileId, "ScriptFileName", pProfile->ScriptFileName);
      g_pIni->WriteInteger(sProfileId, "NumPassw", pProfile->NumPassw);

      if (pProfile->AdvancedOptionsUsed) {
        g_pIni->WriteInteger(sProfileId, "AdvancedPasswOptions",
          pProfile->AdvancedPasswOptions.Flags);
        g_pIni->WriteString(sProfileId, "AmbigChars",
          pProfile->AdvancedPasswOptions.AmbigChars);
        g_pIni->WriteString(sProfileId, "SpecialSym",
          pProfile->AdvancedPasswOptions.SpecialSymbols);
        g_pIni->WriteInteger(sProfileId, "MaxWordLen",
          pProfile->AdvancedPasswOptions.MaxWordLen);
        g_pIni->WriteString(sProfileId, "TrigramFile",
          pProfile->AdvancedPasswOptions.TrigramFileName);
      }
    }

    g_pIni->UpdateFile();
  }
  catch (Exception& e) {
    if (g_terminateAction != TerminateAction::SystemShutdown)
      MsgBox(TRLFormat("Error while writing to configuration file:\n%s.",
        e.Message.c_str()), MB_ICONERROR);
    return false;
  }
  return true;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::UpdateEntropyProgress(bool blForce)
{
  //static const WString MAX_ENTROPY_STRING = "/" + IntToStr(RandomPool::MAX_ENTROPY);
  static bool blReachedMaxLast = false;

  bool blReachedMax = m_entropyMng.EntropyBits == EntropyManager::MAX_ENTROPYBITS;

  if (m_blShowEntProgress && (blForce || !blReachedMax || !blReachedMaxLast))
  {
    blReachedMaxLast = blReachedMax;
    WString sEntropyBits = Format("%s%d%s (%d%%)",
      ARRAYOFCONST((
      blReachedMax ? ">" : "",
      m_entropyMng.EntropyBits,
      m_entropyMng.EntropyBits > RandomPool::MAX_ENTROPY ? "+" : "",
      std::min<int>(100, round(
        100.0 * m_entropyMng.EntropyBits / RandomPool::MAX_ENTROPY)))));
    StatusBar->Panels->Items[2]->Text = sEntropyBits;
  }
}
//---------------------------------------------------------------------------
int __fastcall TMainForm::LoadCharSet(const WString& sInput,
  bool blShowError)
{
  m_sCharSetInput = sInput;
  WString sResult = sInput;
  m_passwGen.SetupCharSets(sResult, m_passwOptions.AmbigChars,
    m_passwOptions.SpecialSymbols, m_passwOptions.Flags & PASSWOPTION_EXCLUDEAMBIG,
    m_passwOptions.Flags & PASSWOPTION_INCLUDESUBSET);

  if (sResult.IsEmpty()) {
    if (blShowError) {
      m_blCharSetError = true;
      CharSetInfoLbl->Caption = TRL("Invalid character set.");
      CharSetInfoLbl->Font->Color = clRed;
    }

    return 0;
  }

  m_blCharSetError = false;

  AddEntryToList(CharSetList, sInput, true);

  int nSetSize = m_passwGen.CustomCharSetW32.length();
  m_sCharSetInfo = TRLFormat("%d characters / %.1f bits per character",
      nSetSize, m_passwGen.CustomCharSetEntropy);

  CharSetInfoLbl->Caption = m_sCharSetInfo;
  CharSetInfoLbl->Font->Color = clWindowText;

  return nSetSize;
}
//---------------------------------------------------------------------------
int __fastcall TMainForm::LoadWordListFile(const WString& sInput,
  bool blShowError,
  bool blResetInfoOnly)
{
  if (!blResetInfoOnly || m_sWordListInfo.IsEmpty()) {
    WString sTrimmed = RemoveCommentAndTrim(sInput);
    WString sFileName;

    if (!sTrimmed.IsEmpty() && !SameFileName(sTrimmed, WLFNLIST_DEFAULT))
      sFileName = sTrimmed;

    int nNumOfWords = m_passwGen.LoadWordListFile(sFileName,
        m_passwOptions.MaxWordLen,
        m_passwOptions.Flags & PASSWOPTION_LOWERCASEWORDS);

    if (nNumOfWords <= 0) {
      if (blShowError) {
        m_sWLFileNameErr = sFileName;
        if (nNumOfWords < 0)
          WordListInfoLbl->Caption = TRL("Could not open file.");
        else
          WordListInfoLbl->Caption = TRL("Not enough valid words.");
        WordListInfoLbl->Font->Color = clRed;
      }

      return nNumOfWords;
    }

    m_sWLFileName = sFileName;

    if (sFileName.IsEmpty())
      AddEntryToList(WLFNList, WLFNLIST_DEFAULT, false);
    else
      AddEntryToList(WLFNList, sInput, false);

    m_sWordListInfo = TRLFormat("%d words / %.1f bits per word",
        m_passwGen.WordListSize, m_passwGen.WordListEntropy);
  }

  m_sWLFileNameErr = WString();

  WordListInfoLbl->Caption = m_sWordListInfo;
  WordListInfoLbl->Font->Color = clWindowText;

  return m_passwGen.WordListSize;
}
//---------------------------------------------------------------------------
int __fastcall TMainForm::LoadTrigramFile(const WString& sInput)
{
  WString sFileName = sInput;

  int nResult = m_passwGen.LoadTrigramFile(sFileName);

  if (nResult <= 0) {
    WString sMsg;
    if (nResult < 0)
      sMsg = TRL("Could not open trigram file.");
    else
      sMsg = TRL("Invalid trigram file.");
    if (m_blStartup)
      DelayStartupError(sMsg);
    else
      MsgBox(sMsg, MB_ICONERROR);
  }

  return nResult;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CryptText(bool blEncrypt,
  const SecureWString* psText,
  TForm* pParentForm)
{
  int nFlags = PASSWENTER_FLAG_ENABLEPASSWCACHE;
  nFlags |= blEncrypt ? PASSWENTER_FLAG_ENCRYPT | PASSWENTER_FLAG_CONFIRMPASSW :
    PASSWENTER_FLAG_DECRYPT | PASSWENTER_FLAG_ENABLEOLDVER;
  bool blSuccess = PasswEnterDlg->Execute(nFlags, WString(), pParentForm) == mrOk;

  SecureWString sPassw;
  if (blSuccess)
    sPassw = PasswEnterDlg->GetPassw();

  PasswEnterDlg->Clear();
  m_randPool.Flush();

  if (!blSuccess)
    return;

  Refresh();
  Screen->Cursor = crHourGlass;

  int nPasswBytes = sPassw.StrLenBytes();
  int nResult;

  if (blEncrypt) {
    nResult = EncryptText(psText, sPassw.Bytes(), nPasswBytes, m_randPool);

    // flush the pool to make sure we're back in the "add entropy" state
    m_randPool.Flush();
  }
  else {
    int nTryVersion = PasswEnterDlg->OldVersionCheck->Checked ? 0 :
      CRYPTTEXT_VERSION;

    SecureAnsiString asPassw;
    if (nTryVersion == 0) {
      int nAnsiLen = WideCharToMultiByte(CP_ACP, 0, sPassw, -1, nullptr, 0,
          nullptr, nullptr);

      asPassw.New(nAnsiLen);

      WideCharToMultiByte(CP_ACP, 0, sPassw, -1, asPassw, nAnsiLen,
        nullptr, nullptr);
    }

    do {
      const word8* pTryPassw;
      int nTryPasswBytes;

      if (nTryVersion < 2) {
        pTryPassw = asPassw.Bytes();
        nTryPasswBytes = asPassw.StrLenBytes();
      }
      else {
        pTryPassw = sPassw.Bytes();
        nTryPasswBytes = nPasswBytes;
      }

      nResult = DecryptText(psText, pTryPassw, nTryPasswBytes, nTryVersion);
    } while ((nResult == CRYPTTEXT_ERROR_TEXTCORRUPTED ||
        nResult == CRYPTTEXT_ERROR_BADKEY) &&
      ++nTryVersion < CRYPTTEXT_VERSION);
  }

  Screen->Cursor = crDefault;

  WString sMsg;
  switch (nResult) {
  case CRYPTTEXT_OK:
    if (blEncrypt)
      sMsg = TRL("Text successfully encrypted.");
    else
      sMsg = TRL("Text successfully decrypted.");
    break;
  case CRYPTTEXT_ERROR_CLIPBOARD:
    sMsg = TRL("Could not open clipboard.");
    break;
  case CRYPTTEXT_ERROR_NOTEXT:
    sMsg = TRL("No text available to process.");
    break;
  case CRYPTTEXT_ERROR_TEXTTOOLONG:
    sMsg = TRL("The text to be encrypted is too long.");
    break;
  case CRYPTTEXT_ERROR_OUTOFMEMORY:
    sMsg = TRL("Not enough memory available to perform\nthe operation. "
        "Try to use a shorter text.");
    break;
  case CRYPTTEXT_ERROR_TEXTCORRUPTED:
  case CRYPTTEXT_ERROR_BADKEY:
    sMsg = TRL("Decryption failed. This may be attributed\nto the following reasons:")
      + WString("\n");
    if (nResult == CRYPTTEXT_ERROR_BADKEY)
      sMsg += TRL("- You entered a wrong password.") + WString("\n");
    sMsg += TRL("- The text is corrupted.\n"
        "- The text is not encrypted.");
    break;
  case CRYPTTEXT_ERROR_DECOMPRFAILED:
    sMsg = TRL("This should not have happened:\nDecryption successful, but "
        "decompression\nfailed.");
    break;
  default:
    sMsg = "Unknown error"; // should never happen
  }

  if (nResult == CRYPTTEXT_OK) {
    if (g_nAppState & APPSTATE_HIDDEN)
      ShowTrayInfo(sMsg, bfInfo);
    else
      InfoBoxForm->ShowInfo(sMsg, pParentForm);
  }
  else {
    if (g_nAppState & APPSTATE_HIDDEN)
      ShowTrayInfo(sMsg, bfError);
    else
      MsgBox(sMsg, MB_ICONERROR);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::OnSetSensitiveClipboardData(void)
{
  if (g_config.AutoClearClip)
    m_nAutoClearClipCnt = g_config.AutoClearClipTime;
}
//---------------------------------------------------------------------------
int __fastcall TMainForm::AddEntryToList(TComboBox* pComboBox,
  const WString& sEntry,
  bool blCaseSensitive)
{
  TStrings* pStrList = pComboBox->Items;

  int nPos = -1;
  if (blCaseSensitive) {
    for (int nI = 0; nI < pStrList->Count; nI++) {
      if (pStrList->Strings[nI] == sEntry) {
        nPos = nI;
        break;
      }
    }
  }
  else
    nPos = pStrList->IndexOf(sEntry); // IndexOf() is case-insensitive for TStrings

  if (nPos < 0) {
    if (pStrList->Count == LISTS_MAX_ENTRIES)
      pStrList->Delete(LISTS_MAX_ENTRIES - 1);
    pStrList->Insert(0, sEntry);
    nPos = 0;
  }
  else {
    pStrList->Move(nPos, 0);
    pComboBox->ItemIndex = 0;
  }
  return nPos;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ShowPasswInfo(int nPasswLen,
  int nPasswBits,
  bool blCommonPassw,
  bool blEstimated)
{
  PasswSecurityBarPanel->Width = std::max<int>(std::min(nPasswBits / 128.0, 1.0) *
      PasswSecurityBar->Width, 4);
  WString sCaption;
  if (blCommonPassw) {
    sCaption = "**";
    PasswInfoLbl->Hint = TRL("Matches common password!");
  }
  else if (blEstimated) {
    sCaption = "*";
    PasswInfoLbl->Hint = WString(); //TRL("Estimated");
  }
  else
    PasswInfoLbl->Hint = WString();
  sCaption += TRLFormat("%d bits / %d characters", nPasswBits, nPasswLen);
  PasswInfoLbl->Caption = sCaption;
}
//---------------------------------------------------------------------------
bool __fastcall TMainForm::ApplyConfig(const Configuration& config)
{
  bool blLangChanged = false;
  if (config.LanguageIndex != g_config.LanguageIndex) {
    const WString& sLangVersion = m_languages[config.LanguageIndex].Version;

    if (CompareVersionNumbers(sLangVersion, PROGRAM_LANGVER_MIN) < 0) {
      if (MsgBox(TRLFormat("The version of this language (%s) is not\ncompatible "
            "with this program version.\nThis version of %s requires a language\n"
            "version of at least %s.\n\nDo you want to use it anyway?",
            sLangVersion.c_str(), PROGRAM_NAME, PROGRAM_LANGVER_MIN),
            MB_ICONWARNING + MB_YESNO + MB_DEFBUTTON2) == IDNO)
        return false;
    }
    else if (CompareVersionNumbers(sLangVersion, PROGRAM_VERSION) > 0) {
      if (MsgBox(TRLFormat("The version of this language (%s) is higher\nthan "
            "the version of this program (%s).\nTherefore, version incompatibilities "
            "may occur.\n\nDo you want to use it anyway?",
            sLangVersion.c_str(), PROGRAM_VERSION), MB_ICONWARNING +
          MB_YESNO + MB_DEFBUTTON2) == IDNO)
        return false;
    }
    blLangChanged = true;
  }

  if (config.HotKeys.empty())
    DeactivateHotKeys();
  else {
    bool blActivate = false;
    if (config.HotKeys.size() == g_config.HotKeys.size()) {
      HotKeyList::const_iterator it1 = config.HotKeys.begin();
      HotKeyList::const_iterator it2 = g_config.HotKeys.begin();
      for ( ; it1 != config.HotKeys.end(); it1++, it2++) {
        if (it1->first != it2->first) {
          blActivate = true;
          break;
        }
      }
    }
    else
      blActivate = true;

    //if (blActivate && ActivateHotKeys(config.HotKeys) == 0)
    //  return false;
    if (blActivate)
      ActivateHotKeys(config.HotKeys);
  }

  if (!SameText(config.GUIFontString, g_config.GUIFontString))
    ChangeGUIFont(config.GUIFontString);

  TrayIcon->Visible = config.ShowSysTrayIconConst ||
    ((g_nAppState & APPSTATE_MINIMIZED) && config.MinimizeToSysTray);

  g_sNewline = NewlineCharToString(config.FileNewlineChar);

  if (config.LaunchSystemStartup != g_config.LaunchSystemStartup) {
    static const WString APP_VAL = "PasswordTech",
      REG_KEY = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    bool blSuccess = false;
    std::unique_ptr<TRegistry> pRegistry(new TRegistry());

    if ((blSuccess = pRegistry->OpenKey(REG_KEY, false))) {
      bool blRegValExists = pRegistry->ValueExists(APP_VAL);
      if (config.LaunchSystemStartup && !blRegValExists) {
        pRegistry->WriteString(APP_VAL,
          "\"" + Application->ExeName + "\" -" + CMDLINE_SILENT);
      }
      else if (!config.LaunchSystemStartup && blRegValExists) {
        blSuccess = pRegistry->DeleteValue(APP_VAL);
      }
    }

    if (!blSuccess) {
      MsgBox(TRLFormat("Could not access registry key\n%s.", REG_KEY.c_str()),
        MB_ICONERROR);
      //return false;
    }
  }

  m_randPool.ChangeCipher(static_cast<RandomPool::CipherType>(
    config.RandomPoolCipher));

  SecureClipboard::GetInstance().AutoClear = config.AutoClearClip;
  if (!config.AutoClearClip)
    m_nAutoClearClipCnt = 0;

  /*if (!blLangChanged && !TStyleManager::TrySetStyle(config.UiStyleName, false)) {
    MsgBox(TRLFormat("Could not apply user interface style\n\"%s\".",
      config.UiStyleName.c_str()), MB_ICONERROR);
    return false;
  }*/
  bool blStyleChanged = !SameText(config.UiStyleName, g_config.UiStyleName);

  g_config = config;

  if ((blLangChanged || blStyleChanged) &&
      MsgBox(TRL("The program has to be restarted in order\nfor the changes "
        "to take effect.\n\nDo you want to restart now?"),
        MB_ICONQUESTION + MB_YESNO) == IDYES)
  {
    g_terminateAction = TerminateAction::RestartProgram;
    //Close();
  }

  return true;
}
//---------------------------------------------------------------------------
int __fastcall TMainForm::ActivateHotKeys(const HotKeyList& hotKeys)
{
  int nId = 0;
  WString sErrMsg;

  if (!m_hotKeys.empty())
    DeactivateHotKeys();

  for (const auto& kv : hotKeys)
  {
    Word wKey;
    TShiftState ss;
    ShortCutToKey(kv.first, wKey, ss);

    word32 lMod = 0;
    if (ss.Contains(ssAlt))
      lMod |= MOD_ALT;
    if (ss.Contains(ssCtrl))
      lMod |= MOD_CONTROL;
    if (ss.Contains(ssShift))
      lMod |= MOD_SHIFT;

    if (RegisterHotKey(Handle, nId, lMod, wKey)) {
      m_hotKeys.push_back(kv.second);
      nId++;
    }
    else {
      if (sErrMsg.IsEmpty())
        sErrMsg = TRL("Could not register the following hot keys:");
      sErrMsg += WString("\n") + WString(ShortCutToText(kv.first));
    }
  }

  if (!sErrMsg.IsEmpty()) {
    if (m_blStartup)
      DelayStartupError(sErrMsg);
    else
      MsgBox(sErrMsg, MB_ICONERROR);
  }

  return nId;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::DeactivateHotKeys(void)
{
  for (int nI = 0; nI < m_hotKeys.size(); nI++) {
    UnregisterHotKey(Handle, nI);
  }
  m_hotKeys.clear();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CreateProfile(const WString& sProfileName,
  bool blSaveAdvancedOptions,
  int nCreateIdx)
{
  PWGenProfile* pProfile;
  std::unique_ptr<PWGenProfile> newProfile;

  if (nCreateIdx < 0) {
    pProfile = new PWGenProfile;
    newProfile.reset(pProfile);
  }
  else
    pProfile = g_profileList[nCreateIdx].get();

  pProfile->ProfileName = sProfileName;
  pProfile->IncludeChars = IncludeCharsCheck->Checked;
  pProfile->CharsLength = CharsLengthSpinBtn->Position;
  pProfile->CharSet = CharSetList->Text;
  pProfile->IncludeWords = IncludeWordsCheck->Checked;
  pProfile->WordsNum = WordsNumSpinBtn->Position;
  pProfile->WordListFileName = WLFNList->Text;
  pProfile->CombineWordsChars = CombineWordsCharsCheck->Checked;
  pProfile->SpecifyLength = SpecifyLengthCheck->Checked;
  pProfile->SpecifyLengthString = SpecifyLengthBox->Text;
  pProfile->FormatPassw = FormatPasswCheck->Checked;
  pProfile->FormatString = FormatList->Text;
  pProfile->RunScript = RunScriptCheck->Checked;
  pProfile->ScriptFileName = ScriptList->Text;
  pProfile->NumPassw = StrToIntDef(NumPasswBox->Text, 0);
  pProfile->AdvancedOptionsUsed = blSaveAdvancedOptions;

  if (blSaveAdvancedOptions)
    pProfile->AdvancedPasswOptions = m_passwOptions;

  if (newProfile)
    g_profileList.push_back(std::move(newProfile));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::LoadProfile(int nIndex)
{
  auto pProfile = g_profileList[nIndex].get();

  IncludeCharsCheck->Checked = pProfile->IncludeChars;
  CharsLengthSpinBtn->Position = pProfile->CharsLength;
  CharSetList->Text = pProfile->CharSet;
  IncludeWordsCheck->Checked = pProfile->IncludeWords;
  WordsNumSpinBtn->Position = pProfile->WordsNum;
  WLFNList->Text = pProfile->WordListFileName;
  CombineWordsCharsCheck->Checked = pProfile->CombineWordsChars;
  SpecifyLengthCheck->Checked = pProfile->SpecifyLength;
  SpecifyLengthBox->Text = pProfile->SpecifyLengthString;
  FormatPasswCheck->Checked = pProfile->FormatPassw;
  FormatList->Text = pProfile->FormatString;
  FormatPasswInfoLbl->Caption = WString();
  RunScriptCheck->Checked = pProfile->RunScript;
  ScriptList->Text = pProfile->ScriptFileName;
  NumPasswBox->Text = WString(pProfile->NumPassw);
  //NumPasswBoxExit(this);

  if (pProfile->AdvancedOptionsUsed) {
    m_passwOptions = pProfile->AdvancedPasswOptions;
    PasswOptionsDlg->SetOptions(m_passwOptions);
    SetAdvancedBtnCaption();
    if (LoadTrigramFile(m_passwOptions.TrigramFileName) <= 0)
      m_passwOptions.TrigramFileName = WString();
  }

  LoadCharSet(CharSetList->Text, true);
  LoadWordListFile(WLFNList->Text, true);

  ProfileList->ItemIndex = nIndex;
}
//---------------------------------------------------------------------------
bool __fastcall TMainForm::LoadProfile(const WString& sName)
{
  int nIdx = FindPWGenProfile(sName);
  if (nIdx >= 0) {
    LoadProfile(nIdx);
    return true;
  }
  return false;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::DeleteProfile(int nIndex)
{
  if (nIndex >= 0 && nIndex < g_profileList.size()) {
    //delete g_profileList[nIndex];
    g_profileList.erase(g_profileList.begin() + nIndex);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::UpdateProfileControls(void)
{
  while (MainMenu_File_Profile->Count > 2) {
    delete MainMenu_File_Profile->Items[0];
    delete TrayMenu_Profile->Items[0];
  }

  ProfileList->Clear();

  for (int nI = 0; nI < g_profileList.size(); nI++) {
    TMenuItem* pMenuItem1 = new TMenuItem(MainMenu_File_Profile);
    WString sCaption;
    if (nI < sizeof(PROFILES_MENU_SHORTCUTS) - 1) {
      sCaption = "&" + WString(PROFILES_MENU_SHORTCUTS[nI]) + " ";
      pMenuItem1->ShortCut = ShortCut(PROFILES_MENU_SHORTCUTS[nI],
          TShiftState() << ssAlt << ssShift);
    }
    sCaption += g_profileList[nI]->ProfileName;
    pMenuItem1->Caption = sCaption;
    pMenuItem1->OnClick = MainMenu_File_ProfileClick;
    pMenuItem1->Tag = nI;

    MainMenu_File_Profile->Insert(nI, pMenuItem1);

    TMenuItem* pMenuItem2 = new TMenuItem(TrayMenu_Profile);
    pMenuItem2->Caption = pMenuItem1->Caption;
//    pMenuItem2->RadioItem = true;
    pMenuItem2->OnClick = MainMenu_File_ProfileClick;
    pMenuItem2->Tag = nI;

    TrayMenu_Profile->Insert(nI, pMenuItem2);

    ProfileList->Items->Add(g_profileList[nI]->ProfileName);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ChangeGUIFont(const WString& sFontStr)
{
  Tag = MAINFORM_TAG_REPAINT_COMBOBOXES;

  StringToFont(sFontStr, Font);

  ProfileLbl->Font = Font;
  ProfileLbl->Font->Style = TFontStyles() << fsBold;
  IncludeCharsCheck->Font = Font;
  IncludeCharsCheck->Font->Style = TFontStyles() << fsBold;
  IncludeWordsCheck->Font = IncludeCharsCheck->Font;
  FormatPasswCheck->Font = IncludeCharsCheck->Font;
  RunScriptCheck->Font = IncludeCharsCheck->Font;
  MultiplePasswLbl->Font = IncludeCharsCheck->Font;
  GenerateBtn->Font = IncludeCharsCheck->Font;

  // AboutForm
  AboutForm->Font = Font;

  // ProgressForm
  ProgressForm->Font = Font;

  // ConfigurationDlg
  ConfigurationDlg->Font = Font;

  // CreateRandDataFileDlg
  CreateRandDataFileDlg->Font = Font;

  // CreateTrigramFileDlg
  CreateTrigramFileDlg->Font = Font;

  // PasswEnterDlg
  PasswEnterDlg->Font = Font;
  PasswEnterDlg->PasswLbl->Font = Font;
  PasswEnterDlg->PasswLbl->Font->Style = TFontStyles() << fsBold;
  PasswEnterDlg->KeyFileLbl->Font = PasswEnterDlg->PasswLbl->Font;

  // PasswOptionsDlg
  PasswOptionsDlg->Font = Font;

  // ProfileEditDlg
  ProfileEditDlg->Font = Font;

  // MPPasswGenForm
  MPPasswGenForm->Font = Font;
  MPPasswGenForm->EnterPasswBtn->Font = Font;
  MPPasswGenForm->EnterPasswBtn->Font->Style = TFontStyles() << fsBold;
  MPPasswGenForm->ResultingPasswLbl->Font = MPPasswGenForm->EnterPasswBtn->Font;
  MPPasswGenForm->GenerateBtn->Font = MPPasswGenForm->EnterPasswBtn->Font;

  // ProvideEntropyDlg
  ProvideEntropyDlg->Font = Font;

  // InfoBox
  InfoBoxForm->Font = Font;

  // Password manager
  PasswMngForm->Font = Font;
  PasswMngForm->TitleLbl->Font = Font;
  PasswMngForm->UserNameLbl->Font = Font;
  PasswMngForm->PasswLbl->Font = Font;
  PasswMngForm->UrlLbl->Font = Font;
  PasswMngForm->KeywordLbl->Font = Font;
  PasswMngForm->KeyValueListLbl->Font = Font;
  PasswMngForm->NotesLbl->Font = Font;
  PasswMngForm->CreationTimeLbl->Font = Font;
  PasswMngForm->LastModificationLbl->Font = Font;

  // Password manager column selection
  PasswMngColDlg->Font = Font;

  // Password database settings
  PasswDbSettingsDlg->Font = Font;

  // Password manager key-value editor
  PasswMngKeyValDlg->Font = Font;

  // Password manager database properties
  PasswMngDbPropDlg->Font = Font;

  // Password history
  PasswHistoryDlg->Font = Font;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ShowInfoBox(const WString& sInfo)
{
  InfoBoxForm->ShowInfo(sInfo);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::UseKeySeededPRNG(void)
{
  if (!g_pKeySeededPRNG)
    return;
  g_pRandSrc = g_pKeySeededPRNG.get();
  m_passwGen.RandGen = g_pRandSrc;
  Caption = FormatW("%s (%s)", PROGRAM_NAME,
      TRL("WARNING: Random generator is deterministic (press F7 to deactivate)!").c_str());
  Application->Title = Caption;
  TrayIcon->Hint = Caption;
  MainMenu_Tools_DetermRandGen_Reset->Enabled = true;
  MainMenu_Tools_DetermRandGen_Deactivate->Enabled = true;

  ShowInfoBox(TRL("Deterministic random generator has been ACTIVATED."));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::GeneratePassw(GeneratePasswDest dest,
  TCustomEdit* pEditBox)
{
  word64 qNumOfPassw = (dest == gpdConsole) ? g_cmdLineOptions.GenNumPassw : 1;

  WString sFileName;
  Word wFileOpenMode = fmCreate;

  if (dest == gpdGuiList || dest == gpdClipboardList || dest == gpdFileList) {
    WString sNum = NumPasswBox->Text;
    int nFactor = 1;
    if (!sNum.IsEmpty()) {
      auto lastChar = toupper(*sNum.LastChar());
      switch (lastChar) {
      case 'K':
        nFactor = 1000; break;
      case 'M':
        nFactor = 1'000'000; break;
      case 'B':
      case 'G':
        nFactor = 1'000'000'000; break;
      }
      if (nFactor > 1)
        sNum.Delete(sNum.Length(), 1);
    }
    qNumOfPassw = StrToInt64Def(sNum, 0) * nFactor;
    if (qNumOfPassw < 2 || qNumOfPassw > PASSW_MAX_NUM) {
      MsgBox(FormatW(EnableInt64FormatSpec(TRL("Invalid number of passwords.\n"
        "Allowed range is %d-%d.")), 2ull, PASSW_MAX_NUM), MB_ICONERROR);
      NumPasswBox->SetFocus();
      return;
    }

    if (dest == gpdFileList) {
      SaveDlg->FilterIndex = 1;
      SaveDlg->Options >> ofOverwritePrompt;

      BeforeDisplayDlg();
      bool blSuccess = SaveDlg->Execute();
      AfterDisplayDlg();

      SaveDlg->Options << ofOverwritePrompt;

      if (!blSuccess)
        return;

      sFileName = SaveDlg->FileName;

      if (FileExists(sFileName)) {
        switch (MsgBox(TRL("The selected file already exists. Do you\n"
              "want to append passwords to this file\n"
              "(existing passwords will be preserved),\n"
              "or do you want to completely overwrite it?\n\n"
              "Yes -> append passwords,\n"
              "No -> overwrite file,\n"
              "Cancel -> cancel process."), MB_ICONQUESTION + MB_YESNOCANCEL))
        {
        case IDYES:
          wFileOpenMode = fmOpenReadWrite;
          break;
        case IDCANCEL:
          return;
        }
      }
    }
  }

  int nCharsLen = 0, nNumOfWords = 0;
  int nCharSetSize = 0;
  int nPassphrMinLength = -1, nPassphrMaxLength = -1;
  bool blPassphrLenAllChars = false;
  //double dBasePasswSec = 0;

  if (IncludeCharsCheck->Checked) {
    nCharSetSize = m_passwGen.CustomCharSetW32.length();
    nCharsLen = CharsLengthSpinBtn->Position;
    if ((m_passwOptions.Flags & PASSWOPTION_EACHCHARONLYONCE) &&
        nCharsLen > m_passwGen.CustomCharSetUniqueSize)
    {
      MsgBox(TRLFormat("If the option \"Each character must occur\nonly once\" is "
          "activated, the password length\nis limited to %d unique characters.",
          m_passwGen.CustomCharSetUniqueSize), MB_ICONWARNING);
      CharsLengthSpinBtn->Position = m_passwGen.CustomCharSetUniqueSize;
      nCharsLen = m_passwGen.CustomCharSetUniqueSize;
    }
  }

  if (IncludeWordsCheck->Checked) {
    nNumOfWords = WordsNumSpinBtn->Position;
    if (SpecifyLengthCheck->Checked && !SpecifyLengthBox->Text.IsEmpty()) {
      WString sText = SpecifyLengthBox->Text;

      int nSepPos = sText.Pos("*");
      if (nSepPos > 0) {
        blPassphrLenAllChars = true;
        sText.Delete(nSepPos, 1);
      }

      int nLen = sText.Length();
      if (nLen >= 2 && sText[1] == '>') {
        bool blGtEq = nLen >= 3 && sText[2] == '=';
        if (blGtEq)
          nPassphrMinLength = sText.SubString(3, nLen - 2).ToIntDef(-1);
        else
          nPassphrMinLength = sText.SubString(2, nLen - 1).ToIntDef(-2) + 1;
        nPassphrMaxLength = INT_MAX;
      }
      else if (nLen >= 2 && sText[1] == '<') {
        bool blLtEq = nLen >= 3 && sText[2] == '=';
        if (blLtEq)
          nPassphrMaxLength = sText.SubString(3, nLen - 2).ToIntDef(-1);
        else
          nPassphrMaxLength = sText.SubString(2, nLen - 1).ToIntDef(-1) - 1;
        nPassphrMinLength = 0;
      }
      else if ((nSepPos = sText.Pos("-")) >= 2) {
        nPassphrMinLength = sText.SubString(1, nSepPos - 1).ToIntDef(-1);
        nPassphrMaxLength = sText.SubString(nSepPos + 1, nLen - nSepPos).ToIntDef(-1);
      }
      else if (nLen > 0) {
        nPassphrMinLength = nPassphrMaxLength = sText.ToIntDef(-1);
      }

      if (nPassphrMaxLength < nPassphrMinLength)
        std::swap(nPassphrMaxLength, nPassphrMinLength);

      if (nPassphrMinLength < 0 || nPassphrMaxLength < 1) {
        MsgBox(TRL("Passphrase: Invalid length range."), MB_ICONERROR);
        return;
      }
    }

    if (m_passwOptions.Flags & PASSWOPTION_EACHWORDONLYONCE &&
        nNumOfWords > m_passwGen.WordListSize)
    {
      MsgBox(TRLFormat("If the option \"Each word must occur only\nonce\" is "
          "activated, the number of words\nis limited to the size of the word list (%d).",
          m_passwGen.WordListSize), MB_ICONWARNING);
      WordsNumSpinBtn->Position = m_passwGen.WordListSize;
      nNumOfWords = m_passwGen.WordListSize;
    }
  }

  w32string sFormatPassw;
  if (FormatPasswCheck->Checked)
    sFormatPassw = WStringToW32String(FormatList->Text);

  //bool blScriptGenStandalone = false;

  //std::unique_ptr<TScriptingThread> pScriptThread;
  bool blScripting = false;
  if (RunScriptCheck->Checked && ScriptList->GetTextLen() != 0) {
    WString sFileName = RemoveCommentAndTrim(ScriptList->Text);
    if (!sFileName.IsEmpty()) {
      if (ExtractFilePath(sFileName).IsEmpty())
        sFileName = g_sExePath + sFileName;
      if (!m_pScript || CompareText(m_pScript->FileName, sFileName) != 0)
      {
        try {
          m_pScript.reset(new LuaScript);
          m_pScript->SetPasswGenerator(&m_passwGen);
          m_pScript->LoadFile(sFileName);
        }
        catch (Exception& e)
        {
          m_pScript.reset();
          MsgBox(TRLFormat("Error while loading script:\n%s.", e.Message.c_str()),
            MB_ICONERROR);
          return;
        }
      }
      blScripting = true;
      AddEntryToList(ScriptList, ScriptList->Text, true);
    }
  }

  if (nCharsLen == 0 && nNumOfWords == 0 && sFormatPassw.empty() &&
      !blScripting)
    return;

  WString sErrorMsg;
  double dPasswSec = 0;
  int nPasswSec = 0;
  int nTotalPasswSec = 0;
  SecureW32String sChars;
  SecureW32String sWords;
  SecureW32String sFormatted;
  SecureWString sFromScript;
  SecureWString sPasswList;
  wchar_t nullChar = '\0';
  wchar_t* pwszPassw = nullptr;
  int nPasswLen = 0;
  int nPasswLenWChars = 0;
  bool blCommonPasswMatch = false;
  //bool blProgFormInit = false;
  auto progressPtr = std::make_shared<std::atomic<word64>>(0);
  auto& qPasswCnt = *progressPtr;

  std::unique_ptr<RandomPool> pRandPool;
  //std::unique_ptr<SplitMix64> pFastRandGen;
  if (dest != gpdConsole) {
    if (IsRandomPoolActive()) {
      m_entropyMng.AddSystemEntropy();
      //pFastRandGen.reset(new SplitMix64(g_fastRandGen.GetWord64()));
      pRandPool.reset(new RandomPool(m_randPool));
      m_passwGen.RandGen = pRandPool.get();
    }

    if (qNumOfPassw > 1) {
      if (dest == gpdGuiList)
        PasswListForm->Close();

      Refresh();
      Screen->Cursor = crHourGlass;
    }

    Enabled = false;
  }

  //std::atomic<bool> cancelFlag(false);
  TaskCancelToken cancelToken;

  auto generateAsync = [&]() {
    const bool blAsync = GetCurrentThreadId() != MainThreadID;
    std::unique_ptr<TScriptingThread> pScriptThread;

    try {
      int nFlags = m_passwOptions.Flags;
      bool blExcludeDuplicates = nFlags & PASSWOPTION_EXCLUDEDUPLICATES;

      int nPasswFlags = 0, nPassphrFlags = 0, nFormatFlags = 0;
      bool blFirstCharNotLC = nFlags & PASSWOPTION_FIRSTCHARNOTLC;

      if (nCharsLen != 0) {
        if (blFirstCharNotLC)
          nPasswFlags |= PASSW_FLAG_FIRSTCHARNOTLC;
        if (nFlags & PASSWOPTION_EXCLUDEREPCHARS)
          nPasswFlags |= PASSW_FLAG_EXCLUDEREPCHARS;
        if (nFlags & PASSWOPTION_INCLUDESUBSET)
          nPasswFlags |= PASSW_FLAG_INCLUDESUBSET;
        if (nFlags & PASSWOPTION_EACHCHARONLYONCE)
          nPasswFlags |= PASSW_FLAG_EACHCHARONLYONCE;
        if (nCharSetSize >= 128 && nCharsLen >= 128)
          nPasswFlags |= PASSW_FLAG_CHECKDUPLICATESBYSET;
        for (int nI = 0; nI < PASSWGEN_NUMINCLUDECHARSETS; nI++) {
          if (nFlags & (PASSWOPTION_INCLUDEUPPERCASE << nI))
            nPasswFlags |= PASSW_FLAG_INCLUDEUPPERCASE << nI;
        }
        if (m_passwGen.CustomCharSetType == cstPhoneticUpperCase)
          nPasswFlags |= PASSW_FLAG_PHONETICUPPERCASE;
        else if (m_passwGen.CustomCharSetType == cstPhoneticMixedCase)
          nPasswFlags |= PASSW_FLAG_PHONETICMIXEDCASE;

        sChars.New(nCharsLen + 1);
      }

      if (nNumOfWords != 0) {
        if (CombineWordsCharsCheck->Checked)
          nPassphrFlags |= PASSPHR_FLAG_COMBINEWCH;
        if (nFlags & PASSWOPTION_CAPITALIZEWORDS)
          nPassphrFlags |= PASSPHR_FLAG_CAPITALIZEWORDS;
        if (nFlags & PASSWOPTION_DONTSEPWORDS)
          nPassphrFlags |= PASSPHR_FLAG_DONTSEPWORDS;
        if (nFlags & PASSWOPTION_DONTSEPWORDSCHARS)
          nPassphrFlags |= PASSPHR_FLAG_DONTSEPWCH;
        if (nFlags & PASSWOPTION_REVERSEWCHORDER)
          nPassphrFlags |= PASSPHR_FLAG_REVERSEWCHORDER;
        if (nFlags & PASSWOPTION_EACHWORDONLYONCE)
          nPassphrFlags |= PASSPHR_FLAG_EACHWORDONLYONCE;

        if (nPassphrFlags & PASSPHR_FLAG_COMBINEWCH)
          nPasswFlags &= ~PASSW_FLAG_FIRSTCHARNOTLC;

        sWords.New(nCharsLen + nNumOfWords * (WORDLIST_MAX_WORDLEN + 2) + 1);
      }

      if (!sFormatPassw.empty()) {
        if (nFlags & PASSWOPTION_EXCLUDEREPCHARS)
          nFormatFlags |= PASSFORMAT_FLAG_EXCLUDEREPCHARS;

        sFormatted.New(PASSWFORMAT_MAX_CHARS + 1);
      }

      if (blScripting) {
        int nGenFlags = 0;
        WString sInitPasswFormat;
        if (nCharsLen != 0)
          nGenFlags |= 1;
        if (nNumOfWords != 0)
          nGenFlags |= 2;
        if (!sFormatPassw.empty()) {
          nGenFlags |= 4;
          sInitPasswFormat = FormatList->Text;
        }
        pScriptThread.reset(new TScriptingThread(m_pScript.get()));
        pScriptThread->SetInitParam(
          qNumOfPassw,
          dest,
          nGenFlags,
          nFlags,
          nCharsLen,
          nNumOfWords,
          sInitPasswFormat);
        if (m_pScript->Flags & LuaScript::FLAG_STANDALONE_PASSW_GEN) {
          nCharsLen = nNumOfWords = 0;
          //dBasePasswSec = 0;
          sFormatPassw.clear();
        }
        m_pScript->SetRandomGenerator(pRandPool ? pRandPool.get() : g_pRandSrc);
      }

	  const word32 lMaxPasswListBytes =
#ifdef _WIN64
        PASSWLIST_MAX_BYTES;
#else
        blExcludeDuplicates ? PASSWLIST_MAX_BYTES / 2 : PASSWLIST_MAX_BYTES;
#endif
      word32 lPasswListWChars = 0;

      static const AnsiString asPasswListHeader =
        "%d passwords generated.\r\nEntropy of the first "
        "password: %d bits.\r\nMaximum entropy of the entire list: %d bits.";
      int nPasswListHeaderLen;

      bool blFirstGen = true;
      bool blKeepPrevPassw = false;
      bool blCheckEachPassw = qNumOfPassw > 1 &&
        (nFlags & PASSWOPTION_CHECKEACHPASSW);
      std::unordered_set<SecureWString,SecureStringHashFunction>
        uniquePasswList;
      std::unique_ptr<TStringFileStreamW> pFile;
      WString sPasswAppendix((dest == gpdGuiList || dest == gpdClipboardList) ?
        CRLF : g_sNewline);

      // start script thread for the first time
      if (pScriptThread)
        pScriptThread->Start();

      double dBasePasswSec = 0;
      while (qPasswCnt < qNumOfPassw && !cancelToken) {

        if (nCharsLen != 0 && !blKeepPrevPassw) {
          switch (m_passwGen.CustomCharSetType) {
          case cstStandard:
          case cstStandardWithFreq:
            nPasswLen = m_passwGen.GetPassword(sChars, nCharsLen, nPasswFlags);
            break;
          case cstPhonetic:
          case cstPhoneticUpperCase:
          case cstPhoneticMixedCase:
            nPasswLen = m_passwGen.GetPhoneticPassw(sChars, nCharsLen,
                nPasswFlags);
          }
          if (blFirstGen) {
            if ((m_passwGen.CustomCharSetType == cstStandard ||
                m_passwGen.CustomCharSetType == cstStandardWithFreq) &&
                m_passwOptions.Flags & PASSWOPTION_EACHCHARONLYONCE)
              dBasePasswSec = m_passwGen.CalcPermSetEntropy(nCharSetSize, nPasswLen);
            else
              dBasePasswSec = m_passwGen.CustomCharSetEntropy * nPasswLen;
          }
        }

        if (nNumOfWords != 0) {
          int nNetWordsLen;
          nPasswLen = m_passwGen.GetPassphrase(sWords, nNumOfWords, sChars,
            nPassphrFlags, &nNetWordsLen);
          if (nPassphrMinLength >= 0) {
            int nBaseLen = blPassphrLenAllChars ? nPasswLen : nNetWordsLen;
            if (nBaseLen < nPassphrMinLength || nBaseLen > nPassphrMaxLength)
            {
              blKeepPrevPassw = true;
              continue;
            }
          }
          if (blFirstGen) {
            if (m_passwOptions.Flags & PASSWOPTION_EACHWORDONLYONCE)
              dBasePasswSec += m_passwGen.CalcPermSetEntropy(
                m_passwGen.WordListSize, nNumOfWords);
            else
              dBasePasswSec += m_passwGen.WordListEntropy * nNumOfWords;
          }
        }

        dPasswSec = dBasePasswSec;

        blKeepPrevPassw = false;

        word32* pPassw = (nNumOfWords != 0) ? sWords : sChars;

        if (!sFormatPassw.empty()) {
          double dFormatSec = 0;
          word32 lInvalidSpec;
          int nPasswPhUsed, nFormattedLen;
          nFormattedLen = m_passwGen.GetFormatPassw(sFormatted,
              PASSWFORMAT_MAX_CHARS, sFormatPassw, nFormatFlags, pPassw,
              &nPasswPhUsed, &lInvalidSpec,
              (blFirstGen || blCheckEachPassw) ? &dFormatSec : nullptr);

          if (dFormatSec > 0) {
            if (nPasswPhUsed == PASSFORMAT_PWUSED_NOTUSED && pPassw != nullptr)
              dPasswSec = dFormatSec;
            else
              dPasswSec += dFormatSec;
          }

          if (blFirstGen) {
            FormatPasswInfoLbl->Caption = WString();

            if (nPasswPhUsed == PASSFORMAT_PWUSED_NOTUSED && pPassw != nullptr)
            {
              //dPasswSec = dFormatSec;
              sChars.Clear();
              sWords.Clear();
              nCharsLen = nNumOfWords = 0;
              dBasePasswSec = 0;
              TThread::Synchronize(nullptr, _di_TThreadProcedure([this]{
                FormatPasswInfoLbl->Caption = TRL("\"P\" is not specified.");
              }));
            }
            else if (nPasswPhUsed == PASSFORMAT_PWUSED_EMPTYPW) {
              TThread::Synchronize(nullptr, _di_TThreadProcedure([this]{
                FormatPasswInfoLbl->Caption = WString("\"P\": ") +
                  TRL("Password not available.");
              }));
            }
            else if (nPasswPhUsed > 0 && nPasswPhUsed < nPasswLen) {
              TThread::Synchronize(nullptr, _di_TThreadProcedure([this]{
                FormatPasswInfoLbl->Caption = WString("\"P\": ") +
                  TRL("Password too long.");
              }));
            }
            else if (lInvalidSpec != 0) {
              w32string sW32Char(1, lInvalidSpec);
              WString sWChar = W32StringToWString(sW32Char);
              TThread::Synchronize(nullptr, _di_TThreadProcedure([&]{
                FormatPasswInfoLbl->Caption = TRLFormat("Invalid format specifier "
                    "\"%s\".", sWChar.c_str());
              }));
            }
          }
          pPassw = sFormatted;
          nPasswLen = nFormattedLen;
        }

        pwszPassw = reinterpret_cast<wchar_t*>(pPassw);
        if (pwszPassw != nullptr) {
          W32CharToWCharInternal(pwszPassw);
          nPasswLenWChars = wcslen(pwszPassw);
          if (blFirstCharNotLC)
            pwszPassw[0] = toupper(pwszPassw[0]);
        }

        if (pScriptThread) {
          pScriptThread->CallGenerate(
            qPasswCnt + 1,
            pwszPassw, dPasswSec);
          int nTimeouts = 0;
          while (!cancelToken) {
            TWaitResult wr = pScriptThread->ResultEvent->WaitFor(1000);

            if (wr == wrSignaled) {
              // check if error state has been set
              if (!pScriptThread->ErrorMessage.IsEmpty())
                throw Exception(pScriptThread->ErrorMessage);

              const SecureWString& sScriptPassw = pScriptThread->GetResultPassw();
              if (!sScriptPassw.IsStrEmpty()) {
                sFromScript.AssignStr(sScriptPassw, std::min<word32>(
                    PASSWSCRIPT_MAX_CHARS, sScriptPassw.StrLen()));
                //sFromScript.back() = '\0';
                pwszPassw = sFromScript;
                nPasswLen = GetNumOfUnicodeChars(sFromScript);
                nPasswLenWChars = sFromScript.StrLen();
              }
              // max. entropy is given by total number of Unicode characters
              // (log_2(1114112) = ~20) and password length
              dPasswSec = std::min(20.0 * sFromScript.StrLen(),
                  std::max(0.0, pScriptThread->GetResultPasswSec()));
              break;
            }
            else if (wr == wrTimeout) {
              if (pScriptThread->Finished && !pScriptThread->ErrorMessage.IsEmpty())
                throw Exception(pScriptThread->ErrorMessage);

              if (blAsync && ++nTimeouts == 1) {
                ProgressForm->SetProgressMessageAsync(this,
                  TRL("Waiting for script ..."));
              }
            }
            else {
              throw Exception(FormatW("ResultEvent::WaitFor() error (%d)",
                static_cast<int>(wr)));
            }
          }
          if (cancelToken) {
            if (nTimeouts > 0) {
              TerminateThread(reinterpret_cast<HANDLE>(pScriptThread->Handle), 0);
              m_pScript.reset();
              if (cancelToken.Reason == TaskCancelReason::UserCancel) {
                TThread::Synchronize(nullptr, _di_TThreadProcedure([] {
                  MsgBox(TRL("Script thread was terminated forcibly.\n"
                    "Please check script for infinite loops."),
                    MB_ICONERROR);
                  }));
              }
            }
            break;
          }
          if (nTimeouts > 0) {
            ProgressForm->SetProgressMessageAsync(this, WString());
          }
        }

        if (pwszPassw == nullptr)
          pwszPassw = &nullChar;

        if (qNumOfPassw > 1 && nPasswLenWChars > 0 && blExcludeDuplicates) {
          auto ret = uniquePasswList.emplace(pwszPassw, nPasswLenWChars + 1);
          if (!ret.second)
            continue;
        }

        if (blFirstGen || blCheckEachPassw) {
          dPasswSec = std::min<double>(dPasswSec, RandomPool::MAX_ENTROPY);
          nPasswSec = FloorEntropyBits(dPasswSec);
        }

        if (qNumOfPassw == 1 || blCheckEachPassw) {
          if (g_config.TestCommonPassw && !m_commonPassw.empty()) {
            std::wstring testStr(pwszPassw);
            if (m_commonPassw.count(testStr)) {
              blCommonPasswMatch = true;
              nPasswSec = std::min(nPasswSec, m_nCommonPasswEntropy);
            }
            eraseStlString(testStr);
          }
        }

        if (blCheckEachPassw) {
          sPasswAppendix = "  [";
          if (blCommonPasswMatch)
            sPasswAppendix += "*";
          sPasswAppendix += IntToStr(nPasswSec) + "]" +
            ((dest == gpdGuiList || dest == gpdClipboardList) ? CRLF : g_sNewline);
        }

        if (blFirstGen) {
          blFirstGen = false;
          if (dest == gpdGuiList || dest == gpdClipboardList) {
            nTotalPasswSec = FloorEntropyBits(std::min<double>(
              dPasswSec * qNumOfPassw, RandomPool::MAX_ENTROPY));

            WString sHeader = FormatW(
              EnableInt64FormatSpec(TRL(asPasswListHeader)),
              qNumOfPassw, static_cast<word64>(nPasswSec),
              static_cast<word64>(nTotalPasswSec)) + CRLF + CRLF;
            nPasswListHeaderLen = sHeader.Length();

            // use 64-bit int to avoid 32-bit overflow
            word64 qEstSizeWChars = nPasswListHeaderLen +
              (nPasswLenWChars + sPasswAppendix.Length()) * qNumOfPassw + 1;
            if (qEstSizeWChars * sizeof(wchar_t) > lMaxPasswListBytes)
              throw Exception(TRL("The estimated list size will probably exceed\n"
                  "the internal memory limit.\nPlease reduce the number of passwords"));

            sPasswList.New(qEstSizeWChars);
            sPasswList.StrCat(sHeader.c_str(), nPasswListHeaderLen,
              lPasswListWChars);
          }
          else if (dest == gpdFileList) {
            pFile.reset(new TStringFileStreamW(sFileName, wFileOpenMode,
                g_config.FileEncoding, true, PASSW_MAX_BYTES));
            if (wFileOpenMode == fmOpenReadWrite) {
              pFile->FileEnd();
            }
          }
        }

        bool blBreakLoop = false;
        switch (dest) {
        case gpdGuiList:
        case gpdClipboardList:
          {
            int nAppendixLen = sPasswAppendix.Length();
            if (lPasswListWChars + nPasswLenWChars +
                nAppendixLen >= sPasswList.Size()) {
              word64 qNewSizeWChars = static_cast<double>(lPasswListWChars) /
                qPasswCnt * qNumOfPassw + nPasswLenWChars + nAppendixLen + 1;
              if (qNewSizeWChars * sizeof(wchar_t) > lMaxPasswListBytes) {
                TThread::Synchronize(nullptr, _di_TThreadProcedure([&] {
                  NumPasswBox->Text = IntToStr(static_cast<__int64>(qPasswCnt));
                  MsgBox(FormatW(EnableInt64FormatSpec(TRL(
                    "The memory limit of the password list has\n"
                    "been reached.\n\nThe number of passwords has been\n"
                    "reduced to %d.")), qPasswCnt.load()), MB_ICONWARNING);
                }));
                blBreakLoop = true;
                break;
              }
              sPasswList.Grow(qNewSizeWChars);
            }

            sPasswList.StrCat(pwszPassw, nPasswLenWChars, lPasswListWChars);
            sPasswList.StrCat(sPasswAppendix.c_str(), sPasswAppendix.Length(),
              lPasswListWChars);
          }

          break;

        case gpdFileList:

          if (!pFile->WriteString(pwszPassw, nPasswLenWChars))
            OutOfDiskSpaceError();

          if (!pFile->WriteString(sPasswAppendix.c_str(), sPasswAppendix.Length()))
            OutOfDiskSpaceError();

          break;

        case gpdMsgBox:
          {
            bool blContinue = false;
            TThread::Synchronize(nullptr, _di_TThreadProcedure([&] {
              if (nPasswLen > 1000)
                MsgBox(TRL("Password is too long to show it in a\nmessage box like this.\n\n"
                    "It will be copied to the clipboard."),
                  MB_ICONWARNING);
              else {
                SecureWString sPasswMsg = FormatW_Secure(
                  TRL("The generated password is:\n\n%s\n\nThe estimated "
                    "security is %d bits.\n\nYes -> copy password to clipboard,\n"
                    "No -> generate new password,\nCancel -> cancel process."),
                  pwszPassw, nPasswSec);

                BeforeDisplayDlg();
                switch (MessageBox(Application->Handle, sPasswMsg, PROGRAM_NAME,
                    MB_ICONINFORMATION + MB_YESNOCANCEL)) {
                case IDCANCEL:
                  blBreakLoop = true;
                  pwszPassw = nullptr;
                  break;
                case IDNO:
                  blContinue = true;
                }
                AfterDisplayDlg();
              }
            }));
            if (blContinue)
              continue;
          }

          break;

        case gpdConsole:
            std::wcout << WStringToUtf8(pwszPassw).c_str() << std::endl;
          break;

        default:
          break;
        }

        if (blBreakLoop)
          break;

        qPasswCnt++;
      }

      if (cancelToken && cancelToken.Reason == TaskCancelReason::UserCancel &&
          qNumOfPassw > 1) {
        TThread::Synchronize(nullptr, _di_TThreadProcedure([&qPasswCnt] {
          MsgBox(EUserCancel::UserCancelMsg + ".\n\n" +
            FormatW(EnableInt64FormatSpec(
              TRL("%d passwords generated.")), qPasswCnt.load()), MB_ICONWARNING);
        }));
      }

      if (pScriptThread)
        pScriptThread->Terminate();

      if (nTotalPasswSec == 0)
        nTotalPasswSec = FloorEntropyBits(
          std::min<double>(dPasswSec * qPasswCnt, RandomPool::MAX_ENTROPY));

      //if (dest != gpdConsole)
      //  Refresh();

      if ((dest == gpdGuiList || dest == gpdClipboardList) && qPasswCnt != 0) {
        int nStrOffset = 0;

        if (qPasswCnt < qNumOfPassw) {
          WString sNewHeader = FormatW(EnableInt64FormatSpec(
            TRL(asPasswListHeader)),
            qPasswCnt.load(), static_cast<word64>(nPasswSec),
            static_cast<word64>(nTotalPasswSec)) + CRLF + CRLF;
          int nNewHeaderLen = sNewHeader.Length();
          nStrOffset = nPasswListHeaderLen - nNewHeaderLen;

          wcsncpy(sPasswList.Data() + nStrOffset, sNewHeader.c_str(), nNewHeaderLen);
        }

        sPasswList[lPasswListWChars - 2] = '\0';

        //SetEditBoxTextBuf(PasswListForm->PasswList, sPasswList.Data() + nStrOffset);
        //sPasswList.Empty();
        pwszPassw = &sPasswList[nStrOffset];
      }
    }
    catch (std::exception& e) {
      sErrorMsg = CppStdExceptionToString(e);
    }
    catch (Exception& e) {
      sErrorMsg = e.Message;
    }

    if (pScriptThread)
      pScriptThread->Terminate();

    if (!sErrorMsg.IsEmpty()) {
     sErrorMsg = TRLFormat("An error has occurred during password generation:\n%s.",
       sErrorMsg.c_str());
      if (dest == gpdFileList) {
        sErrorMsg += WString("\n\n") + FormatW(EnableInt64FormatSpec(TRL(
          "%d passwords written to file \"%s\".")),
          qPasswCnt.load(), ExtractFileName(SaveDlg->FileName).c_str());
      }
    }
  };

  if (dest == gpdConsole) {
    generateAsync();
    if (!sErrorMsg.IsEmpty())
      std::wcout << WStringToUtf8(sErrorMsg).c_str() << std::endl;
  }
  else {
    auto pTask = TTask::Create(generateAsync);

    pTask->Start();

    int nTimeout = 0;
    bool blProgInit = false;
    while (!pTask->Wait(10)) {
      Application->ProcessMessages();

      nTimeout += 10;
      if (!blProgInit && ((dest == gpdGuiList && qNumOfPassw >= 100'000) ||
          nTimeout >= 1000) && !ProgressForm->Visible) {
        blProgInit = true;
        ProgressForm->Init(this,
          TRL(qNumOfPassw == 1 ? "Generating password" :
              "Generating password list"),
          TRL("%d of %d passwords generated."),
          qNumOfPassw,
          cancelToken.Get(),
          progressPtr);

        Screen->Cursor = crAppStart;
      }
    }

    Enabled = true;
    Screen->Cursor = crDefault;

    if (!cancelToken || cancelToken.Reason == TaskCancelReason::UserCancel) {
      if (!sErrorMsg.IsEmpty())
        MsgBox(sErrorMsg, MB_ICONERROR);
      else if (qPasswCnt != 0 && pwszPassw != nullptr) {
        try {
          switch (dest) {
          case gpdGuiSingle:
            if (pEditBox == nullptr) {
              {
                TagOverrider ovr(PasswBox, 0);
                ClearEditBoxTextBuf(PasswBox);
                SetEditBoxTextBuf(PasswBox, pwszPassw);
              }
              PasswBox->Tag |= PASSWBOX_TAG_PASSW_GEN;
              ShowPasswInfo(nPasswLen, nPasswSec, blCommonPasswMatch);
              PasswBox->SetFocus();
              if (g_config.AutoClearPassw)
                m_nAutoClearPasswCnt = g_config.AutoClearPasswTime;
            }
            else
              SetEditBoxTextBuf(pEditBox, pwszPassw);
            break;

          case gpdGuiList:
            ProgressForm->SetProgressMessage(this,
              TRL("Copying password list ..."));
            SetEditBoxTextBuf(PasswListForm->PasswList, pwszPassw);
            PasswListForm->Execute();
            break;

          case gpdClipboardList:
            SecureClipboard::GetInstance().SetData(pwszPassw);
            //CopiedSensitiveDataToClipboard();
            ProgressForm->Terminate(this);
            ShowInfoBox(TRL("Password list copied to clipboard."));
            break;

          case gpdFileList:
            MsgBox(FormatW(EnableInt64FormatSpec(TRL(
              "File \"%s\" successfully created.\n\n%d passwords "
              "written.")), ExtractFileName(SaveDlg->FileName).c_str(), qPasswCnt.load()),
              MB_ICONINFORMATION);
            break;

          case gpdMsgBox:
          case gpdClipboard:
            SecureClipboard::GetInstance().SetData(pwszPassw);
            //SetClipboardTextBuf(pwszPassw, nullptr);
            //CopiedSensitiveDataToClipboard();
            ShowTrayInfo(TRL("Password copied to clipboard."), bfInfo);
            break;

          case gpdAutotype:
            TSendKeysThread::TerminateAndWait();
            if (!TSendKeysThread::ThreadRunning())
              new TSendKeysThread(Handle, g_config.AutotypeDelay,
                pwszPassw, nPasswLenWChars);
            break;

          default:
            break;
          }
        }
        catch (std::exception& e) {
          sErrorMsg = CppStdExceptionToString(e);
        }
        catch (Exception& e) {
          sErrorMsg = e.Message;
        }

        if (!sErrorMsg.IsEmpty())
          MsgBox(TRLFormat("An error has occurred while trying to send/\n"
            "process the password data:\n%s.", sErrorMsg.c_str()), MB_ICONERROR);
      }
    }

    ProgressForm->Terminate(this);

    if (IsRandomPoolActive()) {
      m_randPool.Flush();
      m_entropyMng.ConsumeEntropyBits(nTotalPasswSec);
      m_passwGen.RandGen = &m_randPool;
      UpdateEntropyProgress();
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ShowTrayInfo(const WString& sInfo,
  TBalloonFlags flags)
{
  TrayIcon->BalloonHint = sInfo;
  TrayIcon->BalloonFlags = flags;
  TrayIcon->BalloonTimeout = std::max(5000, 50 * sInfo.Length());
  TrayIcon->ShowBalloonHint();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::GenerateBtnClick(TObject *Sender)
{
  GeneratePasswDest dest;

  if (Sender == GenerateBtn)
    dest = gpdGuiSingle;
  else if (Sender == GenerateBtn2)
    dest = gpdGuiList;
  //else if (Sender == GenerateBtn3)
  //  dest = gpdFileList;
  else if (Sender == GenerateMenu_Clipboard)
    dest = gpdClipboardList;
  else if (Sender == GenerateMenu_File)
    dest = gpdFileList;
  else if (Sender == TrayMenu_GenPassw)
    dest = gpdClipboard;
  else if (Sender == TrayMenu_GenAndShowPassw)
    dest = gpdMsgBox;
  else
    dest = gpdAutotype;

  GeneratePassw(dest);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::IncludeCharsCheckClick(TObject *Sender)
{
  bool blChecked1 = IncludeCharsCheck->Checked;
  bool blChecked2 = IncludeWordsCheck->Checked;
  bool blChecked3 = FormatPasswCheck->Checked;
  bool blChecked4 = RunScriptCheck->Checked;

  CharsLengthLbl->Enabled = blChecked1;
  CharsLengthBox->Enabled = blChecked1;
  CharsLengthSpinBtn->Enabled = blChecked1;
  CombineWordsCharsCheck->Enabled = blChecked1 && blChecked2;

  WordsNumLbl->Enabled = blChecked2;
  WordsNumBox->Enabled = blChecked2;
  WordsNumSpinBtn->Enabled = blChecked2;
  SpecifyLengthCheck->Enabled = blChecked2;
  SpecifyLengthBox->Enabled = blChecked2 && SpecifyLengthCheck->Checked;

  FormatList->Enabled = blChecked3;
  FormatPasswInfoLbl->Enabled = blChecked3;
//  FormatPasswHelpBtn->Visible = blChecked3;

  ScriptList->Enabled = blChecked4;
  ReloadScriptBtn->Enabled = blChecked4;
  BrowseBtn2->Enabled = blChecked4;

  bool blEnableGen = blChecked1 || blChecked2 || blChecked3 || blChecked4;
  NumPasswBox->Enabled = blEnableGen;
  GenerateBtn->Enabled = blEnableGen;
  GenerateBtn2->Enabled = blEnableGen;
  //GenerateBtn3->Visible = blEnableGen;
  NumPasswBox->Enabled = blEnableGen;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharSetInfoBtnClick(TObject *Sender)
{
  CharSetListExit(this);

  // insert line breaks
  w32string sCharsW32 = m_passwGen.CustomCharSetW32;
  int nLineBreaks = (sCharsW32.length() - 1) / 40;
  for (int nI = 0; nI < nLineBreaks; nI++)
    sCharsW32.insert((nI + 1) * 40 + nI, 1, '\n');

  WString sCharsW16 = W32StringToWString(sCharsW32);
  switch (m_passwGen.CustomCharSetType) {
  case cstStandardWithFreq:
    sCharsW16 += WString("\n\n") + TRL("(with different frequencies)");
    break;
  case cstPhonetic:
  case cstPhoneticUpperCase:
  case cstPhoneticMixedCase:
    {
      WString sSource;
      if (m_passwOptions.TrigramFileName.IsEmpty())
        sSource = TRL("default (English) trigrams");
      else
        sSource = TRLFormat("File \"%s\"", m_passwOptions.TrigramFileName.c_str());
      sCharsW16 += WString("\n\n") + TRLFormat("(phonetic, based on trigram frequencies\n"
          "from the following source:\n<%s>)", sSource.c_str());
    }
    break;
  default:
    break;
  }

  WString sMsg;
  if (m_blCharSetError)
    sMsg = TRL("The character set you entered is invalid.\nIt must contain at least "
        "2 different characters.") + WString("\n\n");
  sMsg += FormatW("%s\n\n%s\n\n%s.",
      TRL("Currently loaded character set:").c_str(), sCharsW16.c_str(),
      m_sCharSetInfo.c_str());
  MsgBox(sMsg, MB_ICONINFORMATION);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::BrowseBtnClick(TObject *Sender)
{
  BrowseBtn->Refresh();
  OpenDlg->FilterIndex = 1;
  OpenDlg->Title = TRL("Select word list file");
  BeforeDisplayDlg();
  if (OpenDlg->Execute())
    WLFNList->Text = OpenDlg->FileName;
  AfterDisplayDlg();
  WLFNListExit(this);
  WLFNList->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ClearClipBtnClick(TObject *Sender)
{
  SecureClipboard::GetInstance().ClearData(true);
  //Clipboard()->Clear();

  WString sMsg = TRL("Clipboard cleared.");
  if (g_nAppState & APPSTATE_HIDDEN)
    ShowTrayInfo(sMsg, bfInfo);
  else
    ShowInfoBox(sMsg);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MPPasswGenBtnClick(TObject *Sender)
{
  //if (g_nAppState & APPSTATE_HIDDEN)
  //  ShowWindow(Application->Handle, SW_SHOW);
  if (MPPasswGenForm->Visible) {
    MPPasswGenForm->WindowState = wsNormal;
    ShowWindow(MPPasswGenForm->Handle, SW_SHOW);
    MPPasswGenForm->SetFocus();
  }
  else
    MPPasswGenForm->Show();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CryptTextBtnMouseUp(TObject *Sender,
  TMouseButton Button, TShiftState Shift, int X, int Y)
{
  if (Button == mbRight)
    CryptText(false);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CryptTextBtnClick(TObject *Sender)
{
  CryptText(true);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::EntropyProgressMenu_ResetCountersClick(TObject *Sender)
{
  m_entropyMng.ResetCounters();
  UpdateEntropyProgress();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_AboutClick(TObject *Sender)
{
  AboutForm->ShowModal();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Options_SaveSettingsNowClick(TObject *Sender)
{
  if (SaveConfig())
    InfoBoxForm->ShowInfo(TRL("Settings saved."));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AppMessage(MSG& msg, bool& blHandled)
{
  static int nMouseMoveCnt = 60, nMouseWheelCnt = 0;
  int nEntBits = 0;

  switch (msg.message) {
  case WM_LBUTTONDBLCLK:
  case WM_LBUTTONDOWN:
  case WM_MBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
    nEntBits = m_entropyMng.AddEvent(msg, entMouseClick, ENTROPY_MOUSECLICK);
    break;

  case WM_MOUSEMOVE:
    if (nMouseMoveCnt-- == 0) {
      nEntBits = m_entropyMng.AddEvent(msg, entMouseMove, ENTROPY_MOUSEMOVE);
      nMouseMoveCnt = 50 + fprng_rand(50);
    }
    break;

  case WM_MOUSEWHEEL:
    if (nMouseWheelCnt-- == 0) {
      nEntBits = m_entropyMng.AddEvent(msg, entMouseWheel, ENTROPY_MOUSEWHEEL);
      nMouseWheelCnt = fprng_rand(10);
    }
    break;

  case WM_KEYDOWN:
    nEntBits = m_entropyMng.AddEvent(msg, entKeyboard, ENTROPY_KEYBOARD);
    break;
  }

  if (nEntBits != 0)
    UpdateEntropyProgress();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AppException(TObject* Sender, Sysutils::Exception* E)
{
  // this should not happen...
  BeforeDisplayDlg();
  Application->MessageBox(FormatW("Global exception handler caught the\n"
    "following exception:\n"
    "Exception type: %s\n"
    "Error message: \"%s\"", E->ClassName().c_str(), E->Message.c_str()).c_str(),
    L"Global exception handler", MB_ICONERROR);
  //Application->MessageBox(FormatW("Sorry, this should not have happened.\n"
  //    "The following error has not been handled\nproperly by the application:\n\n\"%s\"\n\n"
  //    "It is recommended to restart the program\nto avoid an undefined behavior.\n\n"
  //    "Please report this error to %s.\nThank you!", E->Message.c_str(),
  //    PROGRAM_AUTHOR_EMAIL).c_str(), L"Unhandled exception", MB_ICONERROR);
  AfterDisplayDlg();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AppMinimize(TObject* Sender)
{
  if (g_config.MinimizeToSysTray) {
    ShowWindow(Application->Handle, SW_HIDE);
    g_nAppState |= APPSTATE_HIDDEN;
    TrayIcon->Visible = true;
  }

  if (g_config.Database.LockMinimize && PasswMngForm->IsDbOpenOrLocked() &&
      !(g_nAppState & APPSTATE_AUTOTYPE))
    PasswMngForm->WindowState = wsMinimized;

  g_nAppState |= APPSTATE_MINIMIZED;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AppRestore(TObject* Sender)
{
  if (!Application->ShowMainForm) {
    Application->ShowMainForm = true;
    Visible = true;
    return;
  }

  if (g_config.MinimizeToSysTray) {
    TrayIcon->Visible = g_config.ShowSysTrayIconConst;
    ShowWindow(Application->Handle, SW_SHOW);
    if (!MPPasswGenForm->Visible)
      ShowWindow(MPPasswGenForm->Handle, SW_HIDE);
    if (!PasswMngForm->Visible)
      ShowWindow(PasswMngForm->Handle, SW_HIDE);
    Application->BringToFront();
    g_nAppState &= ~APPSTATE_HIDDEN;
  }

  g_nAppState &= ~APPSTATE_MINIMIZED;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AppDeactivate(TObject* Sender)
{
  TopMostManager::GetInstance().OnAppDeactivate();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::RestoreAction(void)
{
  if (Application->ShowMainForm)
    Application->Restore();
  else {
    Application->ShowMainForm = true;
    Visible = true;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenu_RestoreClick(TObject *Sender)
{
  RestoreAction();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenu_EncryptClipClick(TObject *Sender)
{
  CryptText(true);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenu_DecryptClipClick(TObject *Sender)
{
  CryptText(false);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenu_ExitClick(TObject *Sender)
{
  Close();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TimerTick(TObject *Sender)
{
  static int nTouchPoolCnt = 0, nRandomizeCnt = 0, nMovePoolCnt = 0,
             nWriteSeedFileCnt = 0;

  if (++nTouchPoolCnt == TIMER_TOUCHPOOL) {
    m_randPool.TouchPool();
    nTouchPoolCnt = 0;
  }

  if (++nRandomizeCnt == TIMER_RANDOMIZE) {
    m_entropyMng.AddSystemEntropy();
    UpdateEntropyProgress();
    nRandomizeCnt = 0;
  }

  if (++nMovePoolCnt == TIMER_MOVEPOOL) {
    // take the opportunity to reseed the fast PRNG
    g_fastRandGen.Randomize();

    m_randPool.MovePool();
    nMovePoolCnt = 0;
  }

  if (++nWriteSeedFileCnt == TIMER_WRITESEEDFILE) {
    WriteRandSeedFile(false);
    nWriteSeedFileCnt = 0;
  }

  if (m_nAutoClearClipCnt > 0 && --m_nAutoClearClipCnt == 0)
    SecureClipboard::GetInstance().ClearData();
    //Clipboard()->Clear();

  if (m_nAutoClearPasswCnt > 0 && --m_nAutoClearPasswCnt == 0) {
    TagOverrider ovr(PasswBox, 0);
    ClearEditBoxTextBuf(PasswBox);
    PasswInfoLbl->Caption = TRL("Click on \"Generate\"");
    PasswSecurityBar->Width = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::HelpBtnClick(TObject *Sender)
{
  ExecuteShellOp(m_sHelpFileName);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::SetAdvancedBtnCaption(void)
{
  //static const WString sAdvanced = TRL("Advanced");

  int nNum = 0;
  int nFlags = m_passwOptions.Flags;
  while (nFlags) {
    if (nFlags & 1)
      nNum++;
    nFlags >>= 1;
  }

  nFlags = m_passwOptions.Flags;
  WString sHint;
  if (nNum) {
    for (int i = 0; i < PASSWOPTIONS_NUM; i++) {
      if (nFlags & (1 << i)) {
        WString sOption;
        if (PASSWOPTIONS_STARRED[i])
          sOption = "*";
        sOption += PasswOptionsDlg->BitToString(i);
        if (nNum > 5 && sOption.Length() > 40)
          sOption = sOption.SubString(1, 40-3) + "...";
        sHint += "\n" + sOption;
      }
    }
  }

  WString sCaption = TRL("Advanced"); //sAdvanced;
  sHint = TRL("Advanced password options") + " - " +
    TRLFormat("%d option(s) active", nNum) + sHint;

  if (nNum) {
    sCaption += "|" + IntToStr(nNum);
    if (nFlags & PASSWOPTIONS_STARRED_BITFIELD)
      sCaption += "*";
  }

  AdvancedBtn->Caption = sCaption;
  AdvancedBtn->Hint = sHint;
  //TRL("Advanced password options") + "\n" +
  //  TRLFormat("%d active options", nNum);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AdvancedBtnClick(TObject *Sender)
{
  PasswOptionsDlg->SetOptions(m_passwOptions);
  if (PasswOptionsDlg->ShowModal() == mrOk) {
    PasswOptions old = m_passwOptions;

    PasswOptionsDlg->GetOptions(m_passwOptions);

    if (m_passwOptions.MaxWordLen != old.MaxWordLen ||
      (m_passwOptions.Flags & PASSWOPTION_LOWERCASEWORDS) !=
      (old.Flags & PASSWOPTION_LOWERCASEWORDS))
      LoadWordListFile(WLFNList->Text, true);

    if (!SameFileName(m_passwOptions.TrigramFileName, old.TrigramFileName)) {
      if (LoadTrigramFile(m_passwOptions.TrigramFileName) <= 0)
        m_passwOptions.TrigramFileName = old.TrigramFileName;
    }

    LoadCharSet(CharSetList->Text, true);
    SetAdvancedBtnCaption();
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenuPopup(TObject *Sender)
{
  TComboBox* pBox = reinterpret_cast<TComboBox*>(ListMenu->PopupComponent);
  HWND hChild = GetWindow(pBox->Handle, GW_CHILD);

  bool blSelected = pBox->SelLength > 0;

  ListMenu_Undo->Enabled = SendMessage(hChild, EM_CANUNDO, 0, 0);
  ListMenu_SelectAll->Enabled = SendMessage(hChild, WM_GETTEXTLENGTH, 0, 0) > 0;
  ListMenu_Cut->Enabled = blSelected;
  ListMenu_Copy->Enabled = blSelected;
  ListMenu_Paste->Enabled = Clipboard()->HasFormat(CF_TEXT);
  ListMenu_Delete->Enabled = blSelected;
  ListMenu_RemoveEntry->Enabled = pBox->ItemIndex >= 0;
  ListMenu_ClearList->Enabled = pBox->Items->Count > 0;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_UndoClick(TObject *Sender)
{
  SendMessage(GetWindow(((TComboBox*) ListMenu->PopupComponent)->Handle,
      GW_CHILD), WM_UNDO, 0, 0);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_CutClick(TObject *Sender)
{
  SendMessage(((TComboBox*) ListMenu->PopupComponent)->Handle, WM_CUT, 0, 0);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_CopyClick(TObject *Sender)
{
  SendMessage(((TComboBox*) ListMenu->PopupComponent)->Handle, WM_COPY, 0, 0);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_PasteClick(TObject *Sender)
{
  SendMessage(((TComboBox*) ListMenu->PopupComponent)->Handle, WM_PASTE, 0, 0);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_DeleteClick(TObject *Sender)
{
  ((TComboBox*) ListMenu->PopupComponent)->SelText = WString();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_SelectAllClick(TObject *Sender)
{
  ((TComboBox*) ListMenu->PopupComponent)->SelectAll();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_ClearListClick(TObject *Sender)
{
  if (((TComboBox*) ListMenu->PopupComponent)->Items->Count > 0) {
    if (MsgBox(TRL("All entries will be removed from the list.\nAre you sure?"),
        MB_ICONWARNING + MB_YESNO + MB_DEFBUTTON2) == IDYES)
      ((TComboBox*) ListMenu->PopupComponent)->Clear();
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharSetListEnter(TObject *Sender)
{
  ListMenu->PopupComponent = CharSetList;
  ListMenuPopup(this);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WLFNListEnter(TObject *Sender)
{
  ListMenu->PopupComponent = WLFNList;
  ListMenuPopup(this);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharSetListKeyPress(TObject *Sender, char &Key)
{
  if (Key == VK_RETURN) {
    CharSetInfoBtnClick(this);
    Key = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::NumPasswBoxKeyPress(TObject *Sender, char &Key)
{
  if (Key == VK_RETURN) {
    GeneratePassw(gpdGuiList);
    Key = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxKeyPress(TObject *Sender, char &Key)
{
  if (Key == VK_RETURN) {
    GeneratePassw(gpdGuiSingle);
    Key = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharSetListExit(TObject *Sender)
{
  if (CharSetList->Text != m_sCharSetInput)
    LoadCharSet(CharSetList->Text, true);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_CopyClick(TObject *Sender)
{
  if (g_config.AutoClearClip) {
    SecureWString sCopy = GetEditBoxSelTextBuf(PasswBox);
    SecureClipboard::GetInstance().SetData(sCopy.c_str());
  }
  else
    PasswBox->CopyToClipboard();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_SelectAllClick(TObject *Sender)
{
  PasswBox->SelectAll();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_ChangeFontClick(TObject *Sender)
{
  //FontDlg->Font = PasswBox->Font;
  BeforeDisplayDlg();
  if (FontDlg->Execute()) {
    PasswBox->Font = FontDlg->Font;
    MPPasswGenForm->PasswBox->Font = FontDlg->Font;
  }
  AfterDisplayDlg();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TogglePasswBtnClick(TObject *Sender)
{
  TagOverrider ovr(PasswBox, 0);
  PasswBox->PasswordChar = TogglePasswBtn->Down ? PASSWORD_CHAR : '\0';
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharsLengthBoxExit(TObject *Sender)
{
  int nValue = StrToIntDef(CharsLengthBox->Text, 0);

  if (nValue < CharsLengthSpinBtn->Min || nValue > CharsLengthSpinBtn->Max)
    CharsLengthBox->Text = WString(CharsLengthSpinBtn->Position);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WordsNumBoxExit(TObject *Sender)
{
  int nValue = StrToIntDef(WordsNumBox->Text, 0);

  if (nValue < WordsNumSpinBtn->Min || nValue > WordsNumSpinBtn->Max)
    WordsNumBox->Text = WString(WordsNumSpinBtn->Position);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_VisitWebsiteClick(TObject *Sender)
{
  ExecuteShellOp(PROGRAM_URL_WEBSITE);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_DonateClick(TObject *Sender)
{
  const int STD_PRICES[2] = {  9, 10 };
  const int PRO_PRICES[2] = { 18, 20 };
  const int STD_NUM_UPDATES = 3;
  if (MsgBox(TRLFormat(
      "Donate at least one of the following amounts\nto receive a Donor Key:\n\n"
      "%d EUR / %d USD: Valid for current version and %d updates.\n"
      "%d EUR / %d USD: Valid for the entire lifetime of main version 3 "
      "(unlimited updates).\n\n"
      "Do you want to donate now?",
      STD_PRICES[0], STD_PRICES[1], STD_NUM_UPDATES, PRO_PRICES[0], PRO_PRICES[1]),
      MB_ICONINFORMATION + MB_YESNO) == IDYES)
    ExecuteShellOp(PROGRAM_URL_DONATE);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::NumPasswBoxExit(TObject *Sender)
{
  /*int nValue = StrToIntDef(NumPasswBox->Text, 0);

  if (nValue < 2)
    NumPasswBox->Text = WString("2");
  else if (nValue > PASSW_MAX_NUM)
    NumPasswBox->Text = WString(PASSW_MAX_NUM);*/
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormatListExit(TObject *Sender)
{
  WString sInput = FormatList->Text;
  if (!sInput.IsEmpty())
    AddEntryToList(FormatList, sInput, true);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WLFNListExit(TObject *Sender)
{
  WString sFileName = WLFNList->Text;

  if (sFileName.IsEmpty() || SameFileName(sFileName, WLFNLIST_DEFAULT))
    LoadWordListFile(WString(), true, m_sWLFileName.IsEmpty());
  else
    LoadWordListFile(sFileName, true, SameFileName(sFileName, m_sWLFileName));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WordListInfoBtnClick(TObject *Sender)
{
  WLFNListExit(this);

  WString sMsg;

  if (!m_sWLFileNameErr.IsEmpty())
    sMsg = TRLFormat("Error while loading the file\n\"%s\":\n%s",
        m_sWLFileNameErr.c_str(), WordListInfoLbl->Caption.c_str()) + WString("\n\n");

  WString sWordList = m_sWLFileName;
  if (m_sWLFileName.IsEmpty())
    sWordList = WString("<") + TRL("default (English) word list") + WString(">");
  else
    sWordList = WString("\"") + m_sWLFileName + WString("\"");

  sMsg += FormatW("%s\n%s.\n\n%s.\n\n%s %s\n%s %s\n%s %s",
      TRL("Currently loaded word list:").c_str(),
      sWordList.c_str(), m_sWordListInfo.c_str(),
      TRL("First word:").c_str(),
      m_passwGen.GetWord(0).c_str(),
      TRL("Random word:").c_str(),
      m_passwGen.GetWord(fprng_rand(m_passwGen.WordListSize)).c_str(),
      TRL("Last word:").c_str(),
      m_passwGen.GetWord(m_passwGen.WordListSize - 1).c_str());

  MsgBox(sMsg, MB_ICONINFORMATION);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::WLFNListKeyPress(TObject *Sender, char &Key)
{
  if (Key == VK_RETURN) {
    WordListInfoBtnClick(this);
    Key = 0;
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormatListEnter(TObject *Sender)
{
  ListMenu->PopupComponent = FormatList;
  ListMenuPopup(this);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::CharSetHelpBtnClick(TObject *Sender)
{
  QuickHelpForm->Execute(
    m_sCharSetHelp,
    SettingsGroup->ClientOrigin.x,
    CharSetList->ClientOrigin.y + CharSetList->Height + 8);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormatPasswHelpBtnClick(TObject *Sender)
{
  QuickHelpForm->Execute(
    m_sFormatPasswHelp,
    SettingsGroup->ClientOrigin.x,
    FormatList->ClientOrigin.y + FormatList->Height + 8);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxChange(TObject *Sender)
{
  if (PasswBox->Tag & PASSWBOX_TAG_PASSW_GEN) {
    PasswBox->Tag &= ~PASSWBOX_TAG_PASSW_GEN;
    m_nAutoClearPasswCnt = 0;
  }
  if (PasswBox->Tag & PASSWBOX_TAG_PASSW_TEST) {
    int nPasswLen = GetEditBoxTextLen(PasswBox);
    int nPasswBits = 0;
    bool blCommonPasswMatch = false;

    if (nPasswLen != 0) {
      SecureWString sPassw = GetEditBoxTextBuf(PasswBox);
      if (g_config.TestCommonPassw && !m_commonPassw.empty()) {
        std::wstring testStr(sPassw.c_str());
        if (m_commonPassw.count(testStr)) {
          blCommonPasswMatch = true;
          nPasswBits = m_nCommonPasswEntropy;
        }
        eraseStlString(testStr);
      }
      if (!blCommonPasswMatch) {
        if (g_config.UseAdvancedPasswEst)
          nPasswBits = FloorEntropyBits(
            ZxcvbnMatch(WStringToUtf8(sPassw).c_str(), nullptr, nullptr));
        else
          nPasswBits = m_passwGen.EstimatePasswSecurity(sPassw);
      }
    }

    ShowPasswInfo(nPasswLen, nPasswBits, blCommonPasswMatch, true);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenuPopup(TObject *Sender)
{
  bool blManip = !PasswBox->ReadOnly;
  bool blSelected = PasswBox->SelLength != 0;
  bool blHasText = GetEditBoxTextLen(PasswBox) > 0;

  PasswBoxMenu_Undo->Enabled = blManip && PasswBox->CanUndo;
  PasswBoxMenu_Cut->Enabled = blManip && blSelected;
  PasswBoxMenu_Copy->Enabled = blSelected;
  PasswBoxMenu_EncryptCopy->Enabled = blSelected;
  PasswBoxMenu_Paste->Enabled = blManip && Clipboard()->HasFormat(CF_TEXT);
  PasswBoxMenu_PerformAutotype->Enabled = blHasText;
  PasswBoxMenu_Delete->Enabled = blManip && blSelected;
  PasswBoxMenu_AddToDb->Enabled = blSelected && PasswMngForm->IsDbOpen();
  PasswBoxMenu_SelectAll->Enabled = blHasText;
  PasswBoxMenu_SaveAsFile->Enabled = blHasText;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_UndoClick(TObject *Sender)
{
  PasswBox->Undo();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_CutClick(TObject *Sender)
{
  if (g_config.AutoClearClip) {
    SecureWString sCut = GetEditBoxSelTextBuf(PasswBox);
    SecureClipboard::GetInstance().SetData(sCut.c_str());
    PasswBox->SetSelTextBuf(const_cast<wchar_t*>(L""));
  }
  else
    PasswBox->CutToClipboard();
  //CopiedSensitiveDataToClipboard();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_PasteClick(TObject *Sender)
{
  PasswBox->PasteFromClipboard();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_DeleteClick(TObject *Sender)
{
  //PasswBox->SelText = WString();
  PasswBox->SetSelTextBuf(const_cast<wchar_t*>(L""));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_EditableClick(TObject *Sender)
{
  PasswBox->ReadOnly = !PasswBoxMenu_Editable->Checked;
  PasswBoxMenu_EnablePasswTest->Enabled = PasswBoxMenu_Editable->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_TimerInfoClick(TObject *Sender)
{
  WString sTimer;
  switch (g_highResTimer) {
  case HighResTimer::None:
    sTimer = "N/A";
    break;
  case HighResTimer::RDTSC:
    sTimer = "Time stamp counter (RDTSC instruction)";
    break;
  case HighResTimer::QPC:
    sTimer = "QueryPerformanceCounter (Windows API)";
  }

  __int64 qValue;
  HighResTimer(&qValue);
  WString sValue = IntToStr(qValue);

  MsgBox(TRLFormat("High-resolution timer used:\n%s.\n\nCurrent value: %s.",
      sTimer.c_str(), sValue.c_str()), MB_ICONINFORMATION);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ProfileEditorBtnClick(TObject *Sender)
{
  if (ProfileEditDlg->Execute())
    UpdateProfileControls();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_File_ProfileClick(TObject *Sender)
{
  TMenuItem* pMenuItem = reinterpret_cast<TMenuItem*>(Sender);

  LoadProfile(pMenuItem->Tag);

  WString sMsg = TRLFormat("Profile \"%s\" activated.",
    g_profileList[pMenuItem->Tag]->ProfileName.c_str());
  if (g_nAppState & APPSTATE_HIDDEN)
    ShowTrayInfo(sMsg, bfInfo);
  else {
    Refresh();
    ShowInfoBox(sMsg);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::OnHotKey(TMessage& msg)
{
  if (static_cast<word32>(msg.WParam) >= m_hotKeys.size() ||
    TaskCancelManager::GetInstance().GetNumTasksRunning() ||
    Screen->ActiveForm == ProgressForm || IsDisplayDlg() ||
    (Screen->ActiveForm != nullptr &&
      Screen->ActiveForm->FormState.Contains(fsModal)))
    return;

  bool blWasMinimized = g_nAppState & APPSTATE_MINIMIZED;

  const HotKeyEntry& hke = m_hotKeys[msg.WParam];

  auto showMainWin = [this,&hke,blWasMinimized](bool blForce = false)
  {
    if (blForce || hke.ShowMainWin) {
      if (blWasMinimized)
        RestoreAction();
      Application->BringToFront();
    }
  };

  //if (hke.Action == hkaPasswMsgBox || hke.Action == hkaShowMPPG)
  //{
  //  showMainWin();
  //}

  int dest = -1;

  switch (hke.Action) {
  case hkaShowMPPG:
    showMainWin(true);
    MPPasswGenBtnClick(this);
    break;
  case hkaShowPasswManager:
    showMainWin(true);
    PasswMngBtnClick(this);
    break;
  case hkaSearchDatabase:
    PasswMngForm->SearchDbForKeyword(false);
    showMainWin();
    break;
  case hkaSearchDatabaseAutotype:
    PasswMngForm->SearchDbForKeyword(true);
    showMainWin();
    break;
  case hkaSinglePassw:
    showMainWin();
    dest = gpdGuiSingle;
    break;
  case hkaPasswList:
    showMainWin();
    dest = gpdGuiList;
    break;
  case hkaPasswClipboard:
    showMainWin();
    dest = gpdClipboard;
    break;
  case hkaPasswMsgBox:
    showMainWin(true);
    dest = gpdMsgBox;
    break;
  case hkaPasswAutotype:
    showMainWin();
    dest = gpdAutotype;
    break;
  case hkaNone:
    showMainWin(true);
    break;
  }

  if (dest < 0)
    return;

  //if ((dest == gpdGuiList || dest ==  && Screen->ActiveForm != nullptr &&
  //    Screen->ActiveForm->FormState.Contains(fsModal))
  //  return;

  GeneratePassw(static_cast<GeneratePasswDest>(dest));

  if (hke.Action == hkaPasswMsgBox && !hke.ShowMainWin && blWasMinimized)
    Application->Minimize();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenuPopup(TObject *Sender)
{
  bool blEnable = true;
  if (IsDisplayDlg() || TaskCancelManager::GetInstance().GetNumTasksRunning() ||
      ProgressForm->Visible)
    blEnable = false;
  else if (Screen->ActiveForm != nullptr)
    blEnable = !Screen->ActiveForm->FormState.Contains(fsModal);

  for (int nI = 0; nI < TrayMenu->Items->Count; nI++)
    TrayMenu->Items->Items[nI]->Enabled = blEnable;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_CreateTrigramFileClick(
  TObject *Sender)
{
  CreateTrigramFileDlg->ShowModal();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_CheckForUpdatesClick(
  TObject *Sender)
{
  Screen->Cursor = crHourGlass;

  switch (TUpdateCheckThread::CheckForUpdates(true)) {
  case TUpdateCheckThread::CheckResult::Negative:
    MsgBox(TRLFormat("Your version of %s is up-to-date.", PROGRAM_NAME), MB_ICONINFORMATION);
  // no break here!
  case TUpdateCheckThread::CheckResult::Positive:
    m_lastUpdateCheck = TDateTime::CurrentDate();
    break;
  default:
    break;
  }

  Screen->Cursor = crDefault;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_DetermRandGen_ResetClick(
  TObject *Sender)
{
  if (g_pKeySeededPRNG) {
    g_pKeySeededPRNG->Reset();
    ShowInfoBox(TRL("Deterministic random generator has been RESET."));
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_DetermRandGen_DeactivateClick(
  TObject *Sender)
{
  if (g_pKeySeededPRNG) {
    g_pKeySeededPRNG.reset();
    g_pRandSrc = &m_randPool;
    m_passwGen.RandGen = g_pRandSrc;

    Caption = PROGRAM_NAME;
    Application->Title = PROGRAM_NAME;
    TrayIcon->Hint = PROGRAM_NAME;

    MainMenu_Tools_DetermRandGen_Reset->Enabled = false;
    MainMenu_Tools_DetermRandGen_Deactivate->Enabled = false;

    ShowInfoBox(TRL("Deterministic random generator has been DEACTIVATED."));
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_EnablePasswTestClick(
  TObject *Sender)
{
  if (PasswBoxMenu_EnablePasswTest->Checked)
    PasswBox->Tag |= PASSWBOX_TAG_PASSW_TEST;
  else
    PasswBox->Tag &= ~PASSWBOX_TAG_PASSW_TEST;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_File_ExitClick(TObject *Sender)
{
  Close();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_EncryptCopyClick(TObject *Sender)
{
  SecureWString sText = GetEditBoxSelTextBuf(PasswBox);
  CryptText(true, &sText);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Options_ConfigClick(TObject *Sender)
{
  if (ConfigurationDlg->ShowModal() == mrOk) {
    if (g_terminateAction == TerminateAction::RestartProgram)
      Close();
  }
  else
    ConfigurationDlg->SetOptions(g_config);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_CreateRandDataFileClick(
  TObject *Sender)
{
  CreateRandDataFileDlg->ShowModal();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormResize(TObject *Sender)
{
  Tag = MAINFORM_TAG_REPAINT_COMBOBOXES;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Options_AlwaysOnTopClick(
  TObject *Sender)
{
  TopMostManager::GetInstance().AlwaysOnTop =
    MainMenu_Options_AlwaysOnTop->Checked;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_ProvideAddEntropy_AsTextClick(
  TObject *Sender)
{
  if (ProvideEntropyDlg->ShowModal() == mrOk)
    UpdateEntropyProgress();
  ClearEditBoxTextBuf(ProvideEntropyDlg->TextBox);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_ProvideAddEntropy_FromFileClick(
  TObject *Sender)
{
  OpenDlg->FilterIndex = 0;
  OpenDlg->Title = TRL("Select high-entropy file");

  BeforeDisplayDlg();

  if (OpenDlg->Execute()) {
    //TFileStream* pFile = nullptr;
    WString sMsg;
    bool blSuccess = false;

    Screen->Cursor = crHourGlass;

    try {
      std::unique_ptr<TFileStream> pFile(new TFileStream(OpenDlg->FileName, fmOpenRead));

      const int IO_BUFSIZE = 65536;
      SecureMem<word8> buf(IO_BUFSIZE);
      int nBytesRead;
      word32 lEntBits = 0;

      while ((nBytesRead = pFile->Read(buf, IO_BUFSIZE)) != 0 &&
             lEntBits < 1'000'000'000) {
        lEntBits += m_entropyMng.AddData(buf, nBytesRead, 4, 4);
      }

      sMsg = TRLFormat("%d bits of entropy have been added to the random pool.",
          lEntBits);
      blSuccess = true;
    }
    catch (Exception& e) {
      sMsg = TRL("Could not open file.");
    }

    m_randPool.Flush();
    UpdateEntropyProgress();

    Screen->Cursor = crDefault;

    MsgBox(sMsg, blSuccess ? MB_ICONINFORMATION : MB_ICONERROR);
  }

  AfterDisplayDlg();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswGroupMouseMove(TObject *Sender,
  TShiftState Shift, int X, int Y)
{
  if (Shift.Contains(ssLeft))
    StartEditBoxDragDrop(PasswBox);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::SpecifyLengthCheckClick(TObject *Sender)
{
  SpecifyLengthBox->Enabled = SpecifyLengthCheck->Checked;
  if (SpecifyLengthCheck->Checked && !m_blStartup)
    SpecifyLengthBox->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_AddToDbClick(TObject *Sender)
{
  SecureWString sPassw = GetEditBoxSelTextBuf(PasswBox);
  PasswMngForm->AddPassw(sPassw, false);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswMngBtnClick(TObject *Sender)
{
  //if (g_nAppState & APPSTATE_HIDDEN)
    //ShowWindow(Application->Handle, SW_SHOW);
  if (PasswMngForm->Visible) {
    PasswMngForm->WindowState = wsNormal;
    ShowWindow(PasswMngForm->Handle, SW_SHOW);
    PasswMngForm->SetFocus();
    //if ((g_nAppState & APPSTATE_HIDDEN) && PasswMngForm->Tag != 0)
    //  PasswMngForm->FormActivate(this);
  }
  else
    PasswMngForm->Show();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_SaveAsFileClick(TObject *Sender)
{
  SaveDlg->Title = TRL("Save password");
  Tag = 1;
  if (!SaveDlg->Execute()) {
    Tag = 0;
    return;
  }
  Tag = 0;

  WString sFileName = SaveDlg->FileName;

  bool blSuccess = false;
  WString sMsg;

  try {
    SecureWString sPassw = GetEditBoxTextBuf(PasswBox);

    std::unique_ptr<TStringFileStreamW> pFile(new TStringFileStreamW(
       sFileName, fmCreate, g_config.FileEncoding, true, PASSW_MAX_BYTES));

    if (!pFile->WriteString(sPassw, sPassw.StrLen()))
      OutOfDiskSpaceError();

    blSuccess = true;
    sMsg = TRLFormat("File \"%s\" successfully created.",
      ExtractFileName(sFileName).c_str());
  }
  catch (Exception& e) {
    sMsg = TRLFormat("Error while creating file\n\"%s\":\n%s.", sFileName.c_str(),
        e.Message.c_str());
  }

  Screen->Cursor = crDefault;

  MsgBox(sMsg, (blSuccess) ? MB_ICONINFORMATION : MB_ICONERROR);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ListMenu_RemoveEntryClick(TObject *Sender)
{
  int nItemIdx = ((TComboBox*) ListMenu->PopupComponent)->ItemIndex;
  if (nItemIdx >= 0)
    ((TComboBox*) ListMenu->PopupComponent)->Items->Delete(nItemIdx);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_OptionsClick(TObject *Sender)
{
  int nCountdown = PasswEnterDlg->GetPasswCacheExpiryCountdown();
  MainMenu_Options_ClearPasswCache->Enabled = nCountdown > 0;
  MainMenu_Options_ClearPasswCache->Caption = TRL("Clear Password Cache");
  if (nCountdown > 0) {
    MainMenu_Options_ClearPasswCache->Caption += nCountdown > 99 ?
      FormatW(" (%1.1fmin)", nCountdown / 60.0) : FormatW(" (%dsec)", nCountdown);
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Options_ClearPasswCacheClick(TObject *Sender)
{
  PasswEnterDlg->ClearPasswCache();
  ShowInfoBox(TRL("Password cache cleared."));
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_EnterDonorKeyClick(TObject *Sender)
{
  WString sInput = m_asDonorKey;
  BeforeDisplayDlg();
  if (InputQuery(TRL("Donor Key"), TRL("Enter donor key:"), sInput)) {
    AnsiString asId;
    int nType;
    WString sMsg;
    switch (CheckDonorKey(sInput, &asId, &nType)) {
    case DONOR_KEY_VALID:
      m_asDonorKey = sInput.Trim();
      g_asDonorInfo = asId.Trim();
      sMsg = TRLFormat("Your Donor ID is: %s\n"
          "Supported number of updates: %s",
          WString(asId).c_str(),
          nType == DONOR_TYPE_STD ? IntToStr(DONOR_STD_NUM_UPDATES).c_str() :
            TRL("Unlimited").c_str());
      if (MsgBox(TRL("Thank you for your support!") + "\n\n" + sMsg + "\n\n" +
          TRL("Copy this information to the clipboard?"),
          MB_ICONQUESTION + MB_YESNO) == IDYES)
        Clipboard()->AsText = sMsg;
      SetDonorUI(nType);
      AboutForm->SetDonorUI();
      break;
    case DONOR_KEY_EXPIRED:
      MsgBox(TRL("Donor key has expired."), MB_ICONERROR);
      break;
    default:
      MsgBox(TRL("Donor key is invalid."), MB_ICONERROR);
    }
  }
  AfterDisplayDlg();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Options_HideEntProgressClick(TObject *Sender)

{
  m_blShowEntProgress = !m_blShowEntProgress;
  MainMenu_Options_HideEntProgress->Checked = !m_blShowEntProgress;
  if (m_blShowEntProgress)
    UpdateEntropyProgress(true);
  else
    StatusBar->Panels->Items[2]->Text = WString();
  //StatusBar->Panels->Items[4]->Text = "";
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_HelpClick(TObject *Sender)
{
  MainMenu_Help_TotalEntBits->Caption = TRLFormat("Total Entropy Bits: %d",
      m_entropyMng.TotalEntropyBits);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Tools_DetermRandGen_SetupClick(TObject *Sender)

{
  SecureWString sPassw;
  bool blSuccess = PasswEnterDlg->Execute(PASSWENTER_FLAG_CONFIRMPASSW |
      PASSWENTER_FLAG_ENABLEPASSWCACHE,
      TRL("Master password"), this) == mrOk;

  if (blSuccess)
    sPassw = PasswEnterDlg->GetPassw();

  PasswEnterDlg->Clear();
  m_randPool.Flush();

  if (!blSuccess)
    return;

  SecureMem<word8> key(AESCtrPRNG::KEY_SIZE);

  sha256_hmac(sPassw.Bytes(), sPassw.StrLenBytes(),
    reinterpret_cast<const word8*>(MPPG_KEYGEN_SALTSTR),
    sizeof(MPPG_KEYGEN_SALTSTR) - 1, key.Data(), 0);

  if (!g_pKeySeededPRNG)
    g_pKeySeededPRNG.reset(new AESCtrPRNG);

  g_pKeySeededPRNG->SeedWithKey(key, key.Size(), nullptr, 0);

  UseKeySeededPRNG();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ReloadScriptBtnClick(TObject *Sender)
{
  m_pScript.reset();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::BrowseBtn2Click(TObject *Sender)
{
  BrowseBtn2->Refresh();
  OpenDlg->FilterIndex = 4;
  OpenDlg->Title = TRL("Select Lua script file");
  BeforeDisplayDlg();
  if (OpenDlg->Execute())
    ScriptList->Text = OpenDlg->FileName;
  AfterDisplayDlg();
  ScriptList->SetFocus();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayIconDblClick(TObject *Sender)
{
  if (g_nAppState & APPSTATE_MINIMIZED)
    Application->Restore();
  else if (!Application->ShowMainForm)
    RestoreAction();
  else
    Application->BringToFront();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::ProfileListSelect(TObject *Sender)
{
  if (ProfileList->ItemIndex >= 0)
    LoadProfile(ProfileList->ItemIndex);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AddProfileBtnClick(TObject *Sender)
{
  int nFlags = ProfileEditDlg->Execute(true);
  if (nFlags & TProfileEditDlg::MODIFIED)
    UpdateProfileControls();
  if (nFlags & TProfileEditDlg::ADDED)
    ProfileList->ItemIndex = g_profileList.size() - 1;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::PasswBoxMenu_PerformAutotypeClick(TObject *Sender)
{
  SecureWString sPassw = GetEditBoxTextBuf(PasswBox);
  if (g_config.MinimizeAutotype) {
    g_nAppState |= APPSTATE_AUTOTYPE;
    Application->Minimize();
    SendKeys sk(g_config.AutotypeDelay);
    sk.SendString(sPassw.c_str());
    g_nAppState &= ~APPSTATE_AUTOTYPE;
  }
  else {
    TSendKeysThread::TerminateAndWait();
    if (!TSendKeysThread::ThreadRunning())
      new TSendKeysThread(Handle, g_config.AutotypeDelay,
        sPassw.c_str(), sPassw.StrLen());
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::FormCloseQuery(TObject *Sender, bool &CanClose)
{
  if (g_config.ConfirmExit && g_terminateAction == TerminateAction::None &&
      MsgBox(TRLFormat("Are you sure you want to exit\n%s?", PROGRAM_NAME),
      MB_ICONQUESTION + MB_YESNO + MB_DEFBUTTON2) == IDNO)
  {
    CanClose = false;
    return;
  }

  CanClose = PasswMngForm->CloseQuery();

  if (CanClose) {
    try {
      TSendKeysThread::TerminateAndWait(250);

      if (CheckThreadRunning()) {
        TaskCancelManager::GetInstance().CancelAll(
          TaskCancelReason::ProgramTermination);

        auto cancelFlag = std::make_shared<std::atomic<bool>>(false);

        auto pTask = TTask::Create([&cancelFlag]()
        {
          while (CheckThreadRunning() && !cancelFlag)
            TThread::Sleep(10);
        });

        pTask->Start();

        if (!pTask->Wait(250) && !ProgressForm->Visible) {
          ProgressForm->ExecuteModal(this, PROGRAM_NAME,
            TRL("Waiting for active threads to finish ..."), cancelFlag,
            [&pTask](unsigned int timeout)
            {
              return pTask->Wait(timeout);
            });
        }
      }
    }
    catch (...)
    {
    }
  }
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::MainMenu_Help_GetTranslationsClick(TObject *Sender)
{
  ExecuteShellOp(PROGRAM_URL_TRANSL);
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::TrayMenu_ResetWindowPosClick(TObject *Sender)
{
  RECT rect;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
  int y = rect.top, x = rect.left;
  Top = y;
  Left = x;
  PasswListForm->Top = y;
  PasswListForm->Left = x;
  MPPasswGenForm->Top = y;
  MPPasswGenForm->Left = x;
  QuickHelpForm->Top = y;
  QuickHelpForm->Left = x;
  PasswMngForm->Top = y;
  PasswMngForm->Left = x;
  PasswMngKeyValDlg->Top = y;
  PasswMngKeyValDlg->Left = x;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AdvancedOptionsMenu_DeactivateAllClick(TObject *Sender)
{
  m_passwOptions.Flags = 0;
  SetAdvancedBtnCaption();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::AdvancedOptionsMenu_DeactivateAllStarredClick(TObject *Sender)
{
  m_passwOptions.Flags &= ~PASSWOPTIONS_STARRED_BITFIELD;
  SpecifyLengthCheck->Checked = false;
  SetAdvancedBtnCaption();
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::OnQueryEndSession(TWMQueryEndSession& msg)
{
  g_terminateAction = TerminateAction::SystemShutdown;
  msg.Result = 1;
}
//---------------------------------------------------------------------------
void __fastcall TMainForm::OnEndSession(TWMEndSession& msg)
{
  if (msg.EndSession) {
    // first perform close actions and save settings
    // before trying to cancel running threads
    TCloseAction ca = caNone;
    FormClose(this, ca);

    PasswEnterDlg->OnEndSession();
    MPPasswGenForm->OnEndSession();

    TaskCancelManager::GetInstance().CancelAll(TaskCancelReason::SystemShutdown);
    TSendKeysThread::TerminateAndWait(250);

    if (CheckThreadRunning()) {
      auto pTask = TTask::Create([this]()
      {
        while (CheckThreadRunning())
         TThread::Sleep(10);
      });

      pTask->Start();

      int nTimeout = 0;
      while (!pTask->Wait(100) && nTimeout < 1000) {
        Application->ProcessMessages();
        nTimeout += 100;
      }
    }

    m_randPool.Flush();
  }
  else
    g_terminateAction = TerminateAction::None;
  msg.Result = 0;
}
//---------------------------------------------------------------------------
