/*
 * svn-fast-export.c
 * ----------
 *  Walk through each revision of a local Subversion repository and export it
 *  in a stream that git-fast-import can consume.
 *
 * Author: Chris Lee <clee@kde.org>
 * License: MIT <http://www.opensource.org/licenses/mit-license.php>
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include <apr_general.h>
#include <apr_lib.h>
#include <apr_getopt.h>

#include <svn_types.h>
#include <svn_pools.h>
#include <svn_repos.h>

#undef SVN_ERR
#define SVN_ERR(expr) SVN_INT_ERR(expr)
#define apr_sane_push(arr, contents) *(char **)apr_array_push(arr) = contents

#define TRUNK "/trunk/"

int dump_blob(svn_fs_root_t *root, char *full_path, apr_pool_t *pool)
{
    svn_filesize_t stream_length;
    svn_stream_t *stream;
    apr_size_t len;
    char buf[8];

    SVN_ERR(svn_fs_file_length(&stream_length, root, full_path, pool));
    SVN_ERR(svn_fs_file_contents(&stream, root, full_path, pool));

    fprintf(stdout, "data %li\n", stream_length);

    do {
        len = sizeof(buf);
        SVN_ERR(svn_stream_read(stream, buf, &len));
        fprintf (stdout, "%s\0", buf);
    } while (len);

    fprintf(stdout, "\n");

    return 0;
}

int export_revision(svn_revnum_t rev, svn_repos_t *repo, svn_fs_t *fs, apr_pool_t *pool)
{
    apr_array_header_t *file_changes;
    apr_hash_t *changes, *props;
    apr_hash_index_t *i;
    apr_pool_t *revpool;

    svn_fs_path_change_t *change;
    svn_fs_root_t *root_obj;
    svn_boolean_t is_dir;

    char *path, *file_change;
    unsigned int mark;
    const void *key;
    void *val;
 
    fprintf(stderr, "Exporting revision %li... ", rev);

    SVN_ERR(svn_fs_revision_root(&root_obj, fs, rev, pool));
    SVN_ERR(svn_fs_paths_changed(&changes, root_obj, pool));
    SVN_ERR(svn_fs_revision_proplist(&props, fs, rev, pool));

    revpool = svn_pool_create(pool);

    file_changes = apr_array_make(pool, apr_hash_count(changes), sizeof(char *));
    mark = 1;
    for (i = apr_hash_first(pool, changes); i; i = apr_hash_next(i)) {
        svn_pool_clear(revpool);
        apr_hash_this(i, &key, NULL, &val);
        path = (char *)key;
        change = (svn_fs_path_change_t *)val;

        SVN_ERR(svn_fs_is_dir(&is_dir, root_obj, path, revpool));

        if (is_dir || strncmp(TRUNK, path, strlen(TRUNK))) {
            continue;
        }

        if (change->change_kind == svn_fs_path_change_delete) {
            *(char **)apr_array_push(file_changes) = ((char *)svn_string_createf(pool, "D %s", path + strlen(TRUNK))->data);
        } else {
            *(char **)apr_array_push(file_changes) = (char *)svn_string_createf(pool, "M 644 :%u %s", mark, path + strlen(TRUNK))->data;
            fprintf(stdout, "blob\nmark :%u\n", mark++);
            // dump_blob(root_obj, (char *)path, revpool);
        }
    }

    if (file_changes->nelts == 0) {
        fprintf(stderr, "skipping.\n");
        svn_pool_destroy(revpool);
        return 0;
    }

    fprintf(stdout, "commit refs/heads/master\n");
    fprintf(stdout, apr_array_pstrcat(pool, file_changes, '\n'));
    fprintf(stdout, "\n\n");

    svn_pool_destroy(revpool);

    fprintf(stderr, "done!\n");

    return 0;
}

int crawl_revisions(char *repos_path)
{
    apr_pool_t *pool, *subpool;
    svn_repos_t *repos;
    svn_revnum_t youngest_rev, min_rev, max_rev, rev;
    svn_fs_t *fs;

    pool = svn_pool_create(NULL);

    SVN_ERR(svn_repos_open(&repos, repos_path, pool));

    fs = svn_repos_fs(repos);

    SVN_ERR(svn_fs_initialize(pool));
    SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));

    min_rev = 1;
    max_rev = youngest_rev;

    subpool = svn_pool_create(pool);
    for (rev = min_rev; rev <= max_rev; rev++) {
        svn_pool_clear(subpool);
        export_revision(rev, repos, fs, subpool);
    }

    svn_pool_destroy(pool);

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s REPOS_PATH\n", argv[0]);
        return -1;
    }

    if (apr_initialize() != APR_SUCCESS) {
        fprintf(stderr, "You lose at apr_initialize().\n");
        return -1;
    }

    crawl_revisions(argv[1]);

    apr_terminate();

    return 0;
}