# PublicTest21

PublicTest20 no longer crashes after selecting a map entry. Instead the real
game displays its own "Loading Failed" screen and remains alive.

Static analysis of the supplied Forlorn 1.9c Mach-O identifies the exact
failure test inside PlayLayer -initialLoading:

1. PlayLayer has an NSDictionary *levelDict ivar.
2. initialLoading fills levelDict from one of two paths.
3. It sends [levelDict count].
4. If count == 0 it calls AppDelegate -levelLoadingFailed.

The two real level sources are:

LOCAL:
  LevelScene constructs `Level%03d.plist` with +[NSString stringWithFormat:]
  and passes it through -initWithString:. PlayLayer later loads that plist
  through NSDictionary initWithContentsOfFile:.

REMOTE R/N/B TEST BUTTONS:
  tags 100/101/102 select GameManager levelURLR/levelURLN/levelURLB, set
  levelSelect to the literal "URL", then PlayLayer uses:
    [NSURL URLWithString:currentURL]
    [[NSDictionary alloc] initWithContentsOfURL:url]

The 1.9c binary contains these three original test URLs:
  http://dl.dropbox.com/u/7279678/testLevel.plist
  http://dl.dropbox.com/u/19031182/testLevel.plist
  http://dl.dropbox.com/u/15147073/testLevel.plist

PublicTest20 had neither working NSString stringWithFormat/initWithString
storage nor NSDictionary initWithContentsOfURL, so both kinds of level source
could collapse into an empty levelDict.

PublicTest21 adds:
- NSString stringWithFormat: with the integer/object/C-string formatting used
  by Forlorn, including `Level%03d.plist`;
- NSString initWithString:, initWithUTF8String:, initWithFormat:, copy;
- NSURL URLWithString:/initWithString:/absoluteString storage;
- NSDictionary/NSMutableDictionary initWithContentsOfURL and
  dictionaryWithContentsOfURL;
- WinHTTP GET with redirects, status logging and bounded downloads;
- XML plist parser in addition to the existing binary bplist00 parser;
- both IPA and downloaded plists use the same fake Foundation object builder;
- explicit IOS LEVEL LOAD / IOS NET PLIST diagnostics.

No remote Forlorn level files are bundled in this source. If an original
server URL is no longer reachable, the game will still show Loading Failed,
but PublicTest21 will report the real HTTP status/failure instead of silently
turning the URL into an empty dictionary.

PublicTest20 NPOT PVR, UIKit text, title scenery, input, NSInvocation and
realloc fixes are retained.
