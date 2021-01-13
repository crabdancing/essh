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
            cout << "essh: " << line << endl;
    }
};

ConsoleLogger logger;

bool argImpliesValueLater(const string& arg) {
    // args specified in OpenSSH man page
    string value_arg_letters = "BbcDEeFIiJLlmOopQRSWw";

    return any_of(value_arg_letters.begin(), value_arg_letters.end(),
                  // [=] allows access to outer scope.
                  [=](int i){ return i==arg[1]; }
                  );
}

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
public:
    explicit ParseSSHArgs(const vector<string>& args) {
        bool expecting_value = false;
        for (const auto& arg: args) {
            if (arg.empty()) continue;
            if (expecting_value) {
                // previous arg is flag expecting value
                expecting_value = false;
                continue;
            }
            if (arg[0] == '-' && arg.length() > 1) { // arg is flag...
                if (arg[1] == 'v') verbose++; // count verbose

                if (argImpliesValueLater(arg))
                    // flag expects value
                    expecting_value = true;
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

int main(int argc, char** argv) {
    // could do this more efficiently in C, but I don't care enough.
    vector<string> args;
    // skips first string via init i at 1 (call path)
    // iterate over all, and convert to string because lazy.
    for (int i=1;i<argc;i++) {
        args.emplace_back(argv[i]);
    }
    GenSSHCommand genSSHCommand;
    genSSHCommand.add_args((args));

    ParseSSHArgs parseSSHArgs(args);
    string dest = parseSSHArgs.getSSHDest();
    logger.setVerbose(parseSSHArgs.verboseMode());
    logger.log("verbose mode activated through -v flag.");
    if (not dest.empty()) {
        // we found SSH's destination! We can use this information to decide how to behave.

        // handle SSH pass check if we have a SSH password and tell genSSHCommand
        handleSSHPass(genSSHCommand, dest);
        callHookFamily("pre", dest);
        genSSHCommand.run();
        callHookFamily("post", dest);
    } else {
        // We run our SSH command without any tampering
        genSSHCommand.run();
    }

    return 0;
}
