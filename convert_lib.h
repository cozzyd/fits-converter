// Shared TTree -> FITS bintable conversion. Used by both tree2fits and
// rdf2fits. Header-only because the column readers are templated.

#pragma once

#include <TTree.h>
#include <TLeaf.h>
#include <TObjArray.h>
#include <TTreeReader.h>
#include <TTreeReaderValue.h>
#include <TTreeReaderArray.h>
#include <fitsio.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tree2fits {

enum class Shape { Scalar, FixedArray, Vla, String };

template <typename T> struct FitsTrait;
template <> struct FitsTrait<bool>               { static constexpr char tc='L'; static constexpr int dt=TLOGICAL;  };
template <> struct FitsTrait<char>               { static constexpr char tc='S'; static constexpr int dt=TSBYTE;    };
template <> struct FitsTrait<unsigned char>      { static constexpr char tc='B'; static constexpr int dt=TBYTE;     };
template <> struct FitsTrait<short>              { static constexpr char tc='I'; static constexpr int dt=TSHORT;    };
template <> struct FitsTrait<unsigned short>     { static constexpr char tc='U'; static constexpr int dt=TUSHORT;   };
template <> struct FitsTrait<int>                { static constexpr char tc='J'; static constexpr int dt=TINT;      };
template <> struct FitsTrait<unsigned int>       { static constexpr char tc='V'; static constexpr int dt=TUINT;     };
template <> struct FitsTrait<long long>          { static constexpr char tc='K'; static constexpr int dt=TLONGLONG; };
template <> struct FitsTrait<unsigned long long> { static constexpr char tc='K'; static constexpr int dt=TLONGLONG; };
template <> struct FitsTrait<float>              { static constexpr char tc='E'; static constexpr int dt=TFLOAT;    };
template <> struct FitsTrait<double>             { static constexpr char tc='D'; static constexpr int dt=TDOUBLE;   };

class Column {
public:
    virtual ~Column() = default;
    virtual void write(fitsfile* fp, int colnum, LONGLONG row, int& status) = 0;
    std::string name;
    std::string title;
    std::string tform;
};

template <typename T>
class ScalarColumn : public Column {
    TTreeReaderValue<T> v;
public:
    ScalarColumn(TTreeReader& r, const std::string& nm, const std::string & ttl) : v(r, nm.c_str()) {
        name = nm;
        title = ttl;
        tform = std::string("1") + FitsTrait<T>::tc;
    }
    void write(fitsfile* fp, int colnum, LONGLONG row, int& status) override {
        T val = *v;
        fits_write_col(fp, FitsTrait<T>::dt, colnum, row, 1, 1, &val, &status);
    }
};

template <typename T>
class FixedArrayColumn : public Column {
    TTreeReaderArray<T> a;
    long fixed_len;
    std::unique_ptr<T[]> buf;
public:
    FixedArrayColumn(TTreeReader& r, const std::string& nm, const std::string & ttl, long n)
        : a(r, nm.c_str()), fixed_len(n), buf(new T[n]()) {
        name = nm;
        title = ttl;
        tform = std::to_string(n) + FitsTrait<T>::tc;
    }
    void write(fitsfile* fp, int colnum, LONGLONG row, int& status) override {
        long n = std::min<long>(static_cast<long>(a.GetSize()), fixed_len);
        for (long i = 0; i < n; ++i) buf[i] = a[i];
        for (long i = n; i < fixed_len; ++i) buf[i] = T{};
        fits_write_col(fp, FitsTrait<T>::dt, colnum, row, 1, fixed_len,
                       buf.get(), &status);
    }
};

template <typename T>
class VlaColumn : public Column {
    TTreeReaderArray<T> a;
    long max_vla;
    std::unique_ptr<T[]> buf;
public:
    VlaColumn(TTreeReader& r, const std::string& nm, const std::string & ttl, long maxn)
        : a(r, nm.c_str()), max_vla(maxn), buf(new T[maxn]) {
        name = nm;
        title = ttl;
        char tf[64];
        std::snprintf(tf, sizeof tf, "1P%c(%ld)", FitsTrait<T>::tc, maxn);
        tform = tf;
    }
    void write(fitsfile* fp, int colnum, LONGLONG row, int& status) override {
        LONGLONG n = static_cast<LONGLONG>(a.GetSize());
        if (n > max_vla) {
            std::cerr << "warning: row " << row << " col '" << name << "' size "
                      << n << " > maxvla " << max_vla << "; truncating\n";
            n = max_vla;
        }
        if (n == 0) return;
        for (LONGLONG i = 0; i < n; ++i) buf[i] = a[i];
        fits_write_col(fp, FitsTrait<T>::dt, colnum, row, 1, n,
                       buf.get(), &status);
    }
};

class StringColumn : public Column {
    TTreeReaderArray<char> a;
    long fixed_len;
    std::vector<char> buf;
public:
    StringColumn(TTreeReader& r, const std::string& nm, const std::string & ttle, long n)
        : a(r, nm.c_str()), fixed_len(n), buf(n + 1, 0) {
        name = nm;
        title = ttle;
        tform = std::to_string(n) + "A";
    }
    void write(fitsfile* fp, int colnum, LONGLONG row, int& status) override {
        long n = std::min<long>(static_cast<long>(a.GetSize()), fixed_len);
        for (long i = 0; i < n; ++i) buf[i] = a[i];
        buf[n] = 0;
        char* p = buf.data();
        fits_write_col(fp, TSTRING, colnum, row, 1, 1, &p, &status);
    }
};

inline std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

inline std::string normalize_type(std::string s) {
    s = trim(std::move(s));
    static const std::map<std::string, std::string> alias = {
        {"bool",               "Bool_t"},
        {"char",               "Char_t"},
        {"signed char",        "Char_t"},
        {"Int8_t",             "Char_t"},
        {"unsigned char",      "UChar_t"},
        {"UInt8_t",            "UChar_t"},
        {"short",              "Short_t"},
        {"Int16_t",            "Short_t"},
        {"unsigned short",     "UShort_t"},
        {"UInt16_t",           "UShort_t"},
        {"int",                "Int_t"},
        {"Int32_t",            "Int_t"},
        {"unsigned",           "UInt_t"},
        {"unsigned int",       "UInt_t"},
        {"UInt32_t",           "UInt_t"},
        {"long long",          "Long64_t"},
        {"Int64_t",            "Long64_t"},
        {"unsigned long long", "ULong64_t"},
        {"UInt64_t",           "ULong64_t"},
        {"float",              "Float_t"},
        {"Float16_t",          "Float_t"},
        {"double",             "Double_t"},
        {"Double32_t",         "Double_t"},
    };
    auto it = alias.find(s);
    return it != alias.end() ? it->second : s;
}

inline std::string vector_element_type(const std::string& cls) {
    auto lt = cls.find('<');
    auto gt = cls.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt) return "";
    std::string inner = cls.substr(lt + 1, gt - lt - 1);
    auto comma = inner.find(',');
    if (comma != std::string::npos) inner = inner.substr(0, comma);
    return trim(inner);
}

inline bool is_vector_type(const std::string& t) {
    return t.rfind("vector<", 0) == 0 ||
           t.rfind("ROOT::VecOps::RVec<", 0) == 0 ||
           t.rfind("RVec<", 0) == 0;
}

#define TREE2FITS_DISPATCH(ROOT_NAME, CPP_TYPE)                                \
    if (elem == ROOT_NAME) {                                                   \
        switch (shape) {                                                       \
            case Shape::Scalar:                                                \
                return std::make_unique<ScalarColumn<CPP_TYPE>>(reader, name, title); \
            case Shape::FixedArray:                                            \
                return std::make_unique<FixedArrayColumn<CPP_TYPE>>(reader, name,title,  n); \
            case Shape::Vla:                                                   \
                return std::make_unique<VlaColumn<CPP_TYPE>>(reader, name, title, max_vla); \
            case Shape::String:                                                \
                break;                                                         \
        }                                                                      \
    }

inline std::unique_ptr<Column> make_column(TTreeReader& reader,
                                           const std::string& name,
                                           const std::string& title,
                                           const std::string& elem_raw,
                                           Shape shape, long n, long max_vla) {
    if (shape == Shape::String) {
        return std::make_unique<StringColumn>(reader, name, title, n);
    }
    const std::string elem = normalize_type(elem_raw);
    TREE2FITS_DISPATCH("Bool_t",    bool)
    TREE2FITS_DISPATCH("Char_t",    char)
    TREE2FITS_DISPATCH("UChar_t",   unsigned char)
    TREE2FITS_DISPATCH("Short_t",   short)
    TREE2FITS_DISPATCH("UShort_t",  unsigned short)
    TREE2FITS_DISPATCH("Int_t",     int)
    TREE2FITS_DISPATCH("UInt_t",    unsigned int)
    TREE2FITS_DISPATCH("Long64_t",  long long)
    TREE2FITS_DISPATCH("ULong64_t", unsigned long long)
    TREE2FITS_DISPATCH("Float_t",   float)
    TREE2FITS_DISPATCH("Double_t",  double)
    return nullptr;
}

#undef TREE2FITS_DISPATCH

[[noreturn]] inline void die_fits(int status, const char* what) {
    fits_report_error(stderr, status);
    std::cerr << "fits error during: " << what << "\n";
    std::exit(2);
}

inline void check_fits(int status, const char* what) {
    if (status) die_fits(status, what);
}

// Walks the tree's leaves and writes a FITS binary table HDU. Returns 0 on
// success. Exits the process on FITS errors via die_fits().
inline int convert_tree_to_fits(TTree* tree, const char* tname,
                                const char* out_path, long max_vla) {
    TTreeReader reader(tree);

    struct ColumnSpec {
        std::string name;
        std::string title;
        std::string elem;
        Shape shape;
        long n;
    };
    std::vector<ColumnSpec> specs;

    TObjArray* leaves = tree->GetListOfLeaves();
    const int nleaves = leaves->GetEntries();
    for (int i = 0; i < nleaves; ++i) {
        TLeaf* leaf = static_cast<TLeaf*>(leaves->At(i));
        const std::string lname = leaf->GetName();
        const std::string ltitle = leaf->GetTitle();
        const std::string ltype = leaf->GetTypeName();

        if (is_vector_type(ltype)) {
            const std::string elem = vector_element_type(ltype);
            if (elem.empty()) {
                std::cerr << "skipping '" << lname
                          << "': cannot parse element type from '" << ltype << "'\n";
                continue;
            }
            specs.push_back({lname, ltitle, elem, Shape::Vla, 0});
            continue;
        }

        TLeaf* lc = leaf->GetLeafCount();
        const long static_len = leaf->GetLenStatic();

        if (ltype == "Char_t" && !lc && static_len > 1) {
            specs.push_back({lname, ltitle,"", Shape::String, static_len});
        } else if (lc) {
            specs.push_back({lname, ltitle,ltype, Shape::Vla, 0});
        } else if (static_len > 1) {
            specs.push_back({lname, ltitle,ltype, Shape::FixedArray, static_len});
        } else {
            specs.push_back({lname, ltitle,ltype, Shape::Scalar, 1});
        }
    }

    std::vector<std::unique_ptr<Column>> cols;
    cols.reserve(specs.size());
    for (auto& sp : specs) {
        auto col = make_column(reader, sp.name, sp.title, sp.elem, sp.shape, sp.n, max_vla);
        if (!col) {
            std::cerr << "skipping '" << sp.name << "': unsupported element type '"
                      << sp.elem << "'\n";
            continue;
        }
        cols.push_back(std::move(col));
    }

    if (cols.empty()) {
        std::cerr << "no convertible columns in tree '" << tname << "'\n";
        return 1;
    }

    const Long64_t nentries = tree->GetEntries();

    fitsfile* fptr = nullptr;
    int status = 0;
    const std::string outspec = std::string("!") + out_path;
    fits_create_file(&fptr, outspec.c_str(), &status);
    check_fits(status, "create_file");

    const int ncols = static_cast<int>(cols.size());
    std::vector<char*> ttype(ncols), tform(ncols), tunit(ncols);
    std::string empty;
    for (int i = 0; i < ncols; ++i) {
        ttype[i] = const_cast<char*>(cols[i]->name.c_str());
        tform[i] = const_cast<char*>(cols[i]->tform.c_str());
        tunit[i] = const_cast<char*>(cols[i]->title.c_str());
    }
    fits_create_tbl(fptr, BINARY_TBL, nentries, ncols,
                    ttype.data(), tform.data(), tunit.data(),
                    const_cast<char*>(tname), &status);
    check_fits(status, "create_tbl");

    LONGLONG row = 0;
    while (reader.Next()) {
        ++row;
        for (int c = 0; c < ncols; ++c) {
            cols[c]->write(fptr, c + 1, row, status);
            if (status) {
                std::string what = "write_col " + cols[c]->name +
                                   " row " + std::to_string(row);
                die_fits(status, what.c_str());
            }
        }
    }

    fits_close_file(fptr, &status);
    check_fits(status, "close_file");

    std::cout << "wrote " << nentries << " rows, " << ncols
              << " columns to " << out_path << "\n";
    return 0;
}

} // namespace tree2fits
