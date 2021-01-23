/*
 *
 *	 Copyright (c) 2020-2021, Bernd Rellermeyer
 *
 *	 This source code is released for free distribution under the terms of the
 *	 GNU General Public License version 2 or (at your option) any later version.
 *
 */

#include "general.h"	/* must always come first */
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
		true, "identifier", "zettel identifiers"
	},
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
									vString *buffer)
{
	char *c = (char *)tag->name;

	vStringCatS (buffer, c);

	/* Find the first unexpected character for a warning message. */
	if (*c != '!') while (*c > 0x20 && *c < 0x7F) c++;
	if (*c)
		verbose ("Unexpected character %#04x in tag %s\n",
				 (unsigned char)*c, vStringValue (buffer));

	return vStringValue (buffer);
}

static const char *zRenderFieldSummary (const tagEntryInfo *const tag,
										const char *value CTAGS_ATTR_UNUSED,
										vString *buffer)
{
	static vString *tmp;
	tmp = vStringNewOrClearWithAutoRelease (tmp);
	char *line = readLineFromBypassForTag (tmp, tag, NULL);

	if (line)
	{
		/* The following is essentially the function renderCompactInputLine
		 * from field.c. */
		bool lineStarted = false;
		const char *p = line;
		int c = *p;

		/*	Write everything up to, but not including, the newline. */
		for (;	c != NEWLINE  &&  c != '\0'; c = *++p)
		{
			if (lineStarted	 || ! isspace (c))	/* Ignore leading spaces. */
			{
				lineStarted = true;

				if (isspace (c))
				{
					int next;

					/*	Consume repeating white space. */
					while (next = *(p + 1) , isspace (next)	 &&	 next != NEWLINE) ++p;
					c = SPACE;	/* Force space character for any white space. */
				}

				if (c != CRETURN  ||  *(p + 1) != NEWLINE)
					vStringPut (buffer, c);
			}
		}
	}
	else
		vStringClear (buffer);

	return vStringValue (buffer);
}

enum zettelMarkdownField {
	F_TAG,
	F_SUMMARY
};

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

static xtagDefinition ZettelMarkdownXtagTable [] = {
	{
		.name = "folgezettel",
		.description = "Include tags for Folgezettel links",
		.enabled = false
	}
};

static char *zXrefFormat = NULL;

static void zProcessXformatOption (const langType language CTAGS_ATTR_UNUSED,
								   const char *name CTAGS_ATTR_UNUSED,
								   const char *arg)
{
	zXrefFormat = PARSER_TRASH_BOX (eStrdup (arg), eFree);
}

static parameterHandlerTable ZettelMarkdownParameterHandlerTable [] = {
	{
		.name = "xformat",
		.desc = "Specify custom xref format (string)",
		.handleParameter = zProcessXformatOption
	}
};

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
								   "^(next:)\\[\\[([^] \t\n]+)\\]\\](\n-+\n)?",
								   "*\\2", "w", "{_role=identifier}{_field=encodedTagName:}{_field=summaryLine:}{_extra=folgezettel}{_advanceTo=1end}", NULL);
	addLanguageTagMultiTableRegex (language, "main",
								   "^\\[\\[([^] \t\n]+)\\]\\](\n-+\n)?",
								   "\\1", "w", "{_role=identifier}{_field=encodedTagName:}{_field=summaryLine:}", NULL);

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

	def->xtagTable = ZettelMarkdownXtagTable;
	def->xtagCount = ARRAY_SIZE (ZettelMarkdownXtagTable);

	def->parameterHandlerTable = ZettelMarkdownParameterHandlerTable;
	def->parameterHandlerCount = ARRAY_SIZE (ZettelMarkdownParameterHandlerTable);

	def->parser = findZettelMarkdownTags;
	def->initialize = initializeZettelMarkdownParser;

	return def;
}
