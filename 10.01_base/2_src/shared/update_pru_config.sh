#!/bin/bash
#
# update_pru_config.sh
# Adds several addresses from PRU linker map to
# the code C-array generated by hexpru --array

# get entry point address from PRU linker map and append to C-array with PRU code
# $1 = PRU number
# $2 = raw hexpru--array file
# $3 = clpru linker map
# $4 = hexpru array output

cmdline="$0 $*"
prunum=$1
rawarray=$2
configfile_c=$3.c
configfile_h=$3.h
linkermap=$4

rm -f $configfile_h
rm -f $configfile_c

# prefixes in array file

echo "/*** Following code generated by \"$cmdline\" ***/" >>$configfile_h
echo >>$configfile_h
echo "/*** Following code generated by \"$cmdline\" ***/" >>$configfile_c
echo >>$configfile_c

echo "#define _PRU${prunum}_CONFIG_C_" >>$configfile_c
echo "#include <stdint.h>" >>$configfile_c
echo >>$configfile_c

echo "// under c++ linker error with "const" attribute ?!"	>>$configfile_c
echo "#define const"	>>$configfile_c
echo >>$configfile_c
echo "// BEGIN original hexpru --array output " >>$configfile_c
cat $rawarray >>$configfile_c
echo "// END   original hexpru --array output " >>$configfile_c
echo >>$configfile_c


echo >>$configfile_c


echo "Opening header file"
echo "#ifndef _PRU${prunum}_CONFIG_H_" >>$configfile_h
echo "#define _PRU${prunum}_CONFIG_H_" >>$configfile_h
echo >>$configfile_h
echo "#include <stdint.h>"  >>$configfile_h
echo >>$configfile_h

echo >>$configfile_c
echo "Generate sizeof() for code image"
echo "// sizeof() for code image" >>$configfile_c
echo "unsigned pru${prunum}_sizeof_code(void) { "  >>$configfile_c
echo "  return sizeof(pru${prunum}_image_0) ;" >>$configfile_c
echo "}" >>$configfile_c
echo >>$configfile_c

echo "#ifndef _PRU${prunum}_CONFIG_C_" >>$configfile_h
echo "// under c++ linker error with "const" attribute ?!"	>>$configfile_c
echo "// extern const uint32_t pru${prunum}_image_0[] ;" >>$configfile_h
echo "extern uint32_t pru${prunum}_image_0[] ;" >>$configfile_h
echo "#endif" >>$configfile_h
echo >>$configfile_h

echo "unsigned pru${prunum}_sizeof_code(void) ;"  >>$configfile_h
echo >>$configfile_h


echo 'Search entry address from linker map line: ENTRY POINT SYMBOL: "_c_int00_noinit_noargs"  address: 00000000'


set -- `grep "ENTRY POINT" < $linkermap`

echo "// code entry point $4 from linker map file:" >>$configfile_h
echo "#define PRU${prunum}_ENTRY_ADDR	0x$6" >>$configfile_h
echo >>$configfile_h


# do not scan for  mailbox start, locate always to "0" / 0x10000
#
# echo 'Get mailbox page & offset in PRU internal shared 12 KB RAM'
# set -- `gawk  '
# /GLOBAL SYMBOLS:/ { table_found=1 }
# table_found && /mailbox/ { mailbox_addr = strtonum("0x" $2)}
# END {printf "0x%x", mailbox_addr - 0x10000 }
# '   $linkermap`

echo "// Mailbox page & offset in PRU internal shared 12 KB RAM" >>$configfile_h
echo "// Accessible by both PRUs, must be located in shared RAM" >>$configfile_h
echo "// offset 0 == addr 0x10000 in linker cmd files for PRU0 AND PRU1 projects." >>$configfile_h
echo "// For use with prussdrv_map_prumem()" >>$configfile_h
echo "#ifndef PRU_MAILBOX_RAM_ID" >>$configfile_h
echo "  #define PRU_MAILBOX_RAM_ID	PRUSS0_SHARED_DATARAM" >>$configfile_h
echo "  #define PRU_MAILBOX_RAM_OFFSET	0" >>$configfile_h
echo "#endif" >>$configfile_h
echo >>$configfile_h

echo "// Device register page & offset in PRU0 8KB RAM mapped into PRU1 space" >>$configfile_h
echo "// offset 0 == addr 0x2000 in linker cmd files for PRU1 projects." >>$configfile_h
echo "// For use with prussdrv_map_prumem()" >>$configfile_h
echo "#ifndef PRU_DEVICEREGISTER_RAM_ID" >>$configfile_h
echo "  #define PRU_DEVICEREGISTER_RAM_ID	PRUSS0_PRU0_DATARAM" >>$configfile_h
echo "  #define PRU_DEVICEREGISTER_RAM_OFFSET	0" >>$configfile_h
echo "#endif" >>$configfile_h
echo >>$configfile_h



echo "#endif" >>$configfile_h
