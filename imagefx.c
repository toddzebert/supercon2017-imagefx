
// some simple image processing demos

#include "cambadge.h"
#include "globals.h"

// states used by this application

#define s_start 0
#define s_run 1
#define s_quit 2
#define s_camgrab 3
#define s_camwait 4
#define s_camrestart 5

#define bufsize (128*96+1)
#define bufstart 8
#define linelength 129

#define ct_bmp 0
#define ct_dir 1
#define ct_avi 2

// note that #define cambufsize (dispwidth*dispheight*3+256)
#define cambuffmono_size (dispwidth * dispheight + 64) // "plus a little"
#define cambuffmono_offset cambuffmono_size
#define cambuffmono_offset2 cambuffmono_size * 2

typedef unsigned char mono_buffer_t[cambuffmono_size];

typedef signed int Kernel_t[3][3];


// all below from camera.c, with slight modifications
char camname[12];

void docamname(unsigned int n, unsigned int ct) { // create a formatted filename CAMxxx.BMP or AVI, or dirname CAM000

    unsigned int i = 0;
    if (ct == ct_dir) {
        camname[i++] = '\\';
    }
    camname[i++] = 'C';
    camname[i++] = 'A';
    camname[i++] = 'M';
    camname[i++] = (n / 1000) + '0';
    camname[i++] = ((n % 1000) / 100) + '0';
    camname[i++] = ((n % 100) / 10) + '0';
    camname[i++] = (n % 10) + '0';

    if (ct == ct_bmp) {
        camname[i++] = '.';
        camname[i++] = 'B';
        camname[i++] = 'M';
        camname[i++] = 'P';
    }
    if (ct == ct_avi) {
        camname[i++] = '.';
        camname[i++] = 'A';
        camname[i++] = 'V';
        camname[i++] = 'I';
    }

    camname[i++] = 0;
}



// convolution edge handling
int reflect(int M, int x) {
    if(x < 0) {
        return -x - 1;
    }
    if(x >= M) {
        return 2*M - x - 1;
    }
   return x;
}

/**
 * reads from cambuffer, writes to cambuffer + cambuffmono_offset
 * assumes 3x3 matrix
 *
 * @param Kernel
 * @param scaler is # of right bit shifts, 0 = n/c, 1 = /2, 2 = /4, 3 = /8
 * @param offset
 */
void convolution(mono_buffer_t sourceBuffer, mono_buffer_t targetBuffer, Kernel_t Kernel, signed int scaler, signed int offset) {
    int op, np;
    unsigned int x, y, x1, y1;
    int j, k;
    int32_t sum;

    for (y = 0; y != ypixels; y++) {
        for (x = 0; x != xpixels; x++) {
            sum = 0;
            for (k = -1; k <= 1; k++) {
                for (j = -1; j <= 1; j++) {
                    x1 = reflect(xpixels, x - j);
                    y1 = reflect(ypixels, y - k);

                    op = *(sourceBuffer + x1 + y1 * xpixels);

                    sum = sum + Kernel[j+1][k+1] * op; // +1's offset into array
                }
            }

            np = ((sum >> scaler) + offset);
            if (np < 0) np = 0; else if (np > 255) np = 255;
            *(targetBuffer + x + y * xpixels) = np;

        }
    }

}

// assumes 3x3
void copyKernel(Kernel_t fromKernel, Kernel_t toKernel) {
    int j, k;

    for (k = -1; k <= 1; k++) {
        for (j = -1; j <= 1; j++) {
            toKernel[j+1][k+1] = fromKernel[j+1][k+1];
        }
    }
}


// modified from original code
char* imagefx(unsigned int action) {
    static unsigned int state, effect, page, val1, val2;
    unsigned int x, y, i, d, r, g, b, e, f;
    static unsigned char explock;
    BOOL newEffect = TRUE;
    static signed int kernelCenter;
    static signed int effectBase;
    static unsigned int camfile = 0;
    static void * camgrab_buff = 0;


    const Kernel_t BoxcarKernel = {
        {7, 7, 7},
        {7, 7, 7},
        {7, 7, 7},
      };

    const Kernel_t SobelKernelX= {
        {1, 0, -1},
        {2, 1, -2},
        {1, 0, -1},
      };

    const Kernel_t SobelKernelY = {
        {1, 2, 1},
        {0, 1, 1},
        {-1, -2, -1},
      };

    const Kernel_t Edge3Kernel = {
        {-1, -1, -1},
        {-1, 8, -1},
        {-1, -1, -1},
      };

    const Kernel_t SharpenKernel = {
        {0, -1, 0},
        {-1, 5, -1},
        {0, -1, 0},
      };

    const Kernel_t InvertKernel = {
        {0, 0, 0},
        {0, -1, 0},
        {0, 0, 0},
      };

    Kernel_t Kernel, Kernel2;

    unsigned int s0;

    static unsigned char pb0save, pb1save;


    switch (action) {
        case act_name: return ("ToddFX");
        case act_help: return ("Todd Camera effects");

        case act_start:
            // called once when app is selected from menu
            state = s_start;
            effect = 0;
            cam_enable(cammode_128x96_z1_mono);
            cam_grabenable(camen_grab, bufstart - 1, 0);
            page = 1;
            val1 = val2 = 0;

            // from scope.c init code
            claimadc(1);
            ANSELBSET = 1;
            TRISBSET = 1;
            CNPUBCLR = 1;

            pb0save = RPB0Rbits.RPB0R;
            pb1save = RPB1Rbits.RPB1R;
            RPB0Rbits.RPB0R = 0;
            RPB1Rbits.RPB1R = 0;
            // end scope.c code

            kernelCenter = 0;

            camgrab_buff = 0;

            // normal exit
            return (0);
    } //switch

    if (action != act_poll) return (0);


    if (butpress & powerbut) state = s_quit; // exit with nonzero value to indicate we want to quit

    if (butpress & but1) {
        effect++;
        newEffect = TRUE;
        val1 = val2 = 0;
        state = s_start;
    }

    if (butpress & but2) {
        explock ^= 1;
        cam_setreg(0x13, explock ? 0xe0 : 0xe7);
    }

    if (butpress & but3) {
        if (effect > 1) {
            effect--;
            newEffect = TRUE;
        }
        val1 = val2 = 0;
        state = s_start;
    }

    if (butpress & but4) if (led1) led1_on;
        else led1_off;

    if (butpress & but5) {
        state = s_camgrab;
    }

    // analog read
    AD1CHS = 2 << 16; // select channel
    AD1CON1bits.SAMP = 1; // initiates sampling
    while (AD1CON1bits.SAMP); // wait til sample done
    while (AD1CON1bits.DONE == 0); // wait til conversion done
    s0 = ADC1BUF0;

    printf(tabx13 taby11 "A:%d  ", s0);


    switch (state) {
        case s_start:
            printf(cls top butcol "EXIT  " whi inv "IMAGEFX" inv butcol "  LIGHT" bot "Efct+");
            printf(bot tabx15 taby12 "Efct-");
            state = s_run;
            camfile = 0;
            for (i = 0; i != cambufsize; cambuffer[i++] = 0);

        case s_run:
            if (!cam_newframe) break;

            printf(tabx8 taby12 butcol);
            if (explock) printf(inv "ExLok" inv);
            else printf("ExLok");
            printf(tabx0 taby11 yel);

            switch (effect) {

                case 0: // update one line per frame
                    printf("Slowscan");

                    if (newEffect) {
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart;
                    }

                    monopalette(0, 255);
                    plotblock(0, 11 + ypixels - val1, xpixels, 1, c_grn);
                    dispimage(0, 12 + ypixels - val1, xpixels, 1, (img_mono | img_revscan), cambuffer + bufstart + val1 * xpixels);
                    if (++val1 == ypixels - 1) val1 = 0;

                    break;
                    unsigned char* charptr;
                    unsigned short* shortptr;


                case 1: // temporal FIR filter
                    printf("Ghost");

                    if (newEffect) {
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufsize + bufstart;
                    }

                    charptr = cambuffer + bufstart;
                    shortptr = cambuffer_s + (bufstart + bufsize) / 2; // shorts to store pixel:fraction as fixed-point to avoid rounding errors
                    y = 250;
                    for (i = 0; i != xpixels * ypixels; i++) {
                        x = *charptr++;
                        d = x * (255 - y)+((unsigned int) *shortptr * y) / 256; // mix proportion of old and new pixel
                        *shortptr++ = d;
                    }
                    monopalette(0, 255);
                    // img_skip skips over the lsbytes, displaying the MSbyte
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan | img_skip1), cambuffer + bufsize + bufstart);

                    break;


                case 3: // use camera capture start parameters to de-stabilise
                    printf("Unstabilise");

                    if (newEffect) {
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart;
                    }

                    effectBase = 1 + ((20 * s0) >> 10);
                    // >> 10 divides by 1024

                    monopalette(0, 255);
                    // original was = 30 + randnum(-15, 15)
                    xstart = 30 + randnum(~effectBase + 1, effectBase);
                    ystart = 30 + randnum(~effectBase + 1, effectBase);
                    // ~effectBase + 1 negates it

                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart);

                    break;
                    unsigned char * inptr, *outptr;
                    int op,np,er,z;


                case 2 :
                    printf("Dither"); // Floyd=stienberg error diffusion

                    if (newEffect) {
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart;
                    }

                    for(y=0 ; y != ypixels; y++) {

                        for (x = 0; x != xpixels; x++) {
                            inptr = cambuffer + bufstart + x + y * xpixels;
                            op = *inptr;

                            if (op > 0x80) np = 0xff; else np = 0;
                            er = op - np;
                            *inptr = np;
                            inptr++; //right
                            z = (int) *inptr + er * 7/16;
                            if (z<0) z = 0; else if (z > 255) z = 255;
                            *inptr = z;
                            inptr += (xpixels - 2); // down & left
                            z = (int) *inptr + er * 3/16;
                            if (z < 0) z = 0; else if (z > 255) z = 255;
                            *inptr ++= z;
                            z = (int) *inptr + er * 5/16;//down
                            if (z < 0) z = 0; else if (z > 255) z = 255;
                            *inptr ++= z;
                            z = (int) *inptr + er * 1/16; //down & right
                            if (z < 0) z = 0; else if (z > 255) z = 255;
                            *inptr = z;

                        } // x
                    } // y

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart);
                    break;


                case 4:
                    printf("Boxcar");

                    if (newEffect) {
                        copyKernel(BoxcarKernel, Kernel);
                        kernelCenter = Kernel[1][1];
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                    }

                    Kernel[1][1] = kernelCenter - ((kernelCenter * s0) >> 10);
                    // >> 10 divides by 1024

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 5, 0);
                    // scaler 5 to /64

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                case 5:
                    printf("SobelX");

                    if (newEffect) {
                        copyKernel(SobelKernelX, Kernel);
                        kernelCenter = Kernel[1][1];
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                    }

                    // @TODO
                    Kernel[1][1] = kernelCenter - ((2 * kernelCenter * s0) >> 10);

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 1, 128);
                    // scaler 1 to /2

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                case 6:
                    printf("SobelY");

                    if (newEffect) {
                        copyKernel(SobelKernelY, Kernel);
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                    }

                    // @TODO .... so...


                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 1, 0); // 5 would be safe
                    // scaler 1 to /2

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                case 7:
                    printf("SobelXY");

                    if (newEffect) {
                        copyKernel(SobelKernelX, Kernel);
                        copyKernel(SobelKernelY, Kernel2);
                        newEffect = FALSE;
                        camgrab_buff = cambuffer + bufstart;
                    }

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 2, 64); // 5 would be safe
                    // scaler 2 to /4

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset2, Kernel2, 2, 64); // 5 would be safe
                    // scaler 2 to /4

                    // process both cambuffmono_offset and cambuffmono_offset2 BACK into cambuffer, via function
                    for (y = 0; y != ypixels; y++) {
                        for (x = 0; x != xpixels; x++) {
                            i = *(cambuffer + bufstart + cambuffmono_offset + x + y * xpixels)
                                + *(cambuffer + bufstart + cambuffmono_offset2 + x + y * xpixels);

                            // @TODO find better way to do abs? Anyway X's and Y's output has already been normalized to 256
                            i = i >> 1; // = /2

                            // if (np < 0) np = 0; else if (np > 255) np = 255;

                            *(cambuffer + bufstart + x + y * xpixels) = i;
                        }
                    }

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart); // @NOTE final img is back in reg buffer!
                    break;


                case 8:
                    printf("Edge3");

                    if (newEffect) {
                        copyKernel(Edge3Kernel, Kernel);
                        kernelCenter = Kernel[1][1];
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                        newEffect = FALSE;
                    }

                    Kernel[1][1] = kernelCenter - ((kernelCenter * s0) >> 10);

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 1, 64); // 8 would be safe
                    // scaler 1 for /2

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                case 9:
                    printf("Sharpen");

                    if (newEffect) {
                        copyKernel(SharpenKernel, Kernel);
                        kernelCenter = Kernel[1][1];
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                        newEffect = FALSE;
                    }

                    Kernel[1][1] = kernelCenter - ((kernelCenter * s0) >> 10);

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 0, 0); // 5 would be safe
                    // NO scaler, was 1 to /2

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                case 10:
                    printf("Invert");

                    if (newEffect) {
                        copyKernel(InvertKernel, Kernel);
                        camgrab_buff = cambuffer + bufstart + cambuffmono_offset;
                        newEffect = FALSE;
                    }

                    convolution(cambuffer + bufstart, cambuffer + bufstart + cambuffmono_offset, Kernel, 0, 255);
                    // scaler 0 to not scale

                    monopalette (0,255);
                    dispimage(0, 12, xpixels, ypixels, (img_mono | img_revscan), cambuffer + bufstart + cambuffmono_offset);
                    break;


                default:
                    effect = 0;

            } // switch effect

            cam_grabenable(camen_grab, bufstart - 1, 0); // buffer swap for new frame

            break;


        // copied from camera.c
        case s_camgrab:
            printf(bot whi);
            state = s_camrestart;
            if (!cardmounted) {
                printf(inv "No Card         " inv del);
                break;
            }

            cam_grabdisable();

            i = FSchdir("\\CAMERA");
            if (i) {
                FSmkdir("CAMERA");
                FSchdir("CAMERA");
            }

            i = 0;
            do { // find first unused filename
                docamname(camfile++, ct_bmp);
                printf(bot "%-21s", camname);
                fptr = FSfopen(camname, FS_READ);
                i = (fptr != NULL);
                if (i) FSfclose(fptr);
            } while (i);

            fptr = FSfopen(camname, FS_WRITE);
            FSchdir("\\"); // exit dir for easier tidyup if error

            i = writebmpheader(xpixels, ypixels, 1);
            if (i == 0) {
                FSfclose(fptr);
                printf("Err writing header" bot "OK");
                state = s_camwait;
                break;
            }

            i = FSfwrite(camgrab_buff, xpixels * ypixels, 1, fptr);
            FSfclose(fptr);
            if (i == 0) {
                printf("Err writing image" bot "OK");
                state = s_camwait;
                break;
            }


            break;

        case s_camrestart:
            if (butstate) break; // in case trig held

            cam_grabenable(camen_start, 7, 0);
            led1_off;
            state = s_start;
            break;


        case s_camwait:
            if (!butpress) break;
            state = s_start;
            cam_grabenable(camen_start, 7, 0);
            break;


        case s_quit:
            cam_grabdisable();

            // copied from scope.c case s_exit
            claimadc(0);
            ANSELBCLR = 3;
            TRISBCLR = 3; // set for input mode

            RPB0Rbits.RPB0R = pb0save;
            RPB1Rbits.RPB1R = pb1save;
            CNPUBSET = 1 << 1; // enable pull up?
            // end scope.c code

            // normal app exit
            return ("");

            break;


    } // switch state

    return (0);


}
