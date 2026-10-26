# lint

`shckit lint FILE...` reads each text whole and reports each line that breaks a rule below, naming the file, the line number and the rule, as [check](check.md) does. It reports each line shckit can't read too, by check's **unknown-line**. It doesn't modify files.

lint is for the text people write. SHC's own output doesn't pass it: lint refuses each pool's label, which SHCASM pads with blanks, and inline text, which keeps its programmer's columns.

## The rules

**columns**: a mnemonic, operands or a comment off the file's column for it, on an instruction or directive line. A text is read down its columns, and a field off its column breaks the read.

A file's columns are its own, so lint passes any columns a file keeps. A field's column is the one the most lines start it at, or on a tie the one the first of them uses. Lines with a label keep columns of their own, since the statement starts after the label, as in a table of `.EQU` lines. Where the field before reaches a field's column, the field starts past it and a blank instead. A comment alone on its line, and a continuation, may stand at any column.

A label starts its line, since shckit reads one nowhere else: an indented label is a line shckit can't read.

**tab**: a tab. Its width is each reader's own, so a text's columns line up at one width only.

**trailing-blank**: a blank at the end of a line. It is in the bytes and not on the page.

**line-ends**: a line end unlike most of the file's, or on a tie unlike the first line's. asmsh takes either, but a tool that takes LF alone keeps a CR LF line's CR as a byte of the line.

**export-comment**: a label an `.EXPORT` names, without a comment line right above it. Other files use an exported label, and the comment above it says what it is to them: what it takes, does and returns, or what it holds.
