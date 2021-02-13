/*
 *
 *	 Copyright (c) 2020-2021, Bernd Rellermeyer
 *
 *	 This source code is released for free distribution under the terms of the
 *	 GNU General Public License version 2 or (at your option) any later version.
 *
 */

#include "general.h"	/* must always come first */
#include <string.h>
#include "entry.h"
#include "options.h"
#include "read.h"
#include "param.h"
#include "parse.h"
#include "trashbox.h"

/* This private include is needed for Option.customXfmt. */
#define OPTION_WRITE
#include "options_p.h"

/* This is copied from entry_p.h and needed for zRenderFieldSummary. */
extern char *readLineFromBypassForTag (vString *const vLine,
									   const tagEntryInfo *const tag,
									   long *const pSeekValue);

static roleDefinition ZettelMarkdownWikilinkRoleTable [] = {
	{
		true, "ref", "references"
	}
};

static roleDefinition ZettelMarkdownCitekeyRoleTable [] = {
	{
		true, "bibliography", "bibliography entries"
	},
};

static kindDefinition ZettelMarkdownKindTable [] = {
	{
		true, 'w', "wikilink", "wiki links",
		.referenceOnly = true,
		ATTACH_ROLES (ZettelMarkdownWikilinkRoleTable),
	},
	{
		true, 'c', "citekey", "citation keys",
		.referenceOnly = false,
		ATTACH_ROLES (ZettelMarkdownCitekeyRoleTable),
	},
};

static const char *zRenderFieldTag (const tagEntryInfo *const tag,
									const char *value CTAGS_ATTR_UNUSED,
									vString *buffer);

static const char *zRenderFieldSummary (const tagEntryInfo *const tag,
										const char *value CTAGS_ATTR_UNUSED,
										vString *buffer);

static fieldDefinition ZettelMarkdownFieldTable [] = {
	{
		.name = "encodedTagName",
		.description = "encoded tag name",
		.render = zRenderFieldTag,
		.enabled = false
	},
	{
		.name = "summaryLine",
		.description = "summary line",
		.render = zRenderFieldSummary,
		.enabled = false
	}
};

static char *zXrefFormat = NULL;
static char *zSummaryDefFormat = "%C";
static char *zSummaryRefFormat = "%C";

static void zProcessXformatOption (const langType language CTAGS_ATTR_UNUSED,
								   const char *name CTAGS_ATTR_UNUSED,
								   const char *arg)
{
	zXrefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zSetSummaryDefFormat (const langType language CTAGS_ATTR_UNUSED,
								  const char *name,
								  const char *arg)
{
	zSummaryDefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static void zSetSummaryRefFormat (const langType language CTAGS_ATTR_UNUSED,
								  const char *name,
								  const char *arg)
{
	zSummaryRefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static parameterHandlerTable ZettelMarkdownParameterHandlerTable [] = {
	{
		.name = "xformat",
		.desc = "Specify custom xref format (string)",
		.handleParameter = zProcessXformatOption
	},
	{
		.name = "summary-definition-format",
		.desc = "Summary format string for definitions (string)",
		.handleParameter = zSetSummaryDefFormat
	},
	{
		.name = "summary-reference-format",
		.desc = "Summary format string for references (string)",
		.handleParameter = zSetSummaryRefFormat
	}
};

static char *zPercentEncode (char *buffer, char *string,
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

static const char *zRenderFieldTag (const tagEntryInfo *const tag,
									const char *value CTAGS_ATTR_UNUSED,
									vString *buffer)
{
	char *c = (char *)tag->name;

	/* Percent-encode title and keywords tags. */
	size_t length = c == NULL ? 0 : 3 * strlen (c) + 1;
	char *b = xMalloc (3 * length + 1, char);
	char *p = b;

	/* Percent-encode a leading exclamation mark as it conflicts with
	 * pseudo-tags when sorting. */
	if (*c == '!') p = zPercentEncode (p, c++, 1, true);
	p = zPercentEncode (p, c, b + length - p, false);

	vStringNCatS (buffer, b, p - b);
	eFree (b);

	return vStringValue (buffer);
}

static const char *zRenderFieldSummary (const tagEntryInfo *const tag,
										const char *value CTAGS_ATTR_UNUSED,
										vString *buffer)
{
	static fmtElement *defFmt = NULL;
	static fmtElement *refFmt = NULL;

	MIO *mio = mio_new_memory (NULL, 0, eRealloc, eFreeNoNullCheck);

	if (tag->extensionFields.roleBits)
	{
		if (refFmt == NULL) refFmt = fmtNew (zSummaryRefFormat);
		fmtPrint (refFmt, mio, tag);
	}
	else
	{
		if (defFmt == NULL) defFmt = fmtNew (zSummaryDefFormat);
		fmtPrint (defFmt, mio, tag);
	}

	size_t size = 0;
	char *s = (char *)mio_memory_get_data (mio, &size);
	vStringNCatS (buffer, s, size);

	mio_unref (mio);

	return vStringValue (buffer);
}

static void findZettelMarkdownTags (void)
{
	/* This is needed to overwrite the value of the _xformat command line
	 * option.	It can be set to
	 * "%R %-16{*.encodedTagName} %-10z %4n %-16F %{*.summaryLine}"
	 * by the ZettelMarkdown xformat parameter for GNU Global. */
	if (zXrefFormat != NULL)
	{
		if (Option.customXfmt) fmtDelete (Option.customXfmt);
		Option.customXfmt = fmtNew (zXrefFormat);
	}
}

static void initializeZettelMarkdownParser (const langType language)
{
	addLanguageRegexTable (language, "main");
	addLanguageRegexTable (language, "rest");
	addLanguageRegexTable (language, "metadata");
	addLanguageRegexTable (language, "verbatim");
	addLanguageRegexTable (language, "fencedcode");
	addLanguageRegexTable (language, "backtickcode");
	addLanguageRegexTable (language, "comment");

	/* Enter a YAML metadata block. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^---([ \t][^\n]*)?\n",
								   "", "", "{tenter=metadata}{_guest=Yaml,0start,}", NULL);

	/* Enter a verbatim block. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^	 [^\n]*\n",
								   "", "", "{tenter=verbatim}{_advanceTo=0start}", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^\t[^\n]*\n",
								   "", "", "{tenter=verbatim}{_advanceTo=0start}", NULL);

	/* Enter a fenced code block. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^[ \t]*~~~~*[^~\n]*\n",
								   "", "", "{tenter=fencedcode}", NULL);

	/* Enter a backtick code block. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^[ \t]*````*[^`\n]*\n",
								   "", "", "{tenter=backtickcode}", NULL);

	/* Skip verbatim text (and skip level-two setext headers). */
	addLanguageTagMultiTableRegex (language, "main",
								   "^``([^\n]|\n[^\n])+``(\n-+\n)?",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^`([^`\n]|\n[^`\n])+`(\n-+\n)?",
								   "", "", "", NULL);

	/* Enter an HTML comment. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^<!--",
								   "", "", "{tenter=comment}", NULL);

	/* Wiki link (skip level-two setext headers). */
	addLanguageTagMultiTableRegex (language, "main",
								   "^\\[\\[([^]]+)\\]\\](\n-+\n)?",
								   "\\1", "w", "{_role=ref}{_field=encodedTagName:}{_field=summaryLine:}", NULL);

	/* Skip numbered examples (and skip level-two setext headers). */
	addLanguageTagMultiTableRegex (language, "main",
								   "^[ \t]*\\(@[a-zA-Z0-9_-]*\\)(\n-+\n)?",
								   "", "", "", NULL);

	/* Skip email addresses (and skip level-two setext headers) */
	addLanguageTagMultiTableRegex (language, "main",
								   "^<([^@> \t\n]|\"[^@>\t\n]*\")+(\\.([^@> \t\n]|\"[^@>\t\n]\"))*@(\n-+\n)?",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^mailto:([^@> \t\n]|\"[^@>\t\n]*\")+(\\.([^@> \t\n]|\"[^@>\t\n]\"))*@(\n-+\n)?",
								   "", "", "", NULL);

	/* Pandoc citation (skip level-two setext headers). */
	addLanguageTagMultiTableRegex (language, "main",
								   "^@([a-zA-Z0-9_][a-zA-Z0-9_:.#$%&-+?<>~/]*)(\n-+\n)?",
								   "@\\1", "c", "{_role=bibliography}{_field=encodedTagName:}{_field=summaryLine:}", NULL);

	/* Skip backslash escapes, [, <, `, m, n, @ and level-two setext headers. */
	addLanguageTagMultiTableRegex (language, "main",
								   "^\\\\[^\n](\n-+\n)?",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^[[<`mn@\\\\](\n-+\n)?",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^[^[<`mn@\\\\\n]+(\n-+\n)?",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^[^\n]*",
								   "", "", "{tquit}", NULL);

	/* Skip until the beginning of the next line or quit. */
	addLanguageTagMultiTableRegex (language, "rest",
								   "^[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "rest",
								   "^[^\n]*",
								   "", "", "{tquit}", NULL);

	/* YAML metadata block. */
	addLanguageTagMultiTableRegex (language, "metadata",
								   "^---([ \t][^\n]*)?\n",
								   "", "", "{_guest=Yaml,,0end}{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "metadata",
								   "^\\.\\.\\.([ \t][^\n]*)?\n",
								   "", "", "{_guest=Yaml,,0end}{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "metadata",
								   "^[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "metadata",
								   "^[^\n]*",
								   "", "", "{tquit}", NULL);

	/* Verbatim block. */
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^ {0,3}[^ \t\n][^\n]*\n",
								   "", "", "{tleave}{_advanceTo=0start}", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^ {4}[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^ {4}[^\n]*",
								   "", "", "{quit}", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^\t[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^\t[^\n]*",
								   "", "", "{quit}", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^[ \t]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "verbatim",
								   "^[ \t]*",
								   "", "", "{quit}", NULL);

	/* Fenced code block. */
	addLanguageTagMultiTableRegex (language, "fencedcode",
								   "^[ \t]*~~~~*[ \t]*\n",
								   "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "fencedcode",
								   "^[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "fencedcode",
								   "^[^\n]*",
								   "", "", "{tquit}", NULL);

	/* Backtick code block. */
	addLanguageTagMultiTableRegex (language, "backtickcode",
								   "^[ \t]*````*[ \t]*\n",
								   "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "backtickcode",
								   "^[^\n]*\n",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "backtickcode",
								   "^[^\n]*",
								   "", "", "{tquit}", NULL);

	/* HTML comment (skip level-two setext headers). */
	addLanguageTagMultiTableRegex (language, "comment",
								   "^--[ \t]*>(\n-+\n)?",
								   "", "", "{tleave}", NULL);
	addLanguageTagMultiTableRegex (language, "comment",
								   "^[^-]+",
								   "", "", "", NULL);
	addLanguageTagMultiTableRegex (language, "comment",
								   "^-+",
								   "", "", "", NULL);
}

extern parserDefinition* ZettelMarkdownParser (void)
{
	parserDefinition* const def = parserNew ("ZettelMarkdown");

	def->enabled = false;

	def->kindTable = ZettelMarkdownKindTable;
	def->kindCount = ARRAY_SIZE (ZettelMarkdownKindTable);

	def->fieldTable = ZettelMarkdownFieldTable;
	def->fieldCount = ARRAY_SIZE (ZettelMarkdownFieldTable);

	def->parameterHandlerTable = ZettelMarkdownParameterHandlerTable;
	def->parameterHandlerCount = ARRAY_SIZE (ZettelMarkdownParameterHandlerTable);

	def->parser = findZettelMarkdownTags;
	def->initialize = initializeZettelMarkdownParser;

	return def;
}
