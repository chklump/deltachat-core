/*******************************************************************************
 *
 *                              Delta Chat Core
 *                      Copyright (C) 2017 Björn Petersen
 *                   Contact: r10s@b44t.com, http://b44t.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see http://www.gnu.org/licenses/ .
 *
 ******************************************************************************/


#include <ctype.h>
#include <memory.h>
#include "dc_context.h"
#include "dc_key.h"
#include "dc_pgp.h"
#include "dc_tools.h"


/*******************************************************************************
 * Main interface
 ******************************************************************************/


void dc_wipe_secret_mem(void* buf, size_t buf_bytes)
{
	/* wipe private keys or othere secrets with zeros so that secrets are no longer in RAM */
	if( buf == NULL || buf_bytes <= 0 ) {
		return;
	}

	memset(buf, 0x00, buf_bytes);
}


static void dc_key_empty(dc_key_t* ths) /* only use before calling setters; take care when using this function together with reference counting, prefer new objects instead */
{
	if( ths == NULL ) {
		return;
	}

	if( ths->m_type==DC_KEY_PRIVATE ) {
		dc_wipe_secret_mem(ths->m_binary, ths->m_bytes);
	}

	free(ths->m_binary);
	ths->m_binary = NULL;
	ths->m_bytes = 0;
	ths->m_type = DC_KEY_PUBLIC;
}


dc_key_t* dc_key_new()
{
	dc_key_t* ths;

	if( (ths=calloc(1, sizeof(dc_key_t)))==NULL ) {
		exit(44); /* cannot allocate little memory, unrecoverable error */
	}
	ths->_m_heap_refcnt = 1;
	return ths;
}


dc_key_t* dc_key_ref(dc_key_t* ths)
{
	if( ths==NULL ) {
		return NULL;
	}
	ths->_m_heap_refcnt++;
	return ths;
}


void dc_key_unref(dc_key_t* ths)
{
	if( ths==NULL ) {
		return;
	}

	ths->_m_heap_refcnt--;
	if( ths->_m_heap_refcnt != 0 ) {
		return;
	}

	dc_key_empty(ths);
	free(ths);
}


int dc_key_set_from_binary(dc_key_t* ths, const void* data, int bytes, int type)
{
    dc_key_empty(ths);
    if( ths==NULL || data==NULL || bytes <= 0 ) {
		return 0;
    }
    ths->m_binary = malloc(bytes);
    if( ths->m_binary == NULL ) {
		exit(40);
    }
    memcpy(ths->m_binary, data, bytes);
    ths->m_bytes = bytes;
    ths->m_type = type;
    return 1;
}


int dc_key_set_from_key(dc_key_t* ths, const dc_key_t* o)
{
	dc_key_empty(ths);
	if( ths==NULL || o==NULL ) {
		return 0;
	}
	return dc_key_set_from_binary(ths, o->m_binary, o->m_bytes, o->m_type);
}


int dc_key_set_from_stmt(dc_key_t* ths, sqlite3_stmt* stmt, int index, int type)
{
	dc_key_empty(ths);
	if( ths==NULL || stmt==NULL ) {
		return 0;
	}
	return dc_key_set_from_binary(ths, (unsigned char*)sqlite3_column_blob(stmt, index), sqlite3_column_bytes(stmt, index), type);
}


int dc_key_set_from_base64(dc_key_t* ths, const char* base64, int type)
{
	size_t indx = 0, result_len = 0;
	char* result = NULL;

	dc_key_empty(ths);

	if( ths==NULL || base64==NULL ) {
		return 0;
	}

	if( mailmime_base64_body_parse(base64, strlen(base64), &indx, &result/*must be freed using mmap_string_unref()*/, &result_len)!=MAILIMF_NO_ERROR
	 || result == NULL || result_len == 0 ) {
		return 0; /* bad key */
	}

	dc_key_set_from_binary(ths, result, result_len, type);
	mmap_string_unref(result);

	return 1;
}


int dc_key_set_from_file(dc_key_t* ths, const char* pathNfilename, dc_context_t* mailbox)
{
	char*      buf = NULL;
	const char *headerline, *base64; // just pointers inside buf, must not be freed
	size_t     buf_bytes;
	int        type = -1, success = 0;

	dc_key_empty(ths);

	if( ths==NULL || pathNfilename==NULL ) {
		goto cleanup;
	}

	if( !dc_read_file(pathNfilename, (void**)&buf, &buf_bytes, mailbox)
	 || buf_bytes < 50 ) {
		goto cleanup; /* error is already loged */
	}

	if( !dc_split_armored_data(buf, &headerline, NULL, NULL, &base64)
	 || headerline == NULL || base64 == NULL ) {
		goto cleanup;
	}

	if( strcmp(headerline, "-----BEGIN PGP PUBLIC KEY BLOCK-----")==0 ) {
		type = DC_KEY_PUBLIC;
	}
	else if( strcmp(headerline, "-----BEGIN PGP PRIVATE KEY BLOCK-----")==0 ) {
		type = DC_KEY_PRIVATE;
	}
	else {
		dc_log_warning(mailbox, 0, "Header missing for key \"%s\".", pathNfilename);
		goto cleanup;
	}

	if( !dc_key_set_from_base64(ths, base64, type) ) {
		dc_log_warning(mailbox, 0, "Bad data in key \"%s\".", pathNfilename);
		goto cleanup;
	}

	success = 1;

cleanup:
	free(buf);
	return success;
}


int dc_key_equals(const dc_key_t* ths, const dc_key_t* o)
{
	if( ths==NULL || o==NULL
	 || ths->m_binary==NULL || ths->m_bytes<=0 || o->m_binary==NULL || o->m_bytes<=0 ) {
		return 0; /*error*/
	}

	if( ths->m_bytes != o->m_bytes ) {
		return 0; /*different size -> the keys cannot be equal*/
	}

	if( ths->m_type != o->m_type ) {
		return 0; /* cannot compare public with private keys */
	}

	return memcmp(ths->m_binary, o->m_binary, o->m_bytes)==0? 1 : 0;
}


/*******************************************************************************
 * Save/Load keys
 ******************************************************************************/


int dc_key_save_self_keypair__(const dc_key_t* public_key, const dc_key_t* private_key, const char* addr, int is_default, dc_sqlite3_t* sql)
{
	sqlite3_stmt* stmt;

	if( public_key==NULL || private_key==NULL || addr==NULL || sql==NULL
	 || public_key->m_binary==NULL || private_key->m_binary==NULL ) {
		return 0;
	}

	stmt = dc_sqlite3_predefine__(sql, INSERT_INTO_keypairs_aippc,
		"INSERT INTO keypairs (addr, is_default, public_key, private_key, created) VALUES (?,?,?,?,?);");
	sqlite3_bind_text (stmt, 1, addr, -1, SQLITE_STATIC);
	sqlite3_bind_int  (stmt, 2, is_default);
	sqlite3_bind_blob (stmt, 3, public_key->m_binary, public_key->m_bytes, SQLITE_STATIC);
	sqlite3_bind_blob (stmt, 4, private_key->m_binary, private_key->m_bytes, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 5, time(NULL));
	if( sqlite3_step(stmt) != SQLITE_DONE ) {
		return 0;
	}

	return 1;
}


int dc_key_load_self_public__(dc_key_t* ths, const char* self_addr, dc_sqlite3_t* sql)
{
	sqlite3_stmt* stmt;

	if( ths==NULL || self_addr==NULL || sql==NULL ) {
		return 0;
	}

	dc_key_empty(ths);
	stmt = dc_sqlite3_predefine__(sql, SELECT_public_key_FROM_keypairs_WHERE_default,
		"SELECT public_key FROM keypairs WHERE addr=? AND is_default=1;");
	sqlite3_bind_text (stmt, 1, self_addr, -1, SQLITE_STATIC);
	if( sqlite3_step(stmt) != SQLITE_ROW ) {
		return 0;
	}
	dc_key_set_from_stmt(ths, stmt, 0, DC_KEY_PUBLIC);
	return 1;
}


int dc_key_load_self_private__(dc_key_t* ths, const char* self_addr, dc_sqlite3_t* sql)
{
	sqlite3_stmt* stmt;

	if( ths==NULL || self_addr==NULL || sql==NULL ) {
		return 0;
	}

	dc_key_empty(ths);
	stmt = dc_sqlite3_predefine__(sql, SELECT_private_key_FROM_keypairs_WHERE_default,
		"SELECT private_key FROM keypairs WHERE addr=? AND is_default=1;");
	sqlite3_bind_text (stmt, 1, self_addr, -1, SQLITE_STATIC);
	if( sqlite3_step(stmt) != SQLITE_ROW ) {
		return 0;
	}
	dc_key_set_from_stmt(ths, stmt, 0, DC_KEY_PRIVATE);
	return 1;
}


/*******************************************************************************
 * Render keys
 ******************************************************************************/


static long crc_octets(const unsigned char *octets, size_t len)
{
	#define CRC24_INIT 0xB704CEL
	#define CRC24_POLY 0x1864CFBL
	long crc = CRC24_INIT;
	int i;
	while (len--) {
		crc ^= (*octets++) << 16;
		for (i = 0; i < 8; i++) {
			crc <<= 1;
			if (crc & 0x1000000)
			crc ^= CRC24_POLY;
		}
	}
	return crc & 0xFFFFFFL;
}


char* dc_render_base64(const void* buf, size_t buf_bytes, int break_every, const char* break_chars,
                       int add_checksum /*0=no checksum, 1=add without break, 2=add with break_chars*/)
{
	char* ret = NULL;

	if( buf==NULL || buf_bytes<=0 ) {
		goto cleanup;
	}

	if( (ret = encode_base64((const char*)buf, buf_bytes))==NULL ) {
		goto cleanup;
	}

	#if 0
	if( add_checksum == 1/*appended checksum*/ ) {
		long checksum = crc_octets(buf, buf_bytes);
		uint8_t c[3];
		c[0] = (uint8_t)((checksum >> 16)&0xFF);
		c[1] = (uint8_t)((checksum >> 8)&0xFF);
		c[2] = (uint8_t)((checksum)&0xFF);
		char* c64 = encode_base64((const char*)c, 3);
			char* temp = ret;
				ret = dc_mprintf("%s=%s", temp, c64);
			free(temp);
		free(c64);
	}
	#endif

	if( break_every>0 ) {
		char* temp = ret;
			ret = dc_insert_breaks(temp, break_every, break_chars);
		free(temp);
	}

	if( add_checksum == 2/*checksum with break character*/ ) {
		long checksum = crc_octets(buf, buf_bytes);
		uint8_t c[3];
		c[0] = (uint8_t)((checksum >> 16)&0xFF);
		c[1] = (uint8_t)((checksum >> 8)&0xFF);
		c[2] = (uint8_t)((checksum)&0xFF);
		char* c64 = encode_base64((const char*)c, 3);
			char* temp = ret;
				ret = dc_mprintf("%s%s=%s", temp, break_chars, c64);
			free(temp);
		free(c64);
	}

cleanup:
	return ret;
}


char* dc_key_render_base64(const dc_key_t* ths, int break_every, const char* break_chars, int add_checksum)
{
	if( ths==NULL ) {
		return NULL;
	}
	return dc_render_base64(ths->m_binary, ths->m_bytes, break_every, break_chars, add_checksum);
}


char* dc_key_render_asc(const dc_key_t* ths, const char* add_header_lines /*must be terminated by \r\n*/)
{
	/* see RFC 4880, 6.2.  Forming ASCII Armor, https://tools.ietf.org/html/rfc4880#section-6.2 */
	char *base64 = NULL, *ret = NULL;

	if( ths==NULL ) {
		goto cleanup;
	}

	if( (base64=dc_key_render_base64(ths, 76, "\r\n", 2/*checksum in new line*/))==NULL ) { /* RFC: The encoded output stream must be represented in lines of no more than 76 characters each. */
		goto cleanup;
	}

	ret = dc_mprintf("-----BEGIN PGP %s KEY BLOCK-----\r\n%s\r\n%s\r\n-----END PGP %s KEY BLOCK-----\r\n",
		ths->m_type==DC_KEY_PUBLIC? "PUBLIC" : "PRIVATE",
		add_header_lines? add_header_lines : "",
		base64,
		ths->m_type==DC_KEY_PUBLIC? "PUBLIC" : "PRIVATE");

cleanup:
	free(base64);
	return ret;
}


int dc_key_render_asc_to_file(const dc_key_t* key, const char* file, dc_context_t* mailbox /* for logging only */)
{
	int   success = 0;
	char* file_content = NULL;

	if( key == NULL || file == NULL || mailbox == NULL ) {
		goto cleanup;
	}

	file_content = dc_key_render_asc(key, NULL);
	if( file_content == NULL ) {
		goto cleanup;
	}

	if( !dc_write_file(file, file_content, strlen(file_content), mailbox) ) {
		dc_log_error(mailbox, 0, "Cannot write key to %s", file);
		goto cleanup;
	}

cleanup:
	free(file_content);
	return success;
}


/* make a fingerprint human-readable */
char* dc_format_fingerprint(const char* fingerprint)
{
	int i = 0, fingerprint_len = strlen(fingerprint);
	dc_strbuilder_t ret;
	dc_strbuilder_init(&ret, 0);

    while( fingerprint[i] ) {
		dc_strbuilder_catf(&ret, "%c", fingerprint[i]);
		i++;
		if( i != fingerprint_len ) {
			if( i%20 == 0 ) {
				dc_strbuilder_cat(&ret, "\n");
			}
			else if( i%4 == 0 ) {
				dc_strbuilder_cat(&ret, " ");
			}
		}
    }

	return ret.m_buf;
}


/* bring a human-readable or otherwise formatted fingerprint back to the
40-characters-uppercase-hex format */
char* dc_normalize_fingerprint(const char* in)
{
	if( in == NULL ) {
		return NULL;
	}

	dc_strbuilder_t out;
	dc_strbuilder_init(&out, 0);

	const char* p1 = in;
	while( *p1 ) {
		if( (*p1 >= '0' && *p1 <= '9') || (*p1 >= 'A' && *p1 <= 'F') || (*p1 >= 'a' && *p1 <= 'f') ) {
			dc_strbuilder_catf(&out, "%c", toupper(*p1)); /* make uppercase which is needed as we do not search case-insensitive, see comment in dc_sqlite3.c */
		}
		p1++;
	}

	return out.m_buf;
}


char* dc_key_get_fingerprint(const dc_key_t* key)
{
	uint8_t* fingerprint_buf = NULL;
	size_t   fingerprint_bytes = 0;
	char*    fingerprint_hex = NULL;

	if( key == NULL ) {
		goto cleanup;
	}

	if( !dc_pgp_calc_fingerprint(key, &fingerprint_buf, &fingerprint_bytes) ) {
		goto cleanup;
	}

	fingerprint_hex = dc_binary_to_uc_hex(fingerprint_buf, fingerprint_bytes);

cleanup:
	free(fingerprint_buf);
	return fingerprint_hex? fingerprint_hex : dc_strdup(NULL);
}


char* dc_key_get_formatted_fingerprint(const dc_key_t* key)
{
	char* rawhex = dc_key_get_fingerprint(key);
	char* formatted = dc_format_fingerprint(rawhex);
	free(rawhex);
	return formatted;
}