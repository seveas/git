/*
 * Helper functions for tree diff generation
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "tree.h"
#include "dir.h"

static void show_entry(struct diff_options *opt, const char *prefix,
		       struct tree_desc *desc, struct strbuf *base);

/* Returns true if and only if "dir" is a leading directory of "path" */
static int is_dir_prefix(const char *path, const char *dir, int dirlen)
{
	return !strncmp(path, dir, dirlen) &&
		(!path[dirlen] || path[dirlen] == '/');
}

static int check_recursion_depth(struct strbuf *name,
				 const struct pathspec *ps,
				 int max_depth)
{
	int i;

	if (!ps->nr)
		return within_depth(name->buf, name->len, 1, max_depth);

	/*
	 * We look through the pathspecs in reverse-sorted order, because we
	 * want to find the longest match first (e.g., "a/b" is better for
	 * checking depth than "a/b/c").
	 */
	for (i = ps->nr - 1; i >= 0; i--) {
		const struct pathspec_item *item = ps->items+i;

		/*
		 * If the name to match is longer than the pathspec, then we
		 * are only interested if the pathspec matches and we are
		 * within the allowed depth.
		 */
		if (name->len >= item->len) {
			if (!is_dir_prefix(name->buf, item->match, item->len))
				continue;
			return within_depth(name->buf + item->len,
					    name->len - item->len,
					    1, max_depth);
		}

		/*
		 * Otherwise, our name is shorter than the pathspec. We need to
		 * check if it is a prefix of the pathspec; if so, we must
		 * always recurse in order to process further (the resulting
		 * paths we find might or might not match our pathspec, but we
		 * cannot know until we recurse).
		 */
		if (is_dir_prefix(item->match, name->buf, name->len))
			return 1;
	}
	return 0;
}

static int should_recurse(struct strbuf *name, struct diff_options *opt)
{
	if (!DIFF_OPT_TST(opt, RECURSIVE))
		return 0;
	if (!opt->max_depth_valid)
		return 1;

	/*
	 * We catch this during diff_setup_done, but let's double-check
	 * against any internal munging.
	 */
	if (opt->pathspec.has_wildcard)
		die("BUG: wildcard pathspecs are incompatible with max-depth");

	return check_recursion_depth(name, &opt->pathspec, opt->max_depth);
}

static int compare_tree_entry(struct tree_desc *t1, struct tree_desc *t2,
			      struct strbuf *base, struct diff_options *opt)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp, pathlen1, pathlen2;
	int old_baselen = base->len;

	sha1 = tree_entry_extract(t1, &path1, &mode1);
	sha2 = tree_entry_extract(t2, &path2, &mode2);

	pathlen1 = tree_entry_len(&t1->entry);
	pathlen2 = tree_entry_len(&t2->entry);
	cmp = base_name_compare(path1, pathlen1, mode1, path2, pathlen2, mode2);
	if (cmp < 0) {
		show_entry(opt, "-", t1, base);
		return -1;
	}
	if (cmp > 0) {
		show_entry(opt, "+", t2, base);
		return 1;
	}
	if (!DIFF_OPT_TST(opt, FIND_COPIES_HARDER) && !hashcmp(sha1, sha2) && mode1 == mode2)
		return 0;

	/*
	 * If the filemode has changed to/from a directory from/to a regular
	 * file, we need to consider it a remove and an add.
	 */
	if (S_ISDIR(mode1) != S_ISDIR(mode2)) {
		show_entry(opt, "-", t1, base);
		show_entry(opt, "+", t2, base);
		return 0;
	}

	strbuf_add(base, path1, pathlen1);
	if (S_ISDIR(mode1) && should_recurse(base, opt)) {
		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE)) {
			opt->change(opt, mode1, mode2,
				    sha1, sha2, 1, 1, base->buf, 0, 0);
		}
		strbuf_addch(base, '/');
		diff_tree_sha1(sha1, sha2, base->buf, opt);
	} else {
		opt->change(opt, mode1, mode2, sha1, sha2, 1, 1, base->buf, 0, 0);
	}
	strbuf_setlen(base, old_baselen);
	return 0;
}

/* A whole sub-tree went away or appeared */
static void show_tree(struct diff_options *opt, const char *prefix,
		      struct tree_desc *desc, struct strbuf *base)
{
	enum interesting match = entry_not_interesting;
	for (; desc->size; update_tree_entry(desc)) {
		if (match != all_entries_interesting) {
			match = tree_entry_interesting(&desc->entry, base, 0,
						       &opt->pathspec);
			if (match == all_entries_not_interesting)
				break;
			if (match == entry_not_interesting)
				continue;
		}
		show_entry(opt, prefix, desc, base);
	}
}

/* A file entry went away or appeared */
static void show_entry(struct diff_options *opt, const char *prefix,
		       struct tree_desc *desc, struct strbuf *base)
{
	unsigned mode;
	const char *path;
	const unsigned char *sha1 = tree_entry_extract(desc, &path, &mode);
	int pathlen = tree_entry_len(&desc->entry);
	int old_baselen = base->len;

	strbuf_add(base, path, pathlen);
	if (S_ISDIR(mode) && should_recurse(base, opt)) {
		enum object_type type;
		struct tree_desc inner;
		void *tree;
		unsigned long size;

		tree = read_sha1_file(sha1, &type, &size);
		if (!tree || type != OBJ_TREE)
			die("corrupt tree sha %s", sha1_to_hex(sha1));

		if (DIFF_OPT_TST(opt, TREE_IN_RECURSIVE))
			opt->add_remove(opt, *prefix, mode, sha1, 1, base->buf, 0);

		strbuf_addch(base, '/');

		init_tree_desc(&inner, tree, size);
		show_tree(opt, prefix, &inner, base);
		free(tree);
	} else
		opt->add_remove(opt, prefix[0], mode, sha1, 1, base->buf, 0);

	strbuf_setlen(base, old_baselen);
}

static void skip_uninteresting(struct tree_desc *t, struct strbuf *base,
			       struct diff_options *opt,
			       enum interesting *match)
{
	while (t->size) {
		*match = tree_entry_interesting(&t->entry, base, 0, &opt->pathspec);
		if (*match) {
			if (*match == all_entries_not_interesting)
				t->size = 0;
			break;
		}
		update_tree_entry(t);
	}
}

int diff_tree(struct tree_desc *t1, struct tree_desc *t2,
	      const char *base_str, struct diff_options *opt)
{
	struct strbuf base;
	int baselen = strlen(base_str);
	enum interesting t1_match = entry_not_interesting;
	enum interesting t2_match = entry_not_interesting;

	/* Enable recursion indefinitely */
	opt->pathspec.recursive = DIFF_OPT_TST(opt, RECURSIVE);
	opt->pathspec.max_depth = -1;

	strbuf_init(&base, PATH_MAX);
	strbuf_add(&base, base_str, baselen);

	for (;;) {
		if (diff_can_quit_early(opt))
			break;
		if (opt->pathspec.nr) {
			skip_uninteresting(t1, &base, opt, &t1_match);
			skip_uninteresting(t2, &base, opt, &t2_match);
		}
		if (!t1->size) {
			if (!t2->size)
				break;
			show_entry(opt, "+", t2, &base);
			update_tree_entry(t2);
			continue;
		}
		if (!t2->size) {
			show_entry(opt, "-", t1, &base);
			update_tree_entry(t1);
			continue;
		}
		switch (compare_tree_entry(t1, t2, &base, opt)) {
		case -1:
			update_tree_entry(t1);
			continue;
		case 0:
			update_tree_entry(t1);
			/* Fallthrough */
		case 1:
			update_tree_entry(t2);
			continue;
		}
		die("git diff-tree: internal error");
	}

	strbuf_release(&base);
	return 0;
}

/*
 * Does it look like the resulting diff might be due to a rename?
 *  - single entry
 *  - not a valid previous file
 */
static inline int diff_might_be_rename(void)
{
	return diff_queued_diff.nr == 1 &&
		!DIFF_FILE_VALID(diff_queued_diff.queue[0]->one);
}

static void try_to_follow_renames(struct tree_desc *t1, struct tree_desc *t2, const char *base, struct diff_options *opt)
{
	struct diff_options diff_opts;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_filepair *choice;
	const char *paths[1];
	int i;

	/* Remove the file creation entry from the diff queue, and remember it */
	choice = q->queue[0];
	q->nr = 0;

	diff_setup(&diff_opts);
	DIFF_OPT_SET(&diff_opts, RECURSIVE);
	DIFF_OPT_SET(&diff_opts, FIND_COPIES_HARDER);
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_opts.single_follow = opt->pathspec.raw[0];
	diff_opts.break_opt = opt->break_opt;
	diff_opts.rename_score = opt->rename_score;
	paths[0] = NULL;
	diff_tree_setup_paths(paths, &diff_opts);
	diff_setup_done(&diff_opts);
	diff_tree(t1, t2, base, &diff_opts);
	diffcore_std(&diff_opts);
	diff_tree_release_paths(&diff_opts);

	/* Go through the new set of filepairing, and see if we find a more interesting one */
	opt->found_follow = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];

		/*
		 * Found a source? Not only do we use that for the new
		 * diff_queued_diff, we will also use that as the path in
		 * the future!
		 */
		if ((p->status == 'R' || p->status == 'C') &&
		    !strcmp(p->two->path, opt->pathspec.raw[0])) {
			/* Switch the file-pairs around */
			q->queue[i] = choice;
			choice = p;

			/* Update the path we use from now on.. */
			diff_tree_release_paths(opt);
			opt->pathspec.raw[0] = xstrdup(p->one->path);
			diff_tree_setup_paths(opt->pathspec.raw, opt);

			/*
			 * The caller expects us to return a set of vanilla
			 * filepairs to let a later call to diffcore_std()
			 * it makes to sort the renames out (among other
			 * things), but we already have found renames
			 * ourselves; signal diffcore_std() not to muck with
			 * rename information.
			 */
			opt->found_follow = 1;
			break;
		}
	}

	/*
	 * Then, discard all the non-relevant file pairs...
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		diff_free_filepair(p);
	}

	/*
	 * .. and re-instate the one we want (which might be either the
	 * original one, or the rename/copy we found)
	 */
	q->queue[0] = choice;
	q->nr = 1;
}

int diff_tree_sha1(const unsigned char *old, const unsigned char *new, const char *base, struct diff_options *opt)
{
	void *tree1, *tree2;
	struct tree_desc t1, t2;
	unsigned long size1, size2;
	int retval;

	tree1 = read_object_with_reference(old, tree_type, &size1, NULL);
	if (!tree1)
		die("unable to read source tree (%s)", sha1_to_hex(old));
	tree2 = read_object_with_reference(new, tree_type, &size2, NULL);
	if (!tree2)
		die("unable to read destination tree (%s)", sha1_to_hex(new));
	init_tree_desc(&t1, tree1, size1);
	init_tree_desc(&t2, tree2, size2);
	retval = diff_tree(&t1, &t2, base, opt);
	if (!*base && DIFF_OPT_TST(opt, FOLLOW_RENAMES) && diff_might_be_rename()) {
		init_tree_desc(&t1, tree1, size1);
		init_tree_desc(&t2, tree2, size2);
		try_to_follow_renames(&t1, &t2, base, opt);
	}
	free(tree1);
	free(tree2);
	return retval;
}

int diff_root_tree_sha1(const unsigned char *new, const char *base, struct diff_options *opt)
{
	int retval;
	void *tree;
	unsigned long size;
	struct tree_desc empty, real;

	tree = read_object_with_reference(new, tree_type, &size, NULL);
	if (!tree)
		die("unable to read root tree (%s)", sha1_to_hex(new));
	init_tree_desc(&real, tree, size);

	init_tree_desc(&empty, "", 0);
	retval = diff_tree(&empty, &real, base, opt);
	free(tree);
	return retval;
}

void diff_tree_release_paths(struct diff_options *opt)
{
	free_pathspec(&opt->pathspec);
}

void diff_tree_setup_paths(const char **p, struct diff_options *opt)
{
	init_pathspec(&opt->pathspec, p);
}
