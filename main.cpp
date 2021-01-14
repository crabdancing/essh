// Copyleft (C) Alexandria Pettit 2021
// GNU GPLv3

#include <iostream>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <algorithm>

using namespace std;

string getHome() {
    return getenv("HOME");
}

class ConsoleLogger {
    int verbose_level;

public:
    void setVerbose(int level = 0) {
        verbose_level = level;
    }

    void log(const string& line, int min_verbose_level = 1) const {
        if (verbose_level >= min_verbose_level)
            cerr << "essh: " << line << endl;
    }
};

ConsoleLogger logger;

string readEntireFile(const string& file_path) {
    streampos size;
    char *file_contents = nullptr;

    ifstream file (file_path, ios::in|ios::binary|ios::ate);
    if (file.is_open())
    {
        size = file.tellg();
        file_contents = new char [size];
        file.seekg (0, ios::beg);
        file.read (file_contents, size);
        file.close();
    }
    return file_contents;
}

bool file_exists (const string& path) {
    return access(path.c_str(), F_OK) != -1;
}

class ParseSSHArgs {
    int verbose = 0;
    string ssh_destination;
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
                logger.log(string("Flag ") + *it + "implies value later.");
                expecting_value = true;
            }
            if (flagIsVerbose(*it)) {
                ++verbose;
            }
        }
    }

public:
    explicit ParseSSHArgs(const vector<string>& args) {
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

            if (ssh_destination.empty()) {
                // THIS argument is special!
                ssh_destination = arg;
            }
        }
    }

    string getSSHDest() { return ssh_destination; }
    [[nodiscard]] int verboseMode() const { return verbose; }
};


class GenSSHCommand {
    string ssh_args;
    bool is_sshpass = false;
public:

    void add_arg(const string& arg) {
        ssh_args += " " + arg;
    }

    void add_args(const vector<string>& args) {
        for (const auto& arg:args) add_arg(arg);
    }

    void setSSHPass(bool value) {
        this->is_sshpass = value;
    }

    void run() {
        string ssh_cmd = "ssh";
        if (is_sshpass)
            ssh_cmd += "pass -e ssh";
        ssh_cmd += ssh_args;
        system(ssh_cmd.c_str());
    }
};

void callHookFamily(const string& prefix, const string& dest) {
    string path_to_hook_script = getHome() + "/.ssh/" + prefix + ".d/" + dest;
    string log_string;
    if (file_exists(path_to_hook_script)) {
        log_string += "running ";
        system(path_to_hook_script.c_str());
    } else {
        log_string += "no ";
    }
    logger.log(log_string + prefix + ".d in: " + string(path_to_hook_script));

}

void handleSSHPass(GenSSHCommand& genSshCommand, const string& dest) {
    string path_to_sshpass_pw_file = getHome() + "/.ssh/sshpass/" + dest;
    if (file_exists(path_to_sshpass_pw_file)) {
        logger.log(string("sshpass password file found: ") + string(path_to_sshpass_pw_file));
        string pw = readEntireFile(path_to_sshpass_pw_file);
        setenv("SSHPASS", pw.c_str(), true);
        genSshCommand.setSSHPass(true);
    }
}


vector<string> cargsToStringArgs(int argc, char** argv) {
    vector<string> args;
    // skips first string via init i at 1 (call path)
    // iterate over all, and convert to string because lazy.
    for (int i=1;i<argc;i++) {
        args.emplace_back(argv[i]);
    }
    return args;
}

int main(int argc, char** argv) {
    // Convert args to std::string
    vector<string> args = cargsToStringArgs(argc, argv);

    // GenSSHCommand is responsible for tracking our configuration params
    // of the command and doing the final generation
    GenSSHCommand genSSHCommand;
    // Tell it what our args are, so we can pass them exactly to SSH.
    genSSHCommand.add_args(args);

    // ParseSSHArgs allows us to figure out things like whether
    // -v has been passed, or which flag is the destination
    ParseSSHArgs parseSSHArgs(args);
    string dest = parseSSHArgs.getSSHDest();

    // Verbosity is a count of the number of -v flags passed
    logger.setVerbose(parseSSHArgs.verboseMode());
    // log statements default to verbosity 1.
    logger.log("verbose mode activated through -v flag.");

    if (not dest.empty()) { // we found SSH's destination!
        // handle SSH pass check if we have a SSH password and tell genSSHCommand
        handleSSHPass(genSSHCommand, dest);
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
