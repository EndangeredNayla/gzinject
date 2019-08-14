#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "gzi.h"
#include "lz77.h"
#include "gzinject.h"

typedef int (*gzi_action_t)(gzi_ctxt_t *ctxt, int pos);

static int gzi_cmd_file(gzi_ctxt_t *ctxt, int pos){
    ctxt->curfile = ctxt->codes[pos].data & 0xFF;
    if(verbose){
        printf("Setting current file to %d\n",ctxt->curfile);
    }
    return 1;
}

static int gzi_cmd_lz77_decomp(gzi_ctxt_t *ctxt, int pos){
    int32_t curfile = ctxt->curfile;
    if(curfile<0){
        printf("Warning: No file Selected, not decompressing.\n");
        return 0;
    }
    if(verbose){
        printf("LZ77 Decompressing %d\n",curfile);
    }
    int decompsize = addpadding(lz77_decompressed_size(ctxt->file_ptrs[curfile]),16);
    uint8_t *decomp = calloc(decompsize,1);
    lz77_decompress(ctxt->file_ptrs[curfile],decomp);
    free(ctxt->file_ptrs[curfile]);
    ctxt->file_ptrs[curfile] = decomp;
    ctxt->file_sizes[curfile] = decompsize;
    return 1;
}

static int gzi_cmd_lz77_comp(gzi_ctxt_t *ctxt, int pos){
    int32_t curfile = ctxt->curfile;
    if(curfile<0){
        printf("Warning: No file selected, not compressing.\n");
        return 0;
    }
    if(verbose){
        printf("LZ77 Compressing %d\n",curfile);
    }
    uint8_t *comp = NULL;
    uint32_t len = ctxt->file_sizes[curfile];

    // I hate this, but it works for now.
    len -= (8 - (len & 0x8));
    int complen = lz77_compress(ctxt->file_ptrs[curfile],&comp,len,&len);
    free(ctxt->file_ptrs[curfile]);
    ctxt->file_ptrs[curfile] = comp;
    ctxt->file_sizes[curfile] = complen;
    return 1;
}

static int gzi_cmd_apply_patch(gzi_ctxt_t *ctxt, int pos){
    int32_t curfile = ctxt->curfile;
    if(curfile<0){
        printf("Warning: No file selected, not applying patch.\n");
    }
    gzi_code code = ctxt->codes[pos];
    uint32_t val = code.data;
    if(verbose){
        printf("Apply patch to %d. offset %d = %d\n",curfile,code.offset,code.data);
    }
    switch(code.len){
        case 1:
            *((uint8_t*)(ctxt->file_ptrs[curfile] + code.offset)) = (uint8_t)val;
            break;
        case 2:
            *((uint16_t*)(ctxt->file_ptrs[curfile] + code.offset)) = REVERSEENDIAN16((uint16_t)val);
            break;
        case 4:
        default:
            *((uint32_t*)(ctxt->file_ptrs[curfile] + code.offset)) = REVERSEENDIAN32(val);
            break;
    }
    return 1;
}

static gzi_action_t commands[] = {
    gzi_cmd_file,
    gzi_cmd_lz77_decomp,
    gzi_cmd_lz77_comp,
    gzi_cmd_apply_patch,
};

static char *readline(FILE *fle){
    char *line = NULL;
    int buflen=256;
    for(int i=0;;++i){
        int c = fgetc(fle);

        if(i%buflen==0){
            char *new = realloc(line,i+buflen);
            line = new;
        }
        if(c==EOF || c=='\n'){
            line[i] = 0;
            return line;
        }else{
            line[i] = c;
        }
    }
}

int gzi_parse_file(gzi_ctxt_t *ctxt, const char *file){
    FILE *fle = fopen(file,"r");
    if(!fle){
        fprintf(stderr,"Could not open %s, cannot parse file.\n",file);
    }
    if(verbose){
        printf("Parings gzi file %s\n",file);
    }
    while(!feof(fle)){
        char *line = readline(fle);
        if(!line){
            fprintf(stderr,"Could not readline from gzi file %s.\n",file);
            return 0;
        }
        if(line[0]=='#'){
            free(line);
            continue;
        } 
        char command[6];
        char offset[10];
        char data[10];
        sscanf(line,"%5s %9s %9s",command,offset,data);
        ctxt->codecnt++;
        gzi_code *new_codes = realloc(ctxt->codes,sizeof(gzi_code) * ctxt->codecnt);
        if(new_codes){
            ctxt->codes = new_codes;
        }
        gzi_code code;
        uint16_t cmd;
        sscanf(command,"%"SCNx16,&cmd);
        code.command = (cmd & 0xFF00) >> 8;
        code.len = cmd & 0xFF;
        sscanf(offset,"%"SCNx32,&code.offset);
        sscanf(data,"%"SCNx32,&code.data);
        memcpy(ctxt->codes + (ctxt->codecnt - 1),&code,sizeof(code));
        free(line);
    }
    fclose(fle);
    return 1;
}

int gzi_run(gzi_ctxt_t *ctxt){
    if(verbose){
        printf("Running gzi commands\n");
    }
    for(int i=0;i<ctxt->codecnt;i++){
        commands[ctxt->codes[i].command](ctxt,i);
    }
    return 1;
}

int gzi_init(gzi_ctxt_t *ctxt, uint8_t **files, uint32_t *filesizes, int filecnt){
    ctxt->codes = NULL;
    ctxt->codecnt=0;
    ctxt->curfile=-1;
    ctxt->file_ptrs = files;
    ctxt->file_sizes = filesizes;
    ctxt->filecnt = filecnt;
    return 1;
}

int gzi_destroy(gzi_ctxt_t *ctxt){
    if(ctxt->codes) free(ctxt->codes);
    return 1;
}   