/* misc utility routines, accumulated over the years by Steve Tell.
 */

#include <stdio.h>

void
xprint(FILE *fp, unsigned char *buf, int len)
{
	int i;

	for(i = 0; i < len; i++)
		fprintf(fp, "%02x ", buf[i] & 0xff);
}

/*
 * print character,  in C style.
 */
void cputc(char c, FILE *fp)
{
        switch(c & 0xff) {
        case '\n':
                fprintf(fp, "\\n");
                break;
        case '\r':
                fprintf(fp, "\\r");
                break;
        case '\t':
                fprintf(fp, "\\t");
                break;
        case '\f':
                fprintf(fp, "\\f");
                break;
        case '\b':
                fprintf(fp, "\\b");
                break;
        default:
                if(c < ' ') {
                        fprintf(fp, "\\\%03o", c);
                } else {
                        fputc(c, fp);
                }
        }
}

void
cprint(FILE *fp, unsigned char *buf, int len)
{
	int i;

	for(i = 0; i < len; i++)
		cputc(buf[i] & 0xff, fp);
}
