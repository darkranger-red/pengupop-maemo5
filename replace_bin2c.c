/*  Convert binary files to C source code.
    Copyright (C) 2007, 2008, 2009  Morten Hustveit <morten@rashbox.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
  FILE* input;
  FILE* output;
  char* symbol;
  size_t sz;
  int ch, n = 0;

  if(argc != 4)
    return EXIT_FAILURE;

  input = fopen(argv[1], "r");
  output = fopen(argv[2], "w");

  if(!input || !output)
    return EXIT_FAILURE;

  symbol = argv[3];

  fseek(input, 0, SEEK_END);
  sz = ftell(input);
  fseek(input, 0, SEEK_SET);

  fprintf(output, "static size_t size_%s = %zu;\n"
                  "static unsigned char %s[] = {\n", symbol, sz, symbol);

  while(EOF != (ch = getc(input)))
  {
    if((n++ & 0xf) == 0xf)
      fprintf(output, "\n");
    fprintf(output, " 0x%02x,", ch);
  }

  fprintf(output, "\n};\n");

  fclose(output);
  fclose(input);

  return EXIT_SUCCESS;
}
