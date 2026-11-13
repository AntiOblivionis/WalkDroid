"""Validate physical load addresses before any MT8590 RAM boot.

Stock boot.img has dummy 0x100... addresses accepted by the eMMC loader but not
the Fastboot memcpy handler. Never submit that raw backup to fastboot boot.
"""
import pathlib,struct,gzip

KERNEL_ADDR=0x80008000
RAMDISK_ADDR=0x84000000
TAGS_ADDR=0x80000100
RAM_START=0x80000000
RAM_END=0x9dc00000
LK_RANGE=(0x81e00000,0x82000000)

def validate_image(path):
    b=pathlib.Path(path).read_bytes()
    if len(b)!=16777216 or b[:8]!=b'ANDROID!':raise ValueError('Unexpected boot image format/extent')
    ks,ka,rs,ra,ss,sa,tags,page,ds,_=struct.unpack_from('<10I',b,8)
    if (ka,ra,tags)!=(KERNEL_ADDR,RAMDISK_ADDR,TAGS_ADDR):
        raise ValueError('RAM load addresses invalid for this MT8590; raw stock header is NOT RAM-boot safe')
    if page!=2048 or ss or ds:raise ValueError('Unsupported page/secondary/device tree layout')
    regions=[(ka,ka+ks),(ra,ra+rs),(tags,tags+0x7000)]
    for start,end in regions:
        if not RAM_START<=start<end<=RAM_END:raise ValueError('Load range outside actual DRAM')
        if start<LK_RANGE[1] and LK_RANGE[0]<end:raise ValueError('Load range overlaps running LK')
    for i,(a,z) in enumerate(regions):
        for c,d in regions[i+1:]:
            if a<d and c<z:raise ValueError('RAM load ranges overlap')
    roff=page+(ks+page-1)//page*page
    kernel=b[page:page+ks];rd=b[roff:roff+rs]
    for name,component in [('KERNEL',kernel),('ROOTFS',rd)]:
        if component[:4]!=bytes.fromhex('88168858') or struct.unpack_from('<I',component,4)[0]!=len(component)-512:
            raise ValueError('MTK component header invalid: '+name)
    if struct.unpack_from('<I',kernel,512+0x24)[0]!=0x016f2818:raise ValueError('ARM zImage magic absent')
    cpio=gzip.decompress(rd[512:])
    if not cpio.startswith(b'070701'):raise ValueError('Ramdisk is not newc')
    return {'kernel_address':hex(ka),'ramdisk_address':hex(ra),'tags_address':hex(tags),
            'load_ranges':[[hex(a),hex(z)] for a,z in regions]}


if __name__ == '__main__':
    import argparse, json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', help='locally assembled RAM boot image')
    args = parser.parse_args()
    print(json.dumps(validate_image(args.image), indent=2))
