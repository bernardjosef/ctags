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
	R_BIBLIOGRAPHY = 0,
	R_IDENTIFIER = 0,
	R_FOLGEZETTEL
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

static roleDefinition ZettelMetadataNextlinkRoleTable [] = {
	{
		true, "identifier", "zettel identifiers"
	},
	{
		true, "folgezettel", "folgezettel identifiers"
	}
};

enum zettelMetadataKind {
	K_NEXTLINK_FOLGEZETTEL = -3,
	K_FOLGEZETTEL,
	K_NONE,
	K_ID,
	K_TITLE,
	K_KEYWORD,
	K_CITEKEY,
	K_NEXTLINK
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
	},
	{
		true, 'n', "nextlink", "next zettel links",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMetadataNextlinkRoleTable)
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

enum zettelMetadataXtag {
	X_NEXTLINK,
	X_FOLGEZETTEL
};

static xtagDefinition ZettelMetadataXtagTable [] = {
	{
		.name = "nextlink",
		.description = "Include tags for next links",
		.enabled = false
	},
	{
		.name = "folgezettel",
		.description = "Include Folgezettel tags for next links",
		.enabled = false
	}
};

static char *zmSummaryDefFormat = "%{ZettelMetadata.identifier}:%{ZettelMetadata.title}";
static char *zmSummaryRefFormat = "%{ZettelMetadata.identifier}:%{ZettelMetadata.title}";
static bool zmPrefix = false;

static void zmSetSummaryDefFormat (const langType language CTAGS_ATTR_UNUSED,
								   const char *name,
								   const char *arg)
{
	zmSummaryDefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetSummaryRefFormat (const langType language CTAGS_ATTR_UNUSED,
								   const char *name,
								   const char *arg)
{
	zmSummaryRefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zmSetPrefix (const langType language CTAGS_ATTR_UNUSED,
						 const char *name,
						 const char *arg)
{
	zmPrefix = paramParserBool (arg, zmPrefix, name, "parameter");
}

static parameterHandlerTable ZettelMetadataParameterHandlerTable [] = {
	{
		.name = "summary-definition-format",
		.desc = "Summary format string for definitions (string)",
		.handleParameter = zmSetSummaryDefFormat
	},
	{
		.name = "summary-reference-format",
		.desc = "Summary format string for references (string)",
		.handleParameter = zmSetSummaryRefFormat
	},
	{
		.name = "prefix-title-and-keyword-tags",
		.desc = "Prefix title tags with _ and keyword tags with # (true or [false])",
		.handleParameter = zmSetPrefix
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
		if (i < length && (force || *c < 0x21 || *c > 0x7E || *c == '%'))
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
			/* Percent-encode title and keywords tags. */
			size_t length = c == NULL ? 0 : 3 * strlen (c) + 1;
			char *b = xMalloc (3 * length + 1, char);
			char *p = b;

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
	static fmtElement *defFmt = NULL;
	static fmtElement *refFmt = NULL;

	MIO *mio = mio_new_memory (NULL, 0, eRealloc, eFreeNoNullCheck);

	if (tag->extensionFields.roleBits)
	{
		if (refFmt == NULL) refFmt = fmtNew (zmSummaryRefFormat);
		fmtPrint (refFmt, mio, tag);
	}
	else
	{
		if (defFmt == NULL) defFmt = fmtNew (zmSummaryDefFormat);
		fmtPrint (defFmt, mio, tag);
	}

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

		if (subparser->id &&
			(ZettelMetadataFieldTable[F_IDENTIFIER].enabled ||
			 ZettelMetadataFieldTable[F_SUMMARY].enabled))
			attachParserFieldToCorkEntry (t->corkIndex,
										  ZettelMetadataFieldTable[F_IDENTIFIER].ftype,
										  subparser->id);

		if (subparser->title &&
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

	while (subparser->blockTypeStack != NULL)
	{
		zmPopTokenType (subparser);
	}

	subparser->mapping = 0;
	subparser->sequence = 0;

	if (subparser->id)
	{
		eFree (subparser->id);
		subparser->id = NULL;
	}

	if (subparser->title)
	{
		eFree (subparser->title);
		subparser->title = NULL;
	}

	zmClearCorkStack (subparser);
}

static void zmMakeTagEntry (zmSubparser *subparser, char *value, int kindIndex, int roleIndex, unsigned int line)
{
	if (ZettelMetadataKindTable[kindIndex].enabled)
	{
		tagEntryInfo tag;

		tag.kindIndex = K_NONE;

		if (roleIndex == R_NONE)
			initTagEntry (&tag, value, kindIndex);
		else if (roleIndex < ZettelMetadataKindTable[kindIndex].nRoles &&
				 ZettelMetadataKindTable[kindIndex].roles[roleIndex].enabled)
			initRefTagEntry (&tag, value, kindIndex, roleIndex);

		if (tag.kindIndex != K_NONE)
		{
			/* The line number is meaningless if the parser is running as a
			 * guest parser. */
			tag.lineNumber = 1;
			tag.filePosition = getInputFilePositionForLine (line);

			attachParserField (&tag, false,
							   ZettelMetadataFieldTable[F_TAG].ftype,
							   NULL);

			attachParserField (&tag, false,
							   ZettelMetadataFieldTable[F_SUMMARY].ftype,
							   NULL);

			zmPushTag (subparser, makeTagEntry (&tag));
		}
	}
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
				}
				else if (token->data.scalar.length == 5 &&
						 strncmp (value, "title", 5) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_TITLE;
				}
				else if (token->data.scalar.length == 8 &&
						 strncmp (value, "keywords", 8) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_KEYWORD;
				}
				else if (token->data.scalar.length == 6 &&
						 strncmp (value, "nocite", 6) == 0)
				{
					subparser->scalar = S_VALUE;
					subparser->kind = K_CITEKEY;
				}
				else if (token->data.scalar.length == 4 &&
						 strncmp (value, "next", 4) == 0)
				{
					subparser->scalar = S_VALUE;

					/* Use pseudo-kinds for Folgezettel tags. */
					if (isXtagEnabled(ZettelMetadataXtagTable[X_NEXTLINK].xtype))
					{
						if (isXtagEnabled(ZettelMetadataXtagTable[X_FOLGEZETTEL].xtype))
							subparser->kind = K_NEXTLINK_FOLGEZETTEL;
						else
							subparser->kind = K_NEXTLINK;
					}
					else if (isXtagEnabled(ZettelMetadataXtagTable[X_FOLGEZETTEL].xtype))
						subparser->kind = K_FOLGEZETTEL;
				}
				else
					subparser->kind = K_NONE;
			}
			else if (subparser->scalar == S_VALUE &&
					 subparser->mapping == 1)
			{
				switch (subparser->kind)
				{
					case K_ID:
					{
						size_t length = token->data.scalar.length;

						if (subparser->id)
							eFree (subparser->id);

						subparser->id = xMalloc (length + 1, char);
						strncpy (subparser->id,
								 (char *)token->data.scalar.value,
								 length);
						subparser->id[length] = '\0';

						zmMakeTagEntry (subparser,
										(char *)token->data.scalar.value,
										K_ID, K_NONE,
										token->start_mark.line + 1);
						break;
					}
					case K_TITLE:
					{
						size_t length = token->data.scalar.length;

						if (subparser->title)
							eFree (subparser->title);

						subparser->title = xMalloc (length + 1, char);
						strncpy (subparser->title,
								 (char *)token->data.scalar.value,
								 length);
						subparser->title[length] = '\0';

						if (zmPrefix)
						{
							/* Prefix title tags. */
							size_t length = token->data.scalar.length;
							char *b = xMalloc (length + 2, char);

							b = strcpy (b, "_");

							char *value = strcat (b, ((char *)token->data.scalar.value));

							zmMakeTagEntry (subparser,
											value,
											K_TITLE, R_NONE,
											token->start_mark.line + 1);

							eFree (value);
						}
						else
							zmMakeTagEntry (subparser,
											(char *)token->data.scalar.value,
											K_TITLE, K_NONE,
											token->start_mark.line + 1);
						break;
					}
					case K_KEYWORD:
						if (zmPrefix)
						{
							/* Prefix keyword tags. */
							size_t length = token->data.scalar.length;
							char *b = xMalloc (length + 2, char);

							b = strcpy (b, "#");

							char *value = strcat (b, ((char *)token->data.scalar.value));

							zmMakeTagEntry (subparser,
											value,
											K_KEYWORD, R_INDEX,
											token->start_mark.line + 1);

							eFree (value);
						}
						else
							zmMakeTagEntry (subparser,
											(char *)token->data.scalar.value,
											K_KEYWORD, R_INDEX,
											token->start_mark.line + 1);
						break;
					case K_CITEKEY:
					{
						/* Split the scalar value into citation keys. */
						char *value = strdup ((char *)token->data.scalar.value);
						char *p = value;
						char *citekey;

						while ((citekey = strsep (&p, ", \t\n\r")) != NULL)
							if (*citekey == '@')
								zmMakeTagEntry (subparser,
												citekey,
												K_CITEKEY, R_BIBLIOGRAPHY,
												token->start_mark.line + 1);

						eFree (value);
						break;
					}
					case K_NEXTLINK_FOLGEZETTEL:
					case K_NEXTLINK:
						zmMakeTagEntry (subparser,
										(char *)token->data.scalar.value,
										K_NEXTLINK, R_IDENTIFIER,
										token->start_mark.line + 1);
						if (subparser->kind == K_NEXTLINK)
							break;
					case K_FOLGEZETTEL:
					{
						/* Prefix Folgezettel tags. */
						size_t length = token->data.scalar.length;
						char *b = xMalloc (length + 2, char);

						b = strcpy (b, "_");

						char *value = strcat (b, ((char *)token->data.scalar.value));
						zmMakeTagEntry (subparser,
										value,
										K_NEXTLINK, R_FOLGEZETTEL,
										token->start_mark.line + 1);

						eFree (value);
						break;
					}
					default:
						break;
				}
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

	def->xtagTable = ZettelMetadataXtagTable;
	def->xtagCount = ARRAY_SIZE (ZettelMetadataXtagTable);

	def->parameterHandlerTable = ZettelMetadataParameterHandlerTable;
	def->parameterHandlerCount = ARRAY_SIZE (ZettelMetadataParameterHandlerTable);

	def->parser = findZettelMetadataTags;
	def->useCork = CORK_QUEUE;

	return def;
}
