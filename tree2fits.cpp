// tree2fits — convert a ROOT TTree to a FITS binary table.
// usage: tree2fits input.root tree_name output.fits [options]

#include "convert_lib.h"

#include <TFile.h>
#include <TTree.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

void usage(const char* prog) {
    std::cerr <<
        "usage: " << prog << " input.root tree_name output.fits [options]\n"
        "  --maxvla N          per-row cap for variable-length arrays (default 65536)\n"
        "  --header-from FILE   copy header keywords from a FITS HDU;\n"
        "                       FILE may use cfitsio extended syntax (e.g. hk.fits[EVENTS])\n"
        "  --header FILE        read header-template lines (\"KEY = value / comment\") from FILE\n"
        "  --header-key CARD    add one header-template line inline (repeatable)\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }
    const char* in_path  = argv[1];
    const char* tname    = argv[2];
    const char* out_path = argv[3];

    long max_vla = 65536;
    tree2fits::HeaderSpec header;

    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* opt) {
            if (i + 1 >= argc) {
                std::cerr << opt << " requires an argument\n";
                std::exit(1);
            }
            return std::string(argv[++i]);
        };
        if (a == "--maxvla") {
            max_vla = std::atol(need("--maxvla").c_str());
        } else if (a == "--header-from") {
            header.copy_from = need("--header-from");
        } else if (a == "--header") {
            tree2fits::read_header_template_file(need("--header"), header);
        } else if (a == "--header-key") {
            header.templates.push_back(need("--header-key"));
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (i == 4 && a[0] != '-') {
            // Backward compat: bare 4th positional arg is maxvla.
            max_vla = std::atol(a.c_str());
        } else {
            std::cerr << "unknown arg: " << a << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    std::unique_ptr<TFile> f(TFile::Open(in_path, "READ"));
    if (!f || f->IsZombie()) {
        std::cerr << "could not open " << in_path << "\n";
        return 1;
    }
    TTree* tree = dynamic_cast<TTree*>(f->Get(tname));
    if (!tree) {
        std::cerr << "tree '" << tname << "' not found in " << in_path << "\n";
        return 1;
    }

    return tree2fits::convert_tree_to_fits(tree, tname, out_path, max_vla, header);
}
