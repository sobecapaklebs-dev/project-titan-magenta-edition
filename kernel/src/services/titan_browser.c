#include "titan_browser.h"

#include "titan_browser_net.h"
#include "titan_storage_paths.h"
#include "titan_store_service.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef TITAN_BROWSER_BRIDGE_BASE
#define TITAN_BROWSER_BRIDGE_BASE "http://192.168.0.2:8080"
#endif

#define TITAN_BROWSER_HISTORY_CAPACITY 8u

typedef enum TitanBrowserOperation
{
    TITAN_BROWSER_OPERATION_NONE = 0,
    TITAN_BROWSER_OPERATION_PAGE,
    TITAN_BROWSER_OPERATION_DOWNLOAD,
    TITAN_BROWSER_OPERATION_REPOSITORY
} TitanBrowserOperation;

static TitanBrowserSnapshot g_browser;
static char g_browser_title[TITAN_BROWSER_TITLE_CAPACITY];
static char g_browser_url[TITAN_BROWSER_URL_CAPACITY];
static char g_browser_status[TITAN_BROWSER_STATUS_CAPACITY];
static char g_browser_lines[TITAN_BROWSER_LINE_COUNT][TITAN_BROWSER_LINE_CAPACITY];
static TitanBrowserLink g_browser_links[TITAN_BROWSER_LINK_CAPACITY];
static char g_browser_history[TITAN_BROWSER_HISTORY_CAPACITY][TITAN_BROWSER_URL_CAPACITY];
static unsigned int g_browser_history_count;
static unsigned int g_browser_history_index;
static char g_browser_bridge[192];
static char g_browser_pending_media[TITAN_BROWSER_URL_CAPACITY];
static TitanBrowserOperation g_browser_operation;

static const char g_browser_home_document[] =
    "<title>Titan Browser</title>"
    "<h1>Internet no Project Titan</h1>"
    "<p>Navegador leve para paginas compativeis com o PlayStation 2.</p>"
    "<p>Sites HTTPS passam pelo Titan Stream Bridge.</p>"
    "<a href=\"titan://address\">Digitar endereco</a>"
    "<a href=\"https://example.com/\">Abrir pagina HTTPS</a>";

static void TitanBrowser_CopyString(
    char *destination,
    size_t capacity,
    const char *source
)
{
    size_t index;

    if (destination == 0 || capacity == 0u)
    {
        return;
    }

    index = 0u;
    while (
        source != 0 &&
        source[index] != '\0' &&
        index + 1u < capacity
    )
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static int TitanBrowser_EqualsInsensitiveN(
    const char *left,
    const char *right,
    size_t count
)
{
    size_t index;

    if (left == 0 || right == 0)
    {
        return 0;
    }

    for (index = 0u; index < count; index++)
    {
        if (
            left[index] == '\0' ||
            right[index] == '\0' ||
            tolower((unsigned char)left[index]) !=
                tolower((unsigned char)right[index])
        )
        {
            return 0;
        }
    }
    return 1;
}

static const char *TitanBrowser_FindInsensitive(
    const char *text,
    const char *needle
)
{
    size_t needle_length;

    if (text == 0 || needle == 0)
    {
        return 0;
    }

    needle_length = strlen(needle);
    if (needle_length == 0u)
    {
        return text;
    }

    while (*text != '\0')
    {
        if (TitanBrowser_EqualsInsensitiveN(text, needle, needle_length))
        {
            return text;
        }
        text++;
    }
    return 0;
}

static int TitanBrowser_IsBlockTag(const char *tag)
{
    return
        TitanBrowser_EqualsInsensitiveN(tag, "br", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "p", 1u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/p", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "div", 3u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/div", 4u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "li", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/li", 3u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "h1", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/h1", 3u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "h2", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/h2", 3u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "h3", 2u) ||
        TitanBrowser_EqualsInsensitiveN(tag, "/h3", 3u);
}

static char TitanBrowser_DecodeEntity(const char *text, size_t *consumed)
{
    if (strncmp(text, "&amp;", 5u) == 0)
    {
        *consumed = 5u;
        return '&';
    }
    if (strncmp(text, "&lt;", 4u) == 0)
    {
        *consumed = 4u;
        return '<';
    }
    if (strncmp(text, "&gt;", 4u) == 0)
    {
        *consumed = 4u;
        return '>';
    }
    if (strncmp(text, "&quot;", 6u) == 0)
    {
        *consumed = 6u;
        return '"';
    }
    if (strncmp(text, "&#39;", 5u) == 0)
    {
        *consumed = 5u;
        return '\'';
    }
    if (strncmp(text, "&nbsp;", 6u) == 0)
    {
        *consumed = 6u;
        return ' ';
    }
    *consumed = 1u;
    return *text;
}

static void TitanBrowser_AppendVisibleCharacter(
    char character,
    unsigned int *line_index,
    unsigned int *line_length,
    int active_link
)
{
    if (character == '\r' || character == '\n' || character == '\t')
    {
        character = ' ';
    }

    if (
        character == ' ' &&
        (
            *line_length == 0u ||
            g_browser_lines[*line_index][*line_length - 1u] == ' '
        )
    )
    {
        return;
    }

    if (*line_length + 1u >= TITAN_BROWSER_LINE_CAPACITY)
    {
        if (*line_index + 1u >= TITAN_BROWSER_LINE_COUNT)
        {
            return;
        }
        g_browser_lines[*line_index][*line_length] = '\0';
        (*line_index)++;
        *line_length = 0u;
    }

    g_browser_lines[*line_index][*line_length] = character;
    (*line_length)++;
    g_browser_lines[*line_index][*line_length] = '\0';

    if (
        active_link >= 0 &&
        (unsigned int)active_link < TITAN_BROWSER_LINK_CAPACITY
    )
    {
        size_t link_length;

        link_length = strlen(g_browser_links[active_link].label);
        if (
            character == ' ' &&
            (
                link_length == 0u ||
                g_browser_links[active_link].label[link_length - 1u] == ' '
            )
        )
        {
            return;
        }
        if (link_length + 1u < sizeof(g_browser_links[active_link].label))
        {
            g_browser_links[active_link].label[link_length] = character;
            g_browser_links[active_link].label[link_length + 1u] = '\0';
        }
    }
}

static void TitanBrowser_FinishLine(
    unsigned int *line_index,
    unsigned int *line_length
)
{
    while (
        *line_length > 0u &&
        g_browser_lines[*line_index][*line_length - 1u] == ' '
    )
    {
        (*line_length)--;
    }
    g_browser_lines[*line_index][*line_length] = '\0';

    if (*line_length > 0u && *line_index + 1u < TITAN_BROWSER_LINE_COUNT)
    {
        (*line_index)++;
        *line_length = 0u;
        g_browser_lines[*line_index][0] = '\0';
    }
}

static int TitanBrowser_ParseHref(
    const char *tag,
    char *output,
    size_t capacity
)
{
    const char *href;
    const char *value;
    char quote;
    size_t length;

    href = TitanBrowser_FindInsensitive(tag, "href");
    if (href == 0)
    {
        return 0;
    }
    href += 4u;
    while (*href == ' ' || *href == '\t')
    {
        href++;
    }
    if (*href != '=')
    {
        return 0;
    }
    href++;
    while (*href == ' ' || *href == '\t')
    {
        href++;
    }

    quote = 0;
    if (*href == '\'' || *href == '"')
    {
        quote = *href;
        href++;
    }
    value = href;
    while (
        *href != '\0' &&
        (
            (quote != 0 && *href != quote) ||
            (quote == 0 && *href != ' ' && *href != '\t' && *href != '>')
        )
    )
    {
        href++;
    }

    length = (size_t)(href - value);
    if (length == 0u || length >= capacity)
    {
        return 0;
    }
    memcpy(output, value, length);
    output[length] = '\0';
    return 1;
}

static void TitanBrowser_ParseTitle(const char *html)
{
    const char *start;
    const char *end;
    size_t length;

    start = TitanBrowser_FindInsensitive(html, "<title");
    if (start == 0)
    {
        return;
    }
    start = strchr(start, '>');
    if (start == 0)
    {
        return;
    }
    start++;
    end = TitanBrowser_FindInsensitive(start, "</title>");
    if (end == 0)
    {
        return;
    }

    length = (size_t)(end - start);
    if (length >= sizeof(g_browser_title))
    {
        length = sizeof(g_browser_title) - 1u;
    }
    memcpy(g_browser_title, start, length);
    g_browser_title[length] = '\0';
}

static int TitanBrowser_ParseDocument(
    const char *url,
    const char *html,
    size_t html_size
)
{
    char tag[256];
    size_t index;
    unsigned int line_index;
    unsigned int line_length;
    int active_link;

    if (html == 0 || html_size == 0u)
    {
        return 0;
    }

    memset(g_browser_lines, 0, sizeof(g_browser_lines));
    memset(g_browser_links, 0, sizeof(g_browser_links));
    g_browser.line_count = 0u;
    g_browser.link_count = 0u;
    g_browser.selected_link = 0u;
    g_browser.scroll_line = 0u;
    TitanBrowser_CopyString(g_browser_title, sizeof(g_browser_title), "Titan Browser");
    TitanBrowser_CopyString(g_browser_url, sizeof(g_browser_url), url);

    TitanBrowser_ParseTitle(html);
    line_index = 0u;
    line_length = 0u;
    active_link = -1;

    index = 0u;
    while (index < html_size && html[index] != '\0')
    {
        if (html[index] == '<')
        {
            size_t tag_length;
            size_t tag_end;

            tag_end = index + 1u;
            while (
                tag_end < html_size &&
                html[tag_end] != '\0' &&
                html[tag_end] != '>'
            )
            {
                tag_end++;
            }
            if (tag_end >= html_size || html[tag_end] != '>')
            {
                break;
            }

            tag_length = tag_end - index - 1u;
            if (tag_length >= sizeof(tag))
            {
                tag_length = sizeof(tag) - 1u;
            }
            memcpy(tag, html + index + 1u, tag_length);
            tag[tag_length] = '\0';

            if (
                TitanBrowser_EqualsInsensitiveN(tag, "script", 6u) ||
                TitanBrowser_EqualsInsensitiveN(tag, "style", 5u)
            )
            {
                const char *closing;
                closing = TitanBrowser_FindInsensitive(
                    html + tag_end + 1u,
                    TitanBrowser_EqualsInsensitiveN(tag, "script", 6u)
                        ? "</script>"
                        : "</style>"
                );
                if (closing == 0)
                {
                    break;
                }
                index = (size_t)(closing - html) +
                    (TitanBrowser_EqualsInsensitiveN(tag, "script", 6u) ? 9u : 8u);
                continue;
            }

            if (
                TitanBrowser_EqualsInsensitiveN(tag, "a ", 2u) ||
                TitanBrowser_EqualsInsensitiveN(tag, "a\t", 2u)
            )
            {
                if (
                    g_browser.link_count < TITAN_BROWSER_LINK_CAPACITY &&
                    TitanBrowser_ParseHref(
                        tag,
                        g_browser_links[g_browser.link_count].url,
                        sizeof(g_browser_links[g_browser.link_count].url)
                    )
                )
                {
                    active_link = (int)g_browser.link_count;
                    g_browser.link_count++;
                }
            }
            else if (TitanBrowser_EqualsInsensitiveN(tag, "/a", 2u))
            {
                if (active_link >= 0)
                {
                    TitanBrowserLink *link;
                    link = &g_browser_links[active_link];
                    if (link->label[0] == '\0')
                    {
                        TitanBrowser_CopyString(
                            link->label,
                            sizeof(link->label),
                            link->url
                        );
                    }
                }
                active_link = -1;
                TitanBrowser_FinishLine(&line_index, &line_length);
            }
            else if (TitanBrowser_IsBlockTag(tag))
            {
                TitanBrowser_FinishLine(&line_index, &line_length);
            }

            index = tag_end + 1u;
            continue;
        }

        if (html[index] == '&')
        {
            size_t consumed;
            char decoded;

            decoded = TitanBrowser_DecodeEntity(html + index, &consumed);
            TitanBrowser_AppendVisibleCharacter(
                decoded,
                &line_index,
                &line_length,
                active_link
            );
            index += consumed;
        }
        else
        {
            TitanBrowser_AppendVisibleCharacter(
                html[index],
                &line_index,
                &line_length,
                active_link
            );
            index++;
        }
    }

    if (line_length > 0u)
    {
        g_browser_lines[line_index][line_length] = '\0';
        line_index++;
    }
    g_browser.line_count = line_index;
    if (g_browser.line_count == 0u)
    {
        TitanBrowser_CopyString(
            g_browser_lines[0],
            sizeof(g_browser_lines[0]),
            "A pagina nao contem texto compativel."
        );
        g_browser.line_count = 1u;
    }
    return 1;
}

static int TitanBrowser_UrlEncode(
    const char *input,
    char *output,
    size_t capacity
)
{
    static const char hexadecimal[] = "0123456789ABCDEF";
    size_t used;

    if (input == 0 || output == 0 || capacity == 0u)
    {
        return 0;
    }

    used = 0u;
    while (*input != '\0')
    {
        unsigned char character;

        character = (unsigned char)*input++;
        if (
            isalnum(character) ||
            character == '-' ||
            character == '_' ||
            character == '.' ||
            character == '~'
        )
        {
            if (used + 1u >= capacity)
            {
                return 0;
            }
            output[used++] = (char)character;
        }
        else
        {
            if (used + 3u >= capacity)
            {
                return 0;
            }
            output[used++] = '%';
            output[used++] = hexadecimal[(character >> 4u) & 0x0fu];
            output[used++] = hexadecimal[character & 0x0fu];
        }
    }
    output[used] = '\0';
    return 1;
}

static int TitanBrowser_HexValue(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    character = (char)tolower((unsigned char)character);
    if (character >= 'a' && character <= 'f')
    {
        return 10 + character - 'a';
    }
    return -1;
}

static int TitanBrowser_UrlDecode(
    const char *input,
    char *output,
    size_t capacity
)
{
    size_t used;

    if (input == 0 || output == 0 || capacity == 0u)
    {
        return 0;
    }

    used = 0u;
    while (*input != '\0')
    {
        char character;

        character = *input++;
        if (character == '%' && input[0] != '\0' && input[1] != '\0')
        {
            int high;
            int low;

            high = TitanBrowser_HexValue(input[0]);
            low = TitanBrowser_HexValue(input[1]);
            if (high < 0 || low < 0)
            {
                return 0;
            }
            character = (char)((high << 4) | low);
            input += 2u;
        }
        else if (character == '+')
        {
            character = ' ';
        }

        if (used + 1u >= capacity)
        {
            return 0;
        }
        output[used++] = character;
    }
    output[used] = '\0';
    return 1;
}

static int TitanBrowser_IsYoutubeUrl(const char *url)
{
    return
        TitanBrowser_FindInsensitive(url, "youtube.com/") != 0 ||
        TitanBrowser_FindInsensitive(url, "youtu.be/") != 0;
}

int TitanBrowser_NormalizeUrl(
    const char *input,
    char *output,
    size_t output_capacity
)
{
    const char *start;
    size_t length;

    if (input == 0 || output == 0 || output_capacity == 0u)
    {
        return 0;
    }

    start = input;
    while (*start == ' ' || *start == '\t')
    {
        start++;
    }
    length = strlen(start);
    while (length > 0u && (start[length - 1u] == ' ' || start[length - 1u] == '\t'))
    {
        length--;
    }
    if (length == 0u)
    {
        return 0;
    }

    if (
        TitanBrowser_EqualsInsensitiveN(start, "http://", 7u) ||
        TitanBrowser_EqualsInsensitiveN(start, "https://", 8u) ||
        TitanBrowser_EqualsInsensitiveN(start, "titan://", 8u)
    )
    {
        if (length >= output_capacity)
        {
            return 0;
        }
        memcpy(output, start, length);
        output[length] = '\0';
        return 1;
    }

    if (length + 7u >= output_capacity)
    {
        return 0;
    }
    memcpy(output, "http://", 7u);
    memcpy(output + 7u, start, length);
    output[length + 7u] = '\0';
    return 1;
}

int TitanBrowser_BuildTransportUrl(
    const char *normalized_url,
    char *output,
    size_t output_capacity
)
{
    char encoded[TITAN_BROWSER_URL_CAPACITY * 3u];
    const char *endpoint;
    int result;

    if (normalized_url == 0 || output == 0 || output_capacity == 0u)
    {
        return 0;
    }

    if (TitanBrowser_EqualsInsensitiveN(normalized_url, "http://", 7u))
    {
        if (strlen(normalized_url) >= output_capacity)
        {
            return 0;
        }
        TitanBrowser_CopyString(output, output_capacity, normalized_url);
        return 1;
    }

    if (!TitanBrowser_EqualsInsensitiveN(normalized_url, "https://", 8u))
    {
        return 0;
    }

    if (!TitanBrowser_UrlEncode(normalized_url, encoded, sizeof(encoded)))
    {
        return 0;
    }
    endpoint = TitanBrowser_IsYoutubeUrl(normalized_url)
        ? "/v1/youtube?url="
        : "/v1/proxy?url=";
    result = snprintf(
        output,
        output_capacity,
        "%s%s%s",
        g_browser_bridge,
        endpoint,
        encoded
    );
    return result > 0 && (size_t)result < output_capacity;
}

static void TitanBrowser_PushHistory(const char *url)
{
    unsigned int index;

    if (
        g_browser_history_count > 0u &&
        strcmp(g_browser_history[g_browser_history_index], url) == 0
    )
    {
        return;
    }

    if (g_browser_history_index + 1u < g_browser_history_count)
    {
        g_browser_history_count = g_browser_history_index + 1u;
    }

    if (g_browser_history_count < TITAN_BROWSER_HISTORY_CAPACITY)
    {
        g_browser_history_index = g_browser_history_count;
        g_browser_history_count++;
    }
    else
    {
        for (index = 1u; index < TITAN_BROWSER_HISTORY_CAPACITY; index++)
        {
            memcpy(
                g_browser_history[index - 1u],
                g_browser_history[index],
                sizeof(g_browser_history[index - 1u])
            );
        }
        g_browser_history_index = TITAN_BROWSER_HISTORY_CAPACITY - 1u;
    }

    TitanBrowser_CopyString(
        g_browser_history[g_browser_history_index],
        sizeof(g_browser_history[g_browser_history_index]),
        url
    );
}

static int TitanBrowser_StartNavigation(const char *url, int push_history)
{
    char normalized[TITAN_BROWSER_URL_CAPACITY];
    char transport[TITAN_BROWSER_URL_CAPACITY * 3u];

    if (
        !TitanBrowser_NormalizeUrl(url, normalized, sizeof(normalized)) ||
        !TitanBrowser_BuildTransportUrl(normalized, transport, sizeof(transport))
    )
    {
        TitanBrowser_CopyString(
            g_browser_status,
            sizeof(g_browser_status),
            "Endereco invalido ou protocolo nao suportado"
        );
        g_browser.state = TITAN_BROWSER_ERROR;
        return 0;
    }

    if (!TitanBrowserNet_BeginPage(transport))
    {
        const TitanBrowserNetSnapshot *network;
        network = TitanBrowserNet_GetSnapshot();
        TitanBrowser_CopyString(
            g_browser_status,
            sizeof(g_browser_status),
            network->error != 0 && network->error[0] != '\0'
                ? network->error
                : "A transferencia de pagina nao pode ser iniciada"
        );
        g_browser.state = TITAN_BROWSER_ERROR;
        return 0;
    }

    if (push_history)
    {
        TitanBrowser_PushHistory(normalized);
    }
    TitanBrowser_CopyString(g_browser_url, sizeof(g_browser_url), normalized);
    TitanBrowser_CopyString(g_browser_title, sizeof(g_browser_title), "Carregando...");
    TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Conectando");
    g_browser.state = TITAN_BROWSER_LOADING;
    g_browser_operation = TITAN_BROWSER_OPERATION_PAGE;
    return 1;
}

static int TitanBrowser_StartRepositoryUrl(const char *url)
{
    char normalized[TITAN_BROWSER_URL_CAPACITY];
    if(!TitanBrowser_NormalizeUrl(url,normalized,sizeof(normalized)))return 0;
    /* Repository files are protocol data, not Web pages. Fetch HTTPS directly
     * through Titan BrowserNet/NetworkClient; no HTML proxy/JS engine required. */
    if(!TitanBrowserNet_BeginPage(normalized))return 0;
    TitanBrowser_CopyString(g_browser_url,sizeof(g_browser_url),normalized);
    TitanBrowser_CopyString(g_browser_status,sizeof(g_browser_status),"Carregando repositorio Titan");
    g_browser.state=TITAN_BROWSER_LOADING;
    g_browser_operation=TITAN_BROWSER_OPERATION_REPOSITORY;
    return 1;
}

static const char *TitanBrowser_QueryValue(const char *url, const char *name)
{
    const char *query;
    size_t name_length;

    query = strchr(url, '?');
    if (query == 0)
    {
        return 0;
    }
    query++;
    name_length = strlen(name);
    while (*query != '\0')
    {
        if (
            strncmp(query, name, name_length) == 0 &&
            query[name_length] == '='
        )
        {
            return query + name_length + 1u;
        }
        query = strchr(query, '&');
        if (query == 0)
        {
            break;
        }
        query++;
    }
    return 0;
}

static void TitanBrowser_DeriveFilename(
    const char *url,
    char *filename,
    size_t capacity
)
{
    const char *start;
    const char *end;
    size_t length;
    size_t index;

    start = strrchr(url, '/');
    start = start != 0 ? start + 1u : url;
    end = strchr(start, '?');
    if (end == 0)
    {
        end = start + strlen(start);
    }
    length = (size_t)(end - start);
    if (length == 0u || length >= capacity)
    {
        TitanBrowser_CopyString(filename, capacity, "download.bin");
        return;
    }

    for (index = 0u; index < length; index++)
    {
        unsigned char character;

        character = (unsigned char)start[index];
        filename[index] =
            isalnum(character) || character == '.' || character == '-' || character == '_'
                ? (char)character
                : '_';
    }
    filename[length] = '\0';
}

static TitanBrowserAction TitanBrowser_StartDownloadUrl(const char *url)
{
    char normalized[TITAN_BROWSER_URL_CAPACITY];
    char transport[TITAN_BROWSER_URL_CAPACITY * 3u];
    char encoded[TITAN_BROWSER_URL_CAPACITY * 3u];
    char filename[96];
    char destination[256];
    int result;

    if (!TitanBrowser_NormalizeUrl(url, normalized, sizeof(normalized)))
    {
        return TITAN_BROWSER_ACTION_NONE;
    }

    if (TitanBrowser_EqualsInsensitiveN(normalized, "https://", 8u))
    {
        if (!TitanBrowser_UrlEncode(normalized, encoded, sizeof(encoded)))
        {
            return TITAN_BROWSER_ACTION_NONE;
        }
        result = snprintf(
            transport,
            sizeof(transport),
            "%s/v1/download?url=%s",
            g_browser_bridge,
            encoded
        );
        if (result <= 0 || (size_t)result >= sizeof(transport))
        {
            return TITAN_BROWSER_ACTION_NONE;
        }
    }
    else
    {
        TitanBrowser_CopyString(transport, sizeof(transport), normalized);
    }

    TitanBrowser_DeriveFilename(normalized, filename, sizeof(filename));
    result = snprintf(
        destination,
        sizeof(destination),
        TITAN_STORAGE_DEVICE_ROOT "/downloads/%s",
        filename
    );
    if (result <= 0 || (size_t)result >= sizeof(destination))
    {
        return TITAN_BROWSER_ACTION_NONE;
    }

    if (!TitanBrowserNet_BeginDownload(transport, destination))
    {
        return TITAN_BROWSER_ACTION_NONE;
    }

    TitanBrowser_CopyString(
        g_browser_status,
        sizeof(g_browser_status),
        "Baixando para mass:/TITAN/downloads"
    );
    g_browser.state = TITAN_BROWSER_DOWNLOADING;
    g_browser_operation = TITAN_BROWSER_OPERATION_DOWNLOAD;
    return TITAN_BROWSER_ACTION_DOWNLOAD_STARTED;
}

void TitanBrowser_Initialize(void)
{
    memset(&g_browser, 0, sizeof(g_browser));
    memset(g_browser_history, 0, sizeof(g_browser_history));
    g_browser_history_count = 0u;
    g_browser_history_index = 0u;
    g_browser_operation = TITAN_BROWSER_OPERATION_NONE;
    g_browser_pending_media[0] = '\0';
    TitanBrowser_CopyString(
        g_browser_bridge,
        sizeof(g_browser_bridge),
        TITAN_BROWSER_BRIDGE_BASE
    );
    TitanBrowserNet_Initialize();
    TitanBrowser_GoHome();
}

void TitanBrowser_Update(void)
{
    const TitanBrowserNetSnapshot *network;

    network = TitanBrowserNet_GetSnapshot();
    g_browser.bytes_received = network->bytes_received;
    g_browser.content_length = network->content_length;

    if (
        network->state == TITAN_BROWSER_NET_QUEUED ||
        network->state == TITAN_BROWSER_NET_CONNECTING
    )
    {
        TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Conectando");
    }
    else if (network->state == TITAN_BROWSER_NET_RECEIVING)
    {
        snprintf(
            g_browser_status,
            sizeof(g_browser_status),
            "%s: %llu bytes",
            g_browser_operation == TITAN_BROWSER_OPERATION_DOWNLOAD
                ? "Baixando"
                : "Recebendo",
            (unsigned long long)network->bytes_received
        );
    }
    else if (network->state == TITAN_BROWSER_NET_COMPLETE)
    {
        if (g_browser_operation == TITAN_BROWSER_OPERATION_PAGE)
        {
            if (
                TitanBrowser_ParseDocument(
                    g_browser_url,
                    (const char *)TitanBrowserNet_GetPageData(),
                    TitanBrowserNet_GetPageSize()
                )
            )
            {
                TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Pagina carregada");
                g_browser.state = TITAN_BROWSER_READY;
            }
            else
            {
                TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Documento vazio ou incompativel");
                g_browser.state = TITAN_BROWSER_ERROR;
            }
        }
        else if (g_browser_operation == TITAN_BROWSER_OPERATION_REPOSITORY)
        {
            TitanStoreResult store_result=TitanStoreService_AcceptRepository(
                g_browser_url,
                (const char *)TitanBrowserNet_GetPageData(),
                TitanBrowserNet_GetPageSize()
            );
            if(store_result==TITAN_STORE_OK)
            {
                TitanBrowser_CopyString(g_browser_status,sizeof(g_browser_status),"Repositorio Titan carregado");
                g_browser.state=TITAN_BROWSER_READY;
            }
            else
            {
                TitanBrowser_CopyString(g_browser_status,sizeof(g_browser_status),"Repositorio Titan invalido");
                g_browser.state=TITAN_BROWSER_ERROR;
            }
        }
        else if (g_browser_operation == TITAN_BROWSER_OPERATION_DOWNLOAD)
        {
            TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Download concluido em mass:/TITAN/downloads");
            g_browser.state = TITAN_BROWSER_READY;
        }
        g_browser_operation = TITAN_BROWSER_OPERATION_NONE;
        TitanBrowserNet_ReleaseResult();
    }
    else if (
        network->state == TITAN_BROWSER_NET_FAILED ||
        network->state == TITAN_BROWSER_NET_CANCELLED
    )
    {
        TitanBrowser_CopyString(
            g_browser_status,
            sizeof(g_browser_status),
            network->state == TITAN_BROWSER_NET_CANCELLED
                ? "Transferencia cancelada"
                : network->error
        );
        g_browser.state = TITAN_BROWSER_ERROR;
        g_browser_operation = TITAN_BROWSER_OPERATION_NONE;
        TitanBrowserNet_ReleaseResult();
    }

    g_browser.title = g_browser_title;
    g_browser.url = g_browser_url;
    g_browser.status = g_browser_status;
    g_browser.can_go_back = g_browser_history_count > 0u && g_browser_history_index > 0u;
    g_browser.can_go_forward =
        g_browser_history_count > 0u &&
        g_browser_history_index + 1u < g_browser_history_count;
}

void TitanBrowser_Shutdown(void)
{
    TitanBrowserNet_Shutdown();
}

int TitanBrowser_Navigate(const char *url)
{
    return TitanBrowser_StartNavigation(url, 1);
}

int TitanBrowser_GoBack(void)
{
    if (g_browser_history_count == 0u || g_browser_history_index == 0u)
    {
        return 0;
    }
    g_browser_history_index--;
    return TitanBrowser_StartNavigation(g_browser_history[g_browser_history_index], 0);
}

int TitanBrowser_GoForward(void)
{
    if (
        g_browser_history_count == 0u ||
        g_browser_history_index + 1u >= g_browser_history_count
    )
    {
        return 0;
    }
    g_browser_history_index++;
    return TitanBrowser_StartNavigation(g_browser_history[g_browser_history_index], 0);
}

void TitanBrowser_GoHome(void)
{
    TitanBrowserNet_Cancel();
    TitanBrowser_ParseDocument(
        "titan://home",
        g_browser_home_document,
        sizeof(g_browser_home_document) - 1u
    );
    TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Pagina inicial local");
    g_browser.state = TITAN_BROWSER_HOME;
    g_browser_operation = TITAN_BROWSER_OPERATION_NONE;
    TitanBrowser_PushHistory("titan://home");
    TitanBrowser_Update();
}

void TitanBrowser_Cancel(void)
{
    TitanBrowserNet_Cancel();
}

void TitanBrowser_MoveLink(int delta)
{
    if (g_browser.link_count == 0u || delta == 0)
    {
        return;
    }
    if (delta < 0)
    {
        g_browser.selected_link = g_browser.selected_link == 0u
            ? g_browser.link_count - 1u
            : g_browser.selected_link - 1u;
    }
    else
    {
        g_browser.selected_link =
            (g_browser.selected_link + 1u) % g_browser.link_count;
    }
}

void TitanBrowser_Scroll(int delta)
{
    int next;

    next = (int)g_browser.scroll_line + delta;
    if (next < 0)
    {
        next = 0;
    }
    if (next >= (int)g_browser.line_count)
    {
        next = g_browser.line_count > 0u ? (int)g_browser.line_count - 1 : 0;
    }
    g_browser.scroll_line = (unsigned int)next;
}

TitanBrowserAction TitanBrowser_ActivateSelected(void)
{
    const TitanBrowserLink *link;
    const char *value;

    link = TitanBrowser_GetLink(g_browser.selected_link);
    if (link == 0)
    {
        return TITAN_BROWSER_ACTION_NONE;
    }

    if (strcmp(link->url, "titan://address") == 0)
    {
        return TITAN_BROWSER_ACTION_OPEN_ADDRESS;
    }
    if (strcmp(link->url, "titan://downloads") == 0)
    {
        static const char downloads_document[] =
            "<title>Downloads</title><h1>Downloads do Titan Browser</h1>"
            "<p>Arquivos concluidos ficam em mass:/TITAN/downloads.</p>"
            "<p>Use Quadrado sobre um link para iniciar um download.</p>";
        TitanBrowser_ParseDocument(
            "titan://downloads",
            downloads_document,
            sizeof(downloads_document) - 1u
        );
        TitanBrowser_CopyString(g_browser_status, sizeof(g_browser_status), "Gerenciador de downloads");
        g_browser.state = TITAN_BROWSER_READY;
        TitanBrowser_PushHistory("titan://downloads");
        return TITAN_BROWSER_ACTION_NONE;
    }
    if (TitanBrowser_EqualsInsensitiveN(link->url,"titan://repository",18u))
    {
        char decoded[TITAN_BROWSER_URL_CAPACITY];
        value=TitanBrowser_QueryValue(link->url,"url");
        if(value&&TitanBrowser_UrlDecode(value,decoded,sizeof(decoded))&&TitanBrowser_StartRepositoryUrl(decoded))
            return TITAN_BROWSER_ACTION_NONE;
        return TITAN_BROWSER_ACTION_NONE;
    }
    if (TitanBrowser_EqualsInsensitiveN(link->url,"titan://package",15u))
    {
        char package_id[TITAN_REPOSITORY_PACKAGE_ID_CAPACITY];
        unsigned int job_id=0u;
        value=TitanBrowser_QueryValue(link->url,"id");
        if(value&&TitanBrowser_UrlDecode(value,package_id,sizeof(package_id))&&
           TitanStoreService_QueuePackage(package_id,&job_id)==TITAN_STORE_OK)
        {
            TitanBrowser_CopyString(g_browser_status,sizeof(g_browser_status),"Pacote adicionado a fila Titan");
            return TITAN_BROWSER_ACTION_DOWNLOAD_STARTED;
        }
        TitanBrowser_CopyString(g_browser_status,sizeof(g_browser_status),"Pacote Titan indisponivel");
        return TITAN_BROWSER_ACTION_NONE;
    }
    if (TitanBrowser_EqualsInsensitiveN(link->url, "titan://stream", 14u))
    {
        value = TitanBrowser_QueryValue(link->url, "url");
        if (
            value != 0 &&
            TitanBrowser_UrlDecode(
                value,
                g_browser_pending_media,
                sizeof(g_browser_pending_media)
            )
        )
        {
            return TITAN_BROWSER_ACTION_PLAY_STREAM;
        }
        return TITAN_BROWSER_ACTION_NONE;
    }
    if (TitanBrowser_EqualsInsensitiveN(link->url, "titan://download", 16u))
    {
        char decoded[TITAN_BROWSER_URL_CAPACITY];

        value = TitanBrowser_QueryValue(link->url, "url");
        if (
            value != 0 &&
            TitanBrowser_UrlDecode(value, decoded, sizeof(decoded))
        )
        {
            return TitanBrowser_StartDownloadUrl(decoded);
        }
        return TITAN_BROWSER_ACTION_NONE;
    }

    TitanBrowser_Navigate(link->url);
    return TITAN_BROWSER_ACTION_NONE;
}

TitanBrowserAction TitanBrowser_DownloadSelected(void)
{
    const TitanBrowserLink *link;

    link = TitanBrowser_GetLink(g_browser.selected_link);
    if (
        link == 0 ||
        TitanBrowser_EqualsInsensitiveN(link->url, "titan://", 8u)
    )
    {
        return TITAN_BROWSER_ACTION_NONE;
    }
    return TitanBrowser_StartDownloadUrl(link->url);
}

const TitanBrowserSnapshot *TitanBrowser_GetSnapshot(void)
{
    return &g_browser;
}

const char *TitanBrowser_GetLine(unsigned int index)
{
    return index < g_browser.line_count ? g_browser_lines[index] : 0;
}

const TitanBrowserLink *TitanBrowser_GetLink(unsigned int index)
{
    return index < g_browser.link_count ? &g_browser_links[index] : 0;
}

const char *TitanBrowser_GetPendingMediaUrl(void)
{
    return g_browser_pending_media;
}

TitanBrowserPlayerResult TitanBrowser_LaunchPendingMedia(void)
{
    /* Browser POC now stays entirely inside Project Titan. The old POC handed
     * MPEG-2 streams to an external ELF; that path is intentionally disabled
     * here until the media player itself is ported to the in-system runtime. */
    return g_browser_pending_media[0] == '\0'
        ? TITAN_BROWSER_PLAYER_NO_STREAM
        : TITAN_BROWSER_PLAYER_MISSING;
}

const char *TitanBrowser_GetPlayerResultName(TitanBrowserPlayerResult result)
{
    switch (result)
    {
        case TITAN_BROWSER_PLAYER_NO_STREAM:
            return "Nenhum stream foi selecionado";
        case TITAN_BROWSER_PLAYER_MISSING:
            return "Player de video interno ainda nao integrado";
        case TITAN_BROWSER_PLAYER_INVALID_ELF:
        case TITAN_BROWSER_PLAYER_HANDOFF_FAILED:
            return "Handoff externo desativado no navegador interno";
        case TITAN_BROWSER_PLAYER_OK:
        default:
            return "OK";
    }
}

int TitanBrowser_SetBridgeBase(const char *base_url)
{
    size_t length;

    if (
        base_url == 0 ||
        !TitanBrowser_EqualsInsensitiveN(base_url, "http://", 7u)
    )
    {
        return 0;
    }
    length = strlen(base_url);
    while (length > 0u && base_url[length - 1u] == '/')
    {
        length--;
    }
    if (length == 0u || length >= sizeof(g_browser_bridge))
    {
        return 0;
    }
    memcpy(g_browser_bridge, base_url, length);
    g_browser_bridge[length] = '\0';
    return 1;
}

const char *TitanBrowser_GetBridgeBase(void)
{
    return g_browser_bridge;
}

int TitanBrowser_LoadDocumentForTest(
    const char *url,
    const char *html,
    size_t html_size
)
{
    int result;

    result = TitanBrowser_ParseDocument(url, html, html_size);
    if (result)
    {
        g_browser.state = TITAN_BROWSER_READY;
        TitanBrowser_Update();
    }
    return result;
}
