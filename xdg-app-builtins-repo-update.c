#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "libgsystem.h"

#include "xdg-app-builtins.h"
#include "xdg-app-utils.h"


static gboolean
ostree_repo_create_appstream (OstreeRepo *repo, GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  const char *location;
  gs_free char *base = NULL;
  gs_strfreev char **argv = NULL;
  gs_free char *cmdline = NULL;
  const char *subject;
  const char *body;
  const char *branch;
  gs_free char *parent = NULL;
  gs_free char *commit_checksum = NULL;
  gs_unref_object OstreeMutableTree *mtree = NULL;
  OstreeRepoTransactionStats stats;
  gs_unref_object GFile *root = NULL;
  gs_unref_object GFile *base_file = NULL;
  gs_unref_object GFile *appstream_dir = NULL;

  location = gs_file_get_path_cached (ostree_repo_get_path (repo));

  base = g_dir_make_tmp ("xdg-appXXXXXX", error);
  if (!base)
    goto out;

  base_file = g_file_new_for_path (base);

  argv = g_new (char *, 11);
  argv[0] = g_strdup ("appstream-builder");
  argv[1] = g_strdup ("--enable-hidpi");
  argv[2] = g_strdup ("--origin=GNOME");
  argv[3] = g_strdup ("--api-version=0.8");
  argv[4] = g_strdup ("--uncompressed-icons");
  argv[5] = g_strconcat ("--ostree-repo=", location, NULL);
  argv[6] = g_strconcat ("--log-dir=", base, "/logs", NULL);
  argv[7] = g_strconcat ("--output-dir=", base, "/appstream", NULL);
  argv[8] = g_strconcat ("--icons-dir=", base, "/appstream/icons", NULL);
  argv[9] = g_strconcat ("--screenshot-dir=", base, "/screenshots", NULL);
  argv[10] = NULL;

  cmdline = g_strjoinv (" ", argv);
  g_debug ("Running %s\n", cmdline);

  if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, error))
    goto out;

  subject = "Update appstream data";
  body = "";
  branch = "appstream";

  if (!ostree_repo_resolve_rev (repo, branch, TRUE, &parent, error))
    goto out;

  if (!ostree_repo_prepare_transaction (repo, NULL, cancellable, error))
    goto out;

  appstream_dir = g_file_get_child (base_file, "appstream");

  mtree = ostree_mutable_tree_new ();
  if (!ostree_repo_write_directory_to_mtree (repo, appstream_dir, mtree, NULL, cancellable, error))
    goto out;

  if (!ostree_repo_write_mtree (repo, mtree, &root, cancellable, error))
    goto out;

  if (!ostree_repo_write_commit (repo, parent, subject, body, NULL,
                                 OSTREE_REPO_FILE (root),
                                 &commit_checksum, cancellable, error))
    goto out;

  ostree_repo_transaction_set_ref (repo, NULL, branch, commit_checksum);

  if (!ostree_repo_commit_transaction (repo, &stats, cancellable, error))
    goto out;

  g_print ("Commit: %s\n", commit_checksum);
  g_print ("Metadata Total: %u\n", stats.metadata_objects_total);
  g_print ("Metadata Written: %u\n", stats.metadata_objects_written);
  g_print ("Content Total: %u\n", stats.content_objects_total);
  g_print ("Content Written: %u\n", stats.content_objects_written);
  g_print ("Content Bytes Written: %" G_GUINT64_FORMAT "\n", stats.content_bytes_written);

  ret = TRUE;

out:
  if (base)
    gs_shutil_rm_rf (base_file, cancellable, NULL);

  return ret;
}

gboolean
xdg_app_builtin_repo_update (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  GOptionContext *context;
  gs_unref_object GFile *repofile = NULL;
  gs_unref_object OstreeRepo *repo = NULL;
  const char *location;

  context = g_option_context_new ("LOCATION - Update repository metadata");

  if (!xdg_app_option_context_parse (context, NULL, &argc, &argv, XDG_APP_BUILTIN_FLAG_NO_DIR, NULL, cancellable, error))
    goto out;

  if (argc < 2)
    {
      usage_error (context, "LOCATION must be specified", error);
      goto out;
    }

  location = argv[1];

  repofile = g_file_new_for_commandline_arg (location);
  repo = ostree_repo_new (repofile);

  if (!ostree_repo_open (repo, cancellable, error))
    goto out;

  g_debug ("Updating OSTree summary for repository");
  if (!ostree_repo_regenerate_summary (repo, NULL, cancellable, error))
    goto out;

  g_debug ("Creating appstream data for repository");
  if (!ostree_repo_create_appstream (repo, cancellable, error))
    goto out;

  ret = TRUE;

 out:
  if (context)
    g_option_context_free (context);

  return ret;
}
