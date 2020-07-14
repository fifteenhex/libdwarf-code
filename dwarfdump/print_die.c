/*
  Copyright (C) 2000-2006 Silicon Graphics, Inc.  All Rights Reserved.
  Portions Copyright 2007-2010 Sun Microsystems, Inc. All rights reserved.
  Portions Copyright 2009-2018 SN Systems Ltd. All rights reserved.
  Portions Copyright 2007-2020 David Anderson. All rights reserved.

  This program is free software; you can redistribute it and/or
  modify it under the terms of version 2 of the GNU General
  Public License as published by the Free Software Foundation.

  This program is distributed in the hope that it would be
  useful, but WITHOUT ANY WARRANTY; without even the implied
  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.

  Further, this software is distributed without any warranty
  that it is free of the rightful claim of any third person
  regarding infringement or the like.  Any license provided
  herein, whether implied or otherwise, applies only to this
  software file.  Patent licenses, if any, provided herein
  do not apply to combinations of this program with other
  software, or any other product whatsoever.

  You should have received a copy of the GNU General Public
  License along with this program; if not, write the Free
  Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
  Boston MA 02110-1301, USA.
*/

/*  The address of the Free Software Foundation is
    Free Software Foundation, Inc., 51 Franklin St, Fifth
    Floor, Boston, MA 02110-1301, USA.  SGI has moved from
    the Crittenden Lane address.  */


#include "globals.h"
#ifdef HAVE_STDINT_H
#include <stdint.h> /* For uintptr_t */
#endif /* HAVE_STDINT_H */
#include "naming.h"
#include "esb.h"                /* For flexible string buffer. */
#include "esb_using_functions.h"
#include "sanitized.h"
#include "print_frames.h"  /* for print_location_operations() . */
#include "macrocheck.h"
#include "helpertree.h"
#include "tag_common.h"

/*  Traverse a DIE and attributes to
    check self references */
static int traverse_one_die(Dwarf_Debug dbg,
    Dwarf_Attribute attrib,
    Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool Dwarf_Bool,
    char **srcfiles,
    Dwarf_Signed cnt, int die_indent_level,
    Dwarf_Error*err);
static int traverse_attribute(Dwarf_Debug dbg,
    Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    Dwarf_Half attr, Dwarf_Attribute attr_in,
    boolean print_else_name_match,
    char **srcfiles, Dwarf_Signed cnt,
    int die_indent_level,
    Dwarf_Error * err);
static int print_die_and_children_internal(Dwarf_Debug dbg,
    Dwarf_Die in_die_in,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    char **srcfiles, Dwarf_Signed cnt,
    Dwarf_Error *);
static int print_one_die_section(Dwarf_Debug dbg,
    Dwarf_Bool is_info,
    Dwarf_Error *pod_err);
static int handle_rnglists(Dwarf_Die die,
    Dwarf_Attribute attrib,
    Dwarf_Half theform,
    Dwarf_Unsigned value,
    Dwarf_Unsigned *rle_offset_out,
    struct esb_s *  esbp,
    int show_form,
    int local_verbose,
    Dwarf_Error *err);

/* Is this a PU has been invalidated by the SN Systems linker? */
#define IsInvalidCode(low,high) ((low == max_address) || (low == 0 && high == 0))

#ifdef HAVE_USAGE_TAG_ATTR
/*  Record TAGs usage */
static unsigned int tag_usage[DW_TAG_last] = {0};
#endif /* HAVE_USAGE_TAG_ATTR */

static int get_form_values(Dwarf_Debug dbg,Dwarf_Attribute attrib,
    Dwarf_Half * theform, Dwarf_Half * directform,Dwarf_Error *err);
static void show_form_itself(int show_form,int verbose,
    int theform, int directform, struct esb_s * str_out);
static int print_exprloc_content(Dwarf_Debug dbg,Dwarf_Die die,
    Dwarf_Attribute attrib,
    boolean checking,
    int die_indent_level,
    int showhextoo,
    struct esb_s *esbp,Dwarf_Error *err);
static int print_attribute(Dwarf_Debug dbg, Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Half attr,
    Dwarf_Attribute actual_addr,
    boolean print_else_name_match,
    int die_indent_level, char **srcfiles,
    Dwarf_Signed cnt,
    boolean *attr_matched,
    Dwarf_Error *err);
static int print_location_list(Dwarf_Debug dbg,
    Dwarf_Die die,
    Dwarf_Attribute attr,
    boolean checking,int no_ending_newline,
    struct esb_s *details,Dwarf_Error *);
static int legal_tag_attr_combination(Dwarf_Half tag, Dwarf_Half attr);
static int legal_tag_tree_combination(Dwarf_Half parent_tag,
    Dwarf_Half child_tag);

static int formxdata_print_value(Dwarf_Debug dbg,
    Dwarf_Die die,Dwarf_Attribute attrib,
    Dwarf_Half theform,
    struct esb_s *esbp, Dwarf_Error * err, Dwarf_Bool hex_format);
static void bracket_hex(const char *s1, Dwarf_Unsigned v,
    const char *s2, struct esb_s * esbp);
static void formx_unsigned(Dwarf_Unsigned u, struct esb_s *esbp,
    Dwarf_Bool hex_format);
static void formx_data16(Dwarf_Form_Data16 * u, struct esb_s *esbp,
    Dwarf_Bool hex_format);

static void formx_signed(Dwarf_Signed s, struct esb_s *esbp);

static int pd_dwarf_names_print_on_error = 1;

static int die_stack_indent_level = 0;
static boolean local_symbols_already_began = FALSE;


typedef const char *(*encoding_type_func) (unsigned,int doprintingonerr);

/* Indicators to record a pair [low,high], these
   are used in printing DIEs to accumulate the high
   and low pc across attributes and to record the pair
   as soon as both are known. Probably would be better to
   use variables as arguments to
   print_attribute().  */
static Dwarf_Addr lowAddr = 0;
static Dwarf_Addr highAddr = 0;
static Dwarf_Bool bSawLow = FALSE;
static Dwarf_Bool bSawHigh = FALSE;

/* The following too is related to high and low pc
attributes of a function. It's misnamed, it really means
'yes, we have high and low pc' if it is TRUE. Defaulting to TRUE
seems bogus. */
static Dwarf_Bool in_valid_code = TRUE;


static const Dwarf_Sig8 zerosig;

#if 0
static void
dump_bytes(const char *msg,Dwarf_Small * start, long len)
{
    Dwarf_Small *end = start + len;
    Dwarf_Small *cur = start;
    printf("%s (0x%lx) ",msg,(unsigned long)start);
    for (; cur < end; cur++) {
        printf("%02x", *cur);
    }
    printf("\n");
}
#endif /* 0 */

struct operation_descr_s {
    int op_code;
    int op_count;
    const char * op_1type;
};
struct operation_descr_s opdesc[]= {
    {DW_OP_addr,1,"addr" },
    {DW_OP_deref,0,"" },
    {DW_OP_const1u,1,"1u" },
    {DW_OP_const1s,1,"1s" },
    {DW_OP_const2u,1,"2u" },
    {DW_OP_const2s,1,"2s" },
    {DW_OP_const4u,1,"4u" },
    {DW_OP_const4s,1,"4s" },
    {DW_OP_const8u,1,"8u" },
    {DW_OP_const8s,1,"8s" },
    {DW_OP_constu,1,"uleb" },
    {DW_OP_consts,1,"sleb" },
    {DW_OP_dup,0,""},
    {DW_OP_drop,0,""},
    {DW_OP_over,0,""},
    {DW_OP_pick,1,"1u"},
    {DW_OP_swap,0,""},
    {DW_OP_rot,0,""},
    {DW_OP_xderef,0,""},
    {DW_OP_abs,0,""},
    {DW_OP_and,0,""},
    {DW_OP_div,0,""},
    {DW_OP_minus,0,""},
    {DW_OP_mod,0,""},
    {DW_OP_mul,0,""},
    {DW_OP_neg,0,""},
    {DW_OP_not,0,""},
    {DW_OP_or,0,""},
    {DW_OP_plus,0,""},
    {DW_OP_plus_uconst,1,"uleb"},
    {DW_OP_shl,0,""},
    {DW_OP_shr,0,""},
    {DW_OP_shra,0,""},
    {DW_OP_xor,0,""},
    {DW_OP_skip,1,"2s"},
    {DW_OP_bra,1,"2s"},
    {DW_OP_eq,0,""},
    {DW_OP_ge,0,""},
    {DW_OP_gt,0,""},
    {DW_OP_le,0,""},
    {DW_OP_lt,0,""},
    {DW_OP_ne,0,""},
    /* lit0 thru reg31 handled specially, no operands */
    /* breg0 thru breg31 handled specially, 1 operand */
    {DW_OP_regx,1,"uleb"},
    {DW_OP_fbreg,1,"sleb"},
    {DW_OP_bregx,2,"uleb"},
    {DW_OP_piece,1,"uleb"},
    {DW_OP_deref_size,1,"1u"},
    {DW_OP_xderef_size,1,"1u"},
    {DW_OP_nop,0,""},
    {DW_OP_push_object_address,0,""},
    {DW_OP_call2,1,"2u"},
    {DW_OP_call4,1,"4u"},
    {DW_OP_call_ref,1,"off"},
    {DW_OP_form_tls_address,0,""},
    {DW_OP_call_frame_cfa,0,""},
    {DW_OP_bit_piece,2,"uleb"},
    {DW_OP_implicit_value,2,"u"},
    {DW_OP_stack_value,0,""},
    {DW_OP_GNU_uninit,0,""},
    {DW_OP_GNU_encoded_addr,1,"addr"},
    {DW_OP_implicit_pointer,2,"addr" }, /* DWARF5 */
    {DW_OP_GNU_implicit_pointer,2,"addr" },
    {DW_OP_entry_value,2,"val" }, /* DWARF5 */
    {DW_OP_GNU_entry_value,2,"val" },
    {DW_OP_const_type,3,"uleb" }, /* DWARF5 */
    {DW_OP_GNU_const_type,3,"uleb" },
    {DW_OP_regval_type,2,"uleb" }, /* DWARF5 */
    {DW_OP_GNU_regval_type,2,"uleb" },
    {DW_OP_deref_type,1,"val" }, /* DWARF5 */
    {DW_OP_GNU_deref_type,1,"val" },
    {DW_OP_convert,1,"uleb" }, /* DWARF5 */
    {DW_OP_GNU_convert,1,"uleb" },
    {DW_OP_reinterpret,1,"uleb" }, /* DWARF5 */
    {DW_OP_GNU_reinterpret,1,"uleb" },

    {DW_OP_GNU_parameter_ref,1,"val" },
    {DW_OP_GNU_const_index,1,"val" },
    {DW_OP_GNU_push_tls_address,0,"" },

    {DW_OP_addrx,1,"uleb" }, /* DWARF5 */
    {DW_OP_GNU_addr_index,1,"val" },
    {DW_OP_constx,1,"u" }, /* DWARF5 */
    {DW_OP_GNU_const_index,1,"u" },

    {DW_OP_GNU_parameter_ref,1,"u" },

    {DW_OP_xderef,0,"" }, /* DWARF5 */
    {DW_OP_xderef_type,2,"1u" }, /* DWARF5 */
    /* terminator */
    {0,0,""}
};

struct die_stack_data_s {
    Dwarf_Die die_;
    /*  sibling_die_globaloffset_ is set while processing the DIE.
        We do not know the sibling global offset
        when we create the stack entry.
        If the sibling attribute absent we never know. */
    Dwarf_Off sibling_die_globaloffset_;
    /*  We may need is_info here too. */
    Dwarf_Off cu_die_offset_; /* global offset. */
    boolean already_printed_;
};

static struct die_stack_data_s empty_stack_entry;

#define DIE_STACK_SIZE 800
static struct die_stack_data_s die_stack[DIE_STACK_SIZE];

#define SET_DIE_STACK_ENTRY(i,x,o) { die_stack[i].die_ = x; \
    die_stack[i].cu_die_offset_ = o;                        \
    die_stack[i].sibling_die_globaloffset_ = 0;             \
    die_stack[i].already_printed_ = FALSE; }
#define EMPTY_DIE_STACK_ENTRY(i) { die_stack[i] = empty_stack_entry; }
#define SET_DIE_STACK_SIBLING(x) {                           \
    die_stack[die_stack_indent_level].sibling_die_globaloffset_ = x; }


/*  The first non-zero sibling offset we can find
    is what we want to return. The lowest sibling
    offset in the stack.  Or 0 if we have none known.
*/
static Dwarf_Off
get_die_stack_sibling()
{
    int i = die_stack_indent_level;
    for( ; i >=0 ; --i)
    {
        Dwarf_Off v = die_stack[i].sibling_die_globaloffset_;
        if (v) {
            return v;
        }
    }
    return 0;
}
static void
possibly_increase_esb_alloc(struct esb_s *esbp,
  Dwarf_Unsigned count,
  Dwarf_Unsigned entrysize)
{
    /*  for bytes of text needed per element */
    Dwarf_Unsigned targetsize = count*entrysize;
    Dwarf_Unsigned used       = esb_string_len(esbp);
    Dwarf_Unsigned cursize    = esb_get_allocated_size(esbp);

    if ((targetsize+used) > cursize) {
        esb_force_allocation(esbp,targetsize+used);
    }
}
static void
dealloc_all_srcfiles(Dwarf_Debug dbg,
  char **srcfiles,
  Dwarf_Signed cnt)
{
    Dwarf_Signed i = 0;

    if(!srcfiles) {
        return;
    }
    for ( ; i < cnt; ++i) {
        dwarf_dealloc(dbg,srcfiles[i],DW_DLA_STRING);
    }
    dwarf_dealloc(dbg,srcfiles, DW_DLA_LIST);
}


/*  Higher stack level numbers must have a smaller sibling
    offset than lower or else the sibling offsets are wrong.
    Stack entries with sibling_die_globaloffset_ 0 must be
    ignored in this, it just means there was no sibling
    attribute at that level.
*/
static void
validate_die_stack_siblings(Dwarf_Debug dbg)
{
    int i = die_stack_indent_level;
    Dwarf_Off innersiboffset = 0;
    for( ; i >=0 ; --i)
    {
        Dwarf_Off v = die_stack[i].sibling_die_globaloffset_;
        if (v) {
            innersiboffset = v;
            break;
        }
    }
    if(!innersiboffset) {
        /* no sibling values to check. */
        return;
    }
    for(--i ; i >= 0 ; --i)
    {
        /* outersiboffset is an outer sibling offset. */
        Dwarf_Off outersiboffset = die_stack[i].sibling_die_globaloffset_;
        if (outersiboffset ) {
            if (outersiboffset < innersiboffset) {
                char small_buf[ESB_FIXED_ALLOC_SIZE];
                Dwarf_Error ouerr = 0;
                /* safe: all values known length. */
                struct esb_s pm;

                esb_constructor_fixed(&pm,small_buf,
                    sizeof(small_buf));
                esb_append_printf_u(&pm,
                    "ERROR: Die stack sibling error, "
                    "outer global offset "
                    "0x%"  DW_PR_XZEROS DW_PR_DUx,outersiboffset);
                esb_append_printf_u(&pm,
                    " less than inner global offset "
                    "0x%"  DW_PR_XZEROS DW_PR_DUx
                    ", the DIE tree is erroneous.",
                    innersiboffset);
                glflags.gf_count_major_errors++;
                print_error_and_continue(dbg,
                    esb_get_string(&pm),
                    DW_DLV_OK,ouerr);
                esb_destructor(&pm);
                return;
            }
            /*  We only need check one level with an offset
                at each entry. */
            break;
        }
    }
    return;
}

static void
append_local_prefix(struct esb_s *esbp)
{
    esb_append(esbp,"\n      ");
}
static int
print_as_info_or_by_cuname()
{
    return (glflags.gf_info_flag || glflags.gf_types_flag
        || glflags.gf_cu_name_flag);
}

#if 0
/*  Only used for debugging. */
static void
dump_die_offsets(Dwarf_Debug dbg, Dwarf_Die die,
    const char *msg)
{
    Dwarf_Error dderr = 0;
    Dwarf_Off goff = 0;
    Dwarf_Off loff = 0;
    Dwarf_Half tag = 0;
    int res = 0;

    res = dwarf_die_offsets(die,&goff, &loff,&dderr);
    DROP_ERROR_INSTANCE(dbg,res,dderr);
    res = dwarf_tag(die, &tag, &dderr);
    DROP_ERROR_INSTANCE(dbg,res,dderr);
    printf("debugonly: Die tag 0x%x GOFF 0x%llx Loff 0x%llx %s\n",
        tag,goff,loff,msg);
}
#endif

static Dwarf_Bool
form_refers_local_info(Dwarf_Half form)
{
    if (form == DW_FORM_GNU_ref_alt ||
        form == DW_FORM_GNU_strp_alt ||
        form == DW_FORM_strp_sup ||
        form == DW_FORM_line_strp ) {
        /*  These do not refer to the current
            section and cannot be checked
            as if they did. */
        return FALSE;
    }
    return TRUE;
}


/* process each compilation unit in .debug_info */
int
print_infos(Dwarf_Debug dbg,Dwarf_Bool is_info,
    Dwarf_Error *pi_err)
{
    int nres = 0;
    nres = print_one_die_section(dbg,is_info,pi_err);
    return nres;
}

static void
print_debug_fission_header(struct Dwarf_Debug_Fission_Per_CU_s *fsd)
{
    const char * fissionsec = ".debug_cu_index";
    unsigned i  = 0;
    struct esb_s hash_str;

    if (!fsd || !fsd->pcu_type) {
        /* No fission data. */
        return;
    }
    esb_constructor(&hash_str);
    printf("\n");
    if (!strcmp(fsd->pcu_type,"tu")) {
        fissionsec = ".debug_tu_index";
    }
    printf("  %-19s = %s\n","Fission section",fissionsec);
    printf("  %-19s = 0x%"  DW_PR_XZEROS DW_PR_DUx "\n","Fission index ",
        fsd->pcu_index);
    format_sig8_string(&fsd->pcu_hash,&hash_str);
    printf("  %-19s = %s\n","Fission hash",esb_get_string(&hash_str));
    /* 0 is always unused. Skip it. */
    esb_destructor(&hash_str);
    printf("  %-19s = %s\n","Fission entries","offset     size        DW_SECTn");
    for( i = 1; i < DW_FISSION_SECT_COUNT; ++i)  {
        const char *nstring = 0;
        Dwarf_Unsigned off = 0;
        Dwarf_Unsigned size = fsd->pcu_size[i];
        int res = 0;
        if (size == 0) {
            continue;
        }
        res = dwarf_get_SECT_name(i,&nstring);
        if (res != DW_DLV_OK) {
            nstring = "Unknown SECT";
        }
        off = fsd->pcu_offset[i];
        printf("  %-19s = 0x%"  DW_PR_XZEROS DW_PR_DUx " 0x%"
            DW_PR_XZEROS DW_PR_DUx " %2d\n",
            nstring,
            off,size,i);
    }
}

static void
print_cu_hdr_cudie(UNUSEDARG Dwarf_Debug dbg,
    UNUSEDARG Dwarf_Die cudie,
    Dwarf_Unsigned overall_offset,
    Dwarf_Unsigned offset )
{
    struct Dwarf_Debug_Fission_Per_CU_s fission_data;

    if (glflags.dense) {
        printf("\n");
        return;
    }
    memset(&fission_data,0,sizeof(fission_data));
    printf("\nCOMPILE_UNIT<header overall offset = 0x%"
        DW_PR_XZEROS DW_PR_DUx ">",
        (Dwarf_Unsigned)(overall_offset - offset));
    printf(":\n");
}


static  void
print_cu_hdr_std(Dwarf_Unsigned cu_header_length,
    Dwarf_Unsigned abbrev_offset,
    Dwarf_Half version_stamp,
    Dwarf_Half address_size,
    /* offset_size is often called length_size in libdwarf. */
    Dwarf_Half offset_size,
    int debug_fission_res,
    Dwarf_Half cu_type,
    struct Dwarf_Debug_Fission_Per_CU_s * fsd)
{
    int res = 0;
    const char *utname = 0;

    res = dwarf_get_UT_name(cu_type,&utname);
    if (res != DW_DLV_OK) {
        glflags.gf_count_major_errors++;
        utname = "ERROR";
    }
    if (glflags.dense) {
        printf("<%s>", "cu_header");
        printf(" %s<0x%" DW_PR_XZEROS  DW_PR_DUx
            ">", "cu_header_length",
            cu_header_length);
        printf(" %s<0x%04x>", "version_stamp",
            version_stamp);
        printf(" %s<0x%"  DW_PR_XZEROS DW_PR_DUx
            ">", "abbrev_offset", abbrev_offset);
        printf(" %s<0x%02x>", "address_size",
            address_size);
        printf(" %s<0x%02x>", "offset_size",
            offset_size);
        printf(" %s<0x%02x %s>", "cu_type",
            cu_type,utname);
        if (debug_fission_res == DW_DLV_OK) {
            struct esb_s hash_str;
            unsigned i = 0;

            esb_constructor(&hash_str);
            format_sig8_string(&fsd->pcu_hash,&hash_str);
            printf(" %s<0x%" DW_PR_XZEROS  DW_PR_DUx  ">", "fissionindex",
                fsd->pcu_index);
            printf(" %s<%s>", "fissionhash",
                esb_get_string(&hash_str));
            esb_destructor(&hash_str);
            for( i = 1; i < DW_FISSION_SECT_COUNT; ++i)  {
                const char *nstring = 0;
                Dwarf_Unsigned off = 0;
                Dwarf_Unsigned size = fsd->pcu_size[i];
                int fires = 0;

                if (size == 0) {
                    continue;
                }
                fires = dwarf_get_SECT_name(i,&nstring);
                if (fires != DW_DLV_OK) {
                    nstring = "UnknownDW_SECT";
                }
                off = fsd->pcu_offset[i];
                printf(" %s< 0x%"  DW_PR_XZEROS DW_PR_DUx " 0x%"
                    DW_PR_XZEROS DW_PR_DUx ">",
                    nstring,
                    off,size);
            }
        }
    } else {
        printf("\nCU_HEADER:\n");
        printf("  %-16s = 0x%" DW_PR_XZEROS DW_PR_DUx
            " %" DW_PR_DUu
            "\n", "cu_header_length",
            cu_header_length,
            cu_header_length);
        printf("  %-16s = 0x%04x     %u\n", "version_stamp",
            version_stamp,version_stamp);
        printf("  %-16s = 0x%" DW_PR_XZEROS DW_PR_DUx
            " %" DW_PR_DUu
            "\n", "abbrev_offset",
            abbrev_offset,
            abbrev_offset);
        printf("  %-16s = 0x%02x       %u\n", "address_size",
            address_size,address_size);
        printf("  %-16s = 0x%02x       %u\n", "offset_size",
            offset_size,offset_size);
        printf("  %-16s = 0x%02x       %s\n", "cu_type",
            cu_type,utname);
        if (debug_fission_res == DW_DLV_OK) {
            print_debug_fission_header(fsd);
        }
    }
}
static void
print_cu_hdr_signature(Dwarf_Sig8 *signature,Dwarf_Unsigned typeoffset)
{
    if (glflags.dense) {
        struct esb_s sig8str;

        esb_constructor(&sig8str);
        format_sig8_string(signature,&sig8str);
        printf(" %s<%s>", "signature",esb_get_string(&sig8str));
        printf(" %s<0x%" DW_PR_XZEROS DW_PR_DUx ">",
            "typeoffset", typeoffset);
        esb_destructor(&sig8str);
    } else {
        struct esb_s sig8str;

        esb_constructor(&sig8str);
        format_sig8_string(signature,&sig8str);
        printf("  %-16s = %s\n", "signature",esb_get_string(&sig8str));
        printf("  %-16s = 0x%" DW_PR_XZEROS DW_PR_DUx " %" DW_PR_DUu "\n",
            "typeoffset",
            typeoffset,typeoffset);
        esb_destructor(&sig8str);
    }
}

static int
get_macinfo_offset(Dwarf_Debug dbg,
    Dwarf_Die cu_die,
    Dwarf_Unsigned *offset,
    Dwarf_Error *macerr)
{
    Dwarf_Attribute attrib= 0;
    int vres = 0;
    int ares = 0;

    ares = dwarf_attr(cu_die, DW_AT_macro_info, &attrib, macerr);
    if (ares == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "ERROR: getting dwarf_attr for DW_AT_macro_info"
            " failed",
            ares,*macerr);
        return ares;
    } else if (ares == DW_DLV_NO_ENTRY) {
        return ares;
    }
    vres = dwarf_global_formref(attrib,offset,macerr);
    if (vres == DW_DLV_ERROR) {
        dwarf_dealloc_attribute(attrib);
        print_error_and_continue(dbg,
        "ERROR: dwarf_global_formref on DW_AT_macro_info failed",
            vres, *macerr);
        return vres;
    } else if (vres == DW_DLV_OK) {
        dwarf_dealloc_attribute(attrib);
    }
    return vres;
}

static void
print_die_secname(Dwarf_Debug dbg,int is_info)
{
    if (print_as_info_or_by_cuname() &&
        glflags.gf_do_print_dwarf) {
        const char * section_name = 0;
        struct esb_s truename;
        char buf[ESB_FIXED_ALLOC_SIZE];

        if (is_info) {
            section_name = ".debug_info";
        } else  {
            section_name = ".debug_types";
        }
        esb_constructor_fixed(&truename,buf,sizeof(buf));
        get_true_section_name(dbg,section_name,
            &truename,TRUE);
        printf("\n%s\n",sanitized(esb_get_string(&truename)));
        esb_destructor(&truename);
    }
}

static Dwarf_Bool
empty_signature(const Dwarf_Sig8 *sigp)
{
    if (memcmp(sigp,&zerosig,sizeof(zerosig))) {
        return FALSE ; /* empty */
    }
    return TRUE;
}


static int
macro_check_cu(Dwarf_Debug dbg,
    Dwarf_Die cu_die2,
    Dwarf_Error *err)
{

    int mres = 0;
    Dwarf_Unsigned offset = 0;

    mres = get_macinfo_offset(dbg,cu_die2,&offset,err);
    if (mres == DW_DLV_NO_ENTRY) {
        /* By far the most likely result. */
        return mres;
    }else if (mres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "ERROR: get_macinfo_offset for "
            "DWARF 2,3,or 4 failed "
            "on a CU die",
            mres,*err);
        return mres;
    } else {
        mres = print_macinfo_by_offset(dbg,
            offset,err);
        if (mres==DW_DLV_ERROR) {
            struct esb_s m;

            esb_constructor(&m);
            esb_append_printf_u(&m,
                "\nERROR: printing macros for "
                " a CU at macinfo offset 0x%x "
                " failed ",offset);
            print_error_and_continue(dbg,
                esb_get_string(&m),
                mres,*err);
            esb_destructor(&m);
        }
    }
    return DW_DLV_OK;
}

/*   */
static int
print_one_die_section(Dwarf_Debug dbg,Dwarf_Bool is_info,
    Dwarf_Error *pod_err)
{
    Dwarf_Unsigned cu_header_length = 0;
    Dwarf_Unsigned abbrev_offset = 0;
    Dwarf_Half version_stamp = 0;
    Dwarf_Half address_size = 0;
    Dwarf_Half extension_size = 0;
    Dwarf_Half length_size = 0;
    Dwarf_Unsigned typeoffset = 0;
    Dwarf_Unsigned next_cu_offset = 0;
    unsigned loop_count = 0;
    int nres = DW_DLV_OK;
    int   cu_count = 0;
    char * cu_short_name = NULL;
    char * cu_long_name = NULL;
    int res = 0;
    Dwarf_Off dieprint_cu_goffset = 0;

    glflags.current_section_id = is_info?DEBUG_INFO:
        DEBUG_TYPES;
    {
        const char * test_section_name = 0;
        res = dwarf_get_die_section_name(dbg,is_info,
            &test_section_name,pod_err);
        if (res == DW_DLV_NO_ENTRY) {
            if(!is_info) {
                /*  No .debug_types. Do not print
                    .debug_types name */
                return DW_DLV_NO_ENTRY;
            }
        }
    }
    /* Loop until it fails.  */
    for (;;++loop_count) {
        int sres = DW_DLV_OK;
        Dwarf_Die cu_die = 0;
        Dwarf_Die cu_die2 = 0;
        struct Dwarf_Debug_Fission_Per_CU_s fission_data;
        int fission_data_result = 0;
        Dwarf_Half cu_type = 0;
        Dwarf_Sig8 signature;
        int offres = 0;

        signature = zerosig;
        /*  glflags.DIE_overall_offset: in case
            dwarf_next_cu_header_d fails due
            to corrupt dwarf. */
        glflags.DIE_overall_offset = dieprint_cu_goffset;
        memset(&fission_data,0,sizeof(fission_data));
        nres = dwarf_next_cu_header_d(dbg,
            is_info,
            &cu_header_length, &version_stamp,
            &abbrev_offset, &address_size,
            &length_size,&extension_size,
            &signature, &typeoffset,
            &next_cu_offset,
            &cu_type, pod_err);
        if (!loop_count) {
            /*  So compress flags show, we waited till
                section loaded to do this. */
            print_die_secname(dbg,is_info);
        }
        if (nres == DW_DLV_NO_ENTRY) {
            return nres;
        }
        if (nres == DW_DLV_ERROR) {
            /*  With corrupt DWARF due to a bad CU die
                we won't know much. */
            print_error_and_continue(dbg,
                "ERROR: Failure reading CU header"
                " or DIE, corrupt DWARF", nres, *pod_err);
            return nres;
        }
        if (cu_count >= glflags.break_after_n_units) {
            const char *m = "CUs";
            if (cu_count == 1) {
                m="CU";
            }
            printf("Break at %d %s\n",cu_count,m);
            break;
        }
        /*  Regardless of any options used, get basic
            information about the current CU: producer, name */
        sres = dwarf_siblingof_b(dbg, NULL,is_info, &cu_die, pod_err);
        if (sres != DW_DLV_OK) {
            /* There is no CU die, which should be impossible. */
            if(sres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "ERROR: dwarf_siblingof_b failed, no CU die",
                    sres, *pod_err);
                return sres;
            }
            print_error_and_continue(dbg,
                "ERROR: dwarf_siblingof_b got NO_ENTRY, no CU die",
                sres, *pod_err);
            return sres;
        }
        /*  Get the CU offset  (when we can)
            for easy error reporting. Ignore errors. */
        offres = dwarf_die_offsets(cu_die,
            &glflags.DIE_overall_offset,
            &glflags.DIE_offset,pod_err);
        DROP_ERROR_INSTANCE(dbg,offres,*pod_err);
        glflags.DIE_CU_overall_offset = glflags.DIE_overall_offset;
        glflags.DIE_CU_offset = glflags.DIE_offset;
        dieprint_cu_goffset = glflags.DIE_overall_offset;

        if (glflags.gf_cu_name_flag) {
            boolean should_skip = FALSE;

            /* always sets should_skip, even if error */
            should_skip_this_cu(dbg, &should_skip,cu_die);
            if (should_skip) {
                dwarf_dealloc_die(cu_die);
                cu_die = 0;
                ++cu_count;
                continue;
            }
        }
        {
        /*  Get producer name for this CU and
            update compiler list */
            int cures = 0;
            struct esb_s producername;

            esb_constructor(&producername);
            /* Fills in some producername no matter what status returned. */
            cures  = get_producer_name(dbg,cu_die,
                dieprint_cu_goffset,&producername,pod_err);
            DROP_ERROR_INSTANCE(dbg,cures,*pod_err);
            update_compiler_target(esb_get_string(&producername));
            esb_destructor(&producername);
        }

        /*  Once the compiler table has been updated, see
            if we need to generate the list of CU compiled
            by all the producers contained in the elf file */
        if (glflags.gf_producer_children_flag) {
            int chres = 0;

            chres = get_cu_name(dbg,cu_die,
                dieprint_cu_goffset,
                &cu_short_name,&cu_long_name,
                pod_err);
            if (chres == DW_DLV_ERROR ) {
                return chres;
            }
            if (chres == DW_DLV_OK) {
                /* Add CU name to current compiler entry */
                add_cu_name_compiler_target(cu_long_name);
            }
        }

        /*  If the current compiler is not requested by the
            user, then move to the next CU */
        if (!checking_this_compiler()) {
            dwarf_dealloc_die(cu_die);
            ++cu_count;
            cu_die = 0;
            continue;
        }
        fission_data_result = dwarf_get_debugfission_for_die(
            cu_die,
            &fission_data,pod_err);
        if (fission_data_result == DW_DLV_ERROR) {
            dwarf_dealloc_die(cu_die);
            cu_die = 0;
            print_error_and_continue(dbg,
                "ERROR: Failure looking for Debug Fission data",
                fission_data_result, *pod_err);
            return fission_data_result;
        }
        if(fission_data_result == DW_DLV_OK) {
            /*  In a .dwp file some checks get all sorts
                of spurious errors.  */
            glflags.gf_suppress_checking_on_dwp = TRUE;
            glflags.gf_check_ranges = FALSE;
            glflags.gf_check_aranges = FALSE;
            glflags.gf_check_decl_file = FALSE;
            glflags.gf_check_lines = FALSE;
            glflags.gf_check_pubname_attr = FALSE;
            glflags.gf_check_fdes = FALSE;
        }

        /*  We have not seen the compile unit  yet, reset these
            error-reporting  globals. */
        glflags.seen_CU = FALSE;
        glflags.need_CU_name = TRUE;
        glflags.need_CU_base_address = TRUE;
        glflags.need_CU_high_address = TRUE;
        /*  Some prerelease gcc versions used ranges but seemingly
            assumed the lack of a base address in the CU was
            defined to be a zero base.
            Assuming a base address (and low and high) is sensible. */
        glflags.CU_base_address = 0;
        glflags.CU_high_address = 0;
        glflags.CU_low_address = 0;

        /*  Release the 'cu_die' created by the call
            to 'dwarf_next_cu_header_d' at the
            top of the main loop. */
        dwarf_dealloc_die(cu_die);
        cu_die = 0; /* For debugging, stale die should be NULL. */

        if ((glflags.gf_info_flag || glflags.gf_types_flag) &&
            glflags.gf_do_print_dwarf) {
            if (glflags.verbose) {
                print_cu_hdr_std(cu_header_length,abbrev_offset,
                    version_stamp,address_size,length_size,
                    fission_data_result,cu_type,&fission_data);
                if (!empty_signature(&signature)) {
                    print_cu_hdr_signature(&signature,typeoffset);
                }
                if (glflags.dense) {
                    printf("\n");
                }
            } else {
                if (!empty_signature(&signature)) {
                    if (glflags.dense) {
                        printf("<%s>", "cu_header");
                    } else {
                        printf("\nCU_HEADER:\n");
                    }
                    print_cu_hdr_signature(&signature,typeoffset);
                    if (glflags.dense) {
                        printf("\n");
                    }
                }
            }
        }

        /* Get abbreviation info for this CU */
        get_abbrev_array_info(dbg,abbrev_offset);

        /*  Process a single compilation unit in .debug_info or
            .debug_types. */
        cu_die2 = 0;
        sres = dwarf_siblingof_b(dbg, NULL,is_info,
            &cu_die2, pod_err);
        if (sres == DW_DLV_OK) {
            int pres = 0;

            if (print_as_info_or_by_cuname() ||
                glflags.gf_search_is_on) {
                Dwarf_Signed cnt = 0;
                char **srcfiles = 0;
                Dwarf_Error srcerr = 0;
                int srcf =  0;

                srcf = dwarf_srcfiles(cu_die2,
                    &srcfiles, &cnt, &srcerr);
                if (srcf == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: dwarf_srcfiles problem ",
                        srcf,srcerr);
                    DROP_ERROR_INSTANCE(dbg,srcf,srcerr);
                    srcfiles = 0;
                    cnt = 0;
                } else if (srcf == DW_DLV_NO_ENTRY) {
                    /*DW_DLV_NO_ENTRY generally means there
                    there is no dW_AT_stmt_list attribute.
                    and we do not want to print anything
                    about statements in that case */
                }
                {
                    /*  Do regardless if dwarf_srcfiles
                        was successful to print die
                        and children as best we can
                        even with errors . */
                    int podres2 = 0;
                    Dwarf_Error lperr = 0;

                    /* Get the CU offset for easy error reporting */
                    podres2 = dwarf_die_offsets(cu_die2,
                        &glflags.DIE_overall_offset,
                        &glflags.DIE_offset,&lperr);
                    DROP_ERROR_INSTANCE(dbg,podres2,lperr);
                    glflags.DIE_CU_overall_offset =
                        glflags.DIE_overall_offset;
                    glflags.DIE_CU_offset = glflags.DIE_offset;
                    dieprint_cu_goffset = glflags.DIE_overall_offset;
                    pres = print_die_and_children(dbg, cu_die2,
                        dieprint_cu_goffset,is_info,
                        srcfiles, cnt,pod_err);
                    if (srcfiles) {
                        dealloc_all_srcfiles(dbg,srcfiles,cnt);
                        srcfiles = 0;
                        cnt = 0;
                    }
                    if (pres == DW_DLV_ERROR) {
                        dwarf_dealloc_die(cu_die2);
                        return pres;
                    }
                }
            }
            /* Dump Ranges Information */
            if (dump_ranges_info) {
                PrintBucketGroup(glflags.pRangesInfo,TRUE);
            }

            /* Check the range array if in checl mode */
            if ( glflags.gf_check_ranges) {
                int rares = 0;
                Dwarf_Error raerr = 0;

                rares = check_range_array_info(dbg,&raerr);
                if (rares == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: range array checks for "
                        "the current CU failed. ",
                        rares,raerr);
                    DROP_ERROR_INSTANCE(dbg,rares,raerr);
                }
            }

            /*  Traverse the line section if in check mode
                or if line-printing requested */
            if (glflags.gf_line_flag ||
                glflags.gf_check_decl_file) {
                int plnres = 0;

                int oldsection = glflags.current_section_id;
                plnres = print_line_numbers_this_cu(dbg,
                    cu_die2,pod_err);
                if (plnres == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: Printing line numbers for "
                        "the current CU failed. ",
                        plnres,*pod_err);
                    /*  Suppress the error so we print
                        whatever we can */
                    DROP_ERROR_INSTANCE(dbg,plnres,*pod_err);
                }
                glflags.current_section_id = oldsection;
            }
            if (glflags.gf_macro_flag || glflags.gf_check_macros) {
                int mres = 0;
                Dwarf_Bool in_import_list = FALSE;
                Dwarf_Unsigned import_offset = 0;
                int oldsection = glflags.current_section_id;

                mres = print_macros_5style_this_cu(dbg, cu_die2,
                    in_import_list,import_offset,pod_err);
                if (mres == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: Printing DWARF5 macros "
                        "for the current CU failed. ",
                        mres,*pod_err);
                    /*  Suppress the error so we print
                        whatever we can */
                    DROP_ERROR_INSTANCE(dbg,mres,*pod_err);
                }
                in_import_list = TRUE;
                if (mres == DW_DLV_OK) {
                    for(;;) {
                        /* Never returns DW_DLV_ERROR */
                        mres = get_next_unprinted_macro_offset(
                            &macro_check_tree, &import_offset);
                        if (mres == DW_DLV_NO_ENTRY) {
                            break;
                        }
                        mres = print_macros_5style_this_cu(dbg,
                            cu_die2,
                            in_import_list,import_offset,
                            pod_err);
                        if (mres == DW_DLV_ERROR) {
                            struct esb_s m;

                            esb_constructor(&m);
                            esb_append_printf_u(&m,
                                "ERROR: Printing DWARF5 macros "
                                " at offset 0x%x "
                                "for the current CU failed. ",
                                import_offset);
                            print_error_and_continue(dbg,
                                esb_get_string(&m),
                                mres,*pod_err);
                            DROP_ERROR_INSTANCE(dbg,mres,*pod_err);
                            esb_destructor(&m);
                            break;
                        }
                    }
                }
                glflags.current_section_id = oldsection;
            }
            if (glflags.gf_macinfo_flag ||
                glflags.gf_check_macros) {
                int mres = 0;

                mres = macro_check_cu(dbg,cu_die2,
                    pod_err);
                if (mres == DW_DLV_ERROR) {
                    if (cu_die2) {
                        dwarf_dealloc_die(cu_die2);
                    }
                    return mres;
                }
            }
            if (cu_die2) {
                dwarf_dealloc_die(cu_die2);
            }
            cu_die2 = 0;
        } else if (sres == DW_DLV_NO_ENTRY) {
            /* Do nothing I guess. */
        } else {
            print_error_and_continue(dbg,
                "ERROR: getting a compilation-unit "
                "CU die failed ",
                sres,*pod_err);
            DROP_ERROR_INSTANCE(dbg,sres,*pod_err);
        }
        cu_die2 = 0;
        ++cu_count;
    } /*  End loop on loop_count */
    return nres;
}

static int
print_a_die_stack(Dwarf_Debug dbg,
    char **srcfiles,
    Dwarf_Signed cnt,
    int lev,
    Dwarf_Error *err)
{
    /*  Print_information TRUE means attribute_matched
        will NOT be set by attribute name match.
        Just print the die at the top of stack.*/
    boolean print_else_name_match = TRUE;
    boolean ignore_die_stack = FALSE;
    boolean attribute_matched = FALSE;
    int res = 0;

    res = print_one_die(dbg,
        die_stack[lev].die_,
        die_stack[lev].cu_die_offset_,
        print_else_name_match,lev,srcfiles,cnt,
        &attribute_matched,
        ignore_die_stack,
        err);
    return res;
}

/* Called with a CU_Die as in_die_in. */
int
print_die_and_children(Dwarf_Debug dbg,
    Dwarf_Die in_die_in,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    char **srcfiles, Dwarf_Signed cnt,
    Dwarf_Error *err)
{
    int res = 0;

    local_symbols_already_began = FALSE;
    res  =print_die_and_children_internal(dbg,
        in_die_in, dieprint_cu_goffset,
        is_info,srcfiles,cnt,err);
    return res;
}

static int
print_die_stack(Dwarf_Debug dbg,
    char **srcfiles,
    Dwarf_Signed cnt,
    Dwarf_Error*err)
{
    int lev = 0;
    /*  Print_information TRUE means attribute_matched
        will NOT be set by attribute name match.
        Just print the dies in the stack.*/

    boolean print_else_name_match = TRUE;
    boolean ignore_die_stack = FALSE;
    boolean attribute_matched = FALSE;

    for (lev = 0; lev <= die_stack_indent_level; ++lev)
    {
        int res = 0;
        res = print_one_die(dbg,die_stack[lev].die_,
            die_stack[lev].cu_die_offset_,
            print_else_name_match,
            lev,srcfiles,cnt,
            &attribute_matched,
            ignore_die_stack,err);
        if (res == DW_DLV_ERROR) {
            return res;
        }
    }
    return DW_DLV_OK;
}

/* recursively follow the die tree */
static int
print_die_and_children_internal(Dwarf_Debug dbg,
    Dwarf_Die in_die_in,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    char **srcfiles, Dwarf_Signed cnt,
    Dwarf_Error *err)
{
    Dwarf_Die child = 0;
    Dwarf_Die sibling = 0;
    int cdres = 0;
    Dwarf_Die in_die = in_die_in;

    for (;;) {
        int offres = 0;

        /* Get the CU offset for easy error reporting */
        offres = dwarf_die_offsets(in_die,
            &glflags.DIE_overall_offset,
            &glflags.DIE_offset,err);
        DROP_ERROR_INSTANCE(dbg,offres,*err);
        SET_DIE_STACK_ENTRY(die_stack_indent_level,in_die,
            dieprint_cu_goffset);

        if ( glflags.gf_check_tag_tree ||
            glflags.gf_print_usage_tag_attr) {
            DWARF_CHECK_COUNT(tag_tree_result,1);
            if (die_stack_indent_level == 0) {
                Dwarf_Half tag = 0;
                int dtres = 0;

                dtres = dwarf_tag(in_die, &tag, err);
                if (dtres != DW_DLV_OK) {
                    DROP_ERROR_INSTANCE(dbg,dtres,*err);
                    DWARF_CHECK_ERROR(tag_tree_result,
                        "Tag-tree root tag unavailable: "
                        "is not DW_TAG_compile_unit");
                } else if (tag == DW_TAG_skeleton_unit) {
                    /* OK */
                } else if (tag == DW_TAG_compile_unit) {
                    /* OK */
                } else if (tag == DW_TAG_partial_unit) {
                    /* OK */
                } else if (tag == DW_TAG_type_unit) {
                    /* OK */
                } else {
                    DWARF_CHECK_ERROR(tag_tree_result,
                        "tag-tree root is not DW_TAG_compile_unit "
                        "or DW_TAG_partial_unit or DW_TAG_type_unit");
                }
            } else {
                Dwarf_Half tag_parent = 0;
                Dwarf_Half tag_child = 0;
                int pres = 0;
                int cres = 0;
                const char *ctagname = "<child tag invalid>";
                const char *ptagname = "<parent tag invalid>";

                pres = dwarf_tag(die_stack[
                    die_stack_indent_level - 1].die_,
                    &tag_parent, err);
                if (pres != DW_DLV_OK) {
                    if (in_die != in_die_in) {
                        dwarf_dealloc_die(in_die);
                    }
                    return cres;
                }
                cres = dwarf_tag(in_die, &tag_child, err);
                if (cres != DW_DLV_OK) {
                    return cres;
                }

                /* Check for specific compiler */
                if (checking_this_compiler()) {
                    /* Process specific TAGs. */
                    tag_specific_checks_setup(tag_child,
                        die_stack_indent_level);
                    if (cres != DW_DLV_OK || pres != DW_DLV_OK) {
                        if (cres == DW_DLV_OK) {
                            ctagname = get_TAG_name(tag_child,
                                pd_dwarf_names_print_on_error);
                        }
                        if (pres == DW_DLV_OK) {
                            ptagname = get_TAG_name(tag_parent,
                                pd_dwarf_names_print_on_error);
                        }
                        DWARF_CHECK_ERROR3(tag_tree_result,
                            ptagname,
                            ctagname,
                            "Tag-tree relation is not standard..");
                    } else if (legal_tag_tree_combination(tag_parent,
                        tag_child)) {
                        /* OK */
                    } else {
                        /* Report errors only if tag-tree check is on */
                        if (glflags.gf_check_tag_tree) {
                            DWARF_CHECK_ERROR3(tag_tree_result,
                                get_TAG_name(tag_parent,
                                    pd_dwarf_names_print_on_error),
                                get_TAG_name(tag_child,
                                    pd_dwarf_names_print_on_error),
                                "tag-tree relation is not standard.");
                        }
                    }
                }
            }
        }
        if (glflags.gf_record_dwarf_error &&
            glflags.gf_check_verbose_mode) {
            glflags.gf_record_dwarf_error = FALSE;
        }
        /* Here do pre-descent processing of the die. */
        {
            boolean an_attribute_match_local = FALSE;
            boolean ignore_die_stack = FALSE;
            int pdres = 0;
            pdres = print_one_die(dbg, in_die,
                dieprint_cu_goffset,
                print_as_info_or_by_cuname(),
                die_stack_indent_level, srcfiles, cnt,
                &an_attribute_match_local,
                ignore_die_stack,
                err);
            if (pdres != DW_DLV_OK) {
                if (in_die != in_die_in) {
                    dwarf_dealloc_die(in_die);
                }
                return pdres;
            }
            validate_die_stack_siblings(dbg);
            if (!print_as_info_or_by_cuname() &&
                an_attribute_match_local) {
                if (glflags.gf_display_parent_tree) {
                    pdres = print_die_stack(dbg,srcfiles,cnt,
                        err);
                    if (pdres == DW_DLV_ERROR) {
                        if (in_die != in_die_in) {
                            dwarf_dealloc_die(in_die);
                        }
                        return pdres;
                    }
                } else {
                    if (glflags.gf_display_children_tree) {
                        pdres = print_a_die_stack(dbg,srcfiles,cnt,
                            die_stack_indent_level,err);
                        if (pdres == DW_DLV_ERROR) {
                            if (in_die != in_die_in) {
                                dwarf_dealloc_die(in_die);
                            }
                            return pdres;
                        }
                    }
                }
                if (glflags.gf_display_children_tree) {
                    glflags.gf_stop_indent_level =
                        die_stack_indent_level;
                    glflags.gf_info_flag = TRUE;
                    glflags.gf_types_flag = TRUE;
                }
            }
        }

        cdres = dwarf_child(in_die, &child, err);
        if (cdres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Call to dwarf_child failed printing die tree",
                cdres,*err);
            if (in_die != in_die_in) {
                dwarf_dealloc_die(in_die);
            }
            return cdres;
        }
        /* Check for specific compiler */
        if (glflags.gf_check_abbreviations &&
            checking_this_compiler()) {
            Dwarf_Half ab_has_child;
            Dwarf_Bool bError = FALSE;
            Dwarf_Half tag = 0;
            int abtres = 0;

            /* This does not return a Dwarf_Error value! */
            abtres = dwarf_die_abbrev_children_flag(in_die,
                &ab_has_child);
            if (abtres == DW_DLV_OK) {
                Dwarf_Error tagerr = 0;

                DWARF_CHECK_COUNT(abbreviations_result,1);
                abtres = dwarf_tag(in_die, &tag, &tagerr);
                if (abtres == DW_DLV_OK) {
                    switch (tag) {
                    case DW_TAG_array_type:
                    case DW_TAG_class_type:
                    case DW_TAG_compile_unit:
                    case DW_TAG_type_unit:
                    case DW_TAG_partial_unit:
                    case DW_TAG_enumeration_type:
                    case DW_TAG_lexical_block:
                    case DW_TAG_namespace:
                    case DW_TAG_structure_type:
                    case DW_TAG_subprogram:
                    case DW_TAG_subroutine_type:
                    case DW_TAG_union_type:
                    case DW_TAG_entry_point:
                    case DW_TAG_inlined_subroutine:
                        break;
                    default:
                        bError =
                            (cdres == DW_DLV_OK && !ab_has_child)
                            ||
                            (cdres == DW_DLV_NO_ENTRY && ab_has_child);
                        if (bError) {
                            DWARF_CHECK_ERROR(abbreviations_result,
                                "check 'dw_children' flag combination.");
                        }
                        break;
                    }
                } else if (abtres == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "Unable to read die tag!",
                        abtres,*err);
                    return abtres;
                }
            } else if (abtres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Unable to read die children flag",
                    abtres,*err);
                return abtres;
            }
        }
        /* child first: we are doing depth-first walk */
        if (cdres == DW_DLV_OK) {
            /*  If the global offset of the (first) child is
                <= the parent DW_AT_sibling global-offset-value
                then the compiler has made a mistake, and
                the DIE tree is corrupt.  */
            int pdacres = 0;
            Dwarf_Off child_overall_offset = 0;
            int cores = dwarf_dieoffset(child,
                &child_overall_offset, err);

            if (cores == DW_DLV_OK) {
                Dwarf_Off parent_sib_val = get_die_stack_sibling();

                if (parent_sib_val &&
                    (parent_sib_val <= child_overall_offset )) {
                    char small_buf[ESB_FIXED_ALLOC_SIZE];
                    struct esb_s pm;

                    esb_constructor_fixed(&pm,small_buf,
                        sizeof(small_buf));
                    glflags.gf_count_major_errors++;
                    esb_append_printf_u(&pm,
                        "ERROR: A parent DW_AT_sibling of "
                        "0x%" DW_PR_XZEROS  DW_PR_DUx,
                        parent_sib_val);
                    esb_append_printf_s(&pm,
                        " points %s the first child ",
                        (parent_sib_val == child_overall_offset)?
                            "at":"before");
                    esb_append_printf_u(&pm,
                        "0x%"  DW_PR_XZEROS  DW_PR_DUx
                        " so the die tree is corrupt "
                        "(showing section, not CU, offsets). ",
                        child_overall_offset);
                    dwarf_error_creation(dbg,err,
                        esb_get_string(&pm));
                    print_error_and_continue(dbg,
                        esb_get_string(&pm),
                        DW_DLV_ERROR,*err);
                    /*   Original test did a print_error()
                        here, which did exit().
                        We would like to return ERROR all the way
                        back, but have no way at present
                        to generate a Dwarf_Error record.
                        Because these sorts of errors
                        are not really recoverable.
                    */
                    esb_destructor(&pm);
                    if (in_die != in_die_in) {
                        dwarf_dealloc_die(in_die);
                    }
                    return DW_DLV_ERROR;
                }
            } else if (cores == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Finding a DIE offset (dwarf_dieoffset())"
                    "failed.",cores,*err);
                if (in_die != in_die_in) {
                    dwarf_dealloc_die(in_die);
                }
                return cores;
            }

            die_stack_indent_level++;
            SET_DIE_STACK_ENTRY(die_stack_indent_level,0,
                dieprint_cu_goffset);
            if (die_stack_indent_level >= DIE_STACK_SIZE ) {
                struct esb_s m;

                esb_constructor(&m);
                esb_append_printf_i(&m,
                    "ERROR: compiled in DIE_STACK_SIZE "
                    "(the depth of the DIE tree in this CU)"
                    " of %d exceeded!"
                    ,DIE_STACK_SIZE);
                dwarf_error_creation(dbg,err,
                    esb_get_string(&m));
                print_error_and_continue(dbg,
                    esb_get_string(&m),
                    DW_DLV_OK,*err);
                esb_destructor(&m);
                if (in_die != in_die_in) {
                    dwarf_dealloc_die(in_die);
                }
                return DW_DLV_ERROR;

            }
            pdacres = print_die_and_children_internal(dbg, child,
                dieprint_cu_goffset,
                is_info, srcfiles, cnt,err);
            EMPTY_DIE_STACK_ENTRY(die_stack_indent_level);
            dwarf_dealloc_die(child);
            die_stack_indent_level--;
            if (pdacres == DW_DLV_ERROR) {
                if (in_die != in_die_in) {
                    dwarf_dealloc_die(in_die);
                }
                return pdacres;
            }
            child = 0;
        } else if (cdres == DW_DLV_ERROR) {
            if (in_die != in_die_in) {
                dwarf_dealloc_die(in_die);
            }
            return cdres;
        }
        /* Stop the display of all children */
        if (glflags.gf_display_children_tree &&
            (glflags.gf_info_flag || glflags.gf_types_flag) &&
            glflags.gf_stop_indent_level == die_stack_indent_level) {

            glflags.gf_info_flag = FALSE;
            glflags.gf_types_flag = FALSE;
        }
        sibling = 0;
        cdres = dwarf_siblingof_b(dbg, in_die,is_info,
            &sibling, err);
        if (cdres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: dwarf_siblingof fails"
                " tracing siblings of a DIE.",
                cdres, *err);
            if (in_die != in_die_in) {
                dwarf_dealloc_die(in_die);
            }
            return cdres;
        }
        /*  print_die_and_children(dbg,sibling,srcfiles,cnt);
            We loop around to actually print this, rather than
            recursing. Recursing is horribly wasteful of stack
            space. */
        /*  If we have a sibling, verify that its offset
            is next to the last processed DIE;
            An incorrect sibling chain is a nasty bug.  */
        if (cdres == DW_DLV_OK && sibling &&
            glflags.gf_check_di_gaps &&
            checking_this_compiler()) {

            Dwarf_Off glb_off;
            DWARF_CHECK_COUNT(di_gaps_result,1);
            if (dwarf_validate_die_sibling(sibling,&glb_off) ==
                DW_DLV_ERROR) {
                Dwarf_Off sib_off;
                struct esb_s msg;

                esb_constructor(&msg);
                dwarf_dieoffset(sibling,&sib_off,err);
                esb_append_printf_u(&msg,
                    "GSIB = 0x%" DW_PR_XZEROS  DW_PR_DUx,
                    sib_off);
                esb_append_printf_u(&msg,
                    " GOFF = 0x%" DW_PR_XZEROS DW_PR_DUx,
                    glb_off);
                esb_append_printf_u(&msg,
                    " Gap = %" DW_PR_DUu " bytes",
                    sib_off-glb_off);
                DWARF_CHECK_ERROR2(di_gaps_result,
                    "Incorrect sibling chain",esb_get_string(&msg));
                esb_destructor(&msg);
            }
        }

        /*  Here do any post-descent (ie post-dwarf_child)
            processing of the in_die. */

        EMPTY_DIE_STACK_ENTRY(die_stack_indent_level);
        if (in_die != in_die_in) {
            /*  Dealloc our in_die, but not the
                argument die, it belongs
                to our caller. Whether the siblingof
                call worked or not. */
            dwarf_dealloc_die(in_die);
            in_die = 0;
        }
        if (cdres == DW_DLV_OK) {
            /*  Set to process the sibling, loop again. */
            in_die = sibling;
            sibling = 0;
        } else {
            /* ASSERT: cdres is DW_DLV_NO_ENTRY  */
            sibling = 0;
            in_die = 0;
            /*  We are done, no more siblings at this level. */
            break;
        }
    }  /* end for loop on siblings */
    return DW_DLV_OK;
}

static void
dealloc_local_atlist(Dwarf_Debug dbg,
    Dwarf_Attribute *atlist,
    Dwarf_Signed    atcnt)
{
    Dwarf_Signed i = 0;

    for (i = 0; i < atcnt; i++) {
        dwarf_dealloc_attribute(atlist[i]);
        atlist[i] = 0;
    }
    dwarf_dealloc(dbg, atlist, DW_DLA_LIST);
}


/* Print one die on error and verbose or non check mode */
#define PRINTING_DIES (glflags.gf_do_print_dwarf || \
    (glflags.gf_record_dwarf_error && glflags.gf_check_verbose_mode))

/*  If print_else_name_match is FALSE,
    check for attribute  matches with -S
    inr print_attribute, and if found,
    print the information anyway.

    if print_else_name_match is true, do not check
    for attribute name matches. Just print.

    Sets *an_attr_matched TRUE if there is
    attribute name or value that matches
    a -S option (the long form option starts with --search)

    Returns DW_DLV_OK DW_DLV_ERROR or DW_DLV_NO_ENTRY
*/
int
print_one_die(Dwarf_Debug dbg, Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    boolean print_else_name_match,
    int die_indent_level,
    char **srcfiles, Dwarf_Signed cnt,
    boolean *an_attr_matched_io,
    boolean ignore_die_stack,
    Dwarf_Error *err)
{
    Dwarf_Signed i = 0;
    Dwarf_Signed j = 0;
    Dwarf_Off offset = 0;
    Dwarf_Off overall_offset = 0;
    const char * tagname = 0;
    Dwarf_Half tag = 0;
    Dwarf_Signed atcnt = 0;
    Dwarf_Attribute *atlist = 0;
    int tres = 0;
    int ores = 0;
    boolean attribute_matchedpod = FALSE;
    int atres = 0;
    int abbrev_code = dwarf_die_abbrev_code(die);

    /* Print using indentation
    < 1><0x000854ff GOFF=0x00546047>    DW_TAG_pointer_type -> 34
    < 1><0x000854ff>    DW_TAG_pointer_type                 -> 18
        DW_TAG_pointer_type                                 ->  2
    */
    /* Attribute indent. */
    int nColumn = glflags.gf_show_global_offsets ? 34 : 18;

    if (glflags.gf_check_abbreviations &&
        checking_this_compiler()) {
        validate_abbrev_code(dbg,abbrev_code);
    }

    if (!ignore_die_stack &&
        die_stack[die_indent_level].already_printed_) {
        /* FALSE seems safe . */
        *an_attr_matched_io = FALSE;
        return DW_DLV_OK;
    }

    /* Reset indentation column if no offsets */
    if (!glflags.gf_display_offsets) {
        nColumn = 2;
    }

    tres = dwarf_tag(die, &tag, err);
    if (tres != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "ERROR: accessing tag of die!",
            tres, *err);
        return tres;
    }
    tagname = get_TAG_name(tag,pd_dwarf_names_print_on_error);

#ifdef HAVE_USAGE_TAG_ATTR
    /* Record usage of TAGs */
    if ( glflags.gf_print_usage_tag_attr && tag < DW_TAG_last) {
        ++tag_usage[tag];
    }
#endif /* HAVE_USAGE_TAG_ATTR */

    tag_specific_checks_setup(tag,die_indent_level);
    ores = dwarf_dieoffset(die, &overall_offset, err);
    if (ores != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "ERROR: failed dwarf_dieoffset call", ores, *err);
        return ores;
    }
    ores = dwarf_die_CU_offset(die, &offset, err);
    if (ores != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "ERROR: dwarf_die_CU_offset failed", ores, *err);
        return ores;
    }

    if (dump_visited_info &&  glflags.gf_check_self_references) {
        printf("<%2d><0x%" DW_PR_XZEROS DW_PR_DUx
            " GOFF=0x%" DW_PR_XZEROS DW_PR_DUx "> ",
            die_indent_level, (Dwarf_Unsigned)offset,
            (Dwarf_Unsigned)overall_offset);
        printf("%*s%s\n",die_indent_level * 2 + 2," ",tagname);
    }

    /* Print the die */
    if (PRINTING_DIES && print_else_name_match) {
        if (!ignore_die_stack) {
            die_stack[die_indent_level].already_printed_ = TRUE;
        }
        if (die_indent_level == 0) {
            print_cu_hdr_cudie(dbg,die, overall_offset, offset);
        } else if (local_symbols_already_began == FALSE &&
            die_indent_level == 1 && !glflags.dense) {

            printf("\nLOCAL_SYMBOLS:\n");
            local_symbols_already_began = TRUE;
        }

        /* Print just the Tags and Attributes */
        if (!glflags.gf_display_offsets) {
            /* Print using indentation */
            printf("%*s%s\n",die_stack_indent_level * 2 + 2," ",tagname);
        } else {
            if (glflags.dense) {
                if (glflags.gf_show_global_offsets) {
                    if (die_indent_level == 0) {
                        printf("<%d><0x%" DW_PR_DUx "+0x%" DW_PR_DUx " GOFF=0x%"
                            DW_PR_DUx ">", die_indent_level,
                            (Dwarf_Unsigned)(overall_offset - offset),
                            (Dwarf_Unsigned)offset,
                                (Dwarf_Unsigned)overall_offset);
                        } else {
                        printf("<%d><0x%" DW_PR_DUx " GOFF=0x%" DW_PR_DUx ">",
                            die_indent_level,
                            (Dwarf_Unsigned)offset,
                            (Dwarf_Unsigned)overall_offset);
                    }
                } else {
                    if (die_indent_level == 0) {
                        printf("<%d><0x%" DW_PR_DUx "+0x%" DW_PR_DUx ">",
                            die_indent_level,
                            (Dwarf_Unsigned)(overall_offset - offset),
                            (Dwarf_Unsigned)offset);
                    } else {
                        printf("<%d><0x%" DW_PR_DUx ">", die_indent_level,
                            (Dwarf_Unsigned)offset);
                    }
                }
                printf("<%s>",tagname);
                if (glflags.verbose) {
                    Dwarf_Off agoff = 0;
                    Dwarf_Unsigned acount = 0;
                    printf(" <abbrev %d",abbrev_code);
                    if (glflags.gf_show_global_offsets) {
                        int agres = 0;

                        agres = dwarf_die_abbrev_global_offset(die,
                            &agoff, &acount,err);
                        if(agres == DW_DLV_ERROR) {
                            print_error_and_continue(dbg,
                                "dwarf_die_abbrev_global_offset call "
                                " failed",
                                agres, *err);
                            return agres;
                        } else if (agres == DW_DLV_NO_ENTRY) {
                            print_error_and_continue(dbg,
                                "dwarf_die_abbrev_global_offset "
                                "no entry?",
                                agres, *err);
                            return agres;
                        } else {
                            printf(" ABGOFF = 0x%"
                                DW_PR_XZEROS DW_PR_DUx
                                " count = 0x%" DW_PR_XZEROS DW_PR_DUx,
                                agoff, acount);
                        }
                    }
                    printf(">");
                }
            } else {
                if (glflags.gf_show_global_offsets) {
                    printf("<%2d><0x%" DW_PR_XZEROS DW_PR_DUx
                        " GOFF=0x%" DW_PR_XZEROS DW_PR_DUx ">",
                        die_indent_level, (Dwarf_Unsigned)offset,
                        (Dwarf_Unsigned)overall_offset);
                } else {
                    printf("<%2d><0x%" DW_PR_XZEROS DW_PR_DUx ">",
                        die_indent_level,
                        (Dwarf_Unsigned)offset);
                }

                /* Print using indentation */
                printf("%*s%s",die_indent_level * 2 + 2," ",tagname);
                if (glflags.verbose) {
                    Dwarf_Off agoff = 0;
                    Dwarf_Unsigned acount = 0;
                    printf(" <abbrev %d",abbrev_code);
                    if (glflags.gf_show_global_offsets) {
                        int agres = 0;

                        agres = dwarf_die_abbrev_global_offset(die,
                            &agoff, &acount,err);
                        if(agres == DW_DLV_ERROR) {
                            print_error_and_continue(dbg,
                                "Call to "
                                "dwarf_die_abbrev_global_offset"
                                "failed ",
                                agres, *err);
                            return agres;
                        } else if (agres == DW_DLV_NO_ENTRY) {
                            print_error_and_continue(dbg,
                                "Call to "
                                "dwarf_die_abbrev_global_offset "
                                " returned NO_ENTRY!",
                                agres,*err);
                            return agres;
                        } else {
                            printf(" ABGOFF = 0x%"
                                DW_PR_XZEROS DW_PR_DUx
                                " count = 0x%" DW_PR_XZEROS DW_PR_DUx,
                                agoff, acount);
                        }
                    }
                    printf(">");
                }
                fputs("\n",stdout);
            }
        }
    }

    atres = dwarf_attrlist(die, &atlist, &atcnt, err);
    if (atres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "ERROR: A call to dwarf_attrlist failed. "
            " Impossible error.", atres,*err);
        return atres;
    } else if (atres == DW_DLV_NO_ENTRY) {
        /* indicates there are no attrs.  It is not an error. */
        atcnt = 0;
    }

    /* Reset any loose references to low or high PC */
    bSawLow = FALSE;
    bSawHigh = FALSE;

    /* Get the offset for easy error reporting: This is not the CU die.  */
    atres = dwarf_die_offsets(die,&glflags.DIE_overall_offset,
        &glflags.DIE_offset,err);
    if (atres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "ERROR: A call to dwarf_die_offsets failed in "
            "printing an attribute. "
            , atres,*err);
        dealloc_local_atlist(dbg,atlist,atcnt);
        return atres;
    }

    for (i = 0; i < atcnt; i++) {
        Dwarf_Half attr;
        int ares;

        ares = dwarf_whatattr(atlist[i], &attr, err);
        if (ares == DW_DLV_OK) {
            /*  Check duplicated attributes; use brute force
                as the number of attributes is quite small;
                the problem was detected with the
                LLVM toolchain, generating more than 12
                repeated attributes */
            if (glflags.gf_check_duplicated_attributes) {
                Dwarf_Half attr_next;
                DWARF_CHECK_COUNT(duplicated_attributes_result,1);
                for (j = i + 1; j < atcnt; ++j) {
                    ares = dwarf_whatattr(atlist[j], &attr_next,err);
                    if (ares == DW_DLV_OK) {
                        if (attr == attr_next) {
                            DWARF_CHECK_ERROR2(
                                duplicated_attributes_result,
                                "Duplicated attribute ",
                                get_AT_name(attr,
                                    pd_dwarf_names_print_on_error));
                        }
                    } else {
                        struct esb_s m;
                        esb_constructor(&m);
                        esb_append_printf_i(&m,
                            "ERROR: dwarf_whatattr entry missing "
                            " when checking for duplicated attributes"
                            " reading attribute ",j);
                        print_error_and_continue(dbg,
                            esb_get_string(&m),
                            ares, *err);
                        esb_destructor(&m);
                        dealloc_local_atlist(dbg,atlist,atcnt);
                        return ares;
                    }
                }
            }

            /* Print using indentation */
            if (!glflags.dense && PRINTING_DIES &&
                print_else_name_match) {

                printf("%*s",die_indent_level * 2 + 2 + nColumn,
                    " ");
            }
            {
                boolean attr_match_localb = FALSE;
                int aresb = 0;

                aresb = print_attribute(dbg, die,
                    dieprint_cu_goffset,
                    attr,
                    atlist[i],
                    print_else_name_match, die_indent_level,
                    srcfiles, cnt,
                    &attr_match_localb,err);
                if (aresb == DW_DLV_ERROR) {
                    struct esb_s m;

                    esb_constructor(&m);
                    esb_append_printf_i(&m,
                        "ERROR: Failed printing attribute %d",
                        i);
                    esb_append_printf_i(&m,
                        " of %d attributes.",atcnt);
                    print_error_and_continue(dbg,
                        esb_get_string(&m),
                        aresb,*err);
                    esb_destructor(&m);
                    dealloc_local_atlist(dbg,atlist,atcnt);
                    return aresb;
                }
                if (print_else_name_match == FALSE && attr_match_localb) {
                    attribute_matchedpod = TRUE;
                }
            }

            if (glflags.gf_record_dwarf_error &&
                glflags.gf_check_verbose_mode) {
                glflags.gf_record_dwarf_error = FALSE;
            }
        } else {
            struct esb_s m;

            esb_constructor(&m);
            esb_append_printf_i(&m,
                "ERROR: Failed getting attribute %d",
                i);
            esb_append_printf_i(&m,
                " of %d attributes.",
                atcnt);
            print_error_and_continue(dbg,
                esb_get_string(&m),ares,*err);
            esb_destructor(&m);
            dealloc_local_atlist(dbg,atlist,atcnt);
            return ares;
        }
    }
    /* atres might have been DW_DLV_NO_ENTRY, atlist NULL */
    if (atlist) {
        dealloc_local_atlist(dbg,atlist,atcnt);
    }
    if (PRINTING_DIES && glflags.dense && print_else_name_match) {
        printf("\n");
    }
    *an_attr_matched_io = attribute_matchedpod;
    return DW_DLV_OK;
}

/*  Encodings have undefined signedness. Accept either
    signedness.  The values are integer-like (they are defined
    in the DWARF specification), so the
    form the compiler uses (as long as it is
    a constant value) is a non-issue.

    The numbers need not be small (in spite of the
    function name), but the result should be an integer.

    If string_out is non-NULL, construct a string output, either
    an error message or the name of the encoding.
    The function pointer passed in is to code generated
    by a script at dwarfdump build time. The code for
    the val_as_string function is generated
    from dwarf.h.  See <build dir>/dwarf_names.c

    The known_signed bool is set true(nonzero) or false (zero)
    and *both* uval_out and sval_out are set to the value,
    though of course uval_out cannot represent a signed
    value properly and sval_out cannot represent all unsigned
    values properly.

    If string_out is non-NULL then attr_name and val_as_string
    must also be non-NULL.  */
static int
get_small_encoding_integer_and_name(Dwarf_Debug dbg,
    Dwarf_Attribute attrib,
    Dwarf_Unsigned * uval_out,
    const char *attr_name,
    struct esb_s* string_out,
    encoding_type_func val_as_string,
    Dwarf_Error * err,
    int show_form)
{
    Dwarf_Unsigned uval = 0;


    int vres = dwarf_formudata(attrib, &uval, err);
    /* It is not formudata, lets check further */
    DROP_ERROR_INSTANCE(dbg,vres,*err);
    if (vres != DW_DLV_OK) {
        Dwarf_Signed sval = 0;
        int ires = 0;

        ires = dwarf_formsdata(attrib, &sval, err);
        /* It is not formudata, lets check further */
        DROP_ERROR_INSTANCE(dbg,ires,*err);
        if (ires != DW_DLV_OK) {
            int jres = 0;

            jres = dwarf_global_formref(attrib,&uval,err);
            if (jres != DW_DLV_OK) {
                if (string_out) {
                    glflags.gf_count_major_errors++;
                    esb_append_printf_s(string_out,
                        "ERROR: %s has a bad form"
                        " for reading a value.",
                        attr_name);
                }
                return jres;
            }
            *uval_out = uval;
        } else {
            uval =  (Dwarf_Unsigned) sval;
            *uval_out = uval;
        }
    } else {
        *uval_out = uval;
    }
    if (string_out) {
        Dwarf_Half theform = 0;
        Dwarf_Half directform = 0;
        char fsbuf[ESB_FIXED_ALLOC_SIZE];
        struct esb_s fstring;
        int fres = 0;

        esb_constructor_fixed(&fstring,fsbuf,sizeof(fsbuf));
        fres  = get_form_values(dbg,attrib,&theform,&directform,err);
        if (fres == DW_DLV_ERROR) {
            return fres;
        }
        esb_append(&fstring, val_as_string((Dwarf_Half) uval,
            pd_dwarf_names_print_on_error));
        show_form_itself(show_form,glflags.verbose,theform,
            directform,&fstring);
        esb_append(string_out,esb_get_string(&fstring));
        esb_destructor(&fstring);
    }
    return DW_DLV_OK;
}




/*  Called for DW_AT_SUN_func_offsets
    We need a 32-bit signed number here.
    But we're getting rid of the __[u]int[n]_t
    dependence so lets use plain characters.

    This is rarely, if ever, used so lets
    report errors but not stop the processing.
    */

static void
get_FLAG_BLOCK_string(Dwarf_Debug dbg, Dwarf_Attribute attrib,
    struct esb_s*esbp)
{
    int fres = 0;
    Dwarf_Block *tempb = 0;
    Dwarf_Unsigned array_len = 0;
    Dwarf_Signed *array = 0;
    Dwarf_Unsigned next = 0;
    Dwarf_Error  fblkerr = 0;

    /* first get compressed block data */
    fres = dwarf_formblock (attrib,&tempb, &fblkerr);
    if (fres != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "DW_FORM_blockn cannot get block\n",
            fres,fblkerr);
        DROP_ERROR_INSTANCE(dbg,fres,fblkerr);
        return;
    }

    fres = dwarf_uncompress_integer_block_a(dbg,
        tempb->bl_len,
        (void *)tempb->bl_data,
        &array_len,&array,&fblkerr);
    /*  uncompress block into 32bit signed int array.
        It's really a block of sleb numbers so the
        compression is minor unless the values
        are close to zero.  */
    if (fres != DW_DLV_OK) {
        dwarf_dealloc(dbg,tempb,DW_DLA_BLOCK);
        print_error_and_continue(dbg,
            "DW_AT_SUN_func_offsets cannot uncompress data\n",
            0,fblkerr);
        DROP_ERROR_INSTANCE(dbg,fres,fblkerr);
        return;
    }
    if (array_len == 0) {
        print_error_and_continue(dbg,
            "DW_AT_SUN_func_offsets has no data (array"
            " length is zero), something badly"
            " wrong",
            DW_DLV_OK,fblkerr);
        return;
    }

    /* fill in string buffer */
    next = 0;
    while (next < array_len) {
        unsigned i = 0;
        /*  Print a full line */
        esb_append(esbp,"\n  ");
        for(i = 0 ; i < 2 && next < array_len; ++i,++next) {
            Dwarf_Signed vs = array[next];
            Dwarf_Unsigned vu = (Dwarf_Unsigned)vs;

            if (i== 1) {
                esb_append(esbp," ");
            }
            esb_append_printf_i(esbp,"%6" DW_PR_DSd " ",vs);
            esb_append_printf_u(esbp,
                "(0x%"  DW_PR_XZEROS DW_PR_DUx ")",vu);
        }
    }
    dwarf_dealloc(dbg,tempb,DW_DLA_BLOCK);
    /* free array buffer */
    dwarf_dealloc_uncompressed_block(dbg, array);
}

static const char *
get_rangelist_type_descr(Dwarf_Ranges *r)
{
    switch (r->dwr_type) {
    case DW_RANGES_ENTRY:             return "range entry";
    case DW_RANGES_ADDRESS_SELECTION: return "addr selection";
    case DW_RANGES_END:               return "range end";
    }
    /* Impossible. */
    return "Unknown";
}


/*  The string produced here will need to be passed
    through sanitized() before actually printing.
    Always returns DW_DLV_OK.  */
int
print_ranges_list_to_extra(Dwarf_Debug dbg,
    Dwarf_Unsigned off,
    Dwarf_Ranges *rangeset,
    Dwarf_Signed rangecount,
    Dwarf_Unsigned bytecount,
    struct esb_s *stringbuf)
{
    const char * sec_name = 0;
    Dwarf_Signed i = 0;
    struct esb_s truename;
    char buf[ESB_FIXED_ALLOC_SIZE];

    esb_constructor_fixed(&truename,buf,sizeof(buf));
    /* We don't want to set the compress data into the secname here. */
    get_true_section_name(dbg,".debug_ranges",
        &truename,FALSE);
    sec_name = esb_get_string(&truename);
    if (glflags.dense) {
        esb_append_printf_i(stringbuf,"< ranges: %" DW_PR_DSd,rangecount);
        esb_append_printf_s(stringbuf," ranges at %s" ,
            sanitized(sec_name));
        esb_append_printf_u(stringbuf," offset %" DW_PR_DUu ,off);
        esb_append_printf_u(stringbuf," (0x%" DW_PR_XZEROS DW_PR_DUx,off);
        esb_append_printf_u(stringbuf,") " "(%" DW_PR_DUu " bytes)>",bytecount);
    } else {
        esb_append_printf_i(stringbuf,"  ranges: %" DW_PR_DSd,rangecount);
        esb_append_printf_s(stringbuf," at %s" ,
            sanitized(sec_name));
        esb_append_printf_u(stringbuf," offset %" DW_PR_DUu ,off);
        esb_append_printf_u(stringbuf," (0x%" DW_PR_XZEROS DW_PR_DUx,off);
        esb_append_printf_u(stringbuf,") " "(%" DW_PR_DUu " bytes)\n",
            bytecount);
    }
    for (i = 0; i < rangecount; ++i) {
        Dwarf_Ranges * r = rangeset +i;
        const char *type = get_rangelist_type_descr(r);
        if (glflags.dense) {
            esb_append_printf_i(stringbuf,"<[%2" DW_PR_DSd,i);
            esb_append_printf_s(stringbuf,"] %s",type);
            esb_append_printf_u(stringbuf," 0x%" DW_PR_XZEROS  DW_PR_DUx,r->dwr_addr1);
            esb_append_printf_u(stringbuf," 0x%" DW_PR_XZEROS  DW_PR_DUx ">",r->dwr_addr2);
        } else {
            esb_append_printf_i(stringbuf,"   [%2" DW_PR_DSd,i);
            esb_append_printf_s(stringbuf,"] %-14s",type);
            esb_append_printf_u(stringbuf," 0x%" DW_PR_XZEROS  DW_PR_DUx,
                r->dwr_addr1);
            esb_append_printf_u(stringbuf,
                " 0x%" DW_PR_XZEROS  DW_PR_DUx "\n",r->dwr_addr2);
        }
    }
    esb_destructor(&truename);
    return DW_DLV_OK;
}

static void
do_dump_visited_info(int level, Dwarf_Off loff,Dwarf_Off goff,
    Dwarf_Off cu_die_goff,
    const char *atname, const char *valname)
{
    printf("<%2d><0x%" DW_PR_XZEROS DW_PR_DUx
        " GOFF=0x%" DW_PR_XZEROS DW_PR_DUx
        " CU-GOFF=0x%" DW_PR_XZEROS DW_PR_DUx
        "> ",
        level, loff, goff,cu_die_goff);
    printf("%*s%s -> %s\n",level * 2 + 2,
        " ",atname,valname);
}

/*  DW_FORM_data16 should not apply here. */
static boolean
is_simple_location_expr(int form)
{
    if (form == DW_FORM_block1 ||
        form == DW_FORM_block2 ||
        form == DW_FORM_block4 ||
        form == DW_FORM_block ) {
        return TRUE;
    }
    return FALSE;
}
static boolean
is_location_form(int form)
{
    if (form == DW_FORM_data4 ||
        form == DW_FORM_data8 ||
        form == DW_FORM_sec_offset ||
        form == DW_FORM_loclistx ||
        form == DW_FORM_rnglistx ) {
        return TRUE;
    }
    return FALSE;
}

static void
show_attr_form_error(Dwarf_Debug dbg,unsigned attr,
    unsigned form,
    struct esb_s *out)
{
    const char *n = 0;
    int res = 0;
    Dwarf_Error formerr = 0;

    esb_append(out,"ERROR: Attribute ");
    esb_append_printf_u(out,"%u",attr);

    esb_append(out," (");
    res = dwarf_get_AT_name(attr,&n);
    if (res != DW_DLV_OK) {
        n = "UknownAttribute";
    }
    esb_append(out,n);
    esb_append(out,") ");
    esb_append(out," has form ");
    esb_append_printf_u(out,"%u",form);
    esb_append(out," (");
    esb_append(out,get_FORM_name(form,FALSE));
    esb_append(out,"), a form which is not appropriate");
    print_error_and_continue(dbg,
        esb_get_string(out), DW_DLV_OK,formerr);
}

/*  Traverse an attribute and following any reference
    in order to detect self references to DIES (loop).
    We do not use print_else_name_match here. Just looking
    for self references to report on. */
static int
traverse_attribute(Dwarf_Debug dbg, Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    Dwarf_Half attr,
    Dwarf_Attribute attr_in,
    UNUSEDARG boolean print_else_name_match,
    char **srcfiles, Dwarf_Signed cnt,
    int die_indent_level,
    Dwarf_Error *err)
{
    Dwarf_Attribute attrib = 0;
    const char * atname = 0;
    int tres = 0;
    Dwarf_Half tag = 0;
    struct esb_s valname;

    esb_constructor(&valname);
    is_info = dwarf_get_die_infotypes_flag(die);
    atname = get_AT_name(attr,pd_dwarf_names_print_on_error);

    /*  The following gets the real attribute, even in the face of an
        incorrect doubling, or worse, of attributes. */
    attrib = attr_in;
    /*  Do not get attr via dwarf_attr: if there are (erroneously)
        multiple of an attr in a DIE, dwarf_attr will not get the
        second, erroneous one and dwarfdump will print the first one
        multiple times. Oops. */

    tres = dwarf_tag(die, &tag, err);
    if (tres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "Cannot get DIE tag in traverse_attribute!",
            tres,*err);
        esb_destructor(&valname);
        return tres;
    } else if (tres == DW_DLV_NO_ENTRY) {
        tag = 0;
    } else {
        /* ok */
    }


    switch (attr) {
    case DW_AT_specification:
    case DW_AT_abstract_origin:
    case DW_AT_type: {
        int res = 0;
        Dwarf_Off die_goff = 0;
        Dwarf_Off ref_goff = 0;
        Dwarf_Die ref_die = 0;
        struct esb_s specificationstr;
        Dwarf_Half theform = 0;
        Dwarf_Half directform = 0;
        char buf[ESB_FIXED_ALLOC_SIZE];

        res = get_form_values(dbg,attrib,&theform,&directform,err);
        if (res != DW_DLV_OK) {
            esb_destructor(&valname);
            return res;
        }
        if (!form_refers_local_info(theform)) {
            break;
        }
        esb_constructor_fixed(&specificationstr,buf,sizeof(buf));
        ++die_indent_level;
        res = get_attr_value(dbg, tag, die, dieprint_cu_goffset,
            attrib, srcfiles, cnt,
            &specificationstr,glflags.show_form_used,glflags.verbose,
            err);
        if (res != DW_DLV_OK) {
            esb_destructor(&valname);
            return res;
        }
        esb_append(&valname, esb_get_string(&specificationstr));
        esb_destructor(&specificationstr);

        /* Get the global offset for reference */
        res = dwarf_global_formref(attrib, &ref_goff, err);
        if (res == DW_DLV_ERROR) {
            int dwerrno = dwarf_errno(*err);
            if (dwerrno == DW_DLE_REF_SIG8_NOT_HANDLED ) {
                /*  No need to stop, ref_sig8 refers out of
                    the current section. */
                DROP_ERROR_INSTANCE(dbg,res,*err);
                break;
            } else {
                print_error_and_continue(dbg,
                    "dwarf_global_formref fails in "
                    "attribute traversal",
                    res, *err);
                esb_destructor(&valname);
                return res;
            }
        } else if (res == DW_DLV_NO_ENTRY) {
            return res;
        }
        /* Gives die offset in section. */
        res = dwarf_dieoffset(die, &die_goff, err);
        if (res == DW_DLV_ERROR) {
            int dwerrno = dwarf_errno(*err);
            if (dwerrno == DW_DLE_REF_SIG8_NOT_HANDLED ) {
                /*  No need to stop, ref_sig8 refers out of
                    the current section. */
                DROP_ERROR_INSTANCE(dbg,res,*err);
                break;
            } else {
                print_error_and_continue(dbg,
                    "dwarf_dieoffset fails in "
                    " attribute traversal", res, *err);
                esb_destructor(&valname);
                return res;
            }
        }

        /* Follow reference chain, looking for self references */
        res = dwarf_offdie_b(dbg,ref_goff,is_info,&ref_die,err);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "dwarf_dieoff_b fails in "
                " attribute traversal", res, *err);
            esb_destructor(&valname);
            return res;
        }
        if (res == DW_DLV_OK) {
            Dwarf_Off target_die_cu_goff = 0;

            if (dump_visited_info) {
                Dwarf_Off die_loff = 0;

                res = dwarf_die_CU_offset(die, &die_loff, err);
                if (res != DW_DLV_OK) {
                    esb_destructor(&valname);
                    return res;
                }
                do_dump_visited_info(die_indent_level,
                    die_loff,die_goff,
                    dieprint_cu_goffset,
                    atname,esb_get_string(&valname));
            }
            ++die_indent_level;
            res =dwarf_CU_dieoffset_given_die(ref_die,
                &target_die_cu_goff, err);
            if (res != DW_DLV_OK) {
                print_error_and_continue(dbg,
                    "dwarf_dieoffset() accessing cu_goff die fails"
                    " in traversal",
                    res,*err);
                esb_destructor(&valname);
                return res;
            }
            res = traverse_one_die(dbg,attrib,ref_die,
                target_die_cu_goff,
                is_info,
                srcfiles,cnt,die_indent_level,
                err);
            DeleteKeyInBucketGroup(glflags.pVisitedInfo,ref_goff);
            dwarf_dealloc_die(ref_die);
            if (res == DW_DLV_ERROR) {
                esb_destructor(&valname);
                return res;
            }
            --die_indent_level;
            ref_die = 0;
        }
        }
        break;
    } /* End switch. */
    esb_destructor(&valname);
    return DW_DLV_OK;
}

/*  Traverse one DIE in order to detect
    self references to DIES.


    This fails to deal with changing CUs via global
    references so srcfiles and cnt
    have possibly inappropriate values. FIXME.
*/
static int
traverse_one_die(Dwarf_Debug dbg,
    Dwarf_Attribute attrib,
    Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Bool is_info,
    char **srcfiles, Dwarf_Signed cnt,
    int die_indent_level,
    Dwarf_Error *err)
{
    Dwarf_Half tag = 0;
    Dwarf_Off overall_offset = 0;
    Dwarf_Signed atcnt = 0;
    int res = 0;
    boolean print_else_name_match = FALSE;

    res = dwarf_tag(die, &tag, err);
    if (res != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "Problem accessing tag of die!"
            " from traverse_one_die", res, *err);
        return res;
    }
    res = dwarf_dieoffset(die, &overall_offset, err);
    if (res != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "dwarf_dieoffset fails in traversing die",
            res, *err);
        return res;
    }

    if (dump_visited_info) {
        Dwarf_Off offset = 0;
        const char * tagname = 0;
        res = dwarf_die_CU_offset(die, &offset, err);
        if (res != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "dwarf_die_CU_offset fails in traversing die",
                res, *err);
            return res;
        }
        tagname = get_TAG_name(tag,pd_dwarf_names_print_on_error);
        do_dump_visited_info(die_indent_level,offset,overall_offset,
            dieprint_cu_goffset,
            tagname,"");
    }

    DWARF_CHECK_COUNT(self_references_result,1);
    if (FindKeyInBucketGroup(glflags.pVisitedInfo,overall_offset)) {
        char * localvaln = NULL;
        Dwarf_Half attr = 0;
        struct esb_s bucketgroupstr;
        const char *atname = NULL;

        esb_constructor(&bucketgroupstr);
        res = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset,
            attrib, srcfiles,
            cnt, &bucketgroupstr,
            glflags.show_form_used, glflags.verbose,err);
        if (res != DW_DLV_OK) {
            return res;
        }
        localvaln = esb_get_string(&bucketgroupstr);

        res = dwarf_whatattr(attrib, &attr, err);
        if (res != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "ERROR: dwarf_whatattr fails in traverse die",
                res,*err);
            return res;
        }
        atname = get_AT_name(attr,pd_dwarf_names_print_on_error);

        /* We have a self reference */
        DWARF_CHECK_ERROR3(self_references_result,
            "Invalid self reference to DIE: ",atname,localvaln);
        esb_destructor(&bucketgroupstr);
    } else {
        Dwarf_Signed i = 0;
        Dwarf_Attribute *atlist = 0;

        /* Add current DIE */
        AddEntryIntoBucketGroup(glflags.pVisitedInfo,overall_offset,
            0,0,0,NULL,FALSE);

        res = dwarf_attrlist(die, &atlist, &atcnt, err);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "dwarf_attrlist fails in traverse die",
                res, *err);
        } else if (res == DW_DLV_NO_ENTRY) {
            /* indicates there are no attrs.
            It is not an error. */
            atcnt = 0;
        }

        for (i = 0; i < atcnt; i++) {
            Dwarf_Half attr;
            int ares;

            ares = dwarf_whatattr(atlist[i], &attr, err);
            if (ares == DW_DLV_OK) {
                ares = traverse_attribute(dbg, die,
                    dieprint_cu_goffset,
                    is_info,
                    attr,
                    atlist[i],
                    print_else_name_match, srcfiles, cnt,
                    die_indent_level,err);
                if (ares == DW_DLV_ERROR) {
                    dealloc_local_atlist(dbg,atlist,atcnt);
                    return ares;
                }
            } else {
                print_error_and_continue(dbg,
                    "dwarf_whatattr entry missing in "
                    " traverse die",
                    ares, *err);
                return ares;
            }
        }

        dealloc_local_atlist(dbg,atlist,atcnt);
        /* Delete current DIE */
        DeleteKeyInBucketGroup(glflags.pVisitedInfo,overall_offset);
    }
    return DW_DLV_OK;
}

/*  Extracted this from print_attribute()
    to get tolerable indents.
    In other words to make it readable.
    It uses global data fields excessively, but so does
    print_attribute().
    The majority of the code here is checking for
    compiler errors.
    Support for .debug_rnglists here is new May 2020.
    */
static int
print_range_attribute(Dwarf_Debug dbg,
   Dwarf_Die die,
   Dwarf_Half attr,
   Dwarf_Attribute attr_in,
   Dwarf_Half theform,
   int pra_dwarf_names_print_on_error,
   boolean print_else_name_match,
   int *append_extra_string,
   struct esb_s *esb_extrap,
   Dwarf_Error *raerr)
{
    Dwarf_Unsigned original_off = 0;
    int fres = 0;
    Dwarf_Half cu_version = 2;
    Dwarf_Half cu_offset_size = 4;

    fres = dwarf_get_version_of_die(die,&cu_version,&cu_offset_size);
    if (fres != DW_DLV_OK) {
        simple_err_return_msg_either_action(fres,
            "\nERROR: Unable to get version of a DIE "
            "to print a range attribute so something "
            " is badly wrong. Assuming DWARF2, offset size 4"
            "  and continuing!");
    }
    if (theform == DW_FORM_rnglistx) {
        fres = dwarf_formudata(attr_in, &original_off, raerr);
        if (fres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: In printing a range DW_FORM_rnglistx  attribute "
                "dwarf_formudata failed ",fres,*raerr);
            return fres;
        }
    } else {
        fres = dwarf_global_formref(attr_in, &original_off, raerr);
        if (fres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: In printing a range attribute "
                "dwarf_global_formref failed ",fres,*raerr);
            return fres;
        }
    }
    if (fres == DW_DLV_OK && cu_version < DWVERSION5) {
        Dwarf_Ranges *rangeset = 0;
        Dwarf_Signed rangecount = 0;
        Dwarf_Unsigned bytecount = 0;
        /*  If this is a dwp the ranges will be
            missing or reported from a tied file.
            For now we add the ranges to dbg, not tiedbg
            as we do not mention tieddbg here.
            May need a new interface. FIXME? */
        int rres = dwarf_get_ranges_a(dbg,original_off,
            die,
            &rangeset,
            &rangecount,&bytecount,raerr);
        if (rres == DW_DLV_OK) {
            /* Ignore ranges inside a stripped function  */
            if (!glflags.gf_suppress_checking_on_dwp &&
                glflags.gf_check_ranges &&
                in_valid_code && checking_this_compiler()) {
                /*  Record the offset, as the ranges check
                    will be done at
                    the end of the compilation unit;
                    this approach solves
                    the issue of DWARF4 generating values
                    for the high pc
                    as offsets relative to the low pc
                    and the compilation
                    unit having DW_AT_ranges attribute. */
                int dores = 0;
                Dwarf_Off die_glb_offset = 0;
                Dwarf_Off die_off = 0;
                dores = dwarf_die_offsets(die,&die_glb_offset,
                    &die_off,raerr);
                if (dores == DW_DLV_ERROR) {
                    return dores;
                }
                if (dores == DW_DLV_OK) {
                    record_range_array_info_entry(die_glb_offset,
                        original_off);
                }
            }
            if (print_else_name_match) {
                *append_extra_string = 1;
                print_ranges_list_to_extra(dbg,
                    original_off,
                    rangeset,rangecount,bytecount,
                    esb_extrap);
            }
            dwarf_ranges_dealloc(dbg,rangeset,rangecount);
        } else if (rres == DW_DLV_ERROR) {
            if ( glflags.gf_suppress_checking_on_dwp) {
                /* Ignore checks */
            } else if ( glflags.gf_do_print_dwarf) {
                printf("\ndwarf_get_ranges() "
                    "cannot find DW_AT_ranges at offset 0x%"
                    DW_PR_XZEROS DW_PR_DUx
                    " (0x%" DW_PR_XZEROS DW_PR_DUx ").",
                    original_off,
                    original_off);
            } else {
                DWARF_CHECK_COUNT(ranges_result,1);
                DWARF_CHECK_ERROR2(ranges_result,
                    get_AT_name(attr,
                        pra_dwarf_names_print_on_error),
                    " cannot find DW_AT_ranges at offset");
            }
            return rres;
        } else {
            /* NO ENTRY */
            if ( glflags.gf_suppress_checking_on_dwp) {
                /* Ignore checks */
            } else if ( glflags.gf_do_print_dwarf) {
                printf("\ndwarf_get_ranges() "
                    "finds no DW_AT_ranges at offset 0x%"
                    DW_PR_XZEROS DW_PR_DUx
                    " (%" DW_PR_DUu ").",
                    original_off,
                    original_off);
            } else {
                DWARF_CHECK_COUNT(ranges_result,1);
                DWARF_CHECK_ERROR2(ranges_result,
                    get_AT_name(attr,
                        pra_dwarf_names_print_on_error),
                    " fails to find DW_AT_ranges at offset");
            }
        }
        return DW_DLV_OK;
    } else if (fres == DW_DLV_OK && cu_version >= DWVERSION5) {
        /*  Here we have to access the .debug_rnglists section
            data with a new layout for DW5.  Here we
            do not need to actually use rleoffset since it
            is identical to original_off.  */
        int res = 0;
        Dwarf_Unsigned rleoffset = 0;

        res = handle_rnglists(die,
            attr_in,
            theform,
            original_off,
            &rleoffset,
            esb_extrap,
            glflags.show_form_used,
            glflags.verbose,
            raerr);
        if (print_else_name_match) {
            *append_extra_string = 1;
        }
        return res;
    }
    /*  DW_DLV_NO_ENTRY or DW_DLV_ERROR */
    if (glflags.gf_do_print_dwarf) {
        struct esb_s local;
        char tmp[ESB_FIXED_ALLOC_SIZE];

        esb_constructor_fixed(&local,tmp,sizeof(tmp));
        esb_append(&local,
            " fails to find DW_AT_ranges offset");
        esb_append_printf_u(&local," attr 0x%x",attr);
        esb_append_printf_u(&local," form 0x%x",theform);
        printf(" %s ",esb_get_string(&local));
        esb_destructor(&local);
    } else {
        DWARF_CHECK_COUNT(ranges_result,1);
        DWARF_CHECK_ERROR2(ranges_result,
            get_AT_name(attr,
            pra_dwarf_names_print_on_error),
            " fails to find DW_AT_ranges offset");
    }
    return fres;
}

/*  A DW_AT_name in a CU DIE will likely have dots
    and be entirely sensible. So lets
    not call things a possible error when they are not.
    Some assemblers allow '.' in an identifier too.

    This is a heuristic, not all that reliable.
    It is only used for a specific DWARF_CHECK_ERROR,
    and the 'altabi.' is from a specific
    (unnamed here) compiler.

    Return 0 (FALSE) if it is a vaguely standard identifier.
    Else return 1 (TRUE), meaning 'it might be a file name
    or have  '.' in it quite sensibly.'

    If we don't do the TAG check we might report "t.c"
    as a questionable DW_AT_name. Which would be silly.
*/
static boolean
dot_ok_in_identifier(int tag,
    const char *val)
{
    if (strncmp(val,"altabi.",7)) {
        /*  Ignore the names of the form 'altabi.name',
            which apply to one specific compiler.  */
        return TRUE;
    }
    if (tag == DW_TAG_compile_unit ||
        tag == DW_TAG_partial_unit ||
        tag == DW_TAG_imported_unit ||
        tag == DW_TAG_skeleton_unit ||
        tag == DW_TAG_type_unit) {
        return TRUE;
    }
    return FALSE;
}

static void
trim_quotes(const char *val,struct esb_s *es)
{
    if (val[0] == '"') {
        size_t l = strlen(val);
        if (l > 2 && val[l-1] == '"') {
            esb_appendn(es,val+1,l-2);
            return;
        }
    }
    esb_append(es,val);
}

static boolean
have_a_search_match(const char *valname,const char *atname)
{
    /*  valname may have had quotes inserted, but search_match_text
        will not. So we need to use a new copy, not valname here.
        */
    char matchbuf[100]; /* not ESB_FIXED_ALLOC_SIZE ? */
    struct esb_s esb_match;
    char *s2;

    esb_constructor_fixed(&esb_match,matchbuf,sizeof(matchbuf));
    trim_quotes(valname,&esb_match);
    s2 = esb_get_string(&esb_match);
    if (glflags.search_match_text ) {
        if (!strcmp(s2,glflags.search_match_text) ||
            !strcmp(atname,glflags.search_match_text)) {

            esb_destructor(&esb_match);
            return TRUE;
        }
    }
    if (glflags.search_any_text) {
        if (is_strstrnocase(s2,glflags.search_any_text) ||
            is_strstrnocase(atname,glflags.search_any_text)) {

            esb_destructor(&esb_match);
            return TRUE;
        }
    }
#ifdef HAVE_REGEX
    if (glflags.search_regex_text) {
        if (!regexec(glflags.search_re,s2,0,NULL,0) ||
            !regexec(glflags.search_re,atname,0,NULL,0)) {

            esb_destructor(&esb_match);
            return TRUE;
        }
    }
#endif
    esb_destructor(&esb_match);
    return FALSE;
}


/*  Use our local die_stack to try to determine
    signedness of the DW_AT_discr_list
    LEB numbers.   Returns -1 if we know
    it is signed.  Returns 1 if we know it is
    unsigned.  Returns 0 if we really do not know. */
static int
determine_discr_signedness(Dwarf_Debug dbg)
{
    Dwarf_Die parent = 0;
    Dwarf_Half tag = 0;
    int tres = 0;
    Dwarf_Error descrerr = 0;

    if (die_stack_indent_level < 1) {
        /*  We have no idea. */
        return 0;
    }
    parent = die_stack[die_stack_indent_level -1].die_;
    if (!parent) {
        /*  We have no idea. */
        return 0;
    }
    tres = dwarf_tag(parent, &tag, &descrerr);
    if (tres != DW_DLV_OK) {
        DROP_ERROR_INSTANCE(dbg,tres,descrerr);
        return 0;
    }
    if (tag != DW_TAG_variant_part) {
        return 0;
    }
    /*  Expect DW_AT_discr or DW_AT_type here, and if
        DW_AT_discr, that might have the DW_AT_type. */

    /*   FIXME: Initially lets just punt, say unsigned. */
    return 1;
}

static void
checksignv(
   struct esb_s *strout,
   const char *title,
   Dwarf_Signed sv,
   Dwarf_Unsigned uv)
{
    /*  The test and output are not entirely meaningful, but
        it can be useful for readers of dwarfdump output. */
    if (uv == (Dwarf_Unsigned)sv) {
        /* Nothing to do here. */
        return;
    }
    esb_append(strout," <");
    esb_append(strout,title);
    esb_append(strout," ");
    esb_append_printf_i(strout,"%" DW_PR_DSd ":",sv);
    esb_append_printf_u(strout,"%" DW_PR_DUu ">",uv);
}

static int
append_discr_array_vals(Dwarf_Debug dbg,
    Dwarf_Dsc_Head h,
    Dwarf_Unsigned arraycount,
    int isunsigned,
    struct esb_s *strout,
    Dwarf_Error*paerr)
{
    Dwarf_Unsigned u = 0;
    if (isunsigned == 0) {
        esb_append(strout,
            "<discriminant list signedness unknown>");
    }
    esb_append_printf_u(strout,
        "\n        discr list array len: "
        "%" DW_PR_DUu "\n",arraycount);
    for(u = 0; u < arraycount; u++) {
        int u2res = 0;
        Dwarf_Half dtype = 0;
        Dwarf_Signed slow = 0;
        Dwarf_Signed shigh = 0;
        Dwarf_Unsigned ulow = 0;
        Dwarf_Unsigned uhigh = 0;
        const char *dsc_name = "";

        u2res = dwarf_discr_entry_u(h,u,
            &dtype,&ulow,&uhigh,paerr);
        if (u2res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "DW_AT_discr_list entry access fail\n",
                u2res, *paerr);
            return u2res;
        }
        u2res = dwarf_discr_entry_s(h,u,
            &dtype,&slow,&shigh,paerr);
        if (u2res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "DW_AT_discr_list entry access fail\n",
                u2res, *paerr);
        }
        if (u2res == DW_DLV_NO_ENTRY) {
            glflags.gf_count_major_errors++;
            esb_append_printf_u(strout,
                "\n          "
                "ERROR: discr index missing! %"  DW_PR_DUu,u);
            break;
        }
        esb_append_printf_u(strout,
            "        "
            "%" DW_PR_DUu ": ",u);
        dsc_name = get_DSC_name(dtype,pd_dwarf_names_print_on_error);
        esb_append(strout,sanitized(dsc_name));
        esb_append(strout," ");
        if (!dtype) {
            if (isunsigned < 0) {
                esb_append_printf_i(strout,"%" DW_PR_DSd,slow);
                checksignv(strout,"as signed:unsigned",slow,ulow);
            } else {
                esb_append_printf_u(strout,"%" DW_PR_DUu,ulow);
                checksignv(strout,"as signed:unsigned",slow,ulow);
            }
        } else {
            if (isunsigned < 0) {
                esb_append_printf_i(strout,"%" DW_PR_DSd,slow);
                checksignv(strout,"as signed:unsigned",slow,ulow);
            } else {
                esb_append_printf_u(strout,"%" DW_PR_DUu,ulow);
                checksignv(strout,"as signed:unsigned",slow,ulow);
            }
            if (isunsigned < 0) {
                esb_append_printf_i(strout,", %" DW_PR_DSd,shigh);
                checksignv(strout,"as signed:unsigned",shigh,uhigh);
            } else {
                esb_append_printf_u(strout,", %" DW_PR_DUu,uhigh);
                checksignv(strout,"as signed:unsigned",shigh,uhigh);
            }
        }
        esb_append(strout,"\n");
    }
    return DW_DLV_OK;
}

/*  Only two types of CU can have highpc or lowpc. */
static boolean
tag_type_is_addressable_cu(int tag)
{
    if (tag == DW_TAG_compile_unit) {
        return TRUE;
    }
    if (tag == DW_TAG_partial_unit) {
        return TRUE;
    }
    return FALSE;
}

static int
print_location_description(Dwarf_Debug dbg,
    Dwarf_Attribute attrib,
    Dwarf_Die die,
    UNUSEDARG int checking,
    Dwarf_Half attr,
    UNUSEDARG int die_indent_level,
    struct esb_s *base,
    struct esb_s *details,
    Dwarf_Error *err)
{
    /*  The value is a location description
        or location list. */
    int res = 0;
    Dwarf_Half theform = 0;
    Dwarf_Half directform = 0;
    Dwarf_Half version = 0;
    Dwarf_Half offset_size = 0;
    res = get_form_values(dbg,attrib,&theform,&directform,
        err);
    if (res == DW_DLV_ERROR) {
        return res;
    }
    res = dwarf_get_version_of_die(die,
        &version,&offset_size);
    if (is_simple_location_expr(theform)) {
        res  = print_location_list(dbg, die, attrib,
            checking,
            TRUE,base,err);
        if (res == DW_DLV_ERROR) {
            return res;
        }
        show_form_itself(glflags.show_form_used, glflags.verbose,
            theform, directform, base);
    } else if (is_location_form(theform)) {
        res  = print_location_list(dbg, die, attrib,
            checking,
            FALSE,
            details,err);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: Cannot get location list"
                " data",
                res, *err);
            return res;
        }
    } else if (theform == DW_FORM_exprloc) {
        /* printed via the form, nothing to do here. */
    } else {
        show_attr_form_error(dbg,attr,theform,base);
    }
    return DW_DLV_OK;
}

/* This was inside print_attribute() */
static void
check_attr_tag_combination(Dwarf_Half tag,Dwarf_Half attr)
{
    const char *tagname = "<tag invalid>";

    DWARF_CHECK_COUNT(attr_tag_result,1);
    if (legal_tag_attr_combination(tag, attr)) {
        /* OK */
    } else {
        /* Report errors only if tag-attr check is on */
        if (glflags.gf_check_attr_tag) {
            tagname = get_TAG_name(tag,
                pd_dwarf_names_print_on_error);
            tag_specific_checks_setup(tag,die_stack_indent_level);
            DWARF_CHECK_ERROR3(attr_tag_result,tagname,
                get_AT_name(attr,
                pd_dwarf_names_print_on_error),
                "check the tag-attr combination");
        }
    }
}

/*  This function needs a rewrite for completeness and
    clarity.  FIXME */
static int
print_hipc_lopc_attribute(Dwarf_Debug dbg,
    Dwarf_Half tag,
    Dwarf_Die die,
    Dwarf_Unsigned dieprint_cu_goffset,
    char ** srcfiles,
    Dwarf_Signed cnt,
    Dwarf_Attribute attrib,
    Dwarf_Half attr,
    Dwarf_Unsigned max_address,
    Dwarf_Bool *bSawLowp,
    Dwarf_Addr *lowAddrp,
    Dwarf_Bool *bSawHighp,
    Dwarf_Addr *highAddrp,
    struct esb_s *valname,
    Dwarf_Error *err)
{
    Dwarf_Half theform =0;
    int rv = 0;
    /* For DWARF4, the high_pc offset from the low_pc */
    Dwarf_Unsigned highpcOff = 0;
    Dwarf_Bool offsetDetected = FALSE;
    char highpcstrbuf[ESB_FIXED_ALLOC_SIZE];
    struct esb_s highpcstr;

    esb_constructor_fixed(&highpcstr,highpcstrbuf,
        sizeof(highpcstrbuf));
    rv = dwarf_whatform(attrib,&theform,err);
    /*  Depending on the form and the attribute,
        process the form. */
    if (rv == DW_DLV_ERROR) {
        print_error_and_continue(dbg, "in print_attribute "
            "dwarf_whatform cannot"
            " Find attr form",
            rv, *err);
        return rv;
    } else if (rv == DW_DLV_NO_ENTRY) {
        return rv;
    }
    if (theform != DW_FORM_addr &&
        !dwarf_addr_form_is_indexed(theform)) {
        /*  New in DWARF4: other forms
            (of class constant) are not an address
            but are instead offset from pc.
            One could test for DWARF4 here
            before adding this string, but that
            seems unnecessary as this
            could not happen with DWARF3 or earlier.
            A normal consumer would have to
            add this value to
            DW_AT_low_pc to get a true pc. */
        esb_append(&highpcstr,"<offset-from-lowpc>");
        /*  Update the high_pc value if we
            are checking the ranges */
        if ( glflags.gf_check_ranges && attr == DW_AT_high_pc) {
            /* Get the offset value */
            int show_form_here = 0;
            int ares = get_small_encoding_integer_and_name(dbg,
                attrib,
                &highpcOff,
                /* attrname */ (const char *) NULL,
                /* err_string */ ( struct esb_s *) NULL,
                (encoding_type_func) 0,
                err,show_form_here);
            if (ares != DW_DLV_OK) {
                if (ares == DW_DLV_NO_ENTRY) {
                    print_error_and_continue(dbg,
                        "get_small_encoding_integer_and_name"
                        " No Entry for DW_AT_high_pc/DW_AT_low_pc",
                        ares, *err);
                } else {
                    print_error_and_continue(dbg,
                        "get_small_encoding_integer_and_name"
                        " Failed for DW_AT_high_pc/DW_AT_low_pc",
                        ares, *err);
                }
                return ares;
            }
            offsetDetected = TRUE;
        }
    }
    rv = get_attr_value(dbg, tag, die,
        dieprint_cu_goffset,
        attrib, srcfiles, cnt,
        &highpcstr,glflags.show_form_used,
        glflags.verbose,err);
    if (rv == DW_DLV_ERROR) {
        return rv;
    }
    esb_empty_string(valname);
    esb_append(valname, esb_get_string(&highpcstr));
    esb_destructor(&highpcstr);

    /* Update base and high addresses for CU */
    if (glflags.seen_CU &&
        (glflags.need_CU_base_address
        || glflags.need_CU_high_address)) {
        /* Update base address for CU */
        if (attr == DW_AT_low_pc) {
            if (glflags.need_CU_base_address &&
                tag_type_is_addressable_cu(tag)) {
                int lres = dwarf_formaddr(attrib,
                    &glflags.CU_base_address, err);
                DROP_ERROR_INSTANCE(dbg,lres,*err);
                if (lres == DW_DLV_OK) {
                    glflags.need_CU_base_address = FALSE;
                    glflags.CU_low_address =
                        glflags.CU_base_address;
                }
            } else if (!glflags.CU_low_address) {
                /*  We take the first non-zero address
                    as meaningful. Even if no such in CU DIE. */
                int fres = dwarf_formaddr(attrib,
                    &glflags.CU_low_address, err);
                DROP_ERROR_INSTANCE(dbg,fres,*err);
                if (fres == DW_DLV_OK) {
                    /*  Stop looking for base. Bogus, but
                        there is none available, so stop. */
                    glflags.need_CU_base_address = FALSE;
                }
            }
        }

        /* Update high address for CU */
        if (attr == DW_AT_high_pc) {
            if (glflags.need_CU_high_address ) {
                /*  This is bogus in that it accepts the first
                    high address in the CU, from any TAG */
                if (theform != DW_FORM_addr &&
                    !dwarf_addr_form_is_indexed(theform)) {
                    /*  New in DWARF4: other forms
                    (of class constant) are not an address
                    but are instead offset from pc. */
                    Dwarf_Unsigned hpcoff = 0;
                    int show_form_here = 0;

                    int ares = get_small_encoding_integer_and_name(
                        dbg,
                        attrib,
                        &hpcoff,
                        /* attrname */ (const char *) NULL,
                        /* err_string */ ( struct esb_s *) NULL,
                        (encoding_type_func) 0,
                        err,show_form_here);
                    if (ares == DW_DLV_OK) {
                        if (*bSawLowp) {
                            glflags.CU_high_address =
                                *lowAddrp + hpcoff;
                        }
                    }
                } else {
                    int ares = dwarf_formaddr(attrib,
                        &glflags.CU_high_address, err);
                    DROP_ERROR_INSTANCE(dbg,ares,*err);
                    if (ares == DW_DLV_OK) {
                        glflags.need_CU_high_address = FALSE;
                    }
                }
            }
        }
    }

    /* Record the low and high addresses as we have them */
    /* For DWARF4 allow the high_pc value as an offset */
    if ((glflags.gf_check_decl_file ||
        glflags.gf_check_ranges ||
        glflags.gf_check_locations) &&
            (theform == DW_FORM_addr ||
            dwarf_addr_form_is_indexed(theform) ||
            offsetDetected)) {

        int cres = 0;
        Dwarf_Addr addr = 0;
        /* Calculate the real high_pc value */
        if (offsetDetected && glflags.seen_PU_base_address) {
            addr = *lowAddrp + highpcOff;
            cres = DW_DLV_OK;
        } else {
            if (theform == DW_FORM_addr ||
                dwarf_addr_form_is_indexed(theform)) {
                cres = dwarf_formaddr(attrib, &addr, err);
            } else {
                /* Bogus. FIXME */
                cres = DW_DLV_NO_ENTRY;
            }
        }
        if(cres == DW_DLV_OK) {
            if (attr == DW_AT_low_pc) {
                *lowAddrp = addr;
                *bSawLowp = TRUE;
                /*  Record the base address of the last seen PU
                    to be used when checking line information */
                if (glflags.seen_PU &&
                    !glflags.seen_PU_base_address) {
                    glflags.seen_PU_base_address = TRUE;
                    glflags.PU_base_address = addr;
                }
            } else { /* DW_AT_high_pc */
                *highAddrp = addr;
                *bSawHighp = TRUE;
                /*  Record the high address of the last seen PU
                    to be used when checking line information */
                if (glflags.seen_PU &&
                    !glflags.seen_PU_high_address) {
                    glflags.seen_PU_high_address = TRUE;
                    glflags.PU_high_address = addr;
                }
            }
        } else  if (cres == DW_DLV_ERROR) {
            int msgnum = dwarf_errno(*err);

            if (msgnum == DW_DLE_MISSING_NEEDED_DEBUG_ADDR_SECTION) {
                print_error_and_continue(dbg,
                    "Some checks cannot be done because "
                    "the .debug_addr section is not present",
                    cres,*err);
                DROP_ERROR_INSTANCE(dbg,cres,*err);
                return DW_DLV_OK;
            }
            return cres;
        }

        /* We have now both low_pc and high_pc values */
        if (*bSawLowp && *bSawHighp) {
            /*  We need to decide if this PU is
                valid, as the SN Linker marks a stripped
                function by setting lowpc to -1;
                also for discarded comdat, both lowpc
                and highpc are zero */
            if (glflags.need_PU_valid_code) {
                glflags.need_PU_valid_code = FALSE;
                /*  To ignore a PU as invalid code,
                    only consider the lowpc and
                    highpc values associated with the
                    DW_TAG_subprogram; other
                    instances of lowpc and highpc,
                    must be ignore (lexical blocks) */
                in_valid_code = TRUE;
                if (IsInvalidCode(*lowAddrp,*highAddrp) &&
                    tag == DW_TAG_subprogram) {
                    in_valid_code = FALSE;
                }
            }
            /*  We have a low_pc/high_pc pair;
                check if they are valid */
            if (in_valid_code) {
                DWARF_CHECK_COUNT(ranges_result,1);
                if (*lowAddrp != max_address &&
                    *lowAddrp > *highAddrp) {
                    DWARF_CHECK_ERROR(ranges_result,
                        ".debug_info: Incorrect values "
                        "for low_pc/high_pc");
                    if (glflags.gf_check_verbose_mode &&
                        PRINTING_UNIQUE) {
                        printf("Low = 0x%" DW_PR_XZEROS DW_PR_DUx
                            ", High = 0x%"
                            DW_PR_XZEROS DW_PR_DUx "\n",
                            *lowAddrp,*highAddrp);
                    }
                }
                if (glflags.gf_check_decl_file ||
                    glflags.gf_check_ranges ||
                    glflags.gf_check_locations) {
                    AddEntryIntoBucketGroup(glflags.pRangesInfo,0,
                        *lowAddrp,
                        *lowAddrp,*highAddrp,NULL,FALSE);
                }
            }
            *bSawLowp = FALSE;
            *bSawHighp = FALSE;
        }
    }
    return DW_DLV_OK;
}

static int
print_attribute(Dwarf_Debug dbg, Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Half attr,
    Dwarf_Attribute attr_in,
    boolean print_else_name_match,
    int die_indent_level,
    char **srcfiles, Dwarf_Signed cnt,
    boolean *attr_duplication,
    Dwarf_Error *err)
{
    Dwarf_Attribute attrib = 0;
    Dwarf_Unsigned  uval = 0;
    const char *    atname = 0;
    int             tres = 0;
    Dwarf_Half      tag = 0;
    int             append_extra_string = 0;
    boolean         found_search_attr = FALSE;
    boolean         bTextFound = FALSE;
    Dwarf_Bool      is_info = FALSE;
    Dwarf_Addr      max_address = 0;
    struct esb_s    valname;
    struct esb_s    esb_extra;
    char            valbuf[ESB_FIXED_ALLOC_SIZE*3];
    char            xtrabuf[ESB_FIXED_ALLOC_SIZE*3];
    int             res = 0;
    boolean         checking = glflags.gf_do_check_dwarf;

    esb_constructor_fixed(&esb_extra,xtrabuf,sizeof(xtrabuf));
    esb_constructor_fixed(&valname,valbuf,sizeof(valbuf));
    is_info = dwarf_get_die_infotypes_flag(die);
    atname = get_AT_name(attr,pd_dwarf_names_print_on_error);
    res = get_address_size_and_max(dbg,0,&max_address,err);
    if (res != DW_DLV_OK) {
        print_error_and_continue(dbg,
            "Getting address maximum"
            " failed in printing attribute ",res,*err);
        return res;
    }

    /*  The following gets the real attribute, even
        in the face of an
        incorrect doubling, or worse, of attributes. */
    attrib = attr_in;
    /*  Do not get attr via dwarf_attr:
        if there are (erroneously)
        multiple of an attr in a DIE,
        dwarf_attr will not get the
        second, erroneous one and dwarfdump will
        print the first one
        multiple times. Oops. */
    tres = dwarf_tag(die, &tag, err);
    if (tres != DW_DLV_OK) {
        print_error_and_continue(dbg, "Getting DIE tag "
            " failed in printing an attribute.",
            tres,*err);
        esb_destructor(&valname);
        esb_destructor(&esb_extra);
        return tres;
    }
    if ((glflags.gf_check_attr_tag ||
        glflags.gf_print_usage_tag_attr)&&
        checking_this_compiler()) {
        check_attr_tag_combination(tag,attr);
    }

    switch (attr) {
    case DW_AT_language:
        res = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_language", &valname,
            get_LANG_name, err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot get DW_AT_language value. ",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_accessibility:
        res  = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_accessibility",
            &valname, get_ACCESS_name,
            err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot get DW_AT_accessibility value",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_visibility:
        res = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_visibility",
            &valname, get_VIS_name,
            err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot get DW_AT_visibility value.",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_virtuality:
        res = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_virtuality",
            &valname,
            get_VIRTUALITY_name, err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot get DW_AT_virtuality",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_identifier_case:
        res = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_identifier",
            &valname, get_ID_name,
            err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot get DW_AT_identifier_case",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_inline:
        res = get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_inline", &valname,
            get_INL_name, err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,"Cannot get DW_AT_inline",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_encoding:
        res =get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_encoding", &valname,
            get_ATE_name, err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR:Cannot get DW_AT_encoding",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_ordering:
        res =get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_ordering", &valname,
            get_ORD_name, err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR:Cannot get DW_AT_ordering",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_calling_convention:
        res =get_small_encoding_integer_and_name(dbg, attrib,
            &uval,
            "DW_AT_calling_convention",
            &valname, get_CC_name,
            err,
            glflags.show_form_used);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR:Cannot get DW_AT_calling_convention",
                res,*err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_discr_list: {      /* DWARF2 */
        /*  This has one of the block forms.
            It should be in a DW_TAG_variant.
            Up to September 2016 it was treated as integer or name
            here, which was quite wrong. */
        enum Dwarf_Form_Class fc = DW_FORM_CLASS_UNKNOWN;
        Dwarf_Half theform = 0;
        Dwarf_Half directform = 0;
        Dwarf_Half version = 0;
        Dwarf_Half offset_size = 0;
        int wres = 0;

        wres = get_form_values(dbg,attrib,&theform,&directform,
            err);
        if (wres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: Cannot get DW_AT_discr_list form values",
                wres, *err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return wres;
        }
        wres = dwarf_get_version_of_die(die,&version,&offset_size);
        if (wres != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "ERROR: Cannot get DIE context version number"
                " for DW_AT_discr_list",
                DW_DLV_OK,0);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            break;
        }
        fc = dwarf_get_form_class(version,attr,offset_size,theform);
        if (fc == DW_FORM_CLASS_BLOCK) {
            int fres = 0;
            Dwarf_Block *tempb = 0;
            /*  the block is a series of entries each of one
                of these formats:
                DW_DSC_label  caselabel
                DW_DSC_range  lowvalue highvalue

                The values are all LEB. Signed or unsigned
                depending on the DW_TAG_variant_part owning
                the DW_TAG_variant.  The DW_TAG_variant_part
                will have a DW_AT_type or a DW_AT_discr
                and that attribute will reveal the signedness of all
                the leb values.
                As a practical matter DW_DSC_label/DW_DSC_range
                value (zero or one, so far)
                can safely be read as ULEB or SLEB
                and one gets a valid value whereas
                the caselabel, lowvalue,highvalue must be
                decoded with the proper sign. the high level
                (dwarfdump in this case) is the agent that
                should determine the proper signedness.  */

            fres = dwarf_formblock(attrib, &tempb,err);
            if (fres == DW_DLV_OK) {
                struct esb_s bformstr;
                int isunsigned = 0; /* Meaning unknown */
                Dwarf_Dsc_Head h = 0;
                Dwarf_Unsigned arraycount = 0;
                int sres = 0;
                char fbuf[ESB_FIXED_ALLOC_SIZE];

                esb_constructor_fixed(&bformstr,fbuf,
                    sizeof(fbuf));
                show_form_itself(glflags.show_form_used,
                    glflags.verbose,
                    theform, directform,&bformstr);
                isunsigned = determine_discr_signedness(dbg);
                esb_empty_string(&valname);

                sres = dwarf_discr_list(dbg,
                    (Dwarf_Small *)tempb->bl_data,
                    tempb->bl_len,
                    &h,&arraycount,err);
                if (sres == DW_DLV_NO_ENTRY) {
                    esb_append(&bformstr,
                        "<empty discriminant list>");
                    break;
                }
                if (sres == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: DW_AT_discr_list access fail",
                        sres, *err);
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return sres;
                }
                sres = append_discr_array_vals(dbg,h,arraycount,
                    isunsigned,&bformstr,err);
                if (sres == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: getting discriminant values "
                        "failed",
                        sres, *err);
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return sres;
                }

                if (glflags.verbose > 1) {
                    unsigned u = 0;
                    esb_append_printf_u(&bformstr,
                        "\n        block byte len:"
                        "0x%" DW_PR_XZEROS DW_PR_DUx
                        "\n        ",tempb->bl_len);
                    for (u = 0; u < tempb->bl_len; u++) {
                        esb_append_printf_u(&bformstr, "%02x ",
                            *(u + (unsigned char *)tempb->bl_data));
                    }
                }
                esb_append(&valname, esb_get_string(&bformstr));
                dwarf_dealloc(dbg,h,DW_DLA_DSC_HEAD);
                dwarf_dealloc(dbg, tempb, DW_DLA_BLOCK);
                esb_destructor(&bformstr);
                tempb = 0;
            } else {
                print_error_and_continue(dbg,
                    "ERROR: DW_AT_discr_list: cannot get list"
                    "data", fres, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return fres;
            }
        } else {
            print_error_and_continue(dbg,
                "DW_AT_discr_list is not form class BLOCK",
                fc, *err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return fc;
        }
        }
        break;
    case DW_AT_data_member_location:
        {
            /*  Value is a constant or a location
                description or location list.
                If a constant, it could be signed or
                unsigned.  Telling whether a constant
                or a reference is nontrivial
                since DW_FORM_data{4,8}
                could be either in DWARF{2,3}  */
            enum Dwarf_Form_Class fc = DW_FORM_CLASS_UNKNOWN;
            Dwarf_Half theform = 0;
            Dwarf_Half directform = 0;
            Dwarf_Half version = 0;
            Dwarf_Half offset_size = 0;
            int wres = 0;

            wres = get_form_values(dbg,attrib,&theform,&directform,
                err);
            if (wres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "ERROR: Cannot get DW_AT_data_member_location"
                    " form values",
                    wres, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return wres;
            }
            wres = dwarf_get_version_of_die(die,&version,
                &offset_size);
            if (wres != DW_DLV_OK) {
                simple_err_return_action(wres,
                    "\nERROR: Cannot get DIE context version number"
                    " via dwarf_get_version_of_die() "
                    "for DW_AT_data_member_location "
                    "which suggests something is badly"
                    " wrong.\n");
            }
            fc = dwarf_get_form_class(version,attr,
                offset_size,theform);
            if (fc == DW_FORM_CLASS_CONSTANT) {
                struct esb_s classconstantstr;
                esb_constructor(&classconstantstr);
                /*  Makes no sense to look at type of our DIE
                    to determine how to print the constant. */
                wres = formxdata_print_value(dbg,NULL,attrib,
                    theform,
                    &classconstantstr,
                    err, FALSE);
                if (wres != DW_DLV_OK) {
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return wres;
                }
                show_form_itself(glflags.show_form_used,
                    glflags.verbose,
                    theform, directform, &classconstantstr);
                esb_empty_string(&valname);
                esb_append(&valname, esb_get_string(&classconstantstr));
                esb_destructor(&classconstantstr);

                if (wres == DW_DLV_OK){
                    /* String appended already. */
                    break;
                } else if (wres == DW_DLV_NO_ENTRY) {
                    simple_err_return_action(wres,
                        "\nERROR: Cannot format"
                        " DW_AT_data_member_location, "
                        "which suggests something is badly wrong.\n");
                    break;
                }
            }
            /*  FALL THRU, this is a
                a location description, or a reference
                to one, or a mistake. */
        }
        append_extra_string = TRUE;
        res = print_location_description(dbg,attrib,die,
            checking, attr,die_indent_level,
            &valname,&esb_extra,err);
        if (res == DW_DLV_ERROR) {
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        break;
    case DW_AT_location:
    case DW_AT_vtable_elem_location:
    case DW_AT_string_length:
    case DW_AT_return_addr:
    case DW_AT_use_location:
    case DW_AT_static_link:
    case DW_AT_frame_base: {
        Dwarf_Half theform = 0;
        Dwarf_Half directform = 0;
        boolean showform = glflags.show_form_used;

        append_extra_string = TRUE;
        res = get_form_values(dbg,attrib,&theform,&directform,
            err);
        if (res == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: Cannot get location form",
                res, *err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        /*  If DW_FORM_block* && show_form_used
            get_attr_value() results
            in duplicating the form name (with -M). */
        if (is_simple_location_expr(theform)) {
            showform = FALSE;
        }
        res = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset,
            attrib, srcfiles, cnt, &valname,
            showform,
            glflags.verbose,
            err);
        if (res == DW_DLV_ERROR) {
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        res = print_location_description(dbg,attrib,die,
            checking,
            attr,die_indent_level,
            &valname,&esb_extra,err);
        if (res == DW_DLV_ERROR) {
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return res;
        }
        }
        break;
    case DW_AT_SUN_func_offsets:
        {
            /* value is a location description or location list */
            Dwarf_Half theform = 0;
            Dwarf_Half directform = 0;
            char buf[100];
            struct esb_s funcformstr;
            int wres = 0;

            esb_constructor_fixed(&funcformstr,buf,sizeof(buf));
            wres = get_form_values(dbg,attrib,&theform,&directform,
                err);
            if (wres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "ERROR: Cannot get DW_AT_SUN_func_offsets"
                    " form values",
                    wres, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return wres;
            }

            get_FLAG_BLOCK_string(dbg, attrib,&funcformstr);
            show_form_itself(glflags.show_form_used,
                glflags.verbose, theform,
                directform,&funcformstr);
            esb_empty_string(&valname);
            esb_append(&valname, esb_get_string(&funcformstr));
            esb_destructor(&funcformstr);
        }
        break;
    case DW_AT_SUN_cf_kind:
        {
            Dwarf_Half kind = 0;
            Dwarf_Unsigned tempud = 0;
            int wres = 0;
            Dwarf_Half theform = 0;
            Dwarf_Half directform = 0;
            struct esb_s cfkindstr;

            wres = get_form_values(dbg,attrib,
                &theform,&directform,
                err);
            if (wres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "ERROR: Cannot get DW_AT_SUr_cf_kind form values",
                    wres, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return wres;
            }

            esb_constructor(&cfkindstr);
            wres = dwarf_formudata (attrib,&tempud, err);
            if (wres == DW_DLV_OK) {
                kind = tempud;
                esb_append(&cfkindstr,
                    get_ATCF_name(kind,pd_dwarf_names_print_on_error));
            } else if (wres == DW_DLV_NO_ENTRY) {
                esb_append(&cfkindstr,  "?");
            } else {
                print_error_and_continue(dbg,
                    "Cannot get formudata length field for"
                    " DW_AT_SUN_cf_kind  ",
                    wres,*err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return res;
            }
            show_form_itself(glflags.show_form_used,
                glflags.verbose,theform,
                directform,&cfkindstr);
            esb_empty_string(&valname);
            esb_append(&valname, esb_get_string(&cfkindstr));
            esb_destructor(&cfkindstr);
        }
        break;
    case DW_AT_upper_bound:
        {
            Dwarf_Half theform;
            int rv;
            struct esb_s upperboundstr;

            esb_constructor(&upperboundstr);
            rv = dwarf_whatform(attrib,&theform,err);
            /*  Depending on the form and the attribute,
                process the form. */
            if (rv == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                "dwarf_whatform Cannot find attr form"
                " for DW_AT_upper_bound.",
                    rv, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return rv;
            } else if (rv == DW_DLV_NO_ENTRY) {
                esb_destructor(&upperboundstr);
                break;
            }

            switch (theform) {
            case DW_FORM_block1: {
                Dwarf_Half btheform = 0;
                Dwarf_Half directform = 0;
                rv  = get_form_values(dbg,attrib,
                    &btheform,&directform,err);
                if (rv == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: Cannot get DW_AT_upper_bound"
                        " form values",
                        rv, *err);
                    return rv;
                }
                rv = print_location_list(dbg, die, attrib,
                    checking,
                    TRUE,
                    &esb_extra,err);
                if (rv == DW_DLV_ERROR) {
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return rv;
                }
                append_extra_string = TRUE;
                show_form_itself(glflags.show_form_used,
                    glflags.verbose,
                    btheform,
                    directform,&valname);
                }
                break;
            default:
                rv = get_attr_value(dbg, tag, die,
                    dieprint_cu_goffset,
                    attrib, srcfiles, cnt, &upperboundstr,
                    glflags.show_form_used,glflags.verbose,
                    err);
                if (rv == DW_DLV_ERROR) {
                    print_error_and_continue(dbg,
                        "ERROR: Cannot get DW_AT_upper_bound"
                        " form value",
                        rv, *err);
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return rv;
                }
                esb_empty_string(&valname);
                esb_append(&valname, esb_get_string(&upperboundstr));
                break;
            }
            esb_destructor(&upperboundstr);
            break;
        }
    case DW_AT_low_pc:
    case DW_AT_high_pc:
        {
            int rv  = 0;
            rv = print_hipc_lopc_attribute(dbg, tag,
                die, dieprint_cu_goffset,
                srcfiles, cnt,
                attrib,
                attr,
                max_address,
                &bSawLow, &lowAddr,
                &bSawHigh, &highAddr,
                &valname,
                err);
            if (rv != DW_DLV_OK) {
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return rv;
            }
        }
        break;
    case DW_AT_ranges:
        {
            Dwarf_Half theform = 0;
            int rv;

            rv = dwarf_whatform(attrib,&theform,err);
            if (rv == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "dwarf_whatform cannot find Attr Form"
                    "for DW_AT_ranges",
                    rv, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return rv;
            } else if (rv == DW_DLV_NO_ENTRY) {
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                break;
            }
            rv = get_attr_value(dbg, tag,die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &valname,
                glflags.show_form_used,glflags.verbose,err);
            if (rv == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot find Attr value"
                    "for DW_AT_ranges",
                    rv, *err);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return rv;
            }
            rv = print_range_attribute(dbg, die, attr,attr_in,
                theform,
                pd_dwarf_names_print_on_error,print_else_name_match,
                &append_extra_string,
                &esb_extra,err);
            if (rv == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot print range attribute "
                    "for DW_AT_ranges",
                    rv, *err);
                /*  We will not stop, this is an omission
                    in libdwarf on DWARF5 rnglists */
                DROP_ERROR_INSTANCE(dbg,rv,*err);
            }
        }
        break;
    case DW_AT_MIPS_linkage_name:
        {
        int ml = 0;
        char linknamebuf[ESB_FIXED_ALLOC_SIZE];
        struct esb_s linkagenamestr;


        esb_constructor_fixed(&linkagenamestr,linknamebuf,
            sizeof(linknamebuf));
        ml = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset, attrib, srcfiles,
            cnt, &linkagenamestr, glflags.show_form_used,
            glflags.verbose,err);
        if (ml == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot  get value "
                "for DW_AT_MIPS_linkage_name ",
                ml, *err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            esb_destructor(&linkagenamestr);
            return ml;
        }
        esb_empty_string(&valname);
        esb_append(&valname, esb_get_string(&linkagenamestr));
        esb_destructor(&linkagenamestr);

        if ( glflags.gf_check_locations ||  glflags.gf_check_ranges) {
            int local_show_form = 0;
            int local_verbose = 0;
            const char *name = 0;
            struct esb_s lesb;

            esb_constructor(&lesb);
            get_attr_value(dbg, tag, die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &lesb, local_show_form,local_verbose,err);

            /*  Look for specific name forms, attempting to
                notice and report 'odd' identifiers.
                Used in the SNC LinkOnce feature. */
            name = esb_get_string(&lesb);
            safe_strcpy(glflags.PU_name,sizeof(glflags.PU_name),
                name,strlen(name));
            esb_destructor(&lesb);
        }
        }
        break;
    case DW_AT_name:
    case DW_AT_GNU_template_name:
        {
        char atnamebuf[ESB_FIXED_ALLOC_SIZE];
        struct esb_s templatenamestr;

        esb_constructor_fixed(&templatenamestr,atnamebuf,
            sizeof(atnamebuf));
        tres = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset,attrib, srcfiles, cnt,
            &templatenamestr, glflags.show_form_used,
            glflags.verbose,err);
        if (tres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot  get value "
                "for DW_AT_name/DW_AT_GNU_template_name ",
                tres, *err);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return tres;
        }
        esb_empty_string(&valname);
        esb_append(&valname, esb_get_string(&templatenamestr));
        esb_destructor(&templatenamestr);

        if ( glflags.gf_check_names && checking_this_compiler()) {
            int local_show_form = FALSE;
            int local_verbose = 0;
            struct esb_s lesb;
            const char *name = 0;

            esb_constructor(&lesb);
            tres = get_attr_value(dbg, tag, die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &lesb, local_show_form,local_verbose,err);
            /*  Look for specific name forms, attempting to
                notice and report 'odd' identifiers. */
            if (tres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot  get check_names value "
                    "for DW_AT_name/DW_AT_GNU_template_name ",
                    tres, *err);
                esb_destructor(&lesb);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return tres;
            }

            name = esb_get_string(&lesb);
            DWARF_CHECK_COUNT(names_result,1);
            if (!strcmp("\"(null)\"",name)) {
                DWARF_CHECK_ERROR(names_result,
                    "string attribute is \"(null)\".");
            } else {
                if (!dot_ok_in_identifier(tag,name)
                    && !glflags.need_CU_name && strchr(name,'.')) {
                    /*  This is a suggestion there 'might' be
                        a surprising name, not a guarantee of an
                        error. */
                    DWARF_CHECK_ERROR(names_result,
                        "string attribute is invalid.");
                }
            }
            esb_destructor(&lesb);
        }
        }

        /* If we are in checking mode and we do not have a PU name */
        if (( glflags.gf_check_locations ||  glflags.gf_check_ranges) &&
            glflags.seen_PU && !glflags.PU_name[0]) {
            int local_show_form = FALSE;
            int local_verbose = 0;
            const char *name = 0;
            struct esb_s lesb;
            int vres = 0;

            esb_constructor(&lesb);
            vres = get_attr_value(dbg, tag, die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &lesb, local_show_form,local_verbose,err);
            if (vres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot get check-locations value "
                    "for DW_AT_name/DW_AT_GNU_template_name ",
                    vres, *err);
                esb_destructor(&lesb);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return vres;
            }
            name = esb_get_string(&lesb);
            safe_strcpy(glflags.PU_name,sizeof(glflags.PU_name),name,
                strlen(name));
            esb_destructor(&lesb);
        }

        /* If we are processing the compile unit, record the name */
        if (glflags.seen_CU && glflags.need_CU_name) {
            /* Lets not get the form name included. */
            struct esb_s lesb;
            int local_show_form_used = FALSE;
            int local_verbose = 0;
            int sres = 0;

            esb_constructor(&lesb);
            sres = get_attr_value(dbg, tag, die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &lesb, local_show_form_used,local_verbose,
                err);
            if (sres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot get CU name "
                    "for DW_AT_name/DW_AT_GNU_template_name ",
                    sres, *err);
                esb_destructor(&lesb);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return sres;
            }

            safe_strcpy(glflags.CU_name,sizeof(glflags.CU_name),
                esb_get_string(&lesb),esb_string_len(&lesb));
            glflags.need_CU_name = FALSE;
            esb_destructor(&lesb);
        }
        break;

    case DW_AT_producer:
        {
        struct esb_s lesb;
        int pres = 0;

        esb_constructor(&lesb);
        pres = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset,attrib, srcfiles, cnt,
            &lesb, glflags.show_form_used,glflags.verbose,
            err);
        if (pres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "Cannot  get value "
                "for DW_AT_producer",
                pres, *err);
            esb_destructor(&lesb);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return pres;
        }

        esb_empty_string(&valname);
        esb_append(&valname, esb_get_string(&lesb));
        esb_destructor(&lesb);
        /* If we are in checking mode, identify the compiler */
        if ( glflags.gf_do_check_dwarf ||
            glflags.gf_search_is_on) {
            /*  Do not use show-form here! We just want
                the producer name, not the form name. */
            int show_form_local = FALSE;
            int local_verbose = 0;
            struct esb_s local_e;

            esb_constructor(&local_e);
            pres = get_attr_value(dbg, tag, die,
                dieprint_cu_goffset,attrib, srcfiles, cnt,
                &local_e, show_form_local,local_verbose,err);
            if (pres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "Cannot  get checking value "
                    "for DW_AT_producer",
                    pres, *err);
                esb_destructor(&local_e);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return pres;
            }
            /* Check if this compiler version is a target */
            update_compiler_target(esb_get_string(&local_e));
            esb_destructor(&local_e);
        }
        }
        break;

    /*  When dealing with SNC linkonce symbols,
        the low_pc and high_pc
        are associated with a specific symbol;
        SNC always generate a name with
        DW_AT_MIPS_linkage_name; GCC does not;
        instead gcc generates
        DW_AT_abstract_origin or DW_AT_specification;
        in that case we have to
        traverse this attribute in order to get the
        name for the linkonce */
    case DW_AT_specification:
    case DW_AT_abstract_origin:
    case DW_AT_type:
        {
        char typebuf[ESB_FIXED_ALLOC_SIZE];
        struct esb_s lesb;

        esb_constructor_fixed(&lesb,typebuf,
            sizeof(typebuf));
        tres = get_attr_value(dbg, tag, die,
            dieprint_cu_goffset,attrib, srcfiles, cnt, &lesb,
            glflags.show_form_used,glflags.verbose,err);
        if (tres == DW_DLV_ERROR) {
            struct esb_s m;
            const char *n =
                get_AT_name(attr,pd_dwarf_names_print_on_error);
            esb_constructor(&m);
            esb_append(&m,
                "Cannot get get value for a ");
            esb_append(&m,n);
            print_error_and_continue(dbg,
                esb_get_string(&m),
                tres,*err);
            esb_destructor(&m);
            esb_destructor(&valname);
            esb_destructor(&esb_extra);
            return tres;
        }
        esb_empty_string(&valname);
        esb_append(&valname, esb_get_string(&lesb));
        esb_destructor(&lesb);

        if (glflags.gf_check_forward_decl ||
            glflags.gf_check_self_references ||
            glflags.gf_search_is_on) {
            Dwarf_Off die_goff = 0;
            Dwarf_Off ref_goff = 0;
            int frres = 0;
            int suppress_check = 0;
            Dwarf_Half theform = 0;
            Dwarf_Half directform = 0;

            frres =get_form_values(dbg,attrib,&theform,&directform,
                err);
            if (frres == DW_DLV_ERROR) {
                const char *n =
                    get_AT_name(attr,pd_dwarf_names_print_on_error);
                struct esb_s m;
                esb_constructor(&m);
                esb_append(&m,
                    "Cannot get forms for a ");
                esb_append(&m,n);
                print_error_and_continue(dbg,
                    esb_get_string(&m),
                    frres,*err);
                esb_destructor(&m);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return frres;
            }
            frres = dwarf_global_formref(attrib, &ref_goff, err);
            if (frres == DW_DLV_ERROR) {
                int myerr = dwarf_errno(*err);
                if (myerr == DW_DLE_REF_SIG8_NOT_HANDLED) {
                    /*  DW_DLE_REF_SIG8_NOT_HANDLED */
                    /*  No offset available, it makes little sense
                        to delve into this sort of reference unless
                        we think a graph of self-refs *across*
                        type-units is possible. Hmm. FIXME? */
                    suppress_check = 1 ;
                    DWARF_CHECK_COUNT(self_references_result,1);
                    DWARF_CHECK_ERROR(self_references_result,
                        "DW_AT_ref_sig8 not handled so "
                        "self references not fully checked");
                    DROP_ERROR_INSTANCE(dbg,frres,*err);
                } else {
                    const char *n =
                        get_AT_name(attr,
                            pd_dwarf_names_print_on_error);
                    struct esb_s m;
                    esb_constructor(&m);
                    esb_append(&m,
                        "Cannot get formref global offset for a ");
                    esb_append(&m,n);
                    print_error_and_continue(dbg,
                        esb_get_string(&m),
                        frres,*err);
                    esb_destructor(&m);
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return frres;
                }
            } else if (frres == DW_DLV_NO_ENTRY) {
                const char *n =
                    get_AT_name(attr,pd_dwarf_names_print_on_error);
                struct esb_s m;

                esb_constructor(&m);
                esb_append(&m,
                    "Cannot get formref global offset for a ");
                esb_append(&m,n);
                print_error_and_continue(dbg,
                    esb_get_string(&m),
                    frres,*err);
                esb_destructor(&m);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return frres;
            }
            frres = dwarf_dieoffset(die, &die_goff, err);
            if (frres != DW_DLV_OK) {
                const char *n =
                    get_AT_name(attr,pd_dwarf_names_print_on_error);
                struct esb_s m;
                esb_constructor(&m);
                esb_append(&m,
                    "Cannot get formref dieoffset offset for a ");
                esb_append(&m,n);
                print_error_and_continue(dbg,
                    esb_get_string(&m),
                    frres,*err);
                esb_destructor(&m);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return frres;
            }

            if (!suppress_check &&
                glflags.gf_check_self_references &&
                form_refers_local_info(theform) ) {
                Dwarf_Die ref_die = 0;
                int  ifres = 0;

                ResetBucketGroup(glflags.pVisitedInfo);
                AddEntryIntoBucketGroup(glflags.pVisitedInfo,
                    die_goff,0,0,0,
                    NULL,FALSE);

                /*  Follow reference chain, looking for
                    self references */
                frres = dwarf_offdie_b(dbg,ref_goff,is_info,
                    &ref_die,err);
                if (frres == DW_DLV_OK) {
                    Dwarf_Off ref_die_cu_goff = 0;
                    Dwarf_Off die_loff = 0; /* CU-relative. */
                    int fresb = 0;

                    if (dump_visited_info) {
                        fresb = dwarf_die_CU_offset(die, &die_loff,
                            err);
                        if (fresb == DW_DLV_OK) {
                            do_dump_visited_info(die_indent_level,
                                die_loff,die_goff,
                                dieprint_cu_goffset,
                                atname,esb_get_string(&valname));
                        } else {
                            esb_destructor(&valname);
                            esb_destructor(&esb_extra);
                            return fresb;
                        }
                    }
                    ++die_indent_level;
                    fresb =dwarf_CU_dieoffset_given_die(ref_die,
                        &ref_die_cu_goff, err);
                        /* Check above call return status? FIXME */
                    if (fresb != DW_DLV_OK) {
                        const char *n =
                            get_AT_name(attr,
                            pd_dwarf_names_print_on_error);
                        struct esb_s m;
                        esb_constructor(&m);
                        esb_append(&m,
                            "Cannot get CU dieoffset "
                            "given die for a ");
                        esb_append(&m,n);
                        print_error_and_continue(dbg,
                            esb_get_string(&m),
                            frres,*err);
                        esb_destructor(&m);
                        esb_destructor(&valname);
                        esb_destructor(&esb_extra);
                        return frres;
                    }

                    ifres = traverse_one_die(dbg,attrib,ref_die,
                        ref_die_cu_goff,
                        is_info,srcfiles,cnt,die_indent_level,
                        err);
                    dwarf_dealloc_die(ref_die);
                    ref_die = 0;
                    --die_indent_level;
                    if (ifres != DW_DLV_OK) {
                        esb_destructor(&valname);
                        esb_destructor(&esb_extra);
                        return ifres;
                    }
                }
                DeleteKeyInBucketGroup(glflags.pVisitedInfo,die_goff);
                if (frres == DW_DLV_ERROR) {
                    esb_destructor(&valname);
                    esb_destructor(&esb_extra);
                    return frres;
                }
            }

            if (!suppress_check && glflags.gf_check_forward_decl) {
                if (attr == DW_AT_specification) {
                    /*  Check the DW_AT_specification
                        does not make forward
                        references to DIEs.
                        DWARF4 specifications, section 2.13.2,
                        but really they are legal,
                        this test is probably wrong. */
                    DWARF_CHECK_COUNT(forward_decl_result,1);
                    if (ref_goff > die_goff) {
                        DWARF_CHECK_ERROR2(forward_decl_result,
                            "Invalid forward reference to DIE: ",
                            esb_get_string(&valname));
                    }
                }
            }

            /*  When doing search, if the attribute is
                DW_AT_specification or
                DW_AT_abstract_origin, get any name
                associated with the DIE
                referenced in the offset.
                The 2 more typical cases are:
                Member functions, where 2 DIES are generated:
                    DIE for the declaration and DIE for the definition
                    and connected via the DW_AT_specification.
                Inlined functions, where 2 DIES are generated:
                    DIE for the concrete instance and
                    DIE for the abstract
                    instance and connected via the
                    DW_AT_abstract_origin.
            */
            if ( glflags.gf_search_is_on &&
                (attr == DW_AT_specification ||
                attr == DW_AT_abstract_origin)) {
                Dwarf_Die ref_die = 0;
                int srcres = 0;

                /* Follow reference chain, looking for the DIE name */
                srcres = dwarf_offdie_b(dbg,ref_goff,is_info,
                    &ref_die,err);
                if (srcres == DW_DLV_OK) {
                    /* Get the DIE name */
                    char *name = 0;
                    srcres = dwarf_diename(ref_die,&name,err);
                    if (srcres == DW_DLV_OK) {
                        esb_empty_string(&valname);
                        esb_append(&valname,name);
                    }
                    if (srcres == DW_DLV_ERROR) {
                        glflags.gf_count_major_errors++;
                        esb_empty_string(&valname);
                        esb_append(&valname,
                            "<ERROR: no name for reference");
                        DROP_ERROR_INSTANCE(dbg,srcres,*err);
                    }
                    /* Release the allocated DIE */
                    dwarf_dealloc_die(ref_die);
                } else if (srcres == DW_DLV_ERROR) {
                    glflags.gf_count_major_errors++;
                    esb_empty_string(&valname);
                    esb_append(&valname,
                        "<ERROR: no referred-to die found ");
                    DROP_ERROR_INSTANCE(dbg,srcres,*err);
                }
            }
        }
        /* If we are in checking mode and we do not have a PU name */
        if (( glflags.gf_check_locations ||
            glflags.gf_check_ranges) &&
            glflags.seen_PU && !glflags.PU_name[0]) {
            if (tag == DW_TAG_subprogram) {
                /* This gets the DW_AT_name if this DIE has one. */
                Dwarf_Addr low_pc =  0;
                struct esb_s pn;
                int found = 0;
                /*  The cu_die_for_print_frames will not be changed
                    by get_proc_name_by_die(). Used when printing frames */
                Dwarf_Die cu_die_for_print_frames = 0;

                esb_constructor(&pn);
                /* Only looks in this one DIE's attributes */
                found = get_proc_name_by_die(dbg,die,
                    low_pc,&pn,
                    &cu_die_for_print_frames,
                    /*pcMap=*/0,
                    err);
                if (found == DW_DLV_ERROR) {
                    struct esb_s m;
                    const char *n =
                        get_AT_name(attr,
                        pd_dwarf_names_print_on_error);
                    esb_constructor(&m);
                    esb_append(&m,
                        "Cannot get get value for a ");
                    esb_append(&m,n);
                    print_error_and_continue(dbg,
                        esb_get_string(&m),
                        found,*err);
                    esb_destructor(&m);
                    return found;
                }
                if (found == DW_DLV_OK) {
                    safe_strcpy(glflags.PU_name,
                        sizeof(glflags.PU_name),
                        esb_get_string(&pn),
                        esb_string_len(&pn));
                }
                esb_destructor(&pn);
            }
        }
        }
        break;
    default:
        {
            char ebuf[ESB_FIXED_ALLOC_SIZE];
            struct esb_s lesb;
            int dres = 0;

            esb_constructor_fixed(&lesb,ebuf,sizeof(ebuf));
            dres = get_attr_value(dbg, tag,die,
                dieprint_cu_goffset,attrib, srcfiles, cnt, &lesb,
                glflags.show_form_used,glflags.verbose,err);
            if (dres == DW_DLV_ERROR) {
                struct esb_s m;
                const char *n =
                    get_AT_name(attr,
                    pd_dwarf_names_print_on_error);
                esb_constructor(&m);
                esb_append(&m,
                    "Cannot get get value for a ");
                esb_append(&m,n);
                print_error_and_continue(dbg,
                    esb_get_string(&m),
                    dres,*err);
                esb_destructor(&m);
                esb_destructor(&valname);
                esb_destructor(&esb_extra);
                return dres;
            }
            esb_empty_string(&valname);
            esb_append(&valname, esb_get_string(&lesb));
            esb_destructor(&lesb);
        }
        break;
    } /* end switch statment on attribute code */
    res = 0; /* value above no longer relevant */

    if (!print_else_name_match) {
        if (have_a_search_match(esb_get_string(&valname),atname)) {
            /* Count occurrence of text */
            ++glflags.search_occurrences;
            if ( glflags.gf_search_wide_format) {
                found_search_attr = TRUE;
            } else {
                PRINT_CU_INFO();
                bTextFound = TRUE;
            }
        }
    }
#if 0  /*  DEBUGGING ONLY */
    /*  This prints all the actual fields as well as the
        macro results that use the fields. */
    printf("DEBUGONLY std print? attr name %u &&  %u  && %u || %u.  %u %u %u\n",
    (unsigned)PRINTING_UNIQUE,
    (unsigned)PRINTING_DIES,
    (unsigned)print_else_name_match,
    (unsigned)bTextFound,
    (unsigned) glflags.gf_do_print_dwarf,
    (unsigned) glflags.gf_check_verbose_mode,
    (unsigned) glflags.gf_record_dwarf_error);
#endif /* DEBUGGING ONLY */
    /*  Above we created detailed messages in
        the valname and esb_extra strings.
        If we're just printing everything
        we will now print those.

        Otherwise If we're checking things we will print those
        that failed a check (a DWARF_CHECK message
        was just printed)

        Otherwise if searching for specific attributes
        (the else_name_match of the following if stmt)
        a match means we print the strings.

        Otherwise we will just discard the
        valname and esb_extra strings.

        That's why the big IF below
        has so much in it.
    */

    if ((PRINTING_UNIQUE && PRINTING_DIES && print_else_name_match)
        || bTextFound) {
        /*  Print just the Tags and Attributes */
        if (!glflags.gf_display_offsets) {
            printf("%-28s\n",atname);
        } else {
            if (glflags.dense) {
                printf(" %s<%s>", atname, esb_get_string(&valname));
                if (append_extra_string) {
                    char *v = esb_get_string(&esb_extra);
                    printf("%s", v);
                }
            } else {
                printf("%-28s", atname);
                if (strlen(atname) >= 28) {
                    printf(" ");
                }
                printf("%s\n", sanitized(esb_get_string(&valname)));
                if (append_extra_string) {
                    char *v = esb_get_string(&esb_extra);
                    printf("%s", sanitized(v));
                }
            }
        }
    }
    esb_destructor(&valname);
    esb_destructor(&esb_extra);
    *attr_duplication = found_search_attr;
    return DW_DLV_OK;
}

int
dwarfdump_print_location_operations(Dwarf_Debug dbg,
    Dwarf_Locdesc * llbuf,    /* Non-zero for old interface. */
    Dwarf_Locdesc_c locdesc,  /* Non-zero for 2015 interface. */
    UNUSEDARG Dwarf_Unsigned llent, /* Which desc we have . */
    Dwarf_Unsigned entrycount,
    UNUSEDARG Dwarf_Small  lkind,
    UNUSEDARG int no_ending_newlines,
    Dwarf_Addr  baseaddr,
    struct esb_s *string_out,
    Dwarf_Error *err)
{
    Dwarf_Half no_of_ops = 0;
    unsigned i = 0;
    Dwarf_Bool report_raw = TRUE;

    if(llbuf) {
        Dwarf_Locdesc *locd = 0;
        locd = llbuf;
        no_of_ops = llbuf->ld_cents;
        possibly_increase_esb_alloc(string_out,no_of_ops,100);
        for (i = 0; i < no_of_ops; i++) {
            Dwarf_Loc * op = &locd->ld_s[i];

            int res = _dwarf_print_one_expr_op(dbg,op,NULL,i,
                report_raw,
                baseaddr,string_out,err);
            if (res == DW_DLV_ERROR) {
                return res;
            }
        }
        return DW_DLV_OK;
    }
    /* ASSERT: locs != NULL */
    no_of_ops = entrycount;
    possibly_increase_esb_alloc(string_out,no_of_ops,100);
    for (i = 0; i < no_of_ops; i++) {
        int res = 0;
        res = _dwarf_print_one_expr_op(dbg,NULL,locdesc,i,
            report_raw,
            baseaddr,string_out,err);
        if (res == DW_DLV_ERROR) {
            return res;
        }
    }
    return DW_DLV_OK;
}

static int
op_has_no_operands(int op)
{
    unsigned i = 0;
    if (op >= DW_OP_lit0 && op <= DW_OP_reg31) {
        return TRUE;
    }
    for (; ; ++i) {
        struct operation_descr_s *odp = opdesc+i;
        if (odp->op_code == 0) {
            break;
        }
        if (odp->op_code != op) {
            continue;
        }
        if (odp->op_count == 0) {
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

static void
show_contents(struct esb_s *string_out,
    unsigned int length,const unsigned char * bp)
{
    unsigned int i = 0;

    if(!length) {
        return;
    }
    esb_append(string_out," contents 0x");
    for (; i < length; ++i,++bp) {
        /*  Do not use DW_PR_DUx here,
            the value  *bp is a const unsigned char. */
        esb_append_printf_u(string_out,"%02x", *bp);
    }
}


int
_dwarf_print_one_expr_op(Dwarf_Debug dbg,
    Dwarf_Loc* expr,
    Dwarf_Locdesc_c exprc,
    int index,
    Dwarf_Bool report_raw,
    UNUSEDARG Dwarf_Addr baseaddr,
    struct esb_s *string_out,
    Dwarf_Error *err)
{
    Dwarf_Small op = 0;
    Dwarf_Unsigned opd1 = 0;
    Dwarf_Unsigned opd2 = 0;
    Dwarf_Unsigned opd3 = 0;
    Dwarf_Unsigned raw1 = 0;
    Dwarf_Unsigned raw2 = 0;
    Dwarf_Unsigned raw3 = 0;
    Dwarf_Unsigned offsetforbranch = 0;
    const char * op_name = 0;

    if (index > 0) {
        esb_append(string_out, " ");
    }
    if (expr) {
        /* DWARF 2,3,4 style */
        op = expr->lr_atom;
        opd1 = expr->lr_number;
        opd2 = expr->lr_number2;
    } else {
        /* DWARF 2,3,4 and DWARF5 style */
        int res = 0;
        res = dwarf_get_location_op_value_d(exprc,
            index,
            &op,&opd1,&opd2,&opd3,
            &raw1,&raw2,&raw3,
            &offsetforbranch,
            err);
        if (res != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "dwarf_get_location_op_value_c "
                "did not get a value!",
                res,*err);
            return res;
        }
        if (report_raw) {
            opd1 = raw1;
            opd2 = raw2;
            opd3 = raw3;
        }
    }
    op_name = get_OP_name(op,pd_dwarf_names_print_on_error);
    esb_append(string_out, op_name);
    if (op_has_no_operands(op)) {
        /* Nothing to add. */
    } else if (op >= DW_OP_breg0 && op <= DW_OP_breg31) {
        esb_append_printf_i(string_out,
            "%+" DW_PR_DSd , opd1);
    } else {
        switch (op) {
        case DW_OP_addr:
            bracket_hex(" ",opd1,"",string_out);
            break;
        case DW_OP_const1s:
        case DW_OP_const2s:
        case DW_OP_const4s:
        case DW_OP_const8s:
        case DW_OP_consts:
        case DW_OP_skip:
        case DW_OP_bra:
        case DW_OP_fbreg:
            esb_append(string_out," ");
            formx_signed(opd1,string_out);
            break;
        case DW_OP_GNU_addr_index: /* unsigned val */
        case DW_OP_addrx:  /* DWARF5: unsigned val */
        case DW_OP_GNU_const_index:
        case DW_OP_constx: /* DWARF5: unsigned val */
        case DW_OP_const1u:
        case DW_OP_const2u:
        case DW_OP_const4u:
        case DW_OP_const8u:
        case DW_OP_constu:
        case DW_OP_pick:
        case DW_OP_plus_uconst:
        case DW_OP_regx:
        case DW_OP_piece:
        case DW_OP_deref_size:
        case DW_OP_xderef_size:
            esb_append_printf_u(string_out,
                " %" DW_PR_DUu , opd1);
            break;
        case DW_OP_bregx:
            bracket_hex(" ",opd1,"",string_out);
            esb_append(string_out,"+");
            formx_signed(opd2,string_out);
            break;
        case DW_OP_call2:
            bracket_hex(" ",opd1,"",string_out);
            break;
        case DW_OP_call4:
            bracket_hex(" ",opd1,"",string_out);
            break;
        case DW_OP_call_ref:
            bracket_hex(" ",opd1,"",string_out);
            break;
        case DW_OP_bit_piece:
            bracket_hex(" ",opd1,"",string_out);
            bracket_hex(" offset ",opd2,"",string_out);
            break;
        case DW_OP_implicit_value:
            {
#define IMPLICIT_VALUE_PRINT_MAX 12
                unsigned int print_len = 0;
                bracket_hex(" ",opd1,"",string_out);
                /*  The other operand is a block of opd1 bytes. */
                /*  FIXME */
                print_len = opd1;
                if (print_len > IMPLICIT_VALUE_PRINT_MAX) {
                    print_len = IMPLICIT_VALUE_PRINT_MAX;
                }
#undef IMPLICIT_VALUE_PRINT_MAX
                {
                    const unsigned char *bp = 0;
                    /*  This is a really ugly cast, a way
                        to implement DW_OP_implicit value in
                        this libdwarf context. */
                    bp = (const unsigned char *)(uintptr_t) opd2;
                    show_contents(string_out,print_len,bp);
                }
            }
            break;

        /* We do not know what the operands, if any, are. */
        case DW_OP_HP_unknown:
        case DW_OP_HP_is_value:
        case DW_OP_HP_fltconst4:
        case DW_OP_HP_fltconst8:
        case DW_OP_HP_mod_range:
        case DW_OP_HP_unmod_range:
        case DW_OP_HP_tls:
        case DW_OP_INTEL_bit_piece:
            break;
        case DW_OP_stack_value:  /* DWARF4 */
            break;
        case DW_OP_GNU_uninit:  /* DW_OP_APPLE_uninit */
            /* No operands. */
            break;
        case DW_OP_GNU_encoded_addr:
            bracket_hex(" ",opd1,"",string_out);
            break;
        case DW_OP_implicit_pointer:       /* DWARF5 */
        case DW_OP_GNU_implicit_pointer:
            bracket_hex(" ",opd1,"",string_out);
            esb_append(string_out, " ");
            formx_signed(opd2,string_out);
            break;
        case DW_OP_entry_value:       /* DWARF5 */
        case DW_OP_GNU_entry_value: {
            const unsigned char *bp = 0;
            unsigned int length = 0;

            length = opd1;
            bracket_hex(" ",opd1,"",string_out);
            bp = (Dwarf_Small *)(uintptr_t) opd2;
            if (!bp) {
                esb_append(string_out,
                    "ERROR: Null databyte pointer DW_OP_entry_value ");
            } else {
                show_contents(string_out,length,bp);
            }
            }
            break;
        case DW_OP_const_type:           /* DWARF5 */
        case DW_OP_GNU_const_type:
            {
            const unsigned char *bp = 0;
            unsigned int length = 0;

            bracket_hex(" ",opd1,"",string_out);
            length = opd2;
            esb_append(string_out," const length: ");
            esb_append_printf_u(string_out,
                "%u" , length);
            /* Now point to the data bytes of the const. */
            bp = (Dwarf_Small *)(uintptr_t)opd3;
            if (!bp) {
                esb_append(string_out,
                    "ERROR: Null databyte pointer DW_OP_const_type ");
            } else {
                show_contents(string_out,length,bp);
            }
            }
            break;
        case DW_OP_regval_type:           /* DWARF5 */
        case DW_OP_GNU_regval_type: {
            esb_append_printf_u(string_out,
                " 0x%" DW_PR_DUx , opd1);
            bracket_hex(" ",opd2,"",string_out);
            }
            break;
        case DW_OP_deref_type: /* DWARF5 */
        case DW_OP_GNU_deref_type: {
            esb_append_printf_u(string_out,
                " 0x%02" DW_PR_DUx , opd1);
            bracket_hex(" ",opd2,"",string_out);
            }
            break;
        case DW_OP_convert: /* DWARF5 */
        case DW_OP_GNU_convert:
        case DW_OP_reinterpret: /* DWARF5 */
        case DW_OP_GNU_reinterpret:
        case DW_OP_GNU_parameter_ref:
            esb_append_printf_u(string_out,
                " 0x%02"  DW_PR_DUx , opd1);
            break;
        default:
            {
                esb_append_printf_u(string_out,
                    " dwarf_op unknown 0x%x", (unsigned)op);
            }
            break;
        }
    }
    return DW_DLV_OK;
}

void
loc_error_check(
    const char *     tagname,
    const char *     attrname,
    Dwarf_Addr lopcfinal,
    Dwarf_Addr rawlopc,
    Dwarf_Addr hipcfinal,
    Dwarf_Addr rawhipc,
    Dwarf_Unsigned offset,
    Dwarf_Addr base_address,
    Dwarf_Bool *bError)
{
    DWARF_CHECK_COUNT(locations_result,1);

    /*  Check the low_pc and high_pc are within
        a valid range in the .text section */
    if (IsValidInBucketGroup(glflags.pRangesInfo,lopcfinal) &&
        IsValidInBucketGroup(glflags.pRangesInfo,hipcfinal)) {
        /* Valid values; do nothing */
    } else {
        /*  At this point may be we are dealing with
            a linkonce symbol */
        if (IsValidInLinkonce(glflags.pLinkonceInfo,glflags.PU_name,
            lopcfinal,hipcfinal)) {
            /* Valid values; do nothing */
        } else {
            struct esb_s m;

            esb_constructor(&m);
            *bError = TRUE;
            esb_append_printf_s(&m,
                ".debug_loc[lists]: Address outside a "
                "valid .text range: TAG %s",tagname);
            esb_append_printf_s(&m," with attribute %s.",
                attrname);
            DWARF_CHECK_ERROR(locations_result,
                esb_get_string(&m));
            if ( glflags.gf_check_verbose_mode && PRINTING_UNIQUE) {
                printf(
                    "Offset = 0x%" DW_PR_XZEROS DW_PR_DUx
                    ", Base = 0x%"  DW_PR_XZEROS DW_PR_DUx ", "
                    "Low = 0x%"  DW_PR_XZEROS DW_PR_DUx
                    " (rawlow = 0x%"  DW_PR_XZEROS DW_PR_DUx
                    "), High = 0x%"  DW_PR_XZEROS DW_PR_DUx
                    " (rawhigh = 0x%"  DW_PR_XZEROS DW_PR_DUx ")\n",
                    offset,base_address,
                    lopcfinal,
                    rawlopc,
                    hipcfinal,
                    rawhipc);
            }
            esb_destructor(&m);
        }
    }
}

static void
print_loclists_context_head(
    Dwarf_Small    lkind,
    Dwarf_Unsigned lle_count,
    Dwarf_Unsigned lle_version,
    Dwarf_Unsigned loclists_index,
    Dwarf_Unsigned bytes_total_in_lle,
    Dwarf_Half     offset_size,
    Dwarf_Half     address_size,
    Dwarf_Half     segment_selector_size,
    Dwarf_Unsigned overall_offset_of_this_context,
    Dwarf_Unsigned total_length_of_this_context,
    Dwarf_Unsigned offset_table_offset,
    Dwarf_Unsigned offset_table_entrycount,
    Dwarf_Bool     loclists_base_present,
    Dwarf_Addr     loclists_base,
    Dwarf_Bool     loclists_base_address_present,
    Dwarf_Addr     loclists_base_address,
    Dwarf_Bool     loclists_debug_addr_base_present,
    Dwarf_Addr     loclists_debug_addr_base,
    Dwarf_Unsigned loclists_offset_lle_set,
    struct esb_s  *esbp)
{
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "bytes total this loclist: %3u",bytes_total_in_lle);
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "number of entries       : %3u", lle_count);
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "context number          : %3u",loclists_index);
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "version                 : %3u",lle_version);
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "address size            : %3u",address_size);
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "offset size             : %3u",offset_size);
    if (segment_selector_size) {
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "segment selector size   : %3u",
            segment_selector_size);
    }
    if(lkind == DW_LKIND_loclists) {
        append_local_prefix(esbp);
            esb_append_printf_u(esbp,
            "offset of context       : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            overall_offset_of_this_context);

        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "offset table entrycount : %3u",
            offset_table_entrycount);
        if (offset_table_entrycount) {
            append_local_prefix(esbp);
            esb_append_printf_u(esbp,
                "offset table offset     : 0x%"
                DW_PR_XZEROS DW_PR_DUx,
                offset_table_offset);
        }
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "offset of this list set : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            loclists_offset_lle_set);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "length of context       : %3u",
            total_length_of_this_context);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "end of context offset   : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            overall_offset_of_this_context +
            total_length_of_this_context);
    }

    if (loclists_base_present) {
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "DW_AT_loclists_base     : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            loclists_base);
    }
    if (loclists_base_address_present) {
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "DW_AT_low_pc(base addr) : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            loclists_base_address);
    }
    if (loclists_debug_addr_base_present) {
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "DW_AT_addr_base         : 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            loclists_debug_addr_base);
    }
    esb_append(esbp,"\n");
}

/*  Fill buffer with location lists data for printing.
    This does the details.
    It's up to the caller to determine which
    esb to put the result in. */
/*ARGSUSED*/
static int
print_location_list(Dwarf_Debug dbg,
    Dwarf_Die die,
    Dwarf_Attribute attr,
    boolean checking,
    int  no_end_newline,
    struct esb_s *details,
    Dwarf_Error* llerr)
{
    Dwarf_Locdesc *llbuf = 0;
    Dwarf_Locdesc **llbufarray = 0; /* Only for older interface. */
    Dwarf_Unsigned no_of_elements = 0;
    Dwarf_Loc_Head_c loclist_head = 0; /* 2015 loclist interface */
    Dwarf_Unsigned i = 0;
    int            lres = 0;
    unsigned int   llent = 0;

    /*  Base address used to update entries in .debug_loc.
        CU_base_address is a global. Terrible way to
        pass in this value. FIXME. See also CU_low_address
        as base address is special for address ranges */
    Dwarf_Addr     base_address = glflags.CU_base_address;
    Dwarf_Addr     lopc = 0;
    Dwarf_Addr     hipc = 0;
    Dwarf_Bool     bError = FALSE;
    Dwarf_Small    lle_value = 0; /* DWARF5 */
    Dwarf_Unsigned lle_count = 0;
    Dwarf_Unsigned loclists_index = 0;

    /*  This is the section offset of the expression, not
        the location description prefix. */
    Dwarf_Unsigned expr_section_offset = 0;
    Dwarf_Half    address_size = 0;
    Dwarf_Half    segment_selector_size = 0;
    Dwarf_Addr     max_address = 0;
    Dwarf_Unsigned lle_version = 2;
    Dwarf_Half     version = 2;
    Dwarf_Half     offset_size = 4;
    Dwarf_Small    lkind = DW_LKIND_unknown;
    /* old and new interfaces differ on signedness.  */
    Dwarf_Signed   locentry_count = 0;
    Dwarf_Unsigned bytes_total_in_lle = 0;
    Dwarf_Unsigned overall_offset_of_this_context = 0;
    Dwarf_Unsigned total_length_of_this_context = 0;
    Dwarf_Bool     loclists_base_present = FALSE;
    Dwarf_Unsigned loclists_base = 0;
    Dwarf_Bool     loclists_base_address_present = FALSE;
    Dwarf_Unsigned loclists_base_address = 0;
    Dwarf_Bool     loclists_debug_addr_base_present = FALSE;
    Dwarf_Unsigned loclists_debug_addr_base = 0;
    Dwarf_Unsigned loclists_offset_lle_set = 0;
    Dwarf_Unsigned offset_table_offset = 0;
    Dwarf_Unsigned offset_table_entrycount = 0;
    struct esb_s   section_truename;


    lres = dwarf_get_version_of_die(die,&version,
        &offset_size);
    if (lres != DW_DLV_OK) {
        /* is DW_DLV_ERROR (see libdwarf query.c) */
        simple_err_only_return_action(lres,
            "\nERROR: die or context bad calling "
            "dwarf_get_version_of_die in print_location_list."
            " Something is very wrong.");
        return DW_DLV_NO_ENTRY;
    }

    lres = get_address_size_and_max(dbg,&address_size,
        &max_address,llerr);
    if (lres != DW_DLV_OK) {
        print_error_and_continue(dbg,"Getting address size"
            " and maximum failed while getting location list",
            lres,*llerr);
        return lres;
    }
    if (version == DWVERSION5 || !glflags.gf_use_old_dwarf_loclist) {
        /* Preferred interface. Deals with DWARF2,3,4,5. */
        lres = dwarf_get_loclist_c(attr,&loclist_head,
            &no_of_elements,llerr);
        if (lres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: dwarf_get_loclist_c fails",
                lres, *llerr);
            return lres;
        } else if (lres == DW_DLV_NO_ENTRY) {
            return lres;
        }
        lres = dwarf_get_loclist_head_basics(loclist_head,
            &lkind,
            &lle_count,&lle_version, &loclists_index,
            &bytes_total_in_lle,
            &offset_size, &address_size, &segment_selector_size,
            &overall_offset_of_this_context,
            &total_length_of_this_context,
            &offset_table_offset,
            &offset_table_entrycount,
            &loclists_base_present,&loclists_base,
            &loclists_base_address_present,&loclists_base_address,
            &loclists_debug_addr_base_present,
            &loclists_debug_addr_base,
            &loclists_offset_lle_set,llerr);
        if (lres != DW_DLV_OK) {
            return lres;
        }
        version = lle_version;
        /*  append_local_prefix(esbp); No,
            here the newline grates, causes blank
            line in the output. So. Just add 6 spaces.
            the output already has a newline. */
        if (lkind != DW_LKIND_expression) {
            esb_append(details,"      ");
            esb_constructor(&section_truename);
            if (lkind == DW_LKIND_loclists) {
                get_true_section_name(dbg,".debug_loclists",
                &section_truename,FALSE);
            } else {
                get_true_section_name(dbg,".debug_loc",
                &section_truename,FALSE);
            }
            esb_append_printf_s(details,"%-15s",
                esb_get_string(&section_truename));
            esb_append_printf_u(details,
                " offset  :"
                " 0x%" DW_PR_XZEROS DW_PR_DUx,
                loclists_offset_lle_set);
            esb_destructor(&section_truename);
            if (glflags.verbose) {
                print_loclists_context_head(lkind,
                    lle_count, lle_version, loclists_index,
                    bytes_total_in_lle,
                    offset_size,address_size, segment_selector_size,
                    overall_offset_of_this_context,
                    total_length_of_this_context,
                    offset_table_offset, offset_table_entrycount,
                    loclists_base_present,loclists_base,
                    loclists_base_address_present,
                    loclists_base_address,
                    loclists_debug_addr_base_present,
                    loclists_debug_addr_base,
                    loclists_offset_lle_set,
                    details);
            } else {
                /* things look better with this...  no -v */
                esb_append(details,"\n   ");
            }
        }
    } else {
        /*  DWARF2 old loclist. Still used. Ignores
            DWARF5.  Does not work well with DWARF4,
            but sort of works there.  */
        Dwarf_Signed sno = 0;
        lres = dwarf_loclist_n(attr, &llbufarray, &sno, llerr);
        if (lres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: dwarf_loclist_n fails",
                lres, *llerr);
            return lres;
        } else if (lres == DW_DLV_NO_ENTRY) {
            return lres;
        }
        no_of_elements = sno;
    }

    possibly_increase_esb_alloc(details, no_of_elements,100);
    for (llent = 0; llent < no_of_elements; ++llent) {
        Dwarf_Unsigned locdesc_offset = 0;
        Dwarf_Locdesc_c locentry = 0; /* 2015 */
        Dwarf_Unsigned rawlowpc = 0;
        Dwarf_Unsigned rawhipc = 0;
        Dwarf_Unsigned ulocentry_count = 0;
        Dwarf_Bool     debug_addr_unavailable = FALSE;
        /*  This has values DW_LKIND*, the same values
            that were in loclist source
            in 2019, but with
            the new value of DW_LKIND_loclists
            for DWARF5.  See libdwarf.h */
        Dwarf_Small loclist_source = 0;
        int no_ending_newline = FALSE;

        if (!glflags.gf_use_old_dwarf_loclist) {
            lres = dwarf_get_locdesc_entry_d(loclist_head,
                llent,
                &lle_value,
                &rawlowpc,&rawhipc,
                &debug_addr_unavailable,
                &lopc, &hipc,
                &ulocentry_count,
                &locentry,
                &loclist_source,
                &expr_section_offset,
                &locdesc_offset,
                llerr);
            if (lres == DW_DLV_ERROR) {
                print_error_and_continue(dbg,
                    "ERROR: dwarf_get_locdesc_entry_c fails",
                    lres, *llerr);
                return lres;
            } else if (lres == DW_DLV_NO_ENTRY) {
                return lres;
            }
            locentry_count = ulocentry_count;
        } else {
            llbuf = llbufarray[llent];
            rawlowpc = lopc = llbuf->ld_lopc;
            rawhipc  = hipc = llbuf->ld_hipc;
            loclist_source = llbuf->ld_from_loclist;
            expr_section_offset = llbuf->ld_section_offset;
            locdesc_offset = expr_section_offset -
                sizeof(Dwarf_Half) - 2 * address_size;
            locentry_count = llbuf->ld_cents;
            ulocentry_count = locentry_count;
            if (lopc == max_address) {
                lle_value = DW_LLE_base_address;
            } else if (lopc== 0 && hipc == 0) {
                lle_value = DW_LLE_end_of_list;
            } else {
                lle_value = DW_LLE_start_end;
            }
        }
        /*  If we have a location list refering to the .debug_loc
            Check for specific compiler we are validating. */
        if ( glflags.gf_check_locations && in_valid_code &&
            loclist_source && checking_this_compiler()) {
            checking = TRUE;
        }
        if (!glflags.dense &&
            loclist_source != DW_LKIND_expression) {
            if (llent == 0) {
                /* These messages go with the list of entries */
                switch(loclist_source){
                case DW_LKIND_loclist:
                    esb_append_printf_u(details,
                        "   <loclist at offset 0x%"
                        DW_PR_XZEROS DW_PR_DUx,
                        loclists_offset_lle_set);
                    esb_append_printf_i(details,
                        " with %ld entries follows>",
                        no_of_elements);
                    break;
                case DW_LKIND_GNU_exp_list:
                    esb_append_printf_u(details,
                        "   <dwo loclist at offset 0x%"
                        DW_PR_XZEROS DW_PR_DUx,
                        loclists_offset_lle_set);
                    esb_append_printf_i(details,
                        " with %ld entries follows>",
                        no_of_elements);
                    break;
                case DW_LKIND_loclists:
                    esb_append_printf_u(details,
                        "   <debug_loclists offset 0x%"
                        DW_PR_XZEROS DW_PR_DUx,
                        loclists_offset_lle_set);
                    esb_append_printf_i(details,
                        " with %ld entries follows>",
                        no_of_elements);
                    break;
                }
            }
            esb_append_printf_i(details, "\n   [%2d]",llent);
        } else {
            no_ending_newline = TRUE;
        }

        /*  When dwarf_debug_addr_index_to_addr() fails
            it is probably
            DW_DLE_MISSING_NEEDED_DEBUG_ADDR_SECTION 257
            (because no TIED file supplied)
            but we don't distinguish that from other errors here. */
        /*  We use DW_LLE names for DW_LKIND_loclist and
            DW_LKIND_loclists. We use LLEX names for
            DW_LKIND_GNU_exp_list */
        if(loclist_source) {
            Dwarf_Half tag = 0;
            Dwarf_Half attrnum = 0;
            const char *tagname = 0;
            const char *attrname = 0;
            int res = 0;

            res = dwarf_tag(die,&tag,llerr);
            if (res != DW_DLV_OK) {
                return res;
            }
            res = dwarf_whatattr(attr,&attrnum,llerr);
            if (res != DW_DLV_OK) {
                return res;
            }
            attrname = get_AT_name(attrnum,FALSE);
            tagname = get_TAG_name(tag,FALSE);
            if (loclist_source == DW_LKIND_GNU_exp_list) {
                print_llex_linecodes(dbg,
                    checking,
                    tagname,
                    attrname,
                    llent,
                    lle_value,
                    base_address,
                    rawlowpc, rawhipc,
                    debug_addr_unavailable,
                    lopc, hipc,
                    locdesc_offset,
                    details,
                    &bError);
            } else if (loclist_source == DW_LKIND_loclist) {
                print_original_loclist_linecodes(dbg,
                    checking,
                    tagname,
                    attrname,
                    llent,
                    lle_value,
                    base_address,
                    rawlowpc, rawhipc,
                    debug_addr_unavailable,
                    lopc, hipc,
                    locdesc_offset,
                    details,
                    &bError);
            } else {
                /* loclist_source == DW_LKIND_loclists */
                print_debug_loclists_linecodes(dbg,
                    checking,
                    tagname,
                    attrname,
                    llent,
                    lle_value,
                    base_address,
                    rawlowpc, rawhipc,
                    debug_addr_unavailable,
                    lopc, hipc,
                    locdesc_offset,
                    details,
                    &bError);
            }
        }
        {
            lres = dwarfdump_print_location_operations(dbg,
                /*  Either llbuf or locentry non-zero.
                    Not both. */
                llbuf,
                locentry,
                llent, /* Which loc desc this is */
                ulocentry_count, /* How many ops in this loc desc */
                loclist_source,
                no_ending_newline,
                base_address,
                details,llerr);
        }
        if (lres == DW_DLV_ERROR) {
            return lres;
        }
    }
    if (!no_end_newline) {
        if (bError &&  glflags.gf_check_verbose_mode &&
            PRINTING_UNIQUE) {
            esb_append(details,"\n");
        } else if (glflags.gf_do_print_dwarf) {
            esb_append(details,"\n");
        }
    }

    if (!glflags.gf_use_old_dwarf_loclist) {
        dwarf_loc_head_c_dealloc(loclist_head);
    } else {
        for (i = 0; i < no_of_elements; ++i) {
            dwarf_dealloc(dbg, llbufarray[i]->ld_s, DW_DLA_LOC_BLOCK);
            dwarf_dealloc(dbg, llbufarray[i], DW_DLA_LOCDESC);
        }
        dwarf_dealloc(dbg, llbufarray, DW_DLA_LIST);
    }
    return DW_DLV_OK;
}

/*  New October 2017.
    The 'decimal' representation here is questionable.
    */
static void
formx_data16(Dwarf_Form_Data16 * u,
    struct esb_s *esbp, Dwarf_Bool hex_format)
{
    unsigned i = 0;

    for( ; i < sizeof(Dwarf_Form_Data16); ++i){
        esb_append(esbp, "0x");
        if (hex_format) {
            esb_append_printf_u(esbp,
                "%02x ", u->fd_data[i]);
        } else {
            esb_append_printf_i(esbp, "%02d ", u->fd_data[i]);
        }
    }
}

static void
formx_unsigned(Dwarf_Unsigned u, struct esb_s *esbp, Dwarf_Bool hex_format)
{
    if (hex_format) {
        esb_append_printf_u(esbp,
            "0x%"  DW_PR_XZEROS DW_PR_DUx , u);
    } else {
        esb_append_printf_u(esbp,
            "%" DW_PR_DUu , u);
    }
}

static void
formx_signed(Dwarf_Signed s, struct esb_s *esbp)
{
    esb_append_printf_i(esbp, "%" DW_PR_DSd ,s);
}
static void
formx_unsigned_and_signed_if_neg(Dwarf_Unsigned tempud,
    Dwarf_Signed tempd,
    const char *leader,Dwarf_Bool hex_format,struct esb_s*esbp)
{
    formx_unsigned(tempud,esbp,hex_format);
    if(tempd < 0) {
        esb_append(esbp,leader);
        formx_signed(tempd,esbp);
        esb_append(esbp,")");
    }
}

/*  If the DIE DW_AT_type exists and is directly known signed/unsigned
    return -1 for signed 1 for unsigned.
    Otherwise return 0 meaning 'no information'.
    So we only need to a messy lookup once per type-die offset  */
static int
check_for_type_unsigned(Dwarf_Debug dbg,
    Dwarf_Die die,
    UNUSEDARG struct esb_s *esbp)
{
    Dwarf_Bool is_info = 0;
    struct Helpertree_Base_s * helperbase = 0;
    struct Helpertree_Map_Entry_s *e = 0;
    int res = 0;
    Dwarf_Attribute attr = 0;
    Dwarf_Attribute encodingattr = 0;
    Dwarf_Error error = 0;
    Dwarf_Unsigned diegoffset = 0;
    Dwarf_Unsigned typedieoffset = 0;
    Dwarf_Die typedie = 0;
    Dwarf_Unsigned tempud = 0;
    int show_form_here = FALSE;
    int retval = 0;

    if(!die) {
        return 0;
    }
    is_info = dwarf_get_die_infotypes_flag(die);
    if(is_info) {
        helperbase = &helpertree_offsets_base_info;
    } else {
        helperbase = &helpertree_offsets_base_types;
    }
    res = dwarf_dieoffset(die,&diegoffset,&error);
    if (res == DW_DLV_ERROR) {
        /* esb_append(esbp,"<helper dieoffset FAIL >"); */
        return 0;
    } else if (res == DW_DLV_NO_ENTRY) {
        /* We don't know sign. */
        /*esb_append(esbp,"<helper dieoffset NO ENTRY>"); */
        return 0;
    }
    /*  This might be wrong. See the typedieoffset check below,
        which is correct... */
    e = helpertree_find(diegoffset,helperbase);
    if(e) {
        /*bracket_hex("<helper FOUND offset ",diegoffset,">",esbp);
        bracket_hex("<helper FOUND val ",e->hm_val,">",esbp); */
        return e->hm_val;
    }

    /*  We look up the DW_AT_type die, if any, and
        use that offset to check for signedness. */

    res = dwarf_attr(die, DW_AT_type, &attr,&error);
    if (res == DW_DLV_ERROR) {
        /*bracket_hex("<helper dwarf_attr FAIL ",diegoffset,">",esbp); */
        helpertree_add_entry(diegoffset, 0,helperbase);
        return 0;
    } else if (res == DW_DLV_NO_ENTRY) {
        /* We don't know sign. */
        /*bracket_hex( "<helper dwarf_attr no entry ",diegoffset,">",esbp); */
        helpertree_add_entry(diegoffset, 0,helperbase);
        return 0;
    }
    res = dwarf_global_formref(attr, &typedieoffset,&error);
    if (res == DW_DLV_ERROR) {
        /*bracket_hex( "<helper global_formreff FAIL" ,diegoffset,">",esbp); */
        dwarf_dealloc_attribute(attr);
        helpertree_add_entry(diegoffset, 0,helperbase);
        return 0;
    } else if (res == DW_DLV_NO_ENTRY) {
        /*esb_append(esbp,"helper NO ENTRY  FAIL ");
        bracket_hex( "<helper global_formreff NO ENTRY" ,diegoffset,">",esbp); */
        dwarf_dealloc_attribute(attr);
        helpertree_add_entry(diegoffset, 0,helperbase);
        return 0;
    }
    dwarf_dealloc_attribute(attr);
    attr = 0;
    e = helpertree_find(typedieoffset,helperbase);
    if(e) {
        /*bracket_hex("<helper FOUND typedieoffset ",typedieoffset,">",esbp);
        bracket_hex("<helper FOUND val ",e->hm_val,">",esbp); */
        return e->hm_val;
    }

    res = dwarf_offdie_b(dbg,typedieoffset,is_info, &typedie,&error);
    if (res == DW_DLV_ERROR) {
        /*bracket_hex( "<helper dwarf_offdie_b  FAIL ",diegoffset,">",esbp); */
        helpertree_add_entry(diegoffset, 0,helperbase);
        helpertree_add_entry(typedieoffset, 0,helperbase);
        return 0;
    } else if (res == DW_DLV_NO_ENTRY) {
        /*bracket_hex( "<helper dwarf_offdie_b  NO ENTRY ",diegoffset,">",esbp); */
        helpertree_add_entry(diegoffset, 0,helperbase);
        helpertree_add_entry(typedieoffset, 0,helperbase);
        return 0;
    }
    res = dwarf_attr(typedie, DW_AT_encoding, &encodingattr,&error);
    if (res == DW_DLV_ERROR) {
        /*bracket_hex( "<helper dwarf_attr typedie  FAIL",diegoffset,">",esbp); */
        dwarf_dealloc_die(typedie);
        helpertree_add_entry(diegoffset, 0,helperbase);
        helpertree_add_entry(typedieoffset, 0,helperbase);
        return 0;
    } else if (res == DW_DLV_NO_ENTRY) {
        /*bracket_hex( "<helper dwarf_attr typedie  NO ENTRY",diegoffset,">",esbp);*/
        dwarf_dealloc_die(typedie);
        helpertree_add_entry(diegoffset, 0,helperbase);
        helpertree_add_entry(typedieoffset, 0,helperbase);
        return 0;
    }

    res = get_small_encoding_integer_and_name(dbg,
        encodingattr,
        &tempud,
        /* attrname */ (const char *) NULL,
        /* err_string */ ( struct esb_s *) NULL,
        (encoding_type_func) 0,
        &error,show_form_here);

    if (res != DW_DLV_OK) {
        DROP_ERROR_INSTANCE(dbg,res,error);
        /*bracket_hex( "<helper small encoding FAIL",diegoffset,">",esbp);*/
        /* dealloc attr *first* then die */
        dwarf_dealloc_attribute(encodingattr);
        dwarf_dealloc_die(typedie);
        helpertree_add_entry(diegoffset, 0,helperbase);
        helpertree_add_entry(typedieoffset, 0,helperbase);
        return 0;
    }
    if (tempud == DW_ATE_signed || tempud == DW_ATE_signed_char) {
        /*esb_append(esbp,"helper small encoding SIGNED ");*/
        retval = -1;
    } else {
        if (tempud == DW_ATE_unsigned || tempud == DW_ATE_unsigned_char) {
            /*esb_append(esbp,"helper small encoding UNSIGNED ");*/
            retval = 1;
        }
    }
    /*bracket_hex( "<helper ENTERED die",diegoffset,">",esbp);
    bracket_hex( "<helper ENTERED typedie",typedieoffset,">",esbp);*/
    helpertree_add_entry(diegoffset,retval,helperbase);
    helpertree_add_entry(typedieoffset, retval,helperbase);
    /* dealloc attr *first* then die */
    dwarf_dealloc_attribute(encodingattr);
    dwarf_dealloc_die(typedie);
    return retval;
}

/*  We think this is an integer. Figure out how to print it.
    In case the signedness is ambiguous (such as on
    DW_FORM_data1 (ie, unknown signedness) print two ways.

    If we were to look at DW_AT_type in the base DIE
    we could follow it and determine if the type
    was unsigned or signed (usually easily) and
    use that information.
*/
static int
formxdata_print_value(Dwarf_Debug dbg,
    Dwarf_Die die,
    Dwarf_Attribute attrib,
    Dwarf_Half theform,
    struct esb_s *esbp,
    Dwarf_Error * pverr, Dwarf_Bool hex_format)
{
    Dwarf_Signed tempsd = 0;
    Dwarf_Unsigned tempud = 0;
    int sres = 0;
    int ures = 0;

    if (theform == DW_FORM_data16) {
        Dwarf_Form_Data16 v16;
        ures = dwarf_formdata16(attrib,
            &v16,pverr);
        if (ures == DW_DLV_OK) {
            formx_data16(&v16,
                esbp,hex_format);
            return DW_DLV_OK;
        } else if (ures == DW_DLV_NO_ENTRY) {
            /* impossible */
            return ures;
        } else {
            return ures;
        }
    }
    ures = dwarf_formudata(attrib, &tempud, pverr);
    if (ures == DW_DLV_OK) {
        sres = dwarf_formsdata(attrib, &tempsd, pverr);
        if (sres == DW_DLV_OK) {
            if (tempud == (Dwarf_Unsigned)tempsd && tempsd >= 0) {
                /*  Data is the same value and not negative,
                    so makes no difference which
                    we print. */
                formx_unsigned(tempud,esbp,hex_format);
                return DW_DLV_OK;
            } else {
                /*  Here we don't know if signed or not and
                    Assuming one or the other changes the
                    interpretation of the bits. */
                int helpertree_unsigned = 0;

                helpertree_unsigned =
                    check_for_type_unsigned(dbg,die,esbp);
                if (!die || !helpertree_unsigned) {
                    /* Signedness unclear. */
                    formx_unsigned_and_signed_if_neg(tempud,tempsd,
                        " (",hex_format,esbp);
                    return DW_DLV_OK;
                } else if (helpertree_unsigned > 0) {
                    formx_unsigned(tempud,esbp,hex_format);
                    return DW_DLV_OK;
                } else {
                    /* Value signed. */
                    formx_signed(tempsd,esbp);
                    return DW_DLV_OK;
                }
            }
        } else if (sres == DW_DLV_NO_ENTRY) {
            formx_unsigned(tempud,esbp,hex_format);
            return DW_DLV_OK;
        } else /* DW_DLV_ERROR */{
            formx_unsigned(tempud,esbp,hex_format);
            DROP_ERROR_INSTANCE(dbg,sres,*pverr);
            return DW_DLV_OK;
        }
    } else if (ures == DW_DLV_ERROR) {
        DROP_ERROR_INSTANCE(dbg,ures,*pverr);
        sres = dwarf_formsdata(attrib, &tempsd, pverr);
        if (sres == DW_DLV_OK) {
            formx_signed(tempsd,esbp);
            return sres;
        } else if (sres == DW_DLV_ERROR) {
            esb_append_printf_u(esbp,
                "<ERROR: form 0x%x ",theform);
            esb_append(esbp,get_FORM_name(theform,FALSE));
            esb_append(esbp,
                " not readable signed or unsigned>");
            simple_err_only_return_action(sres,
                esb_get_string(esbp));
            /* Neither worked. */
            return DW_DLV_ERROR;
        }
    }
    /* NO_ENTRY is crazy, impossible. */
    return DW_DLV_NO_ENTRY;
}

static void
bracket_hex(const char *s1,
    Dwarf_Unsigned v,
    const char *s2,
    struct esb_s * esbp)
{
    Dwarf_Bool hex_format = TRUE;
    esb_append(esbp,s1);
    formx_unsigned(v,esbp,hex_format);
    esb_append(esbp,s2);
}

static int
print_exprloc_content(Dwarf_Debug dbg,Dwarf_Die die,
    Dwarf_Attribute attrib,
    boolean checking,
    UNUSEDARG int die_indent_level,
    UNUSEDARG int showhextoo,
    struct esb_s *esbp,Dwarf_Error* err)
{
    Dwarf_Ptr x = 0;
    Dwarf_Unsigned exprlength = 0;
    int wres = 0;

    wres = dwarf_formexprloc(attrib,&exprlength,&x,err);
    if (wres == DW_DLV_NO_ENTRY) {
        /* Impossible case... */
        print_error_and_continue(dbg,
            "Cannot get a  DW_FORM_exprloc...."
            "NO ENTRY! So not printable. "
            "Something is wrong",
            wres, *err);
        return wres;
    } else if (wres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "Cannot get a  DW_FORM_exprloc...."
            "so not printable. Something is wrong.",
            wres, *err);
        return wres;
    }
    {
        Dwarf_Half address_size = 0;
        Dwarf_Half offset_size = 0;
        Dwarf_Half version = 0;
        int ares = 0;

        {
            unsigned u = 0;
            esb_append_printf_u(esbp,
                "len 0x%04" DW_PR_DUx ": ",exprlength);
            if (showhextoo) {
                for (u = 0; u < exprlength; u++) {
                    if (!u) {
                        esb_append(esbp,"0x");
                    }
                    esb_append_printf_u(esbp,
                        "%02x", *(u + (unsigned char *) x));
                }
                esb_append(esbp,": ");
            }
        }
        /*  If this fails the DIE has no context or dbg
            attached. Very Very wrong. */
        ares = dwarf_get_version_of_die(die,&version,
            &offset_size);
        if (ares != DW_DLV_OK) {
            /* is DW_DLV_ERROR (see libdwarf query.c) */
            simple_err_only_return_action(ares,
                "\nERROR: die or context bad calling "
                "dwarf_get_version_of_die. Something"
                " is very wrong.");
            /*  Cannot return ERROR as we have no
                Dwarf_Error record. */
            return DW_DLV_NO_ENTRY;
        }
        ares = dwarf_get_die_address_size(die,
            &address_size,err);
        if (ares != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "Cannot get die address size for exprloc",
                ares,*err);
            return ares;
        }
        if (!checking) {
            int sres = 0;

            sres =  print_location_operations(dbg,x,
                exprlength,address_size,
                offset_size,version, esbp,err);
            if (sres == DW_DLV_ERROR) {
                glflags.gf_count_major_errors++;
                printf("\nERROR: Unable to create "
                    "expresssion location"
                    " with length 0x%" DW_PR_DUu
                    "as a string.\n",
                    exprlength);
            }
            return sres;
        }
    }
    return DW_DLV_OK;
}

/*  Borrow the definition from pro_encode_nm.h */
/*  Bytes needed to encode a number.
    Not a tight bound, just a reasonable bound.
*/
#ifndef ENCODE_SPACE_NEEDED
#define ENCODE_SPACE_NEEDED   (2*sizeof(Dwarf_Unsigned))
#endif /* ENCODE_SPACE_NEEDED */

/*  Table indexed by the attribute value;
    only standard attributes
    are included, ie. in the range [1..DW_AT_lo_user];
    we waste a
    little bit of space, but accessing the table is fast. */

typedef struct attr_encoding {
    Dwarf_Unsigned entries; /* Attribute occurrences */
    Dwarf_Unsigned formx;   /* Space used by current encoding */
    Dwarf_Unsigned leb128;  /* Space used with LEB128 encoding */
} a_attr_encoding;

/*  The other DW_FORM_datan are lower form values than data16,
    so the following is safe for the unchanging  static table. */
static int attributes_encoding_factor[DW_FORM_data16 + 1];

/*  These must be reset for each object if we are processing
    an archive! see print_attributes_encoding(). */
static a_attr_encoding *attributes_encoding_table = NULL;
static boolean attributes_encoding_do_init = TRUE;

/*  Check the potential amount of space wasted by
    attributes values that can
    be represented as an unsigned LEB128.
    Only attributes with forms:
    DW_FORM_data1, DW_FORM_data2, DW_FORM_data4 and
    DW_FORM_data are checked
*/
static void
check_attributes_encoding(Dwarf_Half attr,Dwarf_Half theform,
    Dwarf_Unsigned value)
{

    if (attributes_encoding_do_init) {
        /* Create table on first call */
        attributes_encoding_table = (a_attr_encoding *)calloc(DW_AT_lo_user,
            sizeof(a_attr_encoding));
        /* We use only 5 slots in the table, for quick access */
        attributes_encoding_factor[DW_FORM_data1] = 1; /* index 0x0b */
        attributes_encoding_factor[DW_FORM_data2] = 2; /* index 0x05 */
        attributes_encoding_factor[DW_FORM_data4] = 4; /* index 0x06 */
        attributes_encoding_factor[DW_FORM_data8] = 8; /* index 0x07 */
        attributes_encoding_factor[DW_FORM_data16] = 16;/* index 0x1e */
        attributes_encoding_do_init = FALSE;
    }

    /* Regardless of the encoding form, count the checks. */
    DWARF_CHECK_COUNT(attr_encoding_result,1);

    /*  For 'DW_AT_stmt_list', due to the way is generated, the value
        can be unknown at compile time and only the assembler can decide
        how to represent the offset; ignore this attribute. */
    if (DW_AT_stmt_list == attr ||
        DW_AT_macros == attr ||
        DW_AT_GNU_macros == attr) {
        return;
    }

    /*  Only checks those attributes that have DW_FORM_dataX:
        DW_FORM_data1, DW_FORM_data2, DW_FORM_data4 and DW_FORM_data8
        DWARF5 adds DW_FORM_data16, but we ignore data16 here
        as it makes no sense as a uleb. */
    if (theform == DW_FORM_data1 || theform == DW_FORM_data2 ||
        theform == DW_FORM_data4 || theform == DW_FORM_data8 ) {
        int res = 0;
        /*  Size of the byte stream buffer that needs to be
            memcpy-ed. */
        int leb128_size = 0;
        /* To encode the attribute value */
        char encode_buffer[ENCODE_SPACE_NEEDED];

        res = dwarf_encode_leb128(value,&leb128_size,
            encode_buffer,sizeof(encode_buffer));
        if (res == DW_DLV_OK) {
            if (attributes_encoding_factor[theform] > leb128_size) {
                int wasted_bytes = attributes_encoding_factor[theform]
                    - leb128_size;
                struct esb_s lesb;
                esb_constructor(&lesb);

                esb_append_printf_i(&lesb,
                    "%" DW_PR_DSd " wasted byte(s)",wasted_bytes);
                DWARF_CHECK_ERROR2(attr_encoding_result,
                    get_AT_name(attr,pd_dwarf_names_print_on_error),
                    esb_get_string(&lesb));
                esb_destructor(&lesb);
                /*  Add the optimized size to the specific
                    attribute, only if we are dealing with
                    a standard attribute. */
                if (attr < DW_AT_lo_user) {
                    attributes_encoding_table[attr].entries += 1;
                    attributes_encoding_table[attr].formx   +=
                        attributes_encoding_factor[theform];
                    attributes_encoding_table[attr].leb128  +=
                        leb128_size;
                }
            }
        }
        /* ignoring error, it should be impossible. */
    }
}

/* Print a detailed encoding usage per attribute */
int
print_attributes_encoding(Dwarf_Debug dbg,
    Dwarf_Error* attr_error)
{
    if (attributes_encoding_table) {
        boolean print_header = TRUE;
        Dwarf_Unsigned total_entries = 0;
        Dwarf_Unsigned total_bytes_formx = 0;
        Dwarf_Unsigned total_bytes_leb128 = 0;
        Dwarf_Unsigned entries = 0;
        Dwarf_Unsigned bytes_formx = 0;
        Dwarf_Unsigned bytes_leb128 = 0;
        int index;
        int count = 0;
        float saved_rate = 0.0;

        for (index = 0; index < DW_AT_lo_user; ++index) {
            if (attributes_encoding_table[index].leb128) {
                if (print_header) {
                    printf("\n*** SPACE USED BY ATTRIBUTE ENCODINGS ***\n");
                    printf("Nro Attribute Name            "
                        "   Entries     Data_x     leb128 Rate\n");
                    print_header = FALSE;
                }
                entries = attributes_encoding_table[index].entries;
                bytes_formx = attributes_encoding_table[index].formx;
                bytes_leb128 = attributes_encoding_table[index].leb128;
                total_entries += entries;
                total_bytes_formx += bytes_formx;
                total_bytes_leb128 += bytes_leb128;
                saved_rate = bytes_leb128 * 100 / bytes_formx;
                printf("%3d %-25s "
                    "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "   /* Entries */
                    "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "   /* FORMx */
                    "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "   /* LEB128 */
                    "%3.0f%%"
                    "\n",
                    ++count,
                    get_AT_name(index,pd_dwarf_names_print_on_error),
                    entries,
                    bytes_formx,
                    bytes_leb128,
                    saved_rate);
            }
        }
        if (!print_header) {
            /* At least we have an entry, print summary and percentage */
            Dwarf_Addr lower = 0;
            Dwarf_Unsigned size = 0;
            int infoerr = 0;

            saved_rate = total_bytes_leb128 * 100 / total_bytes_formx;
            printf("** Summary **                 "
                "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "  /* Entries */
                "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "  /* FORMx */
                "%10" /*DW_PR_XZEROS*/ DW_PR_DUu " "  /* LEB128 */
                "%3.0f%%"
                "\n",
                total_entries,
                total_bytes_formx,
                total_bytes_leb128,
                saved_rate);
            /*  Get .debug_info size (Very unlikely to have
                an error here). */
            infoerr = dwarf_get_section_info_by_name(dbg,
                ".debug_info",&lower,
                &size,attr_error);
            if (infoerr == DW_DLV_ERROR) {
                free(attributes_encoding_table);
                attributes_encoding_table = 0;
                attributes_encoding_do_init = TRUE;
                return infoerr;
            }
            saved_rate = (total_bytes_formx - total_bytes_leb128)
                * 100 / size;
            if (saved_rate > 0) {
                printf("\n** .debug_info size can be reduced "
                    "by %.0f%% **\n",
                    saved_rate);
            }
        }
        free(attributes_encoding_table);
        attributes_encoding_table = 0;
        attributes_encoding_do_init = TRUE;
    }
    return DW_DLV_OK;
}

static void
check_decl_file_only(char **srcfiles,
    Dwarf_Unsigned fileindex,Dwarf_Signed cnt,int attr)
{
    /*  Zero is always a legal index for
        DWARF2,3,4,5, it means
        no source name provided. */

    DWARF_CHECK_COUNT(decl_file_result,1);
    /* zero means no file applicable */
    if (fileindex  &&
        fileindex > ((Dwarf_Unsigned)cnt)) {
        struct esb_s msgb;

        esb_constructor(&msgb);
        if (!srcfiles) {
            esb_append(&msgb,
                "There is a file number=");
            esb_append_printf_u(&msgb,"%" DW_PR_DUu,fileindex);
            esb_append(&msgb," but no source files");
            /*  Extra space char here to avoid pointless
                regression test issues with older versions. */
            esb_append(&msgb,"  are known.");
        } else {
            esb_append(&msgb,"Does not index to valid file name ");
            esb_append(&msgb,"filenum=");
            esb_append_printf_u(&msgb,"%" DW_PR_DUu,fileindex);
            esb_append(&msgb," arraysize=");
            esb_append_printf_i(&msgb,"%" DW_PR_DSd,cnt);
            esb_append(&msgb,".");
        }
        DWARF_CHECK_ERROR2(decl_file_result,
            get_AT_name(attr, pd_dwarf_names_print_on_error),
            esb_get_string(&msgb));
        esb_destructor(&msgb);
    }
}

static int
expand_rnglist_entries(
    UNUSEDARG Dwarf_Die die,
    Dwarf_Rnglists_Head rnglhead,
    Dwarf_Unsigned rnglentriescount,
    Dwarf_Unsigned rnglglobal_offset,
    struct esb_s *  esbp,
    UNUSEDARG int show_form,
    int local_verbose,
    Dwarf_Error *err)
{
    Dwarf_Unsigned count = 0;
    Dwarf_Unsigned i = 0;
    int res = 0;
    Dwarf_Unsigned secoffset = rnglglobal_offset;
    Dwarf_Bool no_debug_addr_available = FALSE;

    count = rnglentriescount;
    if (local_verbose > 1) {
        esb_append(esbp,"\n      "
            "                                                 "
            "secoff");
    }
    for( ; i < count; ++i) {
        unsigned entrylen = 0;
        unsigned rle_code = 0;
        Dwarf_Unsigned raw1 = 0;
        Dwarf_Unsigned raw2 = 0;
        Dwarf_Unsigned cooked1 = 0;
        Dwarf_Unsigned cooked2 = 0;

        res  = dwarf_get_rnglists_entry_fields_a(rnglhead,i,
            &entrylen,&rle_code,
            &raw1,&raw2,
            &no_debug_addr_available,
            &cooked1,&cooked2,
            err);
        if (res != DW_DLV_OK) {
            return res;
        }
        if (local_verbose || no_debug_addr_available ) {
            const char *codename = "<unknown code>";

            dwarf_get_RLE_name(rle_code,&codename);
            esb_append(esbp,"\n      ");
            esb_append_printf_u(esbp,"[%2u] ",i);
            esb_append_printf_s(esbp,"%-20s ",codename);
            if (rle_code != DW_RLE_end_of_list) {
                esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,raw1);
                    esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,raw2);
            } else {
                esb_append(esbp,
                    "                      ");
            }
            if (local_verbose > 1) {
                esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,secoffset);
            }
            if (no_debug_addr_available) {
                esb_append(esbp,"no .debug_addr available");
            }
        }
        if (!no_debug_addr_available) {
            const char *codename = "start,end";

            esb_append(esbp,"\n      ");
            esb_append_printf_u(esbp,"[%2u] ",i);
            if (rle_code == DW_RLE_base_addressx ||
                rle_code == DW_RLE_base_address) {
                codename = "base address";
            } else {
                if (rle_code == DW_RLE_end_of_list) {
                    codename = "end of list";
                }
            }
            esb_append_printf_s(esbp,"%-20s ",codename);
            if (rle_code != DW_RLE_end_of_list) {
                esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,cooked1);
                esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,cooked2);
            } else {
                esb_append(esbp,
                "                      ");
            }
            if (local_verbose > 1) {
                esb_append_printf_u(esbp," 0x%"
                    DW_PR_XZEROS DW_PR_DUx ,secoffset);
            }
        }
        secoffset += entrylen;
    }
    esb_append(esbp,"\n");
    return DW_DLV_OK;
}



/* DWARF5 .debug_rnglists[.dwo] only. */
static int
handle_rnglists(Dwarf_Die die,
    Dwarf_Attribute attrib,
    Dwarf_Half theform,
    Dwarf_Unsigned attrval,
    Dwarf_Unsigned *output_rle_set_offset,
    struct esb_s *  esbp,
    int show_form,
    int local_verbose,
    Dwarf_Error *err)
{
    /* This is DWARF5, by definition. Not earlier. */
    Dwarf_Unsigned global_offset_of_rle_set = 0;
    Dwarf_Unsigned count_rnglists_entries = 0;
    Dwarf_Rnglists_Head rnglhead = 0;
    int res = 0;

    res = dwarf_rnglists_get_rle_head(attrib,
        theform,
        attrval, /* index val or offset depending on form */
        &rnglhead,
        &count_rnglists_entries,
        &global_offset_of_rle_set,
        err);
    if (res != DW_DLV_OK) {
        return res;
    }
    *output_rle_set_offset = global_offset_of_rle_set;
    /*  Here we put newline at start of each line
        of output, different from the usual practice */
    append_local_prefix(esbp);
    esb_append_printf_u(esbp,
        "Offset of rnglists entries: 0x%"
        DW_PR_XZEROS DW_PR_DUx,
        global_offset_of_rle_set);
    if (local_verbose > 1) {
        Dwarf_Unsigned version = 0;
        Dwarf_Unsigned context_index = 0;
        Dwarf_Unsigned rle_count = 0;
        Dwarf_Unsigned total_bytes_in_rle = 0;
        Dwarf_Half     loffset_size = 0;
        Dwarf_Half     laddress_size = 0;
        Dwarf_Half     lsegment_selector_size = 0;
        Dwarf_Unsigned section_offset_of_context = 0;
        Dwarf_Unsigned length_of_context = 0;
        Dwarf_Bool rnglists_base_present = 0;
        Dwarf_Unsigned rnglists_base = 0;
        Dwarf_Bool rnglists_base_address_present = 0;
        Dwarf_Unsigned rnglists_base_address = 0;
        Dwarf_Bool debug_addr_base_present = 0;
        Dwarf_Unsigned debug_addr_base;
        Dwarf_Unsigned offsets_table_offset = 0;
        Dwarf_Unsigned offsets_table_entrycount = 0;

        res = dwarf_get_rnglist_head_basics(rnglhead,
            &rle_count,
            &version,
            &context_index,
            &total_bytes_in_rle,
            &loffset_size,
            &laddress_size,
            &lsegment_selector_size,
            &section_offset_of_context,
            &length_of_context,
            &offsets_table_offset,
            &offsets_table_entrycount,
            &rnglists_base_present,&rnglists_base,
            &rnglists_base_address_present,&rnglists_base_address,
            &debug_addr_base_present,&debug_addr_base,
            err);
        if (res != DW_DLV_OK) {
            dwarf_dealloc_rnglists_head(rnglhead);
            return res;
        }
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "Index of rnglist head     : %u",
            context_index);

        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "rnglist head version      : %u",
            version);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "Record count rnglist set  : %u",
            rle_count);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "Bytes this rnglist set    : %u",
            total_bytes_in_rle);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "offset size               : %u",
            loffset_size);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "address size              : %u",
            laddress_size);

        if (rnglists_base_present) {
            append_local_prefix(esbp);
            esb_append_printf_u(esbp,
            "CU DW_AT_rnglists_base    : 0x%"
                DW_PR_XZEROS DW_PR_DUx,
            rnglists_base);
        }
        if (rnglists_base_address_present) {
            append_local_prefix(esbp);
            esb_append_printf_u(esbp,
            "CU DW_AT_low_pc (baseaddr): 0x%"
                DW_PR_XZEROS DW_PR_DUx,
                rnglists_base_address);
        }
        if (debug_addr_base_present) {
            append_local_prefix(esbp);
            esb_append_printf_u(esbp,
            "CU DW_AT_addr_base        : 0x%"
                DW_PR_XZEROS DW_PR_DUx,
                debug_addr_base);
        }
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "section offset CU rnglists: 0x%"
                DW_PR_XZEROS DW_PR_DUx,
            section_offset_of_context);
        append_local_prefix(esbp);
        esb_append_printf_u(esbp,
            "section length CU rnglists: 0x%"
            DW_PR_XZEROS DW_PR_DUx,
            length_of_context);
        esb_append_printf_u(esbp, " (%u)",
            length_of_context);
    }

    res = expand_rnglist_entries(die,rnglhead,
        count_rnglists_entries,
        global_offset_of_rle_set,
        esbp,
        show_form,local_verbose,err);
    dwarf_dealloc_rnglists_head(rnglhead);
    return res;
}

/*  Fill buffer with attribute value.
    We pass in tag so we can try to do the right thing with
    broken compiler DW_TAG_enumerator

    'cnt' is signed for historical reasons (a mistake
    in an interface), but the value is never negative.

    We append to esbp's buffer.
*/
int
get_attr_value(Dwarf_Debug dbg, Dwarf_Half tag,
    Dwarf_Die die,
    Dwarf_Off dieprint_cu_goffset,
    Dwarf_Attribute attrib,
    char **srcfiles, Dwarf_Signed cnt, struct esb_s *esbp,
    int show_form,
    int local_verbose,
    Dwarf_Error *err)
{
    Dwarf_Half theform = 0;
    char * temps = 0;
    Dwarf_Block *tempb = 0;
    Dwarf_Signed tempsd = 0;
    Dwarf_Unsigned tempud = 0;
    Dwarf_Off off = 0;
    Dwarf_Die die_for_check = 0;
    Dwarf_Half tag_for_check = 0;
    Dwarf_Addr addr = 0;
    int fres = 0;
    int bres = 0;
    int wres = 0;
    int dres = 0;
    Dwarf_Half direct_form = 0;
    Dwarf_Bool is_info = TRUE;
    boolean checking = !PRINTING_DIES;

    is_info = dwarf_get_die_infotypes_flag(die);
    /*  Dwarf_whatform gets the real form, DW_FORM_indir is
        never returned: instead the real form following
        DW_FORM_indir is returned. */
    fres = dwarf_whatform(attrib, &theform, err);
    /*  Depending on the form and the attribute, process the form. */
    if (fres == DW_DLV_ERROR) {
        print_error_and_continue(dbg,
            "dwarf_whatform cannot Find Attr Form",
            fres, *err);
        return fres;
    } else if (fres == DW_DLV_NO_ENTRY) {
        return fres;
    }
    /*  dwarf_whatform_direct gets the 'direct' form, so if
        the form is DW_FORM_indir that is what is returned. */
    fres  = dwarf_whatform_direct(attrib, &direct_form, err);
    DROP_ERROR_INSTANCE(dbg,fres,*err);
    /*  Ignore errors in dwarf_whatform_direct() */

    switch (theform) {
    case DW_FORM_GNU_addr_index:
    case DW_FORM_addrx:
    case DW_FORM_addrx1    :  /* DWARF5 */
    case DW_FORM_addrx2    :  /* DWARF5 */
    case DW_FORM_addrx3    :  /* DWARF5 */
    case DW_FORM_addrx4    :  /* DWARF5 */
    case DW_FORM_addr:
        bres = dwarf_formaddr(attrib, &addr, err);
        if (bres == DW_DLV_OK) {
            if (dwarf_addr_form_is_indexed(theform)) {
                Dwarf_Unsigned index = 0;
                int res = dwarf_get_debug_addr_index
                    (attrib,&index,err);
                if(res != DW_DLV_OK) {
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,"ERROR: getting debug addr"
                        " index on form ");
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," missing index?!");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        res, *err);
                    esb_destructor(&lstr);
                    return res;
                }
                bracket_hex("(addr_index: ",index, ")",esbp);
            }
            bracket_hex("",addr,"",esbp);
        } else if (bres == DW_DLV_ERROR) {
            if (DW_DLE_MISSING_NEEDED_DEBUG_ADDR_SECTION ==
                dwarf_errno(*err)) {
                Dwarf_Unsigned index = 0;
                int res = 0;

                DROP_ERROR_INSTANCE(dbg,bres,*err);
                glflags.gf_debug_addr_missing_search_by_address = 1;
                res = dwarf_get_debug_addr_index(attrib,&index,
                    err);
                if(res != DW_DLV_OK) {
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," missing index. ?!");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        res, *err);
                    esb_destructor(&lstr);
                    return res;
                }
                addr = 0;
                bracket_hex("(addr_index: ",index,
                    ")<no .debug_addr section>",esbp);
                /*  This is normal in a .dwo/DWP file. The .debug_addr
                    is in a .o and in the final executable. */
            } else {
                /* Some bad error. */
                struct esb_s lstr;

                esb_constructor(&lstr);
                esb_append(&lstr,get_FORM_name(theform,FALSE));
                esb_append(&lstr," form with no addr ?!");
                print_error_and_continue(dbg,
                    esb_get_string(&lstr),
                    bres, *err);
                esb_destructor(&lstr);
                if(!glflags.gf_error_code_in_name_search_by_address) {
                    glflags.gf_error_code_in_name_search_by_address =
                        dwarf_errno(*err);
                }
                return bres;
            }
        } else { /* DW_DLV_NO_ENTRY */
            struct esb_s lstr;

            esb_constructor(&lstr);
            esb_append(&lstr,get_FORM_name(theform,FALSE));
            esb_append(&lstr," is a DW_DLV_NO_ENTRY? "
                "something is wrong.");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                bres, *err);
            esb_destructor(&lstr);
            return bres;
        }
        break;
    case DW_FORM_ref_addr:
        {
        Dwarf_Half attr = 0;
        /*  DW_FORM_ref_addr is not accessed thru formref: ** it is an
            address (global section offset) in ** the .debug_info
            section. */
        bres = dwarf_global_formref(attrib, &off, err);
        if (bres == DW_DLV_OK) {
            bracket_hex("<GOFF=",off, ">",esbp);
        } else {
            print_error_and_continue(dbg,
                "DW_FORM_ref_addr form with no reference?!"
                " Something is corrupted.",
                bres, *err);
            return bres;
        }
        wres = dwarf_whatattr(attrib, &attr, err);
        if (wres != DW_DLV_OK) {
            print_error_and_continue(dbg,
                "DW_FORM_ref_addr no attribute number?!"
                " Something is corrupted.",
                wres, *err);
            return wres;
        }
        {
            if (attr == DW_AT_sibling) {
                /*  The value had better be inside the current CU
                    else there is a nasty error here, as a sibling
                    has to be in the same CU, it seems. */
                /*  The target offset (off) had better be
                    following the die's global offset else
                    we have a serious botch. this FORM
                    defines the value as a .debug_info
                    global offset. */
                Dwarf_Off cuoff = 0;
                Dwarf_Off culen = 0;
                Dwarf_Off die_overall_offset = 0;
                int res = 0;
                int ores = dwarf_dieoffset(die, &die_overall_offset,
                    err);
                if (ores != DW_DLV_OK) {
                    print_error_and_continue(dbg,
                        "dwarf_dieoffsetnot available for"
                        "DW_AT_sibling. Something is corrupted.",
                        ores, *err);
                    return ores;
                }
                SET_DIE_STACK_SIBLING(off);
                if (die_overall_offset >= off) {
                    struct esb_s msg;

                    esb_constructor(&msg);
                    esb_append_printf_u(&msg,
                        "ERROR: Sibling DW_FORM_ref_offset 0x%"
                        DW_PR_XZEROS DW_PR_DUx ,
                        off);
                    esb_append_printf_s(&msg,
                        " points %s die Global offset ",
                        (die_overall_offset == off)?"at":"before");
                    esb_append_printf_u(&msg,
                        "0x%"  DW_PR_XZEROS  DW_PR_DUx,
                        die_overall_offset);
                    simple_err_only_return_action(DW_DLV_ERROR,
                        esb_get_string(&msg));
                    esb_destructor(&msg);
                }

                DWARF_CHECK_COUNT(tag_tree_result,1);
                res = dwarf_die_CU_offset_range(die,&cuoff,
                    &culen,err);
                if (res != DW_DLV_OK) {
                    DWARF_CHECK_ERROR(tag_tree_result,
                        "DW_AT_sibling DW_FORM_ref_addr "
                        "offset range of the CU "
                        "is not available");
                    DROP_ERROR_INSTANCE(dbg,res,*err);
                } else {
                    Dwarf_Off cuend = cuoff+culen;
                    if (off <  cuoff || off >= cuend) {
                        DWARF_CHECK_ERROR(tag_tree_result,
                            "DW_AT_sibling DW_FORM_ref_addr "
                            "offset points "
                            "outside of current CU");
                    }
                }
            }
        }
        }
        break;
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
        {
        int refres = 0;
        Dwarf_Half attr = 0;
        Dwarf_Off goff = 0; /* Global offset */

        /* CU-relative offset returned. */
        refres = dwarf_formref(attrib, &off, err);
        if (refres != DW_DLV_OK) {
            struct esb_s msg;

            esb_constructor(&msg);
            /* Report incorrect offset */
            esb_append_printf_s(&msg,
                "reference form %s with no valid local ref?!: %s ",
                get_AT_name(attr,FALSE));
            esb_append_printf_u(&msg,
                ", for offset=<0x%"  DW_PR_XZEROS  DW_PR_DUx ">",
                off);
            print_error_and_continue(dbg,
                esb_get_string(&msg),refres,*err);
            esb_destructor(&msg);
            return refres;
        }

        refres = dwarf_whatattr(attrib, &attr, err);
        if (refres != DW_DLV_OK) {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,get_AT_name(attr,FALSE));
            esb_append(&lstr,
                " is an attribute with no number? Impossible");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),refres,*err);
            esb_destructor(&lstr);
            return refres;
        }

        /*  Convert the local offset 'off' into a global section
            offset 'goff'. */
        refres = dwarf_convert_to_global_offset(attrib,
            off, &goff, err);
        if (refres != DW_DLV_OK) {
            /*  Report incorrect offset */
            struct esb_s msg;

            esb_constructor(&msg);
            esb_append_printf_u(&msg,
                "invalid offset"
                ",off=<0x%"  DW_PR_XZEROS  DW_PR_DUx "> ",
                off);
            esb_append(&msg,"attr: ");
            esb_append(&msg,get_AT_name(attr,FALSE));
            esb_append(&msg,"local offset has no global offset! ");
            print_error_and_continue(dbg,
                esb_get_string(&msg), refres, *err);
            esb_destructor(&msg);
            return refres;
        }
        if (attr == DW_AT_sibling) {
            /*  The value had better be inside the current CU
                else there is a nasty error here, as a sibling
                has to be in the same CU, it seems.
                The target offset (off) had better be
                following the die's global offset else
                we have a serious botch. this FORM
                defines the value as a .debug_info
                global offset. */
            Dwarf_Off die_overall_offset = 0;
            int ores = dwarf_dieoffset(die, &die_overall_offset,err);
            if (ores != DW_DLV_OK) {
                struct esb_s lstr;
                esb_constructor(&lstr);
                esb_append(&lstr,"DW_AT_sibling attr and DIE has");
                esb_append(&lstr,"no die_offset?");
                print_error_and_continue(dbg,
                    esb_get_string(&lstr),
                    ores, *err);
                esb_destructor(&lstr);
                return ores;
            }
            SET_DIE_STACK_SIBLING(goff);
            if (die_overall_offset >= goff) {
                struct esb_s lstr;
                esb_constructor(&lstr);
                esb_append(&lstr,"ERROR in DW_AT_sibling: ");
                esb_append(&lstr,get_FORM_name(theform,FALSE));
                esb_append_printf_u(&lstr,
                    " Sibling offset 0x%"  DW_PR_XZEROS  DW_PR_DUx
                    " points ",
                    goff);
                esb_append(&lstr,
                    (die_overall_offset == goff)?"at":"before");
                esb_append_printf_u(&lstr,
                    " its own die GOFF="
                    "0x%"  DW_PR_XZEROS  DW_PR_DUx,
                    die_overall_offset);
                simple_err_only_return_action(DW_DLV_ERROR,
                    esb_get_string(&lstr));
                esb_destructor(&lstr);
                /*  At present no way to create a Dwarf_Error
                    inside dwarfdump. */
            }

        }

        /*  Do references inside <> to distinguish them ** from
            constants. In dense form this results in <<>>. Ugly for
            dense form, but better than ambiguous. davea 9/94 */
        if (glflags.gf_show_global_offsets) {
            bracket_hex("<",off,"",esbp);
            bracket_hex(" GOFF=",goff,">",esbp);
        } else {
            bracket_hex("<",off,">",esbp);
        }

        if (glflags.gf_check_type_offset) {
            if (attr == DW_AT_type &&
                form_refers_local_info(theform)) {
                dres = dwarf_offdie_b(dbg, goff,
                    is_info,
                    &die_for_check, err);
                if (dres != DW_DLV_OK) {
                    struct esb_s msgc;

                    esb_constructor(&msgc);
                    esb_append_printf_u(&msgc,
                        "DW_AT_type offset does not point to a DIE "
                        "for global offset 0x%"
                        DW_PR_XZEROS DW_PR_DUx, goff);
                    esb_append_printf_u(&msgc,
                        " cu off 0x%" DW_PR_XZEROS DW_PR_DUx,
                        dieprint_cu_goffset);
                    esb_append_printf_u(&msgc,
                        " local offset 0x%" DW_PR_XZEROS DW_PR_DUx,
                        off);
                    esb_append_printf_u(&msgc,
                        " tag 0x%x",tag);

                    DWARF_CHECK_ERROR(type_offset_result,
                        esb_get_string(&msgc));
                    DROP_ERROR_INSTANCE(dbg,dres,*err);
                    esb_destructor(&msgc);
                } else {
                    int tres2 =
                        dwarf_tag(die_for_check, &tag_for_check, err);
                    if (tres2 == DW_DLV_OK) {
                        switch (tag_for_check) {
                        case DW_TAG_array_type:
                        case DW_TAG_class_type:
                        case DW_TAG_enumeration_type:
                        case DW_TAG_pointer_type:
                        case DW_TAG_reference_type:
                        case DW_TAG_rvalue_reference_type:
                        case DW_TAG_restrict_type:
                        case DW_TAG_string_type:
                        case DW_TAG_structure_type:
                        case DW_TAG_subroutine_type:
                        case DW_TAG_typedef:
                        case DW_TAG_union_type:
                        case DW_TAG_ptr_to_member_type:
                        case DW_TAG_set_type:
                        case DW_TAG_subrange_type:
                        case DW_TAG_base_type:
                        case DW_TAG_const_type:
                        case DW_TAG_file_type:
                        case DW_TAG_packed_type:
                        case DW_TAG_thrown_type:
                        case DW_TAG_volatile_type:
                        case DW_TAG_template_type_parameter:
                        case DW_TAG_template_value_parameter:
                        case DW_TAG_unspecified_type:
                        /* Template alias */
                        case DW_TAG_template_alias:
                            /* OK */
                            break;
                        default:
                            {
                                struct esb_s msga;

                                esb_constructor(&msga);
                                esb_append_printf_u(&msga,
                                    "DW_AT_type offset "
                                    "0x%" DW_PR_XZEROS DW_PR_DUx,
                                    goff);
                                esb_append_printf_u(&msga,
                                    " does not point to Type"
                                    " info we got tag 0x%x ",
                                    tag_for_check);
                                esb_append(&msga,
                                    get_TAG_name(tag_for_check,
                                    pd_dwarf_names_print_on_error));
                                DWARF_CHECK_ERROR(type_offset_result,
                                    esb_get_string(&msga));
                                esb_destructor(&msga);
                            }
                            break;
                        }
                        dwarf_dealloc_die(die_for_check);
                        die_for_check = 0;
                    } else {
                        DWARF_CHECK_ERROR(type_offset_result,
                            "DW_AT_type offset does not exist");
                        DROP_ERROR_INSTANCE(dbg,tres2,*err);
                    }
                }
            }
        }
        }
        break;
    case DW_FORM_block:
    case DW_FORM_block1:
    case DW_FORM_block2:
    case DW_FORM_block4:
        fres = dwarf_formblock(attrib, &tempb, err);
        if (fres == DW_DLV_OK) {
            unsigned u = 0;

            if (tempb->bl_len) {
                esb_append(esbp,"0x");
            }
            for (u = 0; u < tempb->bl_len; u++) {
                esb_append_printf_u(esbp,
                    "%02x",
                    *(u + (unsigned char *) tempb->bl_data));
            }
            if (tempb->bl_len) {
                esb_append(esbp," ");
            }
            dwarf_dealloc(dbg, tempb, DW_DLA_BLOCK);
            tempb = 0;
        } else {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,"Form form ");
            esb_append(&lstr,get_FORM_name(theform,FALSE));
            esb_append(&lstr," cannot get block");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                fres, *err);
            esb_destructor(&lstr);
            return fres;
        }
        break;
    case DW_FORM_data1:
    case DW_FORM_data2:
    case DW_FORM_data4:
    case DW_FORM_data8:
    case DW_FORM_data16:
        {
        Dwarf_Half attr = 0;
        fres = dwarf_whatattr(attrib, &attr, err);
        if (fres != DW_DLV_OK) {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,"Form ");
            esb_append(&lstr,get_FORM_name(theform,FALSE));
            esb_append(&lstr," cannot get attribute");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                fres, *err);
            esb_destructor(&lstr);
            return fres;
        } else {
            switch (attr) {
            case DW_AT_ordering:
            case DW_AT_byte_size:
            case DW_AT_bit_offset:
            case DW_AT_bit_size:
            case DW_AT_inline:
            case DW_AT_language:
            case DW_AT_visibility:
            case DW_AT_virtuality:
            case DW_AT_accessibility:
            case DW_AT_address_class:
            case DW_AT_calling_convention:
            case DW_AT_discr_list:      /* DWARF2 */
            case DW_AT_encoding:
            case DW_AT_identifier_case:
            case DW_AT_MIPS_loop_unroll_factor:
            case DW_AT_MIPS_software_pipeline_depth:
            case DW_AT_decl_column:
            case DW_AT_decl_file:
            case DW_AT_decl_line:
            case DW_AT_call_column:
            case DW_AT_call_file:
            case DW_AT_call_line:
            case DW_AT_start_scope:
            case DW_AT_byte_stride:
            case DW_AT_bit_stride:
            case DW_AT_count:
            case DW_AT_stmt_list:
            case DW_AT_MIPS_fde:
                {  int show_form_here = 0;
                wres = get_small_encoding_integer_and_name(dbg,
                    attrib,
                    &tempud,
                    /* attrname */ (const char *) NULL,
                    /* err_string */ ( struct esb_s *) NULL,
                    (encoding_type_func) 0,
                    err,show_form_here);

                if (wres == DW_DLV_OK) {
                    Dwarf_Bool hex_format = TRUE;
                    Dwarf_Half dwversion = 0;

                    formx_unsigned(tempud,esbp,hex_format);
                    /* Check attribute encoding */
                    if (glflags.gf_check_attr_encoding) {
                        check_attributes_encoding(attr,theform,tempud);
                    }
                    if (attr == DW_AT_decl_file ||
                        attr == DW_AT_call_file) {
                        Dwarf_Half offset_size=0;
                        int vres = 0;
                        char declmsgbuf[ESB_FIXED_ALLOC_SIZE];
                        struct esb_s declmsg;
                        char *fname = 0;

                        esb_constructor_fixed(&declmsg,declmsgbuf,
                            sizeof(declmsgbuf));
                        vres = dwarf_get_version_of_die(die,
                            &dwversion,&offset_size);
                        if (srcfiles && vres == DW_DLV_OK) {
                            /*  srcfiles is indexed starting at 0, but
                                DW_AT_decl_file defines that 0 means no
                                file, so tempud 1 means the 0th entry in
                                srcfiles, thus tempud-1 is the correct */
                            if (!tempud) {
                                /* Just print the number,
                                    there is noname. */
                            } else if (tempud > 0 &&
                                tempud <= (Dwarf_Unsigned)cnt) {
                                fname = srcfiles[tempud - 1];
                                esb_append(&declmsg,fname);
                            } else {
                                esb_append(&declmsg,
                                    " <DW_AT_decl_file index ");
                                esb_append_printf_u(&declmsg,
                                    "%" DW_PR_DUu,tempud);
                                esb_append(&declmsg," out of range>");
                            }
                        } else if (vres != DW_DLV_OK) {
                            struct esb_s m;

                            esb_constructor(&m);
                            esb_append_printf_s(&m,
                                "ERROR: Cannot get DIE context "
                                "version number for form %s ",
                                get_FORM_name(theform, FALSE));
                            print_error_and_continue(dbg,
                                esb_get_string(&m),vres,*err);
                            esb_destructor(&m);
                            return vres;
                        }
                        if (fname) {
                            esb_append(esbp, " ");
                            esb_append(esbp, esb_get_string(&declmsg));
                        }
                        esb_destructor(&declmsg);

                        /*  Validate integrity of file indices
                            referenced in .debug_line */
                        if (glflags.gf_check_decl_file) {
                            check_decl_file_only(srcfiles,
                                tempud,cnt,attr);
                        }
                    } /* end decl_file and  call_file  processing */
                } else { /* not DW_DLV_OK on get small encoding */
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,"For form ");
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," and attribute ");
                    esb_append(&lstr,get_AT_name(attr,FALSE));
                    esb_append(&lstr," Cannot get encoding attribute");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        wres, *err);
                    esb_destructor(&lstr);
                    return wres;
                }
                }
                break;
            case DW_AT_const_value:
                /* Do not use hexadecimal format */
                wres = formxdata_print_value(dbg,die,attrib,
                    theform,esbp, err, FALSE);
                if (wres == DW_DLV_OK){
                    /* String appended already. */
                } else if (wres == DW_DLV_NO_ENTRY) {
                    /* nothing? */
                } else {
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,"For form ");
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," and attribute ");
                    esb_append(&lstr,get_AT_name(attr,FALSE));
                    esb_append(&lstr," Cannot get const value ");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        wres, *err);
                    esb_destructor(&lstr);
                    return wres;
                }
                break;
            case DW_AT_GNU_dwo_id:
            case DW_AT_GNU_odr_signature:
            case DW_AT_dwo_id:
                {
                Dwarf_Sig8 v;

                v = zerosig;
                wres = dwarf_formsig8_const(attrib,&v,err);
                if (wres == DW_DLV_OK){
                    struct esb_s t;

                    esb_constructor(&t);
                    format_sig8_string(&v,&t);
                    esb_append(esbp,esb_get_string(&t));
                    esb_destructor(&t);
                } else if (wres == DW_DLV_NO_ENTRY) {
                    /* nothing? */
                    esb_append(esbp,"Impossible: no entry for ");
                    esb_append(esbp,get_FORM_name(theform,FALSE));
                    esb_append(esbp," dwo_id");
                } else {
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,"For form ");
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," and attribute ");
                    esb_append(&lstr,get_AT_name(attr,FALSE));
                    esb_append(&lstr," Cannot get  Dwarf_Sig8 value ");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        wres, *err);
                    esb_destructor(&lstr);
                    return wres;
                }
                }
                break;
            case DW_AT_upper_bound:
            case DW_AT_lower_bound:
            default:  {
                Dwarf_Bool chex = FALSE;
                Dwarf_Die  tdie = die;
                if (DW_AT_ranges        == attr ||
                    DW_AT_location      == attr ||
                    DW_AT_vtable_elem_location == attr ||
                    DW_AT_string_length == attr ||
                    DW_AT_return_addr   == attr ||
                    DW_AT_use_location  == attr ||
                    DW_AT_static_link   == attr||
                    DW_AT_frame_base    == attr) {
                    /*  Do not look for data
                        type for unsigned/signed.
                        and do use HEX. */
                    chex = TRUE;
                    tdie = NULL;
                }
                /* Do not use hexadecimal format except for
                    DW_AT_ranges. */
                wres = formxdata_print_value(dbg,
                    tdie,attrib,
                    theform,esbp, err, chex);
                if (wres == DW_DLV_OK) {
                    /* String appended already. */
                } else if (wres == DW_DLV_NO_ENTRY) {
                    /* nothing? */
                } else {
                    struct esb_s lstr;
                    esb_constructor(&lstr);
                    esb_append(&lstr,"For form ");
                    esb_append(&lstr,get_FORM_name(theform,FALSE));
                    esb_append(&lstr," and attribute ");
                    esb_append(&lstr,get_AT_name(attr,FALSE));
                    esb_append(&lstr," Cannot get  Dwarf_Sig8 value ");
                    print_error_and_continue(dbg,
                        esb_get_string(&lstr),
                        wres, *err);
                    esb_destructor(&lstr);
                    return wres;
                }
                }
                break;
            }
        }
        if (glflags.gf_cu_name_flag) {
            if (attr == DW_AT_MIPS_fde) {
                if (glflags.fde_offset_for_cu_low == DW_DLV_BADOFFSET) {
                    glflags.fde_offset_for_cu_low
                        = glflags.fde_offset_for_cu_high = tempud;
                } else if (tempud < glflags.fde_offset_for_cu_low) {
                    glflags.fde_offset_for_cu_low = tempud;
                } else if (tempud > glflags.fde_offset_for_cu_high) {
                    glflags.fde_offset_for_cu_high = tempud;
                }
            }
        }
        }
        break;
    case DW_FORM_sdata:
        wres = dwarf_formsdata(attrib, &tempsd, err);
        if (wres == DW_DLV_OK) {
            Dwarf_Bool hxform=TRUE;
            tempud = tempsd;
            formx_unsigned_and_signed_if_neg(tempud,tempsd,
                " (",hxform,esbp);
        } else if (wres == DW_DLV_NO_ENTRY) {
            /* nothing? */
        } else {
            print_error_and_continue(dbg,
                "Cannot get DW_FORM_sdata value..",
                wres, *err);
            return wres;
        }
        break;
    case DW_FORM_udata:
        wres = dwarf_formudata(attrib, &tempud, err);
        if (wres == DW_DLV_OK) {
            Dwarf_Bool hex_format = TRUE;
            formx_unsigned(tempud,esbp,hex_format);
        } else if (wres == DW_DLV_NO_ENTRY) {
            /* nothing? */
        } else {
            print_error_and_continue(dbg,
                "Cannot get DW_FORM_udata value..",
                wres, *err);
            return wres;
        }
        break;
    /* various forms for strings. */
    case DW_FORM_string:
    case DW_FORM_strp:
    case DW_FORM_strx:   /* DWARF5 */
    case DW_FORM_strx1:  /* DWARF5 */
    case DW_FORM_strx2:  /* DWARF5 */
    case DW_FORM_strx3:  /* DWARF5 */
    case DW_FORM_strx4:  /* DWARF5 */
    case DW_FORM_strp_sup: /* DWARF5 String in altrnt: tied file */
    case DW_FORM_GNU_strp_alt: /* String in altrnt: tied file */
    case DW_FORM_line_strp: /* DWARF5, offset to .debug_line_str */
        /*  unsigned offset in size of an offset */
    case DW_FORM_GNU_str_index: {
        int sres = dwarf_formstring(attrib, &temps, err);
        if (sres == DW_DLV_OK) {
            if (theform == DW_FORM_strx ||
                theform == DW_FORM_strx1 ||
                theform == DW_FORM_strx2 ||
                theform == DW_FORM_strx3 ||
                theform == DW_FORM_strx4 ||
                theform == DW_FORM_GNU_str_index) {
                char saverbuf[ESB_FIXED_ALLOC_SIZE];
                struct esb_s saver;
                Dwarf_Unsigned index = 0;

                esb_constructor_fixed(&saver,saverbuf,
                    sizeof(saverbuf));
                sres = dwarf_get_debug_str_index(attrib,&index,err);
                esb_append(&saver,temps);
                if(sres == DW_DLV_OK) {
                    bracket_hex("(indexed string: ",index,")",esbp);
                } else {
                    DROP_ERROR_INSTANCE(dbg,sres,*err);
                    esb_append(esbp,
                        "(ERROR: indexed string:no string provided?)");
                }
                esb_append(esbp, esb_get_string(&saver));
                esb_destructor(&saver);
            } else {
                esb_append(esbp,temps);
            }
        } else if (sres == DW_DLV_NO_ENTRY) {
            if (theform == DW_FORM_strx ||
                theform == DW_FORM_GNU_str_index ||
                theform == DW_FORM_strx1 ||
                theform == DW_FORM_strx2 ||
                theform == DW_FORM_strx3 ||
                theform == DW_FORM_strx4 ) {
                esb_append(esbp, "(indexed string,no string provided?)");
            } else {
                esb_append(esbp, "<no string provided?>");
            }
        } else { /* DW_DLV_ERROR */
            if (theform == DW_FORM_strx ||
                theform == DW_FORM_GNU_str_index ||
                theform == DW_FORM_strx1 ||
                theform == DW_FORM_strx2 ||
                theform == DW_FORM_strx3 ||
                theform == DW_FORM_strx4 ) {
                struct esb_s lstr;

                esb_constructor(&lstr);
                esb_append(&lstr,"Cannot get an indexed string on ");
                esb_append(&lstr,get_FORM_name(theform,FALSE));
                esb_append(&lstr,"....");
                print_error_and_continue(dbg,
                    esb_get_string(&lstr),
                    sres, *err);
                esb_destructor(&lstr);
                return sres;
            }
            {
                struct esb_s lstr;

                esb_constructor(&lstr);
                esb_append(&lstr,"Cannot get the form on ");
                esb_append(&lstr,get_FORM_name(theform,FALSE));
                esb_append(&lstr,"....");
                print_error_and_continue(dbg,
                    esb_get_string(&lstr),
                    sres, *err);
                esb_destructor(&lstr);
                return sres;
            }
        }
        }
        break;
    case DW_FORM_flag: {
        Dwarf_Bool tempbool = 0;
        wres = dwarf_formflag(attrib, &tempbool, err);
        if (wres == DW_DLV_OK) {
            if (tempbool) {
                esb_append_printf_i(esbp,
                    "yes(%d)", tempbool);
            } else {
                esb_append(esbp,"no");
            }
        } else if (wres == DW_DLV_NO_ENTRY) {
            /* nothing? */
        } else {
            print_error_and_continue(dbg,
                "Cannot get formflag/p....", wres, *err);
            return wres;
        }
        }
        break;
    case DW_FORM_indirect:
        /*  We should not ever get here, since the true form was
            determined and direct_form has the DW_FORM_indirect if it is
            used here in this attr. */
        esb_append(esbp, get_FORM_name(theform,
            pd_dwarf_names_print_on_error));
        break;
    case DW_FORM_exprloc: {    /* DWARF4 */
        int showhextoo = 1;

        wres = print_exprloc_content(dbg,die,attrib,
            checking,
            0, /* die_indent_level pointless here */
            showhextoo,
            esbp, err);
        if (wres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: cannot print DW_FORM_exprloc content.",
                wres,*err);
            return wres;
        }
        }
        break;
    case DW_FORM_sec_offset: { /* DWARF4, DWARF5 */
        char* emptyattrname = 0;
        int show_form_here = 0;
        wres = get_small_encoding_integer_and_name(dbg,
            attrib,
            &tempud,
            emptyattrname,
            /* err_string */ NULL,
            (encoding_type_func) 0,
            err,show_form_here);
        if (wres == DW_DLV_NO_ENTRY) {
            /* Show nothing? */
        } else if (wres == DW_DLV_ERROR) {
            print_error_and_continue(dbg,
                "ERROR: cannot et DW_FORM_sec_offset value content.",
                wres,*err);
            return wres;
        } else {
            bracket_hex("",tempud,"",esbp);
        }
        }

        break;
    case DW_FORM_flag_present: /* DWARF4 */
        esb_append(esbp,"yes(1)");
        break;
    case DW_FORM_ref_sig8: {  /* DWARF4 */
        Dwarf_Sig8 sig8data;

        sig8data = zerosig;
        wres = dwarf_formsig8(attrib,&sig8data,err);
        if (wres != DW_DLV_OK) {
            /* Show nothing? */
            print_error_and_continue(dbg,
                "ERROR: cannot et DW_FORM_ref_sig8 value content.",
                wres,*err);
            return wres;
        } else {
            struct esb_s sig8str;

            esb_constructor(&sig8str);
            format_sig8_string(&sig8data,&sig8str);
            esb_append(esbp,esb_get_string(&sig8str));
            esb_destructor(&sig8str);
            if (!show_form) {
                esb_append(esbp," <type signature>");
            }
        }
        }
        break;
    case DW_FORM_implicit_const: {  /* DWARF5, attr val is signed uleb */
        wres = dwarf_formsdata(attrib, &tempsd, err);
        if (wres == DW_DLV_OK) {
            Dwarf_Bool hxform=TRUE;
            tempud = tempsd;
            formx_unsigned_and_signed_if_neg(tempud,tempsd,
                " (",hxform,esbp);
        } else if (wres == DW_DLV_NO_ENTRY) {
            /* nothing? */
        } else {
            print_error_and_continue(dbg,
                "ERROR: cannot get signed value of "
                "DW_FORM_implicit_const",
                wres,*err);
            return wres;
        }
        }
        break;
    case DW_FORM_loclistx:   /* DWARF5, index into .debug_loclists */
        wres = dwarf_formudata(attrib, &tempud, err);
        if (wres == DW_DLV_OK) {
            /*  Fall through to end to show the form details. */
            Dwarf_Bool hex_format = TRUE;
            esb_append(esbp,"<index to debug_loclists ");
            formx_unsigned(tempud,esbp,hex_format);
            esb_append(esbp,">");
            break;
        } else if (wres == DW_DLV_NO_ENTRY) {
            /* nothing? */
        } else {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,"Cannot get formudata on ");
            esb_append(&lstr,"DW_FORM_loclistx");
            esb_append(&lstr,"....");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                wres, *err);
            esb_destructor(&lstr);
            return wres;
        }
        break;
    case DW_FORM_rnglistx: {
        /*  DWARF5, index into .debug_rnglists
            Can appear only on DW_AT_ranges attribute.*/
        wres = dwarf_formudata(attrib, &tempud, err);
        if (wres == DW_DLV_OK) {
            /*  Fall through to end to show the form details. */
            Dwarf_Bool hex_format = TRUE;
            esb_append(esbp,"<index to debug_rnglists ");
            formx_unsigned(tempud,esbp,hex_format);
            esb_append(esbp,">");
        } else if (wres == DW_DLV_NO_ENTRY) {
            /*  nothing? */
        } else {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,"Cannot get formudata on ");
            esb_append(&lstr,"DW_FORM_rnglistx");
            esb_append(&lstr,"....");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                wres, *err);
            esb_destructor(&lstr);
            return wres;
        }
        }
        break;
    case DW_FORM_ref_sup4: /* DWARF5 */
    case DW_FORM_ref_sup8: /* DWARF5 */
    case DW_FORM_GNU_ref_alt: {
        bres = dwarf_global_formref(attrib, &off, err);
        if (bres == DW_DLV_OK) {
            bracket_hex("",off,"",esbp);
        } else {
            struct esb_s lstr;
            esb_constructor(&lstr);
            esb_append(&lstr,get_FORM_name(theform,FALSE));
            esb_append(&lstr," form with no reference?!");
            print_error_and_continue(dbg,
                esb_get_string(&lstr),
                bres,*err);
            esb_destructor(&lstr);
            return wres;
        }
        }
        break;
    default: {
        struct esb_s lstr;

        esb_constructor(&lstr);
        esb_append_printf_u(&lstr,
            "ERROR: dwarf_whatform unexpected value, form code 0x%04x",
            theform);
        simple_err_only_return_action(DW_DLV_ERROR,
            esb_get_string(&lstr));
        esb_destructor(&lstr);
        }
        break;
    } /* end switch on theform */
    show_form_itself(show_form,local_verbose,theform,
        direct_form,esbp);
    return DW_DLV_OK;
}

void
format_sig8_string(Dwarf_Sig8*data, struct esb_s *out)
{
    unsigned i = 0;
    esb_append(out,"0x");
    for (; i < sizeof(data->signature); ++i) {
        esb_append_printf_u(out,  "%02x",
            (unsigned char)(data->signature[i]));
    }
}


static int
get_form_values( UNUSEDARG Dwarf_Debug dbg,
    Dwarf_Attribute attrib,
    Dwarf_Half * theform, Dwarf_Half * directform,
    Dwarf_Error *err)
{
    int res = 0;

    res = dwarf_whatform(attrib, theform, err);
    if (res != DW_DLV_OK) {
        return res;
    }
    res = dwarf_whatform_direct(attrib, directform, err);
    return res;
}

static void
show_form_itself(int local_show_form,
    int local_verbose,
    int theform,
    int directform, struct esb_s *esbp)
{
    if (local_show_form
        && directform && directform == DW_FORM_indirect) {
        char *form_indir = " (used DW_FORM_indirect";
        char *form_indir2 = ") ";
        esb_append(esbp, form_indir);
        if (local_verbose) {
            esb_append_printf_i(esbp," %d",DW_FORM_indirect);
        }
        esb_append(esbp, form_indir2);
    }
    if (local_show_form) {
        esb_append(esbp," <form ");
        esb_append(esbp,get_FORM_name(theform,
            pd_dwarf_names_print_on_error));
        if (local_verbose) {
            esb_append_printf_i(esbp," %d",theform);
        }
        esb_append(esbp,">");
    }
}

#include "dwarfdump-ta-table.h"
#include "dwarfdump-ta-ext-table.h"

static int
legal_tag_attr_combination(Dwarf_Half tag, Dwarf_Half attr)
{
    if (tag <= 0) {
        return FALSE;
    }
    if (tag < ATTR_TREE_ROW_COUNT) {
        int index = attr / BITS_PER_WORD;
        if (index < ATTR_TREE_COLUMN_COUNT) {
            unsigned bitflag = ((unsigned)1) << (attr % BITS_PER_WORD);
            int known = ((tag_attr_combination_table[tag][index]
                & bitflag) > 0 ? TRUE : FALSE);
            if (known) {
#ifdef HAVE_USAGE_TAG_ATTR
                /* Record usage of pair (tag,attr) */
                if ( glflags.gf_print_usage_tag_attr) {
                    Usage_Tag_Attr *usage_ptr = usage_tag_attr[tag];
                    while (usage_ptr->attr) {
                        if (attr == usage_ptr->attr) {
                            ++usage_ptr->count;
                            break;
                        }
                        ++usage_ptr;
                    }
                }
#endif /* HAVE_USAGE_TAG_ATTR */
                return TRUE;
            }
        }
    }
    /*  DW_AT_MIPS_fde  used to return TRUE as that was
        convenient for SGI/MIPS users. */
    if (!glflags.gf_suppress_check_extensions_tables) {
        int r = 0;
        for (; r < ATTR_TREE_EXT_ROW_COUNT; ++r ) {
            int c = 1;
            if (tag != tag_attr_combination_ext_table[r][0]) {
                continue;
            }
            for (; c < ATTR_TREE_EXT_COLUMN_COUNT ; ++c) {
                if (tag_attr_combination_ext_table[r][c] == attr) {
                    return TRUE;
                }
            }
        }
    }
    return (FALSE);
}

#include "dwarfdump-tt-table.h"
#include "dwarfdump-tt-ext-table.h"

/*  Look only at valid table entries
    The check here must match the building-logic in
    tag_tree.c
    And must match the tags defined in dwarf.h
    The tag_tree_combination_table is a table of bit flags.  */
static int
legal_tag_tree_combination(Dwarf_Half tag_parent, Dwarf_Half tag_child)
{
    if (tag_parent <= 0) {
        return FALSE;
    }
    if (tag_parent < TAG_TREE_ROW_COUNT) {
        int index = tag_child / BITS_PER_WORD;
        if (index < TAG_TREE_COLUMN_COUNT) {
            unsigned bitflag = ((unsigned)1) << (tag_child % BITS_PER_WORD);
            int known = ((tag_tree_combination_table[tag_parent]
                [index] & bitflag) > 0 ? TRUE : FALSE);
            if (known) {
#ifdef HAVE_USAGE_TAG_ATTR
                /* Record usage of pair (tag_parent,tag_child) */
                if ( glflags.gf_print_usage_tag_attr) {
                    Usage_Tag_Tree *usage_ptr = usage_tag_tree[tag_parent];
                    while (usage_ptr->tag) {
                        if (tag_child == usage_ptr->tag) {
                            ++usage_ptr->count;
                            break;
                        }
                        ++usage_ptr;
                    }
                }
#endif /* HAVE_USAGE_TAG_ATTR */
                return TRUE;
            }
        }
    }
    if (!glflags.gf_suppress_check_extensions_tables) {
        int r = 0;
        for (; r < TAG_TREE_EXT_ROW_COUNT; ++r ) {
            int c = 1;
            if (tag_parent != tag_tree_combination_ext_table[r][0]) {
                continue;
            }
            for (; c < TAG_TREE_EXT_COLUMN_COUNT ; ++c) {
                if (tag_tree_combination_ext_table[r][c] == tag_child) {
                    return TRUE;
                }
            }
        }
    }
    return (FALSE);
}

/* Print a detailed tag and attributes usage */
int
print_tag_attributes_usage(UNUSEDARG Dwarf_Debug dbg,
    UNUSEDARG Dwarf_Error *err)
{
#ifdef HAVE_USAGE_TAG_ATTR
    /*  Traverse the tag-tree table to print its usage and then use the
        DW_TAG value as an index into the tag_attr table to print its
        associated usage all together. */
    boolean print_header = TRUE;
    Rate_Tag_Tree *tag_rate;
    Rate_Tag_Attr *atr_rate;
    Usage_Tag_Tree *usage_tag_tree_ptr;
    Usage_Tag_Attr *usage_tag_attr_ptr;
    Dwarf_Unsigned total_tags = 0;
    Dwarf_Unsigned total_atrs = 0;
    Dwarf_Half total_found_tags = 0;
    Dwarf_Half total_found_atrs = 0;
    Dwarf_Half total_legal_tags = 0;
    Dwarf_Half total_legal_atrs = 0;
    float rate_1;
    float rate_2;
    int tag;
    printf("\n*** TAGS AND ATTRIBUTES USAGE ***\n");
    for (tag = 1; tag < DW_TAG_last; ++tag) {
        /* Print usage of children TAGs */
        if ( glflags.gf_print_usage_tag_attr_full || tag_usage[tag]) {
            usage_tag_tree_ptr = usage_tag_tree[tag];
            if (usage_tag_tree_ptr && print_header) {
                total_tags += tag_usage[tag];
                printf("%6d %s\n",
                    tag_usage[tag],
                    get_TAG_name(tag,pd_dwarf_names_print_on_error));
                print_header = FALSE;
            }
            while (usage_tag_tree_ptr && usage_tag_tree_ptr->tag) {
                if ( glflags.gf_print_usage_tag_attr_full ||
                    usage_tag_tree_ptr->count) {
                    total_tags += usage_tag_tree_ptr->count;
                    printf("%6s %6d %s\n",
                        " ",
                        usage_tag_tree_ptr->count,
                        get_TAG_name(usage_tag_tree_ptr->tag,
                            pd_dwarf_names_print_on_error));
                    /* Record the tag as found */
                    if (usage_tag_tree_ptr->count) {
                        ++rate_tag_tree[tag].found;
                    }
                }
                ++usage_tag_tree_ptr;
            }
        }
        /* Print usage of attributes */
        if ( glflags.gf_print_usage_tag_attr_full || tag_usage[tag]) {
            usage_tag_attr_ptr = usage_tag_attr[tag];
            if (usage_tag_attr_ptr && print_header) {
                total_tags += tag_usage[tag];
                printf("%6d %s\n",
                    tag_usage[tag],
                    get_TAG_name(tag,pd_dwarf_names_print_on_error));
            }
            while (usage_tag_attr_ptr && usage_tag_attr_ptr->attr) {
                if ( glflags.gf_print_usage_tag_attr_full ||
                    usage_tag_attr_ptr->count) {
                    total_atrs += usage_tag_attr_ptr->count;
                    printf("%6s %6d %s\n",
                        " ",
                        usage_tag_attr_ptr->count,
                        get_AT_name(usage_tag_attr_ptr->attr,
                            pd_dwarf_names_print_on_error));
                    /* Record the attribute as found */
                    if (usage_tag_attr_ptr->count) {
                        ++rate_tag_attr[tag].found;
                    }
                }
                ++usage_tag_attr_ptr;
            }
        }
        print_header = TRUE;
    }
    printf("** Summary **\n"
        "Number of tags      : %10" /*DW_PR_XZEROS*/ DW_PR_DUu "\n"  /* TAGs */
        "Number of attributes: %10" /*DW_PR_XZEROS*/ DW_PR_DUu "\n"  /* ATRs */,
        total_tags,
        total_atrs);

    total_legal_tags = 0;
    total_found_tags = 0;
    total_legal_atrs = 0;
    total_found_atrs = 0;

    /* Print percentage of TAGs covered */
    printf("\n*** TAGS AND ATTRIBUTES USAGE RATE ***\n");
    printf("%-32s %-16s %-16s\n"," ","Tags","Attributes");
    printf("%-32s legal found rate legal found rate\n","TAG name");
    for (tag = 1; tag < DW_TAG_last; ++tag) {
        tag_rate = &rate_tag_tree[tag];
        atr_rate = &rate_tag_attr[tag];
        if ( glflags.gf_print_usage_tag_attr_full ||
            tag_rate->found || atr_rate->found) {
            rate_1 = tag_rate->legal ?
                (float)((tag_rate->found * 100) / tag_rate->legal) : 0;
            rate_2 = atr_rate->legal ?
                (float)((atr_rate->found * 100) / atr_rate->legal) : 0;
            /* Skip not defined DW_TAG values (See dwarf.h) */
            if (usage_tag_tree[tag]) {
                total_legal_tags += tag_rate->legal;
                total_found_tags += tag_rate->found;
                total_legal_atrs += atr_rate->legal;
                total_found_atrs += atr_rate->found;
                printf("%-32s %5d %5d %3.0f%% %5d %5d %3.0f%%\n",
                    get_TAG_name(tag,pd_dwarf_names_print_on_error),
                    tag_rate->legal,tag_rate->found,rate_1,
                    atr_rate->legal,atr_rate->found,rate_2);
            }
        }
    }

    /* Print a whole summary */
    rate_1 = total_legal_tags ?
        (float)((total_found_tags * 100) / total_legal_tags) : 0;
    rate_2 = total_legal_atrs ?
        (float)((total_found_atrs * 100) / total_legal_atrs) : 0;
    printf("%-32s %5d %5d %3.0f%% %5d %5d %3.0f%%\n",
        "** Summary **",
        total_legal_tags,total_found_tags,rate_1,
        total_legal_atrs,total_found_atrs,rate_2);

#endif /* HAVE_USAGE_TAG_ATTR */
    return DW_DLV_OK;
}
