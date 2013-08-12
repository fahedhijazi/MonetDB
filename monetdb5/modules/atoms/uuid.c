/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 * 
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * The Original Code is the MonetDB Database System.
 * 
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
*/

/*
 *  A. de Rijke
 * The UUID module
 * The UUID module contains a wrapper for all function in
 * libuuid.
 */

#include "monetdb_config.h"
#include "uuid.h"
#include "mal.h"
#include "mal_exception.h"
#include "muuid.h"

#define UUID_LEN 36
/**
 * Returns the string representation of the given uuid value.
 * Warning: GDK function
 * Returns the length of the string
 */
int
UUIDtoString(str *retval, int *len, str value)
{
        if (*len < UUID_LEN) {
                if (*retval != NULL)
                        GDKfree(*retval);
                *retval = GDKmalloc(sizeof(str) * (*len = UUID_LEN));
        }
        if (value == str_nil) {
                *len = snprintf(*retval, *len, "(nil)");
	} else {
		strncpy(*retval, value, UUID_LEN);
		(*retval)[UUID_LEN] = 0;
		*len = UUID_LEN;
        }
        return(*len);
}

static str
uuid_GenerateUuid(str *retval) {
  str d;
  char * s;

  s = generateUUID();
  if ( s == NULL)
    throw(MAL, "uuid.generateUuid", "Allocation failed");
  d = GDKstrdup(s);

  if (d == NULL)
    throw(MAL, "uuid.generateUuid", "Allocation failed");

  *retval = d;
  free(s);
  return MAL_SUCCEED;
}

str
UUIDgenerateUuid(str *retval) {
  return uuid_GenerateUuid(retval);
}

str
UUIDisaUUID(bit *retval, str *s) {
  *retval = strlen(*s) == UUID_LEN;
  return MAL_SUCCEED;
}

str
UUIDstr2uuid(str *retval, str *s) {
  bit b=0;
  str msg = UUIDisaUUID(&b, s);
  if ( msg != MAL_SUCCEED)
	return msg;
  if ( b== 0)
	throw(MAL,"uuid.uuid","Inconsistent UUID length");
  *retval = GDKstrdup(*s);
  return MAL_SUCCEED;
}

str
UUIDuuid2str(str *retval, str *s) {
  *retval = GDKstrdup(*s);
  return MAL_SUCCEED;
}

str
UUIDequal(bit *retval, str *l, str *r)
{
	if (*l == str_nil || *r == str_nil)
		*retval = bit_nil;
	*retval = strcmp(*l,*r) == 0;
	return MAL_SUCCEED;
}
