#include <iostream>
#include <vector>
#include <filesystem>

using namespace std;
namespace fs = filesystem;

bool argImpliesValueLater(const string& arg) {
    string value_arg_letters = "BbcDEeFIiJLlmOopQRSWw";
    for (const auto& value_arg_letter: value_arg_letters) {
        if (value_arg_letter == arg[1]) {
            return true;
        }
    }
    return false;
}

string parseArgs(const vector<string>& args) {
    bool expecting_value = false;
    for (const auto& arg: args) {
        if (arg.empty()) continue;
        if (expecting_value) {
            // previous arg is flag expecting value
            expecting_value = false;
            continue;
        }
        if (arg[0] == '-' && arg.length() > 1) {
            // arg is flag
            if (argImpliesValueLater(arg))
                // flag expects value
                expecting_value = true;
            continue;
        }
        return arg;
    }
    return "";
}

int main(int argc, char** argv) {
    // could do this more efficiently in C, but I don't care enough.
    vector<string> args(10);
    // skips first string via init i at 1 (call path)
    // iterate over all, and convert to string because lazy.
    for (int i=1;i<argc;i++) {
        // probably something wrong with this approach, given that it generates lots of `empty args in the str
        // TODO: investigate and fix
        args.emplace_back(argv[i]);
        cout<<args[i];
    }
    string ssh_cmd = "ssh ";
    for (const auto& arg:args) ssh_cmd += arg;

    string dest;

    dest = parseArgs(args);
    if (not dest.empty()) {
        fs::path path_to_pre_script = string(getenv("HOME")) + "/.ssh/pre.d/" + dest;
        if (fs::exists(path_to_pre_script)) {
            system(path_to_pre_script.c_str());
        }

        system(ssh_cmd.c_str());

        if (not args.empty()) {
            fs::path path_to_post_script = string(getenv("HOME")) + "/.ssh/post.d/" + dest;
            if (fs::exists(path_to_post_script))
                system(path_to_post_script.c_str());
        }
    } else {
        system(ssh_cmd.c_str());
    }

    return 0;
}
