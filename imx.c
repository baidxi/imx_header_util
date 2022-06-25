#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

struct bcd_header {
    uint8_t tag;
    uint16_t len;
    uint8_t ver;
}__attribute__((packed));

struct bcd_format {
    uint8_t tag;
    uint16_t len;
    uint8_t param;
}__attribute__((packed));

struct bcd_data {
    uint32_t addr;
    uint32_t val;
};

struct bcd_data_hdr {
    struct bcd_header hdr;
    struct bcd_format fmt;
    struct bcd_data data[0];
};

struct bcd_data_start {
    uint32_t start;
    uint32_t len;
    uint32_t plugin;
    struct bcd_data_hdr hdr[0];
};

struct ivt_header {
    uint8_t tag;
    uint16_t len;
    uint8_t ver;
}__attribute__((packed));

struct imx_header {
    struct ivt_header hdr;
    uint32_t entry;         /* 程序加载地址 */
    uint32_t reserved1;
    uint32_t bcd;           /* bcd 存放地址 */
    uint32_t bcd_data;      /* bcd data 存放地址 */
    uint32_t self;          /* IVT 地址， 加载地址减3K之后的地址 */
    uint32_t csf;           
    uint32_t reserved2;
    struct bcd_data_start start[0];
}__attribute__((packed));

static int parse_bcd_conf(const char *file, void *ptr);
#define IMX_HEADER_SIZE 3072

int main(int argc, const char *argv[])
{
    struct imx_header *imx;
    struct bcd_data_start *bcd_start;
    struct bcd_data *bcd_data;
    struct bcd_data_hdr *bcd_hdr;
    size_t size;
    int num_bcd_conf;
    uint32_t entry;
    void *buf;
    FILE *out, *bcd_file;
    
    if (argc < 4) {
        fprintf(stderr, "./%s bcd.cfg entry_addr out.imx\n", argv[0]);
        return -1;
    }

    if (access(argv[1], F_OK) < 0) {
        fprintf(stderr, "%s:%s\n", argv[1], strerror(errno));
        return -1;
    }

    entry  = strtoul(argv[2], NULL, 0);

    if (entry == ULONG_MAX) {
        fprintf(stderr, "argv 2 ERROR %s", strerror(errno));
        return -1;
    }

    out = fopen(argv[3], "wb");

    if (!out) {
        fprintf(stderr, "open %s FAILED %s\n", argv[1], strerror(errno));
        return -1;
    }

    buf = calloc(1, IMX_HEADER_SIZE);

    if (!buf) {
        fprintf(stderr, "alloc header data failed.\n");
        fclose(out);
        return -1;
    }

    imx = buf;
    bcd_start = &imx->start;
    bcd_hdr = &bcd_start->hdr;
    bcd_data = &bcd_hdr->data;

    imx->hdr.tag = 0xd1;
    imx->hdr.len = sizeof(struct imx_header);
    imx->hdr.ver = 0x40;

    imx->entry = entry;
    imx->self = entry - 0xc00; /* 3K */
    imx->bcd_data = entry - 0xc00 + imx->hdr.len;
    imx->bcd = entry - 0xc00 + imx->hdr.len + sizeof(struct bcd_data_start);

    bcd_hdr->hdr.tag = 0xd2;
    bcd_hdr->hdr.ver = 0x40;
    bcd_hdr->fmt.tag = 0xcc;
    bcd_hdr->fmt.len = 0x1e4;
    bcd_hdr->fmt.param = 0x4;

    bcd_start->start = entry - 0x1000; /* 4K */
    bcd_start->len = 0x20000;

    num_bcd_conf = parse_bcd_conf(argv[1], bcd_data);

    bcd_hdr->fmt.len = num_bcd_conf * 8;
    bcd_hdr->hdr.len = num_bcd_conf * 8 + 4;

    size = fwrite(imx, IMX_HEADER_SIZE, 1, out);

    if (size < 0) {
        fprintf(stderr, "write FAILED %s\n", strerror(errno));
    }

out:
    free(buf);
    fclose(out);
    return 0;
}

static int parse_bcd_conf(const char *file, void *ptr)
{
    FILE *fp;
    struct bcd_data *data = ptr;
    char *addr;
    char *val;
    char *tmp;
    int ret = 0;
    #define LINE_SIZE   1024
    char buf[1024];

    fp = fopen(file, "r");

    if (!fp) {
        fprintf(stderr, "open %s FAILED %s\n", file, strerror(errno));
        return -1;
    }

    while(fgets(buf, LINE_SIZE -1, fp)!= NULL) {
        if (ret == 0) {
            if (buf[0] != ';' && buf[1] != ';') {
                fprintf(stderr, "unknow bcd config\n");
                return -1;
            }
            if (strstr(buf, "bcd")) {
                fprintf(stderr, "unknow bcd config\n");
                return -1;
            }
        }
        tmp = strdup(buf);
        if (tmp == NULL) {
            fprintf(stderr, "dump buf error\n");
            return -1;
        }
        addr = strtok(tmp, "=");
        val = strtok(NULL, "=");

        data->addr = strtoul(addr, NULL, 0);
        data->val = strtoul(val, NULL, 0);

        free(tmp);

        if (data->val == ULONG_MAX || data->addr == ULONG_MAX) {
            fprintf(stderr, "ignore input invalid:%s\n", buf);
            continue;
        }

        data++;
        ret++;
    }
    return ret;
}