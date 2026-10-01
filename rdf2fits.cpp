// rdf2fits — RDataFrame wrapper around tree2fits.
//
// Builds an RDataFrame on the input tree, applies --define / --filter steps
// in the order they appear on the command line, optionally restricts to
// --columns, snapshots the result to a temporary ROOT file, and runs the
// shared TTree->FITS converter on it.
//
// usage:
//   rdf2fits input.root tree_name output.fits
//       [--define name=expr]... [--filter expr]...
//       [--columns c1,c2,...] [--maxvla N] [--keep-snapshot path]
//       [--header-from FILE] [--header FILE] [--header-key CARD]...
//       [--primary-header-from FILE] [--primary-header FILE]
//       [--primary-header-key CARD]...

#include <stdlib.h>  // mkstemps (glibc)
#include <unistd.h>

#include "convert_lib.h"

#include <ROOT/RDataFrame.hxx>
#include <ROOT/RDF/RInterface.hxx>
#include <TFile.h>
#include <TTree.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos) comma = s.size();
        std::string tok = s.substr(start, comma - start);
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front())))
            tok.erase(tok.begin());
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back())))
            tok.pop_back();
        if (!tok.empty()) out.push_back(std::move(tok));
        if (comma == s.size()) break;
        start = comma + 1;
    }
    return out;
}

void usage(const char* prog) {
    std::cerr <<
        "usage: " << prog << " input.root tree_name output.fits\n"
        "         [--define name=expr]... [--filter expr]...\n"
        "         [--columns c1,c2,...] [--maxvla N] [--keep-snapshot path]\n"
        "         [--header-from FILE] [--header FILE] [--header-key CARD]...\n"
        "  --define and --filter are applied in the order given.\n"
        "  --columns selects which columns appear in the FITS output (default: all).\n"
        "  --keep-snapshot writes the intermediate ROOT file to PATH and does\n"
        "                  not delete it (useful for debugging).\n"
        "  --header-from copies header keywords from a FITS HDU; FILE may use\n"
        "                  cfitsio extended syntax (e.g. hk.fits[EVENTS]).\n"
        "  --header reads header-template lines (\"KEY = value / comment\") from FILE.\n"
        "  --header-key adds one header-template line inline (repeatable).\n"
        "  --primary-header-from, --primary-header, --primary-header-key do the\n"
        "                  same for the (empty) primary HDU instead of the bintable.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) { usage(argv[0]); return 1; }

    const std::string in_path  = argv[1];
    const std::string tname    = argv[2];
    const std::string out_path = argv[3];

    long max_vla = 65536;
    std::vector<std::string> columns;
    std::string keep_snapshot;
    tree2fits::OutputHeaders headers;

    // Steps to apply to the RDataFrame in order.
    struct Step {
        enum Kind { Define, Filter } kind;
        std::string a, b;  // a=name (define) or expr (filter); b=expr (define)
    };
    std::vector<Step> steps;

    for (int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* opt) {
            if (i + 1 >= argc) {
                std::cerr << opt << " requires an argument\n";
                std::exit(1);
            }
            return std::string(argv[++i]);
        };
        if (a == "--define") {
            std::string spec = need("--define");
            auto eq = spec.find('=');
            if (eq == std::string::npos) {
                std::cerr << "--define expects name=expr, got: " << spec << "\n";
                return 1;
            }
            steps.push_back({Step::Define, spec.substr(0, eq), spec.substr(eq + 1)});
        } else if (a == "--filter") {
            steps.push_back({Step::Filter, need("--filter"), ""});
        } else if (a == "--columns") {
            columns = split_csv(need("--columns"));
        } else if (a == "--maxvla") {
            max_vla = std::atol(need("--maxvla").c_str());
        } else if (a == "--keep-snapshot") {
            keep_snapshot = need("--keep-snapshot");
        } else if (tree2fits::parse_header_option(a, need, headers)) {
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown arg: " << a << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    ROOT::RDataFrame df(tname, in_path);
    ROOT::RDF::RNode node = df;
    for (auto& s : steps) {
        if (s.kind == Step::Define) {
            node = node.Define(s.a, s.b);
        } else {
            node = node.Filter(s.a);
        }
    }

    std::vector<std::string> cols_out;
    if (!columns.empty()) {
        cols_out = std::move(columns);
    } else {
        for (const auto& c : node.GetColumnNames()) cols_out.push_back(c);
    }
    if (cols_out.empty()) {
        std::cerr << "no columns to write\n";
        return 1;
    }

    // Pick a temp file path for the snapshot.
    std::string snap_path;
    bool delete_snap = true;
    if (!keep_snapshot.empty()) {
        snap_path = keep_snapshot;
        delete_snap = false;
    } else {
        char tmpl[] = "/tmp/rdf2fits_XXXXXX.root";
        int fd = mkstemps(tmpl, 5); // 5 = strlen(".root")
        if (fd < 0) {
            std::perror("mkstemps");
            return 2;
        }
        close(fd);
        snap_path = tmpl;
    }

    ROOT::RDF::RSnapshotOptions opts;
    opts.fMode = "RECREATE";
    node.Snapshot(tname, snap_path, cols_out, opts);

    std::unique_ptr<TFile> f(TFile::Open(snap_path.c_str(), "READ"));
    if (!f || f->IsZombie()) {
        std::cerr << "could not reopen snapshot " << snap_path << "\n";
        if (delete_snap) unlink(snap_path.c_str());
        return 2;
    }
    TTree* tree = dynamic_cast<TTree*>(f->Get(tname.c_str()));
    if (!tree) {
        std::cerr << "tree '" << tname << "' missing in snapshot\n";
        if (delete_snap) unlink(snap_path.c_str());
        return 2;
    }

    int rc = tree2fits::convert_tree_to_fits(tree, tname.c_str(),
                                             out_path.c_str(), max_vla, headers);

    f.reset();  // close file before unlinking
    if (delete_snap) {
        unlink(snap_path.c_str());
    } else {
        std::cout << "kept snapshot at " << snap_path << "\n";
    }
    return rc;
}
