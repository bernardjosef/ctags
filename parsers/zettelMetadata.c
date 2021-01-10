/*
 *
 *	 Copyright (c) 2020-2021, Bernd Rellermeyer
 *
 *	 This source code is released for free distribution under the terms of the
 *	 GNU General Public License version 2 or (at your option) any later version.
 *
 */

#include "general.h"	/* must always come first */
#include "yaml.h"
#include "entry.h"
#include "options.h"
#include "read.h"
#include "param.h"
#include "parse.h"
#include "trashbox.h"

// The following lines are copied from fmt_p.h and fmt.c and needed for
// zmRenderFieldSummary.
typedef struct sFmtElement fmtElement;
extern fmtElement *fmtNew (const char* fmtString);
extern int fmtPrint (fmtElement * fmtelts, MIO* fp, const tagEntryInfo *tag);
extern void fmtDelete (fmtElement * fmtelts);

typedef union uFmtSpec {
	char *const_str;
	struct {
		fieldType ftype;
		int width;
		char *raw_fmtstr;
	} field;
} fmtSpec;

struct sFmtElement {
	union uFmtSpec spec;
	int (* printer) (fmtSpec*, MIO* fp, const tagEntryInfo *);
	struct sFmtElement *next;
};

enum zettelMetadataRole {
	R_NONE = -1,
	R_INDEX = 0,
	R_BIBLIOGRAPHY = 0,
	R_REFERENCES = 0
};

static roleDefinition ZettelMetadataKeywordRoleTable [] = {
	{
		true, "index", "index entries"
	}
};

static roleDefinition ZettelMetadataBibliographyRoleTable [] = {
	{
		true, "bibliography", "bibliography files"
	}
};

static roleDefinition ZettelMetadataCitekeyRoleTable [] = {
	{
		true, "reference", "reference entries"
	}
};

enum zettelMetadataKind {
	K_NONE = -1,
	K_ID,
	K_TITLE,
	K_KEYWORD,
	K_BIBLIOGRAPHY,
	K_CITEKEY,
	K_REFTITLE
};

static kindDefinition ZettelMetadataKindTable [] = {
	{
		true, 'i', "id", "identifiers"
	},
	{
		true, 't', "title", "titles"
	},
	{
		true, 'k', "keyword", "keywords",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMetadataKeywordRoleTable)
	},
	{
		true, 'b', "bibliography", "bibliography files",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMetadataBibliographyRoleTable)
	},
	{
		true, 'c', "citekey", "citation keys",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMetadataCitekeyRoleTable)
	},
	{
		true, 'r', "reftitle", "reference titles"
	}
};

static const char *zmRenderFieldTag (const tagEntryInfo *const tag,
									 const char *value CTAGS_ATTR_UNUSED,
									 vString *buffer);

static const char *zmRenderFieldSummary (const tagEntryInfo *const tag,
										 const char *value CTAGS_ATTR_UNUSED,
										 vString *buffer);

enum zettelMetadataField {
	F_TAG,
	F_SUMMARY,
	F_IDENTIFIER,
	F_TITLE
};

static fieldDefinition ZettelMetadataFieldTable [] = {
	{
		.name = "tag",
		.description = "escaped tag name",
		.render = zmRenderFieldTag,
		.enabled = false
	},
	{
		.name = "summary",
		.description = "summary line",
		.render = zmRenderFieldSummary,
		.enabled = false
	},
	{
		.name = "identifier",
		.description = "zettel identifier or citation key",
		.enabled = false
	},
	{
		.name = "title",
		.description = "zettel title or reference title",
		.enabled = false
	}
};

static char *zmSummaryFormat = "%{ZettelMetadata.identifier}:%{ZettelMetadata.title}";
static char *zmTitlePrefix = NULL;
static char *zmReftitlePrefix = NULL;
static char *zmKeywordPrefix = NULL;
static char *zmBibliographyPrefix = NULL;

static void zmSetSummaryFormat (const langType language CTAGS_ATTR_UNUSED,
								const char *name,
								const char *arg)
{
	zmSummaryFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetTitlePrefix (const langType language CTAGS_ATTR_UNUSED,
							  const char *name CTAGS_ATTR_UNUSED,
							  const char *arg)
{
	zmTitlePrefix = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetReftitlePrefix (const langType language CTAGS_ATTR_UNUSED,
								 const char *name CTAGS_ATTR_UNUSED,
								 const char *arg)
{
	zmReftitlePrefix = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetKeywordPrefix (const langType language CTAGS_ATTR_UNUSED,
								const char *name CTAGS_ATTR_UNUSED,
								const char *arg)
{
	zmKeywordPrefix = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetBibliographyPrefix (const langType language CTAGS_ATTR_UNUSED,
									 const char *name CTAGS_ATTR_UNUSED,
									 const char *arg)
{
	zmBibliographyPrefix = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static parameterHandlerTable ZettelMetadataParameterHandlerTable [] = {
	{
		.name = "summary-format",
		.desc = "Summary format string (string)",
		.handleParameter = zmSetSummaryFormat
	},
	{
		.name = "title-prefix",
		.desc = "Prepend title tags (string)",
		.handleParameter = zmSetTitlePrefix
	},
	{
		.name = "reftitle-prefix",
		.desc = "Prepend reftitle tags (string)",
		.handleParameter = zmSetReftitlePrefix
	},
	{
		.name = "keyword-prefix",
		.desc = "Prepend reftitle tags (string)",
		.handleParameter = zmSetKeywordPrefix
	},
	{
		.name = "bibliography-prefix",
		.desc = "Prepend bibliography tags (string)",
		.handleParameter = zmSetBibliographyPrefix
	}
};

static char *zmStringEscape (char *buffer, char *string,
							 size_t length, bool force)
{
	static char hex[] = "0123456789abcdef";

	size_t l = string == NULL ? 0 : strlen (string);
	if (length > l) length = l;

	char *p = buffer;
	char *c = string;

	for (int i = 0; i < length; c++, i++)
	{
		if (i < length &&
			(force || *c < 0x21 || *c > 0x7E || *c == BACKSLASH))
		{
			*p++ = BACKSLASH;
			*p++ = 'x';
			*p++ = hex[*c >> 4 & 0x0F];
			*p++ = hex[*c & 0x0F];
		}
		else
			*p++ = *c;
	}

	return p;
}

static const char *zmRenderFieldTag (const tagEntryInfo *const tag,
									 const char *value CTAGS_ATTR_UNUSED,
									 vString *buffer)
{
	int kind = tag->kindIndex;
	char *c = (char *)tag->name;

	switch (kind)
	{
		case K_TITLE:
		case K_REFTITLE:
		case K_KEYWORD:
		case K_BIBLIOGRAPHY:
		{
			/* Escape zettel titles, reference titles, keywords and
			 * bibliography files. */
			size_t length = c == NULL ? 0 : 3 * strlen (c) + 1;
			char *b = xMalloc (3 * length + 1, char);
			char *p = b;

			/* Skip the prefix of a prepended string or escape the first character
			 * if the beginning of the string matches another prefix. */
			size_t titlePrefixSize = zmTitlePrefix == NULL
				? 0
				: strlen (zmTitlePrefix);
			size_t reftitlePrefixSize = zmReftitlePrefix == NULL
				? 0
				: strlen (zmReftitlePrefix);
			size_t keywordPrefixSize = zmKeywordPrefix == NULL
				? 0
				: strlen (zmKeywordPrefix);
			size_t bibliographyPrefixSize = zmBibliographyPrefix == NULL
				? 0
				: strlen (zmBibliographyPrefix);

			if (kind == K_TITLE)
			{
				if ((titlePrefixSize > 0 &&
					 strncmp (c, zmTitlePrefix, titlePrefixSize) == 0))
				{
					p = strncpy (p, c, titlePrefixSize) + titlePrefixSize;
					c += titlePrefixSize;
				}
				else if ((reftitlePrefixSize > 0 &&
						  strncmp (c, zmReftitlePrefix, reftitlePrefixSize) == 0) ||
						 (keywordPrefixSize > 0 &&
						  strncmp (c, zmKeywordPrefix, keywordPrefixSize) == 0) ||
						 (bibliographyPrefixSize > 0 &&
						  strncmp (c, zmBibliographyPrefix, bibliographyPrefixSize) == 0))
					p = zmStringEscape (p, c++, 1, true);
			}
			else if (kind == K_REFTITLE)
			{
				if ((reftitlePrefixSize > 0 &&
					 strncmp (c, zmReftitlePrefix, reftitlePrefixSize) == 0))
				{
					p = strncpy (p, c, reftitlePrefixSize) + reftitlePrefixSize;
					c += reftitlePrefixSize;
				}
				else if ((titlePrefixSize > 0 &&
						  strncmp (c, zmTitlePrefix, titlePrefixSize) == 0) ||
						 (keywordPrefixSize > 0 &&
						  strncmp (c, zmKeywordPrefix, keywordPrefixSize) == 0) ||
						 (bibliographyPrefixSize > 0 &&
						  strncmp (c, zmBibliographyPrefix, bibliographyPrefixSize) == 0))
					p = zmStringEscape (p, c++, 1, true);
			}
			else if (kind == K_KEYWORD)
			{
				if ((keywordPrefixSize > 0 &&
					 strncmp (c, zmKeywordPrefix, keywordPrefixSize) == 0))
				{
					p = strncpy (p, c, keywordPrefixSize) + keywordPrefixSize;
					c += keywordPrefixSize;
				}
				else if ((titlePrefixSize > 0 &&
						  strncmp (c, zmTitlePrefix, titlePrefixSize) == 0) ||
						 (reftitlePrefixSize > 0 &&
						  strncmp (c, zmReftitlePrefix, reftitlePrefixSize) == 0) ||
						 (bibliographyPrefixSize > 0 &&
						  strncmp (c, zmBibliographyPrefix, bibliographyPrefixSize) == 0))
					p = zmStringEscape (p, c++, 1, true);
			}
			else if (kind == K_BIBLIOGRAPHY)
			{
				if ((bibliographyPrefixSize > 0 &&
					 strncmp (c, zmBibliographyPrefix, bibliographyPrefixSize) == 0))
				{
					p = strncpy (p, c, bibliographyPrefixSize) + bibliographyPrefixSize;
					c += bibliographyPrefixSize;
				}
				else if ((titlePrefixSize > 0 &&
						  strncmp (c, zmTitlePrefix, titlePrefixSize) == 0) ||
						 (reftitlePrefixSize > 0 &&
						  strncmp (c, zmReftitlePrefix, reftitlePrefixSize) == 0) ||
						 (keywordPrefixSize > 0 &&
						  strncmp (c, zmKeywordPrefix, keywordPrefixSize) == 0))
					p = zmStringEscape (p, c++, 1, true);
			}

			/* Escape a leading exclamation mark as it conflicts with pseudo-tags
			 * when sorting. */
			if (p == b && *c == '!')
				p = zmStringEscape (p, c++, 1, true);
			p = zmStringEscape (p, c, b + length - p, false);

			vStringNCatS (buffer, b, p - b);
			eFree (b);
			break;
		}
		default:
			/* Do not escape zettel identifiers and citation keys. */
			vStringCatS (buffer, c);

			/* Find the first unexpected character for a warning message. */
			if (*c != '!') while (*c > 0x20 && *c < 0x7F) c++;
			if (*c)
				verbose ("Unexpected character %#04x in tag %s\n",
						 (unsigned char)*c, vStringValue (buffer));
			break;
	}

	return vStringValue (buffer);
}

static const char *zmRenderFieldSummary (const tagEntryInfo *const tag,
										 const char *value CTAGS_ATTR_UNUSED,
										 vString *buffer)
{
	static fmtElement *fmt = NULL;
	if (fmt == NULL) fmt = fmtNew (zmSummaryFormat);

	MIO *mio = mio_new_memory (NULL, 0, eRealloc, eFreeNoNullCheck);

	fmtPrint (fmt, mio, tag);

	size_t size = 0;
	char *s = (char *)mio_memory_get_data (mio, &size);
	vStringNCatS (buffer, s, size);

	mio_unref (mio);

	return vStringValue (buffer);
}

enum zettelMetadataScalar {
	S_NONE,
	S_KEY,
	S_REFERENCE,
	S_VALUE
};

typedef struct sZmYamlTokenTypeStack {
	yaml_token_type_t token_type;
	struct sZmYamlTokenTypeStack *next;
} zmYamlTokenTypeStack;

typedef struct sZmCorkIndexStack {
	int corkIndex;
	struct sZmCorkIndexStack *next;
} zmCorkIndexStack;

typedef struct sZmSubparser {
	yamlSubparser yaml;
	enum zettelMetadataKind kind;
	enum zettelMetadataRole role;
	enum zettelMetadataScalar scalarType;
	zmYamlTokenTypeStack *blockTypeStack;
	bool reference;
	int mapping;
	int sequence;
	char *id;
	char *title;
	char *citekey;
	char *reftitle;
	zmCorkIndexStack *corkStack;
	zmCorkIndexStack *refCorkStack;
} zmSubparser;

static void zmPushTokenType (zmSubparser *subparser,
							 yaml_token_type_t token_type)
{
	zmYamlTokenTypeStack *t;

	t = xMalloc (1, zmYamlTokenTypeStack);
	t->token_type = token_type;
	t->next = subparser->blockTypeStack;
	subparser->blockTypeStack = t;
}

static yaml_token_type_t zmPopTokenType (zmSubparser *subparser)
{
	zmYamlTokenTypeStack *t = subparser->blockTypeStack;
	yaml_token_type_t token_type = t->token_type;

	subparser->blockTypeStack = t->next;
	eFree (t);

	return token_type;
}

static void zmPushTag (zmSubparser *subparser, int corkIndex)
{
	zmCorkIndexStack *t;

	t = xMalloc (1, zmCorkIndexStack);
	t->corkIndex = corkIndex;

	if (subparser->reference)
	{
		t->next = subparser->refCorkStack;
		subparser->refCorkStack = t;
	}
	else
	{
		t->next = subparser->corkStack;
		subparser->corkStack = t;
	}
}

static void zmClearCorkStack (zmSubparser *subparser)
{
	while (subparser->corkStack != NULL)
	{
		zmCorkIndexStack *t = subparser->corkStack;

		if (subparser->id != NULL &&
			(ZettelMetadataFieldTable[F_IDENTIFIER].enabled ||
			 ZettelMetadataFieldTable[F_SUMMARY].enabled))
			attachParserFieldToCorkEntry (t->corkIndex,
										  ZettelMetadataFieldTable[F_IDENTIFIER].ftype,
										  subparser->id);

		if (subparser->title != NULL &&
			(ZettelMetadataFieldTable[F_TITLE].enabled ||
			 ZettelMetadataFieldTable[F_SUMMARY].enabled))
			attachParserFieldToCorkEntry (t->corkIndex,
										  ZettelMetadataFieldTable[F_TITLE].ftype,
										  subparser->title);

		subparser->corkStack = t->next;
		eFree (t);
	}
}

static void zmClearRefCorkStack (zmSubparser *subparser)
{
	while (subparser->refCorkStack != NULL)
	{
		zmCorkIndexStack *t = subparser->refCorkStack;

		if (subparser->citekey != NULL &&
			(ZettelMetadataFieldTable[F_IDENTIFIER].enabled ||
			 ZettelMetadataFieldTable[F_SUMMARY].enabled))
			attachParserFieldToCorkEntry (t->corkIndex,
										  ZettelMetadataFieldTable[F_IDENTIFIER].ftype,
										  subparser->citekey);

		if (subparser->reftitle != NULL &&
			(ZettelMetadataFieldTable[F_TITLE].enabled ||
			 ZettelMetadataFieldTable[F_SUMMARY].enabled))
			attachParserFieldToCorkEntry (t->corkIndex,
										  ZettelMetadataFieldTable[F_TITLE].ftype,
										  subparser->reftitle);

		subparser->refCorkStack = t->next;
		eFree (t);
	}
}

static void zmResetSubparser (zmSubparser *subparser)
{
	subparser->kind = K_NONE;
	subparser->role = R_NONE;
	subparser->scalarType = S_NONE;

	while (subparser->blockTypeStack != NULL)
	{
		zmPopTokenType (subparser);
	}

	subparser->reference = false;
	subparser->mapping = 0;
	subparser->sequence = 0;

	if (subparser->id != NULL)
	{
		eFree (subparser->id);
		subparser->id = NULL;
	}

	if (subparser->title != NULL)
	{
		eFree (subparser->title);
		subparser->title = NULL;
	}

	if (subparser->citekey != NULL)
	{
		eFree (subparser->citekey);
		subparser->citekey = NULL;
	}

	if (subparser->reftitle != NULL)
	{
		eFree (subparser->reftitle);
		subparser->reftitle = NULL;
	}

	zmClearCorkStack (subparser);
	zmClearRefCorkStack (subparser);
}

static void zmMakeTagEntry (zmSubparser *subparser, yaml_token_t *token)
{
	char *value = (char *)token->data.scalar.value;
	size_t length = value == NULL ? 0 : strlen (value);

	/* Prepend a prefix to the tag. */
	switch (subparser->kind)
	{
		case K_TITLE:
		{
			size_t size = zmTitlePrefix == NULL
				? 0 :
				strlen (zmTitlePrefix);

			if (size > 0)
			{
				char *b = xMalloc (length + size + 1, char);
				b = strcpy (b, zmTitlePrefix);
				value = strcat (b, value);
			}
			break;
		}
		case K_KEYWORD:
		{
			size_t size = zmKeywordPrefix == NULL
				? 0 :
				strlen (zmKeywordPrefix);

			if (size > 0)
			{
				char *b = xMalloc (length + size + 1, char);
				b = strcpy (b, zmKeywordPrefix);
				value = strcat (b, value);
			}
			break;
		}
		case K_BIBLIOGRAPHY:
		{
			size_t size = zmBibliographyPrefix == NULL
				? 0 :
				strlen (zmBibliographyPrefix);

			if (size > 0)
			{
				char *b = xMalloc (length + size + 1, char);
				b = strcpy (b, zmBibliographyPrefix);
				value = strcat (b, value);
			}
			break;
		}
		case K_CITEKEY:
		{
			/* Always prepend @ to citation keys. */
			char *b = xMalloc (length + 2, char);
			b = strcpy (b, "@");
			value = strcat (b, value);
			break;
		}
		case K_REFTITLE:
		{
			size_t size = zmReftitlePrefix == NULL
				? 0 :
				strlen (zmReftitlePrefix);

			if (size > 0)
			{
				char *b = xMalloc (length + size + 1, char);
				b = strcpy (b, zmReftitlePrefix);
				value = strcat (b, value);
			}
			break;
		}
		default:
			break;
	}

	if (ZettelMetadataKindTable[subparser->kind].enabled)
	{
		tagEntryInfo tag;

		tag.kindIndex = K_NONE;

		if (subparser->role == R_NONE)
			initTagEntry (&tag, value, subparser->kind);
		else if (subparser->role < ZettelMetadataKindTable[subparser->kind].nRoles &&
				 ZettelMetadataKindTable[subparser->kind].roles[subparser->role].enabled)
			initRefTagEntry (&tag, value, subparser->kind, subparser->role);

		if (tag.kindIndex != K_NONE)
		{
			// The line number is meaningless if the parser is running as a
			// guest parser.
			tag.lineNumber = 1;
			tag.filePosition = getInputFilePositionForLine (token->start_mark.line + 1);

			attachParserField (&tag, false,
							   ZettelMetadataFieldTable[F_TAG].ftype,
							   NULL);

			attachParserField (&tag, false,
							   ZettelMetadataFieldTable[F_SUMMARY].ftype,
							   NULL);

			zmPushTag (subparser, makeTagEntry (&tag));
		}
	}

	if (value != (char *)token->data.scalar.value)
		eFree (value);
}

static void zmNewTokenCallback (yamlSubparser *yamlSubparser,
								yaml_token_t *token)
{
	zmSubparser *subparser = (zmSubparser *)yamlSubparser;

	switch (token->type)
	{
		case YAML_STREAM_START_TOKEN:
			zmResetSubparser (subparser);
			break;
		case YAML_STREAM_END_TOKEN:
		case YAML_DOCUMENT_START_TOKEN:
		case YAML_DOCUMENT_END_TOKEN:
			zmClearCorkStack (subparser);
			zmClearRefCorkStack (subparser);
			break;
		case YAML_BLOCK_MAPPING_START_TOKEN:
		case YAML_FLOW_MAPPING_START_TOKEN:
			zmPushTokenType (subparser, token->type);
			subparser->mapping++;
			break;
		case YAML_BLOCK_SEQUENCE_START_TOKEN:
		case YAML_FLOW_SEQUENCE_START_TOKEN:
			zmPushTokenType (subparser, token->type);
			subparser->sequence++;
			break;
		case YAML_BLOCK_END_TOKEN:
			if (zmPopTokenType (subparser) == YAML_BLOCK_MAPPING_START_TOKEN)
			{
				subparser->mapping--;
				if (subparser->reference &&
					subparser->mapping < 2 &&
					subparser->sequence < 2)
					zmClearRefCorkStack (subparser);
			}
			else
				subparser->sequence--;
			break;
		case YAML_FLOW_MAPPING_END_TOKEN:
			zmPopTokenType (subparser);
			subparser->mapping--;
			if (subparser->mapping < 2 &&
				subparser->sequence < 2)
				zmClearRefCorkStack (subparser);
			break;
		case YAML_FLOW_SEQUENCE_END_TOKEN:
			zmPopTokenType (subparser);
			subparser->sequence--;
			break;
		case YAML_KEY_TOKEN:
			if (subparser->mapping == 1)
			{
				if (subparser->sequence == 0)
					subparser->scalarType = S_KEY;
				else
					subparser->scalarType = S_NONE;
				subparser->reference = false;
			}
			else if (subparser->reference &&
					 subparser->mapping == 2 &&
					 subparser->sequence < 2)
				subparser->scalarType = S_REFERENCE;
			break;
		case YAML_SCALAR_TOKEN:
			if (subparser->scalarType == S_KEY)
			{
				char *value = (char *)token->data.scalar.value;

				if (token->data.scalar.length == 2 &&
					strncmp (value, "id", 2) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_ID;
					subparser->role = R_NONE;
				}
				else if (token->data.scalar.length == 5 &&
						 strncmp (value, "title", 5) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_TITLE;
					subparser->role = R_NONE;
				}
				else if (token->data.scalar.length == 8 &&
						 strncmp (value, "keywords", 8) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_KEYWORD;
					subparser->role = R_INDEX;
				}
				else if (token->data.scalar.length == 12 &&
						 strncmp (value, "bibliography", 12) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_BIBLIOGRAPHY;
					subparser->role = R_BIBLIOGRAPHY;
				}
				else if (token->data.scalar.length == 6 &&
						 strncmp (value, "nocite", 6) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_CITEKEY;
					subparser->role = R_REFERENCES;
				}
				else if (token->data.scalar.length == 10 &&
						 strncmp (value, "references", 10) == 0)
				{
					subparser->scalarType = S_NONE;
					subparser->kind = K_NONE;
					subparser->role = R_NONE;
					subparser->reference = true;
				}
				else
				{
					subparser->kind = K_NONE;
					subparser->role = R_NONE;
				}
			}
			else if (subparser->scalarType == S_REFERENCE)
			{
				char *value = (char *)token->data.scalar.value;

				if (token->data.scalar.length == 2 &&
					strncmp (value, "id", 2) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_CITEKEY;
					subparser->role = R_NONE;
				}
				else if (token->data.scalar.length == 5 &&
						 strncmp (value, "title", 5) == 0)
				{
					subparser->scalarType = S_VALUE;
					subparser->kind = K_REFTITLE;
					subparser->role = R_NONE;
				}
				else
				{
					subparser->kind = K_NONE;
					subparser->role = R_NONE;
				}
			}
			else if (subparser->scalarType == S_VALUE &&
					 (subparser->mapping == 1 ||
					  (subparser->reference && subparser->mapping == 2)))
			{
				char *value = (char *)token->data.scalar.value;
				size_t length = token->data.scalar.length;

				if (subparser->kind == K_ID)
				{
					if (subparser->id != NULL)
						eFree (subparser->id);

					subparser->id = xMalloc (length + 1, char);
					strncpy (subparser->id, value, length);
					subparser->id[length] = '\0';
				}
				else if (subparser->kind == K_TITLE)
				{
					if (subparser->title != NULL)
						eFree (subparser->title);

					subparser->title = xMalloc (length + 1, char);
					strncpy (subparser->title, value, length);
					subparser->title[length] = '\0';
				}
				else if (subparser->reference && subparser->kind == K_CITEKEY)
				{
					if (subparser->citekey != NULL)
						eFree (subparser->citekey);

					subparser->citekey = xMalloc (length + 1, char);
					strncpy (subparser->citekey, value, length);
					subparser->citekey[length] = '\0';
				}
				else if (subparser->reference && subparser->kind == K_REFTITLE)
				{
					if (subparser->reftitle != NULL)
						eFree (subparser->reftitle);

					subparser->reftitle = xMalloc (length + 1, char);
					strncpy (subparser->reftitle, value, length);
					subparser->reftitle[length] = '\0';
				}

				zmMakeTagEntry (subparser, token);
			}
			break;
		default:
			break;
	}
}

static void findZettelMetadataTags (void)
{
	scheduleRunningBaseparser (0);
}

extern parserDefinition* ZettelMetadataParser (void)
{
	static zmSubparser zmSubparser = {
		.yaml = {
			.subparser = {
				.direction = SUBPARSER_BI_DIRECTION
			},
			.newTokenNotfify = zmNewTokenCallback
		},
		.kind = K_NONE,
		.role = R_NONE,
		.scalarType = S_NONE,
		.blockTypeStack = NULL,
		.reference = false,
		.mapping = 0,
		.sequence = 0,
		.id = NULL,
		.title = NULL,
		.citekey = NULL,
		.reftitle = NULL,
		.corkStack = NULL,
		.refCorkStack = NULL
	};

		static parserDependency ZettelMetadataDependencies [] = {
				{ DEPTYPE_SUBPARSER, "Yaml", &zmSubparser },
		};

	parserDefinition* const def = parserNew ("ZettelMetadata");

	def->enabled = false;

	def->dependencies = ZettelMetadataDependencies;
	def->dependencyCount = ARRAY_SIZE (ZettelMetadataDependencies);

	def->kindTable = ZettelMetadataKindTable;
	def->kindCount = ARRAY_SIZE (ZettelMetadataKindTable);

	def->fieldTable = ZettelMetadataFieldTable;
	def->fieldCount = ARRAY_SIZE (ZettelMetadataFieldTable);

	def->parameterHandlerTable = ZettelMetadataParameterHandlerTable;
	def->parameterHandlerCount = ARRAY_SIZE (ZettelMetadataParameterHandlerTable);

	def->parser = findZettelMetadataTags;
	def->useCork = CORK_QUEUE;

	return def;
}
