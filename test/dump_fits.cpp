// Minimal CFITSIO dumper that prints the binary-table schema and every cell.

#include <fitsio.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void check(int status, const char* what) {
    if (status) {
        fits_report_error(stderr, status);
        std::fprintf(stderr, "fits error at: %s\n", what);
        std::exit(2);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s file.fits\n", argv[0]);
        return 1;
    }
    fitsfile* fp = nullptr;
    int status = 0;
    fits_open_file(&fp, argv[1], READONLY, &status);
    check(status, "open");

    // Non-structural keywords of the (empty) primary HDU, i.e. whatever
    // --primary-header* injected.
    {
        int nkeys = 0;
        fits_get_hdrspace(fp, &nkeys, nullptr, &status);
        check(status, "primary hdrspace");
        std::printf("HDU 1 (primary)\n");
        for (int i = 1; i <= nkeys; ++i) {
            char card[FLEN_CARD];
            fits_read_record(fp, i, card, &status);
            check(status, "primary read_record");
            if (fits_get_keyclass(card) >= TYP_REFSYS_KEY)
                std::printf("  hdr: %s\n", card);
        }
    }

    int hdunum = 0;
    fits_movabs_hdu(fp, 2, nullptr, &status);  // first table HDU
    check(status, "move to HDU 2");
    fits_get_hdu_num(fp, &hdunum);

    long nrows = 0;
    int ncols = 0;
    fits_get_num_rows(fp, &nrows, &status);
    fits_get_num_cols(fp, &ncols, &status);
    check(status, "row/col counts");

    std::printf("HDU %d: %ld rows, %d columns\n", hdunum, nrows, ncols);

    // Print the non-structural header keywords (reference-system, user,
    // COMMENT/HISTORY) — i.e. whatever --header/--header-from injected.
    int nkeys = 0;
    fits_get_hdrspace(fp, &nkeys, nullptr, &status);
    check(status, "hdrspace");
    for (int i = 1; i <= nkeys; ++i) {
        char card[FLEN_CARD];
        fits_read_record(fp, i, card, &status);
        check(status, "read_record");
        if (fits_get_keyclass(card) >= TYP_REFSYS_KEY)
            std::printf("  hdr: %s\n", card);
    }

    std::vector<std::string> names(ncols), forms(ncols);
    std::vector<int> typecodes(ncols);
    std::vector<long> repeats(ncols), widths(ncols);
    for (int c = 0; c < ncols; ++c) {
        char name[FLEN_VALUE], form[FLEN_VALUE];
        fits_make_keyn("TTYPE", c + 1, name, &status);
        fits_read_key(fp, TSTRING, name, name, nullptr, &status);
        fits_make_keyn("TFORM", c + 1, form, &status);
        fits_read_key(fp, TSTRING, form, form, nullptr, &status);
        check(status, "TTYPE/TFORM");
        names[c] = name; forms[c] = form;

        int tc; long rep, wid;
        fits_get_coltype(fp, c + 1, &tc, &rep, &wid, &status);
        check(status, "coltype");
        typecodes[c] = tc; repeats[c] = rep; widths[c] = wid;
        std::printf("  col %d: %-12s  TFORM=%-8s  typecode=%d  repeat=%ld  width=%ld\n",
                    c + 1, names[c].c_str(), forms[c].c_str(), tc, rep, wid);
    }

    for (long row = 1; row <= nrows; ++row) {
        std::printf("--- row %ld ---\n", row);
        for (int c = 0; c < ncols; ++c) {
            int tc = typecodes[c];
            long repeat = repeats[c];

            // VLAs have negative typecode in fits_get_coltype (e.g. -TFLOAT).
            bool is_vla = (tc < 0);
            int abs_tc = is_vla ? -tc : tc;

            long nelem = repeat;
            if (is_vla) {
                long offset = 0;
                fits_read_descript(fp, c + 1, row, &nelem, &offset, &status);
                check(status, "read_descript");
            }

            std::printf("  %s = ", names[c].c_str());
            if (abs_tc == TSTRING) {
                std::vector<char> sbuf(widths[c] + 1, 0);
                char* p = sbuf.data();
                int anyn = 0;
                fits_read_col(fp, TSTRING, c + 1, row, 1, 1, nullptr, &p, &anyn, &status);
                check(status, "read string");
                std::printf("\"%s\"\n", p);
            } else if (abs_tc == TLOGICAL) {
                std::vector<char> v(nelem, 0);
                int anyn = 0;
                fits_read_col(fp, TLOGICAL, c + 1, row, 1, nelem, nullptr, v.data(), &anyn, &status);
                check(status, "read logical");
                std::printf("[");
                for (long k = 0; k < nelem; ++k) std::printf("%s%s", k ? "," : "", v[k] ? "T" : "F");
                std::printf("]\n");
            } else if (abs_tc == TDOUBLE || abs_tc == TFLOAT) {
                std::vector<double> v(nelem, 0.0);
                int anyn = 0;
                fits_read_col(fp, TDOUBLE, c + 1, row, 1, nelem, nullptr, v.data(), &anyn, &status);
                check(status, "read float/double");
                std::printf("[");
                for (long k = 0; k < nelem; ++k) std::printf("%s%g", k ? "," : "", v[k]);
                std::printf("]\n");
            } else {
                std::vector<long long> v(nelem, 0);
                int anyn = 0;
                fits_read_col(fp, TLONGLONG, c + 1, row, 1, nelem, nullptr, v.data(), &anyn, &status);
                check(status, "read int");
                std::printf("[");
                for (long k = 0; k < nelem; ++k) std::printf("%s%lld", k ? "," : "", v[k]);
                std::printf("]\n");
            }
        }
    }

    fits_close_file(fp, &status);
    check(status, "close");
    return 0;
}
