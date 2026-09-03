// Test TTree that exercises the converter:
//   scalars, fixed-size arrays, a leaflist VLA, a Char_t string,
//   and std::vector<T> branches.

#include <TFile.h>
#include <TTree.h>

#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    const char* out = (argc >= 2) ? argv[1] : "test_tree.root";

    TFile f(out, "RECREATE");
    TTree t("events", "test events");

    Int_t      i32;
    UInt_t     u32;
    Long64_t   i64;
    Float_t    fscalar;
    Double_t   dscalar;
    Bool_t     flag;
    Float_t    fixed_arr[3];
    Int_t      n;
    Float_t    var_arr[64];
    Char_t     label[16];
    std::vector<float>* vec_f = new std::vector<float>();
    std::vector<int>*   vec_i = new std::vector<int>();

    t.Branch("i32",       &i32,       "i32/I");
    t.Branch("u32",       &u32,       "u32/i");
    t.Branch("i64",       &i64,       "i64/L");
    t.Branch("fscalar",   &fscalar,   "fscalar/F");
    t.Branch("dscalar",   &dscalar,   "dscalar/D");
    t.Branch("flag",      &flag,      "flag/O");
    t.Branch("fixed_arr",  fixed_arr, "fixed_arr[3]/F");
    t.Branch("n",         &n,         "n/I");
    t.Branch("var_arr",    var_arr,   "var_arr[n]/F");
    t.Branch("label",      label,     "label[16]/C");
    t.Branch("vec_f",     &vec_f);
    t.Branch("vec_i",     &vec_i);

    for (int row = 0; row < 5; ++row) {
        i32     = row - 2;
        u32     = static_cast<UInt_t>(row * 1000u);
        i64     = static_cast<Long64_t>(row) * 1'000'000'000LL;
        fscalar = 0.5f * row;
        dscalar = 1.0 / (row + 1);
        flag    = (row % 2 == 0);
        for (int k = 0; k < 3; ++k) fixed_arr[k] = static_cast<float>(row * 10 + k);
        n = row + 1;
        for (int k = 0; k < n; ++k) var_arr[k] = static_cast<float>(row) + 0.1f * k;
        std::snprintf(label, sizeof label, "row_%d", row);

        vec_f->clear();
        for (int k = 0; k < row + 2; ++k) vec_f->push_back(100.0f + row + 0.01f * k);
        vec_i->clear();
        for (int k = 0; k < row; ++k) vec_i->push_back(row * 100 + k);

        t.Fill();
    }

    t.Write();
    f.Close();
    std::printf("wrote %s with %lld entries\n", out, (long long)t.GetEntries());
    return 0;
}
