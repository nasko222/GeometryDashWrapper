# PublicTest21

PublicTest20 reaches the real game's `Loading Failed` branch instead of
crashing. Forlorn 1.9c's PlayLayer -initialLoading checks levelSelect. When it
equals `URL`, it loads GameManager.currentURL through NSURL and
NSDictionary initWithContentsOfURL:, then fails if levelDict.count is zero.

The visible R/N/B map entries are tags 100/101/102 and are historical
URL-backed test slots, not bundled campaign levels.

PublicTest21 implements string-backed NSURL plus dictionary URL loading. It
also provides a narrow, explicit compatibility fallback for the three built-in
historical testLevel.plist slots:

  /u/7279678/testLevel.plist  -> Level001.plist
  /u/19031182/testLevel.plist -> Level002.plist
  /u/15147073/testLevel.plist -> Level003.plist

Every substitution is logged as `substitution=explicit-local-fallback`.
Arbitrary HTTP URLs are not fabricated.

PT20 NPOT/text support and all earlier iOS fixes remain intact.
