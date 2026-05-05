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

#include <ogc/system.h>

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
    if (!is_rr_mystuff && !is_ctgpr_mystuff && !is_rr_music_mystuff && !is_ctgp_music_mystuff)
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

struct vec
{
    void *data;
    int value_size;
    int len;
    int cap;
};

void vec_init(struct vec *vec, int capacity, int value_size)
{
    vec->data = malloc(value_size * capacity);
    RRC_ASSERT(vec->data != NULL, "OOM while allocating vec data");
    vec->value_size = value_size;
    vec->len = 0;
    vec->cap = capacity;
}

void *vec_at(struct vec *vec, int index)
{
    RRC_ASSERT(index >= 0 && index < vec->len, "vec index out of bounds");
    return (char *)vec->data + (index * vec->value_size);
}

void vec_push(struct vec *vec, void *value)
{
    if (vec->len >= vec->cap)
    {
        int new_cap = vec->cap * 2;
        if (new_cap <= vec->cap) // Check for overflow
        {
            RRC_FATAL("vec capacity overflow");
        }

        void *new_data = realloc(vec->data, new_cap * vec->value_size);
        RRC_ASSERT(new_data != NULL, "OOM while reallocating vec data");
        vec->data = new_data;
        vec->cap = new_cap;
    }

    vec->len++;
    void *dest = vec_at(vec, vec->len - 1);
    memcpy(dest, value, vec->value_size);
}

void vec_free(struct vec *vec)
{
    free(vec->data);
}

static void _rrc_riivo_handle_file_patch(struct vec *sd_files, struct vec *replacements, u32 *mem1, const char *disc_path, const char *external_path)
{
    // TODO: handle main.dol and check if disc_path starts with / or NOT

    // TODO: normalize disc_path and external_path

    struct rrc_riivo_sd_file *sd_files_data = sd_files->data;
    int entrynum = -1;
    for (int i = 0; i < sd_files->len; i++)
    {
        if (strcmp(sd_files_data[i].path, disc_path) == 0)
        {
            entrynum = i;
            break;
        }
    }

    if (entrynum == -1)
    {
        // SD file doesn't exist yet, allocate a new entry.
        entrynum = sd_files->len;
        const char *external_path_m1 = bump_alloc_string(mem1, external_path);

        // TODO: check if file exists

        struct rrc_riivo_sd_file new_file = {
            .path = external_path_m1,
            .file_info = NULL,
        };
        vec_push(sd_files, &new_file);
    }

    const char *disc_path_m1 = bump_alloc_string(mem1, disc_path);

    struct rrc_riivo_file_replacement new_replacement = {
        .disc = disc_path_m1,
        .entrynum = entrynum,
    };

    if (replacements->len >= GLOBAL_MAX_FOLDER_FILES)
    {
        RRC_FATAL("Too many SD files for Riivolution patch loader! Found %d files, but max is %d", entrynum + 1, GLOBAL_MAX_FOLDER_FILES);
    }
    vec_push(replacements, &new_replacement);
    SYS_Report("File replacement! %s -> %s (entrynum %d)\n", disc_path, external_path, entrynum);
}

static void _rrc_riivo_combine_paths(const char *left, const char *right, char *out, int out_sz)
{
    if (left[0] == '\0')
    {
        strncpy(out, right, out_sz);
    }
    else if (right[0] == '\0')
    {
        strncpy(out, left, out_sz);
    }
    else
    {
        // TODO: remove slashes at end of left/right
        snprintf(out, out_sz, "%s/%s", left, right);
    }
}

static struct rrc_result _rrc_riivo_handle_folder_patch(struct vec *sd_files,
                                                        struct vec *replacements,
                                                        u32 *mem1,
                                                        const char *disc_path,
                                                        const char *external_path,
                                                        bool recursive)
{
    DIR *dir = opendir(external_path);
    if (!dir)
    {
        return rrc_result_create_error_errno(errno, "Failed to open folder");
    }

    int disc_path_len = strlen(disc_path);
    int external_path_len = strlen(external_path);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        int d_name_len = strlen(entry->d_name);

        // TODO: verify the sizes are correct

        int disc_path_sz = disc_path_len + d_name_len + 2 /* null terminator + slash */;
        char *cur_disc_path = malloc(disc_path_sz);
        _rrc_riivo_combine_paths(disc_path, entry->d_name, cur_disc_path, disc_path_sz);

        int external_path_sz = external_path_len + d_name_len + 2 /* null terminator + slash */;
        char *cur_external_path = malloc(external_path_sz);
        _rrc_riivo_combine_paths(external_path, entry->d_name, cur_external_path, external_path_sz);

        if (entry->d_type == DT_REG)
        {
            _rrc_riivo_handle_file_patch(sd_files, replacements, mem1, cur_disc_path, cur_external_path);
        }
        else if (recursive && entry->d_type == DT_DIR)
        {
            struct rrc_result res = _rrc_riivo_handle_folder_patch(sd_files, replacements, mem1, cur_disc_path, cur_external_path, true);
            if (res.err)
            {
                // TODO: free malloc()s.
                closedir(dir);
                return res;
            }
        }

        free(cur_disc_path);
        free(cur_external_path);
    }

    closedir(dir);
    return rrc_result_success;
}

struct rrc_result rrc_riivo_patch_loader_parse(struct rrc_settingsfile *settings, u32 *mem1, u32 *mem2, struct parse_riivo_output *out)
{
#define PARSE_REQUIRED_ATTR(node, var, attr)                                                                    \
    const char *var = mxmlElementGetAttr(node, attr);                                                           \
    if (!var)                                                                                                   \
    {                                                                                                           \
        return rrc_result_create_error_corrupted_rr_xml("missing " attr " attribute on " #node " replacement"); \
    }

    out->loader_pul_dest = NULL;
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

    struct vec sd_files;
    vec_init(&sd_files, 4, sizeof(struct rrc_riivo_sd_file));

    struct vec replacements;
    vec_init(&replacements, 4, sizeof(struct rrc_riivo_file_replacement));

    mxml_index_t *options_index = mxmlIndexNew(xml_top, "option", NULL);

    TRY(rrc_patch_loader_append_patches_for_option(xml_top, options_index, "My Stuff", settings->my_stuff, active_patches, &active_patches_count));
    // Just always enable the pack, there is no setting for this.
    TRY(rrc_patch_loader_append_patches_for_option(xml_top, options_index, "Pack", RRC_SETTINGSFILE_PACK_ENABLED_VALUE, active_patches, &active_patches_count));

    // NOTE: Separate Savegame is implemented manually rather than using the xml.

    // Iterate through <patch> elements.
    for (mxml_node_t *cur = mxmlFindElement(xml_top, xml_top, "patch", NULL, NULL, MXML_DESCEND_FIRST); cur != NULL; cur = mxmlGetNextSibling(cur))
    {
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

        mxml_index_t *file_repl_index = mxmlIndexNew(cur, "file", NULL);
        for (mxml_node_t *file = mxmlIndexEnum(file_repl_index); file != NULL; file = mxmlIndexEnum(file_repl_index))
        {
            PARSE_REQUIRED_ATTR(file, disc_path_mxml, "disc");
            PARSE_REQUIRED_ATTR(file, external_path_mxml, "external");

            _rrc_riivo_handle_file_patch(&sd_files, &replacements, mem1, disc_path_mxml, external_path_mxml);
        }
        mxmlIndexDelete(file_repl_index);

        mxml_index_t *folder_repl_index = mxmlIndexNew(cur, "folder", NULL);
        for (mxml_node_t *folder = mxmlIndexEnum(folder_repl_index); folder != NULL; folder = mxmlIndexEnum(folder_repl_index))
        {
            PARSE_REQUIRED_ATTR(folder, disc_path_mxml, "disc");
            PARSE_REQUIRED_ATTR(folder, external_path_mxml, "external");
            const char *recursive_mxml = mxmlElementGetAttr(folder, "recursive");
            bool recursive = recursive_mxml != NULL && strcmp(recursive_mxml, "true") == 0;

            _rrc_riivo_handle_folder_patch(&sd_files, &replacements, mem1, disc_path_mxml, external_path_mxml, recursive);
        }
        mxmlIndexDelete(folder_repl_index);

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
                if (strcmp(valuefile_mxml, RRC_LOADER_PUL_PATH) == 0)
                {
                    // Loader.pul specifically is handled manually elsewhere, so make an exception for this.
                    u32 loader_addr = strtoul(addr_mxml, NULL, 16);
                    out->loader_pul_dest = (void *)loader_addr;
                    continue;
                }

                char error[128];
                snprintf(error, 128, "Unsupported valuefile '%s' in memory patch", valuefile_mxml);

                return rrc_result_create_error_corrupted_rr_xml(error);
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

    // TODO: copy vec data into MEM1 here and replace the NULLs below

    // This address is a `static` in the runtime-ext dol that holds a pointer to the replacements, defined in the linker script.
    struct rrc_riivo_disc *riivo_disc = (struct rrc_riivo_disc *)(RRC_RIIVO_DISC_PTR);
    riivo_disc->sd_files = NULL;
    riivo_disc->replacements = NULL;
    riivo_disc->replacements_count = replacements.len;

    mxmlDelete(xml_top);
    fclose(xml_file);
    vec_free(&sd_files);
    vec_free(&replacements);

    while (1)
        rrc_usleep(10000000);

    return rrc_result_success;
#undef REQUIRE_ATTR
}
