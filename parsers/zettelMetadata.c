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

/* The following lines are copied from fmt_p.h and fmt.c and needed for
 * zmRenderFieldSummary. */
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
	R_BIBLIOGRAPHY = 0
};

static roleDefinition ZettelMetadataKeywordRoleTable [] = {
	{
		true, "index", "index entries"
	}
};

static roleDefinition ZettelMetadataCitekeyRoleTable [] = {
	{
		true, "bibliography", "bibliography entries"
	}
};

enum zettelMetadataKind {
	K_NONE = -1,
	K_ID,
	K_TITLE,
	K_KEYWORD,
	K_CITEKEY
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
		true, 'c', "citekey", "citation keys",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMetadataCitekeyRoleTable)
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
		.name = "encodedTagName",
		.description = "encoded tag name",
		.render = zmRenderFieldTag,
		.enabled = false
	},
	{
		.name = "summaryLine",
		.description = "summary line",
		.render = zmRenderFieldSummary,
		.enabled = false
	},
	{
		.name = "identifier",
		.description = "zettel identifier",
		.enabled = false
	},
	{
		.name = "title",
		.description = "zettel title",
		.enabled = false
	}
};

static char *zmSummaryFormat = "%{ZettelMetadata.identifier}:%{ZettelMetadata.title}";
static char *zmTitlePrefix = NULL;
static char *zmKeywordPrefix = NULL;

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

static void zmSetKeywordPrefix (const langType language CTAGS_ATTR_UNUSED,
								const char *name CTAGS_ATTR_UNUSED,
								const char *arg)
{
	zmKeywordPrefix = PARSER_TRASH_BOX (eStrdup (arg), eFree);
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
		.name = "keyword-prefix",
		.desc = "Prepend reftitle tags (string)",
		.handleParameter = zmSetKeywordPrefix
	}
};

static char *zmPercentEncode (char *buffer, char *string,
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
			(force || *c < 0x21 || *c > 0x7E || *c == '%'))
		{
			*p++ = '%';
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
		case K_KEYWORD:
		{
			/* Percent-encode zettel titles and keywords. */
			size_t length = c == NULL ? 0 : 3 * strlen (c) + 1;
			char *b = xMalloc (3 * length + 1, char);
			char *p = b;

			/* Skip the prefix of a prepended string or percent-encode the
			 * first character if the beginning of the string matches another
			 * prefix. */
			size_t titlePrefixSize = zmTitlePrefix == NULL
				? 0
				: strlen (zmTitlePrefix);
			size_t keywordPrefixSize = zmKeywordPrefix == NULL
				? 0
				: strlen (zmKeywordPrefix);

			if (kind == K_TITLE)
			{
				if ((titlePrefixSize > 0 &&
					 strncmp (c, zmTitlePrefix, titlePrefixSize) == 0))
				{
					p = strncpy (p, c, titlePrefixSize) + titlePrefixSize;
					c += titlePrefixSize;
				}
				else if ((keywordPrefixSize > 0 &&
						  strncmp (c, zmKeywordPrefix, keywordPrefixSize) == 0))
					p = zmPercentEncode (p, c++, 1, true);
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
						  strncmp (c, zmTitlePrefix, titlePrefixSize) == 0))
					p = zmPercentEncode (p, c++, 1, true);
			}

			/* Percent-encode a leading exclamation mark as it conflicts with
			 * pseudo-tags when sorting. */
			if (p == b && *c == '!')
				p = zmPercentEncode (p, c++, 1, true);
			p = zmPercentEncode (p, c, b + length - p, false);

			vStringNCatS (buffer, b, p - b);
			eFree (b);
			break;
		}
		default:
			/* Do not percent-encode zettel identifiers and citation keys. */
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
	enum zettelMetadataScalar scalar;
	enum zettelMetadataKind kind;
	enum zettelMetadataRole role;
	zmYamlTokenTypeStack *blockTypeStack;
	int mapping;
	int sequence;
	char *id;
	char *title;
	zmCorkIndexStack *corkStack;
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
	t->next = subparser->corkStack;
	subparser->corkStack = t;
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

static void zmResetSubparser (zmSubparser *subparser)
{
	subparser->scalar = S_NONE;
	subparser->kind = K_NONE;
	subparser->role = R_NONE;

	while (subparser->blockTypeStack != NULL)
	{
		zmPopTokenType (subparser);
	}

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

	zmClearCorkStack (subparser);
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
		case K_CITEKEY:
		{
			/* Always prepend @ to citation keys. */
			char *b = xMalloc (length + 2, char);
			b = strcpy (b, "@");
			value = strcat (b, value);
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
			/* The line number is meaningless if the parser is running as a
			 * guest parser. */
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
			}
			else
				subparser->sequence--;
			break;
		case YAML_FLOW_MAPPING_END_TOKEN:
			zmPopTokenType (subparser);
			subparser->mapping--;
			break;
		case YAML_FLOW_SEQUENCE_END_TOKEN:
			zmPopTokenType (subparser);
			subparser->sequence--;
			break;
		case YAML_KEY_TOKEN:
			if (subparser->mapping == 1 && subparser->sequence == 0)
				subparser->scalar = S_KEY;
			else
				subparser->scalar = S_NONE;
			break;
		case YAML_SCALAR_TOKEN:
			if (subparser->scalar == S_KEY)
			{
				char *value = (char *)token->data.scalar.value;

				if (token->data.scalar.length == 2 &&
					strncmp (value, "id", 2) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_ID;
					subparser->role = R_NONE;
				}
				else if (token->data.scalar.length == 5 &&
						 strncmp (value, "title", 5) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_TITLE;
					subparser->role = R_NONE;
				}
				else if (token->data.scalar.length == 8 &&
						 strncmp (value, "keywords", 8) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_KEYWORD;
					subparser->role = R_INDEX;
				}
				else if (token->data.scalar.length == 6 &&
						 strncmp (value, "nocite", 6) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_CITEKEY;
					subparser->role = R_BIBLIOGRAPHY;
				}
				else
				{
					subparser->kind = K_NONE;
					subparser->role = R_NONE;
				}
			}
			else if (subparser->scalar == S_VALUE &&
					 subparser->mapping == 1)
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
		.scalar = S_NONE,
		.kind = K_NONE,
		.role = R_NONE,
		.blockTypeStack = NULL,
		.mapping = 0,
		.sequence = 0,
		.id = NULL,
		.title = NULL,
		.corkStack = NULL,
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
