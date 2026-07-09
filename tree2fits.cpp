// tree2fits — convert a ROOT TTree to a FITS binary table.
// usage: tree2fits input.root tree_name output.fits [maxvla]

#include "convert_lib.h"

#include <TFile.h>
#include <TTree.h>

#include <cstdlib>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " input.root tree_name output.fits [maxvla]\n"
                     "  maxvla: per-row cap for variable-length arrays (default 65536)\n";
        return 1;
    }
    const char* in_path  = argv[1];
    const char* tname    = argv[2];
    const char* out_path = argv[3];
    const long max_vla   = (argc >= 5) ? std::atol(argv[4]) : 65536;

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

    return tree2fits::convert_tree_to_fits(tree, tname, out_path, max_vla);
}
