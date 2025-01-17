﻿// validator: no-self-include
/*
main.cpp

Функция main.
*/
/*
Copyright © 1996 Eugene Roshal
Copyright © 2000 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Internal:
#include "keys.hpp"
#include "chgprior.hpp"
#include "farcolor.hpp"
#include "filepanels.hpp"
#include "panel.hpp"
#include "fileedit.hpp"
#include "fileview.hpp"
#include "manager.hpp"
#include "ctrlobj.hpp"
#include "scrbuf.hpp"
#include "lang.hpp"
#include "language.hpp"
#include "imports.hpp"
#include "syslog.hpp"
#include "interf.hpp"
#include "keyboard.hpp"
#include "clipboard.hpp"
#include "pathmix.hpp"
#include "dirmix.hpp"
#include "elevation.hpp"
#include "cmdline.hpp"
#include "console.hpp"
#include "configdb.hpp"
#include "colormix.hpp"
#include "treelist.hpp"
#include "plugins.hpp"
#include "notification.hpp"
#include "exception_handler.hpp"
#include "exception_handler_test.hpp"
#include "constitle.hpp"
#include "string_utils.hpp"
#include "cvtname.hpp"
#include "drivemix.hpp"
#include "new_handler.hpp"
#include "global.hpp"
#include "locale.hpp"

// Platform:
#include "platform.env.hpp"
#include "platform.memory.hpp"
#include "platform.security.hpp"

// Common:
#include "common/algorithm.hpp"
#include "common/range.hpp"
#include "common/scope_exit.hpp"
#include "common/string_utils.hpp"

// External:

//----------------------------------------------------------------------------

#ifdef ENABLE_TESTS
#define TESTS_ENTRYPOINT_ONLY
#include "testing.hpp"
#undef TESTS_ENTRYPOINT_ONLY
#endif

global *Global = nullptr;

static void show_help()
{
	static const auto HelpMsg =
		L"Usage: far [switches] [apath [ppath]]\n\n"
		L"where\n"
		L"  apath - path to a folder (or a file or an archive or command with prefix)\n"
		L"          for the active panel\n"
		L"  ppath - path to a folder (or a file or an archive or command with prefix)\n"
		L"          for the passive panel\n"
		L"The following switches may be used in the command line:\n"
		L" -?   This help.\n"
		L" -clearcache [profilepath [localprofilepath]]\n"
		L"      Clear plugins cache.\n"
		L" -co  Forces FAR to load plugins from the cache only.\n"
#ifdef DIRECT_RT
		L" -do  Direct output.\n"
#endif
		L" -e[<line>[:<pos>]] <filename>\n"
		L"      Edit the specified file.\n"
		L" -export <out.farconfig> [profilepath [localprofilepath]]\n"
		L"      Export settings.\n"
		L" -import <in.farconfig> [profilepath [localprofilepath]]\n"
		L"      Import settings.\n"
		L" -m   Do not load macros.\n"
		L" -ma  Do not execute auto run macros.\n"
		L" -p[<path>]\n"
		L"      Search for \"common\" plugins in the directory, specified by <path>.\n"
		L" -ro[-] Read-Only or Normal config mode.\n"
		L" -s <profilepath> [<localprofilepath>]\n"
		L"      Custom location for Far configuration files - overrides Far.exe.ini.\n"
		L" -set:<parameter>=<value>\n"
		L"      Override the configuration parameter, see far:config for details.\n"
		L" -t <path>\n"
		L"      Location of Far template configuration file - overrides Far.exe.ini.\n"
		L" -title[:<title>]\n"
		L"      If <title> is given, use it as the window title; otherwise inherit\n"
		L"      the console window's title. The dynamically changing value of the \n"
		L"      default title can be reused via the \"%Default\" placeholder.\n"
#ifndef NO_WRAPPER
		L" -u <username>\n"
		L"      Allows to have separate registry settings for different users.\n"
		L"      Affects only 1.x Far Manager plugins.\n"
#endif // NO_WRAPPER
		L" -v <filename>\n"
		L"      View the specified file. If <filename> is -, data is read from the stdin.\n"
		L" -w[-] Stretch to console window instead of console buffer or vise versa.\n"
		""sv;

	std::wcout << HelpMsg << std::flush;
}

static int MainProcess(
    const string& EditName,
    const string& ViewName,
    const string& DestName1,
    const string& DestName2,
    int StartLine,
    int StartChar
)
{
		SCOPED_ACTION(ChangePriority)(THREAD_PRIORITY_NORMAL);

		FarColor InitAttributes;
		if (!console.GetTextAttributes(InitAttributes))
			InitAttributes = colors::ConsoleColorToFarColor(F_LIGHTGRAY | B_BLACK);
		SetRealColor(colors::PaletteColorToFarColor(COL_COMMANDLINEUSERSCREEN));

		string ename(EditName),vname(ViewName), apanel(DestName1),ppanel(DestName2);
		if (ConfigProvider().ShowProblems())
		{
			ename.clear();
			vname.clear();
			StartLine = StartChar = -1;
			apanel = Global->Opt->ProfilePath;
			ppanel = Global->Opt->LocalProfilePath;
		}

		if (!ename.empty() || !vname.empty())
		{
			Global->OnlyEditorViewerUsed = true;

			_tran(SysLog(L"create dummy panels"));
			Global->CtrlObject->CreateDummyFilePanels();
			Global->WindowManager->PluginCommit();

			Global->CtrlObject->Plugins->LoadPlugins();
			Global->CtrlObject->Macro.LoadMacros(true, true);

			if (!ename.empty())
			{
				const auto ShellEditor = FileEditor::create(ename, CP_DEFAULT, FFILEEDIT_CANNEWFILE | FFILEEDIT_ENABLEF6, StartLine, StartChar);
				_tran(SysLog(L"make shelleditor %p",ShellEditor));

				if (!ShellEditor->GetExitCode())  // ????????????
				{
					Global->WindowManager->ExitMainLoop(0);
				}
			}
			// TODO: Этот else убрать только после разборок с возможностью задавать несколько /e и /v в ком.строке
			else if (!vname.empty())
			{
				const auto ShellViewer = FileViewer::create(vname, true);

				if (!ShellViewer->GetExitCode())
				{
					Global->WindowManager->ExitMainLoop(0);
				}

				_tran(SysLog(L"make shellviewer, %p",ShellViewer));
			}

			Global->WindowManager->EnterMainLoop();
		}
		else
		{
			int DirCount=0;

			// воспользуемся тем, что ControlObject::Init() создает панели
			// юзая Global->Opt->*

			const auto SetupPanel = [&](bool active)
			{
				++DirCount;
				string strPath = active? apanel : ppanel;
				if (os::fs::is_file(strPath))
				{
					CutToParent(strPath);
				}

				bool Root = false;
				const auto Type = ParsePath(strPath, nullptr, &Root);
				if(Root && (Type == root_type::drive_letter || Type == root_type::unc_drive_letter || Type == root_type::volume))
				{
					AddEndSlash(strPath);
				}

				auto& CurrentPanelOptions = (Global->Opt->LeftFocus == active)? Global->Opt->LeftPanel : Global->Opt->RightPanel;
				CurrentPanelOptions.m_Type = static_cast<int>(panel_type::FILE_PANEL);  // сменим моду панели
				CurrentPanelOptions.Visible = true;     // и включим ее
				CurrentPanelOptions.Folder = strPath;
			};

			if (!apanel.empty())
			{
				SetupPanel(true);

				if (!ppanel.empty())
				{
					SetupPanel(false);
				}
			}

			// теперь все готово - создаем панели!
			Global->CtrlObject->Init(DirCount);

			// а теперь "провалимся" в каталог или хост-файл (если получится ;-)
			if (!apanel.empty())  // активная панель
			{
				const auto ActivePanel = Global->CtrlObject->Cp()->ActivePanel();
				const auto AnotherPanel = Global->CtrlObject->Cp()->PassivePanel();

				if (!ppanel.empty())  // пассивная панель
				{
					FarChDir(AnotherPanel->GetCurDir());

					if (IsPluginPrefixPath(ppanel))
					{
						AnotherPanel->Parent()->SetActivePanel(AnotherPanel);

						execute_info Info;
						Info.DisplayCommand = ppanel;
						Info.Command = ppanel;

						Global->CtrlObject->CmdLine()->ExecString(Info);
						ActivePanel->Parent()->SetActivePanel(ActivePanel);
					}
					else
					{
						const auto strPath = PointToName(ppanel);

						if (!strPath.empty())
						{
							if (AnotherPanel->GoToFile(strPath))
								AnotherPanel->ProcessKey(Manager::Key(KEY_CTRLPGDN));
						}
					}
				}

				FarChDir(ActivePanel->GetCurDir());

				if (IsPluginPrefixPath(apanel))
				{
					execute_info Info;
					Info.DisplayCommand = apanel;
					Info.Command = apanel;

					Global->CtrlObject->CmdLine()->ExecString(Info);
				}
				else
				{
					const auto strPath = PointToName(apanel);

					if (!strPath.empty())
					{
						if (ActivePanel->GoToFile(strPath))
							ActivePanel->ProcessKey(Manager::Key(KEY_CTRLPGDN));
					}
				}

				// !!! ВНИМАНИЕ !!!
				// Сначала редравим пассивную панель, а потом активную!
				AnotherPanel->Redraw();
				ActivePanel->Redraw();
			}

			Global->WindowManager->EnterMainLoop();
		}

		TreeList::FlushCache();

		// очистим за собой!
		SetScreen({ 0, 0, ScrX, ScrY }, L' ', colors::PaletteColorToFarColor(COL_COMMANDLINEUSERSCREEN));
		console.SetTextAttributes(InitAttributes);
		Global->ScrBuf->ResetLockCount();
		Global->ScrBuf->Flush();

		return 0;
}

static void InitTemplateProfile(string &strTemplatePath)
{
	if (strTemplatePath.empty())
	{
		strTemplatePath = GetFarIniString(L"General"sv, L"TemplateProfile"sv, path::join(L"%FARHOME%"sv, L"Default.farconfig"sv));
	}

	if (!strTemplatePath.empty())
	{
		strTemplatePath = ConvertNameToFull(unquote(os::env::expand(strTemplatePath)));
		DeleteEndSlash(strTemplatePath);

		if (os::fs::is_directory(strTemplatePath))
			path::append(strTemplatePath, L"Default.farconfig"sv);

		Global->Opt->TemplateProfilePath = strTemplatePath;
	}
}

static void InitProfile(string &strProfilePath, string &strLocalProfilePath)
{
	if (Global->Opt->ReadOnlyConfig < 0) // do not override 'far /ro', 'far /ro-'
		Global->Opt->ReadOnlyConfig = GetFarIniInt(L"General"sv, L"ReadOnlyConfig"sv, 0);

	if (!strProfilePath.empty())
	{
		strProfilePath = ConvertNameToFull(unquote(os::env::expand(strProfilePath)));
	}
	if (!strLocalProfilePath.empty())
	{
		strLocalProfilePath = ConvertNameToFull(unquote(os::env::expand(strLocalProfilePath)));
	}

	if (strProfilePath.empty())
	{
		const auto UseSystemProfiles = GetFarIniInt(L"General"sv, L"UseSystemProfiles"sv, 1);
		if (UseSystemProfiles)
		{
			const auto GetShellProfilePath = [](int Idl)
			{
				wchar_t Buffer[MAX_PATH];
				SHGetFolderPath(nullptr, Idl | (Global->Opt->ReadOnlyConfig? 0 : CSIDL_FLAG_CREATE), nullptr, SHGFP_TYPE_CURRENT, Buffer);
				return path::join(Buffer, L"Far Manager"sv, L"Profile"sv);
			};

			// roaming data default path: %APPDATA%\Far Manager\Profile
			Global->Opt->ProfilePath = GetShellProfilePath(CSIDL_APPDATA);

			Global->Opt->LocalProfilePath = UseSystemProfiles == 2?
				Global->Opt->ProfilePath :
				// local data default path: %LOCALAPPDATA%\Far Manager\Profile
				GetShellProfilePath(CSIDL_LOCAL_APPDATA);
		}
		else
		{
			const auto strUserProfileDir = GetFarIniString(L"General"sv, L"UserProfileDir"sv, path::join(L"%FARHOME%"sv, L"Profile"sv));
			const auto strUserLocalProfileDir = GetFarIniString(L"General"sv, L"UserLocalProfileDir"sv, strUserProfileDir);
			Global->Opt->ProfilePath = ConvertNameToFull(unquote(os::env::expand(strUserProfileDir)));
			Global->Opt->LocalProfilePath = ConvertNameToFull(unquote(os::env::expand(strUserLocalProfileDir)));
		}
	}
	else
	{
		Global->Opt->ProfilePath = strProfilePath;
		Global->Opt->LocalProfilePath = !strLocalProfilePath.empty()? strLocalProfilePath : strProfilePath;
	}

	Global->Opt->LoadPlug.strPersonalPluginsPath = path::join(Global->Opt->ProfilePath, L"Plugins"sv);

	os::env::set(L"FARPROFILE"sv, Global->Opt->ProfilePath);
	os::env::set(L"FARLOCALPROFILE"sv, Global->Opt->LocalProfilePath);

	if (!Global->Opt->ReadOnlyConfig)
	{
		CreatePath(path::join(Global->Opt->ProfilePath, L"PluginsData"sv), true);

		const auto SingleProfile = equal_icase(Global->Opt->ProfilePath, Global->Opt->LocalProfilePath);

		if (!SingleProfile)
			CreatePath(path::join(Global->Opt->LocalProfilePath, L"PluginsData"sv), true);

		const auto RandomName = GuidToStr(CreateUuid());

		if (!os::fs::can_create_file(path::join(Global->Opt->ProfilePath, RandomName)) ||
			(!SingleProfile && !os::fs::can_create_file(path::join(Global->Opt->LocalProfilePath, RandomName))))
		{
			Global->Opt->ReadOnlyConfig = true;
		}
	}
}

static bool ProcessServiceModes(span<const wchar_t* const> const Args, int& ServiceResult)
{
	const auto isArg = [](const wchar_t* Arg, string_view const Name)
	{
		return (*Arg == L'/' || *Arg == L'-') && equal_icase(Arg + 1, Name);
	};

	if (Args.size() == 4 && IsElevationArgument(Args[0])) // /service:elevation {GUID} PID UsePrivileges
	{
		ServiceResult = ElevationMain(Args[1], std::wcstoul(Args[2], nullptr, 10), *Args[3] == L'1');
		return true;
	}

	if (in_range(2u, Args.size(), 5u) && (isArg(Args[0], L"export"sv) || isArg(Args[0], L"import"sv)))
	{
		const auto Export = isArg(Args[0], L"export"sv);
		string strProfilePath(Args.size() > 2? Args[2] : L""sv), strLocalProfilePath(Args.size() > 3 ? Args[3] : L""), strTemplatePath(Args.size() > 4 ? Args[4] : L"");
		InitTemplateProfile(strTemplatePath);
		InitProfile(strProfilePath, strLocalProfilePath);
		Global->m_ConfigProvider = new config_provider(Export? config_provider::mode::m_export : config_provider::mode::m_import);
		ConfigProvider().ServiceMode(Args[1]);
		ServiceResult = 0;
		return true;
	}

	if (in_range(1u, Args.size(), 3u) && isArg(Args[0], L"clearcache"sv))
	{
		string strProfilePath(Args.size() > 1? Args[1] : L""sv);
		string strLocalProfilePath(Args.size() > 2? Args[2] : L""sv);
		InitProfile(strProfilePath, strLocalProfilePath);
		(void)config_provider{config_provider::clear_cache{}};
		ServiceResult = 0;
		return true;
	}

	return false;
}

static void UpdateErrorMode()
{
	Global->ErrorMode |= SEM_NOGPFAULTERRORBOX;

	if (ConfigProvider().GeneralCfg()->GetValue<bool>(L"System.Exception"sv, L"IgnoreDataAlignmentFaults"sv))
	{
		Global->ErrorMode |= SEM_NOALIGNMENTFAULTEXCEPT;
	}

	os::set_error_mode(Global->ErrorMode);
}

static int mainImpl(span<const wchar_t* const> const Args)
{
	setlocale(LC_ALL, "");

	// Must be static - dependent static objects exist
	static SCOPED_ACTION(os::com::initialize);

	SCOPED_ACTION(global);

	std::optional<elevation::suppress> NoElevationDuringBoot(std::in_place);

	os::set_error_mode(Global->ErrorMode);

	RegisterTestExceptionsHook();

	os::EnableLowFragmentationHeap();

	if(!console.IsFullscreenSupported())
	{
		const BYTE ReserveAltEnter = 0x8;
		imports.SetConsoleKeyShortcuts(TRUE, ReserveAltEnter, nullptr, 0);
	}

	os::fs::InitCurrentDirectory();

	if (os::fs::GetModuleFileName(nullptr, nullptr, Global->g_strFarModuleName))
	{
		PrepareDiskPath(Global->g_strFarModuleName);
	}

	Global->g_strFarINI = concat(Global->g_strFarModuleName, L".ini"sv);
	Global->g_strFarPath = Global->g_strFarModuleName;
	CutToSlash(Global->g_strFarPath,true);
	os::env::set(L"FARHOME"sv, Global->g_strFarPath);
	AddEndSlash(Global->g_strFarPath);

	if (os::security::is_admin())
		os::env::set(L"FARADMINMODE"sv, L"1"sv);
	else
		os::env::del(L"FARADMINMODE"sv);

	{
		int ServiceResult;
		if (ProcessServiceModes(Args, ServiceResult))
			return ServiceResult;
	}

	SCOPED_ACTION(listener)(update_environment, &ReloadEnvironment);
	SCOPED_ACTION(listener)(update_intl, [] { locale.invalidate(); });
	SCOPED_ACTION(listener)(update_devices, &UpdateSavedDrives);

	_OT(SysLog(L"[[[[[[[[New Session of FAR]]]]]]]]]"));

	string strEditName;
	string strViewName;
	string DestNames[2];
	int StartLine=-1,StartChar=-1;
	int CntDestName=0; // количество параметров-имен каталогов

	string strProfilePath, strLocalProfilePath, strTemplatePath;

	std::optional<string> CustomTitle;

	Options::overrides Overrides;
	FOR_RANGE(Args, Iter)
	{
		const auto& Arg = *Iter;
		if ((Arg[0]==L'/' || Arg[0]==L'-') && Arg[1])
		{
			switch (upper(Arg[1]))
			{
				case L'E':
					if (std::iswdigit(Arg[2]))
					{
						StartLine = static_cast<int>(std::wcstol(Arg + 2, nullptr, 10));
						const wchar_t *ChPtr = wcschr(Arg + 2, L':');

						if (ChPtr)
							StartChar = static_cast<int>(std::wcstol(ChPtr + 1, nullptr, 10));
					}

					if (Iter + 1 != Args.end())
					{
						strEditName = *++Iter;
					}
					break;

				case L'V':
					if (Iter + 1 != Args.end())
					{
						strViewName = *++Iter;
					}
					break;

				case L'M':
					switch (upper(Arg[2]))
					{
					case L'\0':
						Global->Opt->Macro.DisableMacro|=MDOL_ALL;
						break;

					case L'A':
						if (!Arg[3])
							Global->Opt->Macro.DisableMacro|=MDOL_AUTOSTART;
						break;
					}
					break;

#ifndef NO_WRAPPER
				case L'U':
					if (Iter + 1 != Args.end())
					{
						//Affects OEM plugins only!
						Global->strRegUser = *++Iter;
					}
					break;
#endif // NO_WRAPPER

				case L'S':
					{
						constexpr auto SetParam = L"set:"sv;
						if (starts_with_icase(Arg + 1, SetParam))
						{
							if (const auto EqualPtr = wcschr(Arg + 1, L'='))
							{
								Overrides.emplace(string(Arg + 1 + SetParam.size(), EqualPtr), EqualPtr + 1);
							}
						}
						else if (Iter + 1 != Args.end())
						{
							strProfilePath = *++Iter;
							const auto Next = Iter + 1;
							if (Next != Args.end() && *Next[0] != L'-'  && *Next[0] != L'/')
							{
								strLocalProfilePath = *Next;
								Iter = Next;
							}
						}
					}
					break;

				case L'T':
					{
						const auto Title = L"title"sv;
						if (starts_with_icase(Arg + 1, Title))
						{
							if (Arg[1 + Title.size()] == L':')
								CustomTitle = Arg + 1 + Title.size() + 1;
							else
								CustomTitle = L""sv;
						}
						else if (Iter + 1 != Args.end())
						{
							strTemplatePath = *++Iter;
						}
					}
					break;

				case L'P':
					{
						Global->Opt->LoadPlug.PluginsPersonal = false;
						Global->Opt->LoadPlug.MainPluginDir = false;

						if (Arg[2])
						{
							// we can't expand it here - some environment variables might not be available yet
							Global->Opt->LoadPlug.strCustomPluginsPath = &Arg[2];
						}
						else
						{
							// если указан -P без <путь>, то, считаем, что основные
							//  плагины не загружать вооообще!!!
							Global->Opt->LoadPlug.strCustomPluginsPath.clear();
						}
					}
					break;

				case L'C':
					if (upper(Arg[2])==L'O' && !Arg[3])
					{
						Global->Opt->LoadPlug.PluginsCacheOnly = true;
						Global->Opt->LoadPlug.PluginsPersonal = false;
					}
					break;

				case L'?':
				case L'H':
					ControlObject::ShowCopyright(1);
					show_help();
					return 0;

#ifdef DIRECT_RT
				case L'D':
					if (upper(Arg[2])==L'O' && !Arg[3])
						Global->DirectRT=true;
					break;
#endif
				case L'W':
					{
						if(Arg[2] == L'-')
						{
							Global->Opt->WindowMode= false;
						}
						else if(!Arg[2])
						{
							Global->Opt->WindowMode= true;
						}
					}
					break;

				case L'R':
					if (upper(Arg[2]) == L'O')
					{
						if (!Arg[3]) // -ro
						{
							Global->Opt->ReadOnlyConfig = TRUE;
						}
						else if (Arg[3] == L'-') // -ro-
						{
							Global->Opt->ReadOnlyConfig = FALSE;
						}
					}
					break;
			}
		}
		else // простые параметры. Их может быть max две штукА.
		{
			if (CntDestName < 2)
			{
				if (IsPluginPrefixPath(Arg))
				{
					DestNames[CntDestName++] = Arg;
				}
				else
				{
					auto ArgvI = ConvertNameToFull(unquote(os::env::expand(Arg)));
					if (os::fs::exists(ArgvI))
					{
						DestNames[CntDestName++] = ArgvI;
					}
				}
			}
		}
	}

	InitTemplateProfile(strTemplatePath);
	InitProfile(strProfilePath, strLocalProfilePath);
	Global->m_ConfigProvider = new config_provider;

	Global->Opt->Load(std::move(Overrides));

	//Инициализация массива клавиш.
	InitKeysArray();

	if (!Global->Opt->LoadPlug.MainPluginDir) //если есть ключ /p то он отменяет /co
		Global->Opt->LoadPlug.PluginsCacheOnly=false;

	if (Global->Opt->LoadPlug.PluginsCacheOnly)
	{
		Global->Opt->LoadPlug.strCustomPluginsPath.clear();
		Global->Opt->LoadPlug.MainPluginDir=false;
		Global->Opt->LoadPlug.PluginsPersonal=false;
	}

	InitConsole();
	if (CustomTitle.has_value())
		ConsoleTitle::SetUserTitle(CustomTitle->empty() ? Global->strInitTitle : *CustomTitle);

	SCOPE_EXIT
	{
		// BUGBUG, reorder to delete only in ~global()
		delete Global->CtrlObject;
		Global->CtrlObject = nullptr;

		ClearInternalClipboard();
		CloseConsole();
	};

	far_language::instance().load(Global->g_strFarPath, Global->Opt->strLanguage, static_cast<int>(lng::MNewFileName + 1));

	os::env::set(L"FARLANG"sv, Global->Opt->strLanguage);

	if (!Global->Opt->LoadPlug.strCustomPluginsPath.empty())
		Global->Opt->LoadPlug.strCustomPluginsPath = ConvertNameToFull(unquote(os::env::expand(Global->Opt->LoadPlug.strCustomPluginsPath)));

	UpdateErrorMode();

	Global->CtrlObject = new ControlObject;

	NoElevationDuringBoot.reset();

	try
	{
		return MainProcess(strEditName, strViewName, DestNames[0], DestNames[1], StartLine, StartChar);
	}
	catch (const std::exception& e)
	{
		if (ProcessStdException(e, L"mainImpl"sv))
			std::terminate();
		throw;
	}
	catch (...)
	{
		if (ProcessUnknownException(L"mainImpl"sv))
			std::terminate();
		throw;
	}
}

static int wmain_seh(int Argc, const wchar_t* const Argv[])
{
#if defined(SYSLOG)
	atexit(PrintSysLogStat);
#endif

	SCOPED_ACTION(unhandled_exception_filter);
	SCOPED_ACTION(new_handler);

	try
	{
		return mainImpl({ Argv + 1, Argv + Argc });
	}
	catch (const far_known_exception& e)
	{
		std::wcerr << e.what() << std::endl;
		return 1;
	}
	catch (const std::exception& e)
	{
		if (ProcessStdException(e, L"wmain_seh"sv))
			std::terminate();

		unhandled_exception_filter::dismiss();
		RestoreGPFaultUI();
		throw;
	}
	catch (...)
	{
		if (ProcessUnknownException(L"wmain_seh"sv))
			std::terminate();

		unhandled_exception_filter::dismiss();
		RestoreGPFaultUI();
		throw;
	}
}

//#define DEBUG_TESTS

int main()
{
#ifdef ENABLE_TESTS
	if (contains(string_view(GetCommandLine()), L"/service:test"sv))
	{
#ifdef DEBUG_TESTS
		return 0;
#else
		return testing_main(true);
#endif
	}

#ifdef DEBUG_TESTS
	return testing_main(false);
#endif
#endif

	return seh_invoke_with_ui(
	[]
	{
		// wmain is a non-standard extension and not available in gcc.
		int Argc;
		const os::memory::local::ptr Argv(CommandLineToArgvW(GetCommandLine(), &Argc));
		return wmain_seh(Argc, Argv.get());
	},
	[]() -> int
	{
		std::terminate();
	},
	L"main"sv);
}
