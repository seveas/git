#!/bin/sh

test_description='diff --no-index'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	mkdir b &&
	echo 1 >a/1 &&
	echo 2 >a/2 &&
	git init repo &&
	echo 1 >repo/a &&
	mkdir -p non/git &&
	echo 1 >non/git/a &&
	echo 1 >non/git/b
'

test_expect_success 'git diff --no-index directories' '
	test_expect_code 1 git diff --no-index a b >cnt &&
	test_line_count = 14 cnt
'

test_expect_success 'git diff --no-index relative path outside repo' '
	(
		cd repo &&
		test_expect_code 0 git diff --no-index a ../non/git/a &&
		test_expect_code 0 git diff --no-index ../non/git/a ../non/git/b
	)
'

test_expect_success 'git diff --no-index with broken index' '
	(
		cd repo &&
		echo broken >.git/index &&
		git diff --no-index a ../non/git/a
	)
'

test_expect_success 'git diff outside repo with broken index' '
	(
		cd repo &&
		git diff ../non/git/a ../non/git/b
	)
'

test_expect_success 'git diff --no-index executed outside repo gives correct error message' '
	(
		GIT_CEILING_DIRECTORIES=$TRASH_DIRECTORY/non &&
		export GIT_CEILING_DIRECTORIES &&
		cd non/git &&
		test_must_fail git diff --no-index a 2>actual.err &&
		echo "usage: git diff --no-index <path> <path>" >expect.err &&
		test_cmp expect.err actual.err
	)
'

test_expect_success 'diff D F and diff F D' '
	(
		cd repo &&
		echo in-repo >a &&
		echo non-repo >../non/git/a &&
		mkdir sub &&
		echo sub-repo >sub/a &&

		test_must_fail git diff --no-index sub/a ../non/git/a >expect &&
		test_must_fail git diff --no-index sub/a ../non/git/ >actual &&
		test_cmp expect actual &&

		test_must_fail git diff --no-index a ../non/git/a >expect &&
		test_must_fail git diff --no-index a ../non/git/ >actual &&
		test_cmp expect actual &&

		test_must_fail git diff --no-index ../non/git/a a >expect &&
		test_must_fail git diff --no-index ../non/git a >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'turning a file into a directory' '
	(
		cd non/git &&
		mkdir d e e/sub &&
		echo 1 >d/sub &&
		echo 2 >e/sub/file &&
		printf "D\td/sub\nA\te/sub/file\n" >expect &&
		test_must_fail git diff --no-index --name-status d e >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'diff from repo subdir shows real paths (explicit)' '
	echo "diff --git a/../../non/git/a b/../../non/git/b" >expect &&
	test_expect_code 1 \
		git -C repo/sub \
		diff --no-index ../../non/git/a ../../non/git/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff from repo subdir shows real paths (implicit)' '
	echo "diff --git a/../../non/git/a b/../../non/git/b" >expect &&
	test_expect_code 1 \
		git -C repo/sub \
		diff ../../non/git/a ../../non/git/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff --no-index from repo subdir respects config (explicit)' '
	echo "diff --git ../../non/git/a ../../non/git/b" >expect &&
	test_config -C repo diff.noprefix true &&
	test_expect_code 1 \
		git -C repo/sub \
		diff --no-index ../../non/git/a ../../non/git/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success 'diff --no-index from repo subdir respects config (implicit)' '
	echo "diff --git ../../non/git/a ../../non/git/b" >expect &&
	test_config -C repo diff.noprefix true &&
	test_expect_code 1 \
		git -C repo/sub \
		diff ../../non/git/a ../../non/git/b >actual &&
	head -n 1 <actual >actual.head &&
	test_cmp expect actual.head
'

test_expect_success SYMLINKS 'diff --no-index does not follows symlinks' '
	echo a >1 &&
	echo b >2 &&
	ln -s 1 3 &&
	ln -s 2 4 &&
	cat >expect <<-EOF &&
		--- a/3
		+++ b/4
		@@ -1 +1 @@
		-1
		\ No newline at end of file
		+2
		\ No newline at end of file
	EOF
	test_expect_code 1 git diff --no-index 3 4 | tail -n +3 >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'diff --no-index --dereference does follows symlinks' '
	cat >expect <<-EOF &&
		--- a/3
		+++ b/4
		@@ -1 +1 @@
		-a
		+b
	EOF
	test_expect_code 1 git diff --no-index --dereference 3 4 | tail -n +3 >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'diff --no-index --no-dereference does not follow symlinks' '
	cat >expect <<-EOF &&
		--- a/3
		+++ b/4
		@@ -1 +1 @@
		-1
		\ No newline at end of file
		+2
		\ No newline at end of file
	EOF
	test_expect_code 1 git diff --no-index --no-dereference 3 4 | tail -n +3 > actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'diff --no-index --dereference with symlinks to directories' '
	cat >expect <<-EOF &&
		--- a/xx/z
		+++ b/yy/z
		@@ -1 +1 @@
		-x
		+y
	EOF
	mkdir x y &&
	echo x > x/z &&
	echo y > y/z &&
	ln -s x xx &&
	ln -s y yy &&
	test_expect_code 1 git diff --no-index --dereference xx yy | tail -n +3 > actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'diff --no-index handles symlink loops gracefully' '
	mkdir x1 &&
	mkdir x2 &&
	ln -s . x1/loop &&
	test_must_fail git diff --no-index --dereference x1 x2
'

test_expect_success SYMLINKS 'diff --no-index handles multiple symlinks to the same dir' '
	mkdir y1 &&
	mkdir y1/z1 &&
	echo z > y1/z1/zx &&
	mkdir y2 &&
	ln -sf z1 y1/z2 &&
	ln -sd z1 y1/z3 &&
	test_expect_code 1 git diff --no-index --dereference y1 y2
'

test_expect_success SYMLINKS 'diff --no-index --dereference handles broken symlinks gracefully' '
	ln -s /does/not/exist x3 &&
	mkdir x4 &&
	ln -s /does/not/exist x4/x5 &&
	test_must_fail git diff --no-index --dereference x4/x5 x3 &&
	test_must_fail git diff --no-index --dereference x3 x4/x5 &&
	test_must_fail git diff --no-index --dereference x4 x3 &&
	test_must_fail git diff --no-index --dereference x3 x4
'

test_done
