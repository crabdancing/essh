// Copyleft (C) Alexandria Pettit 2021
// GNU GPLv3

#include <iostream>
#include <utility>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <sstream>

using namespace std;

/* ConsoleLogger allows us to easily log to stderr and handle logLine levels concisely.
 * We must declare and initialize it first so that it can be used anywhere else in our code.
 * TODO: add support for inserters or format strings with logger class. */
class ConsoleLogger {
    int verbose_level;

public:
    void setVerbose(int level = 0) {
        verbose_level = level;
    }

    void logLine(const string& line, int min_verbose_level = 1) const {
        if (verbose_level >= min_verbose_level) {
            cerr << "essh: " << line << endl;
        }
    }

    /* Sloppy alternative to figuring out how to elegantly do the stream class properly. */
    void logLine(ostringstream& line_stream, int min_verbose_level = 1) const {
        logLine(line_stream.str(), min_verbose_level);
    }
};

ConsoleLogger logger;

/* User home path, obvs. */
string getHome() {
    return getenv("HOME");
}

/* Reads entire file into memory.
 * Do not use this on large files, obvs. */
string readEntireFile(const string& file_path) {
    streampos size;
    char *file_contents = nullptr;

    ifstream file (file_path,
                   ios::in | // read in file
                   ios::binary | // open as binary in case we have non-ASCII values.
                   ios::ate // seek to end so we can tell how long the file is with tellg()
    );

    if (file.is_open())
    {
        size = file.tellg();
        file_contents = new char [size];
        file.seekg (0, ios::beg);
        file.read (file_contents, size);
        file.close();
    }

    string retval = file_contents;
    delete[] file_contents;
    return retval;
}

/* poor woman's filesystem::exists() for c++14 */
bool fileExists (const string& path) {
    return access(path.c_str(), F_OK) != -1;
}


/* Check whether we have a sshpass password file matching dest. Give us this path.
 * If it doesn't exist, give us empty string. */
string getSSHPassPath(const string& dest) {
    string path_to_sshpass_pw_file = getHome() + "/.ssh/sshpass/" + dest;
    if (fileExists(path_to_sshpass_pw_file)) {
        logger.logLine(string("sshpass password file found: ") + string(path_to_sshpass_pw_file));
        string pw = readEntireFile(path_to_sshpass_pw_file);
        return path_to_sshpass_pw_file;
    }
    return "";
}


void callHookFamily(const string& prefix, const string& dest) {
    ostringstream path_to_hook_script;
    path_to_hook_script << getHome() << "/.ssh/" << prefix << ".d/" << dest;
    string log_string;
    if (fileExists(path_to_hook_script.str())) {
        log_string += "running ";
        system(path_to_hook_script.str().c_str());
    } else {
        log_string += "no ";
    }
    ostringstream log;
    log << log_string  << prefix << ".d in: " << path_to_hook_script.str();
    logger.logLine(log.str());
}

vector<string> cargsToStringArgs(int argc, char **argv) {
    vector<string> args;
    // skips first string via init i at 1 (call path)
    // iterate over all, and convert to string because lazy.
    for (int i=1;i<argc;i++) {
        args.emplace_back(argv[i]);
    }
    return args;
}

/* Read in SSH args and figure out the stuff we're interested in.
 * (i.e., should we switch to verbose mode? What is the destination?) */
class SSHArgs {
    int verbose = 0;
    string ssh_dest;
    bool expecting_value = false;

    static bool flagIsVerbose(char flag) {
        return flag == 'v';
    }

    static bool flagImpliesValueLater(char flag) {
        const string value_arg_letters = "BbcDEeFIiJLlmOopQRSWw";
        return any_of(
                value_arg_letters.begin(),
                value_arg_letters.end(),
                [=](char letter) {
                    return letter == flag;
                });
    }

    void parseFlagArg(const string& arg) {
        // args specified in OpenSSH man page
        auto it = arg.begin();
        ++it; // skip the '-' char
        for (; it != arg.end(); ++it) {
            if (flagImpliesValueLater(*it)) {
                ostringstream log;
                log << "Flag " << *it << " implies value later.";
                logger.logLine(log);
                expecting_value = true;
            }
            if (flagIsVerbose(*it)) {
                ++verbose;
            }
        }
    }

public:
    explicit SSHArgs(const vector<string>& args) {
        for (const auto& arg: args) {
            // skip empty args
            if (arg.empty()) continue;

            if (expecting_value) {
                // previous arg is flag expecting value
                expecting_value = false;
                // skip this value -- it's SSH's problem.
                continue;
            }

            if (arg[0] == '-' && arg.length() > 1) {
                // Handle arg as flag...
                parseFlagArg(arg);
                continue;
            }

            if (ssh_dest.empty()) {
                // THIS argument is special!
                ssh_dest = arg;
            }
        }
    }

    string getDest() { return ssh_dest; }
    int getVerbose() const { return verbose; }
};


/* Responsible for storing the configuration we want for our SSH/SSHPASS command,
 * and finally running it when it's time. */
class GenSSHCommand {
    ostringstream ssh_args;
    string pw;
public:

    void add_args(const vector<string>& args) {
        for (const auto& arg:args) {
            ssh_args << " " << arg;
        }
    }

    void setSSHPass(string password) {
        pw = move(password);
    }

    void run() {
        ostringstream ssh_cmd;
        ssh_cmd << "ssh";
        if (not pw.empty()) {
            ssh_cmd << "pass -e ssh";
            // Se our env var to give `sshpass` utility our password.
            setenv("SSHPASS", pw.c_str(), true);
        }
        ssh_cmd << ssh_args.str();
        system(ssh_cmd.str().c_str());
    }
};

int main(int argc, char** argv) {
    // Convert args to std::string
    vector<string> args = cargsToStringArgs(argc, argv);

    // GenSSHCommand is responsible for tracking our configuration params
    // of the command and doing the final generation
    GenSSHCommand genSSHCommand;
    // Tell it what our args are, so we can pass them exactly to SSH.
    genSSHCommand.add_args(args);

    // SSHArgs allows us to figure out things like whether
    // -v has been passed, or which flag is the destination
    SSHArgs parseSSHArgs(args);
    string dest = parseSSHArgs.getDest();

    // Verbosity is a count of the number of -v flags passed
    logger.setVerbose(parseSSHArgs.getVerbose());
    // logLine statements default to verbosity 1.
    logger.logLine("verbose mode activated through -v flag.");

    if (not dest.empty()) { // we found SSH's destination!
        // handle SSH pass check if we have a SSH password
        string pass_path = getSSHPassPath(dest);
        if (not pass_path.empty()) {
            genSSHCommand.setSSHPass(pass_path);
        }
        // Run the pre-SSH hook
        callHookFamily("pre", dest);
        // Run SSH itself
        genSSHCommand.run();
        // Run the post-SSH hook
        callHookFamily("post", dest);
    } else {
        // No dest found? Maybe we messed up the parsing somewhere...
        // We default to running our SSH command without any tampering
        genSSHCommand.run();
    }

    return 0;
}
