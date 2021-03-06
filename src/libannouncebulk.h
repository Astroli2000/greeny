#ifndef H_LIBANNOUNCEBULK
#define H_LIBANNOUNCEBULK

#include <stdio.h>
#include <stdbool.h>
#include <regex.h>

#include "vector.h"

int ben_error_to_anb( int bencode_error );

enum grn_operation {
	GRN_TRANSFORM_DELETE,
	GRN_TRANSFORM_SET_STRING,
	GRN_TRANSFORM_SUBSTITUTE,
	GRN_TRANSFORM_SUBSTITUTE_REGEX,
};
enum grn_operation grn_human_to_operation( char *human, int *out_err );

// represents any sort of bulk transform to occur
struct grn_transform {
	/**
	 * Null-terminated list of strings representing dictionary keys.
	 * It should point to the dictionary object that *contains* what is to be modified
	 * for all operations except for substitute, because substitute works entirely on
	 * the value itself. The key may also just be NULL right away
	 */
	char **key;
	enum grn_operation operation;
	union grn_transform_payload {
		struct grn_op_delete {
			char *key;
		} delete_;

		struct grn_op_set_string {
			char *key;
			char *val;
		} set_string;

		struct grn_op_substitute {
			char *find;
			char *replace;
		} substitute;

		struct grn_op_substitute_regex {
			// this is inline so we don't have to allocate memory for it and shit
			regex_t find;
			char *replace;
		} substitute_regex;
	} payload;
	enum grn_dynamic_transform {
		GRN_DYNAMIC_TRANSFORM_SELF = 1,
		GRN_DYNAMIC_TRANSFORM_FIRST = 2, // first element of payload, whether it's key or find
		GRN_DYNAMIC_TRANSFORM_SECOND = 4, // second element of payload, whether it's val or replace
		GRN_DYNAMIC_TRANSFORM_KEY = 8,
		GRN_DYNAMIC_TRANSFORM_KEY_ELEMENTS = 16,
	} dynamalloc;
};

struct grn_transform grn_mktransform_set_string( char *key, char *val );
struct grn_transform grn_mktransform_delete( char *key );
struct grn_transform grn_mktransform_substitute( char *find, char *replace );
// can fail because of regex compilation
struct grn_transform grn_mktransform_substitute_regex( char *find_regstr, char *replace, int *out_err );

void grn_free_transform( struct grn_transform *transform );
// frees the vector too
void grn_free_transforms_v( struct vector *vec );

struct grn_callback_arg {
	// progress bar info
	int numerator;
	int denominator;
	char *prev_path;
	char *next_path;
};

struct grn_transform_result {
	char *path;
	char error[64];
};

enum grn_ctx_state {
	GRN_CTX_NEXT,
	GRN_CTX_READ,
	GRN_CTX_TRANSFORM,
	GRN_CTX_REOPEN,
	GRN_CTX_WRITE,
	GRN_CTX_DONE,
};

struct grn_ctx {
	struct grn_transform *transforms;
	int transforms_n;
	char **files;
	int files_c; // index to the currently processing file
	int files_n;
	int file_error; // error during processing current file. Only recoverable errors.
	int errs_n;
	int state; // represents what to do next (sometimes this coincides with what is in progress)
	FILE *fh;
	char *buffer;
	size_t buffer_n;
};

struct grn_ctx *grn_ctx_alloc( int *out_err );
// assumes that individual files are dynamically allocated, as well as the whole
void grn_ctx_set_files( struct grn_ctx *ctx, char **files, int files_n );
// takes ownership of the vector, do not free it
// also assumes that all individual files are dynamically allocated
void grn_ctx_set_files_v( struct grn_ctx *ctx, struct vector *files );
void grn_ctx_set_transforms( struct grn_ctx *ctx, struct grn_transform *transforms, int transforms_n );
// takes ownership of the vector, do not free it
void grn_ctx_set_transforms_v( struct grn_ctx *ctx, struct vector *transforms );

bool grn_ctx_get_is_done( struct grn_ctx *ctx );
// the path of the currently / just processed file
char *grn_ctx_get_c_path( struct grn_ctx *ctx );
char *grn_ctx_get_next_path( struct grn_ctx *ctx );
int grn_ctx_get_c_error( struct grn_ctx *ctx );
int grn_ctx_get_files_n( struct grn_ctx *ctx );
// the number that have been completed
int grn_ctx_get_files_c( struct grn_ctx *ctx );
int grn_ctx_get_errs_n (struct grn_ctx *ctx);

/**
 * Free a context
 * @param ctx a greeny context.
 */
void grn_ctx_free( struct grn_ctx *ctx, int *out_err );

// WARNING: if any of the next few context processing functions fail, run grn_ctx_free and abort.
// all errors are fatal.

/**
 * Perform work on a context until a non-blocking operation is started.
 * Essentially, does the minimum amount of work until a non-blocking operation can begin.
 * Should be used in a high-frequency loop.
 * @param ctx a grn context
 * @return whether the context is done being processed
 */
bool grn_one_step( struct grn_ctx *ctx, int *out_err );

/**
 * Continue processing a context until the current file is done, but does not proceed to the next file.
 * You can use grn_ctx_get_c_path, etc after calling this.
 * Repeatedly calls grn_one_step internally.
 * @param ctx a grn context
 * @return whether the context is done being processed
 */
bool grn_one_file( struct grn_ctx *ctx, int *out_err );

/**
 * Process a context until completion.
 * @param ctx a grn context
 */
void grn_one_context( struct grn_ctx *ctx, int *out_err );

typedef struct {
	char *path;
	bool directory;
} grn_user_path;

typedef struct {
	char *path;
	char *buffer;
	int buffer_n;
} grn_file;

// BEGIN client-specific

enum grn_torrent_client {
	GRN_CLIENT_QBITTORRENT,
	GRN_CLIENT_DELUGE,
	GRN_CLIENT_TRANSMISSION,
	GRN_CLIENT_TRANSMISSION_DAEMON,
	GRN_CLIENT_UTORRENT,
};

/**
* @brief Adds the files for a specific torrent client to the vector
*
* @param vec The vector to add the file paths to
* @param client The enum value of the client (see x_clients.h)
*/
void grn_cat_client( struct vector *vec, int client, int *out_err );

// END client-specific

/**
 * Adds .torrent files to a vector. The paths will all be dynamically allocated.
 * @param vec the vector to add files to (see <vector.h>)
 * @param path a file or directory
 * @param extension the file extension of torrents. If NULL, uses ".torrent". Does not apply to single files; only when searching directories
 * If a filesystem error is encountered (unreadable and nonexistant files, for example) this function will set out_err to GRN_ERR_FS
 * but attempt to continue and return an accurate value anyway.
 */
void grn_cat_torrent_files( struct vector *vec, const char *path, const char *extension, int *out_err );

// BEGIN transform catting
// ONE DAY, we will have a proper vector implementation that can just append a whole buffer to itself
// but then, how do you handle freeing the elements, if you have no idea how they were allocated?
// (after all, the buffer those items were added from is unknown!)

/**
 * Adds the orpheus default transforms to the list.
 * They will be dynamically allocated.
 * @param vec the list of transforms to append to
 * @param user_announce the new announce thing. If it's null, passphrase will be kept; if it's just a passphrase, that will be handled, and if it's a full URL that is "gucci" as well. An error will be thrown if it is neither.
 */
void grn_cat_transforms_orpheus( struct vector *vec, char *user_announce, int *out_err );

// END

#endif

