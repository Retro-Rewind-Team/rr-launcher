/*
    riivo_patch_loader.c - parsing and application of Riivolution XML patches

    Copyright (C) 2025  Retro Rewind Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <riivo.h>
#include "../util.h"
#include "riivo_patch_loader.h"
#include "../settingsfile.h"
#include "loader.h"
#include "binary_loader.h"
#include "../sd.h"
#include <sys/dirent.h>
#include "../time.h"

static char *bump_alloc_string(u32 *arena, const char *src)
{
    int src_len = strlen(src);
    *arena -= src_len + 1;
    char *dest = (char *)*arena;
    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
    return dest;
}

static const char **bump_alloc_string_array(u32 *arena, int count)
{
    *arena -= sizeof(char *) * count;
    return (const char **)*arena;
}

bool should_register_patch_mystuff_aware(bool is_rr_mystuff, bool is_ctgpr_mystuff, bool is_rr_music_mystuff, bool is_ctgp_music_mystuff, int my_stuff_setting)
{
    if(!is_rr_mystuff && !is_ctgpr_mystuff && !is_rr_music_mystuff && !is_ctgp_music_mystuff)
        return true;

    if (is_rr_mystuff && my_stuff_setting == RRC_SETTINGSFILE_MY_STUFF_RR)
        return true;
    if (is_ctgpr_mystuff && my_stuff_setting == RRC_SETTINGSFILE_MY_STUFF_CTGP)
        return true;
    if (is_rr_music_mystuff && my_stuff_setting == RRC_SETTINGSFILE_MY_STUFF_RR_MUSIC)
        return true;
    if (is_ctgp_music_mystuff && my_stuff_setting == RRC_SETTINGSFILE_MY_STUFF_CTGP_MUSIC)
        return true;

    return false;
}

static struct rrc_result rrc_patch_loader_append_patches_for_option(
    mxml_node_t *top,
    mxml_index_t *index,
    const char *name,
    int value,
    const char **patch_list,
    int *patch_count)
{
    if (value == 0)
    {
        // 0 = disabled, no patches to append
        return rrc_result_success;
    }

    mxmlIndexReset(index);
    for (mxml_node_t *option = mxmlIndexEnum(index); option != NULL; option = mxmlIndexEnum(index))
    {
        const char *option_name = mxmlElementGetAttr(option, "name");
        if (strcmp(option_name, name) == 0)
        {
            // Get the nth-1 (0 is the implicit disabled, handled at the top, does not exist in the XML) child (excluding whitespace nodes),
            // which is the selected option.
            mxml_node_t *selected_choice = mxmlFindElement(option, top, "choice", NULL, NULL, MXML_DESCEND_FIRST);
            for (int i = 0; selected_choice != NULL; selected_choice = mxmlGetNextSibling(selected_choice))
            {
                if (mxmlGetType(selected_choice) != MXML_ELEMENT)
                    continue;

                if (i == value - 1)
                {
                    break;
                }
                i++;
            }

            if (!selected_choice)
            {
                return rrc_result_create_error_corrupted_rr_xml("choice option has no children");
            }

            // The children of `selected_choice` are the patches. Append them.
            for (mxml_node_t *patch = mxmlFindElement(selected_choice, top, "patch", NULL, NULL, MXML_DESCEND_FIRST); patch != NULL; patch = mxmlGetNextSibling(patch))
            {
                if (mxmlGetType(patch) != MXML_ELEMENT)
                    continue;

                if (strcmp(mxmlGetElement(patch), "patch") != 0)
                    continue;

                const char *patch_name = mxmlElementGetAttr(patch, "id");
                if (!patch_name)
                {
                    return rrc_result_create_error_corrupted_rr_xml("<patch> without an id encountered");
                }

                // Append the patch name to the list.
                if (*patch_count >= MAX_ENABLED_SETTINGS)
                {
                    return rrc_result_create_error_corrupted_rr_xml("Attempted to enable more than " RRC_STRINGIFY(MAX_ENABLED_SETTINGS) " settings!");
                }
                patch_list[*patch_count] = patch_name;
                (*patch_count)++;
            }

            return rrc_result_success;
        }
    }

    return rrc_result_create_error_corrupted_rr_xml("option not found in xml");
}

// Only need to track immediate files in this folder.
const char **rrc_riivo_patch_loader_get_entries_in_replaced_folder(u32 *arena,
                                                                   const char *folder_path,
                                                                   int *out_count,
                                                                   char *main_dol_replacement_path)
{
    DIR *dir = opendir(folder_path);
    if (!dir)
    {
        rrc_dbg_printf("Failed to open folder '%s' to read contents: %d\n", folder_path, errno);
        *out_count = -1;
        return NULL;
    }

    int cap = 4;
    const char **entries = malloc(sizeof(char *) * cap);
    RRC_ASSERT(entries != NULL, "OOM while allocating space for folder entries");

    struct dirent *entry;
    int i = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || entry->d_type != DT_REG)
            continue;

        if (strcmp(entry->d_name, "main.dol") == 0)
        {
            snprintf(main_dol_replacement_path, PATH_MAX, "%s/main.dol", folder_path);
            continue; // No need to store this entry as main.dol is never needed at runtime.
        }

        if (i >= cap)
        {
            cap *= 2;
            entries = realloc(entries, sizeof(char *) * cap);
            RRC_ASSERT(entries != NULL, "OOM while allocating space for folder entries");
        }

        char *entry_path = bump_alloc_string(arena, entry->d_name);
        entries[i] = entry_path;
        i++;
    }

    if (i >= MAX_FOLDER_FILES)
    {
        RRC_FATAL("Too many files in folder '%s' for Riivolution patch loader! Found %d files, but max is %d", folder_path, i + 1, MAX_FOLDER_FILES);
    }

    const char **entries_m1 = bump_alloc_string_array(arena, i);
    memcpy(entries_m1, entries, sizeof(char *) * i);

    free(entries);
    closedir(dir);

    *out_count = i;
    return entries_m1;
}

// Attempts to replace the previously loaded main DOL of the game with a main.dol replacement
// from the SD card.
static struct rrc_result rrc_replace_main_dol(struct rrc_dol *dol, const char *main_dol_replacement_path)
{
    FILE *main_dol = fopen(main_dol_replacement_path, "r");
    if (!main_dol)
    {
        return rrc_result_create_error_errno(errno, "Failed to open main.dol replacement file");
    }

    int read;
    char *dol_ptr = (char *)dol;
    while ((read = fread(dol_ptr, 1, 1024, main_dol)) > 0)
    {
        dol_ptr += read;
    }

    if (ferror(main_dol))
    {
        return rrc_result_create_error_errno(errno, "Failed to read main.dol replacement");
    }

    fclose(main_dol);
    return rrc_result_success;
}

struct rrc_result rrc_riivo_patch_loader_parse(struct rrc_settingsfile *settings,
                                               struct rrc_dol *dol,
                                               u32 *mem1,
                                               u32 *mem2,
                                               struct parse_riivo_output *out)
{
#define PARSE_REQUIRED_ATTR(node, var, attr)                                                                    \
    const char *var = mxmlElementGetAttr(node, attr);                                                           \
    if (!var)                                                                                                   \
    {                                                                                                           \
        return rrc_result_create_error_corrupted_rr_xml("missing " attr " attribute on " #node " replacement"); \
    }

    int total_cached_folder_files = 0;

    out->loader_pul_dest = NULL;

    u32 mem1_orig = *mem1;
    // Reserve space for file/folder replacements.
    *mem1 -= sizeof(struct rrc_riivo_disc_replacement) * MAX_FILE_PATCHES;
    *mem1 -= sizeof(struct rrc_riivo_disc);
    struct rrc_riivo_disc *riivo_disc = (void *)*mem1;
    riivo_disc->count = 0;
    // Reserve space for memory patches. Note: they don't actually need to be reserved in MEM1,
    // because it's only shortly needed in patch.c and never again at runtime.
    *mem1 -= sizeof(struct rrc_riivo_memory_patch) * MAX_MEMORY_PATCHES;
    out->mem_patches = (void *)*mem1;
    out->mem_patches_count = 0;

    // Read the XML to extract all possible options for the entries.
    FILE *
        xml_file = fopen(RRC_RIIVO_XML_PATH, "r");
    if (!xml_file)
    {
        return rrc_result_create_error_errno(errno, "Failed to open " RRC_RIIVO_XML_PATH);
    }

    mxml_node_t *xml_top = mxmlLoadFile(NULL, xml_file, NULL);

    const char *active_patches[MAX_ENABLED_SETTINGS];
    int active_patches_count = 0;

    // The SD card path to the last-encountered main.dol replacement
    char main_dol_replacement_path[PATH_MAX] = {0};

    mxml_index_t *options_index = mxmlIndexNew(xml_top, "option", NULL);

    TRY(rrc_patch_loader_append_patches_for_option(xml_top, options_index, "My Stuff", settings->my_stuff, active_patches, &active_patches_count));
    // Just always enable the pack, there is no setting for this.
    TRY(rrc_patch_loader_append_patches_for_option(xml_top, options_index, "Pack", RRC_SETTINGSFILE_PACK_ENABLED_VALUE, active_patches, &active_patches_count));

    // NOTE: Separate Savegame is implemented manually rather than using the xml.

    // Iterate through <patch> elements.
    for (mxml_node_t *cur = mxmlFindElement(xml_top, xml_top, "patch", NULL, NULL, MXML_DESCEND_FIRST); cur != NULL; cur = mxmlGetNextSibling(cur))
    {
        if (riivo_disc->count >= MAX_FILE_PATCHES)
        {
            return rrc_result_create_error_corrupted_rr_xml("Attempted to enable more than " RRC_STRINGIFY(MAX_FILE_PATCHES) " file/folder replacements!");
        }

        if (mxmlGetType(cur) != MXML_ELEMENT)
            continue;

        if (strcmp(mxmlGetElement(cur), "patch") != 0)
            continue;

        // We have a <patch> element. Check if the id is an enabled setting, then process any of its contained <file> and <folder> elements.
        const char *elem_id = mxmlElementGetAttr(cur, "id");
        bool enabled = false;
        for (int i = 0; i < active_patches_count; i++)
        {
            if (strcmp(active_patches[i], elem_id) == 0)
            {
                enabled = true;
                break;
            }
        }
        if (!enabled)
            continue;

        // Handle My Stuff separately since they may not have a `disc` attribute.
        // All current My Stuff music options *do* have this attribute, so it's fine for them to use this (for now, anyway...).
        // When all is said and done, we MUST be left with only one folder replacement marked as My Stuff, if My Stuff is enabled.
        // If there are multiple, they will conflict.
        bool is_rr_mystuff = strcmp(elem_id, "RRLoad") == 0;
        bool is_ctgpr_mystuff = strcmp(elem_id, "RRCTGPLoad") == 0;

        // Skip music if the My Stuff exclusive option for it is disabled.
        bool is_rr_music = strcmp(elem_id, "RRLoadMusic") == 0;
        bool is_ctgp_music = strcmp(elem_id, "RRCTGPLoadMusic") == 0;

        mxml_index_t *file_repl_index = mxmlIndexNew(cur, "file", NULL);
        for (mxml_node_t *file = mxmlIndexEnum(file_repl_index); file != NULL; file = mxmlIndexEnum(file_repl_index))
        {
            PARSE_REQUIRED_ATTR(file, disc_path_mxml, "disc");
            PARSE_REQUIRED_ATTR(file, external_path_mxml, "external");

            // Check that the external path exists.
            if (!rrc_sd_file_exists(external_path_mxml))
            {
                // File doesn't exist; don't register it.
                continue;
            }

            char *disc_path_m1 = bump_alloc_string(mem1, disc_path_mxml);
            char *external_path_m1 = bump_alloc_string(mem1, external_path_mxml);

            struct rrc_riivo_disc_replacement *patch_dist = &riivo_disc->replacements[riivo_disc->count];
            patch_dist->disc = disc_path_m1;
            patch_dist->external = external_path_m1;
            patch_dist->type = RRC_RIIVO_FILE_REPLACEMENT;
            patch_dist->folder_contents = NULL;
            patch_dist->folder_contents_count = 0;
            riivo_disc->count++;
        }
        mxmlIndexDelete(file_repl_index);

        if (!is_rr_mystuff && !is_ctgpr_mystuff)
        {    
            mxml_index_t *folder_repl_index = mxmlIndexNew(cur, "folder", NULL);
            for (mxml_node_t *folder = mxmlIndexEnum(folder_repl_index); folder != NULL; folder = mxmlIndexEnum(folder_repl_index))
            {
                PARSE_REQUIRED_ATTR(folder, disc_path_mxml, "disc");
                PARSE_REQUIRED_ATTR(folder, external_path_mxml, "external");

                rrc_dbg_printf("Processing folder replacement: disc='%s', external='%s'\n", disc_path_mxml, external_path_mxml);

                if (!rrc_sd_folder_exists(external_path_mxml))
                {
                    // Folder doesn't exist; don't register it.
                    continue;
                }

                char *disc_path_m1 = bump_alloc_string(mem1, disc_path_mxml);
                char *external_path_m1 = bump_alloc_string(mem1, external_path_mxml);

                int out_count = 0;
                const char **folder_contents = rrc_riivo_patch_loader_get_entries_in_replaced_folder(mem1, external_path_mxml, &out_count, main_dol_replacement_path);

                total_cached_folder_files += out_count;
                if (total_cached_folder_files >= GLOBAL_MAX_FOLDER_FILES)
                {
                    RRC_FATAL("Too many total files cached across all folder replacements for Riivolution patch loader! Found %d files, but max is %d", total_cached_folder_files, GLOBAL_MAX_FOLDER_FILES);
                }

                if (out_count == 0)
                {
                    // The folder exists but is empty, which is a bit suspicious for a folder replacement. Don't register it since it won't actually replace anything.
                    rrc_dbg_printf("WARNING: folder replacement '%s' is empty!\n", external_path_mxml);
                    continue;
                }

                struct rrc_riivo_disc_replacement *patch_dist = &riivo_disc->replacements[riivo_disc->count];
                patch_dist->disc = disc_path_m1;
                patch_dist->external = external_path_m1;
                // We must set the correct type here since My Stuff should take priority.
                patch_dist->type = (is_rr_music || is_ctgp_music) ? RRC_RIIVO_MY_STUFF_REPLACEMENT : RRC_RIIVO_FOLDER_REPLACEMENT;
                patch_dist->folder_contents = folder_contents;
                patch_dist->folder_contents_count = out_count;
                riivo_disc->count++;
            }
            mxmlIndexDelete(folder_repl_index);
        }
        else if ((is_rr_mystuff && settings->my_stuff == RRC_SETTINGSFILE_MY_STUFF_RR) || (is_ctgpr_mystuff && settings->my_stuff == RRC_SETTINGSFILE_MY_STUFF_CTGP))
        {
            // Let's get the first entry in this patch just so we can get the external path,
            // instead of hardcoding it.
            mxml_index_t *folder_repl_index = mxmlIndexNew(cur, "folder", NULL);
            mxml_node_t *folder = mxmlIndexEnum(folder_repl_index);
            PARSE_REQUIRED_ATTR(folder, external_path_mxml, "external");

            // Skip the folder replacement if we're currently looking at the wrong patch.
            if ((is_rr_mystuff && settings->my_stuff != RRC_SETTINGSFILE_MY_STUFF_RR) || (is_ctgpr_mystuff && settings->my_stuff != RRC_SETTINGSFILE_MY_STUFF_CTGP))
            {
                continue;
            }

            if (!rrc_sd_folder_exists(external_path_mxml))
            {
                // Folder doesn't exist; don't register it.
                continue;
            }

            char *external_path_m1 = bump_alloc_string(mem1, external_path_mxml);

            int out_count = 0;
            const char **folder_contents = rrc_riivo_patch_loader_get_entries_in_replaced_folder(mem1, external_path_mxml, &out_count, main_dol_replacement_path);

            total_cached_folder_files += out_count;
            if (total_cached_folder_files >= GLOBAL_MAX_FOLDER_FILES)
            {
                RRC_FATAL("Too many total files cached across all folder replacements for Riivolution patch loader! Found %d files, but max is %d", total_cached_folder_files, GLOBAL_MAX_FOLDER_FILES);
            }

            if (out_count == 0)
            {
                // The folder exists but is empty, which is a bit suspicious for a folder replacement. Don't register it since it won't actually replace anything.
                rrc_dbg_printf("WARNING: folder replacement '%s' is empty!\n", external_path_mxml);
                continue;
            }

            struct rrc_riivo_disc_replacement *patch_dist = &riivo_disc->replacements[riivo_disc->count];
            patch_dist->disc = NULL;
            patch_dist->external = external_path_m1;
            patch_dist->type = RRC_RIIVO_MY_STUFF_REPLACEMENT;
            patch_dist->folder_contents = folder_contents;
            patch_dist->folder_contents_count = out_count;
            riivo_disc->count++;
        }
        else
        {
            rrc_dbg_printf("My Stuff is disabled, skipping folder replacements.\n");
        }

        mxml_index_t *memory_index = mxmlIndexNew(cur, "memory", NULL);
        for (mxml_node_t *memory = mxmlIndexEnum(memory_index); memory != NULL; memory = mxmlIndexEnum(memory_index))
        {
            PARSE_REQUIRED_ATTR(memory, addr_mxml, "offset");

            const char *valuefile_mxml = mxmlElementGetAttr(memory, "valuefile");
            // Bit of a hack, but in general we can't really handle valuefiles easily.
            // It would require loading an SD card file inside of the patch function
            // where we barely only have access to a single function.
            if (valuefile_mxml != NULL)
            {
                if (strcmp(valuefile_mxml, "/" RRC_LOADER_PUL_PATH) == 0)
                {
                    // Loader.pul specifically is handled manually elsewhere, so make an exception for this.
                    u32 loader_addr = strtoul(addr_mxml, NULL, 16);
                    out->loader_pul_dest = (void *)loader_addr;
                    continue;
                }

                return rrc_result_create_error_corrupted_rr_xml("Unhandled valuefile memory patch encountered");
            }

            PARSE_REQUIRED_ATTR(memory, value_mxml, "value");
            const char *original_mxml = mxmlElementGetAttr(memory, "original");

            struct rrc_riivo_memory_patch *patch_dist = &out->mem_patches[out->mem_patches_count];
            out->mem_patches_count++;
            patch_dist->addr = strtoul(addr_mxml, NULL, 16);
            patch_dist->value = strtoul(value_mxml, NULL, 16);
            patch_dist->original_init = false;
            if (original_mxml)
            {
                patch_dist->original = strtoul(original_mxml, NULL, 16);
                patch_dist->original_init = true;
            }
        }
        mxmlIndexDelete(memory_index);
    }

    if (main_dol_replacement_path[0] != '\0')
    {
        TRY(rrc_replace_main_dol(dol, main_dol_replacement_path));
    }

    // This address is a `static` in the runtime-ext dol that holds a pointer to the replacements, defined in the linker script.
    *((struct rrc_riivo_disc **)(RRC_RIIVO_DISC_PTR)) = riivo_disc;
    rrc_invalidate_cache((void *)*mem1, mem1_orig - *mem1);

    mxmlDelete(xml_top);
    fclose(xml_file);

    return rrc_result_success;
#undef REQUIRE_ATTR
}
