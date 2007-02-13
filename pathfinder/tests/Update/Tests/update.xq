let $testdoc := doc("testdoc.xml") return (
(:
test insert attribute with the following tests:
-- non-existing qn non-existing value
-- existing qn non-existing value
-- non-existing qn existing value
-- existing qn existing value
test rename attribute with the following tests:
-- non-existing qn
-- existing qn
test replace attribute value with the following tests:
-- non-existing value
-- existing value
test delete attribute

In passing, also test that rename preserves identity.
:)
for $elem in $testdoc/document/element
	return (do insert attribute author {"Sjoerd Mullender"} into exactly-one($elem),
		do insert attribute title {"test element"} into exactly-one($elem),
		do insert attribute attr {text{$elem/@attribute}} into exactly-one($elem),
		do insert attribute copyright {text{$elem/../@copyright}} into exactly-one($elem),

		do rename exactly-one($elem/@attribute) into "Attribute",

		do rename exactly-one($elem/@dummy) into "foo",
		do replace value of exactly-one($elem/@xyzzy) with "hgulp",
		do replace value of exactly-one($elem/@dummy) with "bar")
,
do delete ($testdoc/document/@title, $testdoc/document/@copyright, $testdoc/document/@foo)
,
for $elem in $testdoc/document/element[@attribute = 50]
	return do rename exactly-one($elem) into "Element"
,
(: test changing comment value with the following test:
-- new text
:)
for $elem in $testdoc//comment()
	return do replace value of exactly-one($elem) with concat(text{$elem}, "foo ")
,
(: test changing CDATA content with the following tests:
-- new text
:)
for $elem in $testdoc/document/element/text()
	return do replace value of exactly-one($elem) with concat("element ", text{$elem})
,
(: test changing processing instructions with the following tests:
-- existing instruction, new target
-- new instruction, existing target
-- new instruction, new target - XXX can't be done since there is no PI construction
-- rename instruction and replace target
:)
(do replace value of exactly-one($testdoc//processing-instruction(pi0000)) with "foo",
 do rename exactly-one($testdoc//processing-instruction(pi1000)) into "bar",
 do replace exactly-one($testdoc//processing-instruction(pi2000)) with (: <?target instruction?> :) exactly-one($testdoc//processing-instruction(pi4000)),
 do rename exactly-one($testdoc//processing-instruction(pi5000)) into "xyzzy",
 do replace value of exactly-one($testdoc//processing-instruction(pi5000)) with "plugh")
,
(: test delete with the following tests:
-- deleting text
-- deleting element
-- deleting comment
-- deleting processing instruction
:)
do delete ($testdoc/document/text()[1],
	   $testdoc/document/element[10],
	   $testdoc/document/comment()[3],
	   $testdoc//processing-instruction("pi4000"))
,
(: test insert with the following tests:
-- test multiple insert as first
-- test multiple insert as last
-- test multiple insert before
-- test multiple insert after
:)
for $elem in $testdoc/document/element[@attribute = 10]
  return (do insert <a/> as first into exactly-one($elem) treat as element(),
	  do insert <b/> as first into exactly-one($elem) treat as element())
,
for $elem in $testdoc/document/element[@attribute = 20]
  return (do insert <a/> as last into exactly-one($elem) treat as element(),
	  do insert <b/> as last into exactly-one($elem) treat as element())
(:
for $elem in $testdoc/document/element[@attribute = 10]
  return (do insert <a/> as first into exactly-one($elem),
	  do insert <b/> as first into exactly-one($elem))
,
for $elem in $testdoc/document/element[@attribute = 20]
  return (do insert <a/> as last into exactly-one($elem),
	  do insert <b/> as last into exactly-one($elem))
:)
,
for $elem in $testdoc/document/element[@attribute = 30]
  return (do insert <a/> before exactly-one($elem),
	  do insert <b/> before exactly-one($elem))
,
for $elem in $testdoc/document/element[@attribute = 40]
  return (do insert <a/> after exactly-one($elem),
	  do insert <b/> after exactly-one($elem))
)
