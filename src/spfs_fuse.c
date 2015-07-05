#include "spotify-fs.h"
#include "spfs_spotify.h"
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <libgen.h>
#include <glib.h>
extern time_t g_logged_in_at;


/* functional files - forward function declarations */
size_t connection_file_read(char *buf, size_t size, off_t offset);
bool connection_file_getattr(const char *path, struct stat *statbuf);
bool artists_file_getattr(const char *path, struct stat *statbuf);

struct spfs_file {
	char *abs_path;
	bool (*spfs_file_getattr)(const char *, struct stat *);
	size_t (*spfs_file_read)(char *, size_t, off_t);
};

static struct spfs_file *spfs_files[] = {
	&(struct spfs_file){ /*connection file*/
		.abs_path = "/connection",
		.spfs_file_getattr = connection_file_getattr,
		.spfs_file_read = connection_file_read
	},
	&(struct spfs_file){ /*artists*/
		.abs_path = "/artists",
		.spfs_file_getattr = artists_file_getattr,
		.spfs_file_read = NULL
	},
	/*SENTINEL*/
	NULL,
};

int spfs_getattr(const char *path, struct stat *statbuf)
{
	int i = 0;
	struct spfs_file *tmpfile = NULL;
	memset(statbuf, 0, sizeof(struct stat));
	if(strcmp(path, "/") == 0) {
		statbuf->st_mode = S_IFDIR | 0755;
		statbuf->st_nlink = 2;
		return 0;
	}
	else {
		while ((tmpfile = spfs_files[i++])) {
			if (strncmp(tmpfile->abs_path, path, strlen(tmpfile->abs_path)) == 0) {
				if (tmpfile->spfs_file_getattr(path, statbuf)) {
					return 0;
				}
			}
		}
	}
	return -ENOENT;
}

bool connection_file_getattr(const char* path, struct stat *statbuf)
{
	if (strcmp("/connection", path) != 0) {
		return false;
	}
	else {
		statbuf->st_mode = S_IFREG | 0444;
		statbuf->st_nlink = 1;
		statbuf->st_size = 64;
		statbuf->st_mtime = g_logged_in_at;
		return true;
	}
}

bool artists_file_getattr(const char *path, struct stat *statbuf)
{
	char *dirname_path_copy = strdup(path);

	bool ret = true;
	if (strcmp("/artists", path) == 0) {
		/*artists root*/
		statbuf->st_mode = S_IFDIR | 0755;
	}
	else if (strcmp(dirname(dirname_path_copy), "/artists") == 0)
	{
		/*artist query */
		/* here we set up a directory, which if changed into will generate
		 * a query for artists matching the directory name.
		 * We defer the actual query, since executing it here might impact
		 * performance due to shell tab completion, and other stat'ing shell extensions*/
		statbuf->st_mode = S_IFDIR | 0755;
		statbuf->st_mtime = time(NULL);
	}
	else {
		/*we're deeper than expected, ignore call*/
		ret = false;
	}
	return ret;
}

#define SP_SESSION (sp_session *)(fuse_get_context()->private_data)

size_t connection_file_read(char *buf, size_t size, off_t offset) {
	sp_session *session = SP_SESSION;
	char *state_str =
		g_strdup(spotify_connectionstate_str(spotify_connectionstate(session)));

	size_t len = 0;

	if ((len = strlen(state_str)) > offset) {
		if ( offset + size > len)
			size = len - offset;
		memcpy(buf, state_str + offset, size);
	}
	else
		size = 0;

	g_free(state_str);
	return size;
}

int spfs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	int i = 0;
	struct spfs_file *tmpfile;
	while((tmpfile = spfs_files[i++]) != NULL)
	{
		if (strcmp(tmpfile->abs_path, path) == 0)
			return tmpfile->spfs_file_read(buf, size, offset);
	}
	return -ENOENT;

}

int spfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
		struct fuse_file_info *fi)
{
	/*TODO: remove the hardcoding of paths, parse
	 * spfs_files instead*/
	char *dirname_path_copy = strdup(path);
	char *basename_path_copy = strdup(path);
	char *artist = NULL;
	char **artists;
	int ret = 0, i = 0;
	if (strcmp(path, "/") == 0)
	{
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		filler(buf, "connection", NULL, 0);
		filler(buf, "artists", NULL, 0);
	}
	else if (strcmp(path, "/artists") == 0) {
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
	}
	else if (strcmp(dirname(dirname_path_copy), "/artists") == 0) {
		/*artist query*/
		sp_session *session = SP_SESSION;
		if (session == NULL) {
			g_warning("No session?");
		}
		artist = g_strdup(basename(basename_path_copy));
		g_debug("querying for artist: %s", artist);
		artists = spotify_artist_search(session, artist);
		if (artists != NULL) {
			g_debug("walking result");
			while (artists[i]) {
				filler(buf, artists[i], NULL, 0);
				++i;
			}
			spotify_artist_search_destroy(artists);
		}
		else {
			g_debug("no artist search result");
			ret = -ENOENT;
		}
	}
	else
		ret = -ENOENT;

	g_debug("exit readdir for path %s ( parts: %s, %s)", path, dirname_path_copy, basename_path_copy);
	return ret;
}

struct fuse_operations spfs_operations = {
	.getattr = spfs_getattr,
	.read = spfs_read,
	.readdir = spfs_readdir,
};
