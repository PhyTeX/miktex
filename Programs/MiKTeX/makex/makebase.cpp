/**
 * @file makebase.cpp
 * @author Christian Schenk
 * @brief MiKTeX MakeBase
 *
 * @copyright Copyright © 1998-2024 Christian Schenk
 *
 * This file is part of the MiKTeX Make Utility Collection.
 *
 * The MiKTeX Make Utility Collection is licensed under GNU General Public
 * License version 2 or any later version.
 */

#include "config.h"

#include "makebase-version.h"

#include <miktex/Configuration/ConfigNames>
#include <miktex/Core/TemporaryDirectory>

#include "MakeUtility.h"

using namespace std;

using namespace MiKTeX::App;
using namespace MiKTeX::Core;
using namespace MiKTeX::Util;

log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("makebase"));

enum
{
    OPT_AAA = 1,
    OPT_DESTNAME,
    OPT_ENGINE_OPTION,
    OPT_NO_DUMP,
};

class MakeBase :
    public MakeUtility
{

public:

    void Run(int argc, const char** argv) override;

private:

    void Usage() override;
    void CreateDestinationDirectory() override;

    void AppendEngineOption(const char* lpszOption)
    {
        engineOptions.push_back(lpszOption);
    }

    BEGIN_OPTION_MAP(MakeBase)
        OPTION_ENTRY_SET(OPT_DESTNAME, destinationName)
        OPTION_ENTRY(OPT_ENGINE_OPTION, AppendEngineOption(optArg))
        OPTION_ENTRY_TRUE(OPT_NO_DUMP, noDumpPrimitive)
    END_OPTION_MAP();

    PathName destinationName;
    bool noDumpPrimitive = false;
    vector<string> engineOptions;
};

void MakeBase::Usage()
{
    OUT__
        << T_("Usage:") << " " << Utils::GetExeName() << " " << T_("[OPTION]... NAME") << "\n"
        << "\n"
        << T_("This program makes a METAFONT base file.") << "\n"
        << "\n"
        << T_("NAME is the name of the base file, such as 'mf'.") << "\n"
        << "\n"
        << T_("Options:") << "\n"
        << "--debug, -d " << T_("Print debugging information.") << "\n"
        << "--dest-name NAME " << T_("Destination file name.") << "\n"
        << "--disable-installer " << T_("Disable the package installer.") << "\n"
        << "--enable-installer " << T_("Enable the package installer.") << "\n"
        << "--engine-option=OPTION " << T_("Add an engine option.") << "\n"
        << "--help, -h " << T_("Print this help screen and exit.") << "\n"
        << "--no-dump " << T_("Don't issue the dump command.") << "\n"
        << "--print-only, -n " << T_("Print what commands would be executed.") << "\n"
        << "--verbose, -v " << T_("Print information on what is being done.") << "\n"
        << "--version, -V " << T_("Print the version number and exit.") << "\n";
}

namespace
{
  const struct option aLongOptions[] =
  {
        COMMON_OPTIONS,
        { "dest-name", required_argument, nullptr, OPT_DESTNAME },
        { "engine-option", required_argument, nullptr, OPT_ENGINE_OPTION },
        { "no-dump", no_argument, nullptr, OPT_NO_DUMP },
        { nullptr, no_argument, nullptr, 0 },
  };
}

void MakeBase::CreateDestinationDirectory()
{
    destinationDirectory = CreateDirectoryFromTemplate(session->GetConfigValue(MIKTEX_CONFIG_SECTION_MAKEBASE, MIKTEX_CONFIG_VALUE_DESTDIR).GetString());
}

void MakeBase::Run(int argc, const char** argv)
{
    // get options and file name
    int optionIndex = 0;
    GetOptions(argc, argv, aLongOptions, optionIndex);
    if (argc - optionIndex != 1)
    {
        FatalError(T_("Invalid command-line."));
    }
    name = argv[optionIndex];

    if (destinationName.Empty())
    {
        destinationName = name;
        destinationName.SetExtension("");
    }

    // create destination directory
    CreateDestinationDirectory();

    // make the base file name
    PathName baseFile(destinationName);
    baseFile.AppendExtension(".base");

    // make fully qualified destination file name
    PathName pathDest(destinationDirectory / destinationName.ToString());
    pathDest.AppendExtension(".base");

    Verbose(fmt::format(T_("Creating the {0} base file..."), Q_(destinationName)));

    // create a temporary working directory
    unique_ptr<TemporaryDirectory> wrkDir = TemporaryDirectory::Create();

    // invoke METAFONT to make the base file
    vector<string> arguments;
    arguments.push_back("--initialize");
    arguments.push_back("--interaction="s + "nonstopmode");
    arguments.push_back("--halt-on-error");
    arguments.insert(arguments.end(), engineOptions.begin(), engineOptions.end());
    if (!noDumpPrimitive)
    {
        arguments.push_back(fmt::format("{}; input modes; dump", name));
    }
    else
    {
        arguments.push_back(name);
    }
    if (!RunProcess(MIKTEX_MF_EXE, arguments, wrkDir->GetPathName()))
    {
        FatalError(fmt::format(T_("METAFONT failed on {0}."), Q_(name)));
    }

    // install the result
    Install(wrkDir->GetPathName() / baseFile.ToString(), pathDest);
}

#if defined(_UNICODE)
#   define MAIN wmain
#   define MAINCHAR wchar_t
#else
#   define MAIN main
#   define MAINCHAR char
#endif

int MAIN(int argc, MAINCHAR** argv)
{
#if defined(MIKTEX_WINDOWS)
    ConsoleCodePageSwitcher cpSwitcher;
#endif
    MakeBase app;
    try
    {
        vector<string> utf8args;
        utf8args.reserve(argc);
        vector<char*> newargv;
        newargv.reserve(argc + 1);
        for (int idx = 0; idx < argc; ++idx)
        {
#if defined(_UNICODE)
            utf8args.push_back(StringUtil::WideCharToUTF8(argv[idx]));
#elif defined(MIKTEX_WINDOWS)
            utf8args.push_back(StringUtil::AnsiToUTF8(argv[idx]));
#else
            utf8args.push_back(argv[idx]);
#endif
            // FIXME: eliminate const cast
            newargv.push_back(const_cast<char*>(utf8args[idx].c_str()));
        }
        newargv.push_back(nullptr);
        app.Init(Session::InitInfo(newargv[0]), newargv);
        app.Run(newargv.size() - 1, const_cast<const char**>(&newargv[0]));
        app.Finalize2(EXIT_SUCCESS);
        logger = nullptr;
        return EXIT_SUCCESS;
    }
    catch (const MiKTeXException& ex)
    {
        ex.Save();
        app.Sorry("makebase", ex);
        app.Finalize2(EXIT_FAILURE);
        logger = nullptr;
        return EXIT_FAILURE;
    }
    catch (const exception& ex)
    {
        app.Sorry("makebase", ex);
        app.Finalize2(EXIT_FAILURE);
        logger = nullptr;
        return EXIT_FAILURE;
    }
    catch (int exitCode)
    {
        app.Finalize2(exitCode);
        logger = nullptr;
        return exitCode;
    }
}
