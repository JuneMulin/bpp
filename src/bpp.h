/*
    Copyright (C) 2016-2017 Tomas Flouri and Ziheng Yang

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Contact: Tomas Flouri <Tomas.Flouri@h-its.org>,
    Heidelberg Institute for Theoretical Studies,
    Schloss-Wolfsbrunnenweg 35, D-69118 Heidelberg, Germany
*/

#include <assert.h>
#include <search.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#if (!defined(WINNT) && !defined(WIN32) && !defined(WIN64))
#include <sys/resource.h>
#endif

/* constants */

#define PROG_NAME "bpp"
#define PROG_VERSION "v0.0.0"

#ifdef __APPLE__
#define PROG_ARCH "macosx_x86_64"
#else
#define PROG_ARCH "linux_x86_64"
#endif

#define BPP_FAILURE  0
#define BPP_SUCCESS  1

#define LINEALLOC 2048
#define ASCII_SIZE 256

#define RTREE_SHOW_LABEL                1
#define RTREE_SHOW_BRANCH_LENGTH        2

#define TREE_TRAVERSE_POSTORDER         1
#define TREE_TRAVERSE_PREORDER          2

/* error codes */

#define ERROR_PHYLIP_SYNTAX            106
#define ERROR_PHYLIP_LONGSEQ           107
#define ERROR_PHYLIP_NONALIGNED        108
#define ERROR_PHYLIP_ILLEGALCHAR       109
#define ERROR_PHYLIP_UNPRINTABLECHAR   110

/* structures and data types */

typedef unsigned int UINT32;
typedef unsigned short WORD;
typedef unsigned char BYTE;

typedef struct snode_s
{
  char * label;
  double length;
  double theta;
  double tau;
  struct snode_s * left;
  struct snode_s * right;
  struct snode_s * parent;
  unsigned int leaves;

  void * data;

  unsigned int node_index;
} snode_t;

typedef struct stree_s
{
  unsigned int tip_count;
  unsigned int inner_count;
  unsigned int edge_count;

  snode_t ** nodes;

  snode_t * root;
} stree_t;

typedef struct gnode_s
{
  char * label;
  double length;
  double time;
  struct gnode_s * left;
  struct gnode_s * right;
  struct gnode_s * parent;
  unsigned int leaves;

  void * data;

  unsigned int node_index;

  unsigned int clv_index;
  unsigned int scaler_index;
  unsigned int pmatrix_index;
} gnode_t;

typedef struct gtree_s
{
  unsigned int tip_count;
  unsigned int inner_count;
  unsigned int edge_count;

  gnode_t ** nodes;
  gnode_t * root;
} gtree_t;

typedef struct msa_s
{
  int count;
  int length;

  char ** sequence;
  char ** label;

} msa_t;

typedef struct locus_s
{
  unsigned int tips_count;
  unsigned int clv_count;
  unsigned int pmatrix_count;
  unsigned int states;
  unsigned int sites;

  double ** clv;
  double ** pmatrix;
  unsigned int * pattern_weights;
  unsigned char ** tipstates;
} locus_t;

/* Simple structure for handling PHYLIP parsing */

typedef struct fasta
{
  FILE * fp;
  char line[LINEALLOC];
  const unsigned int * chrstatus;
  long no;
  long filesize;
  long lineno;
  long stripped_count;
  long stripped[256];
} fasta_t;


/* Simple structure for handling PHYLIP parsing */

typedef struct phylip_s
{
  FILE * fp;
  char * line;
  size_t line_size;
  size_t line_maxsize;
  char buffer[LINEALLOC];
  const unsigned int * chrstatus;
  long no;
  long filesize;
  long lineno;
  long stripped_count;
  long stripped[256];
} phylip_t;

typedef struct mapping_s
{
  char * individual;
  char * species;
  int lineno;
} mapping_t;

typedef struct list_item_s
{
  void * data;
  struct list_item_s * next;
} list_item_t;

typedef struct list_s
{
  list_item_t * head;
  list_item_t * tail;
  long count;
} list_t;

typedef struct ht_item_s
{
  unsigned long key;
  void * value;
} ht_item_t;

typedef struct hashtable_s
{
  unsigned long table_size;
  unsigned long entries_count;
  list_t ** entries;
} hashtable_t;

typedef struct pair_s
{
  char * label;
  void * data;
} pair_t;

/* macros */

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define SWAP(x,y) do { __typeof__ (x) _t = x; x = y; y = _t; } while(0)

#define legacy_rndexp(mean) (-(mean)*log(legacy_rndu()))

/* options */

extern long opt_help;
extern long opt_version;
extern long opt_quiet;
extern long opt_mcmc_steps;
extern long opt_mcmc_rate;
extern long opt_mcmc_burnin;
extern long opt_seed;
extern long opt_stree;
extern long opt_delimit;
extern long opt_cleandata;
extern double opt_tau_alpha;
extern double opt_tau_beta;
extern double opt_theta_alpha;
extern double opt_theta_beta;
extern char * opt_streefile;
extern char * opt_mapfile;
extern char * opt_outfile;
extern char * opt_msafile;
extern char * opt_mapfile;
extern char * cmdline;

/* common data */

extern __thread int bpp_errno;
extern __thread char bpp_errmsg[200];

extern const unsigned int pll_map_nt[256];
extern const unsigned int pll_map_fasta[256];

extern long mmx_present;
extern long sse_present;
extern long sse2_present;
extern long sse3_present;
extern long ssse3_present;
extern long sse41_present;
extern long sse42_present;
extern long popcnt_present;
extern long avx_present;
extern long avx2_present;

/* functions in util.c */

void fatal(const char * format, ...) __attribute__ ((noreturn));
void progress_init(const char * prompt, unsigned long size);
void progress_update(unsigned int progress);
void progress_done(void);
void * xmalloc(size_t size);
void * xcalloc(size_t nmemb, size_t size);
void * xrealloc(void *ptr, size_t size);
char * xstrchrnul(char *s, int c);
char * xstrdup(const char * s);
char * xstrndup(const char * s, size_t len);
long getusec(void);
void show_rusage(void);
FILE * xopen(const char * filename, const char * mode);

/* functions in bpp.c */

void args_init(int argc, char ** argv);

void cmd_help(void);

void getentirecommandline(int argc, char * argv[]);

void fillheader(void);

void show_header(void);

void cmd_ml(void);

/* functions in parse_stree.y */

stree_t * stree_parse_newick(const char * filename);

void stree_destroy(stree_t * tree,
                   void (*cb_destroy)(void *));

/* functions in phylip.c */

phylip_t * phylip_open(const char * filename,
                       const unsigned int * map);

int phylip_rewind(phylip_t * fd);

void phylip_close(phylip_t * fd);

msa_t * phylip_parse_interleaved(phylip_t * fd);

msa_t * phylip_parse_sequential(phylip_t * fd);

msa_t ** phylip_parse_multisequential(phylip_t * fd, int * count);

/* functions in stree.c */

void stree_show_ascii(const snode_t * root, int options);

char * stree_export_newick(const snode_t * root,
                           char * (*cb_serialize)(const snode_t *));

int stree_traverse(snode_t * root,
                   int traversal,
                   int (*cbtrav)(snode_t *),
                   snode_t ** outbuffer,
                   unsigned int * trav_size);

stree_t ** stree_tipstring_nodes(stree_t * root,
                                 char * tipstring,
                                 unsigned int * tiplist_count);

hashtable_t * species_hash(stree_t * tree);

hashtable_t * maplist_hash(list_t * maplist, hashtable_t * sht);

/* functions in arch.c */

unsigned long arch_get_memused(void);

unsigned long arch_get_memtotal(void);

/* functions in msa.c */

void msa_print(msa_t * msa);

void msa_destroy(msa_t * msa);

/* functions in parse_map.y */

list_t * yy_parse_map(const char * filename);

/* functions in mapping.c */

void maplist_print(list_t * map_list);

void map_dealloc(void * data);

hashtable_t * maplist_hash(list_t * maplist, hashtable_t * sht);

/* functions in list.c */

void list_append(list_t * list, void * data);

void list_prepend(list_t * list, void * data);

void list_clear(list_t * list, void (*cb_dealloc)(void *));

/* functions in hash.c */

void * hashtable_find(hashtable_t * ht,
                      void * x,
                      unsigned long hash,
                      int (*cb_cmp)(void *, void *));

hashtable_t * hashtable_create(unsigned long items_count);

int hashtable_strcmp(void * x, void * y);

int hashtable_ptrcmp(void * x, void * y);

unsigned long hash_djb2a(char * s);

unsigned long hash_fnv(char * s);

int hashtable_insert(hashtable_t * ht,
                     void * x,
                     unsigned long hash,
                     int (*cb_cmp)(void *, void *));

void hashtable_destroy(hashtable_t * ht, void (*cb_dealloc)(void *));

/* functions in stree.c */

void stree_init(stree_t * stree, msa_t ** msa, list_t * maplist, int msa_count);

/* functions in random.c */

double legacy_rndu(void);

/* functions in gtree.c */

void gtree_init(stree_t * stree,
                msa_t ** msalist,
                list_t * maplist,
                int msa_count);

char * gtree_export_newick(const gnode_t * root,
                           char * (*cb_serialize)(const gnode_t *));

void gtree_destroy(gtree_t * tree, void (*cb_destroy)(void *));
