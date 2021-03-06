#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <getopt.h>

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

static int parse_bcd_conf(FILE *fp, void *ptr);
#define IMX_HEADER_SIZE 3072

int main(int argc, char * const argv[])
{
    struct imx_header *imx;
    struct bcd_data_start *bcd_start;
    struct bcd_data *bcd_data;
    struct bcd_data_hdr *bcd_hdr;
    size_t size = 0;
    int num_bcd_conf;
    uint32_t entry = 0;
    void *buf = NULL;
    FILE *out_file = NULL, *bcd_file = NULL;
    int c, digit_optind = 0;

    while(1) {
        int ind = optind ? optind : 1;
        int opt_idx = 0;

        static struct option long_options[] = {
            {"config", required_argument, 0, 'c'},    
            {"entry", required_argument, 0, 'e'},
            {"out", required_argument, 0, 'o'},
            {"size", required_argument, 0, 's'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "c:e:o:", long_options, &opt_idx);

        if (c == -1)
            break;

        switch(c) {
            case 'c':
                if (optarg) {
                    printf("config:%s\n", optarg);
                    if (access(optarg, F_OK) < 0) {
                        fprintf(stderr, "Invalid option %s value:%s\n", long_options[opt_idx].name, optarg);
                        goto out;
                    }
                    bcd_file = fopen(optarg, "r");
                    if (!bcd_file) {
                        fprintf(stderr, "ERROR:%s %s\n", strerror(errno), optarg);
                        goto out;
                    }
                }

                break;
            case 'e':
                if (optarg) {
                    printf("entry:%s\n", optarg);
                    entry = strtoul(optarg, NULL, 0);
                    if (entry == ULONG_MAX) {
                        fprintf(stderr, "Invalid option %s value:%s\n", long_options[opt_idx].name, optarg);
                        goto out;
                    }
                }

                break;
            case 'o':
                if (optarg) {
                    printf("out:%s\n", optarg);
                    out_file = fopen(optarg, "w+");
                    if (!out_file) {
                        fprintf(stderr, "ERROR:%s %s\n", strerror(errno), optarg);
                        goto out;
                    }
                }
                break;
            case 's':
                if (optarg) {
                    printf("size:%s\n", optarg);
                    size = strtoul(optarg, NULL, 0);
                    if (size == ULONG_MAX) {
                        fprintf(stderr, "Invalid option %s value:%s\n", long_options[opt_idx].name, optarg);
                        goto out;
                    }
                }
                break;
            default:
                printf("unkown option %c\n", c);
                goto out;
        }
        
    };

    if (!bcd_file || !out_file || !size) {
        fprintf(stderr, "ERROR\n");
        goto out;
    }

    buf = calloc(1, IMX_HEADER_SIZE);

    if (!buf) {
        fprintf(stderr, "alloc header data failed.\n");
        goto out;
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
    bcd_hdr->fmt.param = 0x4;

    bcd_start->start = entry - 0x1000; /* 4K */
    bcd_start->len = size;

    num_bcd_conf = parse_bcd_conf(bcd_file, bcd_data);

    if (num_bcd_conf < 0) {
        goto out;
    }

    bcd_hdr->fmt.len = num_bcd_conf * 8;
    bcd_hdr->hdr.len = num_bcd_conf * 8 + 4;

    size = fwrite(imx, IMX_HEADER_SIZE, 1, out_file);

    if (size < 0) {
        fprintf(stderr, "write FAILED %s\n", strerror(errno));
    }

out:
    if (buf != NULL)
        free(buf);

    if (out_file != NULL)
        fclose(out_file);

    if (bcd_file != NULL)
        fclose(bcd_file);

    return errno;
}

static int parse_bcd_conf(FILE *fp, void *ptr)
{
    struct bcd_data *data = ptr;
    char *addr;
    char *val;
    char *tmp;
    int ret = 0;
    #define LINE_SIZE   1024
    char buf[1024];

    while(fgets(buf, LINE_SIZE -1, fp)!= NULL) {

        if (buf[0] == '\n' || buf[1] == '\r')
            continue;

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