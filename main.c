/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * Copyright (c) 2013 Nedko Arnaudov <nedko@arnaudov.name>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <ftdi.h>

#define FT245R_OFFSET_MANUFACTURER 0x0E
#define FT245R_OFFSET_PRODUCT      0x10
#define FT245R_OFFSET_SERIAL       0x12
#define FT245R_OFFSET_UA           0x18

bool parse_ua_string(unsigned char * buf, size_t buf_size, size_t offset, char ** out)
{
  size_t string_size;
  char * string;
  size_t i;

  string_size = buf[offset + 1] / 2;
  if (string_size == 0)
  {
    *out = NULL;
    return true;
  }

  offset = buf[offset] & (buf_size -1);
  string = malloc(string_size);
  if (string == NULL)
  {
    return false;
  }

  for (i = 0; i < string_size - 1; i++)
    string[i] = buf[offset + 2 + 2 * i];

  string[i] = '\0';

  *out = string;
  return true;
}

void
set_ua_string(
  unsigned char * buf,
  size_t buf_size,
  size_t pointer_offset,
  size_t * ua_offset_ptr,
  char * string)
{
  size_t string_size;
  size_t ua_offset;

  if (buf_size != 128) abort();

  string_size = strlen(string);
  ua_offset = *ua_offset_ptr;

  buf[pointer_offset] = 0x80 + *ua_offset_ptr;
  buf[pointer_offset + 1] = 2 + string_size * 2;

  buf[ua_offset++] = 2 + string_size * 2;
  buf[ua_offset++] = 0x03;
  while (string_size--)
  {
    buf[ua_offset++] = *string++;
    buf[ua_offset++] = 0x00;
  }

  *ua_offset_ptr = ua_offset;
}

struct user_area
{
  char * manufacturer;
  char * product;
  char * serial;
};

struct user_area * user_area_create(void)
{
  return calloc(1, sizeof(struct user_area));
}

void user_area_destroy(struct user_area * ua_ptr)
{
  free(ua_ptr->manufacturer);
  ua_ptr->manufacturer = NULL;

  free(ua_ptr->product);
  ua_ptr->product = NULL;

  free(ua_ptr->serial);
  ua_ptr->serial = NULL;

  free(ua_ptr);
}

struct user_area * parse_user_area(unsigned char * buf, int size)
{
  struct user_area * ua_ptr;

  ua_ptr = user_area_create();
  if (ua_ptr == NULL) return NULL;

  if (!parse_ua_string(buf, size, FT245R_OFFSET_MANUFACTURER, &ua_ptr->manufacturer) ||
      !parse_ua_string(buf, size, FT245R_OFFSET_PRODUCT, &ua_ptr->product) ||
      !parse_ua_string(buf, size, FT245R_OFFSET_SERIAL, &ua_ptr->serial))
  {
    user_area_destroy(ua_ptr);
    return NULL;
  }

  return ua_ptr;
}

void set_user_area(unsigned char * buf, int size, struct user_area * ua_ptr)
{
  size_t ua_offset;

  if (size != 128) abort();
  ua_offset = FT245R_OFFSET_UA;

  memset(buf + ua_offset, 0, size - ua_offset);
  set_ua_string(buf, size, FT245R_OFFSET_MANUFACTURER, &ua_offset, ua_ptr->manufacturer);
  set_ua_string(buf, size, FT245R_OFFSET_PRODUCT, &ua_offset, ua_ptr->product);
  set_ua_string(buf, size, FT245R_OFFSET_SERIAL, &ua_offset, ua_ptr->serial);
}

void compute_checksum(unsigned char * buf, int size)
{
  unsigned short checksum, value;
  unsigned char i;

  checksum = 0xAAAA;

  for (i = 0; i < size / 2 - 1; i++)
  {
    value = buf[i * 2];
    value += buf[(i * 2) + 1] << 8;

    checksum = value^checksum;
    checksum = (checksum << 1) | (checksum >> 15);
  }

  buf[size - 2] = checksum;
  buf[size - 1] = checksum >> 8;
}

bool set_serial(unsigned char * buf, int size, const char * newserial)
{
  struct user_area * ua_ptr;

  ua_ptr = parse_user_area(buf, size);
  if (ua_ptr == NULL) return false;

  /* printf("manufacturer='%s'\n", ua_ptr->manufacturer); */
  /* printf("product='%s'\n", ua_ptr->product); */
  /* printf("serial='%s'\n", ua_ptr->serial); */

  free(ua_ptr->serial);
  ua_ptr->serial = strdup(newserial);
  if (ua_ptr->serial == NULL)
  {
    user_area_destroy(ua_ptr);
    return false;
  }

  set_user_area(buf, size, ua_ptr);

  compute_checksum(buf, size);

  return true;
}

void print_ftdi_error(struct ftdi_context * ftdi, int ret, const char * descr)
{
  fprintf(stderr, "%s: %d (%s)\n", descr, ret, ftdi_get_error_string(ftdi));
}

bool write_eeprom_to_file(unsigned char * buf, int size, const char * filename)
{
  FILE * f;

  f = fopen(filename, "w");
  if (f == NULL) return false;

  fwrite(buf, size, 1, f);
  fclose(f);

  return true;
}

bool create_modified_eeprom_file(struct ftdi_context * ftdi, const char * newserial)
{
  int ret;
  unsigned char buf[128];
  int size;

  if (ftdi->type != TYPE_R)
  {
    fprintf(stderr, "Non type-R ftdi chip\n");
    return false;
  }

  ret = ftdi_read_eeprom(ftdi);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot read ftdi eeprom: %d (%s)");
    return false;
  }

  ret = ftdi_get_eeprom_value(ftdi, CHIP_SIZE, &size);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot get eeprom size");
    return false;
  }

  if (size != 128)
  {
    fprintf(stderr, "EEPROM size is not 128\n");
    return false;
  }

  ret = ftdi_get_eeprom_buf(ftdi, buf, size);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot get eeprom buffer");
    return false;
  }

  if (!set_serial(buf, size, newserial))
  {
    fprintf(stderr, "Out of memory\n");
    return false;
  }

#if 0
  ret = ftdi_set_eeprom_buf(ftdi, buf, size);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot set eeprom buffer");
    return false;
  }

  ret = ftdi_eeprom_decode(ftdi, 1);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot decode eeprom");
    return false;
  }
#endif

  if (!write_eeprom_to_file(buf, size, "ftdi245r.eeprom"))
  {
    fprintf(stderr, "Can't write eeprom file.\n");
    return false;
  }

  return true;
}

int main(int argc, char ** argv)
{
  const char * newserial;
  int ret;
  struct ftdi_context * ftdi;

  if (argc != 2)
  {
    fprintf(stderr, "Usage: ftdi245r_serial <serial>\n");
    ret = EXIT_FAILURE;
    goto exit;
  }

  newserial = argv[1];

  //printf("newserial='%s'\n", newserial);

  ftdi = ftdi_new();
  if (ftdi == NULL)
  {
    fprintf(stderr, "Cannot create a libftdi context\n");
    ret = EXIT_FAILURE;
    goto exit;
  }

  ret = ftdi_usb_open(ftdi, 0x0403, 0x6001);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot open ftdi device");
    ret = EXIT_FAILURE;
    goto free;
  }

  if (!create_modified_eeprom_file(ftdi, newserial))
  {
    ret = EXIT_FAILURE;
    goto close;
  }

  ret = ftdi_usb_close(ftdi);
  if (ret < 0)
  {
    print_ftdi_error(ftdi, ret, "Cannot close ftdi device");
    ret = EXIT_FAILURE;
    goto free;
  }

  ret = EXIT_SUCCESS;
  goto free;

close:
  ftdi_usb_close(ftdi);
free:
  ftdi_free(ftdi);
exit:
  return ret;
}
